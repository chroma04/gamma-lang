//! Evaluating type expressions to [`Ty`] (§5, §A.2).

use super::builtins::builtin_type;
use super::{DeclValue, ModuleId, Sema, Untyped};
use crate::syntax::ast::{self, ExprKind};
use crate::tir;
use crate::types::{CallConv, FnTy, Ty, TyKind};
use num_traits::ToPrimitive;

impl<'s> Sema<'s> {
    /// True if `e` syntactically denotes a type (used to tell aliases from constants).
    pub fn looks_like_type(&mut self, module: ModuleId, e: &ast::Expr) -> bool {
        match &e.kind {
            ExprKind::Ident(id) => {
                if let Some(d) = self.lookup_module_name(module, id.name) {
                    return matches!(self.resolve_decl(d), DeclValue::Type(_));
                }
                builtin_type(id.name.as_str()).is_some()
            }
            ExprKind::Paren(inner) => self.looks_like_type(module, inner),
            // `[2]P(…)[1]` indexes a constructed value (§A.7 rule 12).
            ExprKind::ArrayType { .. } | ExprKind::OptionalType(_) if super::expr::hoist_prefix(e).is_some() => false,
            // `[3]i32(1, 2, 3)` and `?T(x)` construct values (§A.7 rule 12), unless the call
            // instantiates a generic type: `[4]List(i32)`.
            ExprKind::ArrayType { elem: inner, .. } | ExprKind::OptionalType(inner)
                if super::expr::construction_call(inner).is_some() =>
            {
                self.looks_like_type(module, inner)
            }
            ExprKind::Tuple(elems) => elems.iter().all(|el| self.looks_like_type(module, &el.value)),
            ExprKind::OptionalType(_)
            | ExprKind::PtrType { .. }
            | ExprKind::SliceType { .. }
            | ExprKind::ManyPtrType { .. }
            | ExprKind::ArrayType { .. }
            | ExprKind::ErrorUnionType { .. }
            | ExprKind::FnType(_)
            | ExprKind::ContType(_)
            | ExprKind::Struct(_)
            | ExprKind::Union(_) => true,
            ExprKind::Bang { lhs, .. } => self.looks_like_type(module, lhs),
            ExprKind::Call { callee, .. } => match &callee.kind {
                ExprKind::Ident(id) => match self.lookup_module_name(module, id.name) {
                    Some(d) => matches!(self.resolve_decl(d), DeclValue::GenericType(_)),
                    None => false,
                },
                _ => false,
            },
            ExprKind::Attributed { inner, .. } => self.looks_like_type(module, inner),
            _ => false,
        }
    }

