//! Parameters passed without a copy (Appendix D.5).
//!
//! A large parameter (`Types::by_ref`) arrives as the address of the caller's
//! value, and the callee normally copies it into its frame, since a parameter is
//! an object of its own (§11.2). The copy is skipped when the difference cannot
//! be observed: the callee never writes the parameter or takes its address, and
//! nothing can write the caller's value while the callee runs. The second holds
//! when the callee is *pure* here: it has no effects, writes only its own locals,
//! and calls only pure functions, so nothing else runs during the call.

use super::Gen;
use crate::tir::{Block, Builtin, Callee, Expr, ExprKind, FnId, FnKind, LocalId, Stmt};

impl<'a> Gen<'a> {
    /// For each parameter of `f`: whether it is used in place of a copy.
    pub fn borrowed_params(&mut self, f: FnId) -> Vec<bool> {
        let def = &self.prog.fns[f.index()];
        let candidates: Vec<bool> = def.params.iter().map(|p| self.ty.by_ref(def.locals[p.index()].ty)).collect();
        if !candidates.contains(&true) || !self.is_pure(f) {
            return vec![false; def.params.len()];
        }
        let Some(body) = &def.body else { return vec![false; def.params.len()] };
        let mut touched = vec![];
        mutated_locals(body, &mut touched);
        def.params.iter().zip(candidates).map(|(p, c)| c && !touched.contains(p)).collect()
    }

    /// Whether `f` is pure in the sense of the module docs: a named function or a
    /// literal that captures nothing, with no effects, that stores only into its own
    /// locals and calls only pure functions.
    pub fn is_pure(&mut self, f: FnId) -> bool {
        if let Some(r) = self.pure_memo.get(&f) {
            // `None`: being computed (recursion), assumed impure.
            return r.unwrap_or(false);
        }
        self.pure_memo.insert(f, None);
        let prog = self.prog;
        let def = &prog.fns[f.index()];
        let r = matches!(def.kind, FnKind::Named | FnKind::Literal)
            && def.parent.is_none()
            && def.effects.effects.is_empty()
            && def.body.as_ref().is_some_and(|b| self.pure_expr(b));
        self.pure_memo.insert(f, Some(r));
        r
    }

    fn pure_expr(&mut self, e: &'a Expr) -> bool {
        let ok = match &e.kind {
            ExprKind::Block(b) => return self.pure_block(b),
            ExprKind::Loop { cond, body, step, .. } => {
                let mut ok = true;
                for c in cond {
                    c.for_each_child(&mut |x| ok = ok && self.pure_expr(x));
                }
                return ok && self.pure_block(body) && step.as_deref().is_none_or(|s| self.pure_expr(s));
            }
            ExprKind::Call { callee: Callee::Fn(g), .. } => self.is_pure(*g),
            ExprKind::Call { .. }
            | ExprKind::With { .. }
            | ExprKind::Closure(_)
            | ExprKind::HandlerRef(_)
            | ExprKind::Resume { .. }
            | ExprKind::Discard(_)
            | ExprKind::Write { .. }
            | ExprKind::WriteDeferred { .. }
            | ExprKind::Global(_)
            | ExprKind::Upvar { .. } => false,
            ExprKind::Builtin { op, .. } => !matches!(
                op,
                Builtin::AtomicStore
                    | Builtin::AtomicSwap
                    | Builtin::AtomicCas
                    | Builtin::AtomicRmw(_)
                    | Builtin::VolatileStore
                    | Builtin::Sys(_)
                    | Builtin::Fiber
                    | Builtin::HeapAlloc
                    | Builtin::HeapFree
                    | Builtin::AllocTag
                    | Builtin::AllocCheck
                    | Builtin::Asm { .. }
            ),
            _ => true,
        };
        if !ok {
            return false;
        }
        let mut ok = true;
        e.for_each_child(&mut |x| ok = ok && self.pure_expr(x));
        ok
    }

    fn pure_block(&mut self, b: &'a Block) -> bool {
        for s in &b.stmts {
            let ok = match s {
                Stmt::Assign { place, value } => own_place(place) && self.pure_expr(place) && self.pure_expr(value),
                Stmt::AssignTuple { places, value } => {
                    places.iter().flatten().all(own_place)
                        && places.iter().flatten().all(|p| self.pure_expr(p))
                        && self.pure_expr(value)
                }
                Stmt::Let { init, .. } => init.as_ref().is_none_or(|x| self.pure_expr(x)),
                Stmt::LetTuple { init, .. } => self.pure_expr(init),
                Stmt::Expr(x) | Stmt::Defer { body: x, .. } => self.pure_expr(x),
            };
            if !ok {
                return false;
            }
        }
        b.tail.as_ref().is_none_or(|t| self.pure_expr(t))
    }
}

/// True if `place` is (part of) a local of the function itself, not reached through
/// a pointer or a slice.
fn own_place(place: &Expr) -> bool {
    root_local(place).is_some()
}

/// The local that place `e` is part of, if it is not reached through a pointer.
fn root_local(e: &Expr) -> Option<LocalId> {
    match &e.kind {
        ExprKind::Local(l) => Some(*l),
        ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => root_local(base),
        ExprKind::Index { base, .. } if matches!(base.ty.kind(), crate::types::TyKind::Array { .. }) => {
            root_local(base)
        }
        _ => None,
    }
}

/// Collects the locals of the function whose body is `e` that it stores into, takes
/// the address of, or slices.
fn mutated_locals(e: &Expr, out: &mut Vec<LocalId>) {
    match &e.kind {
        ExprKind::AddrOf(p) | ExprKind::Slice { base: p, .. } => out.extend(root_local(p)),
        ExprKind::Block(b) => return mutated_in_block(b, out),
        ExprKind::Loop { cond, body, step, .. } => {
            for c in cond {
                c.for_each_child(&mut |x| mutated_locals(x, out));
            }
            mutated_in_block(body, out);
            if let Some(s) = step {
                mutated_locals(s, out);
            }
            return;
        }
        _ => {}
    }
    e.for_each_child(&mut |x| mutated_locals(x, out));
}

fn mutated_in_block(b: &Block, out: &mut Vec<LocalId>) {
    for s in &b.stmts {
        match s {
            Stmt::Assign { place, .. } => out.extend(root_local(place)),
            Stmt::AssignTuple { places, .. } => out.extend(places.iter().flatten().filter_map(root_local)),
            _ => {}
        }
    }
    b.for_each_child(&mut |x| mutated_locals(x, out));
}
