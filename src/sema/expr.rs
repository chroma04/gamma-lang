//! Checking expressions (§7): literals, names, operators, coercions and
//! control-flow expressions.

use super::DeclValue;
use super::func::{FnCx, LocalBinding, Obligation, PendingCoercion, error_expr, unit};
use super::infer::{Fallback, VarKind};
use super::untyped::Untyped;
use crate::source::Span;
use crate::syntax::ast::{self, Expr as Expr0, ExprKind};
use crate::tir::{self, Coercion, Expr, Target};
use crate::types::{FnTy, Ty, TyKind};
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
        match &e.kind {
            ExprKind::Int(l) => {
                let ty = match l.suffix {
                    Some(s) => suffix_type(s),
                    None => self.literal_var(VarKind::Int),
                };
                mk(ty, tir::ExprKind::Int(BigInt::from(l.value.clone())), span)
            }
            ExprKind::Float(l) => match l.suffix {
                Some(s) => {
                    let fty = match s {
                        crate::syntax::token::FloatSuffix::F16 => crate::types::FloatTy::F16,
                        crate::syntax::token::FloatSuffix::F32 => crate::types::FloatTy::F32,
                        crate::syntax::token::FloatSuffix::F64 => crate::types::FloatTy::F64,
                    };
                    mk(Ty::intern(TyKind::Float(fty)), tir::ExprKind::UntypedFloat(Rc::new(l.value.clone())), span)
                }
                None => {
                    let ty = self.literal_var(VarKind::Float);
                    mk(ty, tir::ExprKind::UntypedFloat(Rc::new(l.value.clone())), span)
                }
            },
            ExprKind::Str(s) => match s.kind {
                crate::syntax::token::StrKind::C => {
                    mk(Ty::many_ptr(true, Ty::u8()), tir::ExprKind::CStr(s.bytes.clone().into()), span)
                }
                _ => mk(Ty::string(), tir::ExprKind::Str(s.bytes.clone().into()), span),
            },
            ExprKind::Bool(b) => mk(Ty::bool(), tir::ExprKind::Bool(*b), span),
            ExprKind::Null => match expect.map(|t| self.infer.shallow(t)) {
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
            },
            ExprKind::Uninit => {
                self.error(span, "`---` may only initialize a variable, field or argument");
                error_expr(span)
            }
            ExprKind::Underscore => {
                self.error(span, "`_` cannot be used as a value");
                error_expr(span)
            }
            ExprKind::Ident(id) => self.name(id.name, span),
            ExprKind::Paren(inner) => self.expr(inner, expect),
            ExprKind::Tuple(elems) => self.tuple(elems, expect, span),
            ExprKind::Unary { op, operand } => self.unary(*op, operand, expect, span),
            ExprKind::Binary { op, lhs, rhs } => {
                let l = self.expr(lhs, None);
                self.binary_op(*op, l, rhs, span)
            }
            ExprKind::Deref(p) => {
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
            ExprKind::Index { base, index } => self.index(base, index, span),
            ExprKind::Slice { base, lo, hi, inclusive } => {
                self.slice_expr(base, lo.as_deref(), hi.as_deref(), *inclusive, span)
            }
            ExprKind::Field { base, name } => self.field(base, *name, span),
            ExprKind::TupleField { base, index, .. } => {
                let b = self.expr(base, None);
                let bt = self.infer.resolve(b.ty);
                let b = self.auto_deref(b);
                match self.infer.resolve(b.ty).kind() {
                    TyKind::Tuple { elems, .. } if (*index as usize) < elems.len() => {
                        mk(elems[*index as usize], tir::ExprKind::Field { base: Box::new(b), index: *index }, span)
                    }
                    TyKind::Error => error_expr(span),
                    _ => {
                        self.error(span, format!("type `{}` has no element {}", bt, index));
                        error_expr(span)
                    }
                }
            }
            ExprKind::Call { callee, args, .. } => self.call(callee, args, expect, span),
            ExprKind::If(i) => self.if_expr(i, true, expect),
            ExprKind::Match(m) => self.match_expr(m, true, expect),
            ExprKind::Fn(lit) => self.fn_literal(lit, expect, None, span),
            ExprKind::Handler(lit) => self.handler_literal(lit, span),
            ExprKind::With(w) => self.with_expr(w, true, expect),
            ExprKind::OptUnwrap(inner) => {
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
            ExprKind::ResUnwrap(inner) => self.res_unwrap(inner, span),
            ExprKind::Bang { lhs, rhs } => {
                if self.type_of_name(lhs).is_some() {
                    self.error(span, "expected a value, found the error-union type `E!T`");
                    return error_expr(span);
                }
                // `x!(args)`: unwrap, then call (§A.7 rule 3).
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
            // A type prefix before a construction applies to the whole type (§A.7 rule 12).
            ExprKind::ArrayType { len, elem } if matches!(elem.kind, ExprKind::Call { .. }) => {
                let ExprKind::Call { callee, args, .. } = &elem.kind else { unreachable!() };
                let elem_ty = self.resolve_type(callee);
                if let Some(ast::Expr { kind: ExprKind::Ident(id), .. }) = len.as_deref()
                    && let Some(&(_, pid)) = self.sema.value_scope.iter().rev().find(|(n, _)| *n == id.name)
                {
                    let len = Ty::intern(TyKind::Param(pid));
                    let t = Ty::intern(TyKind::ParamArray { len, elem: elem_ty });
                    return self.construct_param_array(t, elem_ty, args, span);
                }
                let len = match len {
                    Some(l) => match self.sema.const_usize(self.module, l) {
                        Some(n) => Some(n),
                        None => return error_expr(span),
                    },
                    None => None,
                };
                self.construct_array(len, elem_ty, args, span)
            }
            ExprKind::OptionalType(inner) if matches!(inner.kind, ExprKind::Call { .. }) => {
                let ExprKind::Call { callee, args, .. } = &inner.kind else { unreachable!() };
                let t = self.resolve_type(callee);
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

    /// A name used as a value.
    fn name(&mut self, name: crate::symbol::Symbol, span: Span) -> Expr {
        if let Some((b, depth)) = self.lookup_local_at(name) {
            return match b {
                LocalBinding::Var(id) if depth < self.depth() => {
                    // A local of an enclosing function, captured by reference (§11.6).
                    self.note_capture(depth);
                    let ty = self.local_at(depth, id).ty;
                    mk(ty, tir::ExprKind::Upvar { levels: self.depth() - depth, local: id }, span)
                }
                LocalBinding::Var(id) => mk(self.local_ty(id), tir::ExprKind::Local(id), span),
                LocalBinding::NestedFn(f) => self.fn_ref(f, span),
                LocalBinding::Untyped(v) => self.untyped_literal(&v, span),
                LocalBinding::Label => {
                    self.error(span, format!("`{}` is a label, not a value", name));
                    error_expr(span)
                }
            };
        }
        if let Some(&(_, pid)) = self.sema.value_scope.iter().rev().find(|(n, _)| *n == name) {
            return mk(self.sema.value_params[&pid], tir::ExprKind::ValueParam(pid), span);
        }
        let module = self.module;
        if let Some(d) = self.sema.lookup_module_name(module, name) {
            return self.decl_expr(d, name, span);
        }
        if super::builtins::builtin_type(name.as_str()).is_some() {
            self.error(span, format!("expected a value, found the type `{}`", name));
        } else if super::builtins::builtin_fn(name.as_str()).is_some() {
            self.error(span, format!("the builtin function `{}` can only be called", name));
        } else {
            self.error(span, format!("unknown name `{}`", name));
        }
        error_expr(span)
    }

    /// The value of a top-level declaration named `name`.
    pub fn decl_expr(&mut self, d: super::DeclId, name: crate::symbol::Symbol, span: Span) -> Expr {
        match self.sema.resolve_decl(d) {
            DeclValue::Fn(f) => self.fn_ref(f, span),
            DeclValue::Const { ty, id } => mk(ty, tir::ExprKind::Const(id), span),
            DeclValue::Untyped(v) => self.untyped_literal(&v, span),
            DeclValue::Type(_) | DeclValue::GenericType(_) => {
                self.error(span, format!("expected a value, found the type `{}`", name));
                error_expr(span)
            }
            DeclValue::Effect(_) => {
                self.error(span, format!("expected a value, found the effect `{}`", name));
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
        Some(self.sema.module_member(m, *name))
    }

    /// A named function used as a value: a function pointer (§5.14).
    pub fn fn_ref(&mut self, f: tir::FnId, span: Span) -> Expr {
        let sig = &self.sema.sigs[&f];
        let ty = Ty::intern(TyKind::FnPtr(Box::new(FnTy {
            params: sig.params.iter().map(|p| p.ty).collect(),
            ret: sig.ret,
            effects: sig.effects.clone(),
            variadic: false,
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
            Untyped::Bool(b) => mk(Ty::bool(), tir::ExprKind::Bool(*b), span),
        }
    }

    fn tuple(&mut self, elems: &[ast::TupleElem], expect: Option<Ty>, span: Span) -> Expr {
        let expected_elems: Option<Vec<Ty>> = expect.and_then(|t| match self.infer.shallow(t).kind() {
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
            let v = self.expr(&el.value, exp);
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
            ast::UnOp::Neg => {
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
                self.obligations.push(Obligation::Numeric {
                    ty: v.ty,
                    span,
                    op: "-",
                    int_only: false,
                    signed_only: true,
                });
                mk(v.ty, tir::ExprKind::Unary { op: tir::UnOp::Neg, operand: Box::new(v) }, span)
            }
            ast::UnOp::BitNot => {
                let v = self.expr(operand, expect);
                self.obligations.push(Obligation::Numeric {
                    ty: v.ty,
                    span,
                    op: "~",
                    int_only: true,
                    signed_only: false,
                });
                mk(v.ty, tir::ExprKind::Unary { op: tir::UnOp::BitNot, operand: Box::new(v) }, span)
            }
            ast::UnOp::Not => {
                let v = self.expr(operand, Some(Ty::bool()));
                let v = self.expect_bool(v);
                mk(Ty::bool(), tir::ExprKind::Unary { op: tir::UnOp::Not, operand: Box::new(v) }, span)
            }
            ast::UnOp::AddrOf => {
                let v = self.expr(operand, None);
                match self.place_mutability(&v) {
                    Some(m) => {
                        let ty = Ty::ptr(!m, v.ty);
                        mk(ty, tir::ExprKind::AddrOf(Box::new(v)), span)
                    }
                    None => {
                        self.error(
                            span,
                            "`&` needs a place (a variable, `p^`, field or element), not a temporary value",
                        );
                        error_expr(span)
                    }
                }
            }
            ast::UnOp::MakeErr => match expect.map(|t| self.infer.shallow(t)) {
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
                    self.error(span, "cannot infer the result type of `!e` here");
                    error_expr(span)
                }
            },
            ast::UnOp::Try => self.try_expr(operand, span),
        }
    }

    /// Unifies the types of two operands that must be equal (§6.1, §7.4.1).
    pub fn unify_operands(&mut self, a: Ty, b: Ty, span: Span, what: &str) -> Ty {
        if self.infer.unify(a, b).is_err() {
            let (a, b) = (self.infer.resolve(a), self.infer.resolve(b));
            self.error(span, format!("{} have different types: `{}` and `{}`", what, a, b));
            return Ty::error();
        }
        self.infer.shallow(a)
    }

    /// Checks `lhs op rhs` given the already-checked left operand.
    pub fn binary_op(&mut self, op: ast::BinOp, lhs: Expr, rhs: &ast::Expr, span: Span) -> Expr {
        use ast::BinOp as B;
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
                if !self.infer.require_unsigned(r.ty, Fallback::U32) && !r.ty.is_error() {
                    let t = self.infer.resolve(r.ty);
                    self.error(r.span, format!("a shift amount must have an unsigned integer type, not `{}`", t));
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
            B::Add(f) | B::Sub(f) | B::Mul(f) => {
                let r = self.expr(rhs, Some(lhs.ty));
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
            B::Div | B::Rem | B::BitAnd | B::BitOr | B::BitXor => {
                let r = self.expr(rhs, Some(lhs.ty));
                let ty = self.unify_operands(lhs.ty, r.ty, span, &format!("the operands of `{}`", op.as_str()));
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
            B::In | B::NotIn => self.in_expr(lhs, rhs, op == B::NotIn, span),
            B::Range | B::RangeIncl => {
                self.error(span, "ranges are supported only in `for` loops so far");
                error_expr(span)
            }
        }
    }

    /// Makes two operands of `is` the same type: unify, or coerce one to the other.
    fn equalize(&mut self, l: Expr, r: Expr, span: Span) -> (Expr, Expr) {
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
        let b = self.auto_deref_array(b);
        let i = self.expr(index, None);
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
            return match self.sema.module_member(m, name) {
                Some(d) => self.decl_expr(d, name.name, span),
                None => error_expr(span),
            };
        }
        // `U.V` — a payload-less variant of a union type.
        if let Some(t) = self.type_of_name(base) {
            return self.construct_variant(t, name, None, span);
        }
        let b = self.expr(base, None);
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
            (TyKind::ParamArray { len, .. }, "len") => {
                let TyKind::Param(p) = len.kind() else { unreachable!() };
                return mk(Ty::usize(), tir::ExprKind::ValueParam(*p), span);
            }
            (TyKind::Error, _) => return error_expr(span),
            _ => {}
        }
        if let Some(e) = self.struct_field(b.clone(), name.name, span) {
            return e;
        }
        self.error(name.span, format!("type `{}` has no field `{}`", bt, name.name));
        error_expr(span)
    }

    /// Field `name` of a struct value, looking through `using` fields (§5.10.3).
    fn struct_field(&mut self, b: Expr, name: crate::symbol::Symbol, span: Span) -> Option<Expr> {
        let fields = self.sema.struct_fields(b.ty).or_else(|| self.sema.c_union_fields(b.ty))?;
        if let Some(i) = fields.iter().position(|f| f.name == name) {
            let ty = fields[i].ty;
            if !fields[i].is_pub && !self.sees_private(b.ty) {
                let bt = b.ty;
                self.error(span, format!("the field `{}` of `{}` is private to its module (§3.3)", name, bt));
            }
            return Some(mk(ty, tir::ExprKind::Field { base: Box::new(b), index: i as u32 }, span));
        }
        let TyKind::Adt(id, _) = b.ty.kind() else { return None };
        if !self.sema.adts.get(id).is_some_and(|a| a.has_using) {
            return None;
        }
        let first = fields[0].ty;
        let inner = mk(first, tir::ExprKind::Field { base: Box::new(b), index: 0 }, span);
        self.struct_field(inner, name, span)
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
            cur = fields[0].ty;
            if cur == *inner {
                return true;
            }
        }
        false
    }

    /// `@sqrt(x)`, `@fma(a, b, c)` on floats and the bit operations on integers (§20.3).
    fn numeric_intrinsic(&mut self, n: &str, args: &[ast::Arg], span: Span) -> Expr {
        let arity = if n == "fma" { 3 } else { 1 };
        if args.len() != arity || args.iter().any(|a| !matches!(a.kind, ast::ArgKind::Positional(_))) {
            self.error(span, format!("`@{}` takes {} argument(s)", n, arity));
            return error_expr(span);
        }
        let mut vals = vec![];
        for a in args {
            let ast::ArgKind::Positional(e) = &a.kind else { unreachable!() };
            let v = self.expr(e, vals.first().map(|v: &Expr| v.ty));
            let v = match vals.first() {
                Some(first) => self.coerce(v, first.ty),
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
                let bits = self.sema.with_layouts(|l| l.size_of(t)) * 8;
                if !matches!(t.kind(), TyKind::Int(i) if i.bits as u64 == bits)
                    && !matches!(t.kind(), TyKind::Usize | TyKind::Isize)
                {
                    self.error(span, format!("`@bswap` needs an integer of whole bytes, not `{}`", t));
                }
                (tir::Builtin::Bswap, t)
            }
            _ => (tir::Builtin::Bitreverse, t),
        };
        mk(ty, tir::ExprKind::Builtin { op, args: vals }, span)
    }

    /// Intrinsics that are compile-time constants (§20.3).
    fn intrinsic(&mut self, name: ast::Ident, args: &[ast::Arg], span: Span) -> Expr {
        let type_arg = |cx: &mut Self, i: usize| -> Option<Ty> {
            match args.get(i).map(|a| &a.kind) {
                Some(ast::ArgKind::Positional(e)) => {
                    let t = cx.resolve_type(e);
                    (!t.is_error()).then_some(t)
                }
                _ => {
                    cx.error(span, format!("`@{}` needs a type argument", name.name));
                    None
                }
            }
        };
        let usize_const = |v: u64| mk(Ty::usize(), tir::ExprKind::Int(v.into()), span);
        match name.name.as_str() {
            "size_of" | "align_of" => {
                let Some(t) = type_arg(self, 0) else { return error_expr(span) };
                if super::generics::has_param(t) {
                    let align = name.name.as_str() == "align_of";
                    return mk(Ty::usize(), tir::ExprKind::SizeOf { of: t, align }, span);
                }
                let (size, align) = self.sema.with_layouts(|l| (l.size_of(t), l.align_of(t)));
                usize_const(if name.name.as_str() == "size_of" { size } else { align })
            }
            "offset_of" => {
                let Some(t) = type_arg(self, 0) else { return error_expr(span) };
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
                usize_const(off)
            }
            // Compiler-internal heap primitives behind the core library's heap allocator.
            "heap_alloc" | "heap_free" if self.sema.modules[self.module.0 as usize].is_prelude => {
                let mut vals = vec![];
                let is_alloc = name.name.as_str() == "heap_alloc";
                let tys: Vec<Ty> = if is_alloc {
                    vec![Ty::usize(), Ty::usize()]
                } else {
                    vec![Ty::many_ptr(false, Ty::u8()), Ty::usize(), Ty::usize()]
                };
                for (i, t) in tys.iter().enumerate() {
                    match args.get(i).map(|a| &a.kind) {
                        Some(ast::ArgKind::Positional(e)) => {
                            let v = self.expr(e, Some(*t));
                            vals.push(self.coerce(v, *t));
                        }
                        _ => return error_expr(span),
                    }
                }
                let (op, ty) = if is_alloc {
                    (tir::Builtin::HeapAlloc, Ty::optional(Ty::many_ptr(false, Ty::u8())))
                } else {
                    (tir::Builtin::HeapFree, Ty::unit())
                };
                mk(ty, tir::ExprKind::Builtin { op, args: vals }, span)
            }
            n if (self.sema.modules[self.module.0 as usize].is_prelude
                || self.sema.modules[self.module.0 as usize].is_std)
                && tir::SysOp::ALL.iter().any(|op| op.signature().0 == n) =>
            {
                let op = *tir::SysOp::ALL.iter().find(|op| op.signature().0 == n).unwrap();
                let (_, params, ret) = op.signature();
                if args.len() != params.len() {
                    self.error(span, format!("`@{}` takes {} argument(s)", n, params.len()));
                    return error_expr(span);
                }
                let mut vals = vec![];
                for (i, (a, t)) in args.iter().zip(params).enumerate() {
                    let ast::ArgKind::Positional(e) = &a.kind else { return error_expr(span) };
                    if op == tir::SysOp::ThreadSpawn && i == 0 {
                        // A thread starts from a plain function: `^fn(arg: rawptr) \ {Sys}` (§19.6).
                        let v = self.expr(e, None);
                        let vt = self.infer.resolve(v.ty);
                        let ok = match vt.kind() {
                            TyKind::FnPtr(sig) => {
                                sig.params.len() == 1
                                    && matches!(sig.params[0].kind(), TyKind::Rawptr)
                                    && sig.ret.is_unit()
                                    && self.sema.within_sys(&sig.effects)
                            }
                            _ => false,
                        };
                        if !ok && !vt.is_error() {
                            self.error(e.span, format!("a thread runs a `^fn(arg: rawptr) \\ {{Sys}}`, not `{}`", vt));
                        }
                        vals.push(v);
                        continue;
                    }
                    let v = self.expr(e, Some(t));
                    vals.push(self.coerce(v, t));
                }
                mk(ret, tir::ExprKind::Builtin { op: tir::Builtin::Sys(op), args: vals }, span)
            }
            "hash" => {
                let (Some(ast::ArgKind::Positional(x)), Some(ast::ArgKind::Positional(seed)), None) =
                    (args.first().map(|a| &a.kind), args.get(1).map(|a| &a.kind), args.get(2))
                else {
                    self.error(span, "`@hash(x, seed)` takes a value and a `u64` seed");
                    return error_expr(span);
                };
                let x = self.expr(x, None);
                let xt = self.infer.resolve(x.ty);
                if !xt.has_infer() && !xt.is_error() && !self.eq_allowed(xt) {
                    self.error(
                        span,
                        format!("`@hash` is defined only for types with `is`, and `{}` has none (§16.8)", xt),
                    );
                }
                let seed = self.expr(seed, Some(Ty::u64()));
                let seed = self.coerce(seed, Ty::u64());
                mk(Ty::u64(), tir::ExprKind::Builtin { op: tir::Builtin::Hash, args: vec![x, seed] }, span)
            }
            "static_assert" => {
                let module = self.module;
                self.sema.static_assert(module, args, span);
                unit(span)
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
                let f = self.fn_id.map_or(crate::symbol::Symbol::intern(""), |f| self.sema.prog.fns[f.index()].name);
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
            "sqrt" | "fma" | "clz" | "ctz" | "popcount" | "bswap" | "bitreverse" => {
                self.numeric_intrinsic(name.name.as_str(), args, span)
            }
            // The smallest fiber stack (§14.1), in bytes.
            "min_stack" => usize_const(tir::MIN_STACK),
            "type_name" => {
                let Some(t) = type_arg(self, 0) else { return error_expr(span) };
                mk(Ty::string(), tir::ExprKind::Str(Rc::from(t.to_string().as_bytes())), span)
            }
            other => {
                self.error(span, format!("intrinsic `@{}` is not supported yet", other));
                error_expr(span)
            }
        }
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
            TyKind::Error => return error_expr(span),
            _ => {
                self.error(lhs.span, format!("`else` needs an optional or a result on its left, but this is `{}`", lt));
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
            _ => Some(present),
        };
        let r = self.expr(rhs, rhs_expect);
        self.pop_scope();
        let rt = self.infer.resolve(r.ty);
        let (ty, r) = if rt.is_never() {
            (present, r)
        } else if rt == lt && err.is_none() {
            (lt, r)
        } else if matches!(rt.kind(), TyKind::Result { ok, .. } if *ok == present) {
            (rt, r)
        } else {
            let r = self.coerce(r, present);
            (present, r)
        };
        mk(ty, tir::ExprKind::Else { lhs: Box::new(l), capture: cap, rhs: Box::new(r) }, span)
    }

    /// `try x` (§10.5).
    fn try_expr(&mut self, operand: &ast::Expr, span: Span) -> Expr {
        if self.in_defer {
            self.error(span, "`try` is not allowed in a deferred statement");
        }
        let v = self.expr(operand, None);
        let vt = self.infer.resolve(v.ty);
        // The first `try` of a test decides the test's error type (§3.8).
        if self.infer_test_error
            && self.outer.is_empty()
            && let (TyKind::Result { err, .. }, TyKind::Result { err: cur, ok }) = (vt.kind(), self.ret.kind())
            && cur.is_error()
        {
            self.ret = Ty::result(*err, *ok);
        }
        let (levels, ret) = self.return_target();
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
        for (cond, block) in &i.branches {
            self.push_scope();
            let c = self.cond(cond);
            let b = self.block_expected(block, value, expect);
            self.pop_scope();
            arms.push((c, b));
        }
        let else_ = match &i.else_ {
            Some(ast::IfElse::Block(b)) => Some(self.block_expected(b, value, expect)),
            Some(ast::IfElse::Capture { name, .. }) => {
                self.error(name.span, "error captures are not supported yet");
                None
            }
            None => None,
        };
        // Join branch types (§7.12).
        let ty = if value {
            let mut tys: Vec<Ty> = arms.iter().map(|(_, b)| b.ty).collect();
            if let Some(e) = &else_ {
                tys.push(e.ty);
            }
            match expect {
                Some(t) => t,
                None => tys.iter().copied().find(|t| !t.is_never()).unwrap_or(Ty::never()),
            }
        } else {
            let all_never =
                else_.as_ref().is_some_and(|e| e.ty.is_never()) && arms.iter().all(|(_, b)| b.ty.is_never());
            if all_never { Ty::never() } else { Ty::unit() }
        };
        let mut arms: Vec<(Vec<tir::CondPart>, Expr)> =
            if value { arms.into_iter().map(|(c, b)| (c, self.join_branch(b, ty))).collect() } else { arms };
        let mut else_ = if value { else_.map(|e| self.join_branch(e, ty)) } else { else_ };
        // Build the chain from the end.
        let mut result = None;
        while let Some((cond, then)) = arms.pop() {
            let span = then.span;
            let e = mk(ty, tir::ExprKind::If { cond, then: Box::new(then), else_: else_.take().map(Box::new) }, span);
            else_ = Some(e);
            result = else_.clone();
        }
        let mut r = result.unwrap_or_else(|| unit(i.span));
        r.span = i.span;
        r
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

    fn return_expr(&mut self, v: Option<&ast::Expr>, span: Span) -> Expr {
        if self.in_defer {
            self.error(span, "`return` is not allowed in a deferred statement");
        }
        let (levels, ret) = self.return_target();
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
            return Some(Jump::Label { levels: 0, label: self.labels[idx].id });
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
                return Some(Jump::ClauseReturn { levels });
            }
            let f = &self.outer[k];
            if let Some(idx) = f.labels.iter().rposition(|x| x.name == Some(l.name)) {
                let (is_loop, id) = (f.labels[idx].is_loop, f.labels[idx].id);
                if is_continue && !is_loop {
                    self.error(span, "`continue` can only target a loop");
                }
                return Some(Jump::Label { levels: levels + 1, label: id });
            }
            frame_clause = f.clause.clone();
        }
        self.error(l.span, format!("no enclosing loop or `with` is labeled `{}`", l.name));
        None
    }

    fn break_expr(&mut self, label: Option<ast::Ident>, value: Option<&ast::Expr>, span: Span) -> Expr {
        let Some(jump) = self.find_jump(label, span, false) else { return error_expr(span) };
        let value = match (&jump, value) {
            (Jump::Label { levels: 0, .. }, Some(v)) if self.clause.is_none() || label.is_none() => {
                self.error(v.span, "a `while` loop has no value, so `break` takes no operand here");
                None
            }
            (_, v) => {
                // A `break` that exits a `with` or a `for` over an iterator (§12.7, §13.2).
                let clause = self.clause.clone();
                match (&jump, clause.as_ref().and_then(|c| c.break_ty)) {
                    (Jump::InstallingWith, Some(rt)) => {
                        // The loop's value: the iterator's result type.
                        match v {
                            Some(v) => {
                                let ex = self.expr(v, Some(rt));
                                Some(Box::new(self.coerce(ex, rt)))
                            }
                            None => {
                                if !rt.is_unit() && !rt.is_error() {
                                    self.error(
                                        span,
                                        format!("this loop's value is `{}`, so `break` needs a value", rt),
                                    );
                                }
                                None
                            }
                        }
                    }
                    (Jump::InstallingWith, None) => {
                        let ex = v.map(|v| self.expr(v, None));
                        let ty = ex.as_ref().map_or(Ty::unit(), |e| e.ty);
                        if let Some(h) = self.clause.as_ref().and_then(|c| c.handler) {
                            self.sema.handler_breaks.entry(h.0).or_default().push((ty, span));
                        }
                        ex.map(Box::new)
                    }
                    _ => v.map(|v| Box::new(self.expr(v, None))),
                }
            }
        };
        match jump {
            Jump::Label { levels, label } => {
                mk(Ty::never(), tir::ExprKind::Break { target: Target::Label { levels, label }, value }, span)
            }
            Jump::InstallingWith => {
                mk(Ty::never(), tir::ExprKind::Break { target: Target::InstallingWith, value }, span)
            }
            Jump::ClauseReturn { .. } => error_expr(span),
        }
    }

    fn continue_expr(&mut self, label: Option<ast::Ident>, span: Span) -> Expr {
        match self.find_jump(label, span, true) {
            Some(Jump::Label { levels, label }) => {
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
        (0, 1) => se == te || matches!(se.kind(), Array { elem, .. } | ParamArray { elem, .. } if *elem == te),
        // ^[N]T → []T
        (0, 2) => matches!(se.kind(), Array { elem, .. } | ParamArray { elem, .. } if *elem == te),
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
        IntSuffix::U(n) => Ty::int(false, n.min(65535) as u16),
        IntSuffix::I(n) => Ty::int(true, n.min(65535) as u16),
        IntSuffix::Usize => Ty::usize(),
        IntSuffix::Isize => Ty::isize(),
    }
}

/// Where a `break` or `continue` goes.
enum Jump {
    /// A loop or `with` of the function `levels` frames up.
    Label { levels: u32, label: tir::LabelId },
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
        let f = self.fn_id.map_or(crate::symbol::Symbol::intern(""), |f| self.sema.prog.fns[f.index()].name);
        (span, f)
    }

    /// Whether the private fields of struct type `t` are visible here: only in
    /// the module that declares it (§3.3).
    pub fn sees_private(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Adt(id, _) => self.sema.adts.get(id).is_none_or(|a| a.module == self.module),
            _ => true,
        }
    }

    /// The first private field of struct type `t` if it is not visible here.
    pub fn hidden_field(&self, t: Ty) -> Option<crate::symbol::Symbol> {
        if self.sees_private(t) {
            return None;
        }
        self.sema.struct_fields(t)?.iter().find(|f| !f.is_pub).map(|f| f.name)
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
            let pt = self.infer.resolve(p.ty);
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
        for e in &exprs[first_value + n_values..] {
            let v = self.expr(e, Some(order_ty));
            vals.push(self.coerce(v, order_ty));
        }
        let ty = match op {
            B::AtomicLoad | B::AtomicSwap | B::AtomicRmw(_) | B::VolatileLoad => elem,
            B::AtomicCas => Ty::optional(elem),
            _ => Ty::unit(),
        };
        mk(ty, tir::ExprKind::Builtin { op, args: vals }, span)
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
