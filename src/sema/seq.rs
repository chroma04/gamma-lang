//! Sequences: slicing (§7.9), membership `in` (§7.7) and `for` over arrays and
//! slices (§13.2).

use super::func::{FnCx, LabelValue, LocalBinding, Obligation, block_of, error_expr};
use super::infer::Fallback;
use super::pattern::PatCtx;
use crate::source::Span;
use crate::syntax::ast::{self, ExprKind};
use crate::tir::{self, Expr, Stmt};
use crate::types::{Ty, TyKind};

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

impl<'a, 's> FnCx<'a, 's> {
    /// `a[lo..hi]` and its open-ended forms.
    pub fn slice_expr(
        &mut self,
        base: &ast::Expr,
        lo: Option<&ast::Expr>,
        hi: Option<&ast::Expr>,
        inclusive: bool,
        span: Span,
    ) -> Expr {
        let temp_ok = std::mem::take(&mut self.temp_ref_arg);
        let mut b = self.expr(base, None);
        // A temporary array sliced as an argument for a `[]const T` parameter lives for
        // the call, like `&e` for a `^const T` one (§7.2).
        if temp_ok && self.infer.resolve(b.ty).is_array() && self.place_mutability(&b).is_none() {
            let t = b.ty;
            let p = mk(Ty::ptr(true, t), tir::ExprKind::AddrOfTemp(Box::new(b)), base.span);
            b = mk(t, tir::ExprKind::Deref(Box::new(p)), base.span);
        }
        let bound = |e: Option<&ast::Expr>, cx: &mut Self| e.map(|e| cx.expr(e, None));
        let lo = bound(lo, self);
        let hi = bound(hi, self);
        self.slice_of(b, base.span, lo, hi, inclusive, span)
    }

    /// An array argument `v` for a `[]const T` parameter is passed as a slice of the whole
    /// array; a temporary array lives for the call, like `&e` for a `^const T` one (§7.2).
    pub fn array_arg_as_slice(&mut self, v: Expr, param: Ty) -> Expr {
        let param = self.infer.resolve(param);
        let vt = self.infer.resolve(v.ty);
        if !matches!(param.kind(), TyKind::Slice { is_const: true, .. }) || !vt.is_positional_array() {
            return v;
        }
        let span = v.span;
        let b = if self.place_mutability(&v).is_none() {
            let t = v.ty;
            let p = mk(Ty::ptr(true, t), tir::ExprKind::AddrOfTemp(Box::new(v)), span);
            mk(t, tir::ExprKind::Deref(Box::new(p)), span)
        } else {
            v
        };
        self.slice_of(b, span, None, None, false, span)
    }

    /// Slices the evaluated `b` (written at `base_span`) with the evaluated bounds.
    pub fn slice_of(
        &mut self,
        b: Expr,
        base_span: Span,
        lo: Option<Expr>,
        hi: Option<Expr>,
        inclusive: bool,
        span: Span,
    ) -> Expr {
        let b = match self.infer.resolve(b.ty).kind() {
            TyKind::Ptr { pointee, .. } if pointee.is_array() => {
                let p = *pointee;
                mk(p, tir::ExprKind::Deref(Box::new(b)), span)
            }
            _ => b,
        };
        let b = self.soa_deref(b);
        if let Some(info) = self.soa_of(&b) {
            return self.soa_slice(b, info, lo, hi, inclusive, span);
        }
        let bt = self.infer.resolve(b.ty);
        if bt.enum_index().is_some() && (lo.is_some() || hi.is_some()) {
            self.error(span, format!("`{}` is indexed by its enum, so it is sliced only whole: `a[..]`", bt));
            return error_expr(span);
        }
        let (elem, is_const) = match bt.kind() {
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => match self.place_mutability(&b) {
                Some(m) => (*elem, !m),
                None => {
                    self.error(base_span, "only an array variable (a place) can be sliced, not a temporary array");
                    return error_expr(span);
                }
            },
            TyKind::Slice { elem, is_const } => (*elem, *is_const),
            TyKind::ManyPtr { elem, is_const } => {
                if lo.is_none() || hi.is_none() {
                    self.error(span, "slicing a many-pointer needs both bounds");
                }
                (*elem, *is_const)
            }
            TyKind::Error => return error_expr(span),
            _ => {
                self.error(span, format!("cannot slice a value of type `{}`", bt));
                return error_expr(span);
            }
        };
        let bound = |e: Option<Expr>, cx: &mut Self| {
            e.map(|v| {
                if !cx.infer.require_unsigned(v.ty, Fallback::Usize) && !v.ty.is_error() {
                    let t = cx.infer.resolve(v.ty);
                    cx.error(v.span, format!("a slice bound must have an unsigned integer type, not `{}`", t));
                }
                Box::new(v)
            })
        };
        let lo = bound(lo, self);
        let hi = bound(hi, self);
        mk(Ty::slice(is_const, elem), tir::ExprKind::Slice { base: Box::new(b), lo, hi, inclusive }, span)
    }

