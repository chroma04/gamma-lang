//! Structure-of-arrays types `#soa [N]S` and `#soa []S` (§5.6.4).
//!
//! Each `#soa` type is a struct made by the compiler: for an array, one field `[N]F` for
//! each field `f: F` of `S`; for a slice, one many-pointer `[^]F` for each field and then
//! `len: usize`. The operations of §5.6.4 are lowered here to field accesses, indexing and
//! slicing of those columns, so the back ends see only structs, arrays and slices.

use super::func::{FnCx, error_expr};
use super::infer::Fallback;
use super::{AdtInfo, ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, AdtDef, AdtKind, Builtin, Expr, FieldDef, Stmt};
use crate::types::{AdtId, Ty, TyKind};

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

fn block(stmts: Vec<Stmt>, tail: Expr, span: Span) -> Expr {
    if stmts.is_empty() {
        return tail;
    }
    let ty = tail.ty;
    mk(ty, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(tail), span })), span)
}

fn usize_lit(v: u64, span: Span) -> Expr {
    mk(Ty::usize(), tir::ExprKind::Int(v.into()), span)
}

/// What a `#soa` type stores: elements of the struct `elem`, `len` of them for an array
/// (`None` for a slice), read-only for a `#soa []const S`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SoaInfo {
    pub elem: Ty,
    pub len: Option<u64>,
    pub is_const: bool,
}

/// True for an expression that can be evaluated twice: it reads a variable or constant.
fn repeatable(e: &Expr) -> bool {
    matches!(
        e.kind,
        tir::ExprKind::Local(_) | tir::ExprKind::Upvar { .. } | tir::ExprKind::Const(_) | tir::ExprKind::Global(_)
    )
}

impl<'s> Sema<'s> {
    /// What the `#soa` type `t` stores, if it is one.
    pub fn soa_info(&self, t: Ty) -> Option<SoaInfo> {
        match t.kind() {
            TyKind::Adt(id, _) => self.soa.get(id).copied(),
            _ => None,
        }
    }

    /// The `#soa` type of the struct `elem`: an array of `len` elements, or a slice
    /// (`len` is `None`). Reports an error and gives the error type if `elem` cannot be
    /// stored by field.
    pub fn soa_type(&mut self, elem: Ty, len: Option<u64>, is_const: bool, span: Span) -> Ty {
        if elem.is_error() {
            return Ty::error();
        }
        if super::generics::has_param(elem) {
            self.error(span, format!("`#soa` needs a concrete struct type, not `{}`", elem));
            return Ty::error();
        }
        let id = match elem.kind() {
            TyKind::Adt(id, _) => *id,
            _ => {
                self.error(span, format!("`#soa` stores the fields of a struct, and `{}` is not a struct", elem));
                return Ty::error();
            }
        };
        let fields = match self.prog.adts.get(&id).map(|d| &d.kind) {
            Some(AdtKind::Struct { bits: None, fields, .. }) => fields.clone(),
            Some(AdtKind::Struct { .. }) => {
                self.error(
                    span,
                    format!("`#soa` cannot store `{}`: the fields of a `#bits` struct have no address", elem),
                );
                return Ty::error();
            }
            _ => {
                self.error(span, format!("`#soa` stores the fields of a struct, and `{}` is not a struct", elem));
                return Ty::error();
            }
        };
        if fields.is_empty() {
            self.error(span, format!("`#soa` needs a struct with fields, and `{}` has none", elem));
            return Ty::error();
        }
        if fields.iter().any(|f| f.name.as_str() == "len") {
            self.error(span, format!("`#soa` cannot store `{}`: its field `len` would hide the length, `a.len`", elem));
            return Ty::error();
        }
        let is_const = is_const && len.is_none();
        if let Some(&t) = self.soa_types.get(&(elem, len, is_const)) {
            return t;
        }
        let shape = match len {
            Some(n) => format!("[{}]", n),
            None if is_const => "[]const ".to_string(),
            None => "[]".to_string(),
        };
        let name = Symbol::intern(&format!("#soa {}{}", shape, elem));
        let mut columns: Vec<FieldDef> = fields
            .iter()
            .map(|f| FieldDef {
                name: f.name,
                ty: match len {
                    Some(n) => Ty::array(n, f.ty),
                    None => Ty::many_ptr(is_const, f.ty),
                },
                is_pub: true,
                align: None,
            })
            .collect();
        if len.is_none() {
            columns.push(FieldDef { name: Symbol::intern("len"), ty: Ty::usize(), is_pub: true, align: None });
        }
        let module = self.adts.get(&id).map(|a| a.module).unwrap_or(ModuleId(0));
        let soa = AdtId::fresh(name);
        let defaults = vec![None; columns.len()];
        self.adts.insert(soa, AdtInfo { name, module, defaults, has_using: false });
        let kind = AdtKind::Struct { fields: columns, packed: false, align: None, bits: None };
        self.prog.adts.insert(soa, AdtDef { name, kind });
        self.soa.insert(soa, SoaInfo { elem, len, is_const });
        let t = Ty::intern(TyKind::Adt(soa, Box::new([])));
        self.soa_types.insert((elem, len, is_const), t);
        t
    }

