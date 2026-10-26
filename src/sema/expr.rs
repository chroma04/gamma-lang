//! Checking expressions (§7): literals, names, operators, coercions and
//! control-flow expressions.

use super::DeclValue;
use super::func::{FnCx, LabelValue, LocalBinding, Obligation, PendingCoercion, error_expr, unit};
use super::infer::{Fallback, VarKind};
use super::ty_expr::ArrayLen;
use super::untyped::Untyped;
use crate::diag::Diagnostic;
use crate::source::Span;
use crate::syntax::ast::{self, Expr as Expr0, ExprKind};
use crate::tir::{self, Coercion, Expr, Target};
use crate::types::{CallConv, FnTy, Ty, TyKind};
use num_bigint::BigInt;
use std::rc::Rc;

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

impl<'a, 's> FnCx<'a, 's> {
    /// Checks an expression whose value is used. `expect` is a hint from the context
    /// (the caller still coerces to it where the context is a coercion site).
    pub fn expr(&mut self, e: &ast::Expr, expect: Option<Ty>) -> Expr {
        let span = e.span;
        // `3 + 4j`: an untyped complex constant expression is evaluated exactly (§6.2).
        if matches!(e.kind, ExprKind::Binary { .. } | ExprKind::Unary { op: ast::UnOp::Neg, .. })
            && self.mentions_complex_constant(e)
            && let Some(v) = self.try_untyped(e)
        {
            return match v {
                Ok(v) => self.untyped_literal(&v, span),
                Err(()) => error_expr(span),
            };
        }
        match &e.kind {
            ExprKind::Int(l) => {
                let ty = match l.suffix {
                    Some(s) => suffix_type(s),
                    None => self.literal_var(VarKind::Int),
                };
                mk(ty, tir::ExprKind::Int(BigInt::from(l.value.clone())), span)
            }
            ExprKind::Float(l) => self.float_literal(l, span),
            ExprKind::Str(s) => match s.kind {
                crate::syntax::token::StrKind::C => {
                    mk(Ty::many_ptr(true, Ty::u8()), tir::ExprKind::CStr(s.bytes.clone().into()), span)
                }
                _ => mk(Ty::string(), tir::ExprKind::Str(s.bytes.clone().into()), span),
            },
            ExprKind::Bool(b) => mk(Ty::bool(), tir::ExprKind::Bool(*b), span),
            ExprKind::Null => self.null_value(expect, span),
            ExprKind::Uninit => {
                self.error(span, "`---` may only initialize a variable, field or argument");
                error_expr(span)
            }
            ExprKind::Underscore => {
                self.error(span, "`_` cannot be used as a value");
                error_expr(span)
            }
            ExprKind::Ident(id) => self.name(id.name, expect, span),
            ExprKind::Paren(inner) => self.expr(inner, expect),
            ExprKind::Elements(inner) => {
                let v = self.expr(inner, None);
                self.elements_tuple(v, inner.span).unwrap_or_else(|| error_expr(span))
            }
            ExprKind::Tuple(elems) => self.tuple(elems, expect, span),
            ExprKind::Unary { op, operand } => self.unary(*op, operand, expect, span),
            ExprKind::Binary { op: op @ (ast::BinOp::Range | ast::BinOp::RangeIncl), lhs, rhs } => {
                self.range_value(*op == ast::BinOp::RangeIncl, lhs, rhs, expect, span)
            }
            // `()` is a type only next to another type: `r is ()` compares values.
            ExprKind::Binary { op: op @ (ast::BinOp::Is | ast::BinOp::IsNot), lhs, rhs }
                if (!is_unit_tuple(lhs) && self.denotes_type(lhs))
                    || (!is_unit_tuple(rhs) && self.denotes_type(rhs)) =>
            {
                self.type_is(*op == ast::BinOp::IsNot, lhs, rhs, span)
            }
            ExprKind::Binary { op, lhs, rhs } => {
                let l = self.expr(lhs, None);
                self.binary_op(*op, l, rhs, span)
            }
            ExprKind::Deref(p) => self.deref_expr(p, span),
            ExprKind::Index { base, index } => self.index(base, index, span),
            ExprKind::Slice { base, lo, hi, inclusive } => {
                self.slice_expr(base, lo.as_deref(), hi.as_deref(), *inclusive, span)
            }
            ExprKind::Field { base, name } => self.field(base, *name, span),
            ExprKind::TupleField { base, index, .. } => self.tuple_field(base, *index, span),
            ExprKind::Call { callee, args, .. } => self.call(callee, args, expect, span),
            ExprKind::If(i) => self.if_expr(i, true, expect),
            ExprKind::Match(m) => self.match_expr(m, true, expect),
            ExprKind::Fn(lit) => self.fn_literal(lit, expect, None, span),
            ExprKind::Handler(lit) => self.handler_literal(lit, None, span),
            ExprKind::With(w) => self.with_expr(w, true, expect),
            ExprKind::OptUnwrap(inner) => self.opt_unwrap(inner, span),
            ExprKind::ResUnwrap(inner) => self.res_unwrap(inner, span),
            ExprKind::Bang { lhs, rhs } => self.bang_call(lhs, rhs, span),
            ExprKind::Else { lhs, capture, rhs } => self.else_expr(lhs, *capture, rhs, expect, span),
            ExprKind::Block(b) => self.block_expected(b, true, expect),
            ExprKind::Return(v) => self.return_expr(v.as_deref(), span),
            ExprKind::Break { label, value } => self.break_expr(*label, value.as_deref(), span),
            ExprKind::Continue { label } => self.continue_expr(*label, span),
            ExprKind::Resume { cont, value } => self.resume_expr(cont, value.as_deref(), span),
            // A `for` loop's value is `()` (§8.4).
            ExprKind::For(f) => self.for_loop(f),
            ExprKind::Intrinsic { name, args } => self.intrinsic(*name, args.as_deref().unwrap_or(&[]), span),
            ExprKind::DotIdent(name) => match expect.map(|t| self.infer.resolve(t)) {
                Some(t) => self.construct_variant(super::call::strip_wrappers(t), *name, None, span),
                None => {
                    self.error(span, format!("cannot infer the union type of `.{}` here", name.name));
                    error_expr(span)
                }
            },
            // `[2]P(…)[1].y`: the prefix and the construction come first (§A.7 rule 12).
            ExprKind::ArrayType { .. } | ExprKind::OptionalType(_) if let Some(r) = hoist_prefix(e) => {
                self.expr(&r, expect)
            }
            // A type prefix before a construction applies to the whole type (§A.7 rule 12).
            ExprKind::ArrayType { len, elem } if construction_call(elem).is_some() => {
                self.array_type_construction(e, len.as_deref(), elem, span)
            }
            // `#simd [4]f32(…)` or `#vec [3]f32(…)`: constructing a vector (§5.6.2) or a
            // math vector (§5.6.3).
            ExprKind::Attributed { attrs, inner }
                if matches!(inner.kind, ExprKind::ArrayType { .. }) && construction_call(inner).is_some() =>
            {
                self.vector_construction(attrs, inner, span)
            }
            ExprKind::OptionalType(inner) if construction_call(inner).is_some() => {
                let Some(ExprKind::Call { args, .. }) = construction_call(inner).map(|c| &c.kind) else {
                    unreachable!()
                };
                let t = self.resolve_type(&without_call(inner));
                self.construct_optional(Ty::optional(t), t, args, span)
            }
            ExprKind::OptionalType(_)
            | ExprKind::PtrType { .. }
            | ExprKind::SliceType { .. }
            | ExprKind::ManyPtrType { .. }
            | ExprKind::ArrayType { .. }
            | ExprKind::ErrorUnionType { .. }
            | ExprKind::FnType(_)
            | ExprKind::ContType(_)
            | ExprKind::Struct(_)
            | ExprKind::Union(_)
            | ExprKind::Effect(_) => {
                self.error(span, "expected a value, found a type");
                error_expr(span)
            }
            _ => {
                self.error(span, "this kind of expression is not supported yet");
                error_expr(span)
            }
        }
    }

    /// A float literal: of its suffix's type, or of a type still to be inferred.
    fn float_literal(&mut self, l: &crate::syntax::token::FloatLit, span: Span) -> Expr {
        if l.imag {
            return self.imaginary_literal(l, span);
        }
        let ty = match l.suffix {
            Some(s) => {
                let fty = match s {
                    crate::syntax::token::FloatSuffix::F16 => crate::types::FloatTy::F16,
                    crate::syntax::token::FloatSuffix::F32 => crate::types::FloatTy::F32,
                    crate::syntax::token::FloatSuffix::F64 => crate::types::FloatTy::F64,
                };
                Ty::intern(TyKind::Float(fty))
            }
            None => self.literal_var(VarKind::Float),
        };
        mk(ty, tir::ExprKind::UntypedFloat(Rc::new(l.value.clone())), span)
    }

    /// `#simd [4]f32(…)` or `#vec [3]f32(…)`: constructing a vector (§5.6.2) or a math
    /// vector (§5.6.3).
    fn vector_construction(&mut self, attrs: &[ast::Attribute], inner: &ast::Expr, span: Span) -> Expr {
        let Some(ExprKind::Call { args, .. }) = construction_call(inner).map(|c| &c.kind) else { unreachable!() };
        let module = self.module;
        if attrs.iter().any(|a| a.name.name.as_str() == "soa") {
            let t = self.sema.soa_type_expr(module, &without_call(inner), span);
            return match self.sema.soa_info(t) {
                Some(info) => self.soa_construct(t, info, args, span),
                None => error_expr(span),
            };
        }
        let t = self.sema.vector_type(module, attrs, &without_call(inner), span);
        match t.vector_parts().or(t.math_vector_parts()) {
            Some((n, elem)) => self.construct_array(Some(t), Some(n), elem, args, span),
            None => error_expr(span),
        }
    }

    /// `null`, typed by the optional (or `E!?T`) it is expected to be.
    fn null_value(&mut self, expect: Option<Ty>, span: Span) -> Expr {
        match expect.map(|t| self.infer.shallow(t)) {
            Some(t) if matches!(t.kind(), TyKind::Optional(_)) => mk(t, tir::ExprKind::Null, span),
            // `null` where an `E!?T` is expected is the success value `null` (§10.3).
            Some(t) if matches!(t.kind(), TyKind::Result { ok, .. } if matches!(ok.kind(), TyKind::Optional(_))) => {
                let TyKind::Result { ok, .. } = t.kind() else { unreachable!() };
                mk(*ok, tir::ExprKind::Null, span)
            }
            _ => {
                self.error(span, "cannot infer the type of `null` here; annotate the optional type");
                error_expr(span)
            }
        }
    }

    /// `p^`.
    fn deref_expr(&mut self, p: &ast::Expr, span: Span) -> Expr {
        let p = self.expr(p, None);
        match self.infer.shallow(p.ty).kind() {
            TyKind::Ptr { pointee, .. } => mk(*pointee, tir::ExprKind::Deref(Box::new(p)), span),
            TyKind::Error => error_expr(span),
            _ => {
                let t = self.infer.resolve(p.ty);
                self.error(span, format!("`^` dereferences a pointer `^T`, but this is `{}`", t));
                error_expr(span)
            }
        }
    }

    /// `t.0`: an element of a tuple.
    fn tuple_field(&mut self, base: &ast::Expr, index: u32, span: Span) -> Expr {
        let b = self.expr(base, None);
        let bt = self.infer.resolve(b.ty);
        let b = self.auto_deref(b);
        match self.infer.resolve(b.ty).kind() {
            TyKind::Tuple { elems, .. } if (index as usize) < elems.len() => {
                mk(elems[index as usize], tir::ExprKind::Field { base: Box::new(b), index }, span)
            }
            TyKind::Error => error_expr(span),
            _ => {
                self.error(span, format!("type `{}` has no element {}", bt, index));
                error_expr(span)
            }
        }
    }

    /// `x?`: unwraps an optional.
    fn opt_unwrap(&mut self, inner: &ast::Expr, span: Span) -> Expr {
        let v = self.expr(inner, None);
        match self.infer.resolve(v.ty).kind() {
            TyKind::Optional(t) => mk(*t, tir::ExprKind::Unwrap { expr: Box::new(v), is_result: false }, span),
            TyKind::Error => error_expr(span),
            _ => {
                let t = self.infer.resolve(v.ty);
                self.error(span, format!("`?` unwraps an optional, but this is `{}`", t));
                error_expr(span)
            }
        }
    }

    /// `x!(args)`: unwrap, then call (§A.7 rule 3).
    fn bang_call(&mut self, lhs: &ast::Expr, rhs: &ast::Expr, span: Span) -> Expr {
        if self.type_of_name(lhs).is_some() {
            self.error(span, "expected a value, found the error-union type `E!T`");
            return error_expr(span);
        }
        let callee = self.res_unwrap(lhs, lhs.span.to(span.shrink_to_lo()));
        let args: Vec<ast::Arg> = match &rhs.kind {
            ExprKind::Paren(a) => {
                vec![ast::Arg { kind: ast::ArgKind::Positional((**a).clone()), span: a.span }]
            }
            ExprKind::Tuple(elems) => elems
                .iter()
                .map(|el| ast::Arg {
                    kind: match el.name {
                        Some(n) => ast::ArgKind::Named(n, el.value.clone()),
                        None => ast::ArgKind::Positional(el.value.clone()),
                    },
                    span: el.value.span,
                })
                .collect(),
            _ => {
                self.error(rhs.span, "this is a type, but `!` follows a value; to unwrap twice write `(x!)?`");
                return error_expr(span);
            }
        };
        self.call_value(callee, &args, span)
    }

    /// `[N]T(…)` and `[N][M]T(…)`: an array type prefix applied to a construction `e` (§A.7 rule 12).
    fn array_type_construction(
        &mut self,
        e: &ast::Expr,
        len: Option<&ast::Expr>,
        elem: &ast::Expr,
        span: Span,
    ) -> Expr {
        let Some(ExprKind::Call { args, .. }) = construction_call(elem).map(|c| &c.kind) else { unreachable!() };
        // `[2][3]i32(…)`: the element type is everything after the outer prefix.
        if let Some(l) = len
            && self.sema.length_is_type(self.module, l)
        {
            let t = self.resolve_type(&without_call(e));
            return match t.kind() {
                TyKind::Array { elem, kind: crate::types::ArrayKind::Enum(index), .. } => {
                    self.construct_enum_array(t, *index, *elem, args, span)
                }
                _ => error_expr(span),
            };
        }
        let elem_ty = self.resolve_type(&without_call(elem));
        let len = match len {
            Some(l) => match self.sema.array_len(self.module, l) {
                Some(ArrayLen::Known(n)) => Some(n),
                Some(ArrayLen::Param(len)) => {
                    let t = Ty::intern(TyKind::ParamArray { len, elem: elem_ty });
                    return self.construct_param_array(t, elem_ty, args, span);
                }
                None => return error_expr(span),
            },
            None => None,
        };
        self.construct_array(None, len, elem_ty, args, span)
    }

    /// A name used as a value.
    fn name(&mut self, name: crate::symbol::Symbol, expect: Option<Ty>, span: Span) -> Expr {
        if let Some((b, depth)) = self.lookup_local_at(name) {
            if let LocalBinding::Var(id) = b {
                let decl = self.local_at(depth, id).span;
                self.sema.local_uses.insert((decl, span));
            }
            return match b {
                LocalBinding::Var(id) if depth < self.depth() => {
                    // A local of an enclosing function, captured by reference (§11.6).
                    self.note_capture(depth);
                    let ty = self.local_at(depth, id).ty;
                    mk(ty, tir::ExprKind::Upvar { levels: self.depth() - depth, local: id }, span)
                }
                LocalBinding::Var(id) => mk(self.local_ty(id), tir::ExprKind::Local(id), span),
                LocalBinding::NestedFn(f) if self.open_fns.contains(&f) => {
                    self.recursive_without_ret(name, span);
                    error_expr(span)
                }
                LocalBinding::NestedFn(f) => self.named_fn_value(f, name, expect, span),
                LocalBinding::Untyped(v) => self.untyped_literal(&v, span),
                LocalBinding::Const { ty, id } => mk(ty, tir::ExprKind::Const(id), span),
                LocalBinding::Label => {
                    self.error(span, format!("`{}` is a label, not a value", name));
                    error_expr(span)
                }
                LocalBinding::Type(t) if t.is_error() => error_expr(span),
                LocalBinding::Type(t) => {
                    self.error(span, format!("expected a value, found the type `{}`", t));
                    error_expr(span)
                }
            };
        }
        if let Some(&(_, pid)) = self.sema.value_scope.iter().rev().find(|(n, _)| *n == name) {
            return mk(self.sema.value_params[&pid], tir::ExprKind::ValueParam(pid), span);
        }
        // A constant of the body whose compile-time expression this is (§17.2).
        if let Some((_, b)) = self.sema.local_consts.iter().rev().find(|(n, _)| *n == name) {
            match b.clone() {
                LocalBinding::Untyped(v) => return self.untyped_literal(&v, span),
                LocalBinding::Const { ty, id } => return mk(ty, tir::ExprKind::Const(id), span),
                LocalBinding::Var(_) => {
                    let d = crate::diag::Diagnostic::error(
                        span,
                        format!("`{}` is a variable, so its value is not known at compile time", name),
                    )
                    .with_note("declare it with `::` to make it a compile-time constant (§17.2)");
                    self.sema.emit(d);
                    return error_expr(span);
                }
                _ => {}
            }
        }
        // A nested function of the body whose compile-time argument this is.
        if let Some(&(_, f)) = self.sema.local_fns.iter().rev().find(|(n, _)| *n == name) {
            return self.fn_ref(f, span);
        }
        let module = self.module;
        if let Some(d) = self.sema.lookup_module_name(module, name) {
            return self.decl_expr(d, name, expect, span);
        }
        let effect_param = self.sema.type_params.iter().rev().find(|(n, _)| *n == name).is_some_and(
            |(_, t)| matches!(t.kind(), crate::types::TyKind::Param(p) if self.sema.effect_params.contains(p)),
        );
        if effect_param {
            self.error(span, format!("expected a value, found the effect set `{}`", name));
        } else if super::builtins::builtin_type(name.as_str()).is_some() {
            self.error(span, format!("expected a value, found the type `{}`", name));
        } else if super::builtins::builtin_fn(name.as_str()).is_some() {
            self.error(span, format!("the builtin function `{}` can only be called", name));
        } else {
            let mut d = crate::diag::Diagnostic::error(span, format!("unknown name `{}`", name));
            if let Some(effect) = self.sema.op_effect(module, name) {
                d = crate::diag::Diagnostic::error(span, super::Sema::op_hint(effect, name, None)).with_fix(
                    format!("write `{effect}.{name}`"),
                    Span { hi: span.lo, ..span },
                    format!("{effect}."),
                );
            } else if let Some(hint) = self.sema.universe_hint(name) {
                d = d.with_note(hint);
                if let Some(edits) = self.sema.import_fix(module, name, span) {
                    d = d.with_fix_edits(format!("write `{}`", edits.last().unwrap().text), edits);
                }
            }
            self.sema.emit(d);
        }
        error_expr(span)
    }

