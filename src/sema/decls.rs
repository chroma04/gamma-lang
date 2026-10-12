//! Resolving top-level declarations (§3.4, §3.5, §6.2, §6.3).

use super::untyped::{self, Untyped};
use super::{DeclId, DeclState, EffectInfo, FnSig, ModuleId, OpInfo, ParamInfo, PendingBody, Sema};
use crate::diag::Diagnostic;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, BindingKind, ExprKind, ItemKind};
use crate::tir::{self, AdtKind, FnId};
use crate::types::{EffectId, EffectRef, EffectSet, GenericArg, Ty, TyKind};
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
    /// A `pub` declaration's signature must not mention a private type (§3.3): the
    /// types of a function's parameters and result, of a constant or global, an alias's
    /// type, a struct's `pub` fields and a union's payloads.
    pub(super) fn check_pub_signatures(&mut self) {
        let private = |s: &Self, t: Ty| -> Option<Ty> {
            let mut found = None;
            t.walk(&mut |x| {
                if found.is_none()
                    && let TyKind::Adt(id, _) = x.kind()
                    && let Some(d) = s.adt_decls.get(&id.template())
                    && !s.decls[d.0 as usize].item.is_pub
                {
                    found = Some(x);
                }
            });
            found
        };
        for i in 0..self.decls.len() {
            let d = &self.decls[i];
            // Members of foreign blocks have synthetic items: they are not `pub` declarations.
            if !d.item.is_pub || d.op_of.is_some() || !matches!(d.item.kind, ItemKind::Binding(_)) {
                continue;
            }
            let DeclState::Done(v) = &d.state else { continue };
            let mut tys: Vec<Ty> = vec![];
            let adt_fields =
                |s: &Self, id: crate::types::AdtId, tys: &mut Vec<Ty>| match s.prog.adts.get(&id).map(|a| &a.kind) {
                    Some(AdtKind::Struct { fields, .. }) | Some(AdtKind::CUnion { fields }) => {
                        tys.extend(fields.iter().filter(|f| f.is_pub).map(|f| f.ty))
                    }
                    Some(AdtKind::Union { variants, .. }) => tys.extend(variants.iter().filter_map(|v| v.payload)),
                    None => {}
                };
            match v {
                DeclValue::Fn(f) => {
                    let sig = &self.sigs[f];
                    tys.extend(sig.params.iter().filter(|p| p.generic.is_none()).map(|p| p.ty));
                    tys.push(sig.ret);
                }
                DeclValue::Type(t) => match t.kind() {
                    TyKind::Adt(id, _) if self.adt_decls.get(id) == Some(&DeclId(i as u32)) => {
                        adt_fields(self, *id, &mut tys)
                    }
                    _ => tys.push(*t),
                },
                DeclValue::GenericType(id) if self.adt_decls.get(id) == Some(&DeclId(i as u32)) => {
                    adt_fields(self, *id, &mut tys)
                }
                DeclValue::Const { ty, .. } | DeclValue::Global { ty, .. } => tys.push(*ty),
                _ => {}
            }
            if let Some(p) = tys.into_iter().find_map(|t| private(self, t)) {
                let name = self.decls[i].name;
                self.error(
                    name.span,
                    format!("the `pub` declaration `{}` mentions the private type `{}` (§3.3)", name.name, p),
                );
            }
        }
    }

    /// Resolves a declaration, reporting a cycle if it is already being resolved.
    pub fn resolve_decl(&mut self, id: DeclId) -> DeclValue {
        match &self.decls[id.0 as usize].state {
            DeclState::Done(v) => return v.clone(),
            // `Link :: ?^Node` used by a field of `Node` while `Link` is being resolved:
            // `Node` is usable by now, so resolving the alias again succeeds. A real
            // cycle is found again on the second pass.
            DeclState::InProgress if self.is_structural_alias(id) && self.alias_reentry.insert(id) => {
                let v = self.compute_decl(id);
                self.alias_reentry.remove(&id);
                return v;
            }
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

    /// True if declaration `id` is an alias for a structural type built with pointer,
    /// slice, optional, array or function type syntax; resolving one has no side
    /// effects, so it may be done twice.
    fn is_structural_alias(&self, id: DeclId) -> bool {
        let d = &self.decls[id.0 as usize];
        if d.op_of.is_some() || !d.item.attrs.is_empty() {
            return false;
        }
        let ItemKind::Binding(b) = &d.item.kind else { return false };
        b.ty.is_none()
            && matches!(
                b.init.kind,
                ExprKind::OptionalType(_)
                    | ExprKind::PtrType { .. }
                    | ExprKind::SliceType { .. }
                    | ExprKind::ManyPtrType { .. }
                    | ExprKind::ArrayType { .. }
                    | ExprKind::ErrorUnionType { .. }
                    | ExprKind::FnType(_)
            )
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
        let (mut global, mut thread_local, mut export, mut callconv) = (false, false, None, None);
        let mut hints = (tir::FnHints::default(), None);
        let mut align_attr = None;
        for attr in &item.attrs {
            match attr.name.name.as_str() {
                "global" => global = true,
                // `#global #align(n)` (§20.2); on anything else, an error below.
                "align" if align_attr.is_none() => align_attr = Some(attr),
                "thread_local" => thread_local = true,
                "export" => match attr.args.as_deref() {
                    Some([ast::Expr { kind: ExprKind::Str(s), .. }]) => {
                        export = Some((Symbol::intern(&String::from_utf8_lossy(&s.bytes)), attr.span))
                    }
                    _ => self.error(attr.span, "`#export` takes the symbol name: `#export(\"sym\")`"),
                },
                "callconv" => {
                    if callconv.is_some() {
                        self.error(attr.span, "a function has one calling convention");
                    }
                    callconv = self.callconv_attr(attr).map(|c| (c, attr.span));
                }
                // `#fuel(n)` raises the step budget of this declaration's evaluations (§17.3).
                "fuel" => match attr.args.as_deref() {
                    Some([n]) => match self.try_untyped(module, n) {
                        Some(Ok(super::Untyped::Int(i))) if i.to_u64().is_some_and(|v| v > 0) => fuel = i.to_u64(),
                        _ => self.error(attr.span, "`#fuel(n)` takes a positive integer constant"),
                    },
                    _ => self.error(attr.span, "`#fuel(n)` takes a positive integer constant"),
                },
                // Inlining hints (§11.10) do not change the meaning of a program.
                "inline" | "noinline" | "cold" | "fast_math" => {
                    match attr.name.name.as_str() {
                        "inline" => hints.0.inline = Some(true),
                        "noinline" => hints.0.inline = Some(false),
                        "cold" => hints.0.cold = true,
                        _ => hints.0.fast_math = true,
                    }
                    hints.1 = Some((attr.span, attr.name.name));
                }
                // Uses of the declaration warn (`check_deprecated`).
                "deprecated" => {
                    if !matches!(attr.args.as_deref(), None | Some([ast::Expr { kind: ExprKind::Str(_), .. }])) {
                        self.error(attr.span, "`#deprecated` takes an optional message: `#deprecated(\"use g\")`");
                    }
                }
                "packed" | "align" | "bits" | "c_union" => self.error(
                    attr.span,
                    format!("`#{}` goes directly before a `struct` or `union` literal (§20.2)", attr.name.name),
                ),
                "link_name" => self.error(attr.span, "`#link_name` applies to a member of a `foreign` block (§19.2)"),
                other => self.error(attr.span, format!("unknown attribute `#{}` (§20.2)", other)),
            }
        }
        let saved_fuel = std::mem::replace(&mut self.fuel, fuel.unwrap_or(super::consteval::STEP_BUDGET));
        if thread_local && !global {
            self.error(name.span, "`#thread_local` applies only to a `#global`");
        }
        let align = match align_attr {
            Some(a) if global => self.attr_power_of_two(module, a),
            Some(a) => {
                self.error(
                    a.span,
                    "`#align` goes directly before a `struct` or `union` literal, or on a `#global` (§20.2)",
                );
                None
            }
            None => None,
        };
        let v = if global {
            match &item.kind {
                ItemKind::Binding(b) => self.declare_global(module, name, b, thread_local, align),
                _ => DeclValue::Error,
            }
        } else {
            self.compute_binding(id, module, name, &item)
        };
        self.fuel = saved_fuel;
        if let Some((cc, span)) = callconv {
            match &v {
                DeclValue::Fn(f) => self.apply_callconv(*f, cc, span),
                DeclValue::Error => {}
                _ => self.error(span, "`#callconv` applies only to a function, a foreign function or a `^fn` type"),
            }
        }
        if let (h, Some((span, attr))) = hints {
            match &v {
                DeclValue::Fn(f) => {
                    self.prog.fns[f.index()].hints = h;
                    if h.inline == Some(true) {
                        self.inline_fns.push((*f, span));
                    }
                }
                DeclValue::Error => {}
                _ => self.error(span, format!("`#{}` applies only to a function (§20.2)", attr)),
            }
        }
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
            ExprKind::Struct(_) | ExprKind::Union(_) if b.ty.is_none() => {
                let ty = self.declare_adt(module, Some(id), name.name, init);
                if ty.is_error() { DeclValue::Error } else { DeclValue::Type(ty) }
            }
            ExprKind::Attributed { inner, .. }
                if b.ty.is_none() && matches!(inner.kind, ExprKind::Struct(_) | ExprKind::Union(_)) =>
            {
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
            // `m.X`, an untyped constant of another module.
            ExprKind::Field { base, name } => {
                let m = self.module_named(module, base)?;
                let d = *self.modules[m.0 as usize].scope.get(&name.name)?;
                if !self.decls[d.0 as usize].item.is_pub {
                    return None;
                }
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
    pub(super) fn declare_fn(
        &mut self,
        module: ModuleId,
        name: ast::Ident,
        lit: &ast::FnLit,
        item: &Rc<ast::Item>,
    ) -> DeclValue {
        let Some(sig) = self.resolve_fn_sig(module, &lit.sig, true, &lit.where_) else { return DeclValue::Error };
        // A struct may not have both a field and a method named alike (§7.10).
        if let Some(first) = sig.params.iter().find(|p| p.generic.is_none()) {
            let s = match first.ty.kind() {
                TyKind::Ptr { pointee, .. } => *pointee,
                _ => first.ty,
            };
            if let TyKind::Adt(adt, _) = s.kind()
                && self.adts.get(adt).is_some_and(|a| a.module == module)
                && self.struct_fields(s).is_some_and(|fs| fs.iter().any(|f| f.name == name.name))
            {
                self.error(
                    name.span,
                    format!(
                        "`{}` has a field `{}`, so a method of that name could never be called (§7.10)",
                        s, name.name
                    ),
                );
            }
        }
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
        self.constraint_defer += 1;
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
        self.end_constraint_defer();
        // Instantiations of one generic effect in the declared set must be told apart
        // without knowing the type arguments (§16.6). A second-class parameter's effects
        // are performed where it is called, and checked there.
        if !generics.is_empty() {
            self.check_distinct(sig.span, &effects.effects);
        }
        self.type_params.truncate(scope_len);
        self.value_scope.truncate(value_scope_len);
        ok.then_some(FnSig { module, params, ret, effects, generics })
    }

    /// Reports two instantiations of the same generic effect in `refs` that are neither
    /// identical nor provably distinct (§16.6).
    pub fn check_distinct(&mut self, span: crate::source::Span, refs: &[EffectRef]) {
        for (i, a) in refs.iter().enumerate() {
            for b in &refs[i + 1..] {
                if may_coincide(a, b) {
                    self.error(
                        span,
                        format!(
                            "`{}` and `{}` might be the same effect for some type arguments, so performing either would be ambiguous (§16.6)",
                            a,
                            b
                        ),
                    );
                }
            }
        }
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
        // `m.E` — an effect declared in another module.
        if let ExprKind::Field { base, name } = &e.kind
            && let Some(m) = self.module_named(module, base)
        {
            let d = self.module_member(m, *name)?;
            return match self.resolve_decl(d) {
                DeclValue::Effect(eff) => Some(EffectRef { effect: eff, args: Box::new([]) }),
                DeclValue::Error => None,
                _ => {
                    self.error(e.span, format!("`{}` is not an effect", name.name));
                    None
                }
            };
        }
        if let ExprKind::Field { base, .. } = &e.kind
            && self.names_error(module, base)
        {
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
                self.error(p.span, "effect parameters are types: `effect(T: type)` (§12.1)");
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
        // A name for another declaration (§3.5): `mk :: shapes.make`, `Map :: HashMap`.
        if annot.is_none()
            && let Some(d) = self.aliased_decl(module, init)
        {
            let Some(d) = d else { return DeclValue::Error };
            let v = self.resolve_decl(d);
            if let DeclValue::Type(_)
            | DeclValue::GenericType(_)
            | DeclValue::Effect(_)
            | DeclValue::Fn(_)
            | DeclValue::Op(..)
            | DeclValue::Module(_)
            | DeclValue::Untyped(_)
            | DeclValue::Error = v
            {
                return v;
            }
        }
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

impl<'s> Sema<'s> {
    /// The declaration that `e` names if it is a plain name or `m.name`: a candidate for
    /// an alias declaration. `Some(None)` if `name` is missing from or private to module
    /// `m` (reported).
    fn aliased_decl(&mut self, module: ModuleId, e: &ast::Expr) -> Option<Option<DeclId>> {
        match &e.kind {
            ast::ExprKind::Ident(id) => self.lookup_module_name(module, id.name).map(Some),
            ast::ExprKind::Paren(inner) => self.aliased_decl(module, inner),
            ast::ExprKind::Field { base, name } => {
                let m = self.module_named(module, base)?;
                Some(self.module_member(m, *name))
            }
            _ => None,
        }
    }
}

/// The part of a type that a constant may not contain (§17.4): mutable pointers,
/// `rawptr`, continuations, and allocator handles.
pub fn non_constant_part(t: Ty) -> Option<Ty> {
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

/// Whether types `a` and `b` differ for every substitution of their type parameters:
/// both are concrete and unequal, or their outermost constructors differ (§16.6).
/// True if `a` and `b` are different instantiations of one effect that are not provably
/// distinct: they are the same effect for some type arguments (§16.6).
pub fn may_coincide(a: &EffectRef, b: &EffectRef) -> bool {
    a.effect == b.effect
        && a != b
        && !a.args.iter().zip(b.args.iter()).any(|(x, y)| match (x, y) {
            (GenericArg::Type(x), GenericArg::Type(y)) => provably_distinct(*x, *y),
            _ => false,
        })
}

fn provably_distinct(a: Ty, b: Ty) -> bool {
    use super::generics::has_param;
    if !has_param(a) && !has_param(b) {
        return a != b;
    }
    match (a.kind(), b.kind()) {
        (TyKind::Param(_), _) | (_, TyKind::Param(_)) => false,
        (TyKind::Ptr { pointee: x, .. }, TyKind::Ptr { pointee: y, .. })
        | (TyKind::Slice { elem: x, .. }, TyKind::Slice { elem: y, .. })
        | (TyKind::ManyPtr { elem: x, .. }, TyKind::ManyPtr { elem: y, .. })
        | (TyKind::Optional(x), TyKind::Optional(y)) => provably_distinct(*x, *y),
        (TyKind::Adt(x, xs), TyKind::Adt(y, ys)) => {
            x.template() != y.template()
                || xs.iter().zip(ys.iter()).any(|(p, q)| match (p, q) {
                    (GenericArg::Type(p), GenericArg::Type(q)) => provably_distinct(*p, *q),
                    _ => false,
                })
        }
        (TyKind::Tuple { elems: x, .. }, TyKind::Tuple { elems: y, .. }) => {
            x.len() != y.len() || x.iter().zip(y.iter()).any(|(p, q)| provably_distinct(*p, *q))
        }
        (x, y) => std::mem::discriminant(x) != std::mem::discriminant(y),
    }
}
