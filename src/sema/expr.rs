//! Checking expressions (§7): literals, names, operators, coercions and
//! control-flow expressions.

use super::DeclValue;
use super::func::{FnCx, LocalBinding, Obligation, PendingCoercion, error_expr, unit};
use super::infer::{Fallback, VarKind};
use super::untyped::Untyped;
use crate::source::Span;
use crate::syntax::ast::{self, ExprKind};
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
            ExprKind::Block(b) => self.block_expected(b, true, expect),
            ExprKind::Return(v) => self.return_expr(v.as_deref(), span),
            ExprKind::Break { label, value } => self.break_expr(*label, value.as_deref(), span),
            ExprKind::Continue { label } => self.continue_expr(*label, span),
            ExprKind::For(_) => {
                self.error(span, "`for` is not supported as a value yet; use it as a statement");
                error_expr(span)
            }
            ExprKind::Intrinsic { name, .. } => {
                self.error(span, format!("intrinsic `@{}` is not supported yet", name.name));
                error_expr(span)
            }
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
        if let Some(b) = self.lookup_local(name).cloned() {
            return match b {
                LocalBinding::Var(id) => mk(self.local_ty(id), tir::ExprKind::Local(id), span),
                LocalBinding::Untyped(v) => self.untyped_literal(&v, span),
                LocalBinding::Label => {
                    self.error(span, format!("`{}` is a label, not a value", name));
                    error_expr(span)
                }
            };
        }
        let module = self.module;
        if let Some(d) = self.sema.lookup_module_name(module, name) {
            return match self.sema.resolve_decl(d) {
                DeclValue::Fn(f) => {
                    let sig = &self.sema.sigs[&f];
                    let ty = Ty::intern(TyKind::FnPtr(Box::new(FnTy {
                        params: sig.params.iter().map(|p| p.ty).collect(),
                        ret: sig.ret,
                        effects: sig.effects.clone(),
                        variadic: false,
                    })));
                    mk(ty, tir::ExprKind::FnRef(f), span)
                }
                DeclValue::Const { ty, id } => mk(ty, tir::ExprKind::Const(id), span),
                DeclValue::Untyped(v) => self.untyped_literal(&v, span),
                DeclValue::Type(_) => {
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
                DeclValue::Error => error_expr(span),
            };
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
                    tir::ExprKind::UntypedFloat(r) if is_literal(operand) || v.ty.is_infer() => {
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
                    let v = self.expr(operand, Some(*err));
                    let v = self.coerce(v, *err);
                    mk(t, tir::ExprKind::MakeErr(Box::new(v)), span)
                }
                _ => {
                    self.error(span, "cannot infer the result type of `!e` here");
                    error_expr(span)
                }
            },
            ast::UnOp::Try => {
                self.error(span, "`try` is not supported yet");
                error_expr(span)
            }
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
                let r = self.expr(rhs, Some(lhs.ty));
                let (l, r) = self.equalize(lhs, r, span);
                mk(Ty::bool(), tir::ExprKind::Eq { lhs: Box::new(l), rhs: Box::new(r), negated: op == B::IsNot }, span)
            }
            B::Lt | B::Le | B::Gt | B::Ge => {
                let r = self.expr(rhs, Some(lhs.ty));
                let ty = self.unify_operands(lhs.ty, r.ty, span, "the operands of a comparison");
                self.obligations.push(Obligation::Numeric {
                    ty,
                    span,
                    op: op.as_str(),
                    int_only: false,
                    signed_only: false,
                });
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
            B::In | B::NotIn => {
                self.error(span, "`in` is not supported yet");
                error_expr(span)
            }
            B::Range | B::RangeIncl => {
                self.error(span, "ranges are supported only in `for` loops so far");
                error_expr(span)
            }
        }
    }

    /// Makes two operands of `is` the same type: unify, or coerce one to the other.
    fn equalize(&mut self, l: Expr, r: Expr, span: Span) -> (Expr, Expr) {
        let (lt, rt) = (self.infer.resolve(l.ty), self.infer.resolve(r.ty));
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
            TyKind::Array { elem, .. } | TyKind::Slice { elem, .. } => (*elem, true),
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
            && matches!(pointee.kind(), TyKind::Array { .. })
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
        let fields = self.sema.struct_fields(b.ty)?;
        if let Some(i) = fields.iter().position(|f| f.name == name) {
            let ty = fields[i].ty;
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

    fn return_expr(&mut self, v: Option<&ast::Expr>, span: Span) -> Expr {
        if self.in_defer {
            self.error(span, "`return` is not allowed in a deferred statement");
        }
        let ret = self.ret;
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
        mk(Ty::never(), tir::ExprKind::Return { levels: 0, value: Box::new(value) }, span)
    }

    fn find_label(&mut self, label: Option<ast::Ident>, span: Span, is_continue: bool) -> Option<tir::LabelId> {
        let found = match label {
            Some(l) => self.labels.iter().rposition(|x| x.name == Some(l.name)),
            None => self.labels.iter().rposition(|x| x.is_loop),
        };
        let Some(idx) = found else {
            match label {
                Some(l) => self.error(l.span, format!("no enclosing loop or `with` is labeled `{}`", l.name)),
                None => self.error(span, "`break` and `continue` must be inside a loop"),
            }
            return None;
        };
        if self.in_defer && idx < self.defer_label_base {
            self.error(span, "a deferred statement may not `break` or `continue` out of itself");
        }
        if is_continue && !self.labels[idx].is_loop {
            self.error(span, "`continue` can only target a loop");
        }
        Some(self.labels[idx].id)
    }

    fn break_expr(&mut self, label: Option<ast::Ident>, value: Option<&ast::Expr>, span: Span) -> Expr {
        let target = self.find_label(label, span, false);
        if let Some(v) = value {
            self.error(v.span, "a `while` loop has no value, so `break` takes no operand here");
        }
        match target {
            Some(l) => mk(
                Ty::never(),
                tir::ExprKind::Break { target: Target::Label { levels: 0, label: l }, value: None },
                span,
            ),
            None => error_expr(span),
        }
    }

    fn continue_expr(&mut self, label: Option<ast::Ident>, span: Span) -> Expr {
        match self.find_label(label, span, true) {
            Some(l) => mk(Ty::never(), tir::ExprKind::Continue { target: Target::Label { levels: 0, label: l } }, span),
            None => error_expr(span),
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
        if src.has_infer() || target.has_infer() {
            let span = e.span;
            if self.infer.unify(src, target).is_err() {
                let (s, t) = (self.infer.resolve(src), self.infer.resolve(target));
                self.error(span, format!("expected `{}`, found `{}`", t, s));
            }
            return e;
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
                let array_to_slice = matches!((src.kind(), target.kind()), (Ptr { pointee, .. }, Slice { .. }) if matches!(pointee.kind(), Array { .. }));
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
        (0, 1) => se == te || matches!(se.kind(), Array { elem, .. } if *elem == te),
        // ^[N]T → []T
        (0, 2) => matches!(se.kind(), Array { elem, .. } if *elem == te),
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