    /// The value of a top-level declaration named `name`.
    pub fn decl_expr(&mut self, d: super::DeclId, name: crate::symbol::Symbol, expect: Option<Ty>, span: Span) -> Expr {
        self.sema.check_deprecated(d, span);
        match self.sema.resolve_decl(d) {
            DeclValue::Fn(f) => self.named_fn_value(f, name, expect, span),
            DeclValue::Const { ty, id } => mk(ty, tir::ExprKind::Const(id), span),
            DeclValue::Untyped(v) => self.untyped_literal(&v, span),
            DeclValue::Type(_) | DeclValue::GenericType(_) => {
                self.error(span, format!("expected a value, found the type `{}`", name));
                error_expr(span)
            }
            DeclValue::Effect(_) | DeclValue::EffectInst(_) => {
                self.error(span, format!("expected a value, found the effect `{}`", name));
                error_expr(span)
            }
            DeclValue::EffectSet(_) => {
                self.error(span, format!("expected a value, found the effect set `{}`", name));
                error_expr(span)
            }
            DeclValue::Op(..) => {
                self.error(span, format!("the operation `{}` can only be called", name));
                error_expr(span)
            }
            DeclValue::Module(_) => {
                self.error(span, format!("`{}` is a module, which is not a value (§3.2)", name));
                error_expr(span)
            }
            // Every access to a `#global` performs `Sys` (§19.3).
            DeclValue::Global { ty, id } => {
                let sys = self.sema.sys_set();
                self.record_effects(&sys, span);
                mk(ty, tir::ExprKind::Global(id), span)
            }
            DeclValue::Error => error_expr(span),
        }
    }

    /// The module a module-binding expression (`m` in `m.x`) names, unless a
    /// local shadows it.
    pub fn module_of(&mut self, e: &ast::Expr) -> Option<super::ModuleId> {
        // `m.n` where `n` is a public module binding of module `m`.
        if let ExprKind::Field { base, name } = &e.kind {
            // Remembering failures keeps a long field chain from being walked again at
            // every link. Synthetic nodes share one id and are not remembered.
            let key = e.id;
            if self.not_module.contains(&key) {
                return None;
            }
            let from = self.module;
            let found = self.module_of(base).and_then(|m| self.sema.public_module_member(m, name.name, from));
            if found.is_none() && key.0 != u32::MAX {
                self.not_module.insert(key);
            }
            return found;
        }
        let ExprKind::Ident(id) = &e.kind else { return None };
        if self.lookup_local(id.name).is_some() {
            return None;
        }
        let module = self.module;
        let d = self.sema.lookup_module_name(module, id.name)?;
        match self.sema.resolve_decl(d) {
            DeclValue::Module(m) => Some(m),
            _ => None,
        }
    }

    /// For `m.x` with `m` a module: `Some(x's declaration)`, or `Some(None)` if
    /// `x` is missing or private (reported).
    pub fn qualified_decl(&mut self, e: &ast::Expr) -> Option<Option<super::DeclId>> {
        let ExprKind::Field { base, name } = &e.kind else { return None };
        let m = self.module_of(base)?;
        let from = self.module;
        Some(self.sema.module_member(m, *name, from))
    }

    /// The named function `f` used as a value. A generic one needs its type arguments:
    /// written (`f(u8)`) or taken from the expected function pointer type (§16.7).
    fn named_fn_value(&mut self, f: tir::FnId, name: crate::symbol::Symbol, expect: Option<Ty>, span: Span) -> Expr {
        if !self.sema.sigs[&f].generics.is_empty() {
            if let Some(e) = self.generic_fn_from_expected(f, expect, span) {
                return e;
            }
            self.error(
                span,
                format!(
                    "`{name}` is generic, so it needs its compile-time arguments to be a function value: write `{name}(…)` with only the types and `::` values, or give a function pointer type that determines them (§16.7)"
                ),
            );
            return error_expr(span);
        }
        self.fn_ref(f, span)
    }

    /// A named function used as a value: a function pointer (§5.14).
    pub fn fn_ref(&mut self, f: tir::FnId, span: Span) -> Expr {
        let sig = &self.sema.sigs[&f];
        let ty = Ty::intern(TyKind::FnPtr(Box::new(FnTy {
            params: sig.params.iter().map(|p| p.ty).collect(),
            ret: sig.ret,
            effects: sig.effects.clone(),
            variadic: false,
            callconv: self.sema.prog.fns[f.index()].callconv,
        })));
        mk(ty, tir::ExprKind::FnRef(f), span)
    }

    /// An untyped constant used as a value becomes a literal (§6.2).
    pub fn untyped_literal(&mut self, v: &Untyped, span: Span) -> Expr {
        match v {
            Untyped::Int(i) => {
                let ty = self.literal_var(VarKind::Int);
                mk(ty, tir::ExprKind::Int(i.clone()), span)
            }
            Untyped::Float(r) => {
                let ty = self.literal_var(VarKind::Float);
                mk(ty, tir::ExprKind::UntypedFloat(Rc::new(r.clone())), span)
            }
            Untyped::NegZero => {
                let zero = self.untyped_literal(&Untyped::Float(num_traits::Zero::zero()), span);
                mk(zero.ty, tir::ExprKind::Unary { op: tir::UnOp::Neg, operand: Box::new(zero) }, span)
            }
            Untyped::Bool(b) => mk(Ty::bool(), tir::ExprKind::Bool(*b), span),
            Untyped::Complex(re, im) => {
                let ty = self.literal_var(VarKind::Complex);
                mk(ty, tir::ExprKind::UntypedComplex(Rc::new((re.clone(), im.clone()))), span)
            }
        }
    }

    /// Reports a second-class value (a function or handler literal) used as `what`, which
    /// would let it outlive its captures (§11.6). True if it was reported.
    fn reject_second_class(&mut self, v: &Expr, what: &str) -> bool {
        let msg = match self.infer.resolve(v.ty).kind() {
            TyKind::Fn(_) => {
                format!("a second-class function cannot be {}; use a function pointer `^fn(…)` (§11.6)", what)
            }
            TyKind::Handler(_) => format!("a handler literal is second-class and cannot be {} (§11.6)", what),
            _ => return false,
        };
        self.error(v.span, msg);
        true
    }

    fn tuple(&mut self, elems: &[ast::TupleElem], expect: Option<Ty>, span: Span) -> Expr {
        // The expected tuple type, also through optionals and results that would wrap it.
        let mut want = expect.map(|t| self.infer.shallow(t));
        while let Some(TyKind::Optional(inner) | TyKind::Result { ok: inner, .. }) = want.map(|t| t.kind().clone()) {
            want = Some(self.infer.shallow(inner));
        }
        let expected_elems: Option<Vec<Ty>> = want.and_then(|t| match t.kind() {
            TyKind::Tuple { elems: es, .. } if es.len() == elems.len() => Some(es.to_vec()),
            _ => None,
        });
        let named = elems.first().is_some_and(|e| e.name.is_some());
        if elems.iter().any(|e| e.name.is_some() != named) {
            self.error(span, "a tuple must be either fully named or fully unnamed");
        }
        let mut vals = vec![];
        for (i, el) in elems.iter().enumerate() {
            let exp = expected_elems.as_ref().map(|v| v[i]);
            let mut v = self.expr(&el.value, exp);
            // Each element converts to the expected element type where it can (§6.4):
            // `(a: 1, b: null)` for `(a: u8, b: ?u8)`. A mismatch is left for the
            // coercion of the whole tuple to report.
            if let Some(exp) = exp.map(|t| self.infer.resolve(t))
                && !exp.has_infer()
            {
                let vt = self.infer.resolve(v.ty);
                if vt != exp && (vt.is_infer() || (!vt.has_infer() && self.coercion_steps(vt, exp).is_some())) {
                    v = self.coerce(v, exp);
                }
            }
            self.reject_second_class(&v, "a tuple element");
            vals.push(v);
        }
        let names = if named {
            let ns: Vec<_> = elems.iter().filter_map(|e| e.name.map(|n| n.name)).collect();
            for (i, n) in ns.iter().enumerate() {
                if ns[..i].contains(n) {
                    self.error(span, format!("duplicate tuple element name `{}`", n));
                }
            }
            Some(ns.into_boxed_slice())
        } else {
            None
        };
        let ty = Ty::intern(TyKind::Tuple { elems: vals.iter().map(|v| v.ty).collect(), names });
        mk(ty, tir::ExprKind::Aggregate(vals), span)
    }

    fn unary(&mut self, op: ast::UnOp, operand: &ast::Expr, expect: Option<Ty>, span: Span) -> Expr {
        match op {
            ast::UnOp::Neg => self.neg_expr(operand, expect, span),
            ast::UnOp::BitNot => self.bit_not_expr(operand, expect, span),
            ast::UnOp::Not => {
                let v = self.expr(operand, Some(Ty::bool()));
                if self.infer.resolve(v.ty).is_vector() {
                    return self.vector_unary(op, v, span);
                }
                let v = self.expect_bool(v);
                mk(Ty::bool(), tir::ExprKind::Unary { op: tir::UnOp::Not, operand: Box::new(v) }, span)
            }
            ast::UnOp::AddrOf => self.addr_of_expr(operand, expect, span),
            // `!e` where `?(E!T)` is expected: the result, then wrapped (§6.4).
            ast::UnOp::MakeErr
                if let Some(inner) = expect.map(|t| self.infer.shallow(t)).and_then(|t| t.optional_inner())
                    && matches!(self.infer.shallow(inner).kind(), TyKind::Result { .. }) =>
            {
                let r = self.unary(op, operand, Some(inner), span);
                self.coerce(r, expect.unwrap())
            }
            ast::UnOp::MakeErr => self.make_err_expr(operand, expect, span),
            ast::UnOp::Try => self.try_expr(operand, span),
        }
    }

    /// `-x`.
    fn neg_expr(&mut self, operand: &ast::Expr, expect: Option<Ty>, span: Span) -> Expr {
        // A minus applied directly to a literal is part of the literal (§7.4.1).
        let mut v = self.expr(operand, expect);
        match &mut v.kind {
            tir::ExprKind::Int(i) if is_literal(operand) || v.ty.is_infer() => {
                *i = -i.clone();
                v.span = span;
                return v;
            }
            // Not for zero: exact rationals have no negative zero, and `-0.0` must keep its sign.
            tir::ExprKind::UntypedFloat(r)
                if (is_literal(operand) || v.ty.is_infer()) && !num_traits::Zero::is_zero(&**r) =>
            {
                *r = Rc::new(-(**r).clone());
                v.span = span;
                return v;
            }
            _ => {}
        }
        if self.infer.resolve(v.ty).is_vector() {
            return self.vector_unary(ast::UnOp::Neg, v, span);
        }
        if self.is_compound_number(v.ty) {
            return self.compound_number_unary(ast::UnOp::Neg, v, span);
        }
        self.obligations.push(Obligation::Numeric { ty: v.ty, span, op: "-", int_only: false, signed_only: true });
        mk(v.ty, tir::ExprKind::Unary { op: tir::UnOp::Neg, operand: Box::new(v) }, span)
    }

    /// `~x`.
    fn bit_not_expr(&mut self, operand: &ast::Expr, expect: Option<Ty>, span: Span) -> Expr {
        let v = self.expr(operand, expect);
        let st = self.infer.resolve(v.ty);
        if st.is_vector() {
            return self.vector_unary(ast::UnOp::BitNot, v, span);
        }
        if self.is_compound_number(st) {
            return self.compound_number_unary(ast::UnOp::BitNot, v, span);
        }
        if let Some(u) = self.bits_operand(st, "~", span) {
            let x = self.bits_convert(v, u);
            let mut n = mk(u, tir::ExprKind::Unary { op: tir::UnOp::BitNot, operand: Box::new(x) }, span);
            // Padding fields `_` stay zero (§5.10.5): only the named fields are complemented.
            if let Some(mask) = self.bits_padding_mask(st) {
                let m = mk(u, tir::ExprKind::Int(mask), span);
                n = mk(u, tir::ExprKind::Binary { op: tir::BinOp::BitAnd, lhs: Box::new(n), rhs: Box::new(m) }, span);
            }
            return self.bits_convert(n, st);
        }
        self.obligations.push(Obligation::Numeric { ty: v.ty, span, op: "~", int_only: true, signed_only: false });
        mk(v.ty, tir::ExprKind::Unary { op: tir::UnOp::BitNot, operand: Box::new(v) }, span)
    }

    /// `&place`.
    fn addr_of_expr(&mut self, operand: &ast::Expr, expect: Option<Ty>, span: Span) -> Expr {
        let temp_ok = std::mem::take(&mut self.temp_ref_arg);
        // The expected pointee is a hint for the operand: `f(&.Circle(r))` (§7.2).
        let hint = expect.map(|t| self.infer.shallow(t)).and_then(|t| match t.kind() {
            TyKind::Ptr { pointee, .. } => Some(*pointee),
            _ => None,
        });
        let v = self.expr(operand, hint);
        if self.reject_second_class(&v, "the target of `&`") {
            return error_expr(span);
        }
        if self.sema.soa_gathers.contains(&v.span) {
            self.error(
                span,
                "an element of a `#soa` has no address, since its fields are stored apart (§5.6.4); take the address of a field: `&a[i].f`",
            );
            return error_expr(span);
        }
        if let tir::ExprKind::Field { base, .. } = &v.kind
            && let TyKind::Adt(id, _) = base.ty.kind()
            && matches!(self.sema.prog.adts.get(id).map(|d| &d.kind), Some(tir::AdtKind::Struct { bits: Some(_), .. }))
        {
            self.error(span, "the fields of a `#bits` struct have no address (§5.10.5)");
            return error_expr(span);
        }
        match self.place_mutability(&v) {
            Some(m) => {
                let ty = Ty::ptr(!m, v.ty);
                mk(ty, tir::ExprKind::AddrOf(Box::new(v)), span)
            }
            // `f(&g(x))` for a `^const T` parameter: a temporary that lives until
            // the call returns, as for a method's receiver (§7.10).
            None if temp_ok => mk(Ty::ptr(true, v.ty), tir::ExprKind::AddrOfTemp(Box::new(v)), span),
            None => {
                self.error(span, "`&` needs a place (a variable, `p^`, field or element), not a temporary value");
                error_expr(span)
            }
        }
    }

    /// `!e`: an error value of the expected result type.
    fn make_err_expr(&mut self, operand: &ast::Expr, expect: Option<Ty>, span: Span) -> Expr {
        match expect.map(|t| self.infer.shallow(t)) {
            Some(t) if matches!(t.kind(), TyKind::Result { .. }) => {
                let TyKind::Result { err, .. } = t.kind() else { unreachable!() };
                let err = *err;
                let v = self.expr(operand, Some(err));
                let vt = self.infer.resolve(v.ty);
                // `!e` with `e` of a type that lifts into the error union (§10.6).
                let v = if !vt.has_infer() && vt != err && self.coercion_steps(vt, err).is_none() {
                    match self.lift_variant(vt, err) {
                        Ok(i) => mk(err, tir::ExprKind::Variant { variant: i, payload: Some(Box::new(v)) }, span),
                        Err(_) => self.coerce(v, err),
                    }
                } else {
                    self.coerce(v, err)
                };
                mk(t, tir::ExprKind::MakeErr(Box::new(v)), span)
            }
            _ => {
                let hint = if expect.is_none_or(|t| t.is_bool()) {
                    " (`!e` makes an error; logical negation is `not e`)"
                } else {
                    ""
                };
                self.error(span, format!("cannot infer the result type of `!e` here{}", hint));
                error_expr(span)
            }
        }
    }

    /// Unifies the types of two operands that must be equal (§6.1, §7.4.1).
    pub fn unify_operands(&mut self, a: Ty, b: Ty, span: Span, what: &str) -> Ty {
        if self.infer.unify(a, b).is_err() {
            let (a, b) = (self.infer.resolve(a), self.infer.resolve(b));
            let mut d = Diagnostic::error(span, format!("{} have different types: `{}` and `{}`", what, a, b));
            // `a..b else c` is `(a..b) else c`: `else` binds more loosely than `..`.
            let wrapped = |t: Ty| matches!(t.kind(), TyKind::Optional(_) | TyKind::Result { .. });
            if what == "range bounds" && wrapped(a) != wrapped(b) {
                d = d.with_note("`else` binds more loosely than `..`; parenthesize the bound: `lo..(x else d)` (§B.1)");
            }
            self.sema.emit(d);
            return Ty::error();
        }
        self.infer.shallow(a)
    }

