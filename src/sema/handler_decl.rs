//! Handler declarations `handler E for T needs A { … }` (§12.6).

use super::func::{ClauseInfo, FnCx, LocalBinding};
use super::{FnSig, ModuleId, ParamInfo, Sema};
use crate::diag::Diagnostic;
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind, ItemKind};
use crate::tir::{self, HandlerDeclId};
use crate::types::{EffectRef, EffectSet, ParamId, Ty, TyKind};
use std::rc::Rc;

/// A handler declaration for a generic type: `handler(T: type) E for X(T) { … }`.
pub struct HandlerTemplate {
    pub generics: Vec<ParamId>,
    pub effect: EffectRef,
    pub for_ty: Ty,
    pub effects: EffectSet,
    pub clauses: Vec<tir::FnId>,
    pub span: Span,
}

/// A parameter of a generic handler declaration in scope: a type parameter, or a
/// value parameter of the given type.
#[derive(Clone)]
struct ScopeParam {
    name: Symbol,
    pid: ParamId,
    value_ty: Option<Ty>,
}

/// A clause of a declared handler, waiting for its body to be checked.
pub struct PendingClause {
    module: ModuleId,
    item: Rc<ast::Item>,
    clause: usize,
    fn_id: tir::FnId,
    effects: EffectSet,
    params: Vec<ParamInfo>,
    ret: Ty,
    kind: ast::OpKind,
    effect: EffectRef,
    scope: Vec<ScopeParam>,
}

impl<'s> Sema<'s> {
    /// Registers the handler declarations not registered yet, so `with` can find
    /// them; their clauses are checked once all signatures are known, or earlier when
    /// a compile-time evaluation reaches them.
    pub(super) fn ensure_handlers_declared(&mut self) {
        if !self.handler_items.is_empty() {
            let clauses = self.declare_handlers();
            self.pending_clauses.extend(clauses);
        }
    }

    /// Checks the clause of a handler declaration with function `f` now if it is
    /// still pending; returns whether it was.
    pub(super) fn check_pending_clause(&mut self, f: tir::FnId) -> bool {
        let Some(i) = self.pending_clauses.iter().position(|p| p.fn_id == f) else { return false };
        let p = self.pending_clauses.remove(i);
        let outer = self.enter_module_scope();
        self.check_handler_clauses(vec![p]);
        self.leave_module_scope(outer);
        true
    }

