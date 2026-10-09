//! Fibers and continuations (§14).
//!
//! The evaluator is recursive, so a fiber suspended in the middle of an
//! expression needs a Rust stack of its own: each fiber runs on an OS thread.
//! The threads share the one [`Machine`] through a raw pointer and hand control
//! to each other over channels, so exactly one of them runs at any time while
//! the others block in `recv`. What belongs to a thread rather than to the
//! program (temporaries, call depth, stack limit, the current fiber) is saved
//! before each handoff and restored after it.
//!
//! A `with … on` registers itself under its frame and label, runs its body on a
//! new fiber, and then acts as the *driver*: it waits for messages from the
//! fibers. A fiber that performs a `ctl` operation sends `Perform` and waits;
//! the driver runs the clause, which ends with `resume` (a [`Flow::Resume`]
//! naming the fiber to switch to), `discard` + an exit, or an exit. A fiber
//! whose base frame returns or exits sends `Done`.

use super::machine::*;
use super::value::*;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use std::cell::RefCell;
use std::collections::HashMap;
use std::rc::Rc;
use std::sync::mpsc::{Receiver, Sender, channel};
use std::thread::JoinHandle;

/// Stack size of a fiber's thread, and the part kept in reserve for overflow checks.
const FIBER_STACK: usize = 64 << 20;
const FIBER_MARGIN: usize = 8 << 20;
/// Bytes at the bottom of a Gamma fiber stack kept for reporting an overflow (as in
/// compiled code).
const STACK_RESERVE: u64 = 8192;

/// Values crossing between fiber threads. Only one thread runs at a time and
/// every handoff goes through a channel, so the `Rc`s inside are never used
/// concurrently.
pub struct Sendable<T>(T);
// SAFETY: see the type's documentation.
unsafe impl<T> Send for Sendable<T> {}

impl<T> Sendable<T> {
    /// Unwraps the value (a method, so that closures capture the whole wrapper).
    fn into_inner(self) -> T {
        self.0
    }
}

/// What a suspended fiber is told when control returns to it.
pub enum ToFiber {
    Resume(Value),
    /// Unwind to the fiber's base, running defers (§14.5).
    Cancel,
    /// Stop without running any more Gamma code (the program is trapping).
    Abort,
}

/// What a driver is told when control returns to it.
pub enum ToDriver {
    Perform { fiber: u64, node: Rc<CtxNode>, op: u32, args: Vec<Value>, span: Span },
    Done { fiber: u64, result: R },
}

type WithKey = (u64, LabelId);

thread_local! {
    /// The channel on which the fiber running on this thread is resumed.
    static FIBER_RX: RefCell<Option<Rc<Receiver<Sendable<ToFiber>>>>> = const { RefCell::new(None) };
}

#[derive(Default)]
pub struct Fibers {
    /// The fiber running now (0: the evaluation thread).
    pub cur: u64,
    last_id: u64,
    withs: HashMap<WithKey, WithRec>,
    fibers: HashMap<u64, FiberRec>,
}

struct WithRec {
    tx: Sender<Sendable<ToDriver>>,
    rx: Rc<Receiver<Sendable<ToDriver>>>,
    /// Fibers suspended on this `with`'s operations, i.e. its unconsumed continuations,
    /// oldest first.
    suspended: Vec<u64>,
    /// The context of the body, for fibers made by `@fiber`.
    body_cx: Ctx,
    frame: Rc<Frame>,
    /// The fibers this `with` created.
    owned: Vec<u64>,
}