    /// Resolves a type expression, reporting an error (and returning the error
    /// type) if it does not denote a type.
    pub fn resolve_type(&mut self, module: ModuleId, e: &ast::Expr) -> Ty {
        match &e.kind {
            ExprKind::Ident(id) => {
                if let Some((_, t)) = self.type_params.iter().rev().find(|(n, _)| *n == id.name) {
                    return *t;
                }
                if let Some(d) = self.lookup_module_name(module, id.name) {
                    self.check_deprecated(d, id.span);
                    return match self.resolve_decl(d) {
                        DeclValue::Type(t) => t,
                        DeclValue::Error => Ty::error(),
                        _ => {
                            self.error(e.span, format!("`{}` is not a type", id.name));
                            Ty::error()
                        }
                    };
                }
                if let Some(t) = builtin_type(id.name.as_str()) {
                    return t;
                }
                self.error(e.span, format!("unknown type `{}`", id.name));
                Ty::error()
            }
            ExprKind::Paren(inner) => self.resolve_type(module, inner),
            ExprKind::Tuple(elems) => {
                let named = elems.first().is_some_and(|el| el.name.is_some());
                let tys: Vec<Ty> = elems.iter().map(|el| self.resolve_type(module, &el.value)).collect();
                let names = if named {
                    let names: Vec<_> = elems.iter().map(|el| el.name.map(|n| n.name)).collect();
                    if names.iter().any(|n| n.is_none()) {
                        self.error(e.span, "a tuple type must be either fully named or fully unnamed");
                        return Ty::error();
                    }
                    let names: Vec<_> = names.into_iter().flatten().collect();
                    for (i, n) in names.iter().enumerate() {
                        if names[..i].contains(n) {
                            self.error(e.span, format!("duplicate tuple element name `{}`", n));
                            return Ty::error();
                        }
                    }
                    Some(names.into_boxed_slice())
                } else {
                    None
                };
                Ty::intern(TyKind::Tuple { elems: tys.into_boxed_slice(), names })
            }
            ExprKind::OptionalType(inner) => Ty::optional(self.resolve_type(module, inner)),
            ExprKind::PtrType { is_const, inner } => {
                if let ExprKind::FnType(sig) = &inner.kind {
                    if *is_const {
                        self.error(e.span, "`^const fn` is not a type; function pointers are written `^fn(…)`");
                    }
                    return match self.resolve_fn_sig(module, sig, false, &[]) {
                        Some(s) => Ty::intern(TyKind::FnPtr(Box::new(FnTy {
                            params: s.params.iter().map(|p| p.ty).collect(),
                            ret: s.ret,
                            effects: s.effects,
                            variadic: false,
                            callconv: CallConv::C,
                        }))),
                        None => Ty::error(),
                    };
                }
                Ty::ptr(*is_const, self.resolve_type(module, inner))
            }
            ExprKind::SliceType { is_const, inner } => Ty::slice(*is_const, self.resolve_type(module, inner)),
            ExprKind::ManyPtrType { is_const, inner } => Ty::many_ptr(*is_const, self.resolve_type(module, inner)),
            ExprKind::ArrayType { len, elem } => {
                let elem = self.resolve_type(module, elem);
                let Some(len) = len else {
                    self.error(e.span, "`[_]` is allowed only in array constructors");
                    return Ty::error();
                };
                // `[N]T` with `N` a compile-time value parameter (§16.5).
                if let ExprKind::Ident(id) = &len.kind
                    && let Some(&(_, pid)) = self.value_scope.iter().rev().find(|(n, _)| *n == id.name)
                {
                    if !self.value_params[&pid].is_integer() {
                        self.error(
                            len.span,
                            format!("`{}` is not an integer, so it cannot be an array length", id.name),
                        );
                        return Ty::error();
                    }
                    let len = Ty::intern(TyKind::Param(pid));
                    return Ty::intern(TyKind::ParamArray { len, elem });
                }
                match self.const_usize(module, len) {
                    Some(n) => Ty::array(n, elem),
                    None => Ty::error(),
                }
            }
            ExprKind::ErrorUnionType { err, ok } | ExprKind::Bang { lhs: err, rhs: ok } => {
                let err = self.resolve_type(module, err);
                let ok = self.resolve_type(module, ok);
                Ty::result(err, ok)
            }
            ExprKind::FnType(sig) => match self.resolve_fn_sig(module, sig, false, &[]) {
                Some(s) => Ty::intern(TyKind::Fn(Box::new(FnTy {
                    params: s.params.iter().map(|p| p.ty).collect(),
                    ret: s.ret,
                    effects: s.effects,
                    variadic: false,
                    callconv: CallConv::C,
                }))),
                None => Ty::error(),
            },
            ExprKind::ContType(inner) => Ty::intern(TyKind::Cont(self.resolve_type(module, inner))),
            // `G(args)` — an instantiation of a generic type (§16.4).
            ExprKind::Call { callee, args, .. } => {
                let template = match &callee.kind {
                    ExprKind::Ident(id) => match self.lookup_module_name(module, id.name) {
                        Some(d) => match self.resolve_decl(d) {
                            DeclValue::GenericType(t) => {
                                self.check_deprecated(d, id.span);
                                Some(t)
                            }
                            DeclValue::Error => return Ty::error(),
                            _ => None,
                        },
                        None => None,
                    },
                    _ => None,
                };
                let Some(template) = template else {
                    self.error(e.span, "expected a type");
                    return Ty::error();
                };
                let Some(targs) = self.generic_type_args(module, template, args, e.span) else {
                    return Ty::error();
                };
                for (pid, t) in self.adt_params[&template].clone().into_iter().zip(&targs) {
                    for c in self.constraints.get(&pid).cloned().unwrap_or_default() {
                        if !self.satisfies(*t, c) {
                            self.error(
                                e.span,
                                format!(
                                    "`{}` does not satisfy `{:?}({})` required by `{}`",
                                    t,
                                    c,
                                    pid.name(),
                                    template.name()
                                ),
                            );
                        }
                    }
                }
                self.instantiate_adt(template, targs)
            }
            ExprKind::Attributed { attrs, inner } if matches!(&inner.kind, ExprKind::PtrType { inner: f, .. } if matches!(f.kind, ExprKind::FnType(_))) => {
                self.callconv_fn_ptr(module, attrs, inner, e.span)
            }
            ExprKind::Attributed { inner, .. } if !matches!(inner.kind, ExprKind::Struct(_) | ExprKind::Union(_)) => {
                self.error(e.span, "attributes here must precede a `struct`, a `union` or a `^fn` type");
                Ty::error()
            }
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Attributed { .. } => {
                let name = match &e.kind {
                    ExprKind::Union(_) => "union",
                    ExprKind::Attributed { inner, .. } if matches!(inner.kind, ExprKind::Union(_)) => "union",
                    _ => "struct",
                };
                self.declare_adt(module, None, crate::symbol::Symbol::intern(name), e)
            }
            ExprKind::Intrinsic { name, args } if name.name.as_str() == "type_of" => {
                let Some([ast::Arg { kind: ast::ArgKind::Positional(x), .. }]) = args.as_deref() else {
                    self.error(e.span, "`@type_of(e)` takes one expression");
                    return Ty::error();
                };
                self.check_const_expr(module, x, None).map_or(Ty::error(), |v| v.ty)
            }
            // `m.T` — a type declared in another module.
            ExprKind::Field { base, name } if self.module_named(module, base).is_some() => {
                let m = self.module_named(module, base).unwrap();
                let Some(d) = self.module_member(m, *name) else { return Ty::error() };
                match self.resolve_decl(d) {
                    DeclValue::Type(t) => t,
                    DeclValue::Error => Ty::error(),
                    _ => {
                        self.error(e.span, format!("`{}` is not a type", name.name));
                        Ty::error()
                    }
                }
            }
            // `m.T` where `m` failed to resolve (a bad import, already reported).
            ExprKind::Field { base, .. } if self.names_error(module, base) => Ty::error(),
            _ => {
                self.error(e.span, "expected a type");
                Ty::error()
            }
        }
    }

