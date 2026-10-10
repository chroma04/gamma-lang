//! The static check of continuations (§14.2): when a `ctl` clause uses its
//! continuation `k` only as the operand of `resume` and `discard`, every path
//! through the clause must consume it exactly once.
//!
//! The analysis walks the clause body in evaluation order with a small state:
//! whether `k` may still be unconsumed and whether it may already be consumed on
//! the paths reaching a point. Loops are iterated to a fixed point. `resume` and a
//! `break` to the installing `with` need `k` consumed on every path; consuming needs
//! it unconsumed on every path. Other exits leave the `with`, which discards `k`.

use super::Sema;
use crate::source::Span;
use crate::tir::{Block, CondPart, Expr, ExprKind, FnId, HandlerValue, LabelId, LocalId, Stmt, Target};
use crate::types::TyKind;
use std::collections::HashSet;

/// `k` may be unconsumed on some path reaching this point.
const OPEN: u8 = 1;
/// `k` may be consumed on some path reaching this point.
const DONE: u8 = 2;

impl Sema<'_> {
    /// Checks every `ctl` clause whose continuation is used only by `resume` and
    /// `discard` (§14.2).
    pub(super) fn check_continuations(&mut self) {
        let mut errors = vec![];
        for (i, def) in self.prog.fns.iter().enumerate() {
            let Some(body) = &def.body else { continue };
            let Some(&k) = def.params.last() else { continue };
            if !matches!(def.locals[k.index()].ty.kind(), TyKind::Cont(_)) {
                continue;
            }
            // A nested function that captures `k` may consume it at any time.
            let captured = self
                .prog
                .fns
                .iter()
                .any(|f| f.parent == Some(FnId(i as u32)) && f.body.as_ref().is_some_and(|b| mentions_upvar(b, k)));
            if captured || !only_consumed(body, k) {
                continue;
            }
            let mut cx = Check { k, loops: vec![], errors: vec![] };
            // A clause that finishes normally is reported elsewhere (§14.3).
            cx.expr(body, OPEN);
            errors.extend(cx.errors);
        }
        let mut seen = HashSet::new();
        for (span, msg) in errors {
            if seen.insert((span, msg)) {
                self.error(span, msg);
            }
        }
    }
}

/// True if every use of `k` in `e` is the operand of `resume` or `discard`, outside
/// deferred statements.
fn only_consumed(e: &Expr, k: LocalId) -> bool {
    match &e.kind {
        ExprKind::Local(l) => *l != k,
        ExprKind::Block(b)
            if b.stmts.iter().any(|s| matches!(s, Stmt::Defer { body, .. } if mentions_local(body, k))) =>
        {
            false
        }
        ExprKind::Resume { cont, value } if is_k(cont, k) => only_consumed(value, k),
        ExprKind::Discard(c) if is_k(c, k) => true,
        _ => {
            let mut ok = true;
            e.for_each_child(&mut |c| ok &= only_consumed(c, k));
            ok
        }
    }
}

fn mentions_local(e: &Expr, k: LocalId) -> bool {
    if is_k(e, k) {
        return true;
    }
    let mut found = false;
    e.for_each_child(&mut |c| found |= mentions_local(c, k));
    found
}

fn mentions_upvar(e: &Expr, k: LocalId) -> bool {
    if let ExprKind::Upvar { local, .. } = &e.kind
        && *local == k
    {
        return true;
    }
    let mut found = false;
    e.for_each_child(&mut |c| found |= mentions_upvar(c, k));
    found
}

fn is_k(e: &Expr, k: LocalId) -> bool {
    matches!(e.kind, ExprKind::Local(l) if l == k)
}

struct Check {
    k: LocalId,
    /// Enclosing loops and `with`s of the clause: label, states at `continue`, at `break`.
    loops: Vec<(LabelId, u8, u8)>,
    errors: Vec<(Span, &'static str)>,
}

impl Check {
    /// Leaving the clause with state `s`.
    fn exit(&mut self, s: u8, span: Span) {
        if s & OPEN != 0 {
            self.errors.push((span, "the continuation may be left unconsumed on this path (§14.2)"));
        }
    }

    fn consume(&mut self, s: u8, span: Span) {
        if s & DONE != 0 {
            self.errors.push((span, "the continuation may already have been consumed here (§14.2)"));
        }
    }

