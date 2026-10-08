//! Evaluating type expressions to [`Ty`] (§5, §A.2).

use super::builtins::builtin_type;
use super::{DeclValue, ModuleId, Sema, Untyped};
use crate::syntax::ast::{self, ExprKind};
use crate::types::{FnTy, Ty, TyKind};
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
                    return match self.resolve_fn_sig(module, sig, false) {
                        Some(s) => Ty::intern(TyKind::FnPtr(Box::new(FnTy {
                            params: s.params.iter().map(|p| p.ty).collect(),
                            ret: s.ret,
                            effects: s.effects,
                            variadic: false,
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
            ExprKind::FnType(sig) => match self.resolve_fn_sig(module, sig, false) {
                Some(s) => Ty::intern(TyKind::Fn(Box::new(FnTy {
                    params: s.params.iter().map(|p| p.ty).collect(),
                    ret: s.ret,
                    effects: s.effects,
                    variadic: false,
                }))),
                None => Ty::error(),
            },
            ExprKind::ContType(inner) => Ty::intern(TyKind::Cont(self.resolve_type(module, inner))),
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Attributed { .. } => {
                let name = match &e.kind {
                    ExprKind::Union(_) => "union",
                    _ => "struct",
                };
                self.declare_adt(module, None, crate::symbol::Symbol::intern(name), e)
            }
            _ => {
                self.error(e.span, "expected a type");
                Ty::error()
            }
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
                self.error(
                    e.span,
                    "array lengths must be constant expressions (typed constants are not supported yet)",
                );
                None
            }
        }
    }
}
