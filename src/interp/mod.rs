//! The interpreter: a tree-walking evaluator over the typed IR, and a bytecode
//! VM ([`vm`]) that compiles functions to register code on first call and falls
//! back to the tree-walker for what it does not compile.
//!
//! It serves both compile-time evaluation ([`eval_const`], spec §17) and
//! running programs ([`run_main`]).
//!
//! # Design
//!
//! - **Memory** ([`memory`]): a flat virtual address space. Every local,
//!   temporary, string literal, constant and global lives in its own
//!   allocation at a fresh, never reused address, so out-of-bounds,
//!   use-after-return, null and misaligned accesses and writes to read-only
//!   data are detected exactly (spec §18.4). Values are plain bytes in target
//!   layout; pointers are addresses.
//! - **Values** ([`value`]): first-class data is [`value::Value::Bytes`];
//!   second-class function and handler values are structured so they can
//!   capture frames and handler contexts.
//! - **Control flow**: evaluation returns `Result<Value, Box<Flow>>`. `break`,
//!   `continue`, `return`, effect exits and traps are `Flow`s that propagate
//!   up the Rust stack; blocks run `defer`/`errdefer` statements as flows
//!   pass through them (spec §8.8), loops, `with` and function boundaries
//!   consume the flows aimed at them.
//! - **Effects**: the handler context is a persistent linked list captured by
//!   closures and handler literals (spec §11.6, §12.7).

mod builtins;
mod bytes;
mod calls;
mod consts;
pub mod display;
mod eval;
mod fast;
mod fiber;
mod machine;
pub mod memory;
pub mod num;
mod ops;
mod stmt;
mod sys;
mod threads;
mod value;
mod vm;

#[cfg(test)]
mod testkit;
#[cfg(test)]
mod tests;
#[cfg(test)]
mod tests_fast;
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
use std::io::Write;
use value::*;

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
const STACK_SIZE: usize = 1 << 30;
/// Stack kept in reserve when checking for overflow.
const STACK_MARGIN: usize = 16 << 20;

struct AssertSend<T>(T);
// SAFETY: the wrapped references are handed to the evaluation thread while the
// spawning thread blocks in `thread::scope`; they are never used concurrently.
// The interpreter thread is the only one touching the `Rc`s inside the program.
unsafe impl<T> Send for AssertSend<T> {}

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

fn run_main_inner(
    prog: &Program,
    cfg: &RunConfig,
    main: FnId,
    args: &[String],
    stdout: &mut dyn Write,
    stderr: &mut dyn Write,
) -> Outcome {
    let lookup = |id: AdtId| prog.adts.get(&id);
    let mut m = Machine::new(prog, cfg, &lookup, stack_floor(STACK_SIZE, STACK_MARGIN));
    m.out = Some(stdout);
    m.errw = Some(stderr);
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
    outcome
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

fn eval_const_inner(prog: &Program, cfg: &RunConfig, expr: &Expr) -> Result<ConstValue, Trap> {
    let lookup = |id: AdtId| prog.adts.get(&id);
    let mut m = Machine::new(prog, cfg, &lookup, stack_floor(STACK_SIZE, STACK_MARGIN));
    m.const_mode = true;
    let r = m.with_root_frame(expr.ty, |m, act| {
        let b = match m.eval_bytes(act, expr) {
            Ok(b) => b,
            Err(f) => match *f {
                Flow::Return { frame, value: Value::Bytes(b), .. } if frame == act.fr.id => b,
                other => return Err(Box::new(other)),
            },
        };
        m.to_const_value(expr.span, expr.ty, &b)
    });
    match r {
        Ok(v) => Ok(v),
        Err(f) => match *f {
            Flow::Trap(t) => Err(t),
            Flow::Exit(_) => Err(Trap::new("`Env.exit` at compile time", expr.span)),
            _ => Err(Trap::new("internal error: control flow escaped a constant", expr.span)),
        },
    }
}
