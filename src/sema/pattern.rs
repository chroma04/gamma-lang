//! Checking patterns (§9) and lowering them to [`tir::Pat`].

use super::DeclValue;
use super::func::{FnCx, LocalBinding};
use super::untyped::Untyped;
use crate::diag::Diagnostic;
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind, PatternKind};
use crate::tir::{self, Expr, Pat, PatKind};
use crate::types::{Ty, TyKind};
use num_bigint::BigInt;
use std::rc::Rc;

/// Where a pattern appears, which decides how bare bindings treat optionals
/// and results (§9.3).
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum PatCtx {
    /// `match` arms and binding conditions: patterns implicitly unwrap.
    Refutable,
    /// `for` patterns and destructuring: a bare binding binds the whole value.
    Irrefutable,
    /// The right operand of `is`: no bindings allowed.
    IsTest,
}

fn pat(ty: Ty, kind: PatKind, span: Span) -> Pat {
    Pat { ty, kind, span }
}

impl<'a, 's> FnCx<'a, 's> {
    /// Checks `p` against a scrutinee of type `ty`. Bindings are collected into
    /// `binds` (name, local, span); the caller declares them in scope afterwards.
    /// Binds the names of pattern `p` at the error type, for a pattern that could not
    /// be checked.
    fn bind_unchecked(&mut self, p: &ast::Pattern, ctx: PatCtx, binds: &mut Vec<(Symbol, tir::LocalId, Span)>) {
        match &p.kind {
            PatternKind::Bind(name) if ctx != PatCtx::IsTest => {
                if !binds.iter().any(|(n, _, _)| *n == name.name) {
                    let id = self.new_local(name.name, Ty::error(), false, name.span);
                    binds.push((name.name, id, name.span));
                }
            }
            PatternKind::Err(inner) => self.bind_unchecked(inner, ctx, binds),
            PatternKind::Path { args: Some(args), .. } | PatternKind::Tuple(args) => {
                for a in args {
                    self.bind_unchecked(&a.pat, ctx, binds);
                }
            }
            _ => {}
        }
    }

    pub fn pattern(
        &mut self,
        p: &ast::Pattern,
        ty: Ty,
        ctx: PatCtx,
        binds: &mut Vec<(Symbol, tir::LocalId, Span)>,
    ) -> Pat {
        let ty = self.infer.resolve(ty);
        if ty.is_error() {
            self.bind_unchecked(p, ctx, binds);
            return pat(ty, PatKind::Wild, p.span);
        }
        // Implicit unwrapping of optionals and results in refutable contexts (§9.3). A
        // bare binding unwraps one level: against `??T` it binds the present `?T`.
        if ctx != PatCtx::Irrefutable {
            let bare = matches!(p.kind, PatternKind::Bind(_));
            match ty.kind() {
                TyKind::Optional(inner) if !matches!(p.kind, PatternKind::Null | PatternKind::Wildcard) => {
                    let inner_pat = if bare {
                        self.pattern_here(p, *inner, ctx, binds)
                    } else {
                        self.pattern(p, *inner, ctx, binds)
                    };
                    return pat(ty, PatKind::Some(Box::new(inner_pat)), p.span);
                }
                TyKind::Result { ok, .. } if !matches!(p.kind, PatternKind::Err(_) | PatternKind::Wildcard) => {
                    let inner_pat =
                        if bare { self.pattern_here(p, *ok, ctx, binds) } else { self.pattern(p, *ok, ctx, binds) };
                    return pat(ty, PatKind::Ok(Box::new(inner_pat)), p.span);
                }
                _ => {}
            }
        }
        self.pattern_here(p, ty, ctx, binds)
    }

