//! The interpreter: a bytecode VM ([`vm`]) that compiles each function of the typed
//! IR to register code on first call, and the machine it runs on.
//!
//! It serves both compile-time evaluation ([`eval_const`], spec §17) and
//! running programs ([`run_main`]).
//!
//! # Design
//!
//! - **Memory** ([`memory`]): a flat virtual address space. Every string
//!   literal, constant, global, temporary whose address is taken and local that
//!   needs an address (small locals that never do stay in registers or frame
//!   slots) lives in its own allocation at a fresh, never reused address, so
//!   out-of-bounds,
//!   use-after-return, null and misaligned accesses and writes to read-only
//!   data are detected exactly (spec §18.4). Values are plain bytes in target
//!   layout; pointers are addresses.
//! - **Values** ([`value`]): first-class data is [`value::Value::Bytes`];
//!   second-class function and handler values are structured so they can
//!   capture frames and handler contexts.
//! - **Control flow**: within a function, `break`, `continue` and `return` are
//!   jumps. What crosses functions (effect exits, a closure's `return` from its
//!   parent, traps) is a `Flow` returned as `Err` from the call; each activation
//!   runs the `defer`/`errdefer` statements it passes through (spec §8.8), and
//!   loops, `with` and function boundaries consume the flows aimed at them.
//! - **Effects**: the handler context is a persistent linked list captured by
//!   closures and handler literals (spec §11.6, §12.7).

mod builtins;
mod bytes;
mod calls;
mod consts;
mod coverage;
pub mod display;
mod fast;
#[cfg(not(target_family = "wasm"))]
mod ffi;
#[cfg(target_family = "wasm")]
#[path = "ffi_wasm.rs"]
mod ffi;
mod fiber;
mod fxmap;
mod machine;
pub mod memory;
pub mod num;
mod ops;
mod proc;
mod sys;
mod threads;
mod value;
mod vector;
mod vm;

#[cfg(test)]
mod testkit;
#[cfg(test)]
mod tests;
#[cfg(test)]
mod tests_flow;
#[cfg(test)]
mod tests_mem;
#[cfg(test)]
mod tests_run;

use crate::layout::Target;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use machine::Machine;
use std::cell::RefCell;
use std::io::Write;
use value::*;

pub use coverage::{coverage_counts, coverage_init};
pub use ffi::{add_library_dirs, check_program as foreign_supported, load_library, set_owns_process};
pub use value::Trap;

#[derive(Clone, Debug)]
pub struct RunConfig {
    pub target: Target,
    pub mode: Mode,
    pub step_budget: Option<u64>,
}

/// Build mode (spec §1.4): checked operations trap in `Debug`, `Checked` and
/// `Safe` and are skipped (giving unspecified values) in `Fast`. Integer division
/// by zero and `MIN / -1` always trap, and so do bounds and memory checks. The
/// interpreter runs `Safe` exactly as `Checked`: it checks every memory access in
/// every mode, which is what Safe builds add to Checked ones natively (§18.7).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Debug,
    Checked,
    Safe,
    Fast,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Outcome {
    Exit(u8),
    Trap(Trap),
}

/// Stack size of the evaluation thread.
#[cfg(not(target_family = "wasm"))]
const STACK_SIZE: usize = 1 << 30;
/// Stack kept in reserve when checking for overflow.
#[cfg(not(target_family = "wasm"))]
const STACK_MARGIN: usize = 16 << 20;
/// WebAssembly hosts have no threads: evaluation runs on the caller's stack, which the
/// embedder links with at least this much room (see `web/playground/`).
#[cfg(target_family = "wasm")]
const STACK_SIZE: usize = 24 << 20;
#[cfg(target_family = "wasm")]
const STACK_MARGIN: usize = 2 << 20;

#[cfg(not(target_family = "wasm"))]
struct AssertSend<T>(T);
// SAFETY: the wrapped references are handed to the evaluation thread while the
// spawning thread blocks in `thread::scope`; they are never used concurrently.
// The interpreter thread is the only one touching the `Rc`s inside the program.
#[cfg(not(target_family = "wasm"))]
unsafe impl<T> Send for AssertSend<T> {}

#[cfg(not(target_family = "wasm"))]
impl<T> AssertSend<T> {
    fn into_inner(self) -> T {
        self.0
    }
}

pub(super) fn panic_trap(p: Box<dyn std::any::Any + Send>) -> Trap {
    let msg = p
        .downcast_ref::<String>()
        .cloned()
        .or_else(|| p.downcast_ref::<&str>().map(|s| s.to_string()))
        .unwrap_or_else(|| "unknown panic".into());
    Trap::new(format!("internal error: interpreter panicked: {}", msg), Span::DUMMY)
}