enum Start {
    Body { fr: *const Rc<Frame>, cx: *const Ctx, body: *const Expr },
    Call { f: Value, fty: Ty, arg: Value, fr: Rc<Frame>, cx: Ctx, span: Span },
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum State {
    Running,
    /// Suspended on an operation of the `with` with this key.
    Suspended(WithKey),
    Done,
}

struct FiberRec {
    owner: WithKey,
    /// Incremented at each suspension, so a continuation names one suspension.
    epoch: u64,
    state: State,
    start: Option<Start>,
    tx: Option<Sender<Sendable<ToFiber>>>,
    thread: Option<JoinHandle<()>>,
    /// The size of its stack, less the part reserved for reporting an overflow.
    stack: u64,
}

/// The per-thread part of the machine, saved across a handoff.
struct ThreadState {
    temps: Vec<u64>,
    fiber_stack: u64,
    depth: u32,
    stack_floor: usize,
    cur: u64,
}

fn aborted() -> Box<Flow> {
    trap_flow(Span::DUMMY, "internal error: fiber aborted")
}

impl<'a> Machine<'a> {
    fn save_thread(&mut self) -> ThreadState {
        ThreadState {
            temps: std::mem::take(&mut self.temps),
            fiber_stack: self.fiber_stack,
            depth: self.depth,
            stack_floor: self.stack_floor,
            cur: self.fib.cur,
        }
    }

    fn restore_thread(&mut self, s: ThreadState) {
        self.temps = s.temps;
        self.fiber_stack = s.fiber_stack;
        self.depth = s.depth;
        self.stack_floor = s.stack_floor;
        self.fib.cur = s.cur;
    }

    /// Sends `msg` (which lets another thread run) and blocks until a message arrives on `rx`.
    fn handoff<M, T>(&mut self, tx: &Sender<Sendable<M>>, msg: M, rx: &Receiver<Sendable<T>>) -> Option<T> {
        let saved = self.save_thread();
        // The other threads reach the machine through this pointer: make sure nothing
        // about it is assumed unchanged across the wait.
        std::hint::black_box(self as *mut Self);
        let sent = tx.send(Sendable(msg)).is_ok();
        let got = if sent { rx.recv().ok().map(|m| m.0) } else { None };
        std::hint::black_box(self as *mut Self);
        self.restore_thread(saved);
        got
    }

    /// Makes the bytes of a continuation: the fiber and its suspension number.
    fn cont_bytes(&self, fiber: u64, epoch: u64) -> Value {
        let mut b = self.usize_bytes(fiber).to_vec();
        b.extend_from_slice(&self.usize_bytes(epoch));
        Value::Bytes(b.into())
    }

    fn new_fiber(&mut self, owner: WithKey, start: Start, state: State, stack: &[u8]) -> u64 {
        let stack = self.slice_parts(stack).1.saturating_sub(STACK_RESERVE);
        self.fib.last_id += 1;
        let id = self.fib.last_id;
        self.fib
            .fibers
            .insert(id, FiberRec { owner, epoch: 1, state, start: Some(start), tx: None, thread: None, stack });
        if let Some(w) = self.fib.withs.get_mut(&owner) {
            w.owned.push(id);
        }
        id
    }

    fn check_stack(&mut self, span: Span, stack: &[u8]) -> R<()> {
        let (_, len) = self.slice_parts(stack);
        if !self.fast() && len < MIN_STACK {
            return Err(trap_flow(
                span,
                format!("a fiber stack needs at least {} bytes (`@min_stack`), but this one has {}", MIN_STACK, len),
            ));
        }
        Ok(())
    }

    /// `with h on stack { body }` (§14.1), after the handlers are installed in `cx`.
    pub fn eval_with_on(&mut self, a: Act, span: Span, label: LabelId, cx: Ctx, stack: &[u8], body: &Expr) -> R {
        self.check_stack(span, stack)?;
        let key = (a.fr.id, label);
        let (tx, rx) = channel();
        let rec =
            WithRec { tx, rx: Rc::new(rx), suspended: vec![], body_cx: cx.clone(), frame: a.fr.clone(), owned: vec![] };
        if self.fib.withs.insert(key, rec).is_some() {
            return self.trap(span, "internal error: a `with … on` is active twice");
        }
        let start = Start::Body { fr: a.fr as *const Rc<Frame>, cx: &cx as *const Ctx, body: body as *const Expr };
        let first = self.new_fiber(key, start, State::Running, stack);
        let r = self.drive(span, key, first);
        if let Some(w) = self.fib.withs.remove(&key) {
            for f in w.owned {
                self.fib.fibers.remove(&f);
            }
        }
        match r {
            Err(f) => match *f {
                Flow::Break { frame, label: l, value } if frame == a.fr.id && l == label => Ok(value),
                other => Err(Box::new(other)),
            },
            ok => ok,
        }
    }