    /// True if `e` denotes a type rather than a value: a type parameter in scope, a type
    /// name or instantiation, or a type expression.
    pub(super) fn denotes_type(&mut self, e: &ast::Expr) -> bool {
        match &e.kind {
            ExprKind::Paren(inner) => self.denotes_type(inner),
            ExprKind::Ident(id) => {
                if self.lookup_local(id.name).is_some() {
                    return false;
                }
                self.sema.type_params.iter().any(|(n, _)| *n == id.name) || self.type_of_name(e).is_some()
            }
            ExprKind::Call { .. } | ExprKind::Field { .. } => self.type_of_name(e).is_some(),
            ExprKind::Intrinsic { name, .. } => name.name.as_str() == "type_of",
            _ => {
                let module = self.module;
                self.sema.looks_like_type(module, e)
            }
        }
    }

    /// `A is B` on types (§17): a compile-time `bool`, decided when a generic body is
    /// instantiated if either side mentions its parameters.
    fn type_is(&mut self, negated: bool, lhs: &ast::Expr, rhs: &ast::Expr, span: Span) -> Expr {
        if !self.denotes_type(lhs) || !self.denotes_type(rhs) {
            self.error(span, "`is` compares a type only with another type");
            return error_expr(span);
        }
        let (a, b) = (self.resolve_type(lhs), self.resolve_type(rhs));
        if a.is_error() || b.is_error() {
            return error_expr(span);
        }
        if super::generics::has_param(a) || super::generics::has_param(b) {
            return mk(Ty::bool(), tir::ExprKind::TypeIs { a, b, negated }, span);
        }
        mk(Ty::bool(), tir::ExprKind::Bool((a == b) != negated), span)
    }

    /// Checks `lhs op rhs` given the already-checked left operand.
    pub fn binary_op(&mut self, op: ast::BinOp, lhs: Expr, rhs: &ast::Expr, span: Span) -> Expr {
        use ast::BinOp as B;
        if self.infer.resolve(lhs.ty).is_vector() && !matches!(op, B::Is | B::IsNot) {
            return self.vector_binary(op, lhs, rhs, span);
        }
        // Math vectors and complex numbers (§7.4.7): the right operand may be a scalar.
        if !matches!(op, B::Is | B::IsNot | B::In | B::NotIn | B::And | B::Or | B::Range | B::RangeIncl)
            && self.is_compound_number(lhs.ty)
        {
            let r = self.expr(rhs, None);
            return self.compound_number_binary(op, lhs, r, span);
        }
        match op {
            B::And | B::Or => {
                let l = self.expect_bool(lhs);
                let r = self.expr(rhs, Some(Ty::bool()));
                let r = self.expect_bool(r);
                mk(Ty::bool(), tir::ExprKind::Logic { is_and: op == B::And, lhs: Box::new(l), rhs: Box::new(r) }, span)
            }
            B::Is | B::IsNot => {
                if let Some(p) = self.is_pattern_operand(rhs, lhs.ty) {
                    return self.is_pattern_test(lhs, &p, op == B::IsNot, span);
                }
                let r = self.expr(rhs, Some(lhs.ty));
                // `#soa` slices compare by content, column by column (§5.6.4).
                if let Some(info) = self.soa_of(&lhs).filter(|i| i.len.is_none())
                    && let Some(ri) = self.soa_of(&r).filter(|i| i.len.is_none() && i.elem == info.elem)
                {
                    let info = if info.is_const { info } else { ri };
                    return self.soa_eq(lhs, r, info, op == B::IsNot, span);
                }
                let (l, r) = self.equalize(lhs, r, span);
                mk(Ty::bool(), tir::ExprKind::Eq { lhs: Box::new(l), rhs: Box::new(r), negated: op == B::IsNot }, span)
            }
            B::Lt | B::Le | B::Gt | B::Ge => {
                let r = self.expr(rhs, Some(lhs.ty));
                let ty = self.unify_operands(lhs.ty, r.ty, span, "the operands of a comparison");
                self.obligations.push(Obligation::Ord { ty, span });
                let top = match op {
                    B::Lt => tir::BinOp::Lt,
                    B::Le => tir::BinOp::Le,
                    B::Gt => tir::BinOp::Gt,
                    _ => tir::BinOp::Ge,
                };
                mk(Ty::bool(), tir::ExprKind::Binary { op: top, lhs: Box::new(lhs), rhs: Box::new(r) }, span)
            }
            B::Shl(_) | B::Shr => {
                let r = self.expr(rhs, None);
                if !self.infer.require_unsigned(r.ty, Fallback::Usize) && !r.ty.is_error() {
                    let t = self.infer.resolve(r.ty);
                    self.error(r.span, format!("a shift amount must have an unsigned integer type, not `{}`", t));
                }
                if op == B::Shl(ast::Flavor::Opt) && self.has_optional_operand(&lhs, &lhs) {
                    return self.chained_opt_op(op, lhs, r, span);
                }
                self.obligations.push(Obligation::Numeric {
                    ty: lhs.ty,
                    span,
                    op: op.as_str(),
                    int_only: true,
                    signed_only: false,
                });
                let (top, opt) = match op {
                    B::Shl(f) => (tir::BinOp::Shl(f), f == ast::Flavor::Opt),
                    _ => (tir::BinOp::Shr, false),
                };
                let ty = if opt { Ty::optional(lhs.ty) } else { lhs.ty };
                mk(ty, tir::ExprKind::Binary { op: top, lhs: Box::new(lhs), rhs: Box::new(r) }, span)
            }
            B::Add(ast::Flavor::Checked) | B::Sub(ast::Flavor::Checked)
                if matches!(self.infer.resolve(lhs.ty).kind(), TyKind::ManyPtr { .. }) =>
            {
                self.pointer_arith(op, lhs, rhs, span)
            }
            B::Add(_) | B::Sub(_)
                if matches!(self.infer.resolve(lhs.ty).kind(), TyKind::Ptr { .. } | TyKind::Rawptr) =>
            {
                let t = self.infer.resolve(lhs.ty);
                self.expr(rhs, None);
                self.error(span, format!("`{}` has no arithmetic: only many-pointers `[^]T` do (§5.5)", t));
                error_expr(span)
            }
            B::Add(_) | B::Sub(_) | B::Mul(_) | B::Div | B::Rem | B::BitAnd | B::BitOr | B::BitXor => {
                let expect = match op {
                    B::Add(ast::Flavor::Opt) | B::Sub(ast::Flavor::Opt) | B::Mul(ast::Flavor::Opt) => {
                        self.infer.resolve(lhs.ty).optional_inner().unwrap_or(lhs.ty)
                    }
                    _ => lhs.ty,
                };
                let r = self.expr(rhs, Some(expect));
                // `2 * v`: a scalar times a math vector or a complex number (§7.4.7).
                if self.is_compound_number(r.ty) {
                    return self.compound_number_binary(op, lhs, r, span);
                }
                self.arith_op(op, lhs, r, span)
            }
            B::In | B::NotIn => self.in_expr(lhs, rhs, op == B::NotIn, span),
            B::Range | B::RangeIncl => {
                self.error(span, "a range cannot be an operand here; put it in parentheses");
                error_expr(span)
            }
        }
    }

    fn has_optional_operand(&mut self, lhs: &Expr, r: &Expr) -> bool {
        [lhs.ty, r.ty].into_iter().any(|t| self.infer.resolve(t).optional_inner().is_some())
    }

