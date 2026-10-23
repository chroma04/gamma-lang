//! Structure-of-arrays types `#soa [N]S` and `#soa []S` (§5.6.4).
//!
//! Each `#soa` type is a struct made by the compiler: for an array, one field `[N]F` for
//! each field `f: F` of `S`; for a slice, one many-pointer `[^]F` for each field and then
//! `len: usize`. The operations of §5.6.4 are lowered here to field accesses, indexing and
//! slicing of those columns, so the back ends see only structs, arrays and slices.
//!
//! In a generic body, `#soa []T` of a type parameter `T` is a stand-in: an instance of a
//! compiler-made generic type that each instantiation replaces with the real struct
//! ([`Sema::subst`]). Its operations on whole elements are kept as
//! [`tir::ExprKind::SoaDeferred`] and lowered by [`Sema::soa_expand`] in each instance,
//! with the same builders as for a concrete struct.

use super::func::{FnCx, error_expr};
use super::infer::Fallback;
use super::{AdtInfo, ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, AdtDef, AdtKind, Builtin, Expr, FieldDef, LocalDecl, LocalId, SoaOp, Stmt};
use crate::types::{AdtId, ConstKey, GenericArg, Ty, TyKind};

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

/// A new compiler temporary among `locals`.
fn temp(locals: &mut Vec<LocalDecl>, ty: Ty, span: Span) -> LocalId {
    let id = LocalId(locals.len() as u32);
    locals.push(LocalDecl { name: Symbol::intern("<tmp>"), ty, mutable: true, span });
    id
}

/// What a `#soa` type stores: elements of the struct `elem`, `len` of them for an array
/// (`None` for a slice), read-only for a `#soa []const S`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SoaInfo {
    pub elem: Ty,
    pub len: Option<u64>,
    pub is_const: bool,
}