    /// The driver loop of a `with … on` whose body runs on fiber `first`.
    fn drive(&mut self, span: Span, key: WithKey, first: u64) -> R {
        let mut msg = self.switch_to(span, key, first, ToFiber::Resume(Value::unit()))?;
        loop {
            match msg {
                Some(ToDriver::Perform { fiber, node, op, mut args, span: ps }) => {
                    let epoch = self.fib.fibers.get(&fiber).map_or(0, |f| f.epoch);
                    args.push(self.cont_bytes(fiber, epoch));
                    match self.call_clause(ps, &node, op, args) {
                        Err(f) if matches!(*f, Flow::Resume { .. }) => {
                            let Flow::Resume { fiber, value } = *f else { unreachable!() };
                            msg = self.switch_to(span, key, fiber, ToFiber::Resume(value))?;
                        }
                        Ok(_) => {
                            let r = self.trap(ps, "internal error: a `ctl` clause finished normally");
                            return self.complete(span, key, r, false);
                        }
                        r => return self.complete(span, key, r, false),
                    }
                }
                Some(ToDriver::Done { fiber, result }) => {
                    self.join(fiber);
                    let result = match result {
                        Ok(_) if fiber != first => self.trap(span, "a fiber's function returned"),
                        r => r,
                    };
                    return self.complete(span, key, result, true);
                }
                None => return self.trap(span, "internal error: a fiber thread disappeared"),
            }
        }
    }

    /// Finishes a `with … on` with `outcome` (§14.4). `from_fiber`: a fiber finished, rather
    /// than a clause exiting.
    fn complete(&mut self, span: Span, key: WithKey, outcome: R, from_fiber: bool) -> R {
        if let Err(f) = &outcome
            && matches!(**f, Flow::Trap(_) | Flow::Exit(_))
        {
            self.abort_all(span, key);
            return outcome;
        }
        if from_fiber && outcome.is_err() {
            // An exit crossing the `with`: cancel every other suspended fiber, newest first.
            while let Some(id) = self.fib.withs.get_mut(&key).and_then(|w| w.suspended.pop()) {
                if let Err(f) = self.cancel(span, key, id) {
                    self.abort_all(span, key);
                    return Err(f);
                }
            }
            return outcome;
        }
        let n = self.fib.withs.get(&key).map_or(0, |w| w.suspended.len());
        if n > 0 {
            self.abort_all(span, key);
            let s = if n == 1 { "" } else { "s" };
            return self.trap(
                span,
                format!("with completed while {} fiber{} {} suspended", n, s, if n == 1 { "is" } else { "are" }),
            );
        }
        outcome
    }

    /// Stops every fiber suspended on `key` without running their defers.
    fn abort_all(&mut self, span: Span, key: WithKey) {
        while let Some(id) = self.fib.withs.get_mut(&key).and_then(|w| w.suspended.pop()) {
            if let Ok(Some(ToDriver::Done { fiber, .. })) = self.switch_to(span, key, id, ToFiber::Abort) {
                self.join(fiber);
            }
        }
    }

    /// Cancels suspended fiber `id` (already taken off its `with`'s list) and waits until
    /// it has unwound.
    fn cancel(&mut self, span: Span, key: WithKey, id: u64) -> R<()> {
        match self.switch_to(span, key, id, ToFiber::Cancel)? {
            None => Ok(()),
            Some(ToDriver::Done { fiber, result }) => {
                self.join(fiber);
                match result {
                    Err(f) if matches!(*f, Flow::Cancel) => Ok(()),
                    Err(f) => Err(f),
                    Ok(_) => self.trap(span, "internal error: a discarded fiber finished normally"),
                }
            }
            Some(ToDriver::Perform { span: ps, .. }) => {
                self.trap(ps, "a `ctl` operation was performed while a fiber was being discarded")
            }
        }
    }