    /// True if `e` is a name whose declaration has errors (already reported).
    pub fn names_error(&mut self, module: ModuleId, e: &ast::Expr) -> bool {
        let ExprKind::Ident(id) = &e.kind else { return false };
        match self.lookup_module_name(module, id.name) {
            Some(d) => matches!(self.resolve_decl(d), super::DeclValue::Error),
            None => false,
        }
    }

    /// Evaluates a compile-time `usize` (an array length).
    pub fn const_usize(&mut self, module: ModuleId, e: &ast::Expr) -> Option<u64> {
        match self.try_untyped(module, e) {
            Some(Ok(Untyped::Int(i))) => match i.to_u64() {
                Some(n) => Some(n),
                None => {
                    self.error(e.span, format!("array length {} is out of range", i));
                    None
                }
            },
            Some(Ok(other)) => {
                self.error(e.span, format!("expected an integer array length, found a {}", other.describe()));
                None
            }
            Some(Err(())) => None,
            None => {
                // A typed constant expression, evaluated now (§7.15).
                let ex = self.check_const_expr(module, e, Some(Ty::usize()))?;
                // A length computed from compile-time parameters is not known until
                // instantiation (§16.5).
                let mut params = vec![];
                super::func::visit(&ex, &mut |n| {
                    if let crate::tir::ExprKind::ValueParam(p) = n.kind
                        && !params.contains(&p.name().to_string())
                    {
                        params.push(p.name().to_string());
                    }
                });
                if !params.is_empty() {
                    let list: Vec<String> = params.iter().map(|p| format!("`{}`", p)).collect();
                    self.error(
                        e.span,
                        format!(
                            "an array length computed from the compile-time parameter{} {} is not supported in a type; \
                             use a parameter directly, or nested arrays such as `[R][C]T` (§16.5)",
                            if params.len() > 1 { "s" } else { "" },
                            list.join(" and ")
                        ),
                    );
                    return None;
                }
                let v = self.eval_tir_const(&ex)?;
                let n = crate::types::ConstKey::Bytes(v.ty, v.bytes.into()).as_u64();
                if n.is_none() {
                    self.error(e.span, "array length is out of range");
                }
                n
            }
        }
    }
}

