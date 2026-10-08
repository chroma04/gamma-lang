//! Resolving top-level declarations (§3.4, §3.5, §6.2, §6.3).

use super::untyped::{self, Untyped};
use super::{DeclId, DeclState, EffectInfo, FnSig, ModuleId, OpInfo, ParamInfo, PendingBody, Sema};
use crate::diag::Diagnostic;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, BindingKind, ExprKind, ItemKind};
use crate::tir::{self, FnId};
use crate::types::{EffectId, EffectRef, EffectSet, Ty, TyKind};
use std::rc::Rc;

/// What a top-level binding denotes.
#[derive(Clone, Debug)]
pub enum DeclValue {
    /// A type (an alias, or a nominal `struct`/`union` literal).
    Type(Ty),
    /// An effect.
    Effect(EffectId),
    /// A (non-generic) function.
    Fn(FnId),
    /// An effect operation, callable like a function (§12.1).
    Op(EffectId, u32),
    /// A typed constant.
    Const { ty: Ty, id: tir::ConstId },
    /// An untyped constant (§6.2).
    Untyped(Untyped),
    /// The declaration has errors (already reported).
    Error,
}

impl<'s> Sema<'s> {
    /// Resolves a declaration, reporting a cycle if it is already being resolved.
    pub fn resolve_decl(&mut self, id: DeclId) -> DeclValue {
        match &self.decls[id.0 as usize].state {
            DeclState::Done(v) => return v.clone(),
            DeclState::InProgress => {
                self.report_cycle(id);
                return DeclValue::Error;
            }
            DeclState::Unresolved => {}
        }
        self.decls[id.0 as usize].state = DeclState::InProgress;
        self.resolving.push(id);
        let v = self.compute_decl(id);
        self.resolving.pop();
        // A cycle report may already have marked this declaration as failed.
        if let DeclState::InProgress = self.decls[id.0 as usize].state {
            self.decls[id.0 as usize].state = DeclState::Done(v.clone());
        }
        match &self.decls[id.0 as usize].state {
            DeclState::Done(v) => v.clone(),
            _ => DeclValue::Error,
        }
    }

    fn report_cycle(&mut self, id: DeclId) {
        let start = self.resolving.iter().position(|d| *d == id).unwrap_or(0);
        let cycle: Vec<DeclId> = self.resolving[start..].to_vec();
        let names: Vec<String> = cycle.iter().map(|d| format!("`{}`", self.decls[d.0 as usize].name.name)).collect();
        let span = self.decls[id.0 as usize].name.span;
        let mut diag =
            Diagnostic::error(span, format!("dependency cycle between declarations: {}", names.join(" -> ")));
        for d in &cycle[1..] {
            diag = diag.with_label(self.decls[d.0 as usize].name.span, "part of the cycle");
        }
        self.emit(diag);
        for d in cycle {
            self.decls[d.0 as usize].state = DeclState::Done(DeclValue::Error);
        }
    }

    fn compute_decl(&mut self, id: DeclId) -> DeclValue {
        if let Some((effect_decl, op)) = self.decls[id.0 as usize].op_of {
            return match self.resolve_decl(effect_decl) {
                DeclValue::Effect(e) => DeclValue::Op(e, op),
                _ => DeclValue::Error,
            };
        }
        let item = self.decls[id.0 as usize].item.clone();
        let module = self.decls[id.0 as usize].module;
        let name = self.decls[id.0 as usize].name;
        let ItemKind::Binding(b) = &item.kind else { return DeclValue::Error };
        for attr in &item.attrs {
            self.error(attr.span, format!("attribute `#{}` is not supported yet", attr.name.name));
        }
        if b.kind == BindingKind::Var {
            self.error(b.span, "top-level bindings are immutable; use `::` (mutable storage needs `#global`)");
            return DeclValue::Error;
        }
        let init = &*b.init;
        match &init.kind {
            ExprKind::Fn(lit) if b.ty.is_none() => self.declare_fn(module, name, lit, &item),
            ExprKind::Effect(lit) if b.ty.is_none() => self.declare_effect(module, id, name, lit),
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Attributed { .. } if b.ty.is_none() => {
                let ty = self.declare_adt(module, Some(id), name.name, init);
                if ty.is_error() { DeclValue::Error } else { DeclValue::Type(ty) }
            }
            _ => {
                // An untyped constant expression?
                if b.ty.is_none()
                    && let Some(v) = self.try_untyped(module, init)
                {
                    return match v {
                        Ok(v) => DeclValue::Untyped(v),
                        Err(()) => DeclValue::Error,
                    };
                }
                self.declare_const_or_type(module, name, b.ty.as_deref(), init)
            }
        }
    }