impl SoaInfo {
    /// Whether the element type mentions a type parameter: the fields are not known, and
    /// the operations are lowered in each instance.
    fn generic(self) -> bool {
        super::generics::has_param(self.elem)
    }
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
        let TyKind::Adt(id, args) = t.kind() else { return None };
        match self.soa_templates.get(id) {
            Some(&(len, is_const)) => match args.first() {
                Some(GenericArg::Type(elem)) => Some(SoaInfo { elem: *elem, len, is_const }),
                _ => None,
            },
            None => self.soa.get(id).copied(),
        }
    }

    /// The `#soa` type of the struct `elem`: an array of `len` elements, or a slice
    /// (`len` is `None`). Reports an error and gives the error type if `elem` cannot be
    /// stored by field.
    pub fn soa_type(&mut self, elem: Ty, len: Option<u64>, is_const: bool, span: Span) -> Ty {
        match self.soa_type_of(elem, len, is_const) {
            Ok(t) => t,
            Err(msg) => {
                self.error(span, msg);
                Ty::error()
            }
        }
    }

    /// [`Sema::soa_type`], or why `elem` cannot be stored by field. A type parameter, or a
    /// generic type of one, gives the stand-in that each instantiation replaces.
    pub fn soa_type_of(&mut self, elem: Ty, len: Option<u64>, is_const: bool) -> Result<Ty, String> {
        if elem.is_error() {
            return Ok(Ty::error());
        }
        let is_const = is_const && len.is_none();
        if super::generics::has_param(elem) && matches!(elem.kind(), TyKind::Param(_) | TyKind::Adt(..)) {
            return Ok(self.soa_stand_in(elem, len, is_const));
        }
        let not_struct = || format!("`#soa` stores the fields of a struct, and `{}` is not a struct", elem);
        let TyKind::Adt(id, _) = elem.kind() else { return Err(not_struct()) };
        let id = *id;
        let fields = match self.prog.adts.get(&id).map(|d| &d.kind) {
            Some(AdtKind::Struct { bits: None, fields, .. }) => fields.clone(),
            Some(AdtKind::Struct { .. }) => {
                return Err(format!("`#soa` cannot store `{}`: the fields of a `#bits` struct have no address", elem));
            }
            _ => return Err(not_struct()),
        };
        if fields.is_empty() {
            return Err(format!("`#soa` needs a struct with fields, and `{}` has none", elem));
        }
        if fields.iter().any(|f| f.name.as_str() == "len") {
            return Err(format!("`#soa` cannot store `{}`: its field `len` would hide the length, `a.len`", elem));
        }
        if let Some(&t) = self.soa_types.get(&(elem, len, is_const)) {
            return Ok(t);
        }
        let name = Symbol::intern(&format!("#soa {}{}", soa_shape(len, is_const), elem));
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
        // An instance of the stand-in's template, so that `#soa []T` binds `T` (§16.2).
        soa.set_template(self.soa_template(len, is_const));
        let defaults = vec![None; columns.len()];
        self.adts.insert(soa, AdtInfo { name, module, defaults, has_using: false });
        let kind = AdtKind::Struct { fields: columns, packed: false, align: None, bits: None };
        self.prog.adts.insert(soa, AdtDef { name, kind });
        self.soa.insert(soa, SoaInfo { elem, len, is_const });
        let t = Ty::intern(TyKind::Adt(soa, Box::new([GenericArg::Type(elem)])));
        self.soa_types.insert((elem, len, is_const), t);
        Ok(t)
    }

    /// The stand-in for a `#soa` type of the generic `elem`: the compiler-made generic
    /// type of its shape, applied to `elem`.
    fn soa_stand_in(&mut self, elem: Ty, len: Option<u64>, is_const: bool) -> Ty {
        let id = self.soa_template(len, is_const);
        Ty::intern(TyKind::Adt(id, Box::new([GenericArg::Type(elem)])))
    }

    /// The compiler-made generic type of `#soa` types of a shape: the template of the
    /// stand-ins and of the real structs.
    fn soa_template(&mut self, len: Option<u64>, is_const: bool) -> AdtId {
        let found = self.soa_templates.iter().find(|(_, s)| **s == (len, is_const)).map(|(id, _)| *id);
        found.unwrap_or_else(|| {
            let id = AdtId::fresh(Symbol::intern(&format!("#soa {}", soa_shape(len, is_const))));
            self.soa_templates.insert(id, (len, is_const));
            id
        })
    }

    /// [`Sema::subst`] of the stand-in `#soa` type of `elem`: the real type, or the error
    /// type, with the reason kept for [`Sema::report_soa_errors`].
    pub(super) fn subst_soa(&mut self, elem: Ty, shape: (Option<u64>, bool)) -> Ty {
        match self.soa_type_of(elem, shape.0, shape.1) {
            Ok(t) => t,
            Err(msg) => {
                self.soa_errors.push(msg);
                Ty::error()
            }
        }
    }

    /// Reports, at the instantiation at `span`, the `#soa` types of its type arguments that
    /// cannot exist (§5.6.4, §16.1).
    pub fn report_soa_errors(&mut self, span: Span) {
        let mut seen = vec![];
        for msg in std::mem::take(&mut self.soa_errors) {
            if !seen.contains(&msg) {
                self.error(span, format!("{} (in an instantiation used here)", msg));
                seen.push(msg);
            }
        }
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

    // The builders below take operands that may be used repeatedly (see `repeatable`, and
    // array places through a variable) and need the element's fields.

    /// The fields of the element struct of `info`.
    fn soa_fields(&self, info: SoaInfo) -> Vec<FieldDef> {
        self.struct_fields(info.elem).map(|f| f.to_vec()).unwrap_or_default()
    }

    /// Column `k` of `v`: the array `[N]F` (a place when `v` is one), or the slice `[]F`.
    fn soa_column(&self, v: &Expr, info: SoaInfo, k: usize, span: Span) -> Expr {
        let fields = self.struct_fields(v.ty).map(|f| f.to_vec()).unwrap_or_default();
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

    /// The length of `v`.
    fn soa_len(&self, v: &Expr, info: SoaInfo, span: Span) -> Expr {
        match info.len {
            Some(n) => usize_lit(n, span),
            None => {
                let k = self.struct_fields(v.ty).map_or(0, |f| f.len() - 1);
                mk(Ty::usize(), tir::ExprKind::Field { base: Box::new(v.clone()), index: k as u32 }, span)
            }
        }
    }

    /// Element `i` of every column of `v`, gathered into the element struct.
    fn soa_gather(&self, v: &Expr, info: SoaInfo, i: &Expr, span: Span) -> Expr {
        let fields = self.soa_fields(info);
        let mut parts = vec![];
        for (k, f) in fields.iter().enumerate() {
            let col = self.soa_column(v, info, k, span);
            parts.push(mk(
                f.ty,
                tir::ExprKind::Index { base: Box::new(col), index: Box::new(i.clone()), checked: true },
                span,
            ));
        }
        mk(info.elem, tir::ExprKind::Aggregate(parts), span)
    }

    /// `v[i] = x`: each field of `x` stored in its column.
    fn soa_scatter(&self, v: &Expr, info: SoaInfo, i: &Expr, x: &Expr, span: Span) -> Vec<Stmt> {
        let fields = self.soa_fields(info);
        let mut stmts = vec![];
        for (k, f) in fields.iter().enumerate() {
            let col = self.soa_column(v, info, k, span);
            let place =
                mk(f.ty, tir::ExprKind::Index { base: Box::new(col), index: Box::new(i.clone()), checked: true }, span);
            let part = mk(f.ty, tir::ExprKind::Field { base: Box::new(x.clone()), index: k as u32 }, span);
            stmts.push(Stmt::Assign { place, value: part });
        }
        stmts
    }

    /// `v[lo..hi]` as a `#soa` slice, read-only with `is_const`: each column sliced alike.
    #[allow(clippy::too_many_arguments)]
    fn soa_slice_columns(
        &mut self,
        v: &Expr,
        info: SoaInfo,
        is_const: bool,
        lo: Option<&Expr>,
        hi: Option<&Expr>,
        inclusive: bool,
        span: Span,
    ) -> Expr {
        let t = self.soa_type(info.elem, None, is_const, span);
        let fields = self.soa_fields(info);
        let slice = |this: &Self, k: usize| {
            let col = this.soa_column(v, info, k, span);
            let kind = tir::ExprKind::Slice {
                base: Box::new(col),
                lo: lo.cloned().map(Box::new),
                hi: hi.cloned().map(Box::new),
                inclusive,
            };
            mk(Ty::slice(is_const, fields[k].ty), kind, span)
        };
        let mut parts: Vec<Expr> = (0..fields.len())
            .map(|k| mk(Ty::many_ptr(is_const, fields[k].ty), tir::ExprKind::SlicePtr(Box::new(slice(self, k))), span))
            .collect();
        parts.push(mk(Ty::usize(), tir::ExprKind::SliceLen(Box::new(slice(self, 0))), span));
        mk(t, tir::ExprKind::Aggregate(parts), span)
    }

    /// The slice `v` of type `src` as the `#soa []const` type `target`.
    fn soa_to_const(&self, v: &Expr, src: Ty, target: Ty, span: Span) -> Expr {
        let fields = self.struct_fields(src).map(|f| f.to_vec()).unwrap_or_default();
        let target_fields = self.struct_fields(target).map(|f| f.to_vec()).unwrap_or_default();
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
        mk(target, tir::ExprKind::Aggregate(parts), span)
    }

    /// The instance of the core function `name` (`soa_alloc` or `soa_free`) for the
    /// element of `info`, with its layout arguments.
    fn soa_core(&mut self, name: &str, info: SoaInfo, span: Span) -> Option<(tir::FnId, Expr, Expr)> {
        let Some(f) = self.core_fn("slice", name) else {
            self.error(span, "`#soa` allocation is missing from the core library");
            return None;
        };
        let (sizes, aligns) = self.soa_layout(info, span);
        let k = self.soa_fields(info).len() as u64;
        let k = Ty::intern(TyKind::Value(ConstKey::int(Ty::usize(), &k.into(), 8)));
        // Errors waiting for the instantiation being produced are reported there, not here.
        let pending = std::mem::take(&mut self.soa_errors);
        let f = self.instantiate(f, vec![k], span);
        self.soa_errors = pending;
        Some((f, sizes, aligns))
    }

    /// `T.make(n)`: one block holding every column, from `soa_alloc` (§5.6.4).
    fn soa_make_columns(
        &mut self,
        locals: &mut Vec<LocalDecl>,
        t: Ty,
        info: SoaInfo,
        n: Expr,
        site: Expr,
        span: Span,
    ) -> Expr {
        let Some((f, sizes, aligns)) = self.soa_core("soa_alloc", info, span) else { return error_expr(span) };
        let rt = self.sigs[&f].ret;
        let call = mk(
            rt,
            tir::ExprKind::Call { callee: tir::Callee::Fn(f), args: vec![sizes, aligns, n.clone(), site] },
            span,
        );
        // match call { ok(p) => T(p[0].cast(…), …, n), err(e) => !e }
        let TyKind::Result { err, ok } = rt.kind() else { return error_expr(span) };
        let (err, ok) = (*err, *ok);
        let p = temp(locals, ok, span);
        let e = temp(locals, err, span);
        let fields = self.struct_fields(t).map(|f| f.to_vec()).unwrap_or_default();
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
        mk(result, tir::ExprKind::Match { scrutinee: Box::new(call), arms }, span)
    }

    /// `v.free()`: releases the block from `make` through `soa_free` (§5.6.4).
    fn soa_free_columns(&mut self, v: &Expr, info: SoaInfo, site: Expr, span: Span) -> Expr {
        let Some((f, sizes, aligns)) = self.soa_core("soa_free", info, span) else { return error_expr(span) };
        let first = self.struct_fields(v.ty).map(|f| f[0].ty).unwrap_or(Ty::error());
        let base = mk(first, tir::ExprKind::Field { base: Box::new(v.clone()), index: 0 }, span);
        let base = mk(
            Ty::intern(TyKind::Rawptr),
            tir::ExprKind::Coerce { kind: tir::Coercion::PtrToPtr, expr: Box::new(base) },
            span,
        );
        let len = self.soa_len(v, info, span);
        let args = vec![sizes, aligns, len, base, site];
        mk(self.sigs[&f].ret, tir::ExprKind::Call { callee: tir::Callee::Fn(f), args }, span)
    }

    /// The sizes and alignments of the element's fields, as `[K]usize` arrays.
    fn soa_layout(&mut self, info: SoaInfo, span: Span) -> (Expr, Expr) {
        let fields = self.soa_fields(info);
        let (sizes, aligns): (Vec<u64>, Vec<u64>) =
            self.with_layouts(|l| fields.iter().map(|f| (l.size_of(f.ty), l.align_of(f.ty))).unzip());
        let arr = |v: Vec<u64>| {
            let t = Ty::array(v.len() as u64, Ty::usize());
            mk(t, tir::ExprKind::Array(v.into_iter().map(|x| usize_lit(x, span)).collect()), span)
        };
        (arr(sizes), arr(aligns))
    }

    /// Lowers a [`tir::ExprKind::SoaDeferred`] of an instance, whose types are now concrete.
    /// New temporaries go to `locals`, those of the function the expression is in.
    pub(super) fn soa_expand(&mut self, locals: &mut Vec<LocalDecl>, e: Expr) -> Expr {
        let span = e.span;
        let tir::ExprKind::SoaDeferred { op, args } = e.kind else { return e };
        // A `#soa` type that cannot exist was reported at the instantiation.
        let info_of = |this: &Self, t: Ty| this.soa_info(t).filter(|i| !i.generic());
        let operand = match op {
            SoaOp::Make => match e.ty.kind() {
                TyKind::Result { ok, .. } => *ok,
                _ => Ty::error(),
            },
            _ => args[0].ty,
        };
        let Some(info) = info_of(self, operand) else { return error_expr(span) };
        match op {
            SoaOp::Get => self.soa_gather(&args[0], info, &args[1], span),
            SoaOp::Set => {
                let stmts = self.soa_scatter(&args[0], info, &args[1], &args[2], span);
                super::func::block_of(stmts, span)
            }
            SoaOp::Len => self.soa_len(&args[0], info, span),
            SoaOp::Slice { lo, hi, inclusive } => {
                let Some(to) = info_of(self, e.ty) else { return error_expr(span) };
                let mut bounds = args[1..].iter();
                let lo = if lo { bounds.next() } else { None };
                let hi = if hi { bounds.next() } else { None };
                self.soa_slice_columns(&args[0], info, to.is_const, lo, hi, inclusive, span)
            }
            SoaOp::ToConst => self.soa_to_const(&args[0], args[0].ty, e.ty, span),
            SoaOp::Make => {
                let mut args = args.into_iter();
                let (n, site) = (args.next().unwrap(), args.next().unwrap());
                self.soa_make_columns(locals, operand, info, n, site, span)
            }
            SoaOp::Free => self.soa_free_columns(&args[0], info, args[1].clone(), span),
        }
    }
}

/// How a `#soa` type's name shows its shape: `[4]`, `[]` or `[]const `.
fn soa_shape(len: Option<u64>, is_const: bool) -> String {
    match len {
        Some(n) => format!("[{}]", n),
        None if is_const => "[]const ".to_string(),
        None => "[]".to_string(),
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

    /// The error for an operation on a `#soa` of a type parameter that needs the fields.
    fn soa_needs_fields(&mut self, info: SoaInfo, what: &str, span: Span) -> Expr {
        self.error(
            span,
            format!(
                "{} needs the fields of `{}`, which a generic function does not know; a `#soa` of a type parameter has \
                 whole elements (`a[i]`), `len`, slicing, `make` and `free` (§5.6.4)",
                what, info.elem
            ),
        );
        error_expr(span)
    }

    /// Column `k` of `b`, which is used once.
    fn soa_column_once(&mut self, b: Expr, info: SoaInfo, k: usize, span: Span) -> Expr {
        if info.len.is_some() || repeatable(&b) {
            return self.sema.soa_column(&b, info, k, span);
        }
        let mut stmts = vec![];
        let v = self.bind_temp(b, &mut stmts);
        let col = self.sema.soa_column(&v, info, k, span);
        block(stmts, col, span)
    }

    /// The length of the repeatable `#soa` value `v`.
    fn soa_len(&mut self, v: &Expr, info: SoaInfo, span: Span) -> Expr {
        if info.len.is_none() && info.generic() {
            return mk(Ty::usize(), tir::ExprKind::SoaDeferred { op: SoaOp::Len, args: vec![v.clone()] }, span);
        }
        self.sema.soa_len(v, info, span)
    }

    /// Element `i` of the repeatable `v`, gathered from the columns.
    fn soa_gather(&mut self, v: &Expr, info: SoaInfo, i: &Expr, span: Span) -> Expr {
        if info.generic() {
            let kind = tir::ExprKind::SoaDeferred { op: SoaOp::Get, args: vec![v.clone(), i.clone()] };
            return mk(info.elem, kind, span);
        }
        self.sema.soa_gather(v, info, i, span)
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
        if info.generic() {
            let _ = self.expr(index, None);
            return self.soa_needs_fields(info, &format!("`a[i].{}`", name.name), span);
        }
        let fields = self.sema.soa_fields(info);
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
        if info.generic() {
            return self.soa_needs_fields(info, &format!("the column `a.{}`", name.name), span);
        }
        let fields = self.sema.soa_fields(info);
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
        if info.generic() {
            let kind = tir::ExprKind::SoaDeferred { op: SoaOp::Set, args: vec![v, i, x] };
            stmts.push(Stmt::Expr(mk(Ty::unit(), kind, span)));
        } else {
            stmts.extend(self.sema.soa_scatter(&v, info, &i, &x, span));
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
        let s = if info.generic() {
            let t = self.sema.soa_type(info.elem, None, is_const, span);
            let op = SoaOp::Slice { lo: lo.is_some(), hi: hi.is_some(), inclusive };
            let args = std::iter::once(v).chain(lo).chain(hi).collect();
            mk(t, tir::ExprKind::SoaDeferred { op, args }, span)
        } else {
            self.sema.soa_slice_columns(&v, info, is_const, lo.as_ref(), hi.as_ref(), inclusive, span)
        };
        block(stmts, s, span)
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
        let c = if from.generic() {
            mk(target, tir::ExprKind::SoaDeferred { op: SoaOp::ToConst, args: vec![v] }, span)
        } else {
            self.sema.soa_to_const(&v, src, target, span)
        };
        Some(block(stmts, c, span))
    }

    /// `#soa [N]S(e1, …, eN)` and `#soa [N]S(fill: s)` (§5.6.4).
    pub fn soa_construct(&mut self, t: Ty, info: SoaInfo, args: &[ast::Arg], span: Span) -> Expr {
        let Some(n) = info.len else {
            self.error(span, format!("`{}` is made by slicing an array or by `.make(n)`, not constructed", t));
            return error_expr(span);
        };
        if info.generic() {
            return self.soa_needs_fields(info, &format!("constructing `{}`", t), span);
        }
        let fields = self.sema.soa_fields(info);
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
        if info.generic() {
            return self.soa_needs_fields(info, "`is` on `#soa` slices", span);
        }
        let mut stmts = vec![];
        let lv = self.soa_operand(l, info, &mut stmts);
        let rv = self.soa_operand(r, info, &mut stmts);
        let mut all: Option<Expr> = None;
        for k in 0..self.sema.soa_fields(info).len() {
            let (a, b) = (self.sema.soa_column(&lv, info, k, span), self.sema.soa_column(&rv, info, k, span));
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
    /// hashing, which see a slice by its contents. `what` names the use, for the error on
    /// a `#soa` of a type parameter.
    pub fn soa_columns_tuple(&mut self, e: Expr, what: &str) -> Expr {
        let Some(info) = self.soa_of(&e) else { return e };
        if info.generic() {
            return self.soa_needs_fields(info, what, e.span);
        }
        if info.len.is_some() {
            return e;
        }
        let span = e.span;
        let mut stmts = vec![];
        let v = self.soa_operand(e, info, &mut stmts);
        let fields = self.sema.soa_fields(info);
        let cols: Vec<Expr> = (0..fields.len()).map(|k| self.sema.soa_column(&v, info, k, span)).collect();
        let names = fields.iter().map(|f| f.name).collect::<Vec<_>>().into_boxed_slice();
        let ty = Ty::intern(TyKind::Tuple { elems: cols.iter().map(|c| c.ty).collect(), names: Some(names) });
        block(stmts, mk(ty, tir::ExprKind::Aggregate(cols), span), span)
    }

    /// The effects of the core function `name` count as performed here, and the source
    /// location argument it is given.
    fn soa_core_use(&mut self, name: &str, span: Span) -> Option<Expr> {
        let Some(f) = self.sema.core_fn("slice", name) else {
            self.error(span, "`#soa` allocation is missing from the core library");
            return None;
        };
        let effects = self.sema.sigs[&f].effects.clone();
        self.record_effects(&effects, span);
        let (at, function) = self.call_site(span);
        Some(self.sema.source_location(at, function))
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
        let Some(site) = self.soa_core_use("soa_alloc", span) else { return error_expr(span) };
        let mut stmts = vec![];
        let n = self.bind_temp(n, &mut stmts);
        let m = if info.generic() {
            let err = match self.sema.core_fn("slice", "soa_alloc").map(|f| self.sema.sigs[&f].ret) {
                Some(rt) => match rt.kind() {
                    TyKind::Result { err, .. } => *err,
                    _ => Ty::error(),
                },
                None => Ty::error(),
            };
            mk(Ty::result(err, t), tir::ExprKind::SoaDeferred { op: SoaOp::Make, args: vec![n, site] }, span)
        } else {
            self.sema.soa_make_columns(&mut self.locals, t, info, n, site, span)
        };
        block(stmts, m, span)
    }

    /// `s.free()` for a `#soa` slice from `make` (§5.6.4).
    pub fn soa_free(&mut self, recv: Expr, info: SoaInfo, args: &[ast::Arg], span: Span) -> Expr {
        if info.len.is_some() {
            self.error(span, "only a `#soa` slice from `.make(n)` is freed, not an array");
            return error_expr(span);
        }
        self.arity(args, 0, "free", span);
        let Some(site) = self.soa_core_use("soa_free", span) else { return error_expr(span) };
        let mut stmts = vec![];
        let v = self.soa_operand(recv, info, &mut stmts);
        let call = if info.generic() {
            mk(Ty::unit(), tir::ExprKind::SoaDeferred { op: SoaOp::Free, args: vec![v, site] }, span)
        } else {
            self.sema.soa_free_columns(&v, info, site, span)
        };
        block(stmts, call, span)
    }
}
