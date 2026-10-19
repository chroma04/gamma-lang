//! Vector types `#simd [N]T` (§5.6.2): forming them, and their lane-wise operators,
//! conversions and builtin methods.

use super::func::{FnCx, error_expr};
use super::infer::Fallback;
use super::{ModuleId, Sema};
use crate::source::Span;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, Builtin, Expr};
use crate::types::{Ty, TyKind};

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

/// The largest vector, in bytes (§5.6.2).
pub const MAX_VECTOR_BYTES: u64 = 64;

/// True if `t` may be the lane type of a vector: a fixed-width integer of 8, 16, 32 or 64
/// bits, `f32`, `f64` or `bool`.
pub fn lane_type_ok(t: Ty) -> bool {
    match t.kind() {
        TyKind::Int(i) => matches!(i.bits, 8 | 16 | 32 | 64),
        TyKind::Float(f) => matches!(f.bits(), 32 | 64),
        TyKind::Bool => true,
        _ => false,
    }
}

/// The size of a lane of type `t` (one of [`lane_type_ok`]), in bytes.
pub fn lane_bytes(t: Ty) -> u64 {
    match t.kind() {
        TyKind::Int(i) => i.bits as u64 / 8,
        TyKind::Float(f) => f.bits() as u64 / 8,
        _ => 1,
    }
}

/// Why `len` lanes of `elem` do not make a vector, if they do not.
pub fn vector_shape_error(len: u64, elem: Ty) -> Option<String> {
    if !lane_type_ok(elem) {
        return Some(format!("a vector's lanes are `i8`–`i64`, `u8`–`u64`, `f32`, `f64` or `bool`, not `{}`", elem));
    }
    if len < 2 || !len.is_power_of_two() {
        return Some(format!("a vector has a power of two lanes, at least 2, not {}", len));
    }
    let bytes = len * lane_bytes(elem);
    if bytes > MAX_VECTOR_BYTES {
        return Some(format!(
            "a vector holds at most {} bytes; {} lanes of `{}` are {}",
            MAX_VECTOR_BYTES, len, elem, bytes
        ));
    }
    None
}

/// The mask type for vectors of `len` lanes: `#simd [len]bool`.
pub fn mask_ty(len: u64) -> Ty {
    Ty::vector(len, Ty::bool())
}