    fn declare_handlers(&mut self) -> Vec<PendingClause> {
        let mut pending = vec![];
        for (module, item) in std::mem::take(&mut self.handler_items) {
            let ItemKind::Handler(h) = &item.kind else { continue };
            // The parameters of a handler for a generic type (§12.6).
            let scope = self.handler_params(module, h.params.as_deref().unwrap_or(&[]));
            let saved = self.push_scope_params(&scope);
            let eref = self.resolve_effect_ref(module, &h.effect);
            let for_ty = self.resolve_type(module, &h.for_ty);
            let effects = match &h.effects {
                Some(list) => self.resolve_effect_set(module, list),
                None => EffectSet::empty(),
            };
            self.pop_scope_params(saved);
            let Some(eref) = eref else { continue };
            if for_ty.is_error() {
                continue;
            }
            let generics: Vec<ParamId> = scope.iter().map(|p| p.pid).collect();
            if !generics.is_empty() {
                let mut seen = vec![];
                for_ty.walk(&mut |t| {
                    if let TyKind::Param(p) = t.kind() {
                        seen.push(*p);
                    }
                });
                if let Some(p) = scope.iter().find(|p| !seen.contains(&p.pid)) {
                    self.error(
                        h.span,
                        format!("the parameter `{}` must appear in the type the handler is for", p.name),
                    );
                    continue;
                }
            }
            // No type may match two declarations of one effect (§12.6): neither two for
            // the same type, nor a specific one that a generic one also matches.
            let duplicate = if generics.is_empty() {
                self.prog.handler_decls.iter().any(|d| d.effect == eref && d.for_ty == for_ty)
                    || self.template_matches(&eref, for_ty)
            } else {
                let concrete: Vec<(EffectRef, Ty)> =
                    self.prog.handler_decls.iter().map(|d| (d.effect.clone(), d.for_ty)).collect();
                let tpl = HandlerTemplate {
                    generics: generics.clone(),
                    effect: eref.clone(),
                    for_ty,
                    effects: EffectSet::empty(),
                    clauses: vec![],
                    span: h.span,
                };
                concrete.into_iter().any(|(e, t)| self.template_instance_is(&tpl, &e, t))
                    || self.handler_templates.iter().any(|t| {
                        t.effect.effect == eref.effect
                            && may_unify(t.for_ty, for_ty)
                            && t.effect.args.len() == eref.args.len()
                            && t.effect.args.iter().zip(eref.args.iter()).all(|(a, b)| arg_may_unify(a, b))
                    })
            };
            if duplicate {
                let d = Diagnostic::error(
                    h.span,
                    format!(
                        "a handler of `{}` that applies to `{}` is already declared; a type may match only one declaration of an effect",
                        eref.effect.name(),
                        for_ty
                    ),
                );
                self.emit(d);
                continue;
            }
            let ops: Vec<Symbol> = self.effects[&eref.effect].ops.iter().map(|o| o.name).collect();
            for c in &h.clauses {
                if !ops.contains(&c.name.name) {
                    self.error(c.name.span, format!("`{}` has no operation `{}`", eref.effect.name(), c.name.name));
                }
            }
            let mut clause_ids = vec![];
            for (i, op) in ops.iter().enumerate() {
                let Some(ci) = h.clauses.iter().position(|c| c.name.name == *op) else {
                    self.error(h.span, format!("this handler has no clause for the operation `{}`", op));
                    continue;
                };
                let (mut params, ret, kind) = self.clause_signature(&eref, i);
                let value_params = params.len() + 1;
                // The first clause parameter is the handler value itself.
                params.insert(
                    0,
                    ParamInfo {
                        name: Symbol::intern("self"),
                        ty: for_ty,
                        span: h.for_ty.span,
                        default: None,
                        generic: None,
                        rest: false,
                    },
                );
                // A generic handler's clauses are generic functions of its parameters.
                let mut all = scope_param_infos(&scope, h.span);
                all.extend(params.iter().cloned());
                debug_assert_eq!(params.len(), value_params);
                let sig = FnSig { module, params: all, ret, effects: effects.clone(), generics: generics.clone() };
                let fn_id = self.new_fn(*op, h.clauses[ci].span, sig, tir::FnKind::Clause);
                clause_ids.push(fn_id);
                pending.push(PendingClause {
                    module,
                    item: item.clone(),
                    clause: ci,
                    fn_id,
                    effects: effects.clone(),
                    params,
                    ret,
                    kind,
                    effect: eref.clone(),
                    scope: scope.clone(),
                });
            }
            if generics.is_empty() {
                self.prog.handler_decls.push(tir::HandlerDecl { effect: eref, for_ty, clauses: clause_ids });
                self.handler_decl_effects.push(effects);
            } else {
                self.handler_templates.push(HandlerTemplate {
                    generics,
                    effect: eref,
                    for_ty,
                    effects,
                    clauses: clause_ids,
                    span: h.span,
                });
            }
        }
        pending
    }

    /// A `break` in a clause of a handler declaration exits the `with` that installed
    /// the handler, so its value must have that `with`'s type (§12.7).
    pub(super) fn check_decl_breaks(&mut self) {
        let mut errors = vec![];
        for (decls, ty, with_span) in std::mem::take(&mut self.decl_with_checks) {
            for d in decls {
                for f in &self.prog.handler_decls[d].clauses {
                    for (bt, span) in self.decl_clause_breaks.get(f).into_iter().flatten() {
                        if *bt != ty && !bt.is_never() && !bt.is_error() && !errors.iter().any(|(s, _, _)| s == span) {
                            errors.push((
                                *span,
                                format!(
                                    "this `break` gives a `with` a `{}`, but the `with` has the type `{}` (§12.7)",
                                    bt, ty
                                ),
                                with_span,
                            ));
                        }
                    }
                }
            }
        }
        for (span, msg, with_span) in errors {
            let d =
                crate::diag::Diagnostic::error(span, msg).with_label(with_span, "the `with` that installs the handler");
            self.emit(d);
        }
    }