/// The lowest stack address the evaluator may use on the current thread, whose
/// stack is `size` bytes, keeping `margin` bytes in reserve.
pub(super) fn stack_floor(size: usize, margin: usize) -> usize {
    let marker = 0u8;
    let top = &marker as *const u8 as usize;
    top.saturating_sub(size - margin)
}

/// Runs `prog.main`: installs default handlers for the ambient effects (`Out` to
/// `stdout`, `Err` to `stderr`, `Env` with `args`), calls `main`, and returns
/// the exit status (0, `main`'s `u8` result, or 1 if `main` returns an error
/// value, which is first written to `stderr` as `error: <value>`).
pub fn run_main(
    prog: &Program,
    cfg: &RunConfig,
    args: &[String],
    stdout: &mut dyn Write,
    stderr: &mut dyn Write,
) -> Outcome {
    let Some(main) = prog.main else {
        return Outcome::Trap(Trap::new("the program has no `main` function", Span::DUMMY));
    };
    run_entry(prog, cfg, main, args, stdout, stderr)
}

/// Runs the parameterless function `entry` as [`run_main`] runs `main` (used
/// for the entry of each test, §3.8).
pub fn run_entry(
    prog: &Program,
    cfg: &RunConfig,
    entry: FnId,
    args: &[String],
    stdout: &mut dyn Write,
    stderr: &mut dyn Write,
) -> Outcome {
    #[cfg(target_family = "wasm")]
    return run_main_inner(prog, cfg, entry, args, stdout, stderr);
    #[cfg(not(target_family = "wasm"))]
    {
        let payload = AssertSend((prog, cfg, entry, args, stdout, stderr));
        std::thread::scope(|s| {
            let h = std::thread::Builder::new()
                .stack_size(STACK_SIZE)
                .spawn_scoped(s, move || {
                    let (prog, cfg, entry, args, stdout, stderr) = payload.into_inner();
                    run_main_inner(prog, cfg, entry, args, stdout, stderr)
                })
                .expect("failed to spawn the evaluation thread");
            match h.join() {
                Ok(o) => o,
                Err(p) => Outcome::Trap(panic_trap(p)),
            }
        })
    }
}

fn run_main_inner(
    prog: &Program,
    cfg: &RunConfig,
    main: FnId,
    args: &[String],
    stdout: &mut dyn Write,
    stderr: &mut dyn Write,
) -> Outcome {
    let lookup = |id: AdtId| prog.adts.get(&id);
    let out_failure = RefCell::new(None);
    let err_failure = RefCell::new(None);
    let mut tracked_out = Tracked { inner: stdout, failure: &out_failure };
    let mut tracked_err = Tracked { inner: stderr, failure: &err_failure };
    let mut m = Machine::new(prog, cfg, &lookup, stack_floor(STACK_SIZE, STACK_MARGIN));
    m.out = Some(&mut tracked_out);
    m.errw = Some(&mut tracked_err);
    m.args = args.to_vec();
    let mut cx = Ctx::empty();
    let mut install = |e: Option<EffectId>, k: NativeKind| {
        if let Some(id) = e {
            cx = cx.push(EffectRef { effect: id, args: Box::new([]) }, Installed::Native(k), 0, LabelId(0));
        }
    };
    install(prog.ambient.out, NativeKind::Out);
    install(prog.ambient.err, NativeKind::Err);
    install(prog.ambient.env, NativeKind::Env);
    let r = m.call_function(Span::DUMMY, main, vec![], None, cx, None, false);
    let outcome = match r {
        Ok(v) => m.exit_status(main, v),
        Err(f) => match *f {
            Flow::Exit(c) => Outcome::Exit(c),
            Flow::Trap(t) => Outcome::Trap(t),
            _ => Outcome::Trap(Trap::new("internal error: control flow escaped `main`", Span::DUMMY)),
        },
    };
    #[cfg(feature = "vm-profile")]
    vm::profile_report();
    if let Some(w) = m.out.as_mut() {
        let _ = w.flush();
    }
    // A program whose output was lost does not end successfully.
    let Outcome::Exit(mut code) = outcome else { return outcome };
    if let Some(e) = out_failure.borrow().as_ref() {
        if let Some(w) = m.errw.as_mut() {
            let _ = writeln!(w, "gamma: cannot write the standard output: {e}");
            let _ = w.flush();
        }
        code = code.max(1);
    }
    if err_failure.borrow().is_some() {
        code = code.max(1);
    }
    m.exit_process(code);
    Outcome::Exit(code)
}