    /// `a +? b` with an optional operand, as in `a *? b +? c` (§7.4.2): both operands are
    /// evaluated, and the result is `null` if either is `null`, else the operator applied
    /// to their values.
    fn chained_opt_op(&mut self, op: ast::BinOp, lhs: Expr, r: Expr, span: Span) -> Expr {
        let mut stmts = Vec::new();
        let mut cond = Vec::new();
        let mut operands = Vec::new();
        for e in [lhs, r] {
            let t = self.infer.resolve(e.ty);
            let tmp = self.temp(t, e.span);
            let esp = e.span;
            stmts.push(tir::Stmt::Let { local: tmp, init: Some(e) });
            let value = mk(t, tir::ExprKind::Local(tmp), esp);
            match t.optional_inner() {
                Some(inner) => {
                    let x = self.temp(inner, esp);
                    let bind = tir::Pat { ty: inner, kind: tir::PatKind::Bind(x), span: esp };
                    let pat = tir::Pat { ty: t, kind: tir::PatKind::Some(Box::new(bind)), span: esp };
                    cond.push(tir::CondPart::Match { pat, value });
                    operands.push(mk(inner, tir::ExprKind::Local(x), esp));
                }
                None => operands.push(value),
            }
        }
        let r = operands.pop().unwrap();
        let l = operands.pop().unwrap();
        let then = if let ast::BinOp::Shl(_) = op {
            let ty = Ty::optional(l.ty);
            self.obligations.push(Obligation::Numeric {
                ty: l.ty,
                span,
                op: op.as_str(),
                int_only: true,
                signed_only: false,
            });
            mk(
                ty,
                tir::ExprKind::Binary { op: tir::BinOp::Shl(ast::Flavor::Opt), lhs: Box::new(l), rhs: Box::new(r) },
                span,
            )
        } else {
            self.arith_op(op, l, r, span)
        };
        let ty = then.ty;
        let null = mk(ty, tir::ExprKind::Null, span);
        let test = mk(ty, tir::ExprKind::If { cond, then: Box::new(then), else_: Some(Box::new(null)) }, span);
        mk(ty, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(test), span })), span)
    }

    /// An arithmetic or bitwise operator applied to checked operands.
    pub fn arith_op(&mut self, op: ast::BinOp, lhs: Expr, r: Expr, span: Span) -> Expr {
        use ast::BinOp as B;
        match op {
            B::Add(ast::Flavor::Opt) | B::Sub(ast::Flavor::Opt) | B::Mul(ast::Flavor::Opt)
                if self.has_optional_operand(&lhs, &r) =>
            {
                self.chained_opt_op(op, lhs, r, span)
            }
            B::Add(f) | B::Sub(f) | B::Mul(f) => {
                let ty = self.unify_operands(lhs.ty, r.ty, span, &format!("the operands of `{}`", op.as_str()));
                let int_only = f != ast::Flavor::Checked;
                self.obligations.push(Obligation::Numeric { ty, span, op: op.as_str(), int_only, signed_only: false });
                let top = match op {
                    B::Add(f) => tir::BinOp::Add(f),
                    B::Sub(f) => tir::BinOp::Sub(f),
                    _ => tir::BinOp::Mul(f),
                };
                let rty = if f == ast::Flavor::Opt { Ty::optional(ty) } else { ty };
                mk(rty, tir::ExprKind::Binary { op: top, lhs: Box::new(lhs), rhs: Box::new(r) }, span)
            }
            _ => {
                let ty = self.unify_operands(lhs.ty, r.ty, span, &format!("the operands of `{}`", op.as_str()));
                let st = self.infer.resolve(ty);
                if op != B::Div
                    && op != B::Rem
                    && let Some(u) = self.bits_operand(st, op.as_str(), span)
                {
                    let top = match op {
                        B::BitAnd => tir::BinOp::BitAnd,
                        B::BitOr => tir::BinOp::BitOr,
                        _ => tir::BinOp::BitXor,
                    };
                    let (l, r) = (self.bits_convert(lhs, u), self.bits_convert(r, u));
                    let n = mk(u, tir::ExprKind::Binary { op: top, lhs: Box::new(l), rhs: Box::new(r) }, span);
                    return self.bits_convert(n, st);
                }
                let int_only = op != B::Div;
                self.obligations.push(Obligation::Numeric { ty, span, op: op.as_str(), int_only, signed_only: false });
                let top = match op {
                    B::Div => tir::BinOp::Div,
                    B::Rem => tir::BinOp::Rem,
                    B::BitAnd => tir::BinOp::BitAnd,
                    B::BitOr => tir::BinOp::BitOr,
                    _ => tir::BinOp::BitXor,
                };
                mk(ty, tir::ExprKind::Binary { op: top, lhs: Box::new(lhs), rhs: Box::new(r) }, span)
            }
        }
    }

    /// The bits of the named (non-padding) fields of `#bits` struct `t`, if it has
    /// padding fields `_`.
    fn bits_padding_mask(&mut self, t: Ty) -> Option<num_bigint::BigInt> {
        let fields = self.sema.struct_fields(t)?.to_vec();
        if !fields.iter().any(|f| f.name.as_str() == "_") {
            return None;
        }
        let mut mask = num_bigint::BigInt::from(0);
        let mut off = 0u64;
        for f in &fields {
            let w = self.sema.bit_width(f.ty)?;
            if f.name.as_str() != "_" {
                mask |= ((num_bigint::BigInt::from(1) << w) - 1) << off;
            }
            off += w;
        }
        Some(mask)
    }

    /// The underlying integer of `t` if it is a `#bits` struct, for the bitwise operator
    /// `op` (§7.4.1); reports an error if some field is not a `bool` or an integer.
    fn bits_operand(&mut self, t: Ty, op: &str, span: Span) -> Option<Ty> {
        let u = self.sema.bits_struct_ty(t)?;
        if !self.sema.is_plain(t) {
            self.error(
                span,
                format!("`{}` applies only to `#bits` structs whose fields are all `bool` or integers", op),
            );
        }
        Some(u)
    }

    /// Converts between a `#bits` struct and its underlying integer.
    fn bits_convert(&mut self, v: Expr, to: Ty) -> Expr {
        let span = v.span;
        mk(to, tir::ExprKind::Builtin { op: tir::Builtin::Convert, args: vec![v] }, span)
    }

    /// `lo..hi` or `lo..=hi` as a value: a `Range(T)` or `RangeIncl(T)` (§5.16).
    fn range_value(&mut self, inclusive: bool, lo: &ast::Expr, hi: &ast::Expr, expect: Option<Ty>, span: Span) -> Expr {
        let Some(template) = self.sema.range_template(inclusive) else {
            self.error(span, "the core library's range types are not available");
            return error_expr(span);
        };
        // The bound type comes from the expected range type, if there is one.
        let want = expect.and_then(|t| {
            let t = self.infer.resolve(t);
            self.sema.range_of(t).filter(|(i, _)| *i == inclusive).map(|(_, e)| e)
        });
        let l = self.expr(lo, want);
        let h = self.expr(hi, Some(want.unwrap_or(l.ty)));
        let mut elem = match want {
            Some(w) => w,
            None => self.unify_operands(l.ty, h.ty, span, "range bounds"),
        };
        // The struct type needs a concrete `T` now: untyped bounds take the range
        // fallback (§6.1).
        if self.infer.resolve(elem).has_infer() {
            let _ = self.infer.unify(elem, Ty::usize());
        }
        elem = self.infer.resolve(elem);
        // `Range(T)` needs `Ord(T)`; `RangeIncl(T)` an integer type (§5.16).
        if inclusive {
            let op = "..=";
            self.obligations.push(Obligation::Numeric { ty: elem, span, op, int_only: true, signed_only: false });
        } else {
            self.obligations.push(Obligation::Ord { ty: elem, span });
        }
        let (l, h) = (self.coerce(l, elem), self.coerce(h, elem));
        let ty = self.sema.instantiate_adt(template, vec![elem]);
        mk(ty, tir::ExprKind::Aggregate(vec![l, h]), span)
    }

    /// `p + n`, `p - n` and `p - q` on a many-pointer `p` (§5.5).
    fn pointer_arith(&mut self, op: ast::BinOp, lhs: Expr, rhs: &ast::Expr, span: Span) -> Expr {
        let lt = self.infer.resolve(lhs.ty);
        let r = self.expr(rhs, None);
        let rt = self.infer.resolve(r.ty);
        let b = |op, ty, args| mk(ty, tir::ExprKind::Builtin { op, args }, span);
        let sub = matches!(op, ast::BinOp::Sub(_));
        if let TyKind::ManyPtr { elem, .. } = rt.kind() {
            let TyKind::ManyPtr { elem: le, .. } = lt.kind() else { unreachable!() };
            if !sub || elem != le {
                self.error(span, format!("cannot {} `{}` and `{}`", if sub { "subtract" } else { "add" }, lt, rt));
                return error_expr(span);
            }
            // The distance does not depend on `const`: make both sides read-only.
            let ro = Ty::intern(TyKind::ManyPtr { elem: *elem, is_const: true });
            let (l, r) = (self.coerce(lhs, ro), self.coerce(r, ro));
            return b(tir::Builtin::PtrDiff, Ty::isize(), vec![l, r]);
        }
        if rt.is_infer() {
            self.unify_operands(r.ty, Ty::usize(), span, "a pointer offset");
        } else if rt != Ty::usize() && rt != Ty::isize() && !rt.is_error() {
            self.error(r.span, format!("a pointer offset must be a `usize` or an `isize`, not `{}`", rt));
            return error_expr(span);
        }
        b(if sub { tir::Builtin::PtrSub } else { tir::Builtin::PtrAdd }, lt, vec![lhs, r])
    }

    /// Makes two operands of `is` the same type: unify, or coerce one to the other.
    pub(super) fn equalize(&mut self, l: Expr, r: Expr, span: Span) -> (Expr, Expr) {
        let (lt, rt) = (self.infer.resolve(l.ty), self.infer.resolve(r.ty));
        if !lt.has_infer() && !lt.is_error() && !self.eq_allowed(lt) {
            self.error(span, format!("`is` is not defined for `{}` (§7.6)", lt));
        }
        // A literal compared with an optional or result is wrapped: `x is 1`.
        let wraps = |t: Ty| matches!(t.kind(), TyKind::Optional(_) | TyKind::Result { .. }) && !t.has_infer();
        if rt.is_infer() && wraps(lt) {
            let r = self.coerce(r, lt);
            return (l, r);
        }
        if lt.is_infer() && wraps(rt) {
            let l = self.coerce(l, rt);
            return (l, r);
        }
        // `z is 0`: a real constant compared with a complex number (§7.4.7).
        if let Some(elem) = self.sema.complex_of(lt)
            && let Some(p) = self.promote_real_constant(&r, lt, elem)
        {
            return (l, p);
        }
        if let Some(elem) = self.sema.complex_of(rt)
            && let Some(p) = self.promote_real_constant(&l, rt, elem)
        {
            return (p, r);
        }
        if lt.has_infer() || rt.has_infer() || lt == rt {
            self.unify_operands(l.ty, r.ty, span, "the operands of `is`");
            return (l, r);
        }
        if self.coercion_steps(rt, lt).is_some() {
            let r = self.coerce(r, lt);
            return (l, r);
        }
        if self.coercion_steps(lt, rt).is_some() {
            let l = self.coerce(l, rt);
            return (l, r);
        }
        self.error(span, format!("cannot compare `{}` with `{}`", lt, rt));
        (l, r)
    }

    fn index(&mut self, base: &ast::Expr, index: &ast::Expr, span: Span) -> Expr {
        let b = self.expr(base, None);
        self.index_value(b, index, span)
    }

    /// `b[index]` with the base already checked.
    pub fn index_value(&mut self, b: Expr, index: &ast::Expr, span: Span) -> Expr {
        let b = self.soa_deref(b);
        if let Some(info) = self.soa_of(&b) {
            return self.soa_element(b, info, index, span);
        }
        let b = self.auto_deref_array(b);
        if let Some(e) = self.infer.resolve(b.ty).enum_index() {
            return self.enum_array_index(b, e, index, span);
        }
        let i = self.expr(index, None);
        // `a[r]` with a range value `r` is the slice `a[r.lo..r.hi]` (§5.16).
        let it = self.infer.resolve(i.ty);
        if let Some((inclusive, elem)) = self.sema.range_of(it) {
            let r = self.temp(it, span);
            let field = |n| {
                let base = Box::new(mk(it, tir::ExprKind::Local(r), span));
                mk(elem, tir::ExprKind::Field { base, index: n }, span)
            };
            let s = self.slice_of(b, span, Some(field(0)), Some(field(1)), inclusive, span);
            let ty = s.ty;
            let block = tir::Block { stmts: vec![tir::Stmt::Let { local: r, init: Some(i) }], tail: Some(s), span };
            return mk(ty, tir::ExprKind::Block(Box::new(block)), span);
        }
        if !self.infer.require_unsigned(i.ty, Fallback::Usize) && !i.ty.is_error() {
            let t = self.infer.resolve(i.ty);
            self.error(i.span, format!("an index must have an unsigned integer type, not `{}`", t));
        }
        let bt = self.infer.resolve(b.ty);
        let (elem, checked) = match bt.kind() {
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } | TyKind::Slice { elem, .. } => (*elem, true),
            TyKind::ManyPtr { elem, .. } => (*elem, false),
            TyKind::Error => return error_expr(span),
            _ => {
                self.error(span, format!("cannot index a value of type `{}`", bt));
                return error_expr(span);
            }
        };
        mk(elem, tir::ExprKind::Index { base: Box::new(b), index: Box::new(i), checked }, span)
    }

    /// `a[e]` on an enum-indexed array `[E]T`: `e` is an `E`, and the element is the one at
    /// the position of `e`'s variant among `@variants(E)` (§5.6). It needs no check.
    fn enum_array_index(&mut self, b: Expr, e: Ty, index: &ast::Expr, span: Span) -> Expr {
        let elem = match self.infer.resolve(b.ty).kind() {
            TyKind::Array { elem, .. } => *elem,
            _ => unreachable!(),
        };
        let i = self.expr(index, Some(e));
        let it = self.infer.resolve(i.ty);
        if it.is_error() {
            return error_expr(span);
        }
        if it != e {
            let bt = self.infer.resolve(b.ty);
            let hint = if it.is_infer() || it.is_integer() || self.sema.range_of(it).is_some() {
                "; its elements in the order of the variants are the slice `a[..]`"
            } else {
                ""
            };
            self.error(i.span, format!("`{}` is indexed by `{}` values, not `{}`{}", bt, e, it, hint));
            return error_expr(span);
        }
        let pos = self.variant_position(i, e, span);
        mk(elem, tir::ExprKind::Index { base: Box::new(b), index: Box::new(pos), checked: false }, span)
    }

    /// The position (`usize`) of the variant of the enum value `v` among `@variants(e)`:
    /// its tag when the tags count up from 0 in declaration order, otherwise a `match`.
    fn variant_position(&mut self, v: Expr, e: Ty, span: Span) -> Expr {
        let Some((variants, tag)) = self.sema.union_variants(e) else { return error_expr(span) };
        let counts_up = variants.iter().enumerate().all(|(i, var)| var.tag == BigInt::from(i));
        if counts_up && matches!(tag.kind(), TyKind::Int(t) if !t.signed && t.bits <= 32) {
            let t = mk(tag, tir::ExprKind::Builtin { op: tir::Builtin::Tag, args: vec![v] }, span);
            return self.coerce(t, Ty::usize());
        }
        let arms = (0..variants.len())
            .map(|i| tir::Arm {
                pat: tir::Pat { ty: e, kind: tir::PatKind::Variant { variant: i as u32, payload: None }, span },
                guard: None,
                body: mk(Ty::usize(), tir::ExprKind::Int(i.into()), span),
            })
            .collect();
        mk(Ty::usize(), tir::ExprKind::Match { scrutinee: Box::new(v), arms }, span)
    }

    /// `p[i]` on a pointer to an array indexes the array (§7.9).
    fn auto_deref_array(&mut self, b: Expr) -> Expr {
        if let TyKind::Ptr { pointee, .. } = self.infer.shallow(b.ty).kind()
            && pointee.is_array()
        {
            let span = b.span;
            return mk(*pointee, tir::ExprKind::Deref(Box::new(b)), span);
        }
        b
    }

    /// Field access dereferences one pointer level (§5.5).
    pub fn auto_deref(&mut self, b: Expr) -> Expr {
        if let TyKind::Ptr { pointee, .. } = self.infer.shallow(b.ty).kind() {
            let span = b.span;
            return mk(*pointee, tir::ExprKind::Deref(Box::new(b)), span);
        }
        b
    }

    /// If `e` is a name denoting a type, that type.
    pub fn type_of_name(&mut self, e: &ast::Expr) -> Option<Ty> {
        // A local alias: `Cols :: #soa []T` then `Cols.make(n)`.
        if let ExprKind::Ident(id) = &e.kind
            && let Some(super::func::LocalBinding::Type(t)) = self.lookup_local(id.name)
        {
            return Some(*t);
        }
        if let Some(d) = self.qualified_decl(e) {
            return match self.sema.resolve_decl(d?) {
                DeclValue::Type(t) => Some(t),
                _ => None,
            };
        }
        // `G(args)` where `G` is a generic type.
        if let ExprKind::Call { callee, .. } = &e.kind
            && let ExprKind::Ident(id) = &callee.kind
            && self.lookup_local(id.name).is_none()
        {
            let module = self.module;
            if let Some(d) = self.sema.lookup_module_name(module, id.name)
                && let DeclValue::GenericType(_) = self.sema.resolve_decl(d)
            {
                let t = self.resolve_type(e);
                return (!t.is_error()).then_some(t);
            }
            return None;
        }
        // `m.G(args)` where `G` is a generic type of module `m`.
        if let ExprKind::Call { callee, .. } = &e.kind
            && let ExprKind::Field { base, name } = &callee.kind
            && let Some(m) = self.module_of(base)
        {
            let d = self.sema.modules[m.0 as usize].scope.get(&name.name).copied();
            if let Some(d) = d
                && self.sema.decl_visible(d, self.module)
                && let DeclValue::GenericType(_) = self.sema.resolve_decl(d)
            {
                let t = self.resolve_type(e);
                return (!t.is_error()).then_some(t);
            }
            return None;
        }
        let ExprKind::Ident(id) = &e.kind else { return None };
        if self.lookup_local(id.name).is_some() {
            return None;
        }
        let module = self.module;
        if let Some(d) = self.sema.lookup_module_name(module, id.name) {
            return match self.sema.resolve_decl(d) {
                DeclValue::Type(t) => Some(t),
                _ => None,
            };
        }
        super::builtins::builtin_type(id.name.as_str())
    }

    fn field(&mut self, base: &ast::Expr, name: ast::Ident, span: Span) -> Expr {
        // `m.x` — a declaration of another module (§3.2).
        if let Some(m) = self.module_of(base) {
            let from = self.module;
            return match self.sema.module_member(m, name, from) {
                Some(d) => self.decl_expr(d, name.name, None, span),
                None => error_expr(span),
            };
        }
        // `U.V` — a payload-less variant of a union type.
        if let Some(t) = self.type_of_name(base) {
            return self.construct_variant(t, name, None, span);
        }
        // `a[i].f` on a `#soa`: element `i` of column `f`, without gathering the element.
        let b = if let ExprKind::Index { base: inner, index } = &base.kind {
            let ib = self.expr(inner, None);
            let ib = self.soa_deref(ib);
            if let Some(info) = self.soa_of(&ib) {
                return self.soa_element_field(ib, info, index, name, span);
            }
            self.index_value(ib, index, base.span)
        } else {
            self.expr(base, None)
        };
        let b = self.soa_deref(b);
        if let Some(info) = self.soa_of(&b) {
            return self.soa_member(b, info, name, span);
        }
        let b = self.auto_deref(b);
        let bt = self.infer.resolve(b.ty);
        match (bt.kind(), name.name.as_str()) {
            (TyKind::Tuple { elems, names: Some(names) }, _) => {
                if let Some(i) = names.iter().position(|n| *n == name.name) {
                    return mk(elems[i], tir::ExprKind::Field { base: Box::new(b), index: i as u32 }, span);
                }
            }
            (TyKind::Slice { .. }, "len") => return mk(Ty::usize(), tir::ExprKind::SliceLen(Box::new(b)), span),
            (TyKind::Slice { is_const, elem }, "ptr") => {
                return mk(Ty::many_ptr(*is_const, *elem), tir::ExprKind::SlicePtr(Box::new(b)), span);
            }
            (TyKind::Array { len, .. }, "len") => return mk(Ty::usize(), tir::ExprKind::Int((*len).into()), span),
            (TyKind::ParamArray { len, .. }, "len") => return self.len_value(*len, span),
            (TyKind::Error, _) => return error_expr(span),
            _ => {}
        }
        if let Some(e) = self.struct_field(b, name.name, span) {
            return e;
        }
        let d = Diagnostic::error(name.span, format!("type `{}` has no field `{}`", bt, name.name));
        self.emit_with_continuation_note(d, base.span, name.span);
        error_expr(span)
    }

    /// The value of a length that depends on compile-time value parameters
    /// (a `Param`, `Value` or `LenExpr`), as a `usize` (§16.5).
    fn len_value(&mut self, len: Ty, span: Span) -> Expr {
        match len.kind() {
            TyKind::Param(p) => {
                let v = mk(self.sema.value_params[p], tir::ExprKind::ValueParam(*p), span);
                if v.ty == Ty::usize() {
                    v
                } else {
                    mk(Ty::usize(), tir::ExprKind::Coerce { kind: Coercion::IntWiden, expr: Box::new(v) }, span)
                }
            }
            TyKind::Value(k) => match k.as_int() {
                Some(n) => mk(Ty::usize(), tir::ExprKind::Int(n), span),
                None => error_expr(span),
            },
            TyKind::LenExpr { op, lhs, rhs } => {
                use crate::types::LenOp;
                let op = match op {
                    LenOp::Add => tir::BinOp::Add(ast::Flavor::Checked),
                    LenOp::Sub => tir::BinOp::Sub(ast::Flavor::Checked),
                    LenOp::Mul => tir::BinOp::Mul(ast::Flavor::Checked),
                    LenOp::Shl => tir::BinOp::Shl(ast::Flavor::Checked),
                    LenOp::Div => tir::BinOp::Div,
                    LenOp::Rem => tir::BinOp::Rem,
                    LenOp::Shr => tir::BinOp::Shr,
                    LenOp::And => tir::BinOp::BitAnd,
                    LenOp::Or => tir::BinOp::BitOr,
                    LenOp::Xor => tir::BinOp::BitXor,
                };
                let (lhs, rhs) = (self.len_value(*lhs, span), self.len_value(*rhs, span));
                mk(Ty::usize(), tir::ExprKind::Binary { op, lhs: Box::new(lhs), rhs: Box::new(rhs) }, span)
            }
            _ => error_expr(span),
        }
    }

    /// Field `name` of a struct value, looking through `using` fields (§5.10.3).
    fn struct_field(&mut self, b: Expr, name: crate::symbol::Symbol, span: Span) -> Option<Expr> {
        self.struct_field_in(b, name, span, 0)
    }

    fn struct_field_in(&mut self, b: Expr, name: crate::symbol::Symbol, span: Span, depth: u32) -> Option<Expr> {
        let fields = self.sema.struct_fields(b.ty).or_else(|| self.sema.c_union_fields(b.ty))?;
        if let Some(i) = fields.iter().position(|f| f.name == name) {
            let ty = fields[i].ty;
            if !self.field_visible(fields[i].vis, b.ty) {
                let bt = b.ty;
                self.error(span, hidden_field_message(name, bt, fields[i].vis));
            }
            return Some(mk(ty, tir::ExprKind::Field { base: Box::new(b), index: i as u32 }, span));
        }
        let TyKind::Adt(id, _) = b.ty.kind() else { return None };
        if !self.sema.adts.get(id).is_some_and(|a| a.has_using) {
            return None;
        }
        // A `using` chain that contains itself is an error already; do not follow it forever.
        if depth >= 64 {
            return None;
        }
        let first = fields.first()?.ty;
        let inner = mk(first, tir::ExprKind::Field { base: Box::new(b), index: 0 }, span);
        self.struct_field_in(inner, name, span, depth + 1)
    }

    /// Whether pointer type `src` is `^Outer` (or `^const Outer`) and `target` is
    /// `^S` (resp. `^const S`) for a struct `S` embedded by `using`, directly or through
    /// the embedded structs' own `using` fields.
    pub fn embeds(&mut self, src: Ty, target: Ty) -> bool {
        let (TyKind::Ptr { is_const: sc, pointee: outer }, TyKind::Ptr { is_const: tc, pointee: inner }) =
            (src.kind(), target.kind())
        else {
            return false;
        };
        if *sc && !*tc {
            return false;
        }
        let mut cur = *outer;
        for _ in 0..64 {
            let TyKind::Adt(id, _) = cur.kind() else { return false };
            if !self.sema.adts.get(id).is_some_and(|a| a.has_using) {
                return false;
            }
            let Some(fields) = self.sema.struct_fields(cur) else { return false };
            let Some(first) = fields.first() else { return false };
            cur = first.ty;
            if cur == *inner {
                return true;
            }
        }
        false
    }

    /// `@sqrt(x)`, `@fma(a, b, c)` on floats and the bit operations on integers (§20.3).
    fn numeric_intrinsic(&mut self, n: &str, args: &[ast::Arg], span: Span) -> Expr {
        let rotate = matches!(n, "rotl" | "rotr");
        let arity = match n {
            "fma" => 3,
            "rotl" | "rotr" => 2,
            _ => 1,
        };
        if args.len() != arity || args.iter().any(|a| !matches!(a.kind, ast::ArgKind::Positional(_))) {
            self.error(span, format!("`@{}` takes {} argument(s)", n, arity));
            return error_expr(span);
        }
        let mut vals = vec![];
        for a in args {
            let ast::ArgKind::Positional(e) = &a.kind else { unreachable!() };
            // The amount of a rotation is a `u32`; the other operands share a type.
            let expect =
                if rotate && !vals.is_empty() { Some(Ty::int(false, 32)) } else { vals.first().map(|v: &Expr| v.ty) };
            let v = self.expr(e, expect);
            let v = match expect {
                Some(t) => self.coerce(v, t),
                None => v,
            };
            vals.push(v);
        }
        let float = matches!(n, "sqrt" | "fma");
        // Untyped literals take the usual fallback types (§6.1).
        let t0 = self.infer.resolve(vals[0].ty);
        if t0.is_infer() {
            let fallback = if float { Ty::f64() } else { Ty::i32() };
            let _ = self.infer.unify(t0, fallback);
        }
        let t = self.infer.resolve(vals[0].ty);
        let ok = if float { t.is_float() } else { t.is_integer() };
        if !ok {
            if !t.is_error() {
                let want = if float { "a float" } else { "an integer" };
                self.error(span, format!("`@{}` needs {} of a known type, not `{}`", n, want, t));
            }
            return error_expr(span);
        }
        let (op, ty) = match n {
            "sqrt" => (tir::Builtin::Sqrt, t),
            "fma" => (tir::Builtin::Fma, t),
            "clz" => (tir::Builtin::Clz, Ty::int(false, 32)),
            "ctz" => (tir::Builtin::Ctz, Ty::int(false, 32)),
            "popcount" => (tir::Builtin::Popcount, Ty::int(false, 32)),
            "bswap" => {
                // A whole number of bytes, such as `u24` (stored in 4 bytes).
                if !matches!(t.kind(), TyKind::Int(i) if i.bits % 8 == 0)
                    && !matches!(t.kind(), TyKind::Usize | TyKind::Isize)
                {
                    self.error(span, format!("`@bswap` needs an integer of whole bytes, not `{}`", t));
                }
                (tir::Builtin::Bswap, t)
            }
            "rotl" => (tir::Builtin::Rotl, t),
            "rotr" => (tir::Builtin::Rotr, t),
            _ => (tir::Builtin::Bitreverse, t),
        };
        mk(ty, tir::ExprKind::Builtin { op, args: vals }, span)
    }

    /// The type argument at position `i` of `@name(..)`, or `None` after an error.
    fn intrinsic_type_arg(&mut self, name: ast::Ident, args: &[ast::Arg], i: usize, span: Span) -> Option<Ty> {
        match args.get(i).map(|a| &a.kind) {
            Some(ast::ArgKind::Positional(e)) => {
                let t = self.resolve_type(e);
                (!t.is_error()).then_some(t)
            }
            _ => {
                self.error(span, format!("`@{}` needs a type argument", name.name));
                None
            }
        }
    }

    /// Whether the layout of `t` is still being resolved (and so not known); reports the cycle if so.
    fn layout_pending(&mut self, name: ast::Ident, t: Ty, span: Span) -> bool {
        let Some(id) = self.sema.pending_layout(t) else { return false };
        self.error(
            span,
            format!(
                "`@{}` needs the layout of `{}`, which depends on this value: the declarations form a cycle (§3.4)",
                name.name,
                id.name()
            ),
        );
        true
    }

    /// Intrinsics that are compile-time constants (§20.3).
    fn intrinsic(&mut self, name: ast::Ident, args: &[ast::Arg], span: Span) -> Expr {
        let in_prelude = self.sema.modules[self.module.0 as usize].is_prelude;
        match name.name.as_str() {
            "size_of" | "align_of" => self.layout_intrinsic(name, args, span),
            "min_value" | "max_value" => self.limit_intrinsic(name, args, span),
            "offset_of" => self.offset_of_intrinsic(name, args, span),
            // Compiler-internal heap primitives behind the core library's heap allocator.
            "alloc_tag" | "alloc_check" | "heap_alloc" | "heap_free" if in_prelude => {
                self.heap_intrinsic(name.name.as_str(), args, span)
            }
            n if (in_prelude || self.sema.modules[self.module.0 as usize].is_std)
                && let Some(&op) = tir::SysOp::ALL.iter().find(|op| op.signature().0 == n) =>
            {
                self.sys_intrinsic(op, args, span)
            }
            "hash" => self.hash_intrinsic(args, span),
            "static_assert" => {
                let module = self.module;
                self.sema.static_assert(module, args, span).unwrap_or_else(|| unit(span))
            }
            "compile_error" => {
                let module = self.module;
                let msg = match args.first().map(|a| &a.kind) {
                    Some(ast::ArgKind::Positional(m)) => self.sema.const_string(module, m),
                    _ => None,
                };
                match msg {
                    Some(m) => self.error(span, m),
                    None => self.error(span, "`@compile_error` takes a constant string message"),
                }
                error_expr(span)
            }
            "location" => {
                let f = self.location_function();
                self.sema.source_location(span, f)
            }
            "caller_location" => match self.sema.caller_site {
                Some((site, f)) => self.sema.source_location(site, f),
                None => {
                    self.error(span, "`@caller_location()` is allowed only as a parameter's default value");
                    error_expr(span)
                }
            },
            "atomic_load" | "atomic_store" | "atomic_swap" | "atomic_cas" | "atomic_add" | "atomic_sub"
            | "atomic_and" | "atomic_or" | "atomic_xor" | "atomic_max" | "atomic_min" | "fence" | "volatile_load"
            | "volatile_store" => self.atomic(name.name.as_str(), args, span),
            "black_box" => match args {
                [ast::Arg { kind: ast::ArgKind::Positional(x), .. }] => {
                    let x = self.expr(x, None);
                    let ty = x.ty;
                    mk(ty, tir::ExprKind::Builtin { op: tir::Builtin::BlackBox, args: vec![x] }, span)
                }
                _ => {
                    self.error(span, "`@black_box` takes one value: `@black_box(x)`");
                    error_expr(span)
                }
            },
            "unreachable" => {
                mk(Ty::never(), tir::ExprKind::Builtin { op: tir::Builtin::Unreachable, args: vec![] }, span)
            }
            "expect_true" | "expect_false" => match args.first().map(|a| &a.kind) {
                Some(ast::ArgKind::Positional(b)) if args.len() == 1 => {
                    let v = self.expr(b, Some(Ty::bool()));
                    self.coerce(v, Ty::bool())
                }
                _ => {
                    self.error(span, format!("`@{}` takes one `bool`", name.name));
                    error_expr(span)
                }
            },
            "fiber" => self.fiber_intrinsic(args, span),
            "asm" => self.asm(args, span),
            "write" => self.write_intrinsic(args, span),
            "prefetch" => self.prefetch_intrinsic(args, span),
            "sqrt" | "fma" | "clz" | "ctz" | "popcount" | "bswap" | "bitreverse" | "rotl" | "rotr" => {
                self.numeric_intrinsic(name.name.as_str(), args, span)
            }
            // The smallest fiber stack (§14.1), in bytes.
            "min_stack" => mk(Ty::usize(), tir::ExprKind::Int(tir::MIN_STACK.into()), span),
            "stack_size" => self.stack_size_intrinsic(args, span),
            "type_name" => {
                let Some(t) = self.intrinsic_type_arg(name, args, 0, span) else { return error_expr(span) };
                if super::generics::has_param(t) {
                    // Named in each instance (§16.1).
                    return mk(Ty::string(), tir::ExprKind::TypeName { of: t }, span);
                }
                mk(Ty::string(), tir::ExprKind::Str(Rc::from(t.to_string().as_bytes())), span)
            }
            "variants" => self.variants_intrinsic(name, args, span),
            "variant_name" => self.variant_name_intrinsic(args, span),
            other => {
                self.error(span, format!("intrinsic `@{}` is not supported yet", other));
                error_expr(span)
            }
        }
    }

    /// `@size_of(T)` and `@align_of(T)`.
    fn layout_intrinsic(&mut self, name: ast::Ident, args: &[ast::Arg], span: Span) -> Expr {
        let Some(t) = self.intrinsic_type_arg(name, args, 0, span) else { return error_expr(span) };
        if t.is_never() {
            self.error(span, format!("`never` has no values, so `@{}` does not apply to it", name.name));
            return error_expr(span);
        }
        if super::generics::has_param(t) {
            let align = name.name.as_str() == "align_of";
            return mk(Ty::usize(), tir::ExprKind::SizeOf { of: t, align }, span);
        }
        if self.layout_pending(name, t, span) {
            return error_expr(span);
        }
        let (size, align) = self.sema.with_layouts(|l| (l.size_of(t), l.align_of(t)));
        let v = if name.name.as_str() == "size_of" { size } else { align };
        mk(Ty::usize(), tir::ExprKind::Int(v.into()), span)
    }

    /// `@min_value(T)` and `@max_value(T)`.
    fn limit_intrinsic(&mut self, name: ast::Ident, args: &[ast::Arg], span: Span) -> Expr {
        let Some(t) = self.intrinsic_type_arg(name, args, 0, span) else { return error_expr(span) };
        let max = name.name.as_str() == "max_value";
        if let TyKind::Param(p) = t.kind() {
            if !self.sema.param_has(*p, super::generics::Constraint::Int) {
                self.error(span, format!("`@{}({})` needs `where Int({})`", name.name, t, t));
                return error_expr(span);
            }
            return mk(t, tir::ExprKind::IntLimit { of: t, max }, span);
        }
        match int_limit(t, max, &self.sema.target) {
            Some(v) => mk(t, tir::ExprKind::Int(v), span),
            None => {
                self.error(span, format!("`@{}` takes an integer type, not `{}`", name.name, t));
                error_expr(span)
            }
        }
    }

    /// `@offset_of(S, field)`.
    fn offset_of_intrinsic(&mut self, name: ast::Ident, args: &[ast::Arg], span: Span) -> Expr {
        let Some(t) = self.intrinsic_type_arg(name, args, 0, span) else { return error_expr(span) };
        if self.layout_pending(name, t, span) {
            return error_expr(span);
        }
        let field = match args.get(1).map(|a| &a.kind) {
            Some(ast::ArgKind::Positional(Expr0 { kind: ExprKind::Ident(f), .. })) => f.name,
            _ => {
                self.error(span, "`@offset_of(S, field)` needs a struct type and a field name");
                return error_expr(span);
            }
        };
        let Some(i) = self.sema.struct_fields(t).and_then(|fs| fs.iter().position(|f| f.name == field)) else {
            self.error(span, format!("`{}` has no field `{}`", t, field));
            return error_expr(span);
        };
        let off = self.sema.with_layouts(|l| l.layout(t).offsets.get(i).copied().unwrap_or(0));
        mk(Ty::usize(), tir::ExprKind::Int(off.into()), span)
    }

    /// The positional arguments, each checked against and coerced to the matching type of `tys`;
    /// `None` when one is missing or not positional.
    fn coerced_args(&mut self, tys: &[Ty], args: &[ast::Arg]) -> Option<Vec<Expr>> {
        let mut vals = vec![];
        for (i, t) in tys.iter().enumerate() {
            match args.get(i).map(|a| &a.kind) {
                Some(ast::ArgKind::Positional(e)) => {
                    let v = self.expr(e, Some(*t));
                    vals.push(self.coerce(v, *t));
                }
                _ => return None,
            }
        }
        Some(vals)
    }

    /// The heap primitives behind the core library's allocator (`@alloc_tag`, `@alloc_check`,
    /// `@heap_alloc`, `@heap_free`).
    fn heap_intrinsic(&mut self, name: &str, args: &[ast::Arg], span: Span) -> Expr {
        let bytes = Ty::many_ptr(false, Ty::u8());
        let (tys, op, ty) = match name {
            "alloc_tag" | "alloc_check" => {
                let raw = Ty::intern(TyKind::Rawptr);
                let tys = vec![bytes, raw, raw, Ty::string(), Ty::int(false, 32), Ty::int(false, 32)];
                let op = if name == "alloc_tag" { tir::Builtin::AllocTag } else { tir::Builtin::AllocCheck };
                (tys, op, Ty::unit())
            }
            "heap_alloc" => (vec![Ty::usize(), Ty::usize()], tir::Builtin::HeapAlloc, Ty::optional(bytes)),
            _ => (vec![bytes, Ty::usize(), Ty::usize()], tir::Builtin::HeapFree, Ty::unit()),
        };
        let Some(vals) = self.coerced_args(&tys, args) else { return error_expr(span) };
        mk(ty, tir::ExprKind::Builtin { op, args: vals }, span)
    }

    /// A system operation of the core library (`SysOp`), called as `@name(..)`.
    fn sys_intrinsic(&mut self, op: tir::SysOp, args: &[ast::Arg], span: Span) -> Expr {
        let (n, params, ret) = op.signature();
        if args.len() != params.len() {
            self.error(span, format!("`@{}` takes {} argument(s)", n, params.len()));
            return error_expr(span);
        }
        let mut vals = vec![];
        for (i, (a, t)) in args.iter().zip(params).enumerate() {
            let ast::ArgKind::Positional(e) = &a.kind else { return error_expr(span) };
            if op == tir::SysOp::ThreadSpawn && i == 0 {
                vals.push(self.thread_entry_arg(e));
                continue;
            }
            let v = self.expr(e, Some(t));
            vals.push(self.coerce(v, t));
        }
        mk(ret, tir::ExprKind::Builtin { op: tir::Builtin::Sys(op), args: vals }, span)
    }

    /// The entry function of a spawned thread: a plain function `^fn(arg: rawptr) needs Sys`
    /// (§19.6); a typed `^T` argument has the same representation as `rawptr`.
    fn thread_entry_arg(&mut self, e: &ast::Expr) -> Expr {
        let v = self.expr(e, None);
        let vt = self.infer.resolve(v.ty);
        let ok = match vt.kind() {
            TyKind::FnPtr(sig) => {
                sig.params.len() == 1
                    && matches!(sig.params[0].kind(), TyKind::Rawptr | TyKind::Ptr { .. })
                    && sig.ret.is_unit()
                    && self.sema.within_sys(&sig.effects)
                    && sig.callconv == CallConv::C
            }
            _ => false,
        };
        if !ok && !vt.is_error() {
            self.error(
                e.span,
                format!("a thread runs a `^fn(arg: rawptr) needs Sys` with the C calling convention, not `{}`", vt),
            );
        }
        v
    }

    /// `@hash(x, seed)`.
    fn hash_intrinsic(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        let (Some(ast::ArgKind::Positional(x)), Some(ast::ArgKind::Positional(seed)), None) =
            (args.first().map(|a| &a.kind), args.get(1).map(|a| &a.kind), args.get(2))
        else {
            self.error(span, "`@hash(x, seed)` takes a value and a `u64` seed");
            return error_expr(span);
        };
        let x = self.expr(x, None);
        // A `#soa` slice hashes by its contents, as `is` compares it (§5.6.4).
        let x = self.soa_columns_tuple(x, "`@hash`");
        let xt = self.infer.resolve(x.ty);
        if !xt.has_infer() && !xt.is_error() && !self.eq_allowed(xt) {
            self.error(span, format!("`@hash` is defined only for types with `is`, and `{}` has none (§16.8)", xt));
        }
        let seed = self.expr(seed, Some(Ty::u64()));
        let seed = self.coerce(seed, Ty::u64());
        mk(Ty::u64(), tir::ExprKind::Builtin { op: tir::Builtin::Hash, args: vec![x, seed] }, span)
    }

    /// `@prefetch(p)`: a hint that `p` will be read soon; it has no effect on the program.
    fn prefetch_intrinsic(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        match args {
            [ast::Arg { kind: ast::ArgKind::Positional(p), .. }] => {
                let v = self.expr(p, None);
                let t = self.infer.resolve(v.ty);
                if !t.is_error() && !matches!(t.kind(), TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr) {
                    self.error(p.span, format!("`@prefetch` takes a pointer, not `{}`", t));
                }
                super::func::block_of(vec![tir::Stmt::Expr(v)], span)
            }
            _ => {
                self.error(span, "`@prefetch` takes one pointer: `@prefetch(p)`");
                error_expr(span)
            }
        }
    }

    /// `@stack_size(f)`.
    fn stack_size_intrinsic(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        let f = match args {
            [ast::Arg { kind: ast::ArgKind::Positional(f), .. }] => self.expr(f, None),
            _ => {
                self.error(span, "`@stack_size` takes one function: `@stack_size(f)`");
                return error_expr(span);
            }
        };
        match f.kind {
            tir::ExprKind::FnRef(id) => match self.sema.stack_size(id, span) {
                Some(n) => mk(Ty::usize(), tir::ExprKind::Int(n.into()), span),
                None => error_expr(span),
            },
            _ if f.ty.is_error() => error_expr(span),
            _ => {
                self.error(f.span, "`@stack_size` needs the name of a function (§14.8)");
                error_expr(span)
            }
        }
    }

    /// `@variants(U)`: every variant of a union without payloads, as an array.
    fn variants_intrinsic(&mut self, name: ast::Ident, args: &[ast::Arg], span: Span) -> Expr {
        let Some(t) = self.intrinsic_type_arg(name, args, 0, span) else { return error_expr(span) };
        if matches!(t.kind(), TyKind::Param(_)) {
            self.error(span, format!("`@variants` needs a concrete union type, not the type parameter `{}`", t));
            return error_expr(span);
        }
        let Some((variants, _)) = self.sema.union_variants(t) else {
            let why = if self.is_c_union(t) { " (a `#c_union` has no tag)" } else { "" };
            self.error(span, format!("`@variants` takes a tagged union type, not `{}`{}", t, why));
            return error_expr(span);
        };
        if let Some(v) = variants.iter().find(|v| v.payload.is_some()) {
            self.error(
                span,
                format!("`@variants({})` needs a union without payloads, but variant `{}` has one", t, v.name),
            );
            return error_expr(span);
        }
        let elems = (0..variants.len() as u32)
            .map(|i| mk(t, tir::ExprKind::Variant { variant: i, payload: None }, span))
            .collect::<Vec<_>>();
        mk(Ty::array(elems.len() as u64, t), tir::ExprKind::Array(elems), span)
    }

    /// `@variant_name(x)`: the name of the variant `x` holds.
    fn variant_name_intrinsic(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        let x = match args {
            [ast::Arg { kind: ast::ArgKind::Positional(x), .. }] => self.expr(x, None),
            _ => {
                self.error(span, "`@variant_name` takes one union value: `@variant_name(x)`");
                return error_expr(span);
            }
        };
        let t = self.infer.resolve(x.ty);
        if t.is_error() {
            return error_expr(span);
        }
        let Some((variants, _)) = self.sema.union_variants(t) else {
            let why = if self.is_c_union(t) { " (a `#c_union` has no tag)" } else { "" };
            self.error(span, format!("`@variant_name` takes a value of a tagged union type, not `{}`{}", t, why));
            return error_expr(span);
        };
        let pat = |kind| tir::Pat { ty: t, kind, span };
        let arms = variants
            .iter()
            .enumerate()
            .map(|(i, v)| tir::Arm {
                pat: pat(tir::PatKind::Variant { variant: i as u32, payload: None }),
                guard: None,
                body: mk(Ty::string(), tir::ExprKind::Str(Rc::from(v.name.as_str().as_bytes())), span),
            })
            .collect();
        mk(Ty::string(), tir::ExprKind::Match { scrutinee: Box::new(x), arms }, span)
    }

    // --- optionals and results (§10) ---

    fn res_unwrap(&mut self, inner: &ast::Expr, span: Span) -> Expr {
        let v = self.expr(inner, None);
        match self.infer.resolve(v.ty).kind() {
            TyKind::Result { ok, .. } => mk(*ok, tir::ExprKind::Unwrap { expr: Box::new(v), is_result: true }, span),
            TyKind::Error => error_expr(span),
            _ => {
                let t = self.infer.resolve(v.ty);
                self.error(span, format!("`!` unwraps a result, but this is `{}`", t));
                error_expr(span)
            }
        }
    }

    /// `x else d` and `x else e => d` (§10.4).
    fn else_expr(
        &mut self,
        lhs: &ast::Expr,
        capture: Option<ast::Ident>,
        rhs: &ast::Expr,
        expect: Option<Ty>,
        span: Span,
    ) -> Expr {
        let l = self.expr(lhs, None);
        let lt = self.infer.resolve(l.ty);
        let (present, err) = match lt.kind() {
            TyKind::Optional(t) => (*t, None),
            TyKind::Result { err, ok } => (*ok, Some(*err)),
            TyKind::Error => {
                self.else_after_error(capture, rhs);
                return error_expr(span);
            }
            _ => {
                let mut broken = false;
                lt.walk(&mut |t| broken |= t.is_error());
                if !broken {
                    self.error(
                        lhs.span,
                        format!("`else` needs an optional or a result on its left, but this is `{}`", lt),
                    );
                }
                self.else_after_error(capture, rhs);
                return error_expr(span);
            }
        };
        self.push_scope();
        let cap = match (capture, err) {
            (Some(c), Some(e)) => {
                let id = self.new_local(c.name, e, false, c.span);
                self.declare(c.name, super::func::LocalBinding::Var(id), c.span);
                Some(id)
            }
            (Some(c), None) => {
                self.error(c.span, "an optional has no error value to capture; use `x else d`");
                None
            }
            _ => None,
        };
        // An error value on the right gets its error type from the context (§10.4).
        let rhs_expect = match (&rhs.kind, expect) {
            (ExprKind::Unary { op: ast::UnOp::MakeErr, .. }, Some(t)) if matches!(t.kind(), TyKind::Result { .. }) => {
                Some(t)
            }
            (ExprKind::Unary { op: ast::UnOp::MakeErr, .. }, _) => match self.ret.kind() {
                TyKind::Result { err, .. } => Some(Ty::result(*err, present)),
                _ => None,
            },
            // A result with an optional fallback drops the error: `parse(s) else null`.
            (ExprKind::Null, _) if err.is_some() => Some(Ty::optional(present)),
            _ => Some(present),
        };
        let r = self.expr(rhs, rhs_expect);
        self.pop_scope();
        let rt = self.infer.resolve(r.ty);
        let (ty, r) = if rt.is_never() {
            (present, r)
        } else if rt == lt && err.is_none() {
            (lt, r)
        } else if matches!(rt.kind(), TyKind::Result { ok, .. } if *ok == present)
            || (err.is_some() && rt == Ty::optional(present))
        {
            (rt, r)
        } else {
            let r = self.coerce(r, present);
            (present, r)
        };
        mk(ty, tir::ExprKind::Else { lhs: Box::new(l), capture: cap, rhs: Box::new(r) }, span)
    }

    /// Checks the fallback of an `else` whose left side has an error: its own errors are
    /// reported, and a `break` in it still counts for its loop.
    fn else_after_error(&mut self, capture: Option<ast::Ident>, rhs: &ast::Expr) {
        self.push_scope();
        if let Some(c) = capture {
            let id = self.new_local(c.name, Ty::error(), false, c.span);
            self.declare(c.name, super::func::LocalBinding::Var(id), c.span);
        }
        self.expr(rhs, None);
        self.pop_scope();
    }

    /// `try x` (§10.5).
    fn try_expr(&mut self, operand: &ast::Expr, span: Span) -> Expr {
        if self.in_defer {
            self.error(span, "`try` is not allowed in a deferred statement");
        }
        let v = self.expr(operand, None);
        let vt = self.infer.resolve(v.ty);
        let (levels, ret) = self.return_target();
        self.check_fn_clause_exit(levels, "try", span);
        match (vt.kind(), ret.kind()) {
            (TyKind::Error, _) => error_expr(span),
            (TyKind::Optional(t), TyKind::Optional(_)) => {
                mk(*t, tir::ExprKind::Try { expr: Box::new(v), lift: None, levels }, span)
            }
            (TyKind::Result { err, ok }, TyKind::Result { err: f, .. }) => {
                let lift = if err == f {
                    None
                } else {
                    match self.lift_variant(*err, *f) {
                        Ok(i) => Some(tir::Lift::Variant(i)),
                        Err(msg) => {
                            self.error(span, msg);
                            None
                        }
                    }
                };
                mk(*ok, tir::ExprKind::Try { expr: Box::new(v), lift, levels }, span)
            }
            (TyKind::Optional(_), TyKind::Result { .. }) | (TyKind::Result { .. }, TyKind::Optional(_)) => {
                self.error(
                    span,
                    "`try` does not convert between optionals and results; give the absent value an error: `try (x else !.Missing)`",
                );
                error_expr(span)
            }
            (TyKind::Optional(_) | TyKind::Result { .. }, _)
                if self.sema.in_test && levels as usize == self.outer.len() =>
            {
                let mut d = crate::diag::Diagnostic::error(
                    span,
                    "a test returns no value, so `try` cannot propagate from it; write `ok(x)` to fail the test on an error (§3.8)",
                );
                // `try x` becomes `ok(x)` (only `ok` of a result is defined).
                if matches!(vt.kind(), TyKind::Result { .. }) {
                    let edit =
                        |lo, hi, text: &str| crate::diag::Edit { span: Span { lo, hi, ..span }, text: text.into() };
                    d = d.with_fix_edits(
                        "write `ok(x)`",
                        vec![edit(span.lo, operand.span.lo, "ok("), edit(operand.span.hi, operand.span.hi, ")")],
                    );
                }
                self.sema.emit(d);
                error_expr(span)
            }
            (TyKind::Optional(_) | TyKind::Result { .. }, _) => {
                self.error(span, format!("`try` needs the function to return an optional or a result, not `{}`", ret));
                error_expr(span)
            }
            _ => {
                self.error(span, format!("`try` needs an optional or a result, but this is `{}`", vt));
                error_expr(span)
            }
        }
    }

    /// The variant of union `f` whose payload is `e`, for error lifting (§10.6).
    pub fn lift_variant(&mut self, e: Ty, f: Ty) -> Result<u32, String> {
        let Some((variants, _)) = self.sema.union_variants(f) else {
            return Err(format!(
                "cannot propagate an error of type `{}` from a function whose error type is `{}`",
                e, f
            ));
        };
        let matches: Vec<usize> =
            variants.iter().enumerate().filter(|(_, v)| v.payload == Some(e)).map(|(i, _)| i).collect();
        match matches[..] {
            [i] => Ok(i as u32),
            [] => Err(format!("`{}` has no variant carrying `{}`, so the error cannot be lifted (§10.6)", f, e)),
            _ => Err(format!("`{}` has several variants carrying `{}`; handle the error explicitly", f, e)),
        }
    }

    // --- control flow ---

    /// Checks an `if`. If `value` is false the branches are statements and the
    /// result is `()` (or `never` if every branch diverges).
    pub fn if_expr(&mut self, i: &ast::IfExpr, value: bool, expect: Option<Ty>) -> Expr {
        let value = value && i.else_.is_some();
        let mut arms: Vec<(Vec<tir::CondPart>, Expr)> = vec![];
        let capture = match &i.else_ {
            Some(ast::IfElse::Capture { name, body }) => Some((*name, &**body)),
            _ => None,
        };
        // With `else e => d`, the last branch becomes a `match` on its result (§8.2).
        let mut captured: Option<(Expr, tir::Pat, Expr, tir::Pat)> = None;
        for (k, (cond, block)) in i.branches.iter().enumerate() {
            self.push_scope();
            if let Some((name, body)) = capture
                && k + 1 == i.branches.len()
            {
                let [ast::CondPart::Bind { pat, value: x }] = cond.parts.as_slice() else {
                    self.error(cond.span, "`else e =>` needs a condition that is a single binding on a result (§8.2)");
                    self.pop_scope();
                    return error_expr(i.span);
                };
                let tir::CondPart::Match { pat: p, value: v } = self.binding_cond(pat, x) else { unreachable!() };
                let then = self.block_expected(block, value, expect);
                self.pop_scope();
                let vt = self.infer.resolve(v.ty);
                let TyKind::Result { err, .. } = vt.kind() else {
                    if !vt.is_error() {
                        self.error(x.span, format!("`else e =>` captures the error of a result, but this is `{}`", vt));
                    }
                    return error_expr(i.span);
                };
                if !matches!(&p.kind, tir::PatKind::Ok(q) if self.checker().irrefutable(q)) {
                    self.error(pat.span, "with `else e =>`, the condition may fail only on an error (§8.2)");
                }
                self.push_scope();
                let id = self.new_local(name.name, *err, false, name.span);
                self.declare(name.name, super::func::LocalBinding::Var(id), name.span);
                let mut d = self.expr(body, if value { expect } else { None });
                self.pop_scope();
                if !value && !d.ty.is_never() {
                    // As a statement, the fallback's value is discarded.
                    let sp = d.span;
                    d = mk(
                        Ty::unit(),
                        tir::ExprKind::Block(Box::new(tir::Block {
                            stmts: vec![tir::Stmt::Expr(d)],
                            tail: None,
                            span: sp,
                        })),
                        sp,
                    );
                }
                let err_pat = tir::Pat {
                    ty: vt,
                    kind: tir::PatKind::Err(Box::new(tir::Pat {
                        ty: *err,
                        kind: tir::PatKind::Bind(id),
                        span: name.span,
                    })),
                    span: name.span,
                };
                captured = Some((v, p, then, err_pat));
                // The fallback joins like an `else` block.
                arms.push((vec![], d));
                continue;
            }
            let c = self.cond(cond);
            let b = self.block_expected(block, value, expect);
            self.pop_scope();
            arms.push((c, b));
        }
        // `captured` holds the last branch; `arms` ends with its fallback.
        let else_ = match &i.else_ {
            Some(ast::IfElse::Block(b)) => Some(self.block_expected(b, value, expect)),
            _ => None,
        };
        let else_ = match captured.as_ref() {
            Some(_) => arms.pop().map(|(_, d)| d),
            None => else_,
        };
        // Join branch types (§7.12).
        let ty = if value {
            let mut tys: Vec<Ty> = arms.iter().map(|(_, b)| b.ty).collect();
            if let Some((_, _, then, _)) = &captured {
                tys.push(then.ty);
            }
            if let Some(e) = &else_ {
                tys.push(e.ty);
            }
            match expect {
                Some(t) => t,
                None => self.join_type(&tys),
            }
        } else {
            let all_never = else_.as_ref().is_some_and(|e| e.ty.is_never())
                && arms.iter().all(|(_, b)| b.ty.is_never())
                && captured.as_ref().is_none_or(|(_, _, then, _)| then.ty.is_never());
            if all_never { Ty::never() } else { Ty::unit() }
        };
        let mut arms: Vec<(Vec<tir::CondPart>, Expr)> =
            if value { arms.into_iter().map(|(c, b)| (c, self.join_branch(b, ty))).collect() } else { arms };
        let mut else_ = if value { else_.map(|e| self.join_branch(e, ty)) } else { else_ };
        if let Some((v, p, then, err_pat)) = captured {
            let then = if value { self.join_branch(then, ty) } else { then };
            let d = else_.take().expect("a captured fallback");
            let span = then.span;
            let arms =
                vec![tir::Arm { pat: p, guard: None, body: then }, tir::Arm { pat: err_pat, guard: None, body: d }];
            else_ = Some(mk(ty, tir::ExprKind::Match { scrutinee: Box::new(v), arms }, span));
        }
        // Build the chain from the end.
        while let Some((cond, then)) = arms.pop() {
            let span = then.span;
            let e = mk(ty, tir::ExprKind::If { cond, then: Box::new(then), else_: else_.take().map(Box::new) }, span);
            else_ = Some(e);
        }
        // With a capture and no earlier branch, the whole `if` is the `match`.
        let mut r = else_.unwrap_or_else(|| unit(i.span));
        r.span = i.span;
        r
    }

    /// The type of a value `if` or `match` without an expected type (§7.12): the first
    /// branch type other than `never` to which every other branch coerces, or failing
    /// that the first one (and the others are reported where they are coerced to it).
    pub fn join_type(&mut self, tys: &[Ty]) -> Ty {
        let live: Vec<(Ty, Ty)> = tys.iter().filter(|t| !t.is_never()).map(|&t| (t, self.infer.resolve(t))).collect();
        let Some(&(first, _)) = live.first() else { return Ty::never() };
        for &(t, rt) in &live {
            if rt.has_infer() {
                continue;
            }
            let joins = live.iter().all(|&(_, u)| u == rt || (!u.has_infer() && self.coercion_steps(u, rt).is_some()));
            if joins {
                return t;
            }
        }
        first
    }

    fn join_branch(&mut self, b: Expr, ty: Ty) -> Expr {
        if ty.is_never() {
            return b;
        }
        self.coerce(b, ty)
    }

    /// The function a `return` or `try` leaves: the innermost frame that is not a
    /// handler clause (§8.7). Returns how many frames up it is and its result type.
    pub fn return_target(&self) -> (u32, Ty) {
        if self.clause.is_none() {
            return (0, self.ret);
        }
        for (levels, f) in (1..).zip(self.outer.iter().rev()) {
            if f.clause.is_none() {
                return (levels, f.ret);
            }
        }
        (0, self.ret)
    }

    /// An exit `levels` frames out may not leave a clause of a `fn` operation, which
    /// can only produce a value (§12.7.1).
    fn check_fn_clause_exit(&mut self, levels: u32, what: &str, span: Span) {
        let n = self.outer.len();
        let crossed = std::iter::once(self.clause.as_ref()).chain(
            (0..levels.saturating_sub(1) as usize)
                .map(|i| n.checked_sub(i + 1).and_then(|k| self.outer[k].clause.as_ref())),
        );
        if levels > 0 && crossed.flatten().any(|c| c.op_kind == ast::OpKind::Fn) {
            self.error(span, format!("a clause of a `fn` operation can only produce a value; it cannot `{}`", what));
        }
    }

    fn return_expr(&mut self, v: Option<&ast::Expr>, span: Span) -> Expr {
        if self.in_defer {
            self.error(span, "`return` is not allowed in a deferred statement");
        }
        let (levels, ret) = self.return_target();
        self.check_fn_clause_exit(levels, "return", span);
        if let Some(open) = self.open_ret_at(levels) {
            return self.open_return(v, levels, open, span);
        }
        let value = match v {
            Some(v) => {
                let ex = self.expr(v, Some(ret));
                self.coerce(ex, ret)
            }
            None => {
                if !ret.is_unit() && !ret.is_error() {
                    self.error(span, format!("`return` needs a value of type `{}`", ret));
                }
                unit(span)
            }
        };
        mk(Ty::never(), tir::ExprKind::Return { levels, value: Box::new(value) }, span)
    }

    fn open_ret_at(&self, levels: u32) -> Option<Option<Ty>> {
        match levels {
            0 => self.open_ret,
            n => self.outer[self.outer.len() - n as usize].open_ret,
        }
    }

    /// A `return` in a literal whose return type comes from its `return`s: the first one
    /// with a value fixes the type, the others convert to it.
    fn open_return(&mut self, v: Option<&ast::Expr>, levels: u32, fixed: Option<Ty>, span: Span) -> Expr {
        let value = match (v, fixed) {
            (Some(v), None) => {
                let ex = self.expr(v, None);
                if !ex.ty.is_never() && !ex.ty.is_error() {
                    self.set_open_ret(levels, ex.ty);
                }
                ex
            }
            (Some(v), Some(t)) => {
                let ex = self.expr(v, Some(t));
                self.coerce(ex, t)
            }
            (None, fixed) => {
                match fixed {
                    Some(t) if !self.infer.resolve(t).is_unit() && !t.is_error() => {
                        self.error(span, format!("`return` needs a value of type `{}`", t));
                    }
                    Some(_) => {}
                    None => self.set_open_ret(levels, Ty::unit()),
                }
                unit(span)
            }
        };
        mk(Ty::never(), tir::ExprKind::Return { levels, value: Box::new(value) }, span)
    }

    fn set_open_ret(&mut self, levels: u32, t: Ty) {
        match levels {
            0 => self.open_ret = Some(Some(t)),
            n => {
                let i = self.outer.len() - n as usize;
                self.outer[i].open_ret = Some(Some(t));
            }
        }
    }

    /// Resolves the target of `break` / `continue` (§8.5, §12.7.1).
    fn find_jump(&mut self, label: Option<ast::Ident>, span: Span, is_continue: bool) -> Option<Jump> {
        // The current frame first.
        let here = match label {
            Some(l) => self.labels.iter().rposition(|x| x.name == Some(l.name)),
            None => self.labels.iter().rposition(|x| x.is_loop),
        };
        if let Some(idx) = here {
            if self.in_defer && idx < self.defer_label_base {
                self.error(span, "a deferred statement may not `break` or `continue` out of itself");
            }
            if is_continue && !self.labels[idx].is_loop {
                self.error(span, "`continue` can only target a loop");
            }
            let info = &self.labels[idx];
            return Some(Jump::Label { levels: 0, label: info.id, value: info.value });
        }
        if self.in_defer {
            self.error(span, "a deferred statement may not `break` or `continue` out of itself");
            return None;
        }
        // At clause level: exit the installing `with`, or (for `continue`) finish the clause.
        let Some(clause) = self.clause.clone() else {
            match label {
                Some(l) => self.error(l.span, format!("no enclosing loop or `with` is labeled `{}`", l.name)),
                None => self.error(span, "`break` and `continue` must be inside a loop"),
            }
            return None;
        };
        let Some(l) = label else {
            if clause.op_kind == ast::OpKind::Fn {
                self.error(
                    span,
                    "a clause of a `fn` operation can only produce a value; it cannot `break` or `continue`",
                );
                return None;
            }
            if is_continue && clause.op_kind == ast::OpKind::Ctl && clause.for_label.is_none() {
                self.error(
                    span,
                    "a `ctl` clause cannot `continue`: end it with `resume`, `break` or an exit (§12.7.1)",
                );
                return None;
            }
            // `continue` produces `()` as the operation's result (§12.7.1).
            let ret = self.infer.resolve(self.ret);
            if is_continue && !ret.is_unit() && !ret.is_error() {
                self.error(
                    span,
                    format!("`continue` finishes the clause with `()`, but the operation returns `{}` (§12.7.1)", ret),
                );
                return None;
            }
            return Some(if is_continue { Jump::ClauseReturn { levels: 0 } } else { Jump::InstallingWith });
        };
        // A labeled jump from a handler clause to a construct enclosing the handler.
        let mut frame_clause = Some(clause);
        let n = self.outer.len();
        for (levels, k) in (0..).zip((0..n).rev()) {
            let Some(c) = &frame_clause else { break };
            if is_continue
                && let Some((Some(name), _)) = c.for_label
                && name == l.name
            {
                self.check_fn_clause_exit(levels + 1, "continue", span);
                return Some(Jump::ClauseReturn { levels });
            }
            let f = &self.outer[k];
            if let Some(idx) = f.labels.iter().rposition(|x| x.name == Some(l.name)) {
                let (is_loop, id, value) = (f.labels[idx].is_loop, f.labels[idx].id, f.labels[idx].value);
                if is_continue && !is_loop {
                    self.error(span, "`continue` can only target a loop");
                }
                self.check_fn_clause_exit(levels + 1, if is_continue { "continue" } else { "break" }, span);
                return Some(Jump::Label { levels: levels + 1, label: id, value });
            }
            frame_clause = f.clause.clone();
        }
        self.error(l.span, format!("no enclosing loop or `with` is labeled `{}`", l.name));
        None
    }

    fn break_expr(&mut self, label: Option<ast::Ident>, value: Option<&ast::Expr>, span: Span) -> Expr {
        let Some(jump) = self.find_jump(label, span, false) else { return error_expr(span) };
        let clause_break_ty = self.clause.as_ref().and_then(|c| c.break_ty);
        // Every `break` to a label is recorded: a `while true` loop finishes only if one
        // targets it, and a `with` checks the values.
        if let Jump::Label { levels, label, value: LabelValue::None | LabelValue::Loop(_) } = jump {
            let depth = self.outer.len() - levels as usize;
            self.label_breaks.entry((depth, label)).or_default().push((Ty::unit(), span));
        }
        let value = match (jump, value) {
            (Jump::Label { value: LabelValue::None, .. }, Some(v)) => {
                self.error(v.span, "a `while` loop has no value, so `break` takes no operand here");
                None
            }
            (Jump::Label { value: LabelValue::None, .. }, None) | (Jump::ClauseReturn { .. }, _) => None,
            // A `break` that exits a `for` over an iterator (§13.2).
            (Jump::Label { value: LabelValue::Loop(rt), .. }, v) => self.loop_break_value(v, rt, span),
            (Jump::InstallingWith, v) if let Some(rt) = clause_break_ty => self.loop_break_value(v, rt, span),
            // A `break` that exits a `with` (§12.7): its value is checked against the
            // `with`'s once the `with` is finished.
            (Jump::Label { levels, label, value: LabelValue::With(expect) }, v) => {
                let ex = v.map(|v| {
                    let ex = self.expr(v, expect);
                    match expect {
                        Some(t) => self.coerce(ex, t),
                        None => ex,
                    }
                });
                let ty = ex.as_ref().map_or(Ty::unit(), |e| expect.unwrap_or(e.ty));
                let depth = self.outer.len() - levels as usize;
                self.label_breaks.entry((depth, label)).or_default().push((ty, span));
                ex.map(Box::new)
            }
            (Jump::InstallingWith, v) => {
                let expect = self.clause.as_ref().and_then(|c| c.with_expect);
                let ex = v.map(|v| {
                    let ex = self.expr(v, expect);
                    match expect {
                        Some(t) => self.coerce(ex, t),
                        None => ex,
                    }
                });
                let ty = ex.as_ref().map_or(Ty::unit(), |e| expect.unwrap_or(e.ty));
                if let Some(h) = self.clause.as_ref().and_then(|c| c.handler) {
                    self.sema.handler_breaks.entry(h.0).or_default().push((ty, span));
                }
                if let Some(f) = self.clause.as_ref().and_then(|c| c.decl_clause) {
                    let ty = self.infer.resolve(ty);
                    self.sema.decl_clause_breaks.entry(f).or_default().push((ty, span));
                }
                ex.map(Box::new)
            }
        };
        match jump {
            Jump::Label { levels, label, .. } => {
                mk(Ty::never(), tir::ExprKind::Break { target: Target::Label { levels, label }, value }, span)
            }
            Jump::InstallingWith => {
                mk(Ty::never(), tir::ExprKind::Break { target: Target::InstallingWith, value }, span)
            }
            Jump::ClauseReturn { .. } => error_expr(span),
        }
    }

    /// The value of a `break` out of a `for` loop whose iterator returns `rt`.
    fn loop_break_value(&mut self, v: Option<&ast::Expr>, rt: Ty, span: Span) -> Option<Box<Expr>> {
        match v {
            Some(v) => {
                let ex = self.expr(v, Some(rt));
                Some(Box::new(self.coerce(ex, rt)))
            }
            None => {
                if !rt.is_unit() && !rt.is_error() {
                    self.error(span, format!("this loop's value is `{}`, so `break` needs a value", rt));
                }
                None
            }
        }
    }

    fn continue_expr(&mut self, label: Option<ast::Ident>, span: Span) -> Expr {
        match self.find_jump(label, span, true) {
            Some(Jump::Label { levels, label, .. }) => {
                mk(Ty::never(), tir::ExprKind::Continue { target: Target::Label { levels, label } }, span)
            }
            // `continue` at clause level finishes the clause with `()` (§12.7.1).
            Some(Jump::ClauseReturn { levels }) => {
                mk(Ty::never(), tir::ExprKind::Return { levels, value: Box::new(unit(span)) }, span)
            }
            Some(Jump::InstallingWith) | None => error_expr(span),
        }
    }

    // --- coercions (§6.4) ---

    /// Converts `e` to `target` at a coercion site, or reports a type mismatch.
    pub fn coerce(&mut self, e: Expr, target: Ty) -> Expr {
        let src = self.infer.resolve(e.ty);
        let target = self.infer.resolve(target);
        if src == target || src.is_error() || target.is_error() {
            return e;
        }
        // An array where a `[]const T` is expected is a slice of the whole array (§6.4).
        if let (TyKind::Array { elem: a, .. }, TyKind::Slice { is_const: true, elem: b }) = (src.kind(), target.kind())
            && src.is_positional_array()
            && self.infer.unify(*a, *b).is_ok()
        {
            let v = self.array_as_slice(e, target);
            return self.coerce(v, target);
        }
        if let (TyKind::Slice { is_const: true, .. }, TyKind::Slice { is_const: false, .. }) =
            (src.kind(), target.kind())
            && super::seq::is_held_temp(&e)
        {
            let d = crate::diag::Diagnostic::error(e.span, format!("expected `{}`, found `{}`", target, src)).with_note(
                "a slice of a temporary array is read-only; to pass the array for mutation, put it in a variable first (§7.2)",
            );
            self.sema.emit(d);
            return e;
        }
        if let Some(c) = self.soa_coerce(&e, src, target) {
            return c;
        }
        // `z: Complex(f64) = 1`: a real constant where a complex number is expected (§6.4).
        if let Some(elem) = self.sema.complex_of(target)
            && let Some(p) = self.promote_real_constant(&e, target, elem)
        {
            return p;
        }
        if src.is_never() {
            let span = e.span;
            return mk(target, tir::ExprKind::Coerce { kind: Coercion::FromNever, expr: Box::new(e) }, span);
        }
        if src.is_infer() {
            // Decided after inference: the literal's type becomes the target, or the
            // target's innermost wrapped type (§6.1).
            let span = e.span;
            self.pending.push(PendingCoercion { source: src, target, span });
            return mk(target, tir::ExprKind::Coerce { kind: Coercion::Unresolved, expr: Box::new(e) }, span);
        }
        // A tuple with unresolved elements, to the same tuple with or without names (§5.9).
        if let (TyKind::Tuple { elems: a, names: na }, TyKind::Tuple { elems: b, names: nb }) =
            (src.kind(), target.kind())
            && (src.has_infer() || target.has_infer())
            && a.len() == b.len()
            && na != nb
            && (na.is_none() || nb.is_none())
        {
            let span = e.span;
            if a.iter().zip(b.iter()).any(|(x, y)| self.infer.unify(*x, *y).is_err()) {
                let found = self.infer.resolve(src);
                self.error(span, format!("expected `{}`, found `{}`", target, found));
                return e;
            }
            return mk(target, tir::ExprKind::Coerce { kind: Coercion::TupleRename, expr: Box::new(e) }, span);
        }
        if src.has_infer() || target.has_infer() {
            let span = e.span;
            if self.infer.unify(src, target).is_err() {
                let (s, t) = (self.infer.resolve(src), self.infer.resolve(target));
                self.error(span, format!("expected `{}`, found `{}`", t, s));
            }
            return e;
        }
        // A function pointer (or named function) passed where a second-class
        // function is expected: its effects outside the expected set are handled
        // where it is written (§11.6).
        if let (TyKind::FnPtr(a), TyKind::Fn(b)) = (src.kind(), target.kind())
            && a.params == b.params
            && a.ret == b.ret
        {
            let span = e.span;
            if a.callconv != CallConv::C {
                self.error(
                    span,
                    format!(
                        "`{}` has a calling convention other than C, so it cannot be a second-class `fn` value (§19.2)",
                        src
                    ),
                );
                return e;
            }
            for eff in a.effects.effects.iter() {
                if !b.effects.contains(eff) {
                    self.performed.push((eff.clone(), span));
                }
            }
            return mk(target, tir::ExprKind::Coerce { kind: Coercion::FnToPtr, expr: Box::new(e) }, span);
        }
        // `^Outer` → `^S` through `using` fields (§5.10.3); free, like the other pointer
        // coercions.
        if self.embeds(src, target) {
            let span = e.span;
            return mk(target, tir::ExprKind::Coerce { kind: Coercion::PtrToPtr, expr: Box::new(e) }, span);
        }
        // A second-class function may stand where one with more effects is expected: the
        // callee handles those effects for it (§11.6, §12.3).
        if let (TyKind::Fn(a), TyKind::Fn(b)) = (src.kind(), target.kind())
            && a.params == b.params
            && a.ret == b.ret
            && a.effects.is_subset_of(&b.effects)
        {
            let span = e.span;
            return mk(target, tir::ExprKind::Coerce { kind: Coercion::FnToPtr, expr: Box::new(e) }, span);
        }
        match self.coercion_steps(src, target) {
            Some(steps) => apply_steps(e, steps),
            None => {
                self.error(e.span, format!("expected `{}`, found `{}`", target, src));
                e
            }
        }
    }

    /// The coercions converting `src` to `target`, if any: at most one base coercion
    /// followed by any number of wrapping steps (§6.4).
    pub fn coercion_steps(&mut self, src: Ty, target: Ty) -> Option<Vec<(Coercion, Ty)>> {
        if src == target {
            return Some(vec![]);
        }
        if let Some(k) = base_coercion(src, target) {
            return Some(vec![(k, target)]);
        }
        // Error lifting `E!T` → `F!T` (§10.6).
        if let (TyKind::Result { err: e, ok: o1 }, TyKind::Result { err: f, ok: o2 }) = (src.kind(), target.kind())
            && o1 == o2
            && let Ok(i) = self.lift_variant(*e, *f)
        {
            return Some(vec![(Coercion::LiftErr { variant: i }, target)]);
        }
        match target.kind() {
            TyKind::Optional(inner) => {
                let mut steps = self.coercion_steps(src, *inner)?;
                steps.push((Coercion::WrapOptional, target));
                Some(steps)
            }
            TyKind::Result { ok, .. } => {
                let mut steps = self.coercion_steps(src, *ok)?;
                steps.push((Coercion::WrapOk, target));
                Some(steps)
            }
            _ => None,
        }
    }
}

