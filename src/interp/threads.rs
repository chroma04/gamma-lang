//! Threads (§19.6) in the interpreter.
//!
//! Each Gamma thread runs on an OS thread of its own, and all of them share the
//! one [`Machine`]. A global interpreter lock (the [`Gil`]) makes sure only one
//! of them evaluates at a time: a thread holds it while it runs and lets the
//! others in at regular points (calls and loop iterations) and around calls
//! that may block (joining, sleeping, reading input, network operations, calls into C). The
//! lock is handed over in arrival order, so every thread makes progress. Data
//! races therefore cannot corrupt the machine, and atomics are trivially atomic.
//!
//! The lock exists only once a program starts a thread, so single-threaded
//! programs pay nothing for it. When a thread traps, the trap is recorded, and
//! every thread stops with it the next time it takes the lock (sleepers in a futex are
//! woken), as the whole process stops in compiled code.

use super::fiber::Sendable;
use super::machine::*;
use super::value::*;
use crate::source::Span;
use crate::tir::FnId;
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;

/// Stack size of a Gamma thread's OS thread, and the part kept in reserve.
const THREAD_STACK: usize = 256 << 20;
const THREAD_MARGIN: usize = 16 << 20;

/// How many calls or loop iterations a thread runs before letting others in.
const SLICE: u32 = 2000;

/// How many calls or loop iterations pass between two looks at the clock for a deadline.
const CHECK_TIME: u32 = 1024;

/// A fair lock: tickets are served in the order they were drawn.
pub struct Gil {
    state: Mutex<(u64, u64)>, // (ticket being served, next ticket)
    cv: Condvar,
}

impl Gil {
    /// A lock held by the calling thread.
    fn held() -> Gil {
        Gil { state: Mutex::new((0, 1)), cv: Condvar::new() }
    }

    fn acquire(&self) {
        let mut s = self.state.lock().unwrap_or_else(|e| e.into_inner());
        let ticket = s.1;
        s.1 += 1;
        while s.0 != ticket {
            s = self.cv.wait(s).unwrap_or_else(|e| e.into_inner());
        }
    }

    fn release(&self) {
        let mut s = self.state.lock().unwrap_or_else(|e| e.into_inner());
        s.0 += 1;
        self.cv.notify_all();
    }
}

/// A thread's part of the machine while the thread runs without the lock.
pub(super) struct Released<'a> {
    gil: Arc<Gil>,
    saved: Option<super::fiber::ThreadState<'a>>,
}

/// The threads of a program.
#[derive(Default)]
pub struct Threads {
    pub gil: Option<Arc<Gil>>,
    handles: std::collections::HashMap<i64, JoinHandle<()>>,
    last: i64,
    /// The first trap of any thread: every thread stops with it.
    pub fatal: Option<Trap>,
    since_yield: u32,
}

impl<'a> Machine<'a> {
    /// Called at each call and loop iteration: now and then, lets the other threads run.
    #[inline(always)]
    pub fn tick(&mut self) -> R<()> {
        if self.threads.gil.is_none() && self.deadline.is_none() {
            return Ok(());
        }
        self.tick_slow()
    }

    #[inline(never)]
    fn tick_slow(&mut self) -> R<()> {
        if let Some(d) = self.deadline {
            self.since_check += 1;
            if self.since_check >= CHECK_TIME {
                self.since_check = 0;
                if std::time::Instant::now() >= d {
                    return Err(Box::new(Flow::Trap(Trap::new(super::DEADLINE_TRAP, Span::DUMMY))));
                }
            }
        }
        if self.threads.gil.is_none() {
            return Ok(());
        }
        self.threads.since_yield += 1;
        if self.threads.since_yield < SLICE {
            return Ok(());
        }
        self.threads.since_yield = 0;
        self.yield_thread()
    }

    /// Lets the other threads run, then continues; fails if one of them trapped.
    pub fn yield_thread(&mut self) -> R<()> {
        self.without_gil(|| ());
        self.check_fatal()
    }

    /// Runs `f`, which must not touch the machine, with the lock released so that other
    /// threads can run meanwhile.
    pub fn without_gil<T>(&mut self, f: impl FnOnce() -> T) -> T {
        let Some(mut held) = self.release_gil() else { return f() };
        let r = f();
        self.reacquire_gil(&mut held);
        r
    }

