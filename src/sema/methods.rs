//! Method calls (§7.10): struct fields holding function pointers, builtin
//! methods (§6.6, §21.3) and functions of the receiver type's module.

use super::func::{FnCx, Obligation, error_expr};
use super::{DeclValue, ParamInfo};
use crate::source::Span;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, Builtin, Callee, Expr};
use crate::types::{Ty, TyKind};

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

impl<'a, 's> FnCx<'a, 's> {
    /// `base.name(args)`.
    pub fn method_call(&mut self, base: &ast::Expr, name: ast::Ident, args: &[ast::Arg], span: Span) -> Expr {
        // `U.from_tag(x)` on an enum type.
        if let Some(t) = self.type_of_name(base) {
            if name.name.as_str() == "from_tag" {
                return self.check_from_tag(t, args, span);
            }
            return self.construct_variant(t, name, Some(args), span);
        }
        let recv = self.expr(base, None);
        let rty = self.infer.resolve(recv.ty);
        if rty.is_error() {
            return error_expr(span);
        }
        // 1. A field (holding a function pointer).
        let derefed = self.auto_deref(recv.clone());
        if let Some(fields) = self.sema.struct_fields(derefed.ty)
            && let Some(i) = fields.iter().position(|f| f.name == name.name)
        {
            let fty = fields[i].ty;
            let callee = mk(fty, tir::ExprKind::Field { base: Box::new(derefed), index: i as u32 }, span);
            return self.call_value(callee, args, span);
        }
        // 2. A builtin method.
        if let Some(e) = self.builtin_method(recv.clone(), name, args, span) {
            return e;
        }
        // 3. A function of the module that declares the receiver's type.
        if let Some(e) = self.module_method(recv, name, args, span) {
            return e;
        }
        let rty = self.infer.resolve(rty);
        self.error(name.span, format!("type `{}` has no method `{}`", rty, name.name));
        error_expr(span)
    }

    fn type_arg(&mut self, args: &[ast::Arg], i: usize, span: Span) -> Option<Ty> {
        match args.get(i).map(|a| &a.kind) {
            Some(ArgKind::Positional(e)) => {
                let t = self.resolve_type(e);
                (!t.is_error()).then_some(t)
            }
            _ => {
                self.error(span, "expected a type argument");
                None
            }
        }
    }

    fn value_arg(&mut self, args: &[ast::Arg], i: usize, ty: Ty, span: Span) -> Expr {
        match args.get(i).map(|a| &a.kind) {
            Some(ArgKind::Positional(e)) => {
                let v = self.expr(e, Some(ty));
                self.coerce(v, ty)
            }
            _ => {
                self.error(span, "missing argument");
                error_expr(span)
            }
        }
    }

    fn arity(&mut self, args: &[ast::Arg], n: usize, name: &str, span: Span) -> bool {
        if args.len() != n {
            self.error(span, format!("`{}` takes {} argument{}", name, n, if n == 1 { "" } else { "s" }));
            return false;
        }
        true
    }