    /// `x in c` / `x not in c` (§7.7).
    pub fn in_expr(&mut self, elem: Expr, container: &ast::Expr, negated: bool, span: Span) -> Expr {
        // Ranges: `lo <= x and x < hi` with `x` evaluated once.
        if let ExprKind::Binary { op: op @ (ast::BinOp::Range | ast::BinOp::RangeIncl), lhs, rhs } = &container.kind {
            let lo = self.expr(lhs, Some(elem.ty));
            let hi = self.expr(rhs, Some(elem.ty));
            let ty = self.unify_operands(elem.ty, lo.ty, span, "the operands of `in`");
            self.unify_operands(ty, hi.ty, span, "the operands of `in`");
            self.obligations.push(Obligation::Ord { ty, span });
            let inclusive = *op == ast::BinOp::RangeIncl;
            return self.range_test(elem, vec![], lo, hi, ty, inclusive, negated, span);
        }
        // An inline tuple: every element has the element's type.
        let c = if let ExprKind::Tuple(elems) = &container.kind {
            let mut vals = vec![];
            // As with `is`, a member converts to an optional or result element: `c in ('a', 'b')`
            // with `c: ?u8` (§7.7).
            let et = self.infer.resolve(elem.ty);
            let wraps = matches!(et.kind(), TyKind::Optional(_) | TyKind::Result { .. }) && !et.has_infer();
            for el in elems {
                let v = self.expr(&el.value, Some(elem.ty));
                let vt = self.infer.resolve(v.ty);
                let v = if wraps && vt != et && (vt.is_infer() || self.coercion_steps(vt, et).is_some()) {
                    self.coerce(v, et)
                } else {
                    self.unify_operands(elem.ty, v.ty, v.span, "the element and the tuple members of `in`");
                    v
                };
                vals.push(v);
            }
            let ty = Ty::tuple(vals.iter().map(|v| v.ty).collect());
            mk(ty, tir::ExprKind::Aggregate(vals), container.span)
        } else {
            self.expr(container, None)
        };
        let ct = self.infer.resolve(c.ty);
        // A range value: `r.lo <= x and x < r.hi`.
        if let Some((inclusive, item)) = self.sema.range_of(ct) {
            self.unify_operands(elem.ty, item, span, "the element and the range of `in`");
            let r = self.temp(ct, span);
            let field = |i| {
                let base = Box::new(mk(ct, tir::ExprKind::Local(r), span));
                mk(item, tir::ExprKind::Field { base, index: i }, span)
            };
            let (lo, hi) = (field(0), field(1));
            return self.range_test(elem, vec![(r, c)], lo, hi, item, inclusive, negated, span);
        }
        let item = match ct.kind() {
            TyKind::Array { elem: e, .. } | TyKind::ParamArray { elem: e, .. } | TyKind::Slice { elem: e, .. } => {
                Some(*e)
            }
            TyKind::Tuple { elems, .. } => elems.first().copied().or(Some(elem.ty)),
            TyKind::Error => return error_expr(span),
            _ => None,
        };
        let Some(item) = item else {
            self.error(container.span, format!("`in` needs a range, array, slice or tuple, not `{}`", ct));
            return error_expr(span);
        };
        self.unify_operands(elem.ty, item, span, "the element and the members of `in`");
        // The members are compared with `is` (§7.7).
        let et = self.infer.resolve(item);
        if !et.has_infer() && !et.is_error() && !self.eq_allowed(et) {
            self.error(span, format!("`in` compares with `is`, which is not defined for `{}` (§7.6)", et));
        }
        mk(Ty::bool(), tir::ExprKind::In { elem: Box::new(elem), container: Box::new(c), negated }, span)
    }