    /// Pattern `p` against a value of type `ty`, without unwrapping `ty`.
    fn pattern_here(
        &mut self,
        p: &ast::Pattern,
        ty: Ty,
        ctx: PatCtx,
        binds: &mut Vec<(Symbol, tir::LocalId, Span)>,
    ) -> Pat {
        let ty = self.infer.resolve(ty);
        match &p.kind {
            PatternKind::Wildcard => pat(ty, PatKind::Wild, p.span),
            PatternKind::Bind(name) => {
                if ctx == PatCtx::IsTest {
                    self.error(p.span, "a pattern on the right of `is` cannot bind names; use `if pattern := value`");
                    return pat(ty, PatKind::Wild, p.span);
                }
                if self.names_constant(name.name) {
                    self.error(
                        p.span,
                        format!(
                            "`{}` is a constant, but a bare name in a pattern always binds a new name; \
                             compare with a guard instead (`x if x is {}`)",
                            name.name, name.name
                        ),
                    );
                }
                let id = self.new_local(name.name, ty, false, name.span);
                if binds.iter().any(|(n, _, _)| *n == name.name) {
                    // The name is declared once, by its first binding.
                    self.error(name.span, format!("`{}` is bound twice in this pattern", name.name));
                } else {
                    binds.push((name.name, id, name.span));
                }
                pat(ty, PatKind::Bind(id), p.span)
            }
            PatternKind::Null => {
                if !matches!(ty.kind(), TyKind::Optional(_)) {
                    self.error(p.span, format!("`null` matches only optionals, but this is `{}`", ty));
                }
                pat(ty, PatKind::Null, p.span)
            }
            PatternKind::Lit(e) => self.literal_pattern(e, ty, p.span),
            PatternKind::Range { lo, hi, inclusive } => {
                if !self.integer_pattern_ty(ty) {
                    self.error(p.span, format!("range patterns match integers, but this is `{}`", ty));
                    return pat(ty, PatKind::Wild, p.span);
                }
                let (Some(l), Some(h)) = (self.pattern_int(lo, ty), self.pattern_int(hi, ty)) else {
                    return pat(ty, PatKind::Wild, p.span);
                };
                if (*inclusive && l > h) || (!*inclusive && l >= h) {
                    self.error(p.span, "this range pattern matches nothing");
                }
                pat(ty, PatKind::Range { lo: l, hi: h, inclusive: *inclusive }, p.span)
            }
            PatternKind::Err(inner) => {
                let TyKind::Result { err, .. } = ty.kind() else {
                    self.error(p.span, format!("`!p` matches the error of a result, but this is `{}`", ty));
                    return pat(ty, PatKind::Wild, p.span);
                };
                let ip = self.pattern(inner, *err, ctx, binds);
                pat(ty, PatKind::Err(Box::new(ip)), p.span)
            }
            PatternKind::Tuple(args) => {
                let TyKind::Tuple { elems, names } = ty.kind() else {
                    self.error(p.span, format!("a tuple pattern needs a tuple, but this is `{}`", ty));
                    return pat(ty, PatKind::Wild, p.span);
                };
                let fields = self.sub_patterns(args, elems, names.as_deref(), ctx, binds, p.span, false);
                pat(ty, PatKind::Fields(fields), p.span)
            }
            PatternKind::Path { path, args } => {
                let (errors, nbinds) = (self.sema.sess.diags.error_count(), binds.len());
                let r = self.path_pattern(path, args.as_deref(), ty, ctx, binds, p.span);
                // After an error, still bind the names so that uses of them don't cascade.
                if matches!(r.kind, PatKind::Wild)
                    && self.sema.sess.diags.error_count() > errors
                    && binds.len() == nbinds
                {
                    for a in args.iter().flatten() {
                        self.bind_unchecked(&a.pat, ctx, binds);
                    }
                }
                r
            }
        }
    }

    /// True if `name` refers to a visible constant value (§9.2 "bare names always bind").
    fn names_constant(&mut self, name: Symbol) -> bool {
        match self.lookup_local(name) {
            Some(LocalBinding::Untyped(_)) => return true,
            Some(_) => return false,
            None => {}
        }
        let module = self.module;
        let Some(d) = self.sema.lookup_module_name(module, name) else { return false };
        match self.sema.resolve_decl(d) {
            DeclValue::Untyped(_) => true,
            DeclValue::Const { ty, .. } => {
                ty.is_integer() || ty.is_bool() || ty == Ty::string() || self.sema.union_variants(ty).is_some()
            }
            _ => false,
        }
    }

    fn literal_pattern(&mut self, e: &ast::Expr, ty: Ty, span: Span) -> Pat {
        match &e.kind {
            ExprKind::Str(s) => {
                if ty != Ty::string() {
                    self.error(span, format!("a string pattern needs a `string`, but this is `{}`", ty));
                }
                let v = Expr { ty, kind: tir::ExprKind::Str(Rc::from(&s.bytes[..])), span };
                pat(ty, PatKind::Const(Box::new(v)), span)
            }
            ExprKind::Bool(b) => {
                if !ty.is_bool() {
                    self.error(span, format!("a boolean pattern needs a `bool`, but this is `{}`", ty));
                }
                let v = Expr { ty, kind: tir::ExprKind::Bool(*b), span };
                pat(ty, PatKind::Const(Box::new(v)), span)
            }
            ExprKind::Float(_) => {
                self.error(span, "float literal patterns are not allowed; compare with a guard");
                pat(ty, PatKind::Wild, span)
            }
            ExprKind::Unary { operand, .. } if matches!(operand.kind, ExprKind::Float(_)) => {
                self.error(span, "float literal patterns are not allowed; compare with a guard");
                pat(ty, PatKind::Wild, span)
            }
            _ => {
                if !self.integer_pattern_ty(ty) {
                    self.error(span, format!("an integer pattern needs an integer, but this is `{}`", ty));
                    return pat(ty, PatKind::Wild, span);
                }
                match self.pattern_int(e, ty) {
                    Some(v) => pat(ty, PatKind::Const(Box::new(Expr { ty, kind: tir::ExprKind::Int(v), span })), span),
                    None => pat(ty, PatKind::Wild, span),
                }
            }
        }
    }

