//! Parameters passed without a copy (Appendix D.5).
//!
//! A large parameter (`Types::by_ref`) arrives as the address of the caller's
//! value, and the callee normally copies it into its frame, since a parameter is
//! an object of its own (§11.2). The copy is skipped when the difference cannot
//! be observed: the callee never writes the parameter or takes its address, and
//! nothing can write the caller's value while the callee runs. The second holds
//! when the callee is *pure* here: it has no effects, writes only its own locals,
//! and calls only pure functions, so nothing else runs during the call.
//!
//! It also holds when every call passes a value that nothing writes during the
//! call (`steady_params`): a constant from static storage (see `constarg`), a
//! local of the caller that it never writes after its initialization, never
//! takes the address of and does not share with a function literal, or a
//! temporary. This needs every call to be visible, so a function whose address is
//! taken, or that is a handler clause, exported or a closure, is left out. Such a
//! parameter is typically a `SourceLocation` that `@caller_location()` fills in
//! and that is handed down from `new` to the functions it calls.

use super::Gen;
use crate::tir::{Block, Builtin, Callee, Expr, ExprKind, FnId, FnKind, LocalId, StaticData, Stmt};

impl<'a> Gen<'a> {
    /// For each parameter of `f`: whether it is used in place of a copy.
    pub fn borrowed_params(&mut self, f: FnId) -> Vec<bool> {
        let def = &self.prog.fns[f.index()];
        let candidates: Vec<bool> = def.params.iter().map(|p| self.ty.by_ref(def.locals[p.index()].ty)).collect();
        if !candidates.contains(&true) {
            return vec![false; def.params.len()];
        }
        if !self.is_pure(f) {
            return self.steady_params(f).to_vec();
        }
        let Some(body) = &def.body else { return vec![false; def.params.len()] };
        let mut touched = vec![];
        mutated_locals(body, &mut touched);
        def.params.iter().zip(candidates).map(|(p, c)| c && !touched.contains(p)).collect()
    }

    /// For each parameter of `f`: whether it is passed by address, `f` never writes it,
    /// takes its address or shares it with a function literal, and every call of `f`
    /// passes a value that nothing can write during the call (see the module docs).
    fn steady_params(&mut self, f: FnId) -> &[bool] {
        if self.steady.is_none() {
            let s = self.compute_steady();
            self.steady = Some(s);
        }
        &self.steady.as_ref().unwrap()[f.index()]
    }

    fn compute_steady(&mut self) -> Vec<Vec<bool>> {
        let prog = self.prog;
        // Functions called other than by a direct call.
        let mut escaping = vec![false; prog.fns.len()];
        for d in &prog.fns {
            if let Some(b) = &d.body {
                fn_refs(b, &mut escaping);
            }
        }
        for c in &prog.consts {
            fn_refs(&c.init, &mut escaping);
            for r in c.value.iter().flat_map(|v| &v.relocs) {
                if let StaticData::Fn(g) = r.target {
                    escaping[g.index()] = true;
                }
            }
        }
        for g in &prog.globals {
            if let Some(i) = &g.init {
                fn_refs(i, &mut escaping);
            }
            for r in g.value.iter().flat_map(|v| &v.relocs) {
                if let StaticData::Fn(g) = r.target {
                    escaping[g.index()] = true;
                }
            }
        }
        for (_, g) in &prog.exports {
            escaping[g.index()] = true;
        }
        if let Some(g) = prog.trap_hook {
            escaping[g.index()] = true;
        }
        // Debug information shows parameters in the frame, so they are copied there.
        let debug_info = self.opts.debug_info;
        let mut written: Vec<Vec<LocalId>> = Vec::with_capacity(prog.fns.len());
        for d in &prog.fns {
            let mut w = vec![];
            if let Some(b) = &d.body {
                mutated_locals(b, &mut w);
            }
            written.push(w);
        }
        let captured = self.captured().clone();
        let mut steady: Vec<Vec<bool>> = prog
            .fns
            .iter()
            .enumerate()
            .map(|(i, d)| {
                let f = FnId(i as u32);
                let eligible = !debug_info
                    && matches!(d.kind, FnKind::Named)
                    && d.parent.is_none()
                    && d.body.is_some()
                    && !escaping[i];
                d.params
                    .iter()
                    .map(|p| {
                        eligible
                            && self.ty.by_ref(d.locals[p.index()].ty)
                            && !written[i].contains(p)
                            && !captured.contains(&(f, *p))
                    })
                    .collect()
            })
            .collect();
        // Every call must pass a value that stays as it is during the call.
        for (i, d) in prog.fns.iter().enumerate() {
            let Some(b) = &d.body else { continue };
            let caller = FnId(i as u32);
            let mut calls = vec![];
            direct_calls(b, &mut calls);
            for (g, args) in calls {
                for (k, a) in args.iter().enumerate() {
                    if steady[g.index()].get(k).copied().unwrap_or(false)
                        && !self.steady_arg(caller, a, &written[i], &captured)
                    {
                        steady[g.index()][k] = false;
                    }
                }
            }
        }
        steady
    }