    /// `#soa [N]S` or `#soa []S` written as a type: `inner` is the array or slice type.
    pub fn soa_type_expr(&mut self, module: ModuleId, inner: &ast::Expr, span: Span) -> Ty {
        match &inner.kind {
            ast::ExprKind::ArrayType { len: Some(len), elem } => {
                let elem = self.resolve_type(module, elem);
                let n = match self.array_len(module, len) {
                    Some(super::ty_expr::ArrayLen::Known(n)) => n,
                    Some(super::ty_expr::ArrayLen::Param(_)) => {
                        self.error(len.span, "a `#soa` array's length is a constant, not a compile-time parameter");
                        return Ty::error();
                    }
                    None => return Ty::error(),
                };
                self.soa_type(elem, Some(n), false, span)
            }
            ast::ExprKind::SliceType { is_const, inner } => {
                let elem = self.resolve_type(module, inner);
                self.soa_type(elem, None, *is_const, span)
            }
            _ => {
                self.error(span, "`#soa` precedes an array or slice type of a struct: `#soa [N]S` or `#soa []S`");
                Ty::error()
            }
        }
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// What the type of `e` stores, if it is a `#soa` type.
    pub fn soa_of(&mut self, e: &Expr) -> Option<SoaInfo> {
        let t = self.infer.resolve(e.ty);
        self.sema.soa_info(t)
    }

    /// `p^` if `b` is a pointer to a `#soa` array, else `b`.
    pub fn soa_deref(&mut self, b: Expr) -> Expr {
        let t = self.infer.resolve(b.ty);
        match t.kind() {
            TyKind::Ptr { pointee, .. } if self.sema.soa_info(*pointee).is_some() => {
                let (p, span) = (*pointee, b.span);
                mk(p, tir::ExprKind::Deref(Box::new(b)), span)
            }
            _ => b,
        }
    }

    /// The `#soa` value `b` in a form that can be used several times, with the statements
    /// that compute it: an array place through a pointer, so that its columns stay places;
    /// anything else through a temporary.
    fn soa_operand(&mut self, b: Expr, info: SoaInfo, stmts: &mut Vec<Stmt>) -> Expr {
        if repeatable(&b) {
            return b;
        }
        if info.len.is_some()
            && let Some(m) = self.place_mutability(&b)
        {
            let (t, span) = (b.ty, b.span);
            let pty = Ty::ptr(!m, t);
            let p = self.temp(pty, span);
            stmts.push(Stmt::Let { local: p, init: Some(mk(pty, tir::ExprKind::AddrOf(Box::new(b)), span)) });
            let local = mk(pty, tir::ExprKind::Local(p), span);
            return mk(t, tir::ExprKind::Deref(Box::new(local)), span);
        }
        self.bind_temp(b, stmts)
    }

    /// Column `k` of the `#soa` value `v`, which must be repeatable if it is a slice: the
    /// array `[N]F` (a place when `v` is one), or the slice `[]F`.
    fn soa_column(&mut self, v: &Expr, info: SoaInfo, k: usize, span: Span) -> Expr {
        let fields = self.sema.struct_fields(v.ty).map(|f| f.to_vec()).unwrap_or_default();
        let ft = fields[k].ty;
        let col = mk(ft, tir::ExprKind::Field { base: Box::new(v.clone()), index: k as u32 }, span);
        if info.len.is_some() {
            return col;
        }
        let TyKind::ManyPtr { elem, is_const } = ft.kind() else { unreachable!("a column pointer") };
        let len =
            mk(Ty::usize(), tir::ExprKind::Field { base: Box::new(v.clone()), index: fields.len() as u32 - 1 }, span);
        mk(
            Ty::slice(*is_const, *elem),
            tir::ExprKind::Slice {
                base: Box::new(col),
                lo: Some(Box::new(usize_lit(0, span))),
                hi: Some(Box::new(len)),
                inclusive: false,
            },
            span,
        )
    }

    /// Column `k` of `b`, which is used once.
    fn soa_column_once(&mut self, b: Expr, info: SoaInfo, k: usize, span: Span) -> Expr {
        if info.len.is_some() || repeatable(&b) {
            return self.soa_column(&b, info, k, span);
        }
        let mut stmts = vec![];
        let v = self.bind_temp(b, &mut stmts);
        let col = self.soa_column(&v, info, k, span);
        block(stmts, col, span)
    }

    /// The length of the repeatable `#soa` value `v`.
    fn soa_len(&mut self, v: &Expr, info: SoaInfo, span: Span) -> Expr {
        match info.len {
            Some(n) => usize_lit(n, span),
            None => {
                let k = self.sema.struct_fields(v.ty).map_or(0, |f| f.len() - 1);
                mk(Ty::usize(), tir::ExprKind::Field { base: Box::new(v.clone()), index: k as u32 }, span)
            }
        }
    }

    /// The fields of the element struct of `info`.
    fn soa_fields(&self, info: SoaInfo) -> Vec<FieldDef> {
        self.sema.struct_fields(info.elem).map(|f| f.to_vec()).unwrap_or_default()
    }

    /// Checks an index of a `#soa` value: an unsigned integer.
    fn soa_index_value(&mut self, index: &ast::Expr) -> Expr {
        let i = self.expr(index, None);
        if !self.infer.require_unsigned(i.ty, Fallback::Usize) && !i.ty.is_error() {
            let t = self.infer.resolve(i.ty);
            self.error(i.span, format!("an index must have an unsigned integer type, not `{}`", t));
        }
        i
    }

    /// Element `i` of every column of the repeatable `v`, gathered into the element struct.
    fn soa_gather(&mut self, v: &Expr, info: SoaInfo, i: &Expr, span: Span) -> Expr {
        let n = self.soa_fields(info).len();
        let mut parts = vec![];
        for k in 0..n {
            let col = self.soa_column(v, info, k, span);
            let ft = self.soa_fields(info)[k].ty;
            parts.push(mk(
                ft,
                tir::ExprKind::Index { base: Box::new(col), index: Box::new(i.clone()), checked: true },
                span,
            ));
        }
        mk(info.elem, tir::ExprKind::Aggregate(parts), span)
    }

    /// `a[i]` read: the element, gathered from the columns (§5.6.4).
    pub fn soa_element(&mut self, b: Expr, info: SoaInfo, index: &ast::Expr, span: Span) -> Expr {
        let mut stmts = vec![];
        let v = self.soa_operand(b, info, &mut stmts);
        let i = self.soa_index_value(index);
        let i = self.bind_temp(i, &mut stmts);
        let g = self.soa_gather(&v, info, &i, span);
        self.sema.soa_gathers.insert(span);
        block(stmts, g, span)
    }

    /// `a[i].f`: element `i` of column `f`, a place (§5.6.4).
    pub fn soa_element_field(
        &mut self,
        b: Expr,
        info: SoaInfo,
        index: &ast::Expr,
        name: ast::Ident,
        span: Span,
    ) -> Expr {
        let fields = self.soa_fields(info);
        let Some(k) = fields.iter().position(|f| f.name == name.name) else {
            let _ = self.expr(index, None);
            self.error(name.span, format!("type `{}` has no field `{}`", info.elem, name.name));
            return error_expr(span);
        };
        if !fields[k].is_pub && !self.sees_private(info.elem) {
            self.error(span, format!("the field `{}` of `{}` is private to its module (§3.3)", name.name, info.elem));
        }
        let col = self.soa_column_once(b, info, k, span);
        let i = self.soa_index_value(index);
        mk(fields[k].ty, tir::ExprKind::Index { base: Box::new(col), index: Box::new(i), checked: true }, span)
    }

    /// `a.f` (the column of `f`) or `a.len` on a `#soa` value, or `None` if `name` is
    /// neither.
    pub fn soa_member(&mut self, b: Expr, info: SoaInfo, name: ast::Ident, span: Span) -> Expr {
        if name.name.as_str() == "len" {
            let mut stmts = vec![];
            let v = if info.len.is_some() { b } else { self.soa_operand(b, info, &mut stmts) };
            let len = self.soa_len(&v, info, span);
            return block(stmts, len, span);
        }
        let fields = self.soa_fields(info);
        let Some(k) = fields.iter().position(|f| f.name == name.name) else {
            let t = self.infer.resolve(b.ty);
            self.error(name.span, format!("type `{}` has no column `{}`", t, name.name));
            return error_expr(span);
        };
        if !fields[k].is_pub && !self.sees_private(info.elem) {
            self.error(span, format!("the field `{}` of `{}` is private to its module (§3.3)", name.name, info.elem));
        }
        self.soa_column_once(b, info, k, span)
    }

    /// `a[i] = value`: stores each field of the value in its column (§5.6.4).
    pub fn soa_store(&mut self, b: Expr, info: SoaInfo, index: &ast::Expr, value: &ast::Expr, span: Span) -> Stmt {
        let writable = match info.len {
            Some(_) => self.place_mutability(&b) == Some(true),
            None => !info.is_const,
        };
        if !writable {
            let what = if info.len.is_some() { "an immutable `#soa` array" } else { "a `#soa []const` slice" };
            self.error(span, format!("cannot assign to an element of {}", what));
        }
        let mut stmts = vec![];
        let v = self.soa_operand(b, info, &mut stmts);
        let i = self.soa_index_value(index);
        let i = self.bind_temp(i, &mut stmts);
        let x = self.expr(value, Some(info.elem));
        let x = self.coerce(x, info.elem);
        let x = self.bind_temp(x, &mut stmts);
        let fields = self.soa_fields(info);
        for (k, f) in fields.iter().enumerate() {
            let col = self.soa_column(&v, info, k, span);
            let place =
                mk(f.ty, tir::ExprKind::Index { base: Box::new(col), index: Box::new(i.clone()), checked: true }, span);
            let part = mk(f.ty, tir::ExprKind::Field { base: Box::new(x.clone()), index: k as u32 }, span);
            stmts.push(Stmt::Assign { place, value: part });
        }
        Stmt::Expr(mk(Ty::unit(), tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: None, span })), span))
    }

    /// `a[lo..hi]` (or `a[..]`) on a `#soa` array place or slice: a `#soa` slice.
    pub fn soa_slice(
        &mut self,
        b: Expr,
        info: SoaInfo,
        lo: Option<Expr>,
        hi: Option<Expr>,
        inclusive: bool,
        span: Span,
    ) -> Expr {
        let base_span = b.span;
        let is_const = match info.len {
            Some(_) => match self.place_mutability(&b) {
                Some(m) => !m,
                None => {
                    self.error(base_span, "only a `#soa` array variable (a place) can be sliced, not a temporary");
                    return error_expr(span);
                }
            },
            None => info.is_const,
        };
        let mut stmts = vec![];
        let v = self.soa_operand(b, info, &mut stmts);
        let mut bound = |e: Option<Expr>, cx: &mut Self| {
            e.map(|v| {
                if !cx.infer.require_unsigned(v.ty, Fallback::Usize) && !v.ty.is_error() {
                    let t = cx.infer.resolve(v.ty);
                    cx.error(v.span, format!("a slice bound must have an unsigned integer type, not `{}`", t));
                }
                cx.bind_temp(v, &mut stmts)
            })
        };
        let lo = bound(lo, self);
        let hi = bound(hi, self);
        let t = self.sema.soa_type(info.elem, None, is_const, span);
        let fields = self.soa_fields(info);
        let mut parts = vec![];
        let mut len = None;
        for (k, f) in fields.iter().enumerate() {
            let col = self.soa_column(&v, info, k, span);
            let st = Ty::slice(is_const, f.ty);
            let s = mk(
                st,
                tir::ExprKind::Slice {
                    base: Box::new(col),
                    lo: lo.clone().map(Box::new),
                    hi: hi.clone().map(Box::new),
                    inclusive,
                },
                span,
            );
            let s = self.bind_temp(s, &mut stmts);
            if len.is_none() {
                len = Some(mk(Ty::usize(), tir::ExprKind::SliceLen(Box::new(s.clone())), span));
            }
            parts.push(mk(Ty::many_ptr(is_const, f.ty), tir::ExprKind::SlicePtr(Box::new(s)), span));
        }
        parts.extend(len);
        block(stmts, mk(t, tir::ExprKind::Aggregate(parts), span), span)
    }

    /// The coercions of §5.6.4: `^#soa [N]S` to `#soa []S` (or `[]const`), and
    /// `#soa []S` to `#soa []const S`. `None` if neither applies.
    pub fn soa_coerce(&mut self, e: &Expr, src: Ty, target: Ty) -> Option<Expr> {
        let to = self.sema.soa_info(target).filter(|t| t.len.is_none())?;
        let span = e.span;
        if let TyKind::Ptr { is_const, pointee } = src.kind()
            && let Some(from) = self.sema.soa_info(*pointee)
            && from.len.is_some()
            && from.elem == to.elem
            && (to.is_const || !is_const)
        {
            let arr = mk(*pointee, tir::ExprKind::Deref(Box::new(e.clone())), span);
            let s = self.soa_slice(arr, from, None, None, false, span);
            return Some(if to.is_const { self.soa_coerce(&s, s.ty, target).unwrap_or(s) } else { s });
        }
        let from = self.sema.soa_info(src)?;
        if from.len.is_some() || from.elem != to.elem || from.is_const || !to.is_const {
            return None;
        }
        let mut stmts = vec![];
        let v = self.bind_temp(e.clone(), &mut stmts);
        let fields = self.sema.struct_fields(src).map(|f| f.to_vec()).unwrap_or_default();
        let target_fields = self.sema.struct_fields(target).map(|f| f.to_vec()).unwrap_or_default();
        let parts = fields
            .iter()
            .zip(&target_fields)
            .enumerate()
            .map(|(k, (f, g))| {
                let part = mk(f.ty, tir::ExprKind::Field { base: Box::new(v.clone()), index: k as u32 }, span);
                if f.ty == g.ty {
                    part
                } else {
                    mk(g.ty, tir::ExprKind::Coerce { kind: tir::Coercion::PtrToPtr, expr: Box::new(part) }, span)
                }
            })
            .collect();
        Some(block(stmts, mk(target, tir::ExprKind::Aggregate(parts), span), span))
    }

    /// `#soa [N]S(e1, …, eN)` and `#soa [N]S(fill: s)` (§5.6.4).
    pub fn soa_construct(&mut self, t: Ty, info: SoaInfo, args: &[ast::Arg], span: Span) -> Expr {
        let Some(n) = info.len else {
            self.error(span, format!("`{}` is made by slicing an array or by `.make(n)`, not constructed", t));
            return error_expr(span);
        };
        let fields = self.soa_fields(info);
        let mut stmts = vec![];
        let columns: Vec<Expr> = if let [ast::Arg { kind: ArgKind::Named(name, e), .. }] = args
            && name.name.as_str() == "fill"
        {
            let x = self.field_value(e, info.elem);
            let x = self.bind_temp(x, &mut stmts);
            fields
                .iter()
                .enumerate()
                .map(|(k, f)| {
                    let part = mk(f.ty, tir::ExprKind::Field { base: Box::new(x.clone()), index: k as u32 }, span);
                    mk(Ty::array(n, f.ty), tir::ExprKind::ArrayFill(Box::new(part)), span)
                })
                .collect()
        } else {
            let mut elems = vec![];
            for a in args {
                match &a.kind {
                    ArgKind::Positional(e) => {
                        let x = self.field_value(e, info.elem);
                        elems.push(self.bind_temp(x, &mut stmts));
                    }
                    _ => {
                        self.error(a.span, "the elements of a `#soa` array are written positionally, or as `fill: s`");
                        return error_expr(span);
                    }
                }
            }
            if elems.len() as u64 != n {
                self.error(
                    span,
                    format!("`{}` needs exactly {} elements, but {} given", t, n, super::construct::given(elems.len())),
                );
                return error_expr(span);
            }
            fields
                .iter()
                .enumerate()
                .map(|(k, f)| {
                    let parts = elems
                        .iter()
                        .map(|x| mk(f.ty, tir::ExprKind::Field { base: Box::new(x.clone()), index: k as u32 }, span))
                        .collect();
                    mk(Ty::array(n, f.ty), tir::ExprKind::Array(parts), span)
                })
                .collect()
        };
        block(stmts, mk(t, tir::ExprKind::Aggregate(columns), span), span)
    }

    /// `for s in a` over a `#soa` value: each element in order, gathered.
    pub fn soa_for(&mut self, f: &ast::ForExpr, src: Expr, info: SoaInfo) -> Expr {
        let span = f.span;
        let mut stmts = vec![];
        let v = self.soa_operand(src, info, &mut stmts);
        let i = self.temp(Ty::usize(), span);
        let n = self.temp(Ty::usize(), span);
        let len = self.soa_len(&v, info, span);
        stmts.push(Stmt::Let { local: i, init: Some(usize_lit(0, span)) });
        stmts.push(Stmt::Let { local: n, init: Some(len) });
        let local = |id| mk(Ty::usize(), tir::ExprKind::Local(id), span);
        let lt = mk(
            Ty::bool(),
            tir::ExprKind::Binary { op: tir::BinOp::Lt, lhs: Box::new(local(i)), rhs: Box::new(local(n)) },
            span,
        );
        let incr = Stmt::Assign {
            place: local(i),
            value: mk(
                Ty::usize(),
                tir::ExprKind::Binary {
                    op: tir::BinOp::Add(ast::Flavor::Wrap),
                    lhs: Box::new(local(i)),
                    rhs: Box::new(usize_lit(1, span)),
                },
                span,
            ),
        };
        let step =
            mk(Ty::unit(), tir::ExprKind::Block(Box::new(tir::Block { stmts: vec![incr], tail: None, span })), span);
        let item = self.soa_gather(&v, info, &local(i), span);
        let lp = self.loop_with_pattern(f, info.elem, item, vec![tir::CondPart::Bool(lt)], step);
        stmts.push(Stmt::Expr(lp));
        mk(Ty::unit(), tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: None, span })), span)
    }

    /// `a is b` (or `is not`) on `#soa` slices: the columns compare by content (§5.6.4).
    pub fn soa_eq(&mut self, l: Expr, r: Expr, info: SoaInfo, negated: bool, span: Span) -> Expr {
        let mut stmts = vec![];
        let lv = self.soa_operand(l, info, &mut stmts);
        let rv = self.soa_operand(r, info, &mut stmts);
        let mut all: Option<Expr> = None;
        for k in 0..self.soa_fields(info).len() {
            let (a, b) = (self.soa_column(&lv, info, k, span), self.soa_column(&rv, info, k, span));
            let eq = mk(Ty::bool(), tir::ExprKind::Eq { lhs: Box::new(a), rhs: Box::new(b), negated: false }, span);
            all = Some(match all {
                None => eq,
                Some(x) => {
                    mk(Ty::bool(), tir::ExprKind::Logic { is_and: true, lhs: Box::new(x), rhs: Box::new(eq) }, span)
                }
            });
        }
        let mut e = all.expect("a `#soa` element has fields");
        if negated {
            e = mk(Ty::bool(), tir::ExprKind::Unary { op: tir::UnOp::Not, operand: Box::new(e) }, span);
        }
        block(stmts, e, span)
    }

    /// The columns of a `#soa` slice as a tuple named by the fields, for formatting and
    /// hashing, which see a slice by its contents.
    pub fn soa_columns_tuple(&mut self, e: Expr) -> Expr {
        let Some(info) = self.soa_of(&e).filter(|i| i.len.is_none()) else { return e };
        let span = e.span;
        let mut stmts = vec![];
        let v = self.soa_operand(e, info, &mut stmts);
        let fields = self.soa_fields(info);
        let cols: Vec<Expr> = (0..fields.len()).map(|k| self.soa_column(&v, info, k, span)).collect();
        let names = fields.iter().map(|f| f.name).collect::<Vec<_>>().into_boxed_slice();
        let ty = Ty::intern(TyKind::Tuple { elems: cols.iter().map(|c| c.ty).collect(), names: Some(names) });
        block(stmts, mk(ty, tir::ExprKind::Aggregate(cols), span), span)
    }

    /// `T.make(n)` for a `#soa` slice type `T`: one block holding every column (§5.6.4).
    pub fn soa_make(&mut self, t: Ty, info: SoaInfo, args: &[ast::Arg], span: Span) -> Expr {
        if info.len.is_some() {
            self.error(span, format!("`.make(n)` makes a `#soa` slice; `{}` is an array", t));
            return error_expr(span);
        }
        if !self.arity(args, 1, "make", span) {
            return error_expr(span);
        }
        let n = self.value_arg(args, 0, Ty::usize(), span);
        let (sizes, aligns) = self.soa_layout(info, span);
        let Some(f) = self.sema.core_fn("slice", "soa_alloc") else {
            self.error(span, "`#soa` allocation is missing from the core library");
            return error_expr(span);
        };
        let mut stmts = vec![];
        let n = self.bind_temp(n, &mut stmts);
        let call = self.core_call_values(f, vec![sizes, aligns, n.clone()], span);
        // match call { ok(p) => T(p[0].cast(…), …, n), err(e) => !e }
        let rt = self.infer.resolve(call.ty);
        let TyKind::Result { err, ok } = rt.kind() else { return error_expr(span) };
        let (err, ok) = (*err, *ok);
        let p = self.temp(ok, span);
        let e = self.temp(err, span);
        let fields = self.sema.struct_fields(t).map(|f| f.to_vec()).unwrap_or_default();
        let mut parts = vec![];
        for (k, f) in fields.iter().take(fields.len() - 1).enumerate() {
            let raw = mk(
                Ty::intern(TyKind::Rawptr),
                tir::ExprKind::Index {
                    base: Box::new(mk(ok, tir::ExprKind::Local(p), span)),
                    index: Box::new(usize_lit(k as u64, span)),
                    checked: false,
                },
                span,
            );
            parts.push(mk(f.ty, tir::ExprKind::Builtin { op: Builtin::Cast, args: vec![raw] }, span));
        }
        parts.push(n);
        let result = Ty::result(err, t);
        let value = mk(t, tir::ExprKind::Aggregate(parts), span);
        let wrapped = mk(result, tir::ExprKind::Coerce { kind: tir::Coercion::WrapOk, expr: Box::new(value) }, span);
        let failed = mk(result, tir::ExprKind::MakeErr(Box::new(mk(err, tir::ExprKind::Local(e), span))), span);
        let bind = |l, ty| tir::Pat { ty, kind: tir::PatKind::Bind(l), span };
        let arms = vec![
            tir::Arm {
                pat: tir::Pat { ty: rt, kind: tir::PatKind::Ok(Box::new(bind(p, ok))), span },
                guard: None,
                body: wrapped,
            },
            tir::Arm {
                pat: tir::Pat { ty: rt, kind: tir::PatKind::Err(Box::new(bind(e, err))), span },
                guard: None,
                body: failed,
            },
        ];
        let m = mk(result, tir::ExprKind::Match { scrutinee: Box::new(call), arms }, span);
        block(stmts, m, span)
    }

    /// `s.free()` for a `#soa` slice from `make` (§5.6.4).
    pub fn soa_free(&mut self, recv: Expr, info: SoaInfo, args: &[ast::Arg], span: Span) -> Expr {
        if info.len.is_some() {
            self.error(span, "only a `#soa` slice from `.make(n)` is freed, not an array");
            return error_expr(span);
        }
        self.arity(args, 0, "free", span);
        let Some(f) = self.sema.core_fn("slice", "soa_free") else {
            self.error(span, "`#soa` allocation is missing from the core library");
            return error_expr(span);
        };
        let (sizes, aligns) = self.soa_layout(info, span);
        let mut stmts = vec![];
        let v = self.soa_operand(recv, info, &mut stmts);
        let first = self.sema.struct_fields(v.ty).map(|f| f[0].ty).unwrap_or(Ty::error());
        let base = mk(first, tir::ExprKind::Field { base: Box::new(v.clone()), index: 0 }, span);
        let base = mk(
            Ty::intern(TyKind::Rawptr),
            tir::ExprKind::Coerce { kind: tir::Coercion::PtrToPtr, expr: Box::new(base) },
            span,
        );
        let len = self.soa_len(&v, info, span);
        let call = self.core_call_values(f, vec![sizes, aligns, len, base], span);
        block(stmts, call, span)
    }

    /// The sizes and alignments of the element's fields, as `[K]usize` arrays.
    fn soa_layout(&mut self, info: SoaInfo, span: Span) -> (Expr, Expr) {
        let fields = self.soa_fields(info);
        let (sizes, aligns): (Vec<u64>, Vec<u64>) =
            self.sema.with_layouts(|l| fields.iter().map(|f| (l.size_of(f.ty), l.align_of(f.ty))).unzip());
        let arr = |v: Vec<u64>| {
            let t = Ty::array(v.len() as u64, Ty::usize());
            mk(t, tir::ExprKind::Array(v.into_iter().map(|x| usize_lit(x, span)).collect()), span)
        };
        (arr(sizes), arr(aligns))
    }
}
