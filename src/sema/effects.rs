//! Effects and handlers (§12): performing operations, handler literals,
//! `with`, and `for` over iterator calls (§13.2).

use super::func::{ClauseInfo, FnCx, LabelValue, LocalBinding, NestedFn, error_expr};
use super::pattern::PatCtx;
use super::{FnSig, ParamInfo};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind, ExprKind};
use crate::tir::{self, Expr, HandlerId, Stmt};
use crate::types::{CallConv, EffectId, EffectRef, EffectSet, GenericArg, ParamId, Ty, TyKind};
use std::collections::HashMap;

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

/// Replaces the effect's type parameters in `t` by the instantiation's arguments.
pub fn subst(t: Ty, params: &[ParamId], args: &[GenericArg]) -> Ty {
    if params.is_empty() {
        return t;
    }
    t.map(&mut |x| match x.kind() {
        TyKind::Param(p) => params.iter().position(|q| q == p).and_then(|i| match &args[i] {
            GenericArg::Type(a) => Some(*a),
            _ => None,
        }),
        _ => None,
    })
}

/// Binds type parameters by matching a parameter type against an argument type.
pub fn match_params_pub(pat: Ty, actual: Ty, binds: &mut HashMap<ParamId, Ty>) {
    match_params(pat, actual, binds)
}

fn match_params(pat: Ty, actual: Ty, binds: &mut HashMap<ParamId, Ty>) {
    if actual.has_infer() || actual.is_error() {
        return;
    }
    match (pat.kind(), actual.kind()) {
        (TyKind::Param(p), _) => {
            binds.entry(*p).or_insert(actual);
        }
        (TyKind::ParamArray { len: p, elem: a }, TyKind::ParamArray { len: q, elem: b }) => {
            match_params(*p, *q, binds);
            match_params(*a, *b, binds);
        }
        // `[N]T` against `[4]u8` gives `N = 4` (as a `u64`, converted by the caller).
        (TyKind::ParamArray { len, elem: a }, TyKind::Array { len: n, elem: b }) => {
            if let TyKind::Param(p) = len.kind() {
                let key = crate::types::ConstKey::int(Ty::int(false, 64), &(*n).into(), 8);
                binds.entry(*p).or_insert(Ty::intern(TyKind::Value(key)));
            }
            match_params(*a, *b, binds);
        }
        (TyKind::Ptr { pointee: a, .. }, TyKind::Ptr { pointee: b, .. })
        | (TyKind::Slice { elem: a, .. }, TyKind::Slice { elem: b, .. })
        | (TyKind::ManyPtr { elem: a, .. }, TyKind::ManyPtr { elem: b, .. })
        | (TyKind::Array { elem: a, .. }, TyKind::Array { elem: b, .. })
        | (TyKind::Optional(a), TyKind::Optional(b)) => match_params(*a, *b, binds),
        (TyKind::Result { err: e1, ok: o1 }, TyKind::Result { err: e2, ok: o2 }) => {
            match_params(*e1, *e2, binds);
            match_params(*o1, *o2, binds);
        }
        (TyKind::Adt(a, x), TyKind::Adt(b, y)) if a.template() == b.template() && x.len() == y.len() => {
            for (p, q) in x.iter().zip(y.iter()) {
                if let (GenericArg::Type(p), GenericArg::Type(q)) = (p, q) {
                    match_params(*p, *q, binds);
                }
            }
        }
        (TyKind::Tuple { elems: x, .. }, TyKind::Tuple { elems: y, .. }) if x.len() == y.len() => {
            for (a, b) in x.iter().zip(y.iter()) {
                match_params(*a, *b, binds);
            }
        }
        (TyKind::Fn(f), TyKind::Fn(g)) | (TyKind::FnPtr(f), TyKind::FnPtr(g)) | (TyKind::Fn(f), TyKind::FnPtr(g))
            if f.params.len() == g.params.len() =>
        {
            for (a, b) in f.params.iter().zip(g.params.iter()) {
                match_params(*a, *b, binds);
            }
            match_params(f.ret, g.ret, binds);
            match_effects(&f.effects, &g.effects, binds);
        }
        (TyKind::Effects(a), TyKind::Effects(b)) => match_effects(a, b, binds),
        _ => {}
    }
}