    /// The state after `e` completes normally, given state `s` before it; 0 if it
    /// never does.
    fn expr(&mut self, e: &Expr, s: u8) -> u8 {
        if s == 0 {
            return 0;
        }
        let out = match &e.kind {
            ExprKind::Resume { cont, value } => {
                let s = self.expr(value, s);
                if s != 0 {
                    if is_k(cont, self.k) {
                        self.consume(s, e.span);
                    } else {
                        let s = self.expr(cont, s);
                        self.exit(s, e.span);
                    }
                }
                0
            }
            ExprKind::Discard(c) if is_k(c, self.k) => {
                self.consume(s, e.span);
                DONE
            }
            ExprKind::Break { target, value } => {
                let s = value.as_ref().map_or(s, |v| self.expr(v, s));
                self.jump(target, s, true, e.span);
                0
            }
            ExprKind::Continue { target } => {
                self.jump(target, s, false, e.span);
                0
            }
            // Leaving the function crosses the `with`, which discards `k` (§14.4).
            ExprKind::Return { value, .. } => {
                self.expr(value, s);
                0
            }
            ExprKind::Block(b) => self.block(b, s),
            ExprKind::If { cond, then, else_ } => {
                let (pass, fail) = self.cond(cond, s);
                let t = self.expr(then, pass);
                let f = match else_ {
                    Some(x) => self.expr(x, fail),
                    None => fail,
                };
                t | f
            }
            ExprKind::Match { scrutinee, arms } => {
                let s = self.expr(scrutinee, s);
                let mut out = 0;
                let mut guard_fail = s;
                for a in arms {
                    let entry = guard_fail | s;
                    let g = match &a.guard {
                        Some(g) => self.expr(g, entry),
                        None => entry,
                    };
                    guard_fail |= g;
                    out |= self.expr(&a.body, g);
                }
                out
            }
            ExprKind::Logic { lhs, rhs, .. } | ExprKind::Else { lhs, rhs, .. } => {
                let l = self.expr(lhs, s);
                l | self.expr(rhs, l)
            }
            ExprKind::Loop { label, cond, body, step } => self.loop_(*label, cond, body, step.as_deref(), s),
            ExprKind::With { label, handlers, on, body } => {
                let mut s = s;
                for h in handlers {
                    let (HandlerValue::Literal(x) | HandlerValue::Decl { value: x, .. }) = h;
                    s = self.expr(x, s);
                }
                if let Some(o) = on {
                    s = self.expr(o, s);
                }
                self.loops.push((*label, 0, 0));
                let b = self.expr(body, s);
                let (_, _, brk) = self.loops.pop().unwrap();
                b | brk
            }
            ExprKind::Trap { message } => {
                self.expr(message, s);
                0
            }
            _ => {
                let mut s = s;
                e.for_each_child(&mut |c| s = self.expr(c, s));
                s
            }
        };
        if e.ty.is_never() { 0 } else { out }
    }

    fn jump(&mut self, target: &Target, s: u8, is_break: bool, span: Span) {
        if let Target::Label { levels: 0, label } = target
            && let Some(l) = self.loops.iter_mut().rev().find(|l| l.0 == *label)
        {
            if is_break {
                l.2 |= s;
            } else {
                l.1 |= s;
            }
            return;
        }
        // A `break` completing the installing `with` needs `k` consumed; a jump to an
        // outer label crosses the `with`, which discards it (§14.4).
        if let Target::InstallingWith = target {
            self.exit(s, span);
        }
    }

    /// The states after `cond` succeeds and after it fails.
    fn cond(&mut self, cond: &[CondPart], s: u8) -> (u8, u8) {
        let (mut pass, mut fail) = (s, 0);
        for c in cond {
            match c {
                CondPart::Bool(x) => {
                    pass = self.expr(x, pass);
                    if !matches!(x.kind, ExprKind::Bool(true)) {
                        fail |= pass;
                    }
                }
                CondPart::Match { value, .. } => {
                    pass = self.expr(value, pass);
                    fail |= pass;
                }
            }
        }
        (pass, fail)
    }

    fn loop_(&mut self, label: LabelId, cond: &[CondPart], body: &Block, step: Option<&Expr>, s: u8) -> u8 {
        let mut entry = s;
        loop {
            let saved = self.errors.len();
            let (pass, fail) = self.cond(cond, entry);
            self.loops.push((label, 0, 0));
            let b = self.block(body, pass);
            let (_, cont, brk) = self.loops.pop().unwrap();
            let mut back = b | cont;
            if let Some(st) = step {
                back = self.expr(st, back);
            }
            let next = entry | back;
            if next == entry {
                return fail | brk;
            }
            // Report only what the final iteration of the fixed point finds.
            self.errors.truncate(saved);
            entry = next;
        }
    }

    fn block(&mut self, b: &Block, s: u8) -> u8 {
        let mut s = s;
        for st in &b.stmts {
            match st {
                Stmt::Let { init, .. } => {
                    if let Some(x) = init {
                        s = self.expr(x, s);
                    }
                }
                Stmt::LetTuple { init, .. } => s = self.expr(init, s),
                Stmt::Assign { place, value } => {
                    s = self.expr(place, s);
                    s = self.expr(value, s);
                }
                Stmt::AssignTuple { places, value } => {
                    for p in places.iter().flatten() {
                        s = self.expr(p, s);
                    }
                    s = self.expr(value, s);
                }
                Stmt::Expr(x) => s = self.expr(x, s),
                // Deferred statements do not use `k` (`only_consumed`).
                Stmt::Defer { .. } => {}
            }
        }
        if let Some(t) = &b.tail {
            s = self.expr(t, s);
        }
        s
    }
}