    /// Passes control to fiber `fiber` with `msg` and waits for the next message to the
    /// driver of `key`. Returns `None` if the fiber had not started and is not resumed.
    fn switch_to(&mut self, span: Span, key: WithKey, fiber: u64, msg: ToFiber) -> R<Option<ToDriver>> {
        let Some(rx) = self.fib.withs.get(&key).map(|w| w.rx.clone()) else {
            return self.trap(span, "internal error: no driver for this fiber");
        };
        let resume = matches!(msg, ToFiber::Resume(_));
        let Some(rec) = self.fib.fibers.get_mut(&fiber) else {
            return self.trap(span, "internal error: unknown fiber");
        };
        rec.state = if resume { State::Running } else { State::Done };
        if rec.tx.is_none() {
            if !resume {
                rec.start = None;
                return Ok(None);
            }
            self.spawn(span, fiber)?;
        }
        let tx = self.fib.fibers[&fiber].tx.clone().unwrap();
        Ok(self.handoff(&tx, msg, &rx))
    }

    fn join(&mut self, fiber: u64) {
        if let Some(rec) = self.fib.fibers.get_mut(&fiber) {
            rec.state = State::Done;
            if let Some(h) = rec.thread.take() {
                let _ = h.join();
            }
        }
    }

    /// Starts the thread of fiber `id`; it waits for its first message.
    fn spawn(&mut self, span: Span, id: u64) -> R<()> {
        let rec = self.fib.fibers.get_mut(&id).unwrap();
        let start = rec.start.take();
        let owner = rec.owner;
        let stack = rec.stack;
        let (tx, rx) = channel::<Sendable<ToFiber>>();
        let Some(driver) = self.fib.withs.get(&owner).map(|w| w.tx.clone()) else {
            return self.trap(span, "internal error: a fiber outlived its `with`");
        };
        let m = self as *mut Machine<'a>;
        let payload = Sendable((m, rx, start, driver, stack));
        let body = move || {
            let (m, rx, start, driver, stack) = payload.into_inner();
            let rx = Rc::new(rx);
            FIBER_RX.with(|c| *c.borrow_mut() = Some(rx.clone()));
            let first = rx.recv().map(|m| m.0);
            let result = match first {
                Ok(ToFiber::Resume(_)) => {
                    // SAFETY: the thread that sent the message is now blocked until this one
                    // hands control back (see the module documentation).
                    let m = unsafe { &mut *m };
                    m.temps = vec![];
                    m.depth = 0;
                    m.stack_floor = super::stack_floor(FIBER_STACK, FIBER_MARGIN);
                    m.fib.cur = id;
                    m.fiber_stack = stack;
                    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| m.run_start(start)));
                    let r = r.unwrap_or_else(|p| Err(Box::new(Flow::Trap(super::panic_trap(p)))));
                    m.kill_temps(0);
                    std::hint::black_box(m as *mut Machine);
                    r
                }
                Ok(ToFiber::Cancel) => Err(Box::new(Flow::Cancel)),
                _ => Err(aborted()),
            };
            FIBER_RX.with(|c| c.borrow_mut().take());
            drop(rx);
            let _ = driver.send(Sendable(ToDriver::Done { fiber: id, result }));
        };
        // SAFETY: the driver joins this thread before the `with` (and with it every
        // borrowed value) goes away.
        let h = unsafe { std::thread::Builder::new().stack_size(FIBER_STACK).spawn_unchecked(body) };
        match h {
            Ok(h) => {
                let rec = self.fib.fibers.get_mut(&id).unwrap();
                rec.tx = Some(tx);
                rec.thread = Some(h);
                Ok(())
            }
            Err(e) => self.trap(span, format!("cannot start a fiber: {}", e)),
        }
    }

    fn run_start(&mut self, start: Option<Start>) -> R {
        match start {
            // SAFETY: the pointers refer to the driver's frame, which outlives the fiber.
            Some(Start::Body { fr, cx, body }) => unsafe { self.eval(Act { fr: &*fr, cx: &*cx }, &*body) },
            Some(Start::Call { f, fty, arg, fr, cx, span }) => {
                self.call_value(Act { fr: &fr, cx: &cx }, span, f, fty, vec![arg])
            }
            None => Err(trap_flow(Span::DUMMY, "internal error: a fiber started twice")),
        }
    }

    /// Performs `ctl` operation `op` from the running fiber: suspends it until resumed.
    pub fn ctl_perform(&mut self, span: Span, node: Rc<CtxNode>, op: u32, args: Vec<Value>) -> R {
        let key = (node.with_frame, node.with_label);
        let id = self.fib.cur;
        let Some(tx) = self.fib.withs.get(&key).map(|w| w.tx.clone()) else {
            return self.trap(span, "internal error: a `ctl` operation of a `with` without `on`");
        };
        let Some(rx) = FIBER_RX.with(|c| c.borrow().clone()) else {
            return self.trap(span, "internal error: a `ctl` operation performed outside a fiber");
        };
        let Some(rec) = self.fib.fibers.get_mut(&id) else {
            return self.trap(span, "internal error: unknown fiber");
        };
        rec.epoch += 1;
        rec.state = State::Suspended(key);
        self.fib.withs.get_mut(&key).unwrap().suspended.push(id);
        match self.handoff(&tx, ToDriver::Perform { fiber: id, node, op, args, span }, &rx) {
            Some(ToFiber::Resume(v)) => Ok(v),
            Some(ToFiber::Cancel) => Err(Box::new(Flow::Cancel)),
            _ => Err(aborted()),
        }
    }

    /// Consumes continuation `k` in a clause running in frame `fr` (§14.2): checks it
    /// and returns its fiber and the `with` that drives it.
    fn consume(&mut self, span: Span, fr: &Frame, k: &[u8]) -> R<(u64, WithKey)> {
        let ps = self.ptr_size();
        if k.len() < 2 * ps {
            return self.trap(span, "internal error: a malformed continuation");
        }
        let (id, epoch) = (self.ptr_of(&k[..ps]), self.ptr_of(&k[ps..2 * ps]));
        let Some(clause_with) = fr.installing else {
            return self.trap(span, "internal error: a continuation consumed outside a clause");
        };
        let Some(rec) = self.fib.fibers.get(&id) else {
            return self.trap(span, "this continuation is no longer valid: its `with` has completed (§14.2)");
        };
        let State::Suspended(w) = rec.state else {
            return self.trap(span, "this continuation was already consumed (§14.2)");
        };
        if rec.epoch != epoch {
            return self.trap(span, "this continuation was already consumed (§14.2)");
        }
        if w != clause_with {
            return self
                .trap(span, "a continuation can be consumed only by a clause of the `with` that produced it (§14.2)");
        }
        if let Some(wr) = self.fib.withs.get_mut(&w) {
            wr.suspended.retain(|f| *f != id);
        }
        self.fib.fibers.get_mut(&id).unwrap().state = State::Running;
        Ok((id, w))
    }

    /// `resume k(v)` (§14.3).
    pub fn eval_resume(&mut self, a: Act, span: Span, cont: &Expr, value: &Expr) -> R {
        let k = self.eval_bytes(a, cont)?;
        let v = self.eval(a, value)?;
        let (fiber, _) = self.consume(span, a.fr, &k)?;
        Err(Box::new(Flow::Resume { fiber, value: v }))
    }

    /// `discard k` (§14.5).
    pub fn eval_discard(&mut self, a: Act, span: Span, cont: &Expr) -> R {
        let k = self.eval_bytes(a, cont)?;
        let (fiber, w) = self.consume(span, a.fr, &k)?;
        self.cancel(span, w, fiber)?;
        Ok(Value::unit())
    }

    /// `@fiber(f, arg, stack)` (§14.6).
    pub fn eval_fiber(&mut self, a: Act, span: Span, args: &[Expr]) -> R {
        let f = self.eval(a, &args[0])?;
        let arg = self.eval(a, &args[1])?;
        let stack = self.eval_bytes(a, &args[2])?;
        self.check_stack(span, &stack)?;
        let Some(key) = a.fr.installing else {
            return self.trap(span, "internal error: `@fiber` outside a clause");
        };
        let Some(w) = self.fib.withs.get(&key) else {
            return self.trap(span, "internal error: `@fiber` in a clause of a `with` without `on`");
        };
        let start = Start::Call { f, fty: args[0].ty, arg, fr: w.frame.clone(), cx: w.body_cx.clone(), span };
        let id = self.new_fiber(key, start, State::Suspended(key), &stack);
        self.fib.withs.get_mut(&key).unwrap().suspended.push(id);
        Ok(self.cont_bytes(id, 1))
    }
}