impl<'s> Sema<'s> {
    /// The arguments of `G(args)`, in parameter order: positional, then named,
    /// then defaults (§16.4). Value arguments are evaluated.
    fn generic_type_args(
        &mut self,
        module: ModuleId,
        template: crate::types::AdtId,
        args: &[crate::syntax::ast::Arg],
        span: crate::source::Span,
    ) -> Option<Vec<Ty>> {
        use crate::syntax::ast::ArgKind;
        let params = self.adt_params.get(&template).cloned().unwrap_or_default();
        let defaults = self.adt_param_defaults.get(&template).cloned().unwrap_or_default();
        let mut given: Vec<Option<&ast::Expr>> = vec![None; params.len()];
        let mut next = 0;
        for a in args {
            let (i, e) = match &a.kind {
                ArgKind::Positional(e) => {
                    next += 1;
                    (next - 1, e)
                }
                ArgKind::Named(n, e) => match params.iter().position(|p| p.name() == n.name) {
                    Some(i) => (i, e),
                    None => {
                        self.error(n.span, format!("`{}` has no parameter named `{}`", template.name(), n.name));
                        return None;
                    }
                },
                ArgKind::Spread(_) => {
                    self.error(a.span, "a spread argument cannot instantiate a generic type");
                    return None;
                }
            };
            if i >= params.len() {
                self.error(span, format!("`{}` takes {} argument(s)", template.name(), params.len()));
                return None;
            }
            if given[i].is_some() {
                self.error(a.span, format!("the parameter `{}` is given twice", params[i].name()));
                return None;
            }
            given[i] = Some(e);
        }
        let mut out = vec![];
        for (i, pid) in params.iter().enumerate() {
            let default = defaults.get(i).cloned().flatten();
            let e: &ast::Expr = match (given[i], &default) {
                (Some(e), _) => e,
                (None, Some(d)) => d,
                (None, None) => {
                    self.error(
                        span,
                        format!("missing argument for the parameter `{}` of `{}`", pid.name(), template.name()),
                    );
                    return None;
                }
            };
            let t = match self.value_params.get(pid).copied() {
                Some(vt) => {
                    let map: Vec<(crate::types::ParamId, Ty)> =
                        params.iter().copied().zip(out.iter().copied()).collect();
                    let vt = self.subst(vt, &map);
                    self.comptime_value(module, vt, e)?
                }
                None => self.resolve_type(module, e),
            };
            if t.is_error() {
                return None;
            }
            out.push(t);
        }
        Some(out)
    }

    /// Evaluates the argument of a compile-time value parameter of type `ty`
    /// (§16.5) to a type-level value. Inside a generic body, a compile-time
    /// parameter in scope is passed on as it is.
    pub fn comptime_value(&mut self, module: ModuleId, ty: Ty, e: &ast::Expr) -> Option<Ty> {
        if let ExprKind::Ident(id) = &e.kind
            && let Some(&(_, pid)) = self.value_scope.iter().rev().find(|(n, _)| *n == id.name)
        {
            let have = self.value_params[&pid];
            // A function parameter may be passed where an optional one is expected.
            if have != ty && ty != Ty::optional(have) {
                self.error(e.span, format!("expected a compile-time `{}`, found `{}` of type `{}`", ty, id.name, have));
                return None;
            }
            return Some(Ty::intern(TyKind::Param(pid)));
        }
        let ex = self.check_const_expr(module, e, Some(ty))?;
        // Functions and `null` are known without evaluation, whatever the type.
        let key = match &ex.kind {
            tir::ExprKind::Null => crate::types::ConstKey::Null,
            tir::ExprKind::FnRef(f) => crate::types::ConstKey::Fn(f.0, self.prog.fns[f.index()].name),
            tir::ExprKind::Coerce { expr, .. } if matches!(expr.kind, tir::ExprKind::FnRef(_)) => {
                let tir::ExprKind::FnRef(f) = expr.kind else { unreachable!() };
                crate::types::ConstKey::Fn(f.0, self.prog.fns[f.index()].name)
            }
            _ if super::generics::has_param(ty) => {
                self.error(e.span, "this compile-time argument's type depends on a type parameter; only functions and `null` are supported here yet");
                return None;
            }
            _ => {
                let v = self.eval_tir_const(&ex)?;
                self.const_key(&v, e.span)?
            }
        };
        Some(Ty::intern(TyKind::Value(key)))
    }
}