    /// Evaluates `e` as an untyped constant expression (§6.2) if it has that shape.
    /// Returns `None` if it is not an untyped constant expression, `Some(Err)` after
    /// reporting an error.
    pub fn try_untyped(&mut self, module: ModuleId, e: &ast::Expr) -> Option<Result<Untyped, ()>> {
        match &e.kind {
            ExprKind::Int(l) if l.suffix.is_none() => Some(Ok(Untyped::Int(l.value.clone().into()))),
            ExprKind::Float(l) if l.suffix.is_none() => Some(Ok(Untyped::Float(l.value.clone()))),
            ExprKind::Paren(inner) => self.try_untyped(module, inner),
            ExprKind::Ident(id) => {
                let d = self.lookup_module_name(module, id.name)?;
                match self.resolve_decl(d) {
                    DeclValue::Untyped(v) => Some(Ok(v)),
                    _ => None,
                }
            }
            ExprKind::Unary { op: ast::UnOp::Neg, operand } => {
                let v = self.try_untyped(module, operand)?;
                Some(v.and_then(|v| untyped::negate(&v).map_err(|m| self.error(e.span, m))))
            }
            ExprKind::Binary { op, lhs, rhs } => {
                if matches!(op, ast::BinOp::And | ast::BinOp::Or | ast::BinOp::In | ast::BinOp::NotIn)
                    || matches!(op, ast::BinOp::Range | ast::BinOp::RangeIncl)
                {
                    return None;
                }
                // Flavored operators are not part of untyped constant expressions.
                if matches!(
                    op,
                    ast::BinOp::Add(f) | ast::BinOp::Sub(f) | ast::BinOp::Mul(f) | ast::BinOp::Shl(f)
                        if *f != ast::Flavor::Checked
                ) {
                    return None;
                }
                let a = self.try_untyped(module, lhs)?;
                let b = self.try_untyped(module, rhs)?;
                match (a, b) {
                    (Ok(a), Ok(b)) => Some(untyped::binary(*op, &a, &b).map_err(|m| self.error(e.span, m))),
                    _ => Some(Err(())),
                }
            }
            _ => None,
        }
    }

    /// Resolves the signature of a top-level function and queues its body.
    fn declare_fn(&mut self, module: ModuleId, name: ast::Ident, lit: &ast::FnLit, item: &Rc<ast::Item>) -> DeclValue {
        if !lit.where_.is_empty() {
            self.error(lit.span, "generic functions are not supported yet");
            return DeclValue::Error;
        }
        let Some(sig) = self.resolve_fn_sig(module, &lit.sig, true) else { return DeclValue::Error };
        let id = self.new_fn(name.name, name.span, sig, tir::FnKind::Named);
        self.pending_bodies.push(PendingBody { fn_id: id, module, item: item.clone() });
        DeclValue::Fn(id)
    }

    /// Resolves parameter types, return type and effect set. `literal` is true for
    /// function literals (parameter names required), false for function types.
    pub fn resolve_fn_sig(&mut self, module: ModuleId, sig: &ast::FnSig, literal: bool) -> Option<FnSig> {
        let mut ok = true;
        let mut params = vec![];
        let mut seen_default = false;
        for p in &sig.params {
            if p.comptime {
                self.error(p.span, "compile-time parameters are not supported yet");
                ok = false;
                continue;
            }
            let Some(ty_expr) = &p.ty else {
                self.error(p.span, "parameter types are required here");
                ok = false;
                continue;
            };
            let ty = self.resolve_type(module, ty_expr);
            if ty == Ty::type_() {
                self.error(ty_expr.span, "generic functions are not supported yet");
                ok = false;
            }
            if p.default.is_some() {
                seen_default = true;
            } else if seen_default && literal {
                self.error(p.span, "parameters with defaults may be followed only by parameters with defaults");
            }
            if p.names.is_empty() {
                params.push(ParamInfo { name: Symbol::intern("_"), ty, span: p.span, default: None });
            }
            for n in &p.names {
                let default = p.default.clone().map(Rc::new);
                params.push(ParamInfo { name: n.name, ty, span: n.span, default });
            }
        }
        if sig.variadic {
            self.error(sig.span, "variadic parameters are allowed only in foreign functions");
            ok = false;
        }
        let ret = match &sig.ret {
            Some(r) => self.resolve_type(module, r),
            None => Ty::unit(),
        };
        let effects = match &sig.effects {
            Some(list) => self.resolve_effect_set(module, list),
            None => EffectSet::empty(),
        };
        ok.then_some(FnSig { module, params, ret, effects })
    }