pub fn apply_steps(mut e: Expr, steps: Vec<(Coercion, Ty)>) -> Expr {
    for (kind, ty) in steps {
        let span = e.span;
        e = mk(ty, tir::ExprKind::Coerce { kind, expr: Box::new(e) }, span);
    }
    e
}

/// A single non-wrapping coercion from `src` to `target` (§6.4), if one applies.
pub fn base_coercion(src: Ty, target: Ty) -> Option<Coercion> {
    use TyKind::*;
    match (src.kind(), target.kind()) {
        (Never, _) => Some(Coercion::FromNever),
        (Int(a), Int(b)) => {
            let ok = match (a.signed, b.signed) {
                (false, false) | (true, true) => b.bits >= a.bits,
                (false, true) => b.bits > a.bits,
                (true, false) => false,
            };
            ok.then_some(Coercion::IntWiden)
        }
        (Int(a), Usize) => (!a.signed && a.bits <= 32).then_some(Coercion::IntWiden),
        (Int(a), Isize) => ((!a.signed && a.bits <= 31) || (a.signed && a.bits <= 32)).then_some(Coercion::IntWiden),
        (Float(a), Float(b)) => (b.bits() > a.bits()).then_some(Coercion::FloatWiden),
        (Tuple { elems: a, names: na }, Tuple { elems: b, names: nb }) => {
            (a == b && na != nb && (na.is_none() || nb.is_none())).then_some(Coercion::TupleRename)
        }
        (Optional(a), Optional(b)) if crate::layout::is_nullable_pointer(*a) => {
            pointer_coercion(*a, *b).then_some(Coercion::InOptional)
        }
        (FnPtr(a), FnPtr(b)) => (a.params == b.params
            && a.ret == b.ret
            && a.variadic == b.variadic
            && a.callconv == b.callconv
            && a.effects != b.effects
            && a.effects.is_subset_of(&b.effects))
        .then_some(Coercion::FnPtrWiden),
        _ => {
            if pointer_coercion(src, target) {
                let array_to_slice =
                    matches!((src.kind(), target.kind()), (Ptr { pointee, .. }, Slice { .. }) if pointee.is_array());
                Some(if array_to_slice { Coercion::ArrayPtrToSlice } else { Coercion::PtrToPtr })
            } else {
                None
            }
        }
    }
}