    /// Checks the bodies of declared handlers' clauses.
    pub(super) fn check_handler_clauses(&mut self, pending: Vec<PendingClause>) {
        for p in pending {
            let ItemKind::Handler(h) = &p.item.kind else { continue };
            let c = &h.clauses[p.clause];
            let saved = self.push_scope_params(&p.scope);
            let mut cx = FnCx::new(self, p.module, p.ret, p.effects.clone());
            // Literals and loop bodies in the clause capture its frame.
            cx.fn_id = Some(p.fn_id);
            cx.clause = Some(ClauseInfo {
                op_kind: p.kind,
                for_label: None,
                handler: None,
                break_ty: None,
                effect: Some(p.effect.clone()),
                decl_clause: Some(p.fn_id),
                with_expect: None,
            });
            if c.params.len() != p.params.len() {
                cx.error(
                    c.span,
                    format!(
                        "the clause for `{}` takes {} parameters (the first is the handler value)",
                        c.name.name,
                        p.params.len()
                    ),
                );
            }
            let mut ids = vec![];
            for (i, param) in p.params.iter().enumerate() {
                let (name, span) = c.params.get(i).map_or((param.name, c.span), |n| (n.name, n.span));
                let id = cx.new_local(name, param.ty, false, span);
                if name.as_str() != "_" {
                    cx.declare(name, LocalBinding::Var(id), span);
                }
                ids.push(id);
            }
            // A `ctl` clause must not finish normally (§14.3); see `clause_fn`.
            let expect = (p.kind != ast::OpKind::Ctl).then_some(p.ret);
            let body = match &c.body.kind {
                ExprKind::Block(b) => cx.block_expected(b, true, expect),
                _ => cx.expr(&c.body, expect),
            };
            let body = cx.clause_body(body, p.kind, p.ret);
            // The types of untyped literals given to `break` are known now.
            let breaks = cx.sema.decl_clause_breaks.remove(&p.fn_id).unwrap_or_default();
            let breaks = breaks.into_iter().map(|(t, s)| (cx.settle(t, s), s)).collect();
            cx.sema.decl_clause_breaks.insert(p.fn_id, breaks);
            let (body, locals) = cx.finish(body);
            let def = &mut self.prog.fns[p.fn_id.index()];
            def.params = ids;
            def.locals = locals;
            def.body = Some(body);
            self.pop_scope_params(saved);
        }
    }

    /// The parameters of a generic handler declaration (all compile-time, as for a
    /// generic type: `T: type` or `N: usize`).
    fn handler_params(&mut self, module: ModuleId, params: &[ast::Param]) -> Vec<ScopeParam> {
        let mut out = vec![];
        for p in params {
            let is_effects = p.ty.as_ref().is_some_and(super::decls::is_effects_kind);
            let is_type = is_effects
                || p.ty.as_ref().is_some_and(|t| matches!(&t.kind, ExprKind::Ident(i) if i.name.as_str() == "type"));
            let value_ty = match &p.ty {
                Some(_) if is_type => None,
                Some(t) => Some(self.resolve_type(module, t)),
                None => {
                    self.error(p.span, "a handler parameter needs a type: `T: type` or `N: usize`");
                    continue;
                }
            };
            for n in &p.names {
                let pid = ParamId::fresh(n.name);
                if let Some(t) = value_ty {
                    self.value_params.insert(pid, t);
                }
                if is_effects {
                    self.effect_params.insert(pid);
                }
                out.push(ScopeParam { name: n.name, pid, value_ty });
            }
        }
        out
    }

    /// Brings handler parameters into scope; returns what `pop_scope_params` restores.
    fn push_scope_params(&mut self, scope: &[ScopeParam]) -> (usize, usize) {
        let saved = (self.type_params.len(), self.value_scope.len());
        for p in scope {
            match p.value_ty {
                Some(_) => self.value_scope.push((p.name, p.pid)),
                None => self.type_params.push((p.name, Ty::intern(TyKind::Param(p.pid)))),
            }
        }
        saved
    }

    fn pop_scope_params(&mut self, saved: (usize, usize)) {
        self.type_params.truncate(saved.0);
        self.value_scope.truncate(saved.1);
    }

