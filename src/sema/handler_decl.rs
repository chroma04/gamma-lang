//! Handler declarations `handler E for T \ {A} { … }` (§12.6).

use super::func::{ClauseInfo, FnCx, LocalBinding};
use super::{FnSig, ModuleId, ParamInfo, Sema};
use crate::diag::Diagnostic;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind, ItemKind};
use crate::tir::{self, HandlerDeclId};
use crate::types::{EffectSet, Ty};
use std::rc::Rc;

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
}

impl<'s> Sema<'s> {
    /// Registers every handler declaration so `with` can find them, and returns
    /// their clauses for checking once all signatures are known.
    pub(super) fn declare_handlers(&mut self) -> Vec<PendingClause> {
        let mut pending = vec![];
        for (module, item) in std::mem::take(&mut self.handler_items) {
            let ItemKind::Handler(h) = &item.kind else { continue };
            let Some(eref) = self.resolve_effect_ref(module, &h.effect) else { continue };
            let for_ty = self.resolve_type(module, &h.for_ty);
            if for_ty.is_error() {
                continue;
            }
            let effects = match &h.effects {
                Some(list) => self.resolve_effect_set(module, list),
                None => EffectSet::empty(),
            };
            if let Some(i) = self.prog.handler_decls.iter().position(|d| d.effect == eref && d.for_ty == for_ty) {
                let _ = i;
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
                let (mut params, ret, kind) = {
                    let info = &self.effects[&eref.effect];
                    let o = &info.ops[i];
                    let ps: Vec<ParamInfo> = o
                        .params
                        .iter()
                        .map(|p| ParamInfo {
                            name: p.name,
                            ty: super::effects::subst(p.ty, &info.params, &eref.args),
                            span: p.span,
                            default: None,
                        })
                        .collect();
                    (ps, super::effects::subst(o.ret, &info.params, &eref.args), o.kind)
                };
                // The first clause parameter is the handler value itself.
                params.insert(
                    0,
                    ParamInfo { name: Symbol::intern("self"), ty: for_ty, span: h.for_ty.span, default: None },
                );
                let sig = FnSig { module, params: params.clone(), ret, effects: effects.clone() };
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
                });
            }
            self.prog.handler_decls.push(tir::HandlerDecl { effect: eref, for_ty, clauses: clause_ids });
            self.handler_decl_effects.push(effects);
        }
        pending
    }

    /// Checks the bodies of declared handlers' clauses.
    pub(super) fn check_handler_clauses(&mut self, pending: Vec<PendingClause>) {
        for p in pending {
            let ItemKind::Handler(h) = &p.item.kind else { continue };
            let c = &h.clauses[p.clause];
            let mut cx = FnCx::new(self, p.module, p.ret, p.effects.clone());
            cx.clause = Some(ClauseInfo { op_kind: p.kind, for_label: None, handler: None, break_ty: None });
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
            let body = if body.ty.is_never() { body } else { cx.coerce(body, p.ret) };
            let (body, locals) = cx.finish(body);
            let def = &mut self.prog.fns[p.fn_id.index()];
            def.params = ids;
            def.locals = locals;
            def.body = Some(body);
        }
    }

    /// The handler declarations for values of type `t`.
    pub fn handlers_for(&self, t: Ty) -> Vec<HandlerDeclId> {
        (0..self.prog.handler_decls.len())
            .filter(|&i| self.prog.handler_decls[i].for_ty == t)
            .map(|i| HandlerDeclId(i as u32))
            .collect()
    }
}