/// True if a pointer to `t` may become a pointer to its first element or a slice of it:
/// `t` is not an array (or an array reached by position, §5.6.1, §5.6.2).
fn src_array_ok(t: Ty) -> bool {
    !t.is_array() || t.is_positional_array()
}

/// The pointer coercions of §5.5 (a sequence of them counts as one).
fn pointer_coercion(src: Ty, target: Ty) -> bool {
    use TyKind::*;
    let (sc, se, sk) = match src.kind() {
        Ptr { is_const, pointee } => (*is_const, *pointee, 0),
        ManyPtr { is_const, elem } => (*is_const, *elem, 1),
        Slice { is_const, elem } => (*is_const, *elem, 2),
        _ => return false,
    };
    if matches!(target.kind(), Rawptr) {
        return sk != 2;
    }
    let (tc, te, tk) = match target.kind() {
        Ptr { is_const, pointee } => (*is_const, *pointee, 0),
        ManyPtr { is_const, elem } => (*is_const, *elem, 1),
        Slice { is_const, elem } => (*is_const, *elem, 2),
        _ => return false,
    };
    if sc && !tc {
        return false;
    }
    match (sk, tk) {
        (0, 0) | (1, 1) | (2, 2) => se == te,
        // ^T → [^]T, and ^[N]T → [^]T
        (0, 1) => {
            se == te
                || (src_array_ok(se)
                    && matches!(se.kind(), Array { elem, .. } | ParamArray { elem, .. } if *elem == te))
        }
        // ^[N]T → []T
        (0, 2) => src_array_ok(se) && matches!(se.kind(), Array { elem, .. } | ParamArray { elem, .. } if *elem == te),
        _ => false,
    }
}