    /// Gives the lock up, keeping this thread's part of the machine aside; `None` if there
    /// is no lock (no thread was started). Until [`Machine::reacquire_gil`], the thread
    /// must not touch the machine.
    pub(super) fn release_gil(&mut self) -> Option<Released<'a>> {
        let gil = self.threads.gil.clone()?;
        let saved = self.save_thread();
        // The other threads reach the machine through this pointer: make sure nothing
        // about it is assumed unchanged across the wait.
        std::hint::black_box(self as *mut Self);
        gil.release();
        Some(Released { gil, saved: Some(saved) })
    }

    /// Gives the lock up again after [`Machine::reacquire_gil`] (a callback from C is over).
    #[cfg(not(target_family = "wasm"))]
    pub(super) fn rerelease_gil(&mut self, held: &mut Released<'a>) {
        held.saved = Some(self.save_thread());
        std::hint::black_box(self as *mut Self);
        held.gil.release();
    }

    /// Takes the lock back and puts this thread's part of the machine back.
    pub(super) fn reacquire_gil(&mut self, held: &mut Released<'a>) {
        held.gil.acquire();
        std::hint::black_box(self as *mut Self);
        if let Some(saved) = held.saved.take() {
            self.restore_thread(saved);
        }
    }

    pub(super) fn check_fatal(&self) -> R<()> {
        match &self.threads.fatal {
            Some(t) => Err(Box::new(Flow::Trap(t.clone()))),
            None => Ok(()),
        }
    }

    /// `@thread_spawn(f, arg)`: a new thread calling the function at code address `f` with
    /// `arg`, in an empty handler context (§19.6). Returns its handle.
    pub fn spawn_thread(&mut self, span: Span, f: u64, arg: Value) -> R<i64> {
        let Some(fid) = self.fn_from_addr(f) else {
            return self.trap(span, format!("a thread started from an invalid function pointer {:#x}", f));
        };
        let gil = self.threads.gil.get_or_insert_with(|| Arc::new(Gil::held())).clone();
        self.threads.last += 1;
        let id = self.threads.last;
        let m = self as *mut Machine<'a>;
        let payload = Sendable((m, gil, fid, arg, span));
        let body = move || {
            let (m, gil, fid, arg, span): (*mut Machine, Arc<Gil>, FnId, Value, Span) = payload.into_inner();
            gil.acquire();
            // SAFETY: the lock gives this thread exclusive use of the machine.
            let m = unsafe { &mut *m };
            m.temps = vec![];
            m.depth = 0;
            m.stack_floor = super::stack_floor(THREAD_STACK, THREAD_MARGIN);
            m.fib.cur = 0;
            m.fiber_stack = u64::MAX;
            m.thread_id = id as u64;
            let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                m.call_function(span, fid, vec![arg], None, Ctx::empty(), None, false)
            }));
            let trap = match r {
                Ok(Ok(_)) => None,
                Ok(Err(flow)) => match *flow {
                    Flow::Trap(t) => Some(t),
                    _ => Some(Trap::new("internal error: control flow left a thread", span)),
                },
                Err(p) => Some(super::panic_trap(p)),
            };
            if let Some(t) = trap
                && m.threads.fatal.is_none()
            {
                m.threads.fatal = Some(t);
                // Threads asleep in a futex would never notice: wake them.
                super::sys::wake_futex_waiters();
            }
            m.kill_temps(0);
            std::hint::black_box(m as *mut Machine);
            gil.release();
        };
        // SAFETY: the machine outlives the thread: the program's main thread never gives
        // the lock back once `main` has returned, so a thread still running then stops
        // at its next attempt to take it, before touching the machine again.
        let h = unsafe { std::thread::Builder::new().stack_size(THREAD_STACK).spawn_unchecked(body) };
        match h {
            Ok(h) => {
                self.threads.handles.insert(id, h);
                Ok(id)
            }
            Err(e) => Ok(super::sys::error_code(&e)),
        }
    }

    /// `@thread_join(h)`: waits for the thread to finish.
    pub fn join_thread(&mut self, h: i64) -> R<i64> {
        let Some(handle) = self.threads.handles.remove(&h) else {
            return Ok(super::sys::error_code(&std::io::Error::from_raw_os_error(3))); // ESRCH
        };
        self.without_gil(|| {
            let _ = handle.join();
        });
        self.check_fatal()?;
        Ok(0)
    }

    /// `@thread_detach(h)`: the thread is never joined. Dropping its handle lets the OS
    /// thread release its resources when it ends.
    pub fn detach_thread(&mut self, h: i64) -> i64 {
        match self.threads.handles.remove(&h) {
            Some(_) => 0,
            None => super::sys::error_code(&std::io::Error::from_raw_os_error(3)), // ESRCH
        }
    }
}
