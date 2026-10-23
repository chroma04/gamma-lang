//! Math vectors `#vec [N]T` (§5.6.3) and complex numbers `Complex(T)` (§5.3.1): their
//! operators (§7.4.7), methods and conversions.
//!
//! A math vector's operator is checked as the scalar operator on each pair of elements and
//! becomes a block that evaluates the operands once and builds the result element by
//! element, so every back end sees only scalar operations and their checks keep the
//! operator's span. A complex operator becomes a call of a function of `core/complex`.

use super::func::{FnCx, LocalBinding, error_expr};
use super::infer::Fallback;
use super::{ModuleId, Sema};
use crate::source::Span;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, Builtin, Expr, Stmt};
use crate::types::{Ty, TyKind};

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

/// Why `len` elements of `elem` do not make a math vector, if they do not.
pub fn math_vector_shape_error(len: u64, elem: Ty) -> Option<String> {
    if !(elem.is_integer() || elem.is_float()) {
        return Some(format!("a math vector's elements are integers or floats, not `{}`", elem));
    }
    if !(2..=4).contains(&len) {
        return Some(format!("a math vector has 2, 3 or 4 elements, not {}", len));
    }
    None
}

impl<'s> Sema<'s> {
    /// `T` if `t` is `Complex(T)` (§5.3.1).
    pub fn complex_of(&mut self, t: Ty) -> Option<Ty> {
        let TyKind::Adt(id, args) = t.kind() else { return None };
        let name = crate::symbol::Symbol::intern("Complex");
        let d = self.prelude.and_then(|p| self.modules[p.0 as usize].scope.get(&name).copied())?;
        match self.resolve_decl(d) {
            super::DeclValue::GenericType(template) if template == id.template() => {}
            _ => return None,
        }
        match args.first() {
            Some(crate::types::GenericArg::Type(e)) => Some(*e),
            _ => None,
        }
    }

    /// The element type of `t` if its arithmetic is one of §7.4.7: a math vector or a
    /// complex number.
    pub fn compound_number_elem(&mut self, t: Ty) -> Option<Ty> {
        match t.math_vector_parts() {
            Some((_, e)) => Some(e),
            None => self.complex_of(t),
        }
    }