/// Binds the parameters of effect set `pat` from `actual` (§16.6): the arguments of an
/// effect that occurs once in each, then a single `..E` from the effects of `actual`
/// that `pat` does not name.
fn match_effects(pat: &EffectSet, actual: &EffectSet, binds: &mut HashMap<ParamId, Ty>) {
    for p in pat.effects.iter().filter(|p| !p.is_splice() && !p.args.is_empty()) {
        let mut same = actual.effects.iter().filter(|a| a.effect == p.effect);
        if let (Some(a), None) = (same.next(), same.next()) {
            for (x, y) in p.args.iter().zip(a.args.iter()) {
                if let (GenericArg::Type(x), GenericArg::Type(y)) = (x, y) {
                    match_params(*x, *y, binds);
                }
            }
        }
    }
    let mut splices = pat.effects.iter().filter(|p| p.is_splice());
    if let (Some(sp), None) = (splices.next(), splices.next())
        && let Some(GenericArg::Type(t)) = sp.args.first()
        && let TyKind::Param(e) = t.kind()
    {
        let rest: Vec<EffectRef> = actual
            .effects
            .iter()
            .filter(|a| !pat.effects.iter().any(|p| !p.is_splice() && p.effect == a.effect))
            .cloned()
            .collect();
        binds.entry(*e).or_insert(Ty::intern(TyKind::Effects(EffectSet::new(rest))));
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// The instantiations of `eff` available at this point: declared by the
    /// enclosing functions or handled by enclosing `with`s.
    fn available(&self, eff: EffectId) -> Vec<EffectRef> {
        let mut out: Vec<EffectRef> = vec![];
        let mut add = |list: &mut dyn Iterator<Item = &EffectRef>| {
            for e in list {
                if e.effect == eff && !out.contains(e) {
                    out.push(e.clone());
                }
            }
        };
        add(&mut self.declared.effects.iter());
        add(&mut self.handled.iter());
        for f in &self.outer {
            add(&mut f.declared.effects.iter());
            add(&mut f.handled.iter());
        }
        out
    }

    /// Calling an effect operation performs it (§12.1).
    pub fn perform_op(&mut self, eff: EffectId, op: u32, args: &[ast::Arg], span: Span) -> Expr {
        self.perform_op_inner(eff, op, args, span)
    }

    /// Performs operation `op` of the given instantiation of its effect (`Yield(u8).yield(x)`).
    pub fn perform_op_as(&mut self, eref: EffectRef, op: u32, args: &[ast::Arg], span: Span) -> Expr {
        let info = &self.sema.effects[&eref.effect];
        let (params, module) = (info.params.clone(), info.module);
        let op_info = info.ops[op as usize].clone();
        if eref.args.len() != params.len() {
            self.error(span, format!("`{}` needs {} type argument(s)", eref.effect.name(), params.len()));
            return error_expr(span);
        }
        let ps: Vec<ParamInfo> =
            op_info.params.iter().map(|p| ParamInfo { ty: subst(p.ty, &params, &eref.args), ..p.clone() }).collect();
        let ret = subst(op_info.ret, &params, &eref.args);
        let (vals, pre) = self.bind_args(&ps, module, args, span, "operation");
        self.performed.push((eref.clone(), span));
        if op_info.kind == ast::OpKind::Fn {
            self.fn_performs.push(span);
        }
        let call = mk(ret, tir::ExprKind::Call { callee: tir::Callee::Perform { effect: eref, op }, args: vals }, span);
        self.finish_call(pre, call, false)
    }

    fn perform_op_inner(&mut self, eff: EffectId, op: u32, args: &[ast::Arg], span: Span) -> Expr {
        let info = &self.sema.effects[&eff];
        let params = info.params.clone();
        let op_info = info.ops[op as usize].clone();
        let eref = if params.is_empty() {
            EffectRef { effect: eff, args: Box::new([]) }
        } else {
            // Infer the instantiation from the arguments, or from the one available.
            let positional: Vec<&ast::Expr> = args
                .iter()
                .filter_map(|a| match &a.kind {
                    ArgKind::Positional(e) => Some(e),
                    _ => None,
                })
                .collect();
            // With exactly one instantiation available, that one is performed (§12.1), so
            // the arguments are checked against its parameter types: `emit(.Opened(…))`.
            let avail = self.available(eff);
            let only = match avail.as_slice() {
                [one] => Some(one.clone()),
                _ => None,
            };
            let mut binds = HashMap::new();
            let mut probe = vec![];
            for (i, e) in positional.iter().enumerate() {
                let expect = match (&only, op_info.params.get(i)) {
                    (Some(one), Some(p)) => Some(subst(p.ty, &params, &one.args)),
                    _ => None,
                };
                let v = self.expr(e, expect);
                if let Some(p) = op_info.params.get(i) {
                    let t = self.infer.resolve(v.ty);
                    match_params(p.ty, t, &mut binds);
                }
                probe.push(v);
            }
            let eargs: Option<Vec<GenericArg>> =
                params.iter().map(|p| binds.get(p).map(|t| GenericArg::Type(*t))).collect();
            let eref = match eargs {
                // An instantiation that is not available, where exactly one is: the
                // arguments are coerced to that one (`yield(buf)` with `buf: []u8` in a
                // function performing `Yield(string)`).
                Some(a) => {
                    let inferred = EffectRef { effect: eff, args: a.into_boxed_slice() };
                    let avail = self.available(eff);
                    match avail.as_slice() {
                        [one] if *one != inferred => one.clone(),
                        _ => inferred,
                    }
                }
                None => {
                    let avail = self.available(eff);
                    match avail.as_slice() {
                        [one] => one.clone(),
                        [] => {
                            self.error(
                                span,
                                format!(
                                    "cannot infer which `{}` this performs; no instantiation is available here",
                                    eff.name()
                                ),
                            );
                            return error_expr(span);
                        }
                        _ => {
                            self.error(
                                span,
                                format!("cannot infer which `{}` this performs; several are available", eff.name()),
                            );
                            return error_expr(span);
                        }
                    }
                }
            };
            // Coerce the already-checked arguments to the instantiated parameter types.
            let mut vals = vec![];
            for (i, v) in probe.into_iter().enumerate() {
                match op_info.params.get(i) {
                    Some(p) => {
                        let t = subst(p.ty, &params, &eref.args);
                        vals.push(self.coerce(v, t));
                    }
                    None => self.error(v.span, "too many arguments"),
                }
            }
            if vals.len() < op_info.params.len() || positional.len() != args.len() {
                self.error(span, format!("`{}` takes {} positional argument(s)", op_info.name, op_info.params.len()));
            }
            let ret = subst(op_info.ret, &params, &eref.args);
            self.performed.push((eref.clone(), span));
            if self.sema.effects[&eref.effect].ops[op as usize].kind == ast::OpKind::Fn {
                self.fn_performs.push(span);
            }
            return mk(
                ret,
                tir::ExprKind::Call { callee: tir::Callee::Perform { effect: eref, op }, args: vals },
                span,
            );
        };
        let module = self.sema.effects[&eff].module;
        let (vals, pre) = self.bind_args(&op_info.params, module, args, span, "operation");
        self.performed.push((eref.clone(), span));
        if self.sema.effects[&eref.effect].ops[op as usize].kind == ast::OpKind::Fn {
            self.fn_performs.push(span);
        }
        let call = mk(
            op_info.ret,
            tir::ExprKind::Call { callee: tir::Callee::Perform { effect: eref, op }, args: vals },
            span,
        );
        self.finish_call(pre, call, false)
    }

    /// The parameter and result types of a clause for operation `op` of an instantiated effect.
    pub fn op_signature(&self, eref: &EffectRef, op: usize) -> (Vec<ParamInfo>, Ty, ast::OpKind) {
        self.sema.clause_signature(eref, op)
    }

    /// Checks a handler literal (§12.5), producing a second-class handler value.
    pub fn handler_literal(&mut self, lit: &ast::HandlerLit, with_expect: Option<Ty>, span: Span) -> Expr {
        let Some(hid) = self.new_handler_literal(lit) else { return error_expr(span) };
        self.handler_clauses(hid, lit, with_expect, span);
        mk(Ty::intern(TyKind::Handler(hid.0)), tir::ExprKind::HandlerRef(hid), span)
    }

    /// Starts a handler literal: resolves its effect and reserves its handler, whose
    /// clauses `handler_clauses` checks.
    pub fn new_handler_literal(&mut self, lit: &ast::HandlerLit) -> Option<HandlerId> {
        let module = self.module;
        let eref = self.sema.resolve_effect_ref(module, &lit.effect)?;
        let n_params = self.sema.effects[&eref.effect].params.len();
        if eref.args.len() != n_params {
            self.error(lit.effect.span, format!("`{}` needs {} type argument(s)", eref.effect.name(), n_params));
            return None;
        }
        let hid = HandlerId(self.sema.prog.handlers.len() as u32);
        self.sema.prog.handlers.push(tir::HandlerLit { effect: eref.clone(), clauses: vec![] });
        let ops: Vec<Symbol> = self.sema.effects[&eref.effect].ops.iter().map(|o| o.name).collect();
        for c in &lit.clauses {
            if !ops.contains(&c.name.name) {
                self.error(c.name.span, format!("`{}` has no operation `{}`", eref.effect.name(), c.name.name));
            }
            if lit.clauses.iter().filter(|d| d.name.name == c.name.name).count() > 1 {
                self.error(c.name.span, format!("operation `{}` is handled twice", c.name.name));
            }
        }
        Some(hid)
    }

    /// Checks the clauses of the handler literal `hid`; `with_expect` is the type of the
    /// value of the `with` that installs it, which its `break`s give.
    pub fn handler_clauses(&mut self, hid: HandlerId, lit: &ast::HandlerLit, with_expect: Option<Ty>, span: Span) {
        let eref = self.sema.prog.handlers[hid.index()].effect.clone();
        let ops: Vec<Symbol> = self.sema.effects[&eref.effect].ops.iter().map(|o| o.name).collect();
        let mut clauses = vec![];
        for (i, opname) in ops.iter().enumerate() {
            let Some(c) = lit.clauses.iter().find(|c| c.name.name == *opname) else {
                self.error(span, format!("this handler has no clause for the operation `{}`", opname));
                continue;
            };
            let (params, ret, kind) = self.op_signature(&eref, i);
            let info = ClauseInfo {
                op_kind: kind,
                for_label: None,
                handler: Some(hid),
                break_ty: None,
                effect: Some(eref.clone()),
                decl_clause: None,
                with_expect,
            };
            let f = self.clause_fn(c, &params, ret, info);
            clauses.push(f);
        }
        self.sema.prog.handlers[hid.index()].clauses = clauses;
    }

    /// Checks one clause as a nested function of the current one.
    fn clause_fn(&mut self, c: &ast::Clause, params: &[ParamInfo], ret: Ty, info: ClauseInfo) -> tir::FnId {
        if c.params.len() != params.len() {
            self.error(c.span, format!("the clause for `{}` takes {} parameter(s)", c.name.name, params.len()));
        }
        let sig =
            FnSig { module: self.module, params: params.to_vec(), ret, effects: EffectSet::empty(), generics: vec![] };
        let fn_id = self.sema.new_fn(c.name.name, c.span, sig, tir::FnKind::Clause);
        self.enter_fn(fn_id, ret, EffectSet::empty());
        self.clause = Some(info.clone());
        let mut ids = vec![];
        for (i, p) in params.iter().enumerate() {
            let name = c.params.get(i).map_or(p.name, |n| n.name);
            let sp = c.params.get(i).map_or(c.span, |n| n.span);
            let id = self.new_local(name, p.ty, false, sp);
            if name.as_str() != "_" {
                self.declare(name, LocalBinding::Var(id), sp);
            }
            ids.push(id);
        }
        // A `ctl` clause must not finish normally (§14.3): its body is checked without an
        // expected type, so that a branch that finishes gives it a type other than `never`.
        let expect = (info.op_kind != ast::OpKind::Ctl).then_some(ret);
        let body = match &c.body.kind {
            ExprKind::Block(b) => self.block_expected(b, true, expect),
            _ => self.expr(&c.body, expect),
        };
        let body = self.clause_body(body, info.op_kind, ret);
        self.finish_clause(fn_id, ids, body)
    }

    /// Leaves a clause frame: its performs count at the enclosing point (§12.5).
    fn finish_clause(&mut self, fn_id: tir::FnId, params: Vec<tir::LocalId>, body: Expr) -> tir::FnId {
        let (locals, performed, _) = self.exit_fn();
        self.performed.extend(performed);
        self.sema.prog.fns[fn_id.index()].parent = self.fn_id;
        self.nested.push(NestedFn { fn_id, params, locals, body });
        fn_id
    }

    /// `[L:] with h1, h2, … { body }` (§12.7).
    pub fn with_expr(&mut self, w: &ast::WithExpr, value: bool, expect: Option<Ty>) -> Expr {
        let span = w.span;
        self.push_scope();
        if let Some(l) = w.label {
            self.declare(l.name, LocalBinding::Label, l.span);
        }
        let mut handlers = vec![];
        let mut handled: Vec<EffectRef> = vec![];
        let mut literal_ids = vec![];
        // A handler with errors (already reported) may have handled anything the body does.
        let mut bad_handler = false;
        for h in &w.handlers {
            // `with a, b` is `with a { with b { … } }`: earlier handlers handle what later
            // ones' clauses perform (§12.7).
            let earlier = handled.clone();
            let mut inner = h;
            while let ExprKind::Paren(e) = &inner.kind {
                inner = e;
            }
            let v = match &inner.kind {
                ExprKind::Handler(lit) => self.handler_literal(lit, expect.filter(|_| value), h.span),
                _ => self.expr(h, None),
            };
            // The handler declaration is chosen by the value's type now, so an untyped
            // literal's type is settled here (`x := 0; with &x`).
            let t = self.infer.settle(v.ty);
            match t.kind() {
                TyKind::Handler(id) => {
                    if !self.pending_handlers.is_empty() {
                        self.check_pending_handler(HandlerId(*id), expect.filter(|_| value));
                    }
                    let eref = self.sema.prog.handlers[*id as usize].effect.clone();
                    if handled.contains(&eref) {
                        self.error(h.span, format!("two handlers for `{}` in one `with`", eref.effect.name()));
                    }
                    handled.push(eref);
                    literal_ids.push(*id);
                    handlers.push(tir::HandlerValue::Literal(v));
                }
                TyKind::Error => bad_handler = true,
                _ => {
                    let decls = self.sema.handlers_for(t);
                    if decls.is_empty() {
                        bad_handler = true;
                        let mut d = crate::diag::Diagnostic::error(
                            h.span,
                            format!("expected a handler, but no handler is declared for `{}`", t),
                        );
                        if !matches!(t.kind(), TyKind::Ptr { .. })
                            && !self.sema.handlers_for(Ty::ptr(false, t)).is_empty()
                        {
                            d = d.with_note(format!(
                                "a handler is declared for `^{}`: install it from a variable with `with &x`",
                                t
                            ));
                        }
                        self.sema.emit(d);
                        continue;
                    }
                    for d in &decls {
                        let eref = self.sema.prog.handler_decls[d.index()].effect.clone();
                        if handled.contains(&eref) {
                            self.error(h.span, format!("two handlers for `{}` in one `with`", eref.effect.name()));
                        }
                        handled.push(eref);
                        // The clauses' effects are performed at the `with`, outside it (§12.6).
                        let mut effs = self.sema.handler_decl_effects[d.index()].clone();
                        effs.effects = effs.effects.iter().filter(|e| !earlier.contains(e)).cloned().collect();
                        self.record_effects(&effs, h.span);
                    }
                    handlers.push(tir::HandlerValue::Decl { decls, value: v });
                }
            }
        }
        // A `ctl` operation needs a stack to suspend: `on` is required exactly then (§14.1).
        let has_ctl = handled.iter().any(|e| self.sema.prog.ctl_ops.iter().any(|(eff, _)| *eff == e.effect));
        let on = match &w.on {
            Some(s) => {
                if !has_ctl {
                    self.error(
                        s.span,
                        "`on` gives a stack to fibers, but none of these handlers handles a `ctl` operation",
                    );
                }
                let stack_ty = Ty::intern(TyKind::Slice { elem: Ty::int(false, 8), is_const: false });
                let v = self.expr(s, Some(stack_ty));
                Some(Box::new(self.coerce(v, stack_ty)))
            }
            None => {
                if has_ctl {
                    self.error(
                        span,
                        "this `with` handles `ctl` operations, so it needs a stack: `with h on stack { … }` (§14.1)",
                    );
                }
                None
            }
        };
        let label = self.new_label(w.label.map(|l| l.name), false, LabelValue::With(expect.filter(|_| value)));
        let start = self.performed.len();
        let n = self.handled.len();
        self.handled.extend(handled.iter().cloned());
        let body = self.block_expected(&w.body, value, expect);
        self.handled.truncate(n);
        self.labels.pop();
        self.pop_scope();
        // Whether this `with` handles a perform must not depend on type arguments (§16.6).
        let mut ambiguous = vec![];
        for (p, at) in &self.performed[start..] {
            if p.is_splice()
                && let Some(h) = handled.first()
            {
                ambiguous.push((*at, format!(
                    "this performs `{}` inside a `with` that handles `{}`, which the effect-set parameter might include (§16.6)",
                    p, h
                )));
                continue;
            }
            if let Some(h) = handled.iter().find(|h| super::decls::may_coincide(p, h)) {
                ambiguous.push((*at, format!(
                    "this performs `{}` inside a `with` that handles `{}`, which might be the same effect for some type arguments (§16.6)",
                    p, h
                )));
            }
        }
        for (at, msg) in ambiguous {
            self.error(at, msg);
        }
        // Effects handled here are not performed by the `with` (§12.3).
        let mut i = start;
        while i < self.performed.len() {
            // Not those of a handler literal written before this `with` began (§12.5).
            let shielded = self.shielded_performs.get(&self.performed[i].1).is_some_and(|&h| n >= h);
            if !shielded && (bad_handler || handled.contains(&self.performed[i].0)) {
                self.performed.remove(i);
            } else {
                i += 1;
            }
        }
        // Values of clause-level `break`s must agree with the body's value (§12.7).
        // So must those of `break`s to the `with`'s label (§8.5).
        let mut breaks: Vec<(Ty, Span)> =
            literal_ids.iter().flat_map(|id| self.sema.handler_breaks.get(id).cloned().unwrap_or_default()).collect();
        breaks.extend(self.label_breaks.remove(&(self.outer.len(), label)).unwrap_or_default());
        let any_break = !breaks.is_empty();
        let mut ty = if value { expect.unwrap_or(body.ty) } else { Ty::unit() };
        // A body that never finishes leaves the value to the `break`s.
        if ty.is_never()
            && let Some((bt, _)) = breaks.iter().find(|(bt, _)| !bt.is_never())
        {
            ty = *bt;
        }
        let body = if value && !body.ty.is_never() { self.coerce(body, ty) } else { body };
        for (bt, bspan) in breaks {
            let bt = self.infer.resolve(bt);
            let agrees = bt == ty || ((bt.has_infer() || ty.has_infer()) && self.infer.unify(bt, ty).is_ok());
            if !bt.is_never() && !bt.is_error() && !ty.is_error() && !agrees {
                self.error(bspan, format!("this `break` gives the `with` a `{}`, but its value is `{}`", bt, ty));
            }
        }
        // A `break` to the `with` can complete it even when its body never does: one in
        // a clause of a handler literal or aimed at its label, or possibly one in a clause
        // of a handler declaration for an `esc` or `ctl` operation.
        let decl_exits = handlers.iter().any(|h| match h {
            tir::HandlerValue::Decl { decls, .. } => decls.iter().any(|d| {
                let e = &self.sema.prog.handler_decls[d.index()].effect;
                self.sema.effects[&e.effect].ops.iter().any(|o| o.kind != ast::OpKind::Fn)
            }),
            tir::HandlerValue::Literal(_) => false,
        });
        let clause_exits = any_break || decl_exits;
        // Clauses of handler declarations are checked after function bodies, so their
        // `break`s are checked against this `with` later (`check_decl_breaks`).
        let decl_ids: Vec<usize> = handlers
            .iter()
            .flat_map(|h| match h {
                tir::HandlerValue::Decl { decls, .. } => decls.iter().map(|d| d.index()).collect(),
                tir::HandlerValue::Literal(_) => vec![],
            })
            .collect();
        let with_ty = self.infer.resolve(ty);
        if !decl_ids.is_empty() && !with_ty.has_infer() && !with_ty.is_error() {
            self.sema.decl_with_checks.push((decl_ids, with_ty, span));
        }
        let ty = if !value && body.ty.is_never() && !clause_exits { Ty::never() } else { ty };
        mk(ty, tir::ExprKind::With { label, handlers, on, body: Box::new(body) }, span)
    }

    /// Whether the current frame is the body of a `ctl` clause.
    fn in_ctl_clause(&self) -> bool {
        self.clause.as_ref().is_some_and(|c| c.op_kind == ast::OpKind::Ctl && c.for_label.is_none())
    }

    /// Checks a clause body against the clause's result type. A `ctl` clause never
    /// finishes normally (§14.3).
    pub fn clause_body(&mut self, body: Expr, kind: ast::OpKind, ret: Ty) -> Expr {
        if body.ty.is_never() {
            return body;
        }
        if kind == ast::OpKind::Ctl {
            if !body.ty.is_error() {
                self.error(
                    body.span,
                    "a `ctl` clause must not finish normally: end every path with `resume`, an exit or a `never` call (§14.3)",
                );
            }
            return body;
        }
        self.coerce(body, ret)
    }

    /// Checks a continuation operand of `resume` or `cancel`; returns it and its `R`.
    fn cont_operand(&mut self, what: &str, k: &ast::Expr) -> Option<(Expr, Ty)> {
        if !self.in_ctl_clause() {
            self.error(k.span, format!("`{}` is permitted only in the body of a `ctl` clause (§14)", what));
        } else if self.in_defer {
            self.error(k.span, format!("`{}` cannot appear in a deferred statement", what));
        }
        let v = self.expr(k, None);
        let t = self.infer.resolve(v.ty);
        match t.kind() {
            TyKind::Cont(r) => Some((v, *r)),
            TyKind::Error => None,
            _ => {
                self.error(k.span, format!("expected a continuation `cont(R)`, found `{}`", t));
                None
            }
        }
    }

    /// `resume k(v)` (§14.3).
    pub fn resume_expr(&mut self, k: &ast::Expr, value: Option<&ast::Expr>, span: Span) -> Expr {
        let Some((k, r)) = self.cont_operand("resume", k) else {
            if let Some(v) = value {
                self.expr(v, None);
            }
            return error_expr(span);
        };
        let v = match value {
            Some(v) => {
                let x = self.expr(v, Some(r));
                self.coerce(x, r)
            }
            None => {
                if !r.is_unit() && !r.is_error() {
                    self.error(span, format!("this continuation needs a value of type `{}`: `resume k(value)`", r));
                }
                super::func::unit(span)
            }
        };
        mk(Ty::never(), tir::ExprKind::Resume { cont: Box::new(k), value: Box::new(v) }, span)
    }

    /// `cancel k` (§14.5).
    pub fn cancel_stmt(&mut self, k: &ast::Expr, span: Span) -> Expr {
        match self.cont_operand("cancel", k) {
            Some((k, _)) => mk(Ty::unit(), tir::ExprKind::Cancel(Box::new(k)), span),
            None => error_expr(span),
        }
    }

    /// `@fiber(f, arg, stack)` (§14.6).
    pub fn fiber_intrinsic(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        if !self.in_ctl_clause() || self.in_defer {
            self.error(span, "`@fiber` may be used only in the body of a `ctl` clause (§14.6)");
        }
        let positional: Vec<&ast::Expr> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Positional(e) => Some(e),
                _ => None,
            })
            .collect();
        let [f, arg, stack] = positional.as_slice() else {
            self.error(span, "`@fiber` takes a function pointer, its argument and a stack: `@fiber(f, arg, stack)`");
            return error_expr(span);
        };
        let fv = self.expr(f, None);
        let ft = self.infer.resolve(fv.ty);
        let sig = match ft.kind() {
            TyKind::FnPtr(sig) if sig.params.len() == 1 && sig.ret.is_never() && sig.callconv == CallConv::C => {
                sig.clone()
            }
            TyKind::Error => return error_expr(span),
            _ => {
                self.error(
                    f.span,
                    format!("`@fiber` needs a function pointer `^fn(a: A) -> never \\ {{…}}` with the C calling convention, found `{}`", ft),
                );
                return error_expr(span);
            }
        };
        let a = sig.params[0];
        if matches!(a.kind(), TyKind::Fn(_) | TyKind::Handler(_)) {
            self.error(arg.span, format!("a fiber's argument must have a first-class type, not `{}` (§14.6)", a));
        }
        let av = self.expr(arg, Some(a));
        let av = self.coerce(av, a);
        let stack_ty = Ty::intern(TyKind::Slice { elem: Ty::int(false, 8), is_const: false });
        let sv = self.expr(stack, Some(stack_ty));
        let sv = self.coerce(sv, stack_ty);
        // The fiber runs in the context of the `with`'s body: its effects other than the
        // clause's own are required from outside the `with` (§14.6).
        let own = self.clause.as_ref().and_then(|c| c.effect.clone());
        let rest: Vec<EffectRef> = sig.effects.effects.iter().filter(|e| Some(*e) != own.as_ref()).cloned().collect();
        self.record_effects(&EffectSet::new(rest), span);
        mk(
            Ty::intern(TyKind::Cont(Ty::unit())),
            tir::ExprKind::Builtin { op: tir::Builtin::Fiber, args: vec![fv, av, sv] },
            span,
        )
    }

    /// `for p in f(args) { B }` where `f` is an iterator (§13.2).
    pub fn for_iterator(&mut self, f: &ast::ForExpr) -> Expr {
        let span = f.span;
        let Some(yield_eff) = self.sema.yield_effect else {
            self.error(span, "the `Yield` effect is not available");
            return error_expr(span);
        };
        // The source call is evaluated as written, outside the loop's handler.
        let start = self.performed.len();
        let call = self.expr(&f.iter, None);
        // A call with spread or reordered arguments binds some of them first.
        let (bound, call) = match call.kind {
            tir::ExprKind::Block(b) if matches!(b.tail.as_ref().map(|t| &t.kind), Some(tir::ExprKind::Call { .. })) => {
                let b = *b;
                (b.stmts, b.tail.unwrap())
            }
            kind => (vec![], Expr { kind, ..call }),
        };
        let callee_effects = match &call.kind {
            tir::ExprKind::Call { callee: tir::Callee::Fn(id), .. } => self.sema.sigs[id].effects.clone(),
            tir::ExprKind::Call { callee: tir::Callee::Indirect(c), .. } => match self.infer.resolve(c.ty).kind() {
                TyKind::Fn(s) | TyKind::FnPtr(s) => s.effects.clone(),
                _ => EffectSet::empty(),
            },
            _ => {
                // A call of an effect operation returning an array or slice is iterated like any such value.
                if let Some(e) = self.for_each_value(f, call.clone()) {
                    return e;
                }
                if !call.ty.is_error() {
                    self.error(f.iter.span, "`for` iterates over a range, an array or slice, or a call to an iterator");
                }
                return error_expr(span);
            }
        };
        let yields: Vec<EffectRef> = callee_effects.effects.iter().filter(|e| e.effect == yield_eff).cloned().collect();
        let eref = match yields.as_slice() {
            [one] => one.clone(),
            [] => {
                // A call returning an array or slice is iterated like any such value.
                if let Some(e) = self.for_each_value(f, call) {
                    return e;
                }
                self.error(f.iter.span, "this call is not an iterator: its effects include no `Yield(T)`");
                return error_expr(span);
            }
            _ => {
                self.error(f.iter.span, "this call yields more than one type; `for` needs exactly one `Yield(T)`");
                return error_expr(span);
            }
        };
        let elem = match eref.args.first() {
            Some(GenericArg::Type(t)) => *t,
            _ => Ty::error(),
        };
        // The loop handles the iterator's `Yield(T)`.
        let mut i = start;
        while i < self.performed.len() {
            if self.performed[i].0 == eref {
                self.performed.remove(i);
            } else {
                i += 1;
            }
        }
        let result_ty = call.ty;
        // Bind the arguments first so they are evaluated outside the loop's handler.
        let (hoisted, call) = self.hoist_args(call);
        let pre: Vec<Stmt> = bound.into_iter().chain(hoisted).collect();
        self.push_scope();
        if let Some(l) = f.label {
            self.declare(l.name, LocalBinding::Label, l.span);
        }
        let label = self.new_label(f.label.map(|l| l.name), true, LabelValue::Loop(result_ty));
        let hid = HandlerId(self.sema.prog.handlers.len() as u32);
        self.sema.prog.handlers.push(tir::HandlerLit { effect: eref.clone(), clauses: vec![] });
        // The clause `yield(x) => { p :: x; B }`.
        let x = Symbol::intern("<item>");
        let params = vec![ParamInfo { name: x, ty: elem, span, default: None, generic: None, rest: false }];
        let sig = FnSig {
            module: self.module,
            params: params.clone(),
            ret: Ty::unit(),
            effects: EffectSet::empty(),
            generics: vec![],
        };
        let fn_id = self.sema.new_fn(Symbol::intern("<for body>"), span, sig, tir::FnKind::Clause);
        self.enter_fn(fn_id, Ty::unit(), EffectSet::empty());
        self.clause = Some(ClauseInfo {
            op_kind: ast::OpKind::Esc,
            for_label: Some((f.label.map(|l| l.name), label)),
            handler: Some(hid),
            break_ty: Some(result_ty),
            effect: Some(eref.clone()),
            decl_clause: None,
            with_expect: None,
        });
        let xid = self.new_local(x, elem, false, span);
        let pat_ast = if f.pats.len() == 1 {
            f.pats[0].clone()
        } else {
            let args = f.pats.iter().map(|p| ast::PatArg { name: None, pat: p.clone(), span: p.span }).collect();
            ast::Pattern { id: f.pats[0].id, kind: ast::PatternKind::Tuple(args), span: f.pats[0].span }
        };
        let mut binds = vec![];
        let pat = self.pattern(&pat_ast, elem, PatCtx::Irrefutable, &mut binds);
        if !elem.is_error() && !self.checker().irrefutable(&pat) {
            self.error(pat_ast.span, "a `for` pattern must match every value (it must be irrefutable)");
        }
        self.declare_binds(binds);
        let body = self.block(&f.body, false);
        let scrut = mk(elem, tir::ExprKind::Local(xid), span);
        let m = mk(
            Ty::unit(),
            tir::ExprKind::Match { scrutinee: Box::new(scrut), arms: vec![tir::Arm { pat, guard: None, body }] },
            span,
        );
        self.finish_clause(fn_id, vec![xid], m);
        self.labels.pop();
        self.label_breaks.remove(&(self.outer.len(), label));
        self.pop_scope();
        self.sema.prog.handlers[hid.index()].clauses = vec![fn_id];
        let handler = mk(Ty::intern(TyKind::Handler(hid.0)), tir::ExprKind::HandlerRef(hid), span);
        let with = mk(
            result_ty,
            tir::ExprKind::With {
                label,
                handlers: vec![tir::HandlerValue::Literal(handler)],
                on: None,
                body: Box::new(call),
            },
            span,
        );
        if pre.is_empty() {
            return with;
        }
        mk(result_ty, tir::ExprKind::Block(Box::new(tir::Block { stmts: pre, tail: Some(with), span })), span)
    }

    /// Moves non-constant call arguments into temporaries evaluated before the call.
    fn hoist_args(&mut self, mut call: Expr) -> (Vec<Stmt>, Expr) {
        let mut pre = vec![];
        if let tir::ExprKind::Call { args, .. } = &mut call.kind {
            for a in args.iter_mut() {
                if matches!(a.kind, tir::ExprKind::Int(_) | tir::ExprKind::Str(_) | tir::ExprKind::Bool(_)) {
                    continue;
                }
                // A temporary the argument points to (`&mk()`, or a temporary array passed
                // as a slice) lives for the call, which includes the loop (§7.2): it becomes
                // a local too, rather than dying with the statement that binds the argument.
                self.lift_temps(a, &mut pre);
                let span = a.span;
                let t = self.temp(a.ty, span);
                let v = std::mem::replace(a, mk(a.ty, tir::ExprKind::Local(t), span));
                pre.push(Stmt::Let { local: t, init: Some(v) });
            }
        }
        (pre, call)
    }

    /// Replaces each `AddrOfTemp(v)` in `e` (outside nested calls) by the address of a
    /// local initialized with `v` by a statement added to `pre`.
    pub(super) fn lift_temps(&mut self, e: &mut Expr, pre: &mut Vec<Stmt>) {
        if matches!(e.kind, tir::ExprKind::Call { .. } | tir::ExprKind::Closure(_)) {
            return;
        }
        if let tir::ExprKind::AddrOfTemp(inner) = &mut e.kind {
            self.lift_temps(inner, pre);
            let (span, ty) = (inner.span, inner.ty);
            let t = self.temp(ty, span);
            let v = std::mem::replace(&mut **inner, mk(ty, tir::ExprKind::Local(t), span));
            pre.push(Stmt::Let { local: t, init: Some(v) });
            e.kind = tir::ExprKind::AddrOf(Box::new(mk(ty, tir::ExprKind::Local(t), span)));
            return;
        }
        e.for_each_child_mut(&mut |c| self.lift_temps(c, pre));
    }
}