    /// The value of an integer pattern bound: a literal, a negated literal or a
    /// constant; it must fit `ty`.
    fn pattern_int(&mut self, e: &ast::Expr, ty: Ty) -> Option<BigInt> {
        let v = match &e.kind {
            ExprKind::Int(l) => BigInt::from(l.value.clone()),
            ExprKind::Unary { op: ast::UnOp::Neg, operand } => -self.pattern_int_raw(operand)?,
            _ => match self.try_untyped(e) {
                Some(Ok(Untyped::Int(i))) => i,
                // A typed constant of another module, as a range bound.
                _ if matches!(&e.kind, ExprKind::Field { base, .. } if self.module_of(base).is_some()) => {
                    match self.const_pattern_value(e, ty)?.kind {
                        tir::ExprKind::Int(v) => v,
                        _ => return None,
                    }
                }
                _ => {
                    self.error(e.span, "expected an integer literal or constant here");
                    return None;
                }
            },
        };
        // A literal's type is checked once it is known (when patterns are finalized).
        if !ty.is_infer() && !super::finalize::int_fits(&v, ty, &self.sema.target) {
            self.error(e.span, format!("{} does not fit in `{}`", v, ty));
            return None;
        }
        Some(v)
    }

    /// True if integer patterns can match values of type `ty`: an integer type, or the
    /// type of an integer literal whose type is not known yet, which is then restricted
    /// to integers.
    fn integer_pattern_ty(&mut self, ty: Ty) -> bool {
        ty.is_integer()
            || (self.infer.var_kind(ty) == Some(super::infer::VarKind::Int) && self.infer.require_integer(ty))
    }

    /// The value of the constant `e` (such as `m.MAX`) as a literal of type `ty`, for a
    /// pattern: an integer, `bool` or string.
    fn const_pattern_value(&mut self, e: &ast::Expr, ty: Ty) -> Option<Expr> {
        let v = self.expr(e, Some(ty));
        if v.ty.is_error() {
            return None;
        }
        // An untyped constant is already a literal.
        if let tir::ExprKind::Int(i) = &v.kind
            && ty.is_integer()
        {
            if !super::finalize::int_fits(i, ty, &self.sema.target) {
                self.error(e.span, format!("{} does not fit in `{}`", i, ty));
                return None;
            }
            return Some(Expr { ty, kind: tir::ExprKind::Int(i.clone()), span: e.span });
        }
        let v = self.coerce(v, ty);
        if !(ty.is_integer() || ty.is_bool() || ty == Ty::string()) {
            self.error(
                e.span,
                format!("a constant pattern needs an integer, `bool` or string, not `{}`; compare with a guard", ty),
            );
            return None;
        }
        let Some(c) = self.sema.eval_tir_const(&v) else {
            self.error(e.span, "expected a constant here");
            return None;
        };
        let kind = match self.sema.const_key(&c, e.span)? {
            crate::types::ConstKey::Str(s) => tir::ExprKind::Str(Rc::from(&s[..])),
            crate::types::ConstKey::Bytes(_, b) if ty.is_bool() => tir::ExprKind::Bool(b.first() == Some(&1)),
            crate::types::ConstKey::Bytes(_, b) => {
                let v = if ty.is_signed_integer() {
                    BigInt::from_signed_bytes_le(&b)
                } else {
                    BigInt::from_bytes_le(num_bigint::Sign::Plus, &b)
                };
                tir::ExprKind::Int(v)
            }
            _ => return None,
        };
        Some(Expr { ty, kind, span: e.span })
    }