    /// Checks the shape of `#vec [N]T` once its parts are known.
    pub fn math_vector_type(&mut self, _module: ModuleId, n: u64, elem: Ty, span: Span) -> Ty {
        if super::generics::has_param(elem) {
            self.error(span, format!("a math vector needs a concrete element type, not `{}`", elem));
            return Ty::error();
        }
        if let Some(msg) = math_vector_shape_error(n, elem) {
            self.error(span, msg);
            return Ty::error();
        }
        Ty::math_vector(n, elem)
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// True if `t` (resolved) is a math vector or a complex number.
    pub fn is_compound_number(&mut self, t: Ty) -> bool {
        let t = self.infer.resolve(t);
        t.is_math_vector() || self.sema.complex_of(t).is_some()
    }

    /// `lhs op rhs` where an operand is a math vector or a complex number (§7.4.7). Both
    /// operands are checked; `lhs` is evaluated first.
    pub fn compound_number_binary(&mut self, op: ast::BinOp, lhs: Expr, r: Expr, span: Span) -> Expr {
        use ast::BinOp as B;
        let (lt, rt) = (self.infer.resolve(lhs.ty), self.infer.resolve(r.ty));
        if lt.is_error() || rt.is_error() {
            return error_expr(span);
        }
        let (agg, left_is_agg) = if self.is_compound_number(lt) { (lt, true) } else { (rt, false) };
        let elem = self.sema.compound_number_elem(agg).expect("a compound number operand");
        let other = if left_is_agg { rt } else { lt };
        let both = self.is_compound_number(other);
        let complex = !agg.is_math_vector();
        let what = if complex { "complex numbers" } else { "math vectors" };
        match op {
            B::Is | B::IsNot | B::In | B::NotIn | B::And | B::Or | B::Range | B::RangeIncl => {
                unreachable!("not an arithmetic operator")
            }
            B::Lt | B::Le | B::Gt | B::Ge => {
                self.error(span, format!("{} are not ordered (§7.5)", what));
                return error_expr(span);
            }
            _ => {}
        }
        if let B::Add(ast::Flavor::Opt)
        | B::Sub(ast::Flavor::Opt)
        | B::Mul(ast::Flavor::Opt)
        | B::Shl(ast::Flavor::Opt) = op
        {
            self.error(span, format!("`{}` is not available on {}: an element cannot be `null`", op.as_str(), what));
            return error_expr(span);
        }
        let shift = matches!(op, B::Shl(_) | B::Shr);
        if complex
            && !matches!(
                op,
                B::Add(ast::Flavor::Checked) | B::Sub(ast::Flavor::Checked) | B::Mul(ast::Flavor::Checked) | B::Div
            )
        {
            self.error(
                span,
                format!(
                    "`{}` does not apply to complex numbers; they have `+`, `-`, `*` and `/` (§7.4.7)",
                    op.as_str()
                ),
            );
            return error_expr(span);
        }
        if shift {
            // `v << n`: one scalar amount for every element.
            if !left_is_agg || both {
                self.error(span, "a shift takes a math vector on the left and one scalar amount on the right");
                return error_expr(span);
            }
            if !self.infer.require_unsigned(r.ty, Fallback::Usize) {
                self.error(r.span, format!("a shift amount must have an unsigned integer type, not `{}`", rt));
                return error_expr(span);
            }
        } else if both {
            if self.unify_operands(lt, rt, span, &format!("the operands of `{}`", op.as_str())).is_error() {
                return error_expr(span);
            }
        } else {
            // A scalar operand: `*` on either side, `/` on the right (§7.4.7).
            let scales = matches!(op, B::Mul(_)) || (op == B::Div && left_is_agg);
            if !scales {
                let hint = match op {
                    B::Div => "; divide by a scalar on the right".to_string(),
                    B::Add(_) | B::Sub(_) => {
                        format!("; a scalar is not added to {} (§7.4.7): write the other operand as one", what)
                    }
                    _ => String::new(),
                };
                let shown = |t: Ty| if t.has_infer() { "a literal".to_string() } else { format!("`{}`", t) };
                let (l, r) = (shown(lt), shown(rt));
                self.error(span, format!("`{}` takes two `{}`, not {} and {}{}", op.as_str(), agg, l, r, hint));
                return error_expr(span);
            }
            if self.infer.unify(other, elem).is_err() {
                let other = self.infer.resolve(other);
                let shown = if other.has_infer() { "this literal".to_string() } else { format!("`{}`", other) };
                let msg =
                    format!("`{}` scales `{}` by its element type `{}`, not by {}", op.as_str(), agg, elem, shown);
                self.error(span, msg);
                return error_expr(span);
            }
        }
        if complex {
            let name = match (op, both, left_is_agg) {
                (B::Add(_), _, _) => "op_add",
                (B::Sub(_), _, _) => "op_sub",
                (B::Mul(_), true, _) => "op_mul",
                (B::Mul(_), false, _) => "op_scale",
                (B::Div, true, _) => "op_div",
                _ => "op_div_scalar",
            };
            // `x * z` is `z * x` with `x` evaluated first.
            let swap = !both && !left_is_agg;
            return self.complex_call(name, vec![lhs, r], swap, span);
        }
        let (n, _) = agg.math_vector_parts().unwrap();
        let mut stmts = vec![];
        let l = self.bind_temp(lhs, &mut stmts);
        let r = self.bind_temp(r, &mut stmts);
        let part = |v: &Expr, i: u64, is_agg: bool| -> Expr {
            if !is_agg {
                return v.clone();
            }
            let index = mk(Ty::usize(), tir::ExprKind::Int(i.into()), v.span);
            mk(elem, tir::ExprKind::Index { base: Box::new(v.clone()), index: Box::new(index), checked: false }, v.span)
        };
        let mut elems = vec![];
        for i in 0..n {
            let (a, b) = (part(&l, i, left_is_agg), part(&r, i, !left_is_agg || both));
            let e = if shift {
                self.obligations.push(super::func::Obligation::Numeric {
                    ty: elem,
                    span,
                    op: op.as_str(),
                    int_only: true,
                    signed_only: false,
                });
                let top = match op {
                    B::Shl(f) => tir::BinOp::Shl(f),
                    _ => tir::BinOp::Shr,
                };
                mk(elem, tir::ExprKind::Binary { op: top, lhs: Box::new(a), rhs: Box::new(b) }, span)
            } else {
                self.arith_op(op, a, b, span)
            };
            if e.ty.is_error() {
                return e;
            }
            elems.push(e);
        }
        let arr = mk(agg, tir::ExprKind::Array(elems), span);
        mk(agg, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(arr), span })), span)
    }

    /// A unary operator on a math vector or complex number `v` (§7.4.7).
    pub fn compound_number_unary(&mut self, op: ast::UnOp, v: Expr, span: Span) -> Expr {
        let vt = self.infer.resolve(v.ty);
        if vt.is_error() {
            return error_expr(span);
        }
        let Some((n, elem)) = vt.math_vector_parts() else {
            if op == ast::UnOp::Neg {
                return self.complex_call("op_neg", vec![v], false, span);
            }
            self.error(
                span,
                format!("`{}` does not apply to complex numbers", if op == ast::UnOp::BitNot { "~" } else { "not" }),
            );
            return error_expr(span);
        };
        let (top, int_only, signed_only, s) = match op {
            ast::UnOp::Neg => (tir::UnOp::Neg, false, true, "-"),
            ast::UnOp::BitNot => (tir::UnOp::BitNot, true, false, "~"),
            _ => {
                self.error(span, format!("`not` takes a `bool`, not `{}`", vt));
                return error_expr(span);
            }
        };
        let mut stmts = vec![];
        let x = self.bind_temp(v, &mut stmts);
        let mut elems = vec![];
        for i in 0..n {
            self.obligations.push(super::func::Obligation::Numeric { ty: elem, span, op: s, int_only, signed_only });
            let a = self.math_elem(&x, elem, i);
            elems.push(mk(elem, tir::ExprKind::Unary { op: top, operand: Box::new(a) }, span));
        }
        let arr = mk(vt, tir::ExprKind::Array(elems), span);
        mk(vt, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(arr), span })), span)
    }

    /// Element `i` of the math vector `v` (a local or a constant).
    fn math_elem(&self, v: &Expr, elem: Ty, i: u64) -> Expr {
        let index = mk(Ty::usize(), tir::ExprKind::Int(i.into()), v.span);
        mk(elem, tir::ExprKind::Index { base: Box::new(v.clone()), index: Box::new(index), checked: false }, v.span)
    }

    /// A call of the function `name` of `core/complex` on checked arguments, evaluated in
    /// the order given; with `swap`, the first two are passed in the other order.
    fn complex_call(&mut self, name: &str, args: Vec<Expr>, swap: bool, span: Span) -> Expr {
        let Some(f) = self.sema.core_fn("complex", name) else {
            self.error(span, format!("the complex operation `{}` is missing from the core library", name));
            return error_expr(span);
        };
        let mut stmts = vec![];
        let mut written = vec![];
        for v in args {
            let vt = self.infer.resolve(v.ty);
            let local_name = crate::symbol::Symbol::intern(&format!("<arg{}>", self.locals.len()));
            let local = self.new_local(local_name, vt, false, v.span);
            let vsp = v.span;
            stmts.push(Stmt::Let { local, init: Some(v) });
            self.declare(local_name, LocalBinding::Var(local), vsp);
            let id = ast::Ident { name: local_name, span: vsp };
            let e = ast::Expr { id: ast::NodeId(u32::MAX), kind: ast::ExprKind::Ident(id), span: vsp };
            written.push(ast::Arg { kind: ArgKind::Positional(e), span: vsp });
        }
        if swap {
            written.swap(0, 1);
        }
        let call = self.generic_call(f, &written, span);
        let ty = call.ty;
        mk(ty, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(call), span })), span)
    }

    /// A builtin method of the math vector `recv` (§21.3).
    pub fn math_vector_method(&mut self, recv: Expr, vt: Ty, n: &str, args: &[ast::Arg], span: Span) -> Expr {
        let (len, elem) = vt.math_vector_parts().expect("a math vector receiver");
        let mut stmts = vec![];
        let x = self.bind_temp(recv, &mut stmts);
        let block = |stmts: Vec<Stmt>, tail: Expr| {
            let ty = tail.ty;
            mk(ty, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(tail), span })), span)
        };
        let b = |op, ty, args: Vec<Expr>| mk(ty, tir::ExprKind::Builtin { op, args }, span);
        match n {
            "dot" | "cross" | "min" | "max" => {
                if !self.arity(args, 1, n, span) {
                    return error_expr(span);
                }
                let y = self.value_arg(args, 0, vt, span);
                let y = self.bind_temp(y, &mut stmts);
                if n == "cross" && len != 3 {
                    self.error(span, format!("`.cross` needs three elements, and `{}` has {}", vt, len));
                    return error_expr(span);
                }
                let el = |cx: &Self, v: &Expr, i: u64| cx.math_elem(v, elem, i);
                let tail = match n {
                    "dot" => {
                        let mut acc: Option<Expr> = None;
                        for i in 0..len {
                            let p = self.arith_op(
                                ast::BinOp::Mul(ast::Flavor::Checked),
                                el(self, &x, i),
                                el(self, &y, i),
                                span,
                            );
                            acc = Some(match acc {
                                None => p,
                                Some(a) => self.arith_op(ast::BinOp::Add(ast::Flavor::Checked), a, p, span),
                            });
                        }
                        acc.unwrap()
                    }
                    "cross" => {
                        let mut elems = vec![];
                        for (i, j) in [(1, 2), (2, 0), (0, 1)] {
                            let mul = ast::BinOp::Mul(ast::Flavor::Checked);
                            let p = self.arith_op(mul, el(self, &x, i), el(self, &y, j), span);
                            let q = self.arith_op(mul, el(self, &x, j), el(self, &y, i), span);
                            elems.push(self.arith_op(ast::BinOp::Sub(ast::Flavor::Checked), p, q, span));
                        }
                        mk(vt, tir::ExprKind::Array(elems), span)
                    }
                    _ => {
                        let op = if n == "min" { Builtin::Min } else { Builtin::Max };
                        let elems = (0..len).map(|i| b(op, elem, vec![el(self, &x, i), el(self, &y, i)])).collect();
                        mk(vt, tir::ExprKind::Array(elems), span)
                    }
                };
                block(stmts, tail)
            }
            "abs" => {
                if !self.arity(args, 0, n, span) {
                    return error_expr(span);
                }
                if !(elem.is_signed_integer() || elem.is_float()) {
                    self.error(span, format!("`.abs` needs signed integer or float elements, not `{}`", elem));
                    return error_expr(span);
                }
                let elems = (0..len).map(|i| b(Builtin::Abs, elem, vec![self.math_elem(&x, elem, i)])).collect();
                block(stmts, mk(vt, tir::ExprKind::Array(elems), span))
            }
            "norm" => {
                if !self.arity(args, 0, n, span) {
                    return error_expr(span);
                }
                if !elem.is_float() {
                    self.error(span, format!("`.norm` needs float elements, not `{}`", elem));
                    return error_expr(span);
                }
                let mut acc: Option<Expr> = None;
                for i in 0..len {
                    let e = self.math_elem(&x, elem, i);
                    let p = self.arith_op(ast::BinOp::Mul(ast::Flavor::Checked), e.clone(), e, span);
                    acc = Some(match acc {
                        None => p,
                        Some(a) => self.arith_op(ast::BinOp::Add(ast::Flavor::Checked), a, p, span),
                    });
                }
                block(stmts, b(Builtin::Sqrt, elem, vec![acc.unwrap()]))
            }
            _ => {
                let hint = if super::methods::SLICE_FNS.contains(&n) || matches!(n, "get" | "first" | "last") {
                    format!("; `v[..]` is a slice of its elements, which has `.{}`", n)
                } else {
                    String::new()
                };
                self.error(
                    span,
                    format!(
                        "math vectors have no method `.{}`; they have `dot`, `cross`, `norm`, `min`, `max` and `abs` (§21.3){}",
                        n, hint
                    ),
                );
                error_expr(span)
            }
        }
    }

    /// `T(x)` where `T` or `x`'s type is a math vector (§5.6.3), or `None` if neither is:
    /// to and from the array of its elements, and to a math vector of as many elements
    /// whose type each element converts to losslessly.
    pub fn math_vector_conversion(&mut self, target: Ty, x: Expr, span: Span) -> Option<Expr> {
        let st = self.infer.resolve(x.ty);
        if !target.is_math_vector() && !st.is_math_vector() {
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
        let plain = |t: Ty| matches!(t.kind(), TyKind::Array { kind: crate::types::ArrayKind::Plain, .. });
        let both = st.is_math_vector() && target.is_math_vector();
        if n == m && a == b && (both || plain(st) || plain(target)) {
            return Some(mk(target, tir::ExprKind::Builtin { op: Builtin::Bits, args: vec![x] }, span));
        }
        if both && n == m && super::finalize::lossless(a, b) {
            let mut stmts = vec![];
            let v = self.bind_temp(x, &mut stmts);
            let elems = (0..n)
                .map(|i| {
                    mk(b, tir::ExprKind::Builtin { op: Builtin::Convert, args: vec![self.math_elem(&v, a, i)] }, span)
                })
                .collect();
            let arr = mk(target, tir::ExprKind::Array(elems), span);
            return Some(mk(target, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(arr), span })), span));
        }
        let why =
            if both && n == m { format!("; `{}` does not convert to `{}` losslessly", a, b) } else { String::new() };
        self.error(span, format!("cannot convert `{}` to `{}`{}", st, target, why));
        Some(error_expr(span))
    }
}