/// A writer that remembers its first failure other than a closed pipe (whose reader has
/// gone away), so that a program that lost output can end with an error status.
struct Tracked<'w> {
    inner: &'w mut dyn Write,
    failure: &'w RefCell<Option<String>>,
}

impl Tracked<'_> {
    fn note<T>(&self, r: std::io::Result<T>) -> std::io::Result<T> {
        if let Err(e) = &r
            && e.kind() != std::io::ErrorKind::BrokenPipe
        {
            let mut slot = self.failure.borrow_mut();
            if slot.is_none() {
                // The system's message, without Rust's "(os error N)" suffix.
                let text = e.to_string();
                *slot = Some(text.split(" (os error ").next().unwrap_or_default().to_string());
            }
        }
        r
    }
}

impl Write for Tracked<'_> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let r = self.inner.write(buf);
        self.note(r)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        let r = self.inner.flush();
        self.note(r)
    }
}

impl Machine<'_> {
    fn exit_status(&mut self, main: FnId, v: Value) -> Outcome {
        let ret = self.prog.fns[main.index()].ret;
        let Value::Bytes(b) = v else { return Outcome::Exit(0) };
        let int_status = |m: &Machine, ty: Ty, b: &[u8]| -> u8 {
            match m.ti(ty).int {
                Some(_) => {
                    let v = m.int_of(ty, b);
                    num::wrap(&v, false, 8).to_u64().unwrap_or(0) as u8
                }
                None => 0,
            }
        };
        match ret.kind() {
            TyKind::Result { err, ok } => {
                if self.res_is_err(ret, &b) {
                    let payload = self.res_payload(ret, &b);
                    let mut out = b"error: ".to_vec();
                    // A union's value is named with its type: `E.NotFound` (§3.7).
                    if self.union_def(*err).is_some() {
                        out.extend_from_slice(err.to_string().as_bytes());
                    }
                    if self.format_value(&mut out, *err, &payload, true).is_err() {
                        out.extend_from_slice(b"<unprintable error>");
                    }
                    out.push(b'\n');
                    if let Some(w) = self.errw.as_mut() {
                        let _ = w.write_all(&out);
                    }
                    Outcome::Exit(1)
                } else {
                    let payload = self.res_payload(ret, &b);
                    Outcome::Exit(int_status(self, *ok, &payload))
                }
            }
            _ => Outcome::Exit(int_status(self, ret, &b)),
        }
    }
}

/// Evaluates a closed expression at compile time (spec §17): every check is on,
/// `cfg.step_budget` is enforced, and pointers into static data come back as
/// relocations in the [`ConstValue`]. Calls to functions and uses of
/// [`ExprKind::Const`] are supported; constants are evaluated once per call.
pub fn eval_const(prog: &Program, cfg: &RunConfig, expr: &Expr) -> Result<ConstValue, Trap> {
    #[cfg(target_family = "wasm")]
    return eval_const_inner(prog, cfg, expr);
    #[cfg(not(target_family = "wasm"))]
    {
        let payload = AssertSend((prog, cfg, expr));
        std::thread::scope(|s| {
            let h = std::thread::Builder::new()
                .stack_size(STACK_SIZE)
                .spawn_scoped(s, move || {
                    let (prog, cfg, expr) = payload.into_inner();
                    AssertSend(eval_const_inner(prog, cfg, expr))
                })
                .expect("failed to spawn the evaluation thread");
            match h.join() {
                Ok(o) => o.into_inner(),
                Err(p) => Err(panic_trap(p)),
            }
        })
    }
}

fn eval_const_inner<'a>(prog: &'a Program, cfg: &RunConfig, expr: &'a Expr) -> Result<ConstValue, Trap> {
    let lookup = |id: AdtId| prog.adts.get(&id);
    let mut m = Machine::new(prog, cfg, &lookup, stack_floor(STACK_SIZE, STACK_MARGIN));
    m.const_mode = true;
    let r = m.eval_closed(expr).and_then(|b| m.to_const_value(expr.span, expr.ty, &b));
    match r {
        Ok(v) => Ok(v),
        Err(f) => match *f {
            Flow::Trap(t) => Err(t),
            Flow::Exit(_) => Err(Trap::new("`Env.exit` at compile time", expr.span)),
            _ => Err(Trap::new("internal error: control flow escaped a constant", expr.span)),
        },
    }
}
