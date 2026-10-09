//! Resolving top-level declarations (§3.4, §3.5, §6.2, §6.3).

use super::untyped::{self, Untyped};
use super::{DeclId, DeclState, EffectInfo, FnSig, ModuleId, OpInfo, ParamInfo, PendingBody, Sema};
use crate::diag::Diagnostic;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, BindingKind, ExprKind, ItemKind};
use crate::tir::{self, FnId};
use crate::types::{EffectId, EffectRef, EffectSet, Ty, TyKind};
use num_traits::ToPrimitive;
use std::rc::Rc;

/// What a top-level binding denotes.
#[derive(Clone, Debug)]
pub enum DeclValue {
    /// A type (an alias, or a nominal `struct`/`union` literal).
    Type(Ty),
    /// A generic struct or union, applied to arguments to give a type (§16.4).
    GenericType(crate::types::AdtId),
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
    /// A module binding (§3.2), such as the predeclared `target`, or a foreign block.
    Module(super::ModuleId),
    /// A `#global` variable (§19.3).
    Global { ty: Ty, id: tir::GlobalId },
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
        let ItemKind::Binding(_) = &item.kind else { return DeclValue::Error };
        let mut fuel = None;
        let (mut global, mut thread_local, mut export) = (false, false, None);
        for attr in &item.attrs {
            match attr.name.name.as_str() {
                "global" => global = true,
                "thread_local" => thread_local = true,
                "export" => match attr.args.as_deref() {
                    Some([ast::Expr { kind: ExprKind::Str(s), .. }]) => {
                        export = Some((Symbol::intern(&String::from_utf8_lossy(&s.bytes)), attr.span))
                    }
                    _ => self.error(attr.span, "`#export` takes the symbol name: `#export(\"sym\")`"),
                },
                // `#fuel(n)` raises the step budget of this declaration's evaluations (§17.3).
                "fuel" => match attr.args.as_deref() {
                    Some([n]) => match self.try_untyped(module, n) {
                        Some(Ok(super::Untyped::Int(i))) if i.to_u64().is_some_and(|v| v > 0) => fuel = i.to_u64(),
                        _ => self.error(attr.span, "`#fuel(n)` takes a positive integer constant"),
                    },
                    _ => self.error(attr.span, "`#fuel(n)` takes a positive integer constant"),
                },
                // Inlining hints (§11.10) do not change the meaning of a program.
                "inline" | "noinline" | "cold" => {}
                "packed" | "align" | "bits" | "c_union" | "link_name" | "callconv" | "fast_math" | "deprecated" => {
                    self.error(attr.span, format!("attribute `#{}` is not supported here yet", attr.name.name))
                }
                other => self.error(attr.span, format!("unknown attribute `#{}` (§20.2)", other)),
            }
        }
        let saved_fuel = std::mem::replace(&mut self.fuel, fuel.unwrap_or(super::consteval::STEP_BUDGET));
        if thread_local && !global {
            self.error(name.span, "`#thread_local` applies only to a `#global`");
        }
        let v = if global {
            match &item.kind {
                ItemKind::Binding(b) => self.declare_global(module, name, b, thread_local),
                _ => DeclValue::Error,
            }
        } else {
            self.compute_binding(id, module, name, &item)
        };
        self.fuel = saved_fuel;
        if let Some((sym, span)) = export {
            match &v {
                DeclValue::Fn(f) => self.export_fn(*f, sym, span),
                DeclValue::Error => {}
                _ => self.error(span, "`#export` applies only to a function"),
            }
        }
        v
    }

    fn compute_binding(&mut self, id: DeclId, module: ModuleId, name: ast::Ident, item: &Rc<ast::Item>) -> DeclValue {
        let ItemKind::Binding(b) = &item.kind else { return DeclValue::Error };
        if b.kind == BindingKind::Var {
            self.error(b.span, "top-level bindings are immutable; use `::` (mutable storage needs `#global`)");
            return DeclValue::Error;
        }
        let init = &*b.init;
        if let ExprKind::Import(path) = &init.kind {
            if b.ty.is_some() {
                self.error(init.span, "an import binding cannot have a type");
                return DeclValue::Error;
            }
            let path = String::from_utf8_lossy(&path.bytes).into_owned();
            return match self.import(module, &path, init.span) {
                Some(m) => DeclValue::Module(m),
                None => DeclValue::Error,
            };
        }
        match &init.kind {
            ExprKind::Foreign(block) if b.ty.is_none() => self.declare_foreign(module, name, block),
            ExprKind::Fn(lit) if b.ty.is_none() => self.declare_fn(module, name, lit, item),
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
        let Some(sig) = self.resolve_fn_sig(module, &lit.sig, true, &lit.where_) else { return DeclValue::Error };
        let id = self.new_fn(name.name, name.span, sig, tir::FnKind::Named);
        self.pending_bodies.push(PendingBody { fn_id: id, module, item: item.clone() });
        DeclValue::Fn(id)
    }

    /// Resolves parameter types, return type and effect set. `literal` is true for
    /// function literals (parameter names required), false for function types.
    pub fn resolve_fn_sig(
        &mut self,
        module: ModuleId,
        sig: &ast::FnSig,
        literal: bool,
        where_: &[ast::Constraint],
    ) -> Option<FnSig> {
        let mut ok = true;
        let mut params = vec![];
        let mut generics = vec![];
        let mut seen_default = false;
        let scope_len = self.type_params.len();
        let value_scope_len = self.value_scope.len();
        for p in &sig.params {
            if p.comptime {
                // `N :: usize`: a compile-time value parameter (§16.5).
                if !literal {
                    self.error(p.span, "a function type cannot have compile-time parameters");
                    ok = false;
                    continue;
                }
                let Some(ty_expr) = &p.ty else {
                    self.error(p.span, "a compile-time parameter needs a type: `N :: usize`");
                    ok = false;
                    continue;
                };
                let ty = self.resolve_type(module, ty_expr);
                if matches!(ty.kind(), TyKind::Fn(_) | TyKind::Handler(_) | TyKind::Type) {
                    self.error(ty_expr.span, format!("a compile-time parameter cannot have the type `{}`", ty));
                    ok = false;
                    continue;
                }
                for n in &p.names {
                    let pid = crate::types::ParamId::fresh(n.name);
                    generics.push(pid);
                    self.value_params.insert(pid, ty);
                    self.value_scope.push((n.name, pid));
                    let default = p.default.clone().map(Rc::new);
                    params.push(ParamInfo { name: n.name, ty, span: n.span, default, generic: Some(pid) });
                }
                continue;
            }
            let Some(ty_expr) = &p.ty else {
                self.error(p.span, "parameter types are required here");
                ok = false;
                continue;
            };
            // `T: type` declares a type parameter, visible in the rest of the signature (§16.1).
            if matches!(&ty_expr.kind, ExprKind::Ident(i) if i.name.as_str() == "type") {
                if !literal {
                    self.error(ty_expr.span, "a function type cannot have type parameters");
                    ok = false;
                    continue;
                }
                for n in &p.names {
                    let pid = crate::types::ParamId::fresh(n.name);
                    generics.push(pid);
                    self.type_params.push((n.name, Ty::intern(TyKind::Param(pid))));
                    params.push(ParamInfo {
                        name: n.name,
                        ty: Ty::type_(),
                        span: n.span,
                        default: None,
                        generic: Some(pid),
                    });
                }
                continue;
            }
            let ty = self.resolve_type(module, ty_expr);
            if p.default.is_some() {
                seen_default = true;
            } else if seen_default && literal {
                self.error(p.span, "parameters with defaults may be followed only by parameters with defaults");
            }
            if p.names.is_empty() {
                params.push(ParamInfo { name: Symbol::intern("_"), ty, span: p.span, default: None, generic: None });
            }
            for n in &p.names {
                let default = p.default.clone().map(Rc::new);
                params.push(ParamInfo { name: n.name, ty, span: n.span, default, generic: None });
            }
        }
        if sig.variadic && !self.allow_variadic {
            self.error(sig.span, "variadic parameters are allowed only in foreign functions");
            ok = false;
        }
        let ret = match &sig.ret {
            Some(r) => self.resolve_type(module, r),
            None => Ty::unit(),
        };
        if let (Some(r), TyKind::Fn(_)) = (&sig.ret, ret.kind()) {
            self.error(
                r.span,
                "a second-class function cannot be returned; return a function pointer `^fn(…)` instead (§11.6)",
            );
        }
        let effects = match &sig.effects {
            Some(list) => self.resolve_effect_set(module, list),
            None => EffectSet::empty(),
        };
        for c in where_ {
            self.declare_constraint(module, c);
        }
        self.type_params.truncate(scope_len);
        self.value_scope.truncate(value_scope_len);
        ok.then_some(FnSig { module, params, ret, effects, generics })
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
        // `E(T1, …)`: an instantiation of a generic effect.
        if let ExprKind::Call { callee, args, .. } = &e.kind {
            let base = self.resolve_effect_ref(module, callee)?;
            let n = self.effects[&base.effect].params.len();
            if args.len() != n {
                self.error(e.span, format!("`{}` takes {} type argument(s)", base.effect.name(), n));
                return None;
            }
            let mut targs = vec![];
            for a in args {
                match &a.kind {
                    ast::ArgKind::Positional(t) => {
                        targs.push(crate::types::GenericArg::Type(self.resolve_type(module, t)))
                    }
                    _ => {
                        self.error(a.span, "expected a type argument");
                        return None;
                    }
                }
            }
            return Some(EffectRef { effect: base.effect, args: targs.into_boxed_slice() });
        }
        if let ExprKind::Ident(id) = &e.kind {
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
        let eff = EffectId::fresh(name.name);
        // Type parameters (`effect(T: type)`), visible in the operation signatures.
        let mut params = vec![];
        for p in lit.params.iter().flatten() {
            let is_type =
                p.ty.as_ref().is_some_and(|t| matches!(&t.kind, ExprKind::Ident(i) if i.name.as_str() == "type"));
            if !is_type {
                self.error(p.span, "effect parameters other than types are not supported yet");
            }
            for n in &p.names {
                let pid = crate::types::ParamId::fresh(n.name);
                params.push(pid);
                self.type_params.push((n.name, Ty::intern(TyKind::Param(pid))));
            }
        }
        // Register the effect before resolving operation types, which may mention it.
        self.effects.insert(eff, EffectInfo { name: name.name, module, params: params.clone(), ops: vec![] });
        self.decls[decl.0 as usize].state = DeclState::Done(DeclValue::Effect(eff));
        let mut ops = vec![];
        for (i, op) in lit.ops.iter().enumerate() {
            if op.kind == ast::OpKind::Ctl {
                self.prog.ctl_ops.insert((eff, i as u32));
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
                    params.push(ParamInfo { name: n.name, ty, span: n.span, default: None, generic: None });
                }
            }
            let ret = op.ret.as_ref().map_or(Ty::unit(), |r| self.resolve_type(module, r));
            ops.push(OpInfo { name: op.name.name, kind: op.kind, params, ret });
        }
        let n = self.type_params.len() - params.len();
        self.type_params.truncate(n);
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
        if let Some(bad) = non_constant_part(ty) {
            self.error(init.span, format!("a constant cannot hold `{}`, which contains `{}` (§17.4)", ty, bad));
            return DeclValue::Error;
        }
        // Top-level initializers are evaluated at compile time (§17.2).
        let Some(value) = self.eval_tir_const(&expr) else { return DeclValue::Error };
        let id = tir::ConstId(self.prog.consts.len() as u32);
        self.prog.consts.push(tir::ConstDef { name: name.name, ty, span: name.span, init: expr, value: Some(value) });
        DeclValue::Const { ty, id }
    }
}

/// The part of a type that a constant may not contain (§17.4): mutable pointers,
/// `rawptr`, continuations, and allocator handles.
fn non_constant_part(t: Ty) -> Option<Ty> {
    let mut bad = None;
    t.walk(&mut |x| {
        let is_bad = match x.kind() {
            TyKind::Ptr { is_const, .. } | TyKind::ManyPtr { is_const, elem: _ } | TyKind::Slice { is_const, .. } => {
                !is_const
            }
            TyKind::Rawptr | TyKind::Cont(_) => true,
            TyKind::Adt(id, _) => id.name().as_str() == "Allocator",
            _ => false,
        };
        if is_bad && bad.is_none() {
            bad = Some(x);
        }
    });
    bad
}