    /// The parameters and result type of a clause for operation `op` of an instantiated
    /// effect, and the operation's kind. A `ctl` clause takes the continuation as a final
    /// parameter `k: cont(R)` and has type `never` (§14.2, §14.3).
    pub fn clause_signature(&mut self, eref: &EffectRef, op: usize) -> (Vec<ParamInfo>, Ty, ast::OpKind) {
        let info = &self.effects[&eref.effect];
        let o = info.ops[op].clone();
        let info_params = info.params.clone();
        let mut params: Vec<ParamInfo> = o
            .params
            .iter()
            .map(|p| ParamInfo {
                name: p.name,
                ty: self.subst_effect_args(p.ty, &info_params, &eref.args),
                span: p.span,
                default: None,
                generic: None,
                rest: false,
            })
            .collect();
        let ret = self.subst_effect_args(o.ret, &info_params, &eref.args);
        if o.kind != ast::OpKind::Ctl {
            return (params, ret, o.kind);
        }
        let span = params.last().map_or(Span::DUMMY, |p| p.span);
        params.push(ParamInfo {
            name: Symbol::intern("k"),
            ty: Ty::intern(TyKind::Cont(ret)),
            span,
            default: None,
            generic: None,
            rest: false,
        });
        (params, Ty::never(), o.kind)
    }

    /// The handler declarations for values of type `t`, instantiating those for
    /// generic types that `t` matches.
    pub fn handlers_for(&mut self, t: Ty) -> Vec<HandlerDeclId> {
        self.ensure_handlers_declared();
        let mut out: Vec<HandlerDeclId> = (0..self.prog.handler_decls.len())
            .filter(|&i| self.prog.handler_decls[i].for_ty == t)
            .map(|i| HandlerDeclId(i as u32))
            .collect();
        for k in 0..self.handler_templates.len() {
            let (for_ty, generics, effect) = {
                let tpl = &self.handler_templates[k];
                (tpl.for_ty, tpl.generics.clone(), tpl.effect.effect)
            };
            let mut binds = std::collections::HashMap::new();
            super::effects::match_params_pub(for_ty, t, &mut binds);
            let Some(args) = generics.iter().map(|p| binds.get(p).copied()).collect::<Option<Vec<Ty>>>() else {
                continue;
            };
            let map: Vec<(ParamId, Ty)> = generics.iter().copied().zip(args.iter().copied()).collect();
            if self.subst(for_ty, &map) != t {
                continue;
            }
            // Declarations never overlap (§12.6), so this only skips a repeated match.
            if out.iter().any(|d| self.prog.handler_decls[d.index()].effect.effect == effect) {
                continue;
            }
            if let Some(&d) = self.handler_instances.get(&(k, args.clone())) {
                out.push(d);
                continue;
            }
            let (eref, effects, clauses, span) = {
                let tpl = &self.handler_templates[k];
                (
                    super::generics::subst_eref(&tpl.effect, &map),
                    super::generics::subst_effects(&tpl.effects, &map),
                    tpl.clauses.clone(),
                    tpl.span,
                )
            };
            let clauses = clauses.into_iter().map(|c| self.instantiate(c, args.clone(), span)).collect();
            let id = HandlerDeclId(self.prog.handler_decls.len() as u32);
            self.prog.handler_decls.push(tir::HandlerDecl { effect: eref, for_ty: t, clauses });
            self.handler_decl_effects.push(effects);
            self.handler_instances.insert((k, args), id);
            out.push(id);
        }
        out
    }
}