fn is_literal(e: &ast::Expr) -> bool {
    match &e.kind {
        ExprKind::Int(_) | ExprKind::Float(_) => true,
        ExprKind::Paren(inner) => is_literal(inner),
        _ => false,
    }
}

pub fn suffix_type(s: crate::syntax::token::IntSuffix) -> Ty {
    use crate::syntax::token::IntSuffix;
    match s {
        IntSuffix::U(n) => Ty::int(false, n.min(128) as u16),
        IntSuffix::I(n) => Ty::int(true, n.min(128) as u16),
        IntSuffix::Usize => Ty::usize(),
        IntSuffix::Isize => Ty::isize(),
    }
}

/// Where a `break` or `continue` goes.
#[derive(Clone, Copy)]
enum Jump {
    /// A loop or `with` of the function `levels` frames up, and what a `break` to
    /// it supplies.
    Label { levels: u32, label: tir::LabelId, value: LabelValue },
    /// Out of the `with` that installed the running clause's handler.
    InstallingWith,
    /// Finish the clause `levels` frames up, producing `()`.
    ClauseReturn { levels: u32 },
}

impl<'a, 's> FnCx<'a, 's> {
    /// Whether `is` and `@hash` may be used at type `t` here. The core library's
    /// containers use them on unconstrained parameters: their public
    /// constructors require `Eq` exactly when the default hash and equality are
    /// used (§21.5).
    /// Where this expression is, for `@location()` and `@caller_location()`.
    pub fn call_site(&self, span: Span) -> (Span, crate::symbol::Symbol) {
        (span, self.location_function())
    }

    /// The function a source location names (§20.3): the innermost enclosing function
    /// with a name of its own, so code in a `for` body or a handler clause is located
    /// in the function that contains it.
    pub fn location_function(&self) -> crate::symbol::Symbol {
        let name = |f: tir::FnId| self.sema.prog.fns[f.index()].name;
        let frames = std::iter::once(self.fn_id).chain(self.outer.iter().rev().map(|o| o.fn_id));
        let mut last = None;
        for f in frames.flatten() {
            last = Some(name(f));
            if !name(f).as_str().starts_with('<') {
                return name(f);
            }
        }
        last.unwrap_or_else(|| crate::symbol::Symbol::intern(""))
    }