    /// The builtin methods of §21.3 and the conversions of §6.6.
    fn builtin_method(&mut self, recv: Expr, name: ast::Ident, args: &[ast::Arg], span: Span) -> Option<Expr> {
        let n = name.name.as_str();
        let rty = self.infer.resolve(recv.ty);
        let is_int = rty.is_integer() || self.infer.var_kind(rty) == Some(super::infer::VarKind::Int);
        let is_float = rty.is_float() || self.infer.var_kind(rty) == Some(super::infer::VarKind::Float);
        let b = |op, ty, args: Vec<Expr>| mk(ty, tir::ExprKind::Builtin { op, args }, span);
        if is_int || is_float {
            match n {
                "trunc" | "round" | "floor" | "ceil" | "checked" | "sat" => {
                    if !self.arity(args, 1, n, span) {
                        return Some(error_expr(span));
                    }
                    let t = self.type_arg(args, 0, span)?;
                    let op = match n {
                        "trunc" => Builtin::Trunc,
                        "round" => Builtin::Round,
                        "floor" => Builtin::Floor,
                        "ceil" => Builtin::Ceil,
                        "checked" => Builtin::Checked,
                        _ => Builtin::Sat,
                    };
                    let ok = match op {
                        Builtin::Trunc | Builtin::Checked | Builtin::Sat => t.is_integer(),
                        Builtin::Round => t.is_integer() || t.is_float(),
                        _ => t.is_integer() && is_float,
                    };
                    if !ok {
                        self.error(span, format!("`.{}` cannot convert to `{}`", n, t));
                    }
                    if is_float && matches!(op, Builtin::Round) && t.is_float() {
                        let (TyKind::Float(a), TyKind::Float(b2)) = (self.infer.resolve(recv.ty).kind(), t.kind())
                        else {
                            return Some(b(op, t, vec![recv]));
                        };
                        if b2.bits() >= a.bits() {
                            self.error(
                                span,
                                "`.round(F)` rounds to a narrower float type; widening is a plain conversion",
                            );
                        }
                    }
                    let ty = if op == Builtin::Checked { Ty::optional(t) } else { t };
                    return Some(b(op, ty, vec![recv]));
                }
                "bits" => {
                    if !self.arity(args, 1, n, span) {
                        return Some(error_expr(span));
                    }
                    let t = self.type_arg(args, 0, span)?;
                    return Some(self.bits_cast(recv, t, span));
                }
                "min" | "max" | "abs_diff" => {
                    if !self.arity(args, 1, n, span) {
                        return Some(error_expr(span));
                    }
                    let y = self.value_arg(args, 0, recv.ty, span);
                    let ty =
                        self.unify_operands(recv.ty, y.ty, span, &format!("the receiver and argument of `.{}`", n));
                    let (op, rty) = match n {
                        "min" => (Builtin::Min, ty),
                        "max" => (Builtin::Max, ty),
                        _ => {
                            self.obligations.push(Obligation::Numeric {
                                ty,
                                span,
                                op: "abs_diff",
                                int_only: true,
                                signed_only: false,
                            });
                            (Builtin::AbsDiff, unsigned_of(self.infer.resolve(ty)))
                        }
                    };
                    return Some(b(op, rty, vec![recv, y]));
                }
                "clamp" => {
                    if !self.arity(args, 2, n, span) {
                        return Some(error_expr(span));
                    }
                    let lo = self.value_arg(args, 0, recv.ty, span);
                    let hi = self.value_arg(args, 1, recv.ty, span);
                    let ty = self.unify_operands(recv.ty, lo.ty, span, "the arguments of `.clamp`");
                    self.unify_operands(ty, hi.ty, span, "the arguments of `.clamp`");
                    return Some(b(Builtin::Clamp, ty, vec![recv, lo, hi]));
                }
                "abs" => {
                    self.arity(args, 0, n, span);
                    self.obligations.push(Obligation::Numeric {
                        ty: recv.ty,
                        span,
                        op: "abs",
                        int_only: false,
                        signed_only: true,
                    });
                    let ty = recv.ty;
                    return Some(b(Builtin::Abs, ty, vec![recv]));
                }
                "pow" | "pow_wrap" | "pow_sat" | "pow_checked" if is_int => {
                    if !self.arity(args, 1, n, span) {
                        return Some(error_expr(span));
                    }
                    let e = self.value_arg(args, 0, Ty::u32(), span);
                    let flavor = match n {
                        "pow" => ast::Flavor::Checked,
                        "pow_wrap" => ast::Flavor::Wrap,
                        "pow_sat" => ast::Flavor::Sat,
                        _ => ast::Flavor::Opt,
                    };
                    let ty = if flavor == ast::Flavor::Opt { Ty::optional(recv.ty) } else { recv.ty };
                    return Some(b(Builtin::Pow(flavor), ty, vec![recv, e]));
                }
                "is_nan" | "is_inf" | "is_finite" | "sign_bit" if is_float => {
                    self.arity(args, 0, n, span);
                    let op = match n {
                        "is_nan" => Builtin::IsNan,
                        "is_inf" => Builtin::IsInf,
                        "is_finite" => Builtin::IsFinite,
                        _ => Builtin::SignBit,
                    };
                    return Some(b(op, Ty::bool(), vec![recv]));
                }
                "sqrt" if is_float => {
                    self.arity(args, 0, n, span);
                    let ty = recv.ty;
                    return Some(b(Builtin::Sqrt, ty, vec![recv]));
                }
                "to_ptr" if rty == Ty::usize() => {
                    let t = self.type_arg(args, 0, span)?;
                    return Some(b(Builtin::ToPtr, Ty::optional(t), vec![recv]));
                }
                _ => return None,
            }
        }
        match (rty.kind(), n) {
            (TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_), "addr") => {
                self.arity(args, 0, n, span);
                Some(b(Builtin::Addr, Ty::usize(), vec![recv]))
            }
            (TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::Optional(_), "cast") => {
                let t = self.type_arg(args, 0, span)?;
                Some(b(Builtin::Cast, t, vec![recv]))
            }
            (TyKind::Adt(..), "tag") if self.sema.union_variants(rty).is_some() => {
                self.arity(args, 0, n, span);
                let (_, tag) = self.sema.union_variants(rty).unwrap();
                Some(b(Builtin::Tag, tag, vec![recv]))
            }
            _ => self.collection_method(recv, rty, n, args, span),
        }
    }

    fn collection_method(&mut self, recv: Expr, rty: Ty, n: &str, args: &[ast::Arg], span: Span) -> Option<Expr> {
        // Methods of arrays and slices; a pointer to an array also works.
        let (base, elem, is_const) = match rty.kind() {
            TyKind::Slice { elem, is_const } => (recv, *elem, *is_const),
            TyKind::Array { elem, .. } => {
                let m = self.place_mutability(&recv);
                let c = m != Some(true);
                if m.is_none() && matches!(n, "get_ptr" | "fill" | "swap" | "copy_from") {
                    self.error(span, format!("`.{}` needs an array variable, not a temporary", n));
                }
                (recv, *elem, c)
            }
            TyKind::Ptr { pointee, is_const } if matches!(pointee.kind(), TyKind::Array { .. }) => {
                let TyKind::Array { elem, .. } = pointee.kind() else { unreachable!() };
                let d = mk(*pointee, tir::ExprKind::Deref(Box::new(recv)), span);
                (d, *elem, *is_const)
            }
            _ => return None,
        };
        let b = |op, ty, args: Vec<Expr>| mk(ty, tir::ExprKind::Builtin { op, args }, span);
        match n {
            "get" | "get_ptr" => {
                if !self.arity(args, 1, n, span) {
                    return Some(error_expr(span));
                }
                let i = self.value_arg(args, 0, Ty::usize(), span);
                if n == "get" {
                    Some(b(Builtin::Get, Ty::optional(elem), vec![base, i]))
                } else {
                    Some(b(Builtin::GetPtr, Ty::optional(Ty::ptr(is_const, elem)), vec![base, i]))
                }
            }
            "first" | "last" => {
                self.arity(args, 0, n, span);
                let op = if n == "first" { Builtin::First } else { Builtin::Last };
                Some(b(op, Ty::optional(elem), vec![base]))
            }
            _ => {
                self.error(span, format!("the slice method `.{}` is not supported yet", n));
                Some(error_expr(span))
            }
        }
    }

    /// `x.bits(T)`: reinterpret plain data of the same size (§6.6).
    fn bits_cast(&mut self, v: Expr, t: Ty, span: Span) -> Expr {
        let src = self.infer.resolve(v.ty);
        if !self.is_plain_data(t) {
            self.error(span, format!("`.bits` can only produce plain-data types, not `{}`", t));
        } else if !src.has_infer() {
            let (a, b) = self.sema.with_layouts(|l| (l.size_of(src), l.size_of(t)));
            if a != b {
                self.error(
                    span,
                    format!("`.bits` needs equal sizes, but `{}` has {} bytes and `{}` has {}", src, a, t, b),
                );
            }
        }
        mk(t, tir::ExprKind::Builtin { op: Builtin::Bits, args: vec![v] }, span)
    }

    /// `U.from_tag(x)` (§5.11).
    fn check_from_tag(&mut self, u: Ty, args: &[ast::Arg], span: Span) -> Expr {
        let Some((variants, tag)) = self.sema.union_variants(u) else {
            self.error(span, format!("`from_tag` is defined only for enums, not `{}`", u));
            return error_expr(span);
        };
        if variants.iter().any(|v| v.payload.is_some()) {
            self.error(span, format!("`{}.from_tag` needs an enum (a union without payloads)", u));
        }
        if !self.arity(args, 1, "from_tag", span) {
            return error_expr(span);
        }
        let x = self.value_arg(args, 0, tag, span);
        mk(Ty::optional(u), tir::ExprKind::Builtin { op: Builtin::FromTag, args: vec![x] }, span)
    }

    /// A function `name` of the module that declares the receiver's type, whose first
    /// run-time parameter takes the receiver (§7.10 rule 3).
    fn module_method(&mut self, recv: Expr, name: ast::Ident, args: &[ast::Arg], span: Span) -> Option<Expr> {
        let rty = self.infer.resolve(recv.ty);
        let s = match rty.kind() {
            TyKind::Ptr { pointee, .. } => *pointee,
            _ => rty,
        };
        let TyKind::Adt(id, _) = s.kind() else { return None };
        let module = self.sema.adts.get(id)?.module;
        let d = *self.sema.modules[module.0 as usize].scope.get(&name.name)?;
        let DeclValue::Fn(f) = self.sema.resolve_decl(d) else { return None };
        let sig = self.sema.sigs[&f].clone();
        // The receiver fills the first run-time parameter (§7.10).
        let Some(first) = sig.params.iter().find(|p| p.generic.is_none()) else {
            self.error(span, format!("`{}` takes no receiver", name.name));
            return Some(error_expr(span));
        };
        // Types match exactly, or (for generic methods) as instantiations of one generic type.
        let fits = |p: Ty, a: Ty| match (p.kind(), a.kind()) {
            _ if p == a => true,
            (TyKind::Adt(x, _), TyKind::Adt(y, _)) => x.template() == y.template(),
            _ => false,
        };
        // Receiver adjustments: `&e` for `^T`, `e^` for `T` given a pointer.
        let recv = match (first.ty.kind(), rty.kind()) {
            _ if fits(first.ty, rty) => recv,
            (TyKind::Ptr { is_const, pointee }, _) if fits(*pointee, rty) => match self.place_mutability(&recv) {
                Some(m) => {
                    if !m && !is_const {
                        self.error(recv.span, format!("`{}` needs a mutable receiver", name.name));
                    }
                    mk(Ty::ptr(*is_const, rty), tir::ExprKind::AddrOf(Box::new(recv)), span)
                }
                None if *is_const => mk(Ty::ptr(true, rty), tir::ExprKind::AddrOfTemp(Box::new(recv)), span),
                None => {
                    self.error(
                        recv.span,
                        format!("`{}` modifies its receiver, which must be a variable, not a temporary", name.name),
                    );
                    return Some(error_expr(span));
                }
            },
            (TyKind::Ptr { is_const: true, pointee }, TyKind::Ptr { pointee: p2, .. }) if fits(*pointee, *p2) => {
                let t = Ty::ptr(true, *p2);
                self.coerce(recv, t)
            }
            (_, TyKind::Ptr { pointee, .. }) if fits(first.ty, *pointee) => {
                mk(*pointee, tir::ExprKind::Deref(Box::new(recv)), span)
            }
            _ => {
                self.error(span, format!("`{}` cannot take a receiver of type `{}`", name.name, rty));
                return Some(error_expr(span));
            }
        };
        if !sig.generics.is_empty() {
            return Some(self.generic_call_with(f, Some(recv), args, span));
        }
        let rest: Vec<ParamInfo> = sig.params[1..].to_vec();
        let mut bound = vec![recv];
        bound.extend(self.bind_args(&rest, sig.module, args, span, "method"));
        self.record_effects(&sig.effects, span);
        Some(mk(sig.ret, tir::ExprKind::Call { callee: Callee::Fn(f), args: bound }, span))
    }
}

/// The unsigned integer type of the same width (`abs_diff`).
fn unsigned_of(t: Ty) -> Ty {
    match t.kind() {
        TyKind::Int(i) => Ty::int(false, i.bits),
        TyKind::Isize => Ty::usize(),
        _ => t,
    }
}
