//! Handler declarations `handler E for T \ {A} { … }` (§12.6).

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
    /// Registers every handler declaration so `with` can find them, and returns
    /// their clauses for checking once all signatures are known.
    pub(super) fn declare_handlers(&mut self) -> Vec<PendingClause> {
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
            // Two generic declarations for the same type shape would both apply.
            let shape = |sema: &mut Self, t: Ty, ps: &[ParamId]| {
                let map: Vec<(ParamId, Ty)> = ps.iter().map(|p| (*p, Ty::unit())).collect();
                sema.subst(t, &map)
            };
            let duplicate = if generics.is_empty() {
                self.prog.handler_decls.iter().any(|d| d.effect == eref && d.for_ty == for_ty)
            } else {
                let mine = shape(self, for_ty, &generics);
                let others: Vec<(EffectRef, Ty, Vec<ParamId>)> =
                    self.handler_templates.iter().map(|t| (t.effect.clone(), t.for_ty, t.generics.clone())).collect();
                others.into_iter().any(|(e, t, ps)| e.effect == eref.effect && shape(self, t, &ps) == mine)
            };
            if duplicate {
                let d = Diagnostic::error(
                    h.span,
                    format!("a handler of `{}` for `{}` is already declared", eref.effect.name(), for_ty),
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
            let body = match &c.body.kind {
                ExprKind::Block(b) => cx.block_expected(b, true, Some(p.ret)),
                _ => cx.expr(&c.body, Some(p.ret)),
            };
            let body = cx.clause_body(body, p.kind, p.ret);
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
            let is_type =
                p.ty.as_ref().is_some_and(|t| matches!(&t.kind, ExprKind::Ident(i) if i.name.as_str() == "type"));
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
    pub fn clause_signature(&self, eref: &EffectRef, op: usize) -> (Vec<ParamInfo>, Ty, ast::OpKind) {
        let info = &self.effects[&eref.effect];
        let o = &info.ops[op];
        let mut params: Vec<ParamInfo> = o
            .params
            .iter()
            .map(|p| ParamInfo {
                name: p.name,
                ty: super::effects::subst(p.ty, &info.params, &eref.args),
                span: p.span,
                default: None,
                generic: None,
            })
            .collect();
        let ret = super::effects::subst(o.ret, &info.params, &eref.args);
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
        });
        (params, Ty::never(), o.kind)
    }

    /// The handler declarations for values of type `t`, instantiating those for
    /// generic types that `t` matches.
    pub fn handlers_for(&mut self, t: Ty) -> Vec<HandlerDeclId> {
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
            // One declaration per effect and type (§12.6): a concrete one wins.
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
        })
        .collect()
}