    /// Whether a field of visibility `vis` of type `t` can be named here (§3.3).
    pub fn field_visible(&self, vis: ast::Visibility, t: Ty) -> bool {
        match (vis, t.kind()) {
            (ast::Visibility::Public, _) => true,
            (_, TyKind::Adt(id, _)) => {
                self.sema.adts.get(id).is_none_or(|a| self.sema.visible(vis, a.module, self.module))
            }
            _ => true,
        }
    }

    /// The first field of struct type `t` that is not visible here, if any, with what
    /// the field is visible to: "module" or "package".
    pub fn hidden_field(&self, t: Ty) -> Option<(crate::symbol::Symbol, &'static str)> {
        let f = self.sema.struct_fields(t)?.iter().find(|f| !self.field_visible(f.vis, t))?;
        Some((f.name, if f.vis == ast::Visibility::Package { "package" } else { "module" }))
    }

    pub fn eq_allowed(&self, t: Ty) -> bool {
        self.sema.has_eq(t) || (self.sema.modules[self.module.0 as usize].is_prelude && super::generics::has_param(t))
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// The atomic (§19.6) and volatile (§19.5) intrinsics.
    fn atomic(&mut self, name: &str, args: &[ast::Arg], span: Span) -> Expr {
        use tir::{Builtin as B, RmwOp};
        let (op, n_values, n_orders, writes) = match name {
            "atomic_load" => (B::AtomicLoad, 0, 1, false),
            "atomic_store" => (B::AtomicStore, 1, 1, true),
            "atomic_swap" => (B::AtomicSwap, 1, 1, true),
            "atomic_cas" => (B::AtomicCas, 2, 2, true),
            "atomic_add" => (B::AtomicRmw(RmwOp::Add), 1, 1, true),
            "atomic_sub" => (B::AtomicRmw(RmwOp::Sub), 1, 1, true),
            "atomic_and" => (B::AtomicRmw(RmwOp::And), 1, 1, true),
            "atomic_or" => (B::AtomicRmw(RmwOp::Or), 1, 1, true),
            "atomic_xor" => (B::AtomicRmw(RmwOp::Xor), 1, 1, true),
            "atomic_max" => (B::AtomicRmw(RmwOp::Max), 1, 1, true),
            "atomic_min" => (B::AtomicRmw(RmwOp::Min), 1, 1, true),
            "fence" => (B::Fence, 0, 1, false),
            "volatile_load" => (B::VolatileLoad, 0, 0, false),
            _ => (B::VolatileStore, 1, 0, true),
        };
        let has_ptr = op != B::Fence;
        let expected = has_ptr as usize + n_values + n_orders;
        let exprs: Vec<&ast::Expr> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ast::ArgKind::Positional(e) => Some(e),
                _ => None,
            })
            .collect();
        if exprs.len() != expected || exprs.len() != args.len() {
            self.error(span, format!("`@{}` takes {} positional arguments", name, expected));
            return error_expr(span);
        }
        let order_ty = self.memory_order_ty();
        let mut vals = vec![];
        let mut elem = Ty::unit();
        if has_ptr {
            let p = self.expr(exprs[0], None);
            // The operation depends on the pointee type, so an untyped literal's type is
            // settled here (`x := 0; @atomic_add(&x, 1, .SeqCst)`, §6.1).
            let pt = self.settle(p.ty, p.span);
            match pt.kind() {
                TyKind::Ptr { is_const, pointee } => {
                    if writes && *is_const {
                        self.error(
                            exprs[0].span,
                            format!("`@{}` writes through the pointer, so it needs a `^T`, not `{}`", name, pt),
                        );
                    }
                    elem = *pointee;
                }
                TyKind::Error => return error_expr(span),
                _ => {
                    self.error(exprs[0].span, format!("`@{}` needs a pointer `^T`, not `{}`", name, pt));
                    return error_expr(span);
                }
            }
            let volatile = matches!(op, B::VolatileLoad | B::VolatileStore);
            let int = matches!(elem.kind(), TyKind::Int(i) if matches!(i.bits, 8 | 16 | 32 | 64))
                || matches!(elem.kind(), TyKind::Usize | TyKind::Isize);
            let ptr = matches!(elem.kind(), TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr)
                || matches!(elem.kind(), TyKind::Optional(t) if crate::layout::is_nullable_pointer(*t));
            let ok = match op {
                B::AtomicRmw(_) => int,
                _ if volatile => int || ptr || elem.is_float() || self.sema.bits_struct_ty(elem).is_some(),
                _ => int || ptr || elem.is_bool(),
            };
            if !ok {
                self.error(exprs[0].span, format!("`@{}` is not defined on `{}`", name, elem));
            }
            vals.push(p);
            if volatile {
                let sys = self.sema.sys_set();
                self.record_effects(&sys, span);
            }
        }
        let first_value = has_ptr as usize;
        for e in &exprs[first_value..first_value + n_values] {
            let v = self.expr(e, Some(elem));
            vals.push(self.coerce(v, elem));
        }
        for (k, e) in exprs[first_value + n_values..].iter().enumerate() {
            let v = self.expr(e, Some(order_ty));
            let v = self.coerce(v, order_ty);
            // Orders C11 does not allow for this operation, when written as constants.
            if let tir::ExprKind::Variant { variant, payload: None } = &v.kind
                && let Some((variants, _)) = self.sema.union_variants(order_ty)
            {
                let order = variants[*variant as usize].name.as_str();
                let bad = match (op, k) {
                    (B::AtomicLoad, _) | (B::AtomicCas, 1) => matches!(order, "Release" | "AcqRel"),
                    (B::AtomicStore, _) => matches!(order, "Acquire" | "AcqRel"),
                    _ => false,
                };
                if bad {
                    let what =
                        if op == B::AtomicCas { "as the failure order of `@atomic_cas`" } else { "for this operation" };
                    self.error(e.span, format!("`.{}` is not a valid memory order {} (§19.6)", order, what));
                }
            }
            vals.push(v);
        }
        let ty = match op {
            B::AtomicLoad | B::AtomicSwap | B::AtomicRmw(_) | B::VolatileLoad => elem,
            B::AtomicCas => Ty::optional(elem),
            _ => Ty::unit(),
        };
        mk(ty, tir::ExprKind::Builtin { op, args: vals }, span)
    }

    /// `@asm(template, outputs, inputs, clobbers)` (§19.5).
    fn asm(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        let exprs: Vec<&ast::Expr> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ast::ArgKind::Positional(e) => Some(e),
                _ => None,
            })
            .collect();
        let [template, outputs, inputs, clobbers] = exprs.as_slice() else {
            self.error(
                span,
                "`@asm` takes a template, outputs, inputs and clobbers: `@asm(template, outputs, inputs, clobbers)`",
            );
            return error_expr(span);
        };
        if exprs.len() != args.len() {
            self.error(span, "`@asm` takes positional arguments only");
        }
        let sys = self.sema.sys_set();
        self.record_effects(&sys, span);
        let str_expr = |s: String, span: Span| mk(Ty::string(), tir::ExprKind::Str(Rc::from(s.as_bytes())), span);
        let mut vals = vec![];
        let mut ok = true;
        match self.const_str_here(template) {
            Some(s) => vals.push(str_expr(s, template.span)),
            None => {
                self.error(template.span, "the template of `@asm` must be a compile-time string");
                ok = false;
            }
        }
        let mut counts = [0u32; 2];
        for (k, list) in [outputs, inputs].into_iter().enumerate() {
            for entry in asm_entries(list, true) {
                let parts = match &entry.kind {
                    ast::ExprKind::Tuple(parts) => parts.as_slice(),
                    _ => &[],
                };
                let [c, operand] = parts else {
                    self.error(entry.span, "an `@asm` operand is a pair: `(constraint, operand)`");
                    ok = false;
                    continue;
                };
                let Some(cs) = self.const_str_here(&c.value) else {
                    self.error(c.value.span, "an `@asm` constraint must be a compile-time string");
                    ok = false;
                    continue;
                };
                let v = self.expr(&operand.value, None);
                let t = self.infer.resolve(v.ty);
                let scalar = |t: Ty| {
                    matches!(t.kind(), TyKind::Int(i) if matches!(i.bits, 8 | 16 | 32 | 64))
                        || matches!(
                            t.kind(),
                            TyKind::Usize
                                | TyKind::Isize
                                | TyKind::Bool
                                | TyKind::Rawptr
                                | TyKind::Ptr { .. }
                                | TyKind::ManyPtr { .. }
                                | TyKind::FnPtr(_)
                        )
                        || matches!(t.kind(), TyKind::Float(f) if *f != crate::types::FloatTy::F16)
                        || matches!(t.kind(), TyKind::Optional(p) if crate::layout::is_nullable_pointer(*p))
                };
                let fine = if k == 0 {
                    // An output is written through a pointer to its place.
                    matches!(t.kind(), TyKind::Ptr { is_const: false, pointee } if scalar(*pointee))
                } else {
                    scalar(t) || t.is_error()
                };
                if !fine && !t.is_error() {
                    let what = if k == 0 { "an output needs a `^T` pointer to" } else { "an input must be" };
                    self.error(
                        operand.value.span,
                        format!("{} an integer, float, `bool` or pointer, not `{}`", what, t),
                    );
                    ok = false;
                }
                vals.push(str_expr(cs, c.value.span));
                vals.push(v);
                counts[k] += 1;
            }
        }
        for c in asm_entries(clobbers, false) {
            match self.const_str_here(c) {
                Some(s) => vals.push(str_expr(s, c.span)),
                None => {
                    self.error(c.span, "an `@asm` clobber must be a compile-time string");
                    ok = false;
                }
            }
        }
        if !ok {
            return error_expr(span);
        }
        let op = tir::Builtin::Asm { outputs: counts[0], inputs: counts[1] };
        mk(Ty::unit(), tir::ExprKind::Builtin { op, args: vals }, span)
    }

    /// The value of `e` if it is a compile-time string, checked in the current scope.
    fn const_str_here(&mut self, e: &ast::Expr) -> Option<String> {
        let v = self.expr(e, Some(Ty::string()));
        if let tir::ExprKind::Str(b) = &v.kind {
            return Some(String::from_utf8_lossy(b).into_owned());
        }
        if v.ty.is_error() || !matches!(v.kind, tir::ExprKind::Const(_)) {
            return None;
        }
        let c = self.sema.eval_tir_const(&v)?;
        match self.sema.const_key(&c, e.span)? {
            crate::types::ConstKey::Str(s) => Some(String::from_utf8_lossy(&s).into_owned()),
            _ => None,
        }
    }

    /// The core library's `MemoryOrder`.
    fn memory_order_ty(&mut self) -> Ty {
        let d = self.sema.prelude.and_then(|p| {
            self.sema.modules[p.0 as usize].scope.get(&crate::symbol::Symbol::intern("MemoryOrder")).copied()
        });
        match d.map(|d| self.sema.resolve_decl(d)) {
            Some(DeclValue::Type(t)) => t,
            _ => Ty::error(),
        }
    }
}

/// The entries of an `@asm` operand or clobber list: `()`, one entry, or a tuple of
/// entries. An operand entry is itself a pair `(constraint, operand)`, so a pair whose
/// first element is a string literal is one entry.
fn asm_entries(list: &ast::Expr, pairs: bool) -> Vec<&ast::Expr> {
    let list = match &list.kind {
        ast::ExprKind::Paren(inner) => inner,
        _ => list,
    };
    match &list.kind {
        ast::ExprKind::Tuple(elems)
            if pairs && matches!(elems.first().map(|e| &e.value.kind), Some(ast::ExprKind::Str(_))) =>
        {
            vec![list]
        }
        ast::ExprKind::Tuple(elems) => elems.iter().map(|e| &e.value).collect(),
        _ => vec![list],
    }
}

/// The call at the end of a chain of type prefixes, as in `[2][3]i32(…)`, `?[2]u8(…)`
/// or `[3]^Node(…)`.
pub fn construction_call(e: &ast::Expr) -> Option<&ast::Expr> {
    match &e.kind {
        // `xs.first()` and `xs[0](…)` call something on a value: not a construction.
        ExprKind::Call { callee, .. } if calls_on_value(callee) => None,
        ExprKind::Call { .. } => Some(e),
        ExprKind::ArrayType { elem: inner, .. }
        | ExprKind::OptionalType(inner)
        | ExprKind::PtrType { inner, .. }
        | ExprKind::SliceType { inner, .. }
        | ExprKind::ManyPtrType { inner, .. } => construction_call(inner),
        _ => None,
    }
}

/// True if `callee` is a method or a function pointer reached from a value (`v.f`, `v[i]`
/// where `v` is more than a name path such as `m.T`), so calling it constructs nothing.
fn calls_on_value(callee: &ast::Expr) -> bool {
    fn is_path(e: &ast::Expr) -> bool {
        match &e.kind {
            ExprKind::Ident(_) => true,
            ExprKind::Field { base, .. } => is_path(base),
            _ => false,
        }
    }
    match &callee.kind {
        ExprKind::Field { base, .. } => !is_path(base),
        ExprKind::Index { .. } => true,
        _ => false,
    }
}

/// The type a construction `e` builds: `e` with its final call replaced by the callee.
fn without_call(e: &ast::Expr) -> ast::Expr {
    let mut t = e.clone();
    fn strip(e: &mut ast::Expr) {
        match &mut e.kind {
            ExprKind::Call { callee, .. } => *e = (**callee).clone(),
            ExprKind::ArrayType { elem: inner, .. }
            | ExprKind::OptionalType(inner)
            | ExprKind::PtrType { inner, .. }
            | ExprKind::SliceType { inner, .. }
            | ExprKind::ManyPtrType { inner, .. } => strip(inner),
            _ => {}
        }
    }
    strip(&mut t);
    t
}

/// A type prefix whose operand is a postfix chain on a construction, such as
/// `[2]P(a, b)[1].y`, rewritten so that the prefix applies to the construction and the
/// rest of the chain to the value it constructs: `(([2]P)(a, b))[1].y` (§A.7 rule 12).
pub fn hoist_prefix(e: &ast::Expr) -> Option<ast::Expr> {
    let (ExprKind::ArrayType { elem: inner, .. } | ExprKind::OptionalType(inner)) = &e.kind else { return None };
    if construction_call(inner).is_some() {
        return None;
    }
    // The operand of the innermost prefix: `[2]?u8(1, null)[0]` stacks two of them.
    fn prefix_operand(e: &ast::Expr) -> Option<&ast::Expr> {
        match &e.kind {
            ExprKind::ArrayType { elem: inner, .. }
            | ExprKind::OptionalType(inner)
            | ExprKind::PtrType { inner, .. }
            | ExprKind::SliceType { inner, .. }
            | ExprKind::ManyPtrType { inner, .. } => Some(inner),
            _ => None,
        }
    }
    fn operand(e: &ast::Expr) -> &ast::Expr {
        prefix_operand(e).map_or(e, operand)
    }
    fn operand_mut(e: &mut ast::Expr) -> &mut ast::Expr {
        if prefix_operand(e).is_none() {
            return e;
        }
        match &mut e.kind {
            ExprKind::ArrayType { elem: inner, .. }
            | ExprKind::OptionalType(inner)
            | ExprKind::PtrType { inner, .. }
            | ExprKind::SliceType { inner, .. }
            | ExprKind::ManyPtrType { inner, .. } => operand_mut(inner),
            _ => unreachable!(),
        }
    }
    fn push_down(prefix: &ast::Expr, inner: &ast::Expr) -> Option<ast::Expr> {
        let mut out = inner.clone();
        let next = match &mut out.kind {
            ExprKind::Index { base, .. }
            | ExprKind::Slice { base, .. }
            | ExprKind::Field { base, .. }
            | ExprKind::TupleField { base, .. }
            | ExprKind::OptUnwrap(base)
            | ExprKind::ResUnwrap(base)
            | ExprKind::Deref(base) => base,
            // A method call on the constructed value.
            ExprKind::Call { callee, .. } if calls_on_value(callee) => callee,
            _ => return None,
        };
        **next = match &next.kind {
            // The construction itself: the prefixes apply to it.
            ExprKind::Call { callee, .. } if !calls_on_value(callee) => {
                let mut p = prefix.clone();
                *operand_mut(&mut p) = (**next).clone();
                p.span = prefix.span.to(next.span);
                p
            }
            _ => push_down(prefix, next)?,
        };
        Some(out)
    }
    push_down(e, operand(e))
}

/// True for `()`, which is both the unit type and the unit value.
fn is_unit_tuple(e: &ast::Expr) -> bool {
    matches!(&e.kind, ExprKind::Tuple(elems) if elems.is_empty())
}

/// The smallest (or, with `max`, the largest) value of integer type `t`, or `None` if
/// `t` is not an integer type.
pub fn int_limit(t: Ty, max: bool, target: &crate::layout::Target) -> Option<num_bigint::BigInt> {
    let (signed, bits) = match t.kind() {
        TyKind::Int(i) => (i.signed, i.bits as u32),
        TyKind::Usize => (false, target.ptr_bits()),
        TyKind::Isize => (true, target.ptr_bits()),
        _ => return None,
    };
    let one = num_bigint::BigInt::from(1u8);
    Some(match (signed, max) {
        (false, false) => num_bigint::BigInt::from(0u8),
        (false, true) => (one << bits as usize) - 1u8,
        (true, false) => -(one << (bits - 1) as usize),
        (true, true) => (one << (bits - 1) as usize) - 1u8,
    })
}

/// The error for naming field `name` of type `t`, of visibility `vis`, where it is not
/// visible (§3.3).
pub(super) fn hidden_field_message(name: crate::symbol::Symbol, t: Ty, vis: ast::Visibility) -> String {
    if vis == ast::Visibility::Package {
        format!("the field `{}` of `{}` is visible only in the package of its module (§3.3)", name, t)
    } else {
        format!("the field `{}` of `{}` is private to its module (§3.3)", name, t)
    }
}