    /// Whether argument `a` of a call in `caller` is a value that nothing can write while
    /// the callee runs.
    fn steady_arg(
        &mut self,
        caller: FnId,
        a: &Expr,
        written: &[LocalId],
        captured: &std::collections::HashSet<(FnId, LocalId)>,
    ) -> bool {
        match &a.kind {
            // Built in a temporary of the call.
            ExprKind::Aggregate(_) | ExprKind::Array(_) | ExprKind::Call { .. } => true,
            _ => match root_local(a) {
                // A by-address parameter of the caller is either its own copy or, when
                // read in place, a value that is steady itself (or the caller is pure,
                // and so is the callee, which then reads it in place regardless).
                Some(l) => !written.contains(&l) && !captured.contains(&(caller, l)),
                None => self.is_const_arg(a),
            },
        }
    }

    /// For each parameter of `f`: whether it is used in place and also appears inside an
    /// aggregate or array construction, which `FnGen::expr_into` may build directly in the
    /// result destination while still reading the parameter. A caller must then not pass
    /// a destination that overlaps the parameter's value.
    pub fn read_during_build(&mut self, f: FnId) -> Vec<bool> {
        if let Some(r) = self.early_memo.get(&f) {
            return r.clone();
        }
        let mut r = self.borrowed_params(f);
        if r.contains(&true) {
            let def = &self.prog.fns[f.index()];
            let mut used = vec![];
            if let Some(body) = &def.body {
                locals_in_aggregates(body, false, &mut used);
            }
            for (flag, p) in r.iter_mut().zip(&def.params) {
                *flag = *flag && used.contains(p);
            }
        }
        self.early_memo.insert(f, r.clone());
        r
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
            | ExprKind::Cancel(_)
            | ExprKind::Write { .. }
            | ExprKind::WriteDeferred { .. }
            | ExprKind::SoaDeferred { .. }
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
pub fn root_local(e: &Expr) -> Option<LocalId> {
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

/// Marks the functions that `e` refers to other than as the callee of a direct call.
fn fn_refs(e: &Expr, out: &mut [bool]) {
    if let ExprKind::FnRef(f) | ExprKind::Closure(f) = &e.kind {
        out[f.index()] = true;
    }
    e.for_each_child(&mut |x| fn_refs(x, out));
}

/// Collects the direct calls in `e` with their arguments.
fn direct_calls<'e>(e: &'e Expr, out: &mut Vec<(FnId, &'e [Expr])>) {
    if let ExprKind::Call { callee: Callee::Fn(f), args, .. } = &e.kind {
        out.push((*f, args));
    }
    e.for_each_child(&mut |x| direct_calls(x, out));
}

/// Collects the locals read inside the construction of an aggregate or array in `e`.
fn locals_in_aggregates(e: &Expr, inside: bool, out: &mut Vec<LocalId>) {
    if inside && let ExprKind::Local(l) = &e.kind {
        out.push(*l);
    }
    let inside = inside || matches!(e.kind, ExprKind::Aggregate(_) | ExprKind::Array(_));
    e.for_each_child(&mut |x| locals_in_aggregates(x, inside, out));
}
