//! Construction of structs, tuples, arrays and union variants (§5.10.1, §5.11, §7.11).

use super::func::{FnCx, error_expr};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, Expr, Stmt};
use crate::types::{Ty, TyKind};

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

impl<'a, 's> FnCx<'a, 's> {
    /// `S(f: v, …, ..spread)` for a struct type `S`.
    pub fn construct_struct(&mut self, ty: Ty, args: &[ast::Arg], span: Span) -> Expr {
        if let Some(f) = self.hidden_field(ty) {
            self.error(
                span,
                format!("`{}` cannot be constructed outside its module: its field `{}` is private (§3.3)", ty, f),
            );
            return error_expr(span);
        }
        let fields: Vec<(Symbol, Ty)> =
            self.sema.struct_fields(ty).unwrap_or(&[]).iter().map(|f| (f.name, f.ty)).collect();
        let n = fields.len();
        // Values in the order written; each is bound to a temporary so evaluation
        // order is the written order regardless of field order (§7.1).
        let mut stmts: Vec<Stmt> = vec![];
        let mut slots: Vec<Option<Expr>> = vec![None; n];
        let mut explicit = vec![false; n];
        // Field indices of named arguments, in the order written.
        let mut written: Vec<usize> = vec![];
        let mut bad_args = false;
        for arg in args {
            match &arg.kind {
                ArgKind::Positional(e) => {
                    if !bad_args {
                        self.error(
                            e.span,
                            format!("`{}` is constructed with named arguments only: `{}(field: value)`", ty, ty),
                        );
                    }
                    bad_args = true;
                }
                ArgKind::Named(name, e) => {
                    let Some(i) = fields.iter().position(|(f, _)| *f == name.name) else {
                        self.error(name.span, format!("`{}` has no field `{}`", ty, name.name));
                        let _ = self.expr(e, None);
                        continue;
                    };
                    if explicit[i] {
                        self.error(name.span, format!("field `{}` is given twice", name.name));
                    }
                    explicit[i] = true;
                    let v = self.field_value(e, fields[i].1);
                    written.push(i);
                    slots[i] = Some(v);
                }
                ArgKind::Spread(e) => {
                    let v = self.expr(e, None);
                    let vt = self.infer.resolve(v.ty);
                    let src_fields: Vec<(Symbol, Ty)> = match vt.kind() {
                        TyKind::Tuple { elems, names: Some(names) } => {
                            names.iter().copied().zip(elems.iter().copied()).collect()
                        }
                        _ => match self.sema.struct_fields(vt) {
                            Some(fs) => fs.iter().map(|f| (f.name, f.ty)).collect(),
                            None => {
                                if !vt.is_error() {
                                    self.error(
                                        e.span,
                                        format!("cannot spread a value of type `{}` into named arguments", vt),
                                    );
                                }
                                continue;
                            }
                        },
                    };
                    let tmp_ty = vt;
                    // Values written before the spread are evaluated before it.
                    for &w in &written {
                        let v = slots[w].take().unwrap();
                        slots[w] = Some(self.bind_temp(v, &mut stmts));
                    }
                    written.clear();
                    let base = self.bind_temp(v, &mut stmts);
                    for (k, (fname, fty)) in src_fields.iter().enumerate() {
                        let Some(i) = fields.iter().position(|(f, _)| f == fname) else {
                            self.error(e.span, format!("`{}` has no field `{}` (supplied by this spread)", ty, fname));
                            continue;
                        };
                        if explicit[i] {
                            // A later explicit named argument overrides; an earlier one is a conflict.
                            self.error(e.span, format!("field `{}` is supplied twice", fname));
                            continue;
                        }
                        if slots[i].is_some() {
                            self.error(e.span, format!("field `{}` is supplied by two spreads", fname));
                        }
                        let proj =
                            mk(*fty, tir::ExprKind::Field { base: Box::new(base.clone()), index: k as u32 }, e.span);
                        let _ = tmp_ty;
                        slots[i] = Some(self.coerce(proj, fields[i].1));
                    }
                }
            }
        }
        // Arguments written out of field order are evaluated into temporaries first,
        // so evaluation follows the written order (§7.1).
        if written.windows(2).any(|w| w[0] > w[1]) {
            for &w in &written {
                let v = slots[w].take().unwrap();
                slots[w] = Some(self.bind_temp(v, &mut stmts));
            }
        }
        let mut values = vec![];
        for (i, slot) in slots.into_iter().enumerate() {
            match slot {
                Some(v) => values.push(v),
                None => {
                    // Padding fields `_` of a `#bits` struct are zero (§5.10.5).
                    let default = if fields[i].0.as_str() == "_" && self.sema.bits_struct_ty(ty).is_some() {
                        let ft = fields[i].1;
                        Some(if ft.is_bool() {
                            mk(ft, tir::ExprKind::Bool(false), span)
                        } else {
                            mk(ft, tir::ExprKind::Int(0.into()), span)
                        })
                    } else {
                        self.adt_default(ty, i)
                    };
                    match default {
                        Some(d) => values.push(d),
                        None => {
                            if !bad_args {
                                self.error(span, format!("missing field `{}` (it has no default)", fields[i].0));
                            }
                            values.push(error_expr(span));
                        }
                    }
                }
            }
        }
        let agg = mk(ty, tir::ExprKind::Aggregate(values), span);
        if stmts.is_empty() {
            return agg;
        }
        mk(ty, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(agg), span })), span)
    }

    fn adt_default(&self, ty: Ty, i: usize) -> Option<Expr> {
        let TyKind::Adt(id, _) = ty.kind() else { return None };
        self.sema.adts.get(id)?.defaults.get(i)?.clone()
    }

    /// A constructor field or argument value: `---` for plain data, else coerced.
    fn field_value(&mut self, e: &ast::Expr, ty: Ty) -> Expr {
        if let ast::ExprKind::Uninit = e.kind {
            if !self.is_plain_data(ty) {
                self.error(e.span, format!("`---` requires a plain-data type, but `{}` is not (§4.4)", ty));
            }
            return mk(ty, tir::ExprKind::Uninit, e.span);
        }
        let v = self.expr(e, Some(ty));
        self.coerce(v, ty)
    }

    /// Binds `v` to a temporary (appending the `let` to `stmts`) unless it is a
    /// constant, and returns an expression reading it.
    fn bind_temp(&mut self, v: Expr, stmts: &mut Vec<Stmt>) -> Expr {
        if is_constant(&v) {
            return v;
        }
        let span = v.span;
        let ty = v.ty;
        let t = self.temp(ty, span);
        stmts.push(Stmt::Let { local: t, init: Some(v) });
        mk(ty, tir::ExprKind::Local(t), span)
    }

    /// `.V`, `.V(args)`, `U.V`, `U.V(args)` for a union type `u`.
    pub fn construct_variant(&mut self, u: Ty, name: ast::Ident, args: Option<&[ast::Arg]>, span: Span) -> Expr {
        let Some((variants, _)) = self.sema.union_variants(u) else {
            self.error(span, format!("`.{}` needs a union type, but the expected type is `{}`", name.name, u));
            return error_expr(span);
        };
        let Some(idx) = variants.iter().position(|v| v.name == name.name) else {
            self.error(name.span, format!("`{}` has no variant `{}`", u, name.name));
            return error_expr(span);
        };
        let payload_ty = variants[idx].payload;
        let payload = match (payload_ty, args) {
            (None, None) => None,
            (None, Some(_)) => {
                self.error(span, format!("variant `{}` has no payload", name.name));
                return error_expr(span);
            }
            (Some(_), None) => {
                self.error(span, format!("variant `{}` needs a payload: `.{}(…)`", name.name, name.name));
                return error_expr(span);
            }
            (Some(pt), Some(args)) => Some(Box::new(self.payload_value(pt, args, span))),
        };
        mk(u, tir::ExprKind::Variant { variant: idx as u32, payload }, span)
    }

    /// A variant payload: a tuple payload's elements are written directly (§5.11).
    fn payload_value(&mut self, pt: Ty, args: &[ast::Arg], span: Span) -> Expr {
        if let TyKind::Tuple { elems, names } = pt.kind()
            && (args.len() != 1 || elems.len() != 1 || matches!(args[0].kind, ArgKind::Named(..)))
        {
            return self.construct_tuple(pt, elems, names.as_deref(), args, span);
        }
        match args {
            [ast::Arg { kind: ArgKind::Positional(e), .. }] => self.field_value(e, pt),
            _ => {
                self.error(span, format!("expected one payload value of type `{}`", pt));
                error_expr(span)
            }
        }
    }

    /// Builds a tuple of type `ty` from call-style arguments (positional or named).
    pub fn construct_tuple(
        &mut self,
        ty: Ty,
        elems: &[Ty],
        names: Option<&[Symbol]>,
        args: &[ast::Arg],
        span: Span,
    ) -> Expr {
        let mut slots: Vec<Option<Expr>> = vec![None; elems.len()];
        let mut next = 0;
        for arg in args {
            match &arg.kind {
                ArgKind::Positional(e) => {
                    if next >= elems.len() {
                        self.error(e.span, format!("too many values for `{}`", ty));
                        continue;
                    }
                    slots[next] = Some(self.field_value(e, elems[next]));
                    next += 1;
                }
                ArgKind::Named(n, e) => {
                    let Some(i) = names.and_then(|ns| ns.iter().position(|x| *x == n.name)) else {
                        self.error(n.span, format!("`{}` has no element named `{}`", ty, n.name));
                        continue;
                    };
                    if slots[i].is_some() {
                        self.error(n.span, format!("element `{}` is given twice", n.name));
                    }
                    slots[i] = Some(self.field_value(e, elems[i]));
                }
                ArgKind::Spread(e) => self.error(e.span, "spreads are not supported here yet"),
            }
        }
        let mut values = vec![];
        for (i, s) in slots.into_iter().enumerate() {
            match s {
                Some(v) => values.push(v),
                None => {
                    self.error(span, format!("missing element {} of `{}`", i, ty));
                    values.push(error_expr(span));
                }
            }
        }
        mk(ty, tir::ExprKind::Aggregate(values), span)
    }

    /// `[N]T(e1, …)`, `[_]T(…)` and `[N]T(fill: x)` (§7.11).
    pub fn construct_array(&mut self, len: Option<u64>, elem: Ty, args: &[ast::Arg], span: Span) -> Expr {
        if let [ast::Arg { kind: ArgKind::Named(n, e), .. }] = args
            && n.name.as_str() == "fill"
        {
            let Some(len) = len else {
                self.error(span, "`[_]T(fill: x)` cannot infer the length; write it");
                return error_expr(span);
            };
            let v = self.field_value(e, elem);
            return mk(Ty::array(len, elem), tir::ExprKind::ArrayFill(Box::new(v)), span);
        }
        let mut values = vec![];
        for arg in args {
            match &arg.kind {
                ArgKind::Positional(e) => values.push(self.field_value(e, elem)),
                _ => self.error(arg.span, "array elements are written positionally"),
            }
        }
        if let Some(n) = len
            && n != values.len() as u64
        {
            self.error(span, format!("`[{}]{}` needs exactly {} elements, but {} are given", n, elem, n, values.len()));
        }
        mk(Ty::array(values.len() as u64, elem), tir::ExprKind::Array(values), span)
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// Construction of `[N]T` where `N` is a compile-time parameter: only
    /// `fill:` can give an element count that is not known yet.
    pub fn construct_param_array(&mut self, t: Ty, elem: Ty, args: &[ast::Arg], span: Span) -> Expr {
        if let [ast::Arg { kind: ArgKind::Named(n, e), .. }] = args
            && n.name.as_str() == "fill"
        {
            let v = self.field_value(e, elem);
            return mk(t, tir::ExprKind::ArrayFill(Box::new(v)), span);
        }
        self.error(span, format!("`{}` has a length that is a compile-time parameter; construct it with `fill:`", t));
        error_expr(span)
    }
}

fn is_constant(e: &Expr) -> bool {
    matches!(
        e.kind,
        tir::ExprKind::Int(_)
            | tir::ExprKind::Float(_)
            | tir::ExprKind::UntypedFloat(_)
            | tir::ExprKind::Bool(_)
            | tir::ExprKind::Str(_)
            | tir::ExprKind::Null
            | tir::ExprKind::Uninit
            | tir::ExprKind::Const(_)
    )
}
