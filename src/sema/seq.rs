//! Sequences: slicing (§7.9), membership `in` (§7.7) and `for` over arrays and
//! slices (§13.2).

use super::func::{FnCx, LocalBinding, Obligation, block_of, error_expr};
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
        let b = self.expr(base, None);
        let b = match self.infer.resolve(b.ty).kind() {
            TyKind::Ptr { pointee, .. } if pointee.is_array() => {
                let p = *pointee;
                mk(p, tir::ExprKind::Deref(Box::new(b)), span)
            }
            _ => b,
        };
        let bt = self.infer.resolve(b.ty);
        let (elem, is_const) = match bt.kind() {
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => match self.place_mutability(&b) {
                Some(m) => (*elem, !m),
                None => {
                    self.error(base.span, "only an array variable (a place) can be sliced, not a temporary array");
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
        let bound = |e: Option<&ast::Expr>, cx: &mut Self| {
            e.map(|e| {
                let v = cx.expr(e, None);
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
            let tmp = self.temp(ty, span);
            let x = || mk(ty, tir::ExprKind::Local(tmp), span);
            let cmp = |op, l: Expr, r: Expr| {
                mk(Ty::bool(), tir::ExprKind::Binary { op, lhs: Box::new(l), rhs: Box::new(r) }, span)
            };
            let upper = if *op == ast::BinOp::RangeIncl { tir::BinOp::Le } else { tir::BinOp::Lt };
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
            let block = tir::Block { stmts: vec![Stmt::Let { local: tmp, init: Some(elem) }], tail: Some(test), span };
            return mk(Ty::bool(), tir::ExprKind::Block(Box::new(block)), span);
        }
        // An inline tuple: every element has the element's type.
        let c = if let ExprKind::Tuple(elems) = &container.kind {
            let mut vals = vec![];
            for el in elems {
                let v = self.expr(&el.value, Some(elem.ty));
                self.unify_operands(elem.ty, v.ty, v.span, "the element and the tuple members of `in`");
                vals.push(v);
            }
            let ty = Ty::tuple(vals.iter().map(|v| v.ty).collect());
            mk(ty, tir::ExprKind::Aggregate(vals), container.span)
        } else {
            self.expr(container, None)
        };
        let ct = self.infer.resolve(c.ty);
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
        mk(Ty::bool(), tir::ExprKind::In { elem: Box::new(elem), container: Box::new(c), negated }, span)
    }

    /// `for p in xs` where `xs` is an array place, a pointer to an array, or a slice.
    pub fn for_each_loop(&mut self, f: &ast::ForExpr) -> Option<Expr> {
        let span = f.span;
        let src = self.expr(&f.iter, None);
        let st = self.infer.resolve(src.ty);
        let (slice, elem) = match st.kind() {
            TyKind::Slice { elem, .. } => (src, *elem),
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => {
                let elem = *elem;
                if self.place_mutability(&src).is_none() {
                    self.error(
                        f.iter.span,
                        "`for` iterates over an array variable (a place) or a slice, not a temporary array",
                    );
                    return Some(error_expr(span));
                }
                let m = self.place_mutability(&src) == Some(true);
                let ty = Ty::slice(!m, elem);
                (mk(ty, tir::ExprKind::Slice { base: Box::new(src), lo: None, hi: None, inclusive: false }, span), elem)
            }
            TyKind::Ptr { pointee, is_const } if pointee.is_array() => {
                let (TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. }) = pointee.kind() else {
                    unreachable!()
                };
                let arr = mk(*pointee, tir::ExprKind::Deref(Box::new(src)), span);
                let ty = Ty::slice(*is_const, *elem);
                (
                    mk(ty, tir::ExprKind::Slice { base: Box::new(arr), lo: None, hi: None, inclusive: false }, span),
                    *elem,
                )
            }
            TyKind::Error => return Some(error_expr(span)),
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
        let id = self.new_label(f.label.map(|l| l.name), true);
        let body = self.block(&f.body, false);
        self.labels.pop();
        self.pop_scope();
        let arm = tir::Arm { pat, guard: None, body };
        let m = mk(Ty::unit(), tir::ExprKind::Match { scrutinee: Box::new(item), arms: vec![arm] }, span);
        let body = tir::Block { stmts: vec![Stmt::Expr(m)], tail: None, span };
        mk(Ty::unit(), tir::ExprKind::Loop { label: id, cond, body: Box::new(body), step: Some(Box::new(step)) }, span)
    }
}