    pub fn resolve_effect_set(&mut self, module: ModuleId, list: &[ast::Expr]) -> EffectSet {
        let mut v = vec![];
        for e in list {
            if let Some(r) = self.resolve_effect_ref(module, e) {
                v.push(r);
            }
        }
        EffectSet::new(v)
    }

    pub fn resolve_effect_ref(&mut self, module: ModuleId, e: &ast::Expr) -> Option<EffectRef> {
        if let ExprKind::Ident(id) = &e.kind {
            if id.name.as_str() == "Sys" {
                self.error(e.span, "the `Sys` effect is not supported yet");
                return None;
            }
            if let Some(d) = self.lookup_module_name(module, id.name) {
                return match self.resolve_decl(d) {
                    DeclValue::Effect(eff) => Some(EffectRef { effect: eff, args: Box::new([]) }),
                    DeclValue::Error => None,
                    _ => {
                        self.error(e.span, format!("`{}` is not an effect", id.name));
                        None
                    }
                };
            }
            self.error(e.span, format!("unknown effect `{}`", id.name));
            return None;
        }
        self.error(e.span, "expected an effect name");
        None
    }

    fn declare_effect(&mut self, module: ModuleId, decl: DeclId, name: ast::Ident, lit: &ast::EffectLit) -> DeclValue {
        if lit.params.is_some() {
            self.error(lit.span, "generic effects are not supported yet");
            return DeclValue::Error;
        }
        let eff = EffectId::fresh(name.name);
        // Register the effect before resolving operation types, which may mention it.
        self.effects.insert(eff, EffectInfo { name: name.name, module, ops: vec![] });
        self.decls[decl.0 as usize].state = DeclState::Done(DeclValue::Effect(eff));
        let mut ops = vec![];
        for op in &lit.ops {
            if op.kind != ast::OpKind::Fn {
                self.error(op.span, "`esc` and `ctl` operations are not supported yet");
            }
            let mut params = vec![];
            for p in &op.params {
                let ty = match &p.ty {
                    Some(t) => self.resolve_type(module, t),
                    None => {
                        self.error(p.span, "operation parameters need types");
                        Ty::error()
                    }
                };
                for n in &p.names {
                    params.push(ParamInfo { name: n.name, ty, span: n.span, default: None });
                }
            }
            let ret = op.ret.as_ref().map_or(Ty::unit(), |r| self.resolve_type(module, r));
            ops.push(OpInfo { name: op.name.name, kind: op.kind, params, ret });
        }
        self.effects.get_mut(&eff).unwrap().ops = ops;
        DeclValue::Effect(eff)
    }

    /// A binding whose initializer is a type expression (an alias) or a typed constant.
    fn declare_const_or_type(
        &mut self,
        module: ModuleId,
        name: ast::Ident,
        annot: Option<&ast::Expr>,
        init: &ast::Expr,
    ) -> DeclValue {
        if annot.is_none() && self.looks_like_type(module, init) {
            let ty = self.resolve_type(module, init);
            return if ty.is_error() { DeclValue::Error } else { DeclValue::Type(ty) };
        }
        let expected = annot.map(|a| self.resolve_type(module, a));
        let Some(expr) = self.check_const_expr(module, init, expected) else { return DeclValue::Error };
        if matches!(expr.ty.kind(), TyKind::Error) {
            return DeclValue::Error;
        }
        let ty = expr.ty;
        let id = tir::ConstId(self.prog.consts.len() as u32);
        self.prog.consts.push(tir::ConstDef { name: name.name, ty, span: name.span, init: expr, value: None });
        DeclValue::Const { ty, id }
    }
}
