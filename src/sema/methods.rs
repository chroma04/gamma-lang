//! Method calls (§7.10): struct fields holding function pointers, builtin
//! methods (§6.6, §21.3) and functions of the receiver type's module.

use super::func::{FnCx, Obligation, error_expr};
use super::generics::Constraint;
use super::{DeclValue, ParamInfo};
use crate::diag::Diagnostic;
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
            if t.is_vector() && name.name.as_str() == "load" {
                return self.vector_load(t, args, span);
            }
            return self.construct_variant(t, name, Some(args), span);
        }
        let recv = self.expr(base, None);
        let rty = self.infer.resolve(recv.ty);
        if rty.is_error() {
            return error_expr(span);
        }
        // 1. A field (holding a function pointer).
        let derefed_ty = match self.infer.shallow(recv.ty).kind() {
            TyKind::Ptr { pointee, .. } => *pointee,
            _ => recv.ty,
        };
        if let Some(fields) = self.sema.struct_fields(derefed_ty)
            && let Some(i) = fields.iter().position(|f| f.name == name.name)
        {
            let fty = fields[i].ty;
            let derefed = self.auto_deref(recv);
            let callee = mk(fty, tir::ExprKind::Field { base: Box::new(derefed), index: i as u32 }, span);
            return self.call_value(callee, args, span);
        }
        // 2. A builtin method.
        let mut recv = recv;
        if let Some(e) = self.builtin_method(&mut recv, name, args, span) {
            return e;
        }
        // `r.len()` of a range is a function of the core library (§21.3).
        let name = match self.sema.range_of(rty) {
            Some((inclusive, _)) if name.name.as_str() == "len" => ast::Ident {
                name: crate::symbol::Symbol::intern(if inclusive { "range_incl_len" } else { "range_len" }),
                span: name.span,
            },
            _ => name,
        };
        // 3. A function of the module that declares the receiver's type.
        if let Some(e) = self.module_method(recv, name, args, span) {
            return e;
        }
        let rty = self.infer.resolve(rty);
        let d = Diagnostic::error(name.span, format!("type `{}` has no method `{}`", rty, name.name));
        self.emit_with_continuation_note(d, base.span, name.span);
        error_expr(span)
    }

    /// Emits `d`, a missing field or method `.name` of `base`, adding a note when `.name`
    /// starts a line: such a line continues the previous one (§2.8), which surprises
    /// code that meant `.Variant` as a value of its own.
    pub fn emit_with_continuation_note(&mut self, d: Diagnostic, base: Span, name: Span) {
        let file = self.sema.sess.sources.file(base.file);
        let mut d = d;
        if base.file == name.file && file.line_col(base.hi).0 < file.line_col(name.lo).0 {
            d = d.with_note(
                "a line that starts with `.name` continues the previous line (§2.8); to begin a new \
                 expression with a variant, name its type: `T.Variant`",
            );
        }
        self.sema.emit(d);
    }

    pub(super) fn type_arg(&mut self, args: &[ast::Arg], i: usize, span: Span) -> Option<Ty> {
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

    pub(super) fn value_arg(&mut self, args: &[ast::Arg], i: usize, ty: Ty, span: Span) -> Expr {
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

    pub(super) fn arity(&mut self, args: &[ast::Arg], n: usize, name: &str, span: Span) -> bool {
        if args.len() != n {
            self.error(span, format!("`{}` takes {} argument{}", name, n, if n == 1 { "" } else { "s" }));
            return false;
        }
        true
    }

    /// Whether `t` is a type parameter with constraint `c`.
    fn param_with(&self, t: Ty, c: Constraint) -> bool {
        matches!(t.kind(), TyKind::Param(p) if self.sema.param_has(*p, c))
    }

    /// The builtin methods of §21.3 and the conversions of §6.6.
    fn builtin_method(&mut self, recv: &mut Expr, name: ast::Ident, args: &[ast::Arg], span: Span) -> Option<Expr> {
        let n = name.name.as_str();
        let rty = self.infer.resolve(recv.ty);
        // A type parameter constrained `Int` or `Float` has the methods of its types (§16.3).
        let is_int = rty.is_integer()
            || self.infer.var_kind(rty) == Some(super::infer::VarKind::Int)
            || self.param_with(rty, Constraint::Int);
        let is_float = rty.is_float()
            || self.infer.var_kind(rty) == Some(super::infer::VarKind::Float)
            || self.param_with(rty, Constraint::Float);
        let b = |op, ty, args: Vec<Expr>| mk(ty, tir::ExprKind::Builtin { op, args }, span);
        if is_int || is_float {
            return self.numeric_method(recv, rty, n, (is_int, is_float), args, span);
        }
        match (rty.kind(), n) {
            (TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_), "addr") => {
                self.arity(args, 0, n, span);
                Some(b(Builtin::Addr, Ty::usize(), vec![take_expr(recv)]))
            }
            (
                TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) | TyKind::Optional(_),
                "cast",
            ) => self.pointer_cast(recv, rty, args, span),
            // From an embedded struct back to the struct embedding it (§5.10.3); unchecked.
            (TyKind::Ptr { is_const, pointee }, "downcast_unchecked") => {
                let (is_const, pointee) = (*is_const, *pointee);
                let outer = self.type_arg(args, 0, span)?;
                let target = Ty::ptr(is_const, outer);
                if !self.embeds(Ty::ptr(is_const, outer), Ty::ptr(is_const, pointee)) {
                    self.error(span, format!("`{}` does not embed `{}` with `using`", outer, pointee));
                    return Some(error_expr(span));
                }
                Some(mk(
                    target,
                    tir::ExprKind::Coerce { kind: tir::Coercion::PtrToPtr, expr: Box::new(take_expr(recv)) },
                    span,
                ))
            }
            // The length of a C string, up to its NUL (§5.8).
            (TyKind::ManyPtr { elem, .. }, "cstr_len") if *elem == Ty::u8() => {
                self.arity(args, 0, n, span);
                Some(b(Builtin::CstrLen, Ty::usize(), vec![take_expr(recv)]))
            }
            // Reinterpreting bytes (§6.6): any plain-data or `#bits` value as plain data.
            (_, "bits") if self.is_plain_data(rty) || rty.is_bool() || self.sema.bits_struct_ty(rty).is_some() => {
                if !self.arity(args, 1, n, span) {
                    return Some(error_expr(span));
                }
                let t = self.type_arg(args, 0, span)?;
                Some(self.bits_cast(take_expr(recv), t, span))
            }
            (TyKind::Adt(..), "tag") if self.sema.union_variants(rty).is_some() => {
                self.arity(args, 0, n, span);
                let (_, tag) = self.sema.union_variants(rty).unwrap();
                Some(b(Builtin::Tag, tag, vec![take_expr(recv)]))
            }
            _ => self.collection_method(recv, rty, n, args, span),
        }
    }

    /// The builtin methods of integers and floats; the flags say which the receiver is.
    fn numeric_method(
        &mut self,
        recv: &mut Expr,
        rty: Ty,
        n: &str,
        (is_int, is_float): (bool, bool),
        args: &[ast::Arg],
        span: Span,
    ) -> Option<Expr> {
        let b = |op, ty, args: Vec<Expr>| mk(ty, tir::ExprKind::Builtin { op, args }, span);
        match n {
            "trunc" | "round" | "floor" | "ceil" | "checked" | "sat" => {
                self.numeric_conversion(recv, n, is_float, args, span)
            }
            // `x.to(F)`: the nearest value of float type `F` (§6.6).
            "to" => {
                if !self.arity(args, 1, n, span) {
                    return Some(error_expr(span));
                }
                let t = self.type_arg(args, 0, span)?;
                if !(t.is_float() || self.param_with(t, Constraint::Float)) {
                    self.error(
                        span,
                        format!("`.to` converts to a float type, not `{}`; use `.trunc`, `.round`, `.checked` or `.sat` for integers", t),
                    );
                }
                Some(b(Builtin::Round, t, vec![take_expr(recv)]))
            }
            "unsigned_abs" if is_int => {
                self.arity(args, 0, n, span);
                self.obligations.push(Obligation::Numeric {
                    ty: recv.ty,
                    span,
                    op: "unsigned_abs",
                    int_only: true,
                    signed_only: false,
                });
                let ty = recv.ty;
                let zero = mk(ty, tir::ExprKind::Int(num_bigint::BigInt::from(0)), span);
                let rty = unsigned_of(self.infer.resolve(ty));
                Some(b(Builtin::AbsDiff, rty, vec![take_expr(recv), zero]))
            }
            "bits" => {
                if !self.arity(args, 1, n, span) {
                    return Some(error_expr(span));
                }
                let t = self.type_arg(args, 0, span)?;
                Some(self.bits_cast(take_expr(recv), t, span))
            }
            "min" | "max" | "abs_diff" => self.numeric_pair(recv, n, args, span),
            "clamp" => {
                if !self.arity(args, 2, n, span) {
                    return Some(error_expr(span));
                }
                let lo = self.value_arg(args, 0, recv.ty, span);
                let hi = self.value_arg(args, 1, recv.ty, span);
                let ty = self.unify_operands(recv.ty, lo.ty, span, "the arguments of `.clamp`");
                self.unify_operands(ty, hi.ty, span, "the arguments of `.clamp`");
                Some(b(Builtin::Clamp, ty, vec![take_expr(recv), lo, hi]))
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
                Some(b(Builtin::Abs, ty, vec![take_expr(recv)]))
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
                Some(b(Builtin::Pow(flavor), ty, vec![take_expr(recv), e]))
            }
            "is_nan" | "is_inf" | "is_finite" | "sign_bit" if is_float => {
                self.arity(args, 0, n, span);
                let op = match n {
                    "is_nan" => Builtin::IsNan,
                    "is_inf" => Builtin::IsInf,
                    "is_finite" => Builtin::IsFinite,
                    _ => Builtin::SignBit,
                };
                Some(b(op, Ty::bool(), vec![take_expr(recv)]))
            }
            "sqrt" if is_float => {
                self.arity(args, 0, n, span);
                let ty = recv.ty;
                Some(b(Builtin::Sqrt, ty, vec![take_expr(recv)]))
            }
            "to_ptr" if rty == Ty::usize() => {
                let t = self.type_arg(args, 0, span)?;
                Some(b(Builtin::ToPtr, Ty::optional(t), vec![take_expr(recv)]))
            }
            _ => None,
        }
    }

    /// `x.trunc(T)`, `.round`, `.floor`, `.ceil`, `.checked` and `.sat`: conversions to an integer
    /// type (§6.6).
    fn numeric_conversion(
        &mut self,
        recv: &mut Expr,
        n: &str,
        is_float: bool,
        args: &[ast::Arg],
        span: Span,
    ) -> Option<Expr> {
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
        let t_int = t.is_integer() || self.param_with(t, Constraint::Int);
        let t_float = t.is_float() || self.param_with(t, Constraint::Float);
        let ok = match op {
            Builtin::Trunc | Builtin::Checked | Builtin::Sat => t_int,
            Builtin::Round => t_int,
            _ => t_int && is_float,
        };
        if op == Builtin::Round && t_float && !t_int {
            self.error(span, format!("`.round` converts to an integer; for the nearest `{}` use `.to({})`", t, t));
        } else if !ok {
            self.error(span, format!("`.{}` cannot convert to `{}`", n, t));
        }
        let ty = if op == Builtin::Checked { Ty::optional(t) } else { t };
        Some(mk(ty, tir::ExprKind::Builtin { op, args: vec![take_expr(recv)] }, span))
    }

    /// `x.min(y)`, `x.max(y)` and `x.abs_diff(y)`.
    fn numeric_pair(&mut self, recv: &mut Expr, n: &str, args: &[ast::Arg], span: Span) -> Option<Expr> {
        if !self.arity(args, 1, n, span) {
            return Some(error_expr(span));
        }
        let y = self.value_arg(args, 0, recv.ty, span);
        let ty = self.unify_operands(recv.ty, y.ty, span, &format!("the receiver and argument of `.{}`", n));
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
        Some(mk(rty, tir::ExprKind::Builtin { op, args: vec![take_expr(recv), y] }, span))
    }

    /// `p.cast(T)`: a conversion between pointer types.
    fn pointer_cast(&mut self, recv: &mut Expr, rty: Ty, args: &[ast::Arg], span: Span) -> Option<Expr> {
        let t = self.type_arg(args, 0, span)?;
        // Data pointers convert to data pointers, function pointers to function pointers.
        let kind = |t: Ty| match t.kind() {
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr => Some(false),
            TyKind::FnPtr(_) => Some(true),
            _ => None,
        };
        let same = |a: Ty, b: Ty| kind(a).is_some() && kind(a) == kind(b);
        let ok = match (rty.kind(), t.kind()) {
            (TyKind::Optional(a), TyKind::Optional(b)) => same(*a, *b),
            (TyKind::Optional(_), _) | (_, TyKind::Optional(_)) => false,
            _ => same(rty, t),
        };
        if !ok {
            self.error(
                span,
                format!(
                    "`cast` converts between pointer types, not from `{}` to `{}`; use `.addr()` for an address",
                    rty, t
                ),
            );
            return Some(error_expr(span));
        }
        // A function pointer converts to one with the same shape (§6.6).
        let fn_of = |t: Ty| match t.kind() {
            TyKind::Optional(i) => i.kind().clone(),
            k => k.clone(),
        };
        if let (TyKind::FnPtr(a), TyKind::FnPtr(b)) = (fn_of(rty), fn_of(t))
            && let Some(why) = fn_cast_mismatch(&a, &b)
        {
            self.error(span, format!("cannot cast `{}` to `{}`: {}", rty, t, why));
            return Some(error_expr(span));
        }
        Some(mk(t, tir::ExprKind::Builtin { op: Builtin::Cast, args: vec![take_expr(recv)] }, span))
    }

    fn collection_method(&mut self, recv: &mut Expr, rty: Ty, n: &str, args: &[ast::Arg], span: Span) -> Option<Expr> {
        // Methods of arrays and slices; a pointer to an array also works.
        let arr = match rty.kind() {
            TyKind::Ptr { pointee, .. } => *pointee,
            _ => rty,
        };
        if arr.is_vector() {
            let recv = match rty.kind() {
                TyKind::Ptr { .. } => mk(arr, tir::ExprKind::Deref(Box::new(take_expr(recv))), span),
                _ => take_expr(recv),
            };
            return Some(self.vector_method(recv, arr, n, args, span));
        }
        if arr.enum_index().is_some() {
            self.error(
                span,
                format!("`{}` has no methods; `a[..]` is a slice of its elements, which has `.{}`", arr, n),
            );
            return Some(error_expr(span));
        }
        let (base, elem, is_const) = match rty.kind() {
            TyKind::Slice { elem, is_const } => (take_expr(recv), *elem, *is_const),
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => {
                let m = self.place_mutability(recv);
                let c = m != Some(true);
                if m.is_none() && matches!(n, "get_ptr" | "fill" | "swap" | "copy_from") {
                    self.error(span, format!("`.{}` needs an array variable, not a temporary", n));
                }
                (take_expr(recv), *elem, c)
            }
            TyKind::Ptr { pointee, is_const } if pointee.is_array() => {
                let (d, elem) = super::seq::deref_array_ptr(take_expr(recv), *pointee, span);
                (d, elem, *is_const)
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
            _ if SLICE_FNS.contains(&n) => Some(self.slice_fn(base, n, args, span)),
            _ => {
                if n == "len" || n == "ptr" {
                    let msg = format!("`.{}` of a slice is a field, not a method: write `s.{}` without `()`", n, n);
                    let mut d = Diagnostic::error(span, msg);
                    if args.is_empty() && self.sema.sess.sources.text(span).ends_with("()") {
                        let parens = Span { lo: span.hi - 2, ..span };
                        d = d.with_fix("remove `()`", parens, "");
                    }
                    self.sema.emit(d);
                } else {
                    self.error(span, format!("slices have no method `.{}` (§21.3)", n));
                }
                Some(error_expr(span))
            }
        }
    }

    /// A method of arrays and slices written in Gamma, in the core module `core/slice`:
    /// `a.sort()` calls `sort(a[..])` (§21.3).
    fn slice_fn(&mut self, base: Expr, n: &str, args: &[ast::Arg], span: Span) -> Expr {
        let Some(f) = self.sema.core_fn("slice", n) else {
            self.error(span, format!("the slice method `.{}` is missing from the core library", n));
            return error_expr(span);
        };
        let bt = self.infer.resolve(base.ty);
        let recv = match bt.kind() {
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => {
                let elem = *elem;
                let m = self.place_mutability(&base);
                if m.is_none() {
                    self.error(span, format!("`.{}` needs an array variable, not a temporary", n));
                    return error_expr(span);
                }
                let is_const = m != Some(true);
                let p = mk(Ty::ptr(is_const, bt), tir::ExprKind::AddrOf(Box::new(base)), span);
                self.coerce(p, Ty::intern(TyKind::Slice { elem, is_const }))
            }
            _ => base,
        };
        self.generic_call_with(f, Some(recv), args, span)
    }

    /// `x.bits(T)`: reinterpret plain data of the same size (§6.6).
    fn bits_cast(&mut self, v: Expr, t: Ty, span: Span) -> Expr {
        let src = self.infer.resolve(v.ty);
        if !self.is_plain_data(t) {
            self.error(span, format!("`.bits` can only produce plain-data types, not `{}`", t));
        } else if !src.has_infer() && !super::generics::has_param(src) && !super::generics::has_param(t) {
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

    /// The receiver `recv` (of type `rty`) adjusted to fill a first parameter of type
    /// `param`: `&e` for `^T`, `e^` for `T` given a pointer (§7.10). Reports and gives
    /// `Err` if it does not fit.
    fn adjust_receiver(&mut self, recv: Expr, rty: Ty, param: Ty, name: ast::Ident, span: Span) -> Result<Expr, ()> {
        // Types match exactly, or (for generic methods) as instantiations of one generic type.
        let fits = |p: Ty, a: Ty| match (p.kind(), a.kind()) {
            _ if p == a => true,
            (TyKind::Adt(x, _), TyKind::Adt(y, _)) => x.template() == y.template(),
            _ => false,
        };
        // Receiver adjustments: `&e` for `^T`, `e^` for `T` given a pointer.
        let adjusted = match (param.kind(), rty.kind()) {
            _ if fits(param, rty) => recv,
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
                    return Err(());
                }
            },
            (TyKind::Ptr { is_const: true, pointee }, TyKind::Ptr { pointee: p2, .. }) if fits(*pointee, *p2) => {
                let t = Ty::ptr(true, *p2);
                self.coerce(recv, t)
            }
            (TyKind::Ptr { is_const: false, pointee }, TyKind::Ptr { is_const: false, pointee: p2 })
                if fits(*pointee, *p2) =>
            {
                recv
            }
            (_, TyKind::Ptr { pointee, .. }) if fits(param, *pointee) => {
                mk(*pointee, tir::ExprKind::Deref(Box::new(recv)), span)
            }
            _ => {
                self.error(span, format!("`{}` cannot take a receiver of type `{}`", name.name, rty));
                return Err(());
            }
        };
        Ok(adjusted)
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
        let f = match self.sema.modules[module.0 as usize].scope.get(&name.name).copied() {
            Some(d) => match self.sema.resolve_decl(d) {
                DeclValue::Fn(f) => {
                    // A function whose first parameter is not the receiver's type leaves the
                    // name to an operation that takes it (§7.10).
                    let takes = |ty: Ty| {
                        let t = match ty.kind() {
                            TyKind::Ptr { pointee, .. } => *pointee,
                            _ => ty,
                        };
                        match (t.kind(), s.kind()) {
                            (TyKind::Adt(x, _), TyKind::Adt(y, _)) => x.template() == y.template(),
                            _ => t == s,
                        }
                    };
                    let first = self.sema.sigs[&f].params.iter().find(|p| p.generic.is_none()).map(|p| p.ty);
                    if first.is_some_and(|t| !takes(t)) {
                        match self.op_method(recv.clone(), rty, s, module, name, args, span) {
                            Some(e) => return Some(e),
                            None => f,
                        }
                    } else {
                        f
                    }
                }
                _ => return self.op_method(recv, rty, s, module, name, args, span),
            },
            None => return self.op_method(recv, rty, s, module, name, args, span),
        };
        let sig = self.sema.sigs[&f].clone();
        // The receiver fills the first run-time parameter (§7.10).
        let Some(first) = sig.params.iter().find(|p| p.generic.is_none()) else {
            self.error(span, format!("`{}` takes no receiver", name.name));
            return Some(error_expr(span));
        };
        let Ok(recv) = self.adjust_receiver(recv, rty, first.ty, name, span) else { return Some(error_expr(span)) };
        if !sig.generics.is_empty() {
            return Some(self.generic_call_with(f, Some(recv), args, span));
        }
        let rest: Vec<ParamInfo> = sig.params[1..].to_vec();
        let mut bound = vec![recv];
        let (vals, pre) = self.bind_args(&rest, sig.module, args, span, "method");
        bound.extend(vals);
        self.record_effects(&sig.effects, span);
        let call = mk(sig.ret, tir::ExprKind::Call { callee: Callee::Fn(f), args: bound }, span);
        Some(self.finish_call(pre, call, true))
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// `e.op(args)` where `op` is an operation of a non-generic effect declared in `module`
    /// whose first parameter takes `e` (by value or as a pointer to it): a perform.
    #[allow(clippy::too_many_arguments)]
    fn op_method(
        &mut self,
        recv: Expr,
        rty: Ty,
        s: Ty,
        module: super::ModuleId,
        name: ast::Ident,
        args: &[ast::Arg],
        span: Span,
    ) -> Option<Expr> {
        let cands = self.sema.modules[module.0 as usize].effect_ops.get(&name.name)?.clone();
        for (ed, od) in cands {
            let Some((_, op)) = self.sema.decls[od.0 as usize].op_of else { continue };
            let DeclValue::Effect(eff) = self.sema.resolve_decl(ed) else { continue };
            let info = &self.sema.effects[&eff];
            if !info.params.is_empty() || (self.module != module && !self.sema.decls[ed.0 as usize].item.is_pub) {
                continue;
            }
            let o = info.ops[op as usize].clone();
            let Some(first) = o.params.first() else { continue };
            let fit = match first.ty.kind() {
                TyKind::Ptr { pointee, .. } => *pointee == s,
                _ => first.ty == s,
            };
            if !fit {
                continue;
            }
            let Ok(recv) = self.adjust_receiver(recv, rty, first.ty, name, span) else { return Some(error_expr(span)) };
            let mut vals = vec![recv];
            let (rest, pre) = self.bind_args(&o.params[1..], module, args, span, "operation");
            vals.extend(rest);
            let eref = crate::types::EffectRef { effect: eff, args: Box::new([]) };
            self.performed.push((eref.clone(), span));
            if o.kind == ast::OpKind::Fn {
                self.fn_performs.push(span);
            }
            let call =
                mk(o.ret, tir::ExprKind::Call { callee: Callee::Perform { effect: eref, op }, args: vals }, span);
            return Some(self.finish_call(pre, call, true));
        }
        None
    }
}

/// The methods of arrays and slices implemented by `core/slice`.
pub(super) const SLICE_FNS: &[&str] = &[
    "swap",
    "fill",
    "copy_from",
    "sort",
    "sort_by",
    "sort_by_key",
    "stable_sort",
    "stable_sort_by",
    "stable_sort_by_key",
    "index_of",
    "starts_with",
    "ends_with",
    "min",
    "max",
    "binary_search",
    "utf8_valid",
    "cursor",
];

/// The unsigned integer type of the same width (`abs_diff`).
fn unsigned_of(t: Ty) -> Ty {
    match t.kind() {
        TyKind::Int(i) => Ty::int(false, i.bits),
        TyKind::Isize => Ty::usize(),
        _ => t,
    }
}

/// Why a function pointer type `from` cannot be cast to `to`, if it cannot: the calling
/// convention and the number of parameters must agree, and each parameter and the result
/// must be the same type or both be pointers (§6.6). Effect sets may differ.
fn fn_cast_mismatch(from: &crate::types::FnTy, to: &crate::types::FnTy) -> Option<String> {
    let pointer = |t: Ty| {
        let t = t.optional_inner().unwrap_or(t);
        match t.kind() {
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr => Some(false),
            TyKind::FnPtr(_) => Some(true),
            _ => None,
        }
    };
    let same_repr = |a: Ty, b: Ty| {
        a == b
            || (pointer(a).is_some()
                && pointer(a) == pointer(b)
                && a.optional_inner().is_some() == b.optional_inner().is_some())
    };
    if from.callconv != to.callconv {
        return Some("the calling conventions differ".to_string());
    }
    if from.variadic != to.variadic {
        return Some("only one of them is variadic".to_string());
    }
    if from.params.len() != to.params.len() {
        return Some(format!("they take {} and {} parameters", from.params.len(), to.params.len()));
    }
    for (i, (a, b)) in from.params.iter().zip(to.params.iter()).enumerate() {
        if !same_repr(*a, *b) {
            return Some(format!(
                "parameter {} is `{}` in one and `{}` in the other; only pointers convert to pointers",
                i + 1,
                a,
                b
            ));
        }
    }
    if !same_repr(from.ret, to.ret) {
        return Some(format!("the results `{}` and `{}` differ and are not both pointers", from.ret, to.ret));
    }
    None
}

/// Moves the expression out of `e`, leaving a placeholder.
fn take_expr(e: &mut Expr) -> Expr {
    let span = e.span;
    std::mem::replace(e, error_expr(span))
}
