//! Effects and handlers (§12): performing operations, handler literals,
//! `with`, and `for` over iterator calls (§13.2).

use super::func::{ClauseInfo, FnCx, LocalBinding, NestedFn, error_expr};
use super::pattern::PatCtx;
use super::{FnSig, ParamInfo};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind, ExprKind};
use crate::tir::{self, Expr, HandlerId, Stmt};
use crate::types::{EffectId, EffectRef, EffectSet, GenericArg, ParamId, Ty, TyKind};
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
fn match_params(pat: Ty, actual: Ty, binds: &mut HashMap<ParamId, Ty>) {
    if actual.has_infer() || actual.is_error() {
        return;
    }
    match (pat.kind(), actual.kind()) {
        (TyKind::Param(p), _) => {
            binds.entry(*p).or_insert(actual);
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
        (TyKind::Tuple { elems: x, .. }, TyKind::Tuple { elems: y, .. }) if x.len() == y.len() => {
            for (a, b) in x.iter().zip(y.iter()) {
                match_params(*a, *b, binds);
            }
        }
        _ => {}
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
            let mut binds = HashMap::new();
            let mut probe = vec![];
            for (i, e) in positional.iter().enumerate() {
                let v = self.expr(e, None);
                if let Some(p) = op_info.params.get(i) {
                    let t = self.infer.resolve(v.ty);
                    match_params(p.ty, t, &mut binds);
                }
                probe.push(v);
            }
            let eargs: Option<Vec<GenericArg>> =
                params.iter().map(|p| binds.get(p).map(|t| GenericArg::Type(*t))).collect();
            let eref = match eargs {
                Some(a) => EffectRef { effect: eff, args: a.into_boxed_slice() },
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
            return mk(
                ret,
                tir::ExprKind::Call { callee: tir::Callee::Perform { effect: eref, op }, args: vals },
                span,
            );
        };
        let module = self.sema.effects[&eff].module;
        let vals = self.bind_args(&op_info.params, module, args, span, "operation");
        self.performed.push((eref.clone(), span));
        mk(op_info.ret, tir::ExprKind::Call { callee: tir::Callee::Perform { effect: eref, op }, args: vals }, span)
    }

    /// The parameter and result types of operation `op` of an instantiated effect.
    pub fn op_signature(&self, eref: &EffectRef, op: usize) -> (Vec<ParamInfo>, Ty, ast::OpKind) {
        let info = &self.sema.effects[&eref.effect];
        let o = &info.ops[op];
        let params = o
            .params
            .iter()
            .map(|p| ParamInfo { name: p.name, ty: subst(p.ty, &info.params, &eref.args), span: p.span, default: None })
            .collect();
        (params, subst(o.ret, &info.params, &eref.args), o.kind)
    }

    /// Checks a handler literal (§12.5), producing a second-class handler value.
    pub fn handler_literal(&mut self, lit: &ast::HandlerLit, span: Span) -> Expr {
        let module = self.module;
        let Some(eref) = self.sema.resolve_effect_ref(module, &lit.effect) else { return error_expr(span) };
        let n_params = self.sema.effects[&eref.effect].params.len();
        if eref.args.len() != n_params {
            self.error(lit.effect.span, format!("`{}` needs {} type argument(s)", eref.effect.name(), n_params));
            return error_expr(span);
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
        let mut clauses = vec![];
        for (i, opname) in ops.iter().enumerate() {
            let Some(c) = lit.clauses.iter().find(|c| c.name.name == *opname) else {
                self.error(span, format!("this handler has no clause for the operation `{}`", opname));
                continue;
            };
            let (params, ret, kind) = self.op_signature(&eref, i);
            let info = ClauseInfo { op_kind: kind, for_label: None, handler: Some(hid), break_ty: None };
            let f = self.clause_fn(c, &params, ret, info);
            clauses.push(f);
        }
        self.sema.prog.handlers[hid.index()].clauses = clauses;
        mk(Ty::intern(TyKind::Handler(hid.0)), tir::ExprKind::HandlerRef(hid), span)
    }

    /// Checks one clause as a nested function of the current one.
    fn clause_fn(&mut self, c: &ast::Clause, params: &[ParamInfo], ret: Ty, info: ClauseInfo) -> tir::FnId {
        if c.params.len() != params.len() {
            self.error(c.span, format!("the clause for `{}` takes {} parameter(s)", c.name.name, params.len()));
        }
        let sig = FnSig { module: self.module, params: params.to_vec(), ret, effects: EffectSet::empty() };
        let fn_id = self.sema.new_fn(c.name.name, c.span, sig, tir::FnKind::Clause);
        self.enter_fn(fn_id, ret, EffectSet::empty());
        self.clause = Some(info);
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
        let body = match &c.body.kind {
            ExprKind::Block(b) => self.block_expected(b, true, Some(ret)),
            _ => self.expr(&c.body, Some(ret)),
        };
        let body = if body.ty.is_never() { body } else { self.coerce(body, ret) };
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
        for h in &w.handlers {
            let v = match &h.kind {
                ExprKind::Handler(lit) => self.handler_literal(lit, h.span),
                _ => self.expr(h, None),
            };
            let t = self.infer.resolve(v.ty);
            match t.kind() {
                TyKind::Handler(id) => {
                    let eref = self.sema.prog.handlers[*id as usize].effect.clone();
                    if handled.contains(&eref) {
                        self.error(h.span, format!("two handlers for `{}` in one `with`", eref.effect.name()));
                    }
                    handled.push(eref);
                    literal_ids.push(*id);
                    handlers.push(tir::HandlerValue::Literal(v));
                }
                TyKind::Error => {}
                _ => {
                    let decls = self.sema.handlers_for(t);
                    if decls.is_empty() {
                        self.error(h.span, format!("expected a handler, but no handler is declared for `{}`", t));
                        continue;
                    }
                    for d in &decls {
                        let eref = self.sema.prog.handler_decls[d.index()].effect.clone();
                        if handled.contains(&eref) {
                            self.error(h.span, format!("two handlers for `{}` in one `with`", eref.effect.name()));
                        }
                        handled.push(eref);
                        // The clauses' effects are performed at the `with`, outside it (§12.6).
                        let effs = self.sema.handler_decl_effects[d.index()].clone();
                        self.record_effects(&effs, h.span);
                    }
                    handlers.push(tir::HandlerValue::Decl { decls, value: v });
                }
            }
        }
        if let Some(on) = &w.on {
            self.error(on.span, "`with … on` (fibers) is not supported yet");
        }
        let label = self.new_label(w.label.map(|l| l.name), false);
        let start = self.performed.len();
        let n = self.handled.len();
        self.handled.extend(handled.iter().cloned());
        let body = self.block_expected(&w.body, value, expect);
        self.handled.truncate(n);
        self.labels.pop();
        self.pop_scope();
        // Effects handled here are not performed by the `with` (§12.3).
        let mut i = start;
        while i < self.performed.len() {
            if handled.contains(&self.performed[i].0) {
                self.performed.remove(i);
            } else {
                i += 1;
            }
        }
        // Values of clause-level `break`s must agree with the body's value (§12.7).
        let ty = if value { expect.unwrap_or(body.ty) } else { Ty::unit() };
        let body = if value && !body.ty.is_never() { self.coerce(body, ty) } else { body };
        for id in literal_ids {
            for (bt, bspan) in self.sema.handler_breaks.get(&id).cloned().unwrap_or_default() {
                let bt = self.infer.resolve(bt);
                if !bt.is_never() && !ty.is_error() && bt != ty && !(bt.has_infer() && self.infer.unify(bt, ty).is_ok())
                {
                    self.error(bspan, format!("this `break` gives the `with` a `{}`, but its value is `{}`", bt, ty));
                }
            }
        }
        let ty = if !value && body.ty.is_never() { Ty::never() } else { ty };
        mk(ty, tir::ExprKind::With { label, handlers, on: None, body: Box::new(body) }, span)
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
        let callee_effects = match &call.kind {
            tir::ExprKind::Call { callee: tir::Callee::Fn(id), .. } => self.sema.sigs[id].effects.clone(),
            tir::ExprKind::Call { callee: tir::Callee::Indirect(c), .. } => match self.infer.resolve(c.ty).kind() {
                TyKind::Fn(s) | TyKind::FnPtr(s) => s.effects.clone(),
                _ => EffectSet::empty(),
            },
            _ => {
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
        let (pre, call) = self.hoist_args(call);
        self.push_scope();
        if let Some(l) = f.label {
            self.declare(l.name, LocalBinding::Label, l.span);
        }
        let label = self.new_label(f.label.map(|l| l.name), true);
        let hid = HandlerId(self.sema.prog.handlers.len() as u32);
        self.sema.prog.handlers.push(tir::HandlerLit { effect: eref.clone(), clauses: vec![] });
        // The clause `yield(x) => { p :: x; B }`.
        let x = Symbol::intern("<item>");
        let params = vec![ParamInfo { name: x, ty: elem, span, default: None }];
        let sig = FnSig { module: self.module, params: params.clone(), ret: Ty::unit(), effects: EffectSet::empty() };
        let fn_id = self.sema.new_fn(Symbol::intern("<for body>"), span, sig, tir::FnKind::Clause);
        self.enter_fn(fn_id, Ty::unit(), EffectSet::empty());
        self.clause = Some(ClauseInfo {
            op_kind: ast::OpKind::Esc,
            for_label: Some((f.label.map(|l| l.name), label)),
            handler: Some(hid),
            break_ty: Some(result_ty),
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
                let span = a.span;
                let t = self.temp(a.ty, span);
                let v = std::mem::replace(a, mk(a.ty, tir::ExprKind::Local(t), span));
                pre.push(Stmt::Let { local: t, init: Some(v) });
            }
        }
        (pre, call)
    }
}