impl<'s> Sema<'s> {
    /// `#simd [N]T` (§5.6.2): `attrs` are the attributes before the array type `inner`.
    pub fn vector_type(&mut self, module: ModuleId, attrs: &[ast::Attribute], inner: &ast::Expr, span: Span) -> Ty {
        if let Some(a) = attrs.iter().find(|a| a.name.name.as_str() != "simd") {
            self.error(a.span, format!("`#{}` cannot precede an array type; only `#simd` can", a.name.name));
            return Ty::error();
        }
        if attrs.len() > 1 || attrs[0].args.is_some() {
            self.error(span, "`#simd` takes no arguments and is written once: `#simd [N]T`");
            return Ty::error();
        }
        let ast::ExprKind::ArrayType { len: Some(len), elem } = &inner.kind else {
            self.error(span, "`#simd` makes a vector of an array type with a constant length: `#simd [4]f32`");
            return Ty::error();
        };
        let elem = self.resolve_type(module, elem);
        if elem.is_error() {
            return Ty::error();
        }
        if super::generics::has_param(elem) {
            self.error(span, format!("a vector needs a concrete lane type, not `{}`", elem));
            return Ty::error();
        }
        let n = match self.array_len(module, len) {
            Some(super::ty_expr::ArrayLen::Known(n)) => n,
            Some(super::ty_expr::ArrayLen::Param(_)) => {
                self.error(len.span, "a vector's lane count is a constant, not a compile-time parameter");
                return Ty::error();
            }
            None => return Ty::error(),
        };
        if let Some(msg) = vector_shape_error(n, elem) {
            self.error(span, msg);
            return Ty::error();
        }
        Ty::vector(n, elem)
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// A binary operator whose left operand is the vector `lhs` (§7.4.7): every operator
    /// works lane by lane; `is` compares whole vectors and is handled as for arrays.
    pub fn vector_binary(&mut self, op: ast::BinOp, lhs: Expr, rhs: &ast::Expr, span: Span) -> Expr {
        use ast::BinOp as B;
        let vt = self.infer.resolve(lhs.ty);
        let (_, lane) = vt.vector_parts().expect("a vector operand");
        match op {
            B::And | B::Or => {
                self.expr(rhs, None);
                self.error(
                    span,
                    format!("`{}` takes `bool` values; on the lanes of a mask use `&`, `|` and `~`", op.as_str()),
                );
                return error_expr(span);
            }
            B::In | B::NotIn | B::Range | B::RangeIncl => {
                self.expr(rhs, None);
                self.error(span, format!("`{}` has no meaning on vectors", op.as_str()));
                return error_expr(span);
            }
            B::Shl(_) | B::Shr => {
                let r = self.expr(rhs, None);
                if !lane.is_integer() {
                    self.error(span, format!("`{}` shifts integer lanes, not the lanes of `{}`", op.as_str(), vt));
                    return error_expr(span);
                }
                if op == B::Shl(ast::Flavor::Opt) {
                    self.error(span, "`<<?` is not available on vectors: a lane cannot be `null`");
                    return error_expr(span);
                }
                if !self.infer.require_unsigned(r.ty, Fallback::Usize) && !r.ty.is_error() {
                    let t = self.infer.resolve(r.ty);
                    self.error(r.span, format!("a shift amount must have an unsigned integer type, not `{}`", t));
                }
                let top = if let B::Shl(f) = op { tir::BinOp::Shl(f) } else { tir::BinOp::Shr };
                return mk(vt, tir::ExprKind::Binary { op: top, lhs: Box::new(lhs), rhs: Box::new(r) }, span);
            }
            _ => {}
        }
        let r = self.expr(rhs, Some(vt));
        let rt = self.infer.resolve(r.ty);
        if rt.is_error() {
            return error_expr(span);
        }
        if rt != vt {
            let hint = if rt.is_vector() { "" } else { "; a scalar is not spread over the lanes: use `V(fill: x)`" };
            self.error(
                span,
                format!("the operands of `{}` have different types: `{}` and `{}`{}", op.as_str(), vt, rt, hint),
            );
            return error_expr(span);
        }
        let numeric = lane.is_integer() || lane.is_float();
        let lane_err = |cx: &mut Self, what: &str| {
            cx.error(span, format!("`{}` applies to {}, not to the lanes of `{}`", op.as_str(), what, vt));
            error_expr(span)
        };
        let binary = |b: tir::BinOp, l: Expr, r: Expr| {
            mk(vt, tir::ExprKind::Binary { op: b, lhs: Box::new(l), rhs: Box::new(r) }, span)
        };
        match op {
            B::Lt | B::Le | B::Gt | B::Ge => {
                let m = match op {
                    B::Lt => "lanes_lt",
                    B::Le => "lanes_le",
                    B::Gt => "lanes_gt",
                    _ => "lanes_ge",
                };
                self.error(span, format!("vectors are not ordered; compare lanes with `.{}(w)` (§5.6.2)", m));
                error_expr(span)
            }
            B::Add(f) | B::Sub(f) | B::Mul(f) => {
                if !numeric {
                    return lane_err(self, "numeric lanes");
                }
                if f == ast::Flavor::Opt {
                    self.error(span, format!("`{}` is not available on vectors: a lane cannot be `null`", op.as_str()));
                    return error_expr(span);
                }
                if f != ast::Flavor::Checked && !lane.is_integer() {
                    return lane_err(self, "integer lanes");
                }
                let top = match op {
                    B::Add(f) => tir::BinOp::Add(f),
                    B::Sub(f) => tir::BinOp::Sub(f),
                    _ => tir::BinOp::Mul(f),
                };
                binary(top, lhs, r)
            }
            B::Div if lane.is_float() => binary(tir::BinOp::Div, lhs, r),
            B::Div | B::Rem => {
                self.error(
                    span,
                    format!(
                        "`{}` is not available on vectors of `{}`: targets have no instruction that divides \
                         integer lanes, so the cost would be hidden; divide the lanes one by one",
                        op.as_str(),
                        lane
                    ),
                );
                error_expr(span)
            }
            B::BitAnd | B::BitOr | B::BitXor => {
                if lane.is_float() {
                    return lane_err(self, "integer and `bool` lanes");
                }
                let top = match op {
                    B::BitAnd => tir::BinOp::BitAnd,
                    B::BitOr => tir::BinOp::BitOr,
                    _ => tir::BinOp::BitXor,
                };
                binary(top, lhs, r)
            }
            _ => unreachable!("handled above"),
        }
    }

    /// A unary operator on the vector `v` (§7.4.7).
    pub fn vector_unary(&mut self, op: ast::UnOp, v: Expr, span: Span) -> Expr {
        let vt = self.infer.resolve(v.ty);
        let (_, lane) = vt.vector_parts().expect("a vector operand");
        let top = match op {
            ast::UnOp::Neg if lane.is_signed_integer() || lane.is_float() => tir::UnOp::Neg,
            ast::UnOp::BitNot if !lane.is_float() => tir::UnOp::BitNot,
            ast::UnOp::Not => {
                let hint = if lane.is_bool() { "; the lanes of a mask are complemented by `~m`" } else { "" };
                self.error(span, format!("`not` takes a `bool`, not `{}`{}", vt, hint));
                return error_expr(span);
            }
            _ => {
                let s = if op == ast::UnOp::Neg { "-" } else { "~" };
                self.error(span, format!("`{}` does not apply to the lanes of `{}`", s, vt));
                return error_expr(span);
            }
        };
        mk(vt, tir::ExprKind::Unary { op: top, operand: Box::new(v) }, span)
    }

    /// A builtin method of the vector `recv` (§21.3), or `None` if `n` is not one.
    pub fn vector_method(&mut self, recv: Expr, vt: Ty, n: &str, args: &[ast::Arg], span: Span) -> Expr {
        let (len, lane) = vt.vector_parts().expect("a vector receiver");
        let numeric = lane.is_integer() || lane.is_float();
        let b = |op, ty, args: Vec<Expr>| mk(ty, tir::ExprKind::Builtin { op, args }, span);
        let needs = |cx: &mut Self, what: &str| {
            cx.error(span, format!("`.{}` needs {}, and `{}` has `{}` lanes", n, what, vt, lane));
            error_expr(span)
        };
        match n {
            "reduce_sum" | "reduce_min" | "reduce_max" => {
                if !self.arity(args, 0, n, span) {
                    return error_expr(span);
                }
                if !numeric {
                    return needs(self, "numeric lanes");
                }
                let op = match n {
                    "reduce_sum" => Builtin::ReduceSum,
                    "reduce_min" => Builtin::ReduceMin,
                    _ => Builtin::ReduceMax,
                };
                b(op, lane, vec![recv])
            }
            "lanes_eq" | "lanes_ne" | "lanes_lt" | "lanes_le" | "lanes_gt" | "lanes_ge" => {
                if !self.arity(args, 1, n, span) {
                    return error_expr(span);
                }
                if !matches!(n, "lanes_eq" | "lanes_ne") && !numeric {
                    return needs(self, "numeric lanes");
                }
                let y = self.value_arg(args, 0, vt, span);
                let op = match n {
                    "lanes_eq" => Builtin::LaneEq,
                    "lanes_ne" => Builtin::LaneNe,
                    "lanes_lt" => Builtin::LaneLt,
                    "lanes_le" => Builtin::LaneLe,
                    "lanes_gt" => Builtin::LaneGt,
                    _ => Builtin::LaneGe,
                };
                b(op, mask_ty(len), vec![recv, y])
            }
            "all" | "any" | "count" => {
                if !self.arity(args, 0, n, span) {
                    return error_expr(span);
                }
                if !lane.is_bool() {
                    return needs(self, "a mask (`bool` lanes)");
                }
                let (op, ty) = match n {
                    "all" => (Builtin::MaskAll, Ty::bool()),
                    "any" => (Builtin::MaskAny, Ty::bool()),
                    _ => (Builtin::MaskCount, Ty::usize()),
                };
                b(op, ty, vec![recv])
            }
            "select" => {
                if !self.arity(args, 2, n, span) {
                    return error_expr(span);
                }
                if !lane.is_bool() {
                    return needs(self, "a mask (`bool` lanes)");
                }
                let x = match args.first().map(|a| &a.kind) {
                    Some(ArgKind::Positional(e)) => self.expr(e, None),
                    _ => return error_expr(span),
                };
                let xt = self.infer.resolve(x.ty);
                if xt.is_error() {
                    return error_expr(span);
                }
                match xt.vector_parts() {
                    Some((m, _)) if m == len => {}
                    _ => {
                        self.error(
                            x.span,
                            format!("`m.select(a, b)` takes vectors of {} lanes, like `{}`; not `{}`", len, vt, xt),
                        );
                        return error_expr(span);
                    }
                }
                let y = self.value_arg(args, 1, xt, span);
                b(Builtin::Select, xt, vec![recv, x, y])
            }
            "min" | "max" => {
                if !self.arity(args, 1, n, span) {
                    return error_expr(span);
                }
                if !numeric {
                    return needs(self, "numeric lanes");
                }
                let y = self.value_arg(args, 0, vt, span);
                b(if n == "min" { Builtin::LaneMin } else { Builtin::LaneMax }, vt, vec![recv, y])
            }
            "abs" => {
                if !self.arity(args, 0, n, span) {
                    return error_expr(span);
                }
                if !(lane.is_signed_integer() || lane.is_float()) {
                    return needs(self, "signed integer or float lanes");
                }
                b(Builtin::LaneAbs, vt, vec![recv])
            }
            "to" => {
                if !self.arity(args, 1, n, span) {
                    return error_expr(span);
                }
                let Some(f) = self.type_arg(args, 0, span) else { return error_expr(span) };
                if !f.is_float() || !lane_type_ok(f) || !numeric {
                    self.error(
                        span,
                        format!("`.to(F)` converts numeric lanes to `f32` or `f64` lanes, not `{}` to `{}`", lane, f),
                    );
                    return error_expr(span);
                }
                let t = Ty::vector(len, f);
                if let Some(msg) = vector_shape_error(len, f) {
                    self.error(span, msg);
                    return error_expr(span);
                }
                b(Builtin::LaneConvert, t, vec![recv])
            }
            "shuffle" => self.vector_shuffle(recv, vt, args, span),
            "store" => {
                if !self.arity(args, 1, n, span) {
                    return error_expr(span);
                }
                let dst = self.value_arg(args, 0, Ty::slice(false, lane), span);
                b(Builtin::VecStore, Ty::unit(), vec![recv, dst])
            }
            _ => {
                self.error(
                    span,
                    format!(
                        "vectors have no method `.{}`; they have `reduce_sum`, `reduce_min`, `reduce_max`, `min`, \
                         `max`, `abs`, `to`, `shuffle`, `store` and the comparisons `lanes_eq`, `lanes_ne`, `lanes_lt`, \
                         `lanes_le`, `lanes_gt` and `lanes_ge`, and masks `all`, `any`, `count` and `select` (§21.3)",
                        n
                    ),
                );
                error_expr(span)
            }
        }
    }

    /// `v.shuffle(i0, i1, …)`: a vector of the lanes `v[i0], v[i1], …`, whose indices are
    /// compile-time constants below the lane count.
    fn vector_shuffle(&mut self, recv: Expr, vt: Ty, args: &[ast::Arg], span: Span) -> Expr {
        let (len, lane) = vt.vector_parts().expect("a vector receiver");
        let mut idx = vec![];
        for a in args {
            let ArgKind::Positional(e) = &a.kind else {
                self.error(a.span, "`.shuffle` takes lane indices, written positionally");
                return error_expr(span);
            };
            let v = self.expr(e, Some(Ty::usize()));
            let k = match &v.kind {
                tir::ExprKind::Int(i) if self.infer.unify(v.ty, Ty::usize()).is_ok() => {
                    num_traits::ToPrimitive::to_u64(i)
                }
                _ if v.ty.is_error() => return error_expr(span),
                _ if self.infer.resolve(v.ty) != Ty::usize() => {
                    let t = self.infer.resolve(v.ty);
                    self.error(v.span, format!("a lane index is a `usize`, not `{}`", t));
                    return error_expr(span);
                }
                tir::ExprKind::Const(_) => {
                    self.sema.eval_tir_const(&v).and_then(|c| self.sema.const_key_of_bytes(c.ty, &c.bytes).as_u64())
                }
                _ => None,
            };
            match k {
                Some(k) if k < len => idx.push(mk(Ty::usize(), tir::ExprKind::Int(k.into()), v.span)),
                Some(k) => {
                    self.error(v.span, format!("lane {} does not exist: `{}` has lanes 0 to {}", k, vt, len - 1));
                    return error_expr(span);
                }
                None => {
                    self.error(v.span, "a lane index of `.shuffle` must be a compile-time constant");
                    return error_expr(span);
                }
            }
        }
        let m = idx.len() as u64;
        if let Some(msg) = vector_shape_error(m, lane) {
            self.error(span, format!("`.shuffle` makes a vector of one lane per index: {}", msg));
            return error_expr(span);
        }
        let t = Ty::vector(m, lane);
        let mut all = vec![recv];
        all.extend(idx);
        mk(t, tir::ExprKind::Builtin { op: Builtin::Shuffle, args: all }, span)
    }

    /// `V.load(xs)`: a vector of the first lanes of the slice `xs` (checked: long enough).
    pub fn vector_load(&mut self, vt: Ty, args: &[ast::Arg], span: Span) -> Expr {
        let (_, lane) = vt.vector_parts().expect("a vector type");
        if !self.arity(args, 1, "load", span) {
            return error_expr(span);
        }
        let s = self.value_arg(args, 0, Ty::slice(true, lane), span);
        mk(vt, tir::ExprKind::Builtin { op: Builtin::VecLoad, args: vec![s] }, span)
    }

    /// `T(x)` where `T` or `x`'s type is a vector (§6.5), or `None` if neither is.
    /// A vector converts to and from the array of its lanes, and to a vector of as many
    /// lanes whose type each lane converts to losslessly.
    pub fn vector_conversion(&mut self, target: Ty, x: Expr, span: Span) -> Option<Expr> {
        let st = self.infer.resolve(x.ty);
        if !target.is_vector() && !st.is_vector() {
            return None;
        }
        let parts = |t: Ty| match t.kind() {
            TyKind::Array { len, elem, .. } => Some((*len, *elem)),
            _ => None,
        };
        let (Some((n, a)), Some((m, b))) = (parts(st), parts(target)) else {
            self.error(span, format!("cannot convert `{}` to `{}`", st, target));
            return Some(error_expr(span));
        };
        let both = st.is_vector() && target.is_vector();
        if n == m && a == b && (both || st.is_positional_array() || target.is_positional_array()) {
            // The same lanes: a vector and its array differ only in alignment.
            return Some(mk(target, tir::ExprKind::Builtin { op: Builtin::Bits, args: vec![x] }, span));
        }
        if both && n == m && super::finalize::lossless(a, b) {
            return Some(mk(target, tir::ExprKind::Builtin { op: Builtin::LaneConvert, args: vec![x] }, span));
        }
        let why =
            if both && n == m { format!("; `{}` does not convert to `{}` losslessly", a, b) } else { String::new() };
        self.error(span, format!("cannot convert `{}` to `{}`{}", st, target, why));
        Some(error_expr(span))
    }
}