    /// `{ x := elem; lets…; lo <= x and x <(=) hi }`, negated if `negated`.
    #[allow(clippy::too_many_arguments)]
    fn range_test(
        &mut self,
        elem: Expr,
        lets: Vec<(tir::LocalId, Expr)>,
        lo: Expr,
        hi: Expr,
        ty: Ty,
        inclusive: bool,
        negated: bool,
        span: Span,
    ) -> Expr {
        let tmp = self.temp(ty, span);
        let x = || mk(ty, tir::ExprKind::Local(tmp), span);
        let cmp = |op, l: Expr, r: Expr| {
            mk(Ty::bool(), tir::ExprKind::Binary { op, lhs: Box::new(l), rhs: Box::new(r) }, span)
        };
        let upper = if inclusive { tir::BinOp::Le } else { tir::BinOp::Lt };
        let test = mk(
            Ty::bool(),
            tir::ExprKind::Logic {
                is_and: true,
                lhs: Box::new(cmp(tir::BinOp::Le, lo, x())),
                rhs: Box::new(cmp(upper, x(), hi)),
            },
            span,
        );
        let test = if negated {
            mk(Ty::bool(), tir::ExprKind::Unary { op: tir::UnOp::Not, operand: Box::new(test) }, span)
        } else {
            test
        };
        let mut stmts = vec![Stmt::Let { local: tmp, init: Some(elem) }];
        stmts.extend(lets.into_iter().map(|(local, init)| Stmt::Let { local, init: Some(init) }));
        let block = tir::Block { stmts, tail: Some(test), span };
        mk(Ty::bool(), tir::ExprKind::Block(Box::new(block)), span)
    }

    /// `for p in xs` where `xs` is an array place, a pointer to an array, or a slice.
    pub fn for_each_loop(&mut self, f: &ast::ForExpr) -> Option<Expr> {
        let src = self.expr(&f.iter, None);
        self.for_each_value(f, src)
    }

    /// `for p in src` where `src` (already checked) is an array, a pointer to one, a
    /// slice or a range; `None` if it is none of these.
    pub fn for_each_value(&mut self, f: &ast::ForExpr, src: Expr) -> Option<Expr> {
        let span = f.span;
        let src = self.soa_deref(src);
        if let Some(info) = self.soa_of(&src) {
            return Some(self.soa_for(f, src, info));
        }
        let st = self.infer.resolve(src.ty);
        let (slice, elem) = match st.kind() {
            TyKind::Slice { elem, .. } => (src, *elem),
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => {
                let elem = *elem;
                // A temporary array is iterated from a hidden local that lives as long as the loop,
                // and so is an array in a `#packed` struct, whose elements may be misaligned.
                let place = self.place_mutability(&src).filter(|_| !super::escape::in_packed(&self.sema.prog, &src));
                let (src, m, pre) = match place {
                    Some(m) => (src, m, None),
                    None => {
                        let t = self.temp(st, span);
                        (mk(st, tir::ExprKind::Local(t), span), false, Some(Stmt::Let { local: t, init: Some(src) }))
                    }
                };
                let ty = Ty::slice(!m, elem);
                let slice =
                    mk(ty, tir::ExprKind::Slice { base: Box::new(src), lo: None, hi: None, inclusive: false }, span);
                if let Some(pre) = pre {
                    let block = tir::Block { stmts: vec![pre], tail: Some(slice), span };
                    (mk(ty, tir::ExprKind::Block(Box::new(block)), span), elem)
                } else {
                    (slice, elem)
                }
            }
            TyKind::Ptr { pointee, is_const } if pointee.is_array() => {
                let (arr, elem) = deref_array_ptr(src, *pointee, span);
                let ty = Ty::slice(*is_const, elem);
                (mk(ty, tir::ExprKind::Slice { base: Box::new(arr), lo: None, hi: None, inclusive: false }, span), elem)
            }
            TyKind::Error => return Some(error_expr(span)),
            // A tuple whose elements have one type iterates like the array of them:
            // `{ t := src; for x in [t.0, t.1, …] { body } }`.
            TyKind::Tuple { elems, .. } if !elems.is_empty() => {
                let elem = elems[0];
                for &other in &elems[1..] {
                    if self.infer.unify(elem, other).is_err() {
                        let (a, b) = (self.infer.resolve(elem), self.infer.resolve(other));
                        self.error(
                            f.iter.span,
                            format!(
                                "the elements of a tuple iterated by `for` must have one type, but found `{}` and `{}`",
                                a, b
                            ),
                        );
                        return Some(error_expr(span));
                    }
                }
                let t = self.temp(st, span);
                let items = (0..elems.len() as u32)
                    .map(|i| {
                        let base = Box::new(mk(st, tir::ExprKind::Local(t), span));
                        mk(elem, tir::ExprKind::Field { base, index: i }, span)
                    })
                    .collect();
                let aty = Ty::array(elems.len() as u64, elem);
                let arr = mk(aty, tir::ExprKind::Array(items), span);
                let lp = self.for_each_value(f, arr)?;
                let stmts = vec![Stmt::Let { local: t, init: Some(src) }, Stmt::Expr(lp)];
                return Some(block_of(stmts, span));
            }
            TyKind::Adt(..) if self.sema.range_of(st).is_some() => {
                // A range value: { r := src; for i in r.lo..r.hi { body } }
                let (inclusive, elem) = self.sema.range_of(st).unwrap();
                if !elem.is_integer() {
                    self.error(f.iter.span, format!("`for` iterates over ranges of integers, not of `{}`", elem));
                    return Some(error_expr(span));
                }
                let r = self.temp(st, span);
                let field = |i| {
                    let base = Box::new(mk(st, tir::ExprKind::Local(r), span));
                    mk(elem, tir::ExprKind::Field { base, index: i }, span)
                };
                let lp = self.range_loop(f, field(0), field(1), elem, inclusive);
                let stmts = vec![tir::Stmt::Let { local: r, init: Some(src) }, tir::Stmt::Expr(lp)];
                return Some(super::func::block_of(stmts, span));
            }
            _ => return None,
        };
        // { s := xs[..]; i := 0; loop i < s.len { p :: s[i]; body } step i += 1 }
        let sty = slice.ty;
        let s = self.temp(sty, span);
        let i = self.temp(Ty::usize(), span);
        let local = |id, ty| mk(ty, tir::ExprKind::Local(id), span);
        let len = mk(Ty::usize(), tir::ExprKind::SliceLen(Box::new(local(s, sty))), span);
        let cond = mk(
            Ty::bool(),
            tir::ExprKind::Binary { op: tir::BinOp::Lt, lhs: Box::new(local(i, Ty::usize())), rhs: Box::new(len) },
            span,
        );
        let item = mk(
            elem,
            tir::ExprKind::Index {
                base: Box::new(local(s, sty)),
                index: Box::new(local(i, Ty::usize())),
                checked: false,
            },
            span,
        );
        let one = mk(Ty::usize(), tir::ExprKind::Int(1.into()), span);
        let step = block_of(
            vec![Stmt::Assign {
                place: local(i, Ty::usize()),
                value: mk(
                    Ty::usize(),
                    tir::ExprKind::Binary {
                        op: tir::BinOp::Add(ast::Flavor::Wrap),
                        lhs: Box::new(local(i, Ty::usize())),
                        rhs: Box::new(one),
                    },
                    span,
                ),
            }],
            span,
        );
        let lp = self.loop_with_pattern(f, elem, item, vec![tir::CondPart::Bool(cond)], step);
        let zero = mk(Ty::usize(), tir::ExprKind::Int(0.into()), span);
        Some(block_of(
            vec![Stmt::Let { local: s, init: Some(slice) }, Stmt::Let { local: i, init: Some(zero) }, Stmt::Expr(lp)],
            span,
        ))
    }