    fn pattern_int_raw(&mut self, e: &ast::Expr) -> Option<BigInt> {
        match &e.kind {
            ExprKind::Int(l) => Some(BigInt::from(l.value.clone())),
            _ => {
                self.error(e.span, "expected an integer literal");
                None
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn sub_patterns(
        &mut self,
        args: &[ast::PatArg],
        elems: &[Ty],
        names: Option<&[Symbol]>,
        ctx: PatCtx,
        binds: &mut Vec<(Symbol, tir::LocalId, Span)>,
        span: Span,
        partial: bool,
    ) -> Vec<(u32, Pat)> {
        let mut out = vec![];
        let mut next = 0;
        for a in args {
            let idx = match a.name {
                Some(n) => match names.and_then(|ns| ns.iter().position(|x| *x == n.name)) {
                    Some(i) => i,
                    None => {
                        self.error(n.span, format!("no element named `{}`", n.name));
                        continue;
                    }
                },
                None => {
                    let i = next;
                    next += 1;
                    if i >= elems.len() {
                        self.error(a.span, "too many sub-patterns");
                        continue;
                    }
                    i
                }
            };
            let p = self.pattern(&a.pat, elems[idx], ctx, binds);
            out.push((idx as u32, p));
        }
        if !partial && out.len() < elems.len() && args.iter().all(|a| a.name.is_none()) {
            self.error(span, format!("this pattern has {} elements, but the value has {}", out.len(), elems.len()));
        }
        out
    }

    /// `S(f: p, …)` against a value of type `ty`, where `t` is the struct type named
    /// by `path` (if it names one).
    #[allow(clippy::too_many_arguments)]
    fn struct_pattern(
        &mut self,
        path: &ast::Expr,
        t: Option<Ty>,
        args: Option<&[ast::PatArg]>,
        ty: Ty,
        ctx: PatCtx,
        binds: &mut Vec<(Symbol, tir::LocalId, Span)>,
        span: Span,
    ) -> Pat {
        let Some(t) = t else {
            self.error(path.span, "expected a struct type");
            return pat(ty, PatKind::Wild, span);
        };
        if t != ty {
            self.error(span, format!("this pattern is for `{}`, but the value is `{}`", t, ty));
            return pat(ty, PatKind::Wild, span);
        }
        let Some(fields) = self.sema.struct_fields(t).map(|f| f.to_vec()) else {
            self.error(path.span, format!("`{}` is not a struct type", t));
            return pat(ty, PatKind::Wild, span);
        };
        if let Some(f) = self.hidden_field(t) {
            self.error(
                span,
                format!(
                    "`{}` cannot be matched with a struct pattern outside its module: its field `{}` is private (§3.3)",
                    t, f
                ),
            );
            return pat(ty, PatKind::Wild, span);
        }
        let names: Vec<Symbol> = fields.iter().map(|f| f.name).collect();
        let tys: Vec<Ty> = fields.iter().map(|f| f.ty).collect();
        // A bare name `x` in a struct pattern is shorthand for `x: x`.
        let args: Vec<ast::PatArg> = args
            .unwrap_or(&[])
            .iter()
            .map(|a| match (&a.name, &a.pat.kind) {
                (None, PatternKind::Bind(n)) => ast::PatArg { name: Some(*n), pat: a.pat.clone(), span: a.span },
                _ => a.clone(),
            })
            .collect();
        if let Some(a) = args.iter().find(|a| a.name.is_none()) {
            self.error(a.span, "struct patterns name their fields: `S(field: pattern)`");
        }
        let fs = self.sub_patterns(&args, &tys, Some(&names), ctx, binds, span, true);
        pat(ty, PatKind::Fields(fs), span)
    }

    fn path_pattern(
        &mut self,
        path: &ast::Expr,
        args: Option<&[ast::PatArg]>,
        ty: Ty,
        ctx: PatCtx,
        binds: &mut Vec<(Symbol, tir::LocalId, Span)>,
        span: Span,
    ) -> Pat {
        // `.V` / `.V(…)` — a variant of the scrutinee's union type.
        let (union_ty, vname) = match &path.kind {
            ExprKind::DotIdent(n) => (Some(ty), *n),
            ExprKind::Field { base, name } => match self.type_of_name(base) {
                Some(t) => {
                    if t != ty {
                        self.error(span, format!("this pattern is for `{}`, but the value is `{}`", t, ty));
                        return pat(ty, PatKind::Wild, span);
                    }
                    (Some(t), *name)
                }
                // `m.CONST` — a constant of another module (§9.2).
                None if args.is_none() && self.module_of(base).is_some() => {
                    return match self.const_pattern_value(path, ty) {
                        Some(v) => pat(ty, PatKind::Const(Box::new(v)), span),
                        None => pat(ty, PatKind::Wild, span),
                    };
                }
                // `m.S(f: p, …)` — a struct type of another module.
                None if args.is_some()
                    && let Some(t) = self.type_of_name(path)
                    && self.sema.struct_fields(t).is_some() =>
                {
                    return self.struct_pattern(path, Some(t), args, ty, ctx, binds, span);
                }
                None => {
                    self.error(path.span, "expected a union type or a module before `.` in a pattern");
                    return pat(ty, PatKind::Wild, span);
                }
            },
            ExprKind::Ident(_) | ExprKind::Call { .. } => {
                // `S(f: p, …)` or `G(T)(f: p, …)` — a struct pattern.
                let t = match &path.kind {
                    ExprKind::Ident(_) => self.type_of_name(path),
                    _ => Some(self.resolve_type(path)).filter(|t| !t.is_error()),
                };
                return self.struct_pattern(path, t, args, ty, ctx, binds, span);
            }
            _ => {
                self.error(path.span, "expected a variant, constant or struct pattern");
                return pat(ty, PatKind::Wild, span);
            }
        };
        let u = union_ty.unwrap();
        let Some((variants, _)) = self.sema.union_variants(u) else {
            if self.is_c_union(u) {
                self.error(span, super::matching::c_union_match(u));
                return pat(ty, PatKind::Wild, span);
            }
            let mut d = crate::diag::Diagnostic::error(
                span,
                format!("`.{}` matches a union variant, but this is `{}`", vname.name, u),
            );
            // Results and optionals have no variants; their patterns unwrap (§9.3).
            match vname.name.as_str() {
                "Ok" | "Err" => {
                    d = d.with_note(
                        "a result is not a union: match its error with `!e` (or `!.Variant`) and its \
                         success value with a plain pattern, as in `!e => …` and `v => …` (§9.3)",
                    )
                }
                "Some" | "None" => {
                    d = d.with_note(
                        "an optional is not a union: match absence with `null` and the present value \
                         with a plain pattern, as in `null => …` and `v => …` (§9.3)",
                    )
                }
                _ => {}
            }
            self.sema.emit(d);
            return pat(ty, PatKind::Wild, span);
        };
        let Some(idx) = variants.iter().position(|v| v.name == vname.name) else {
            self.error(vname.span, format!("`{}` has no variant `{}`", u, vname.name));
            return pat(ty, PatKind::Wild, span);
        };
        let payload_ty = variants[idx].payload;
        let payload = match (payload_ty, args) {
            (None, None) => None,
            (None, Some(_)) => {
                self.error(span, format!("variant `{}` has no payload", vname.name));
                None
            }
            (Some(_), None) => {
                if ctx != PatCtx::IsTest {
                    let msg =
                        format!("variant `{}` has a payload; write `.{}(_)` to ignore it", vname.name, vname.name);
                    let end = Span { lo: span.hi, ..span };
                    self.sema.emit(Diagnostic::error(span, msg).with_fix("match any payload with `(_)`", end, "(_)"));
                }
                None
            }
            (Some(pt), Some(args)) => {
                let one = args.len() == 1 && args[0].name.is_none();
                let p = match pt.kind() {
                    // `.Rect(_)` ignores the whole payload.
                    _ if one && matches!(args[0].pat.kind, PatternKind::Wildcard) => pat(pt, PatKind::Wild, span),
                    // One sub-pattern for a payload that is not a 1-tuple matches the whole
                    // tuple: `.Rect(r)` gives `r.w` and `r.h` (§9.2).
                    TyKind::Tuple { elems, .. } if one && elems.len() != 1 => {
                        self.pattern(&args[0].pat, pt, ctx, binds)
                    }
                    // A tuple payload's elements are written directly: `.Rect(w, h)` (§9.2).
                    TyKind::Tuple { elems, names } => {
                        let fs = self.sub_patterns(args, elems, names.as_deref(), ctx, binds, span, false);
                        pat(pt, PatKind::Fields(fs), span)
                    }
                    _ => {
                        if !one {
                            self.error(
                                span,
                                format!("variant `{}` has a single payload; use one sub-pattern", vname.name),
                            );
                        }
                        match args.first() {
                            Some(a) => self.pattern(&a.pat, pt, ctx, binds),
                            None => pat(pt, PatKind::Wild, span),
                        }
                    }
                };
                Some(Box::new(p))
            }
        };
        pat(ty, PatKind::Variant { variant: idx as u32, payload }, span)
    }

    /// Declares the names bound by a pattern in the current scope.
    pub fn declare_binds(&mut self, binds: Vec<(Symbol, tir::LocalId, Span)>) {
        for (name, id, span) in binds {
            self.declare(name, LocalBinding::Var(id), span);
        }
    }
}