impl Sema<'_> {
    /// True if a generic declaration of the same effect also matches the specific
    /// declaration `handler eref for t`.
    fn template_matches(&mut self, eref: &EffectRef, t: Ty) -> bool {
        let tpls: Vec<HandlerTemplate> = self
            .handler_templates
            .iter()
            .map(|x| HandlerTemplate {
                generics: x.generics.clone(),
                effect: x.effect.clone(),
                for_ty: x.for_ty,
                effects: EffectSet::empty(),
                clauses: vec![],
                span: x.span,
            })
            .collect();
        tpls.iter().any(|tpl| self.template_instance_is(tpl, eref, t))
    }

    /// True if instantiating `tpl` for the type `t` gives a handler of `eref`.
    fn template_instance_is(&mut self, tpl: &HandlerTemplate, eref: &EffectRef, t: Ty) -> bool {
        if tpl.effect.effect != eref.effect {
            return false;
        }
        let mut binds = std::collections::HashMap::new();
        super::effects::match_params_pub(tpl.for_ty, t, &mut binds);
        let Some(args) = tpl.generics.iter().map(|p| binds.get(p).copied()).collect::<Option<Vec<Ty>>>() else {
            return false;
        };
        let map: Vec<(ParamId, Ty)> = tpl.generics.iter().copied().zip(args).collect();
        self.subst(tpl.for_ty, &map) == t && super::generics::subst_eref(&tpl.effect, &map) == *eref
    }
}

/// Whether two types of generic handler declarations could be the same type for some
/// arguments. A parameter matches anything; the check over-approximates when one
/// parameter occurs twice.
fn may_unify(a: Ty, b: Ty) -> bool {
    if a == b {
        return true;
    }
    match (a.kind(), b.kind()) {
        (TyKind::Param(_), _) | (_, TyKind::Param(_)) => true,
        (TyKind::ParamArray { elem: x, .. }, TyKind::ParamArray { elem: y, .. })
        | (TyKind::ParamArray { elem: x, .. }, TyKind::Array { elem: y, .. })
        | (TyKind::Array { elem: x, .. }, TyKind::ParamArray { elem: y, .. }) => may_unify(*x, *y),
        (TyKind::Array { len: m, elem: x, kind: i }, TyKind::Array { len: n, elem: y, kind: j }) => {
            m == n && i == j && may_unify(*x, *y)
        }
        (TyKind::Ptr { is_const: c, pointee: x }, TyKind::Ptr { is_const: d, pointee: y })
        | (TyKind::Slice { is_const: c, elem: x }, TyKind::Slice { is_const: d, elem: y })
        | (TyKind::ManyPtr { is_const: c, elem: x }, TyKind::ManyPtr { is_const: d, elem: y }) => {
            c == d && may_unify(*x, *y)
        }
        (TyKind::Optional(x), TyKind::Optional(y)) | (TyKind::Cont(x), TyKind::Cont(y)) => may_unify(*x, *y),
        (TyKind::Result { err: e1, ok: o1 }, TyKind::Result { err: e2, ok: o2 }) => {
            may_unify(*e1, *e2) && may_unify(*o1, *o2)
        }
        (TyKind::Tuple { elems: x, names: n }, TyKind::Tuple { elems: y, names: m }) => {
            n == m && x.len() == y.len() && x.iter().zip(y.iter()).all(|(a, b)| may_unify(*a, *b))
        }
        (TyKind::Adt(a, x), TyKind::Adt(b, y)) => {
            a.template() == b.template()
                && x.len() == y.len()
                && x.iter().zip(y.iter()).all(|(p, q)| arg_may_unify(p, q))
        }
        // Function types may differ only in parts this check does not look into.
        (TyKind::Fn(_), TyKind::Fn(_)) | (TyKind::FnPtr(_), TyKind::FnPtr(_)) => true,
        _ => false,
    }
}

fn arg_may_unify(a: &crate::types::GenericArg, b: &crate::types::GenericArg) -> bool {
    use crate::types::GenericArg;
    match (a, b) {
        (GenericArg::Type(x), GenericArg::Type(y)) => may_unify(*x, *y),
        (GenericArg::Value(x), GenericArg::Value(y)) => x == y,
        (GenericArg::Type(t), GenericArg::Value(_)) | (GenericArg::Value(_), GenericArg::Type(t)) => {
            matches!(t.kind(), TyKind::Param(_))
        }
    }
}

/// The parameter list entries of a generic handler's parameters, which lead the
/// signature of each of its clauses.
fn scope_param_infos(scope: &[ScopeParam], span: Span) -> Vec<ParamInfo> {
    scope
        .iter()
        .map(|p| ParamInfo {
            name: p.name,
            ty: p.value_ty.unwrap_or_else(Ty::type_),
            span,
            default: None,
            generic: Some(p.pid),
            rest: false,
        })
        .collect()
}