    /// The loop of a lowered `for`: binds the head patterns to `item` for each iteration.
    pub fn loop_with_pattern(
        &mut self,
        f: &ast::ForExpr,
        elem: Ty,
        item: Expr,
        cond: Vec<tir::CondPart>,
        step: Expr,
    ) -> Expr {
        let span = f.span;
        self.push_scope();
        if let Some(l) = f.label {
            self.declare(l.name, LocalBinding::Label, l.span);
        }
        // `for a, b in …` is the tuple pattern `(a, b)` (§8.4).
        let pat_ast = if f.pats.len() == 1 {
            f.pats[0].clone()
        } else {
            let args = f.pats.iter().map(|p| ast::PatArg { name: None, pat: p.clone(), span: p.span }).collect();
            let sp = f.pats[0].span.to(f.pats.last().unwrap().span);
            ast::Pattern { id: f.pats[0].id, kind: ast::PatternKind::Tuple(args), span: sp }
        };
        let mut binds = vec![];
        let pat = self.pattern(&pat_ast, elem, PatCtx::Irrefutable, &mut binds);
        if !elem.is_error() && !self.checker().irrefutable(&pat) {
            self.error(pat_ast.span, "a `for` pattern must match every value (it must be irrefutable)");
        }
        self.declare_binds(binds);
        let id = self.new_label(f.label.map(|l| l.name), true, LabelValue::Loop(Ty::unit()));
        let body = self.block(&f.body, false);
        self.labels.pop();
        self.label_breaks.remove(&(self.outer.len(), id));
        self.pop_scope();
        let arm = tir::Arm { pat, guard: None, body };
        let m = mk(Ty::unit(), tir::ExprKind::Match { scrutinee: Box::new(item), arms: vec![arm] }, span);
        let body = tir::Block { stmts: vec![Stmt::Expr(m)], tail: None, span };
        mk(Ty::unit(), tir::ExprKind::Loop { label: id, cond, body: Box::new(body), step: Some(Box::new(step)) }, span)
    }
}

/// `*p` for a pointer `p` to the array type `pointee`, with the array's element type.
pub(super) fn deref_array_ptr(p: Expr, pointee: Ty, span: Span) -> (Expr, Ty) {
    let (TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. }) = pointee.kind() else {
        unreachable!("a pointer to an array")
    };
    (mk(pointee, tir::ExprKind::Deref(Box::new(p)), span), *elem)
}
