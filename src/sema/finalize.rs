//! The end of literal inference (§6.1): decide pending coercions, apply
//! fallbacks, check obligations, then rewrite the body with final types and
//! literal values checked against them.

use super::expr::{apply_steps, base_coercion};
use super::func::{FnCx, Obligation};
use crate::diag::Diagnostic;
use crate::float;
use crate::source::Span;
use crate::tir::{self, Coercion, Expr, ExprKind};
use crate::types::{IntTy, Ty, TyKind};
use num_bigint::BigInt;
use num_rational::BigRational;
use num_traits::One;

impl<'a, 's> FnCx<'a, 's> {
    pub fn finalize(&mut self, mut body: Expr) -> Expr {
        // Coercions from literal types: the literal takes the target type, or the
        // type the target wraps (`T` for `?T` and `E!T`).
        let pending = std::mem::take(&mut self.pending);
        for p in &pending {
            let src = self.infer.resolve(p.source);
            if src.is_infer() {
                let core = literal_core(p.target);
                if self.infer.unify(src, core).is_err() && self.infer.unify(src, p.target).is_err() {
                    let kind = if self.infer.var_kind(src) == Some(super::infer::VarKind::Float) {
                        "a float"
                    } else {
                        "an integer"
                    };
                    let hint = match core.kind() {
                        TyKind::Param(_) => {
                            let c = if kind == "a float" { "Float" } else { "Num" };
                            format!(" (a literal at `{core}` needs `where {c}({core})`)")
                        }
                        _ => String::new(),
                    };
                    self.error(p.span, format!("expected `{}`, found {} literal{}", p.target, kind, hint));
                    self.failed_coercions.push(p.span);
                }
            }
        }
        self.infer.apply_fallbacks();
        let obligations = std::mem::take(&mut self.obligations);
        for o in obligations {
            self.check_obligation(o);
        }
        self.fin_expr(&mut body);
        body
    }

    /// Gives the unresolved literal variables in `ty` their types now, for a construct that
    /// needs them while the body is checked (§6.1): a pending coercion of the variable
    /// decides it as at the end of inference, and otherwise it takes its fallback.
    pub fn settle(&mut self, ty: Ty, at: Span) -> Ty {
        for p in &self.pending {
            let src = self.infer.resolve(p.source);
            if !src.is_infer() {
                continue;
            }
            let mut mentioned = false;
            self.infer.resolve(ty).walk(&mut |x| mentioned |= x == src);
            if mentioned {
                let core = literal_core(p.target);
                if self.infer.unify(src, core).is_err() {
                    let _ = self.infer.unify(src, p.target);
                }
            }
        }
        self.infer.settle(ty, at)
    }

    /// Rewrites a nested function's body once inference is complete.
    pub fn finalize_nested(&mut self, mut body: Expr) -> Expr {
        self.fin_expr(&mut body);
        body
    }

    fn check_obligation(&mut self, o: Obligation) {
        match o {
            Obligation::UnitStatement { ty, span } => {
                let t = self.infer.resolve(ty);
                if !t.is_unit() && !t.is_never() && !t.is_error() {
                    let msg = format!(
                        "the value of this expression (of type `{}`) is unused; write `_ = …` to discard it",
                        t
                    );
                    let at = Span { hi: span.lo, ..span };
                    self.sema.emit(Diagnostic::error(span, msg).with_fix("discard the value with `_ = …`", at, "_ = "));
                }
            }
            Obligation::Conversion { from, to, span } => {
                let from = self.infer.resolve(from);
                if !from.is_error() && !self.sema.conversion_ok(from, to) {
                    // Name the methods that apply to this pair of types (§6.6).
                    let numeric = |t: Ty| t.is_integer() || t.is_float();
                    let methods: &[&str] = match (from.is_float(), to.is_float()) {
                        _ if !numeric(from) || !numeric(to) => &[],
                        (false, false) => &["trunc", "checked", "sat"],
                        (true, false) => &["trunc", "round", "checked", "sat"],
                        _ => &["to"],
                    };
                    let hint = if self.sema.bits_struct_ty(to) == Some(from) {
                        format!(
                            ": not every `{}` is a valid `{}`, since a field has a type with invalid bit patterns; build it with `{}(field: …)` (§5.10.5)",
                            from, to, to
                        )
                    } else if methods.is_empty() {
                        " (§6.5)".to_string()
                    } else {
                        let list: Vec<String> = methods.iter().map(|m| format!("`x.{}({})`", m, to)).collect();
                        format!("; use {} (§6.6)", list.join(", "))
                    };
                    self.error(span, format!("`{}({})` is not a lossless conversion{}", to, from, hint));
                }
            }
            Obligation::Ord { ty, span } => {
                let t = self.infer.resolve(ty);
                if !t.is_error() && !self.sema.has_ord(t) {
                    self.error(span, format!("values of type `{}` are not ordered (§7.5)", t));
                }
            }
            Obligation::Numeric { ty, span, op, int_only, signed_only } => {
                let shown = self.infer.resolve(ty);
                let t = shown;
                if matches!(t.kind(), TyKind::Array { .. } | TyKind::ParamArray { .. }) {
                    self.error(
                        span,
                        "arithmetic does not apply to arrays; use a `#vec [N]T` math vector (§5.6.3), a `#simd [N]T` vector (§5.6.2) or a loop",
                    );
                    return;
                }
                if t.is_error() {
                    return;
                }
                use super::generics::Constraint as C;
                let ok = if let TyKind::Param(p) = t.kind() {
                    let p = *p;
                    (if int_only { self.sema.param_has(p, C::Int) } else { self.sema.param_has(p, C::Num) })
                        && (!signed_only || self.sema.param_has(p, C::Neg))
                } else if int_only {
                    t.is_integer() && (!signed_only || t.is_signed_integer())
                } else if signed_only {
                    t.is_signed_integer() || t.is_float()
                } else {
                    t.is_integer() || t.is_float()
                };
                if !ok {
                    let what = match (int_only, signed_only) {
                        (true, _) => "integer",
                        (false, true) => "signed integer or float",
                        (false, false) => "numeric",
                    };
                    self.error(span, format!("`{}` requires {} operands, not `{}`", op, what, shown));
                }
            }
        }
    }

    /// Patterns carry types (and literal constants) that need final types too.
    fn fin_pat(&mut self, p: &mut tir::Pat) {
        p.ty = self.infer.resolve(p.ty);
        match &mut p.kind {
            // Constants are expressions, finalized with the other children of the `match`.
            tir::PatKind::Range { lo, hi, .. } => {
                for v in [&*lo, &*hi] {
                    if p.ty.is_integer() && !int_fits(v, p.ty, &self.sema.target) {
                        self.error(p.span, format!("{} does not fit in `{}`", super::untyped::show_int(v), p.ty));
                    }
                }
            }
            tir::PatKind::Some(q) | tir::PatKind::Ok(q) | tir::PatKind::Err(q) => self.fin_pat(q),
            tir::PatKind::Variant { payload: Some(q), .. } => self.fin_pat(q),
            tir::PatKind::Fields(fs) => fs.iter_mut().for_each(|(_, q)| self.fin_pat(q)),
            tir::PatKind::Or(ps) => ps.iter_mut().for_each(|q| self.fin_pat(q)),
            _ => {}
        }
    }

    fn fin_expr(&mut self, e: &mut Expr) {
        let errors = self.sema.sess.diags.error_count();
        e.for_each_child_mut(&mut |c| self.fin_expr(c));
        match &mut e.kind {
            ExprKind::Match { arms, .. } => {
                arms.iter_mut().for_each(|a| self.fin_pat(&mut a.pat));
                // A pattern that does not fit the final type was reported already.
                if !self.deferred_matches.is_empty() && self.sema.sess.diags.error_count() == errors {
                    self.check_deferred_match(e);
                }
            }
            ExprKind::If { cond, .. } | ExprKind::Loop { cond, .. } => {
                for c in cond.iter_mut() {
                    if let tir::CondPart::Match { pat, .. } = c {
                        self.fin_pat(pat);
                    }
                }
            }
            _ => {}
        }
        let fixed_by = if matches!(e.kind, ExprKind::Int(_)) { self.infer.fixed_by(e.ty) } else { None };
        e.ty = self.infer.resolve(e.ty);
        match &mut e.kind {
            // Literals at a type parameter are checked per instantiation (§16.1).
            ExprKind::Int(_) | ExprKind::UntypedFloat(_) if matches!(e.ty.kind(), TyKind::Param(_)) => {}
            ExprKind::Int(v) => {
                if e.ty.is_float() {
                    let r = BigRational::from_integer(v.clone());
                    self.fin_float(e, r, true);
                } else if !e.ty.is_error() && !int_fits(v, e.ty, &self.sema.target) {
                    let mut d = Diagnostic::error(
                        e.span,
                        format!("the literal {} does not fit in `{}`", super::untyped::show_int(v), e.ty),
                    );
                    // The type may come from a conversion of a variable the literal flows into.
                    if let Some(span) = fixed_by {
                        d = d.with_label(span, format!(
                            "this conversion gives the literal's variable the type `{0}` (§6.5); `.trunc({0})` or `.checked({0})` would convert from its own type",
                            e.ty
                        ));
                    }
                    self.sema.emit(d);
                }
            }
            ExprKind::UntypedFloat(r) => {
                let r = (**r).clone();
                if e.ty.is_integer() {
                    self.error(e.span, format!("a float literal cannot have the integer type `{}`", e.ty));
                    e.kind = ExprKind::Int(BigInt::from(0));
                } else {
                    self.fin_float(e, r, false);
                }
            }
            ExprKind::Coerce { kind: Coercion::Unresolved, expr } => {
                let inner = std::mem::replace(&mut **expr, super::func::unit(e.span));
                let target = e.ty;
                *e = if inner.ty == target {
                    inner
                } else {
                    match self.coercion_steps_final(inner.ty, target) {
                        Some(steps) => apply_steps(inner, steps),
                        None => {
                            if !self.failed_coercions.contains(&inner.span) {
                                self.error(inner.span, format!("expected `{}`, found `{}`", target, inner.ty));
                            }
                            // Nothing may evaluate a value of the wrong type.
                            super::func::error_expr(inner.span)
                        }
                    }
                };
            }
            _ => {}
        }
    }

    fn coercion_steps_final(&mut self, src: Ty, target: Ty) -> Option<Vec<(Coercion, Ty)>> {
        if src == target {
            return Some(vec![]);
        }
        if let Some(k) = base_coercion(src, target) {
            return Some(vec![(k, target)]);
        }
        let (inner, wrap) = match target.kind() {
            TyKind::Optional(t) => (*t, Coercion::WrapOptional),
            TyKind::Result { ok, .. } => (*ok, Coercion::WrapOk),
            _ => return None,
        };
        let mut steps = self.coercion_steps_final(src, inner)?;
        steps.push((wrap, target));
        Some(steps)
    }

    /// Rounds a literal value to its float type.
    fn fin_float(&mut self, e: &mut Expr, r: BigRational, from_int: bool) {
        let TyKind::Float(f) = e.ty.kind() else {
            e.kind = ExprKind::Float(0.0);
            return;
        };
        match float::round_rational(&r, *f) {
            Some(bits) => {
                let v = float::to_f64(bits, *f);
                if from_int && BigRational::from_float(v) != Some(r.clone()) {
                    self.error(e.span, format!("the integer {} is not exactly representable as `{}`", r, e.ty));
                }
                e.kind = ExprKind::Float(v);
            }
            None => {
                self.error(e.span, format!("the literal is too large for `{}` (it would round to infinity)", e.ty));
                e.kind = ExprKind::Float(0.0);
            }
        }
    }
}

/// True if the integer `v` is a value of the integer type `ty`.
pub fn int_fits(v: &BigInt, ty: Ty, target: &crate::layout::Target) -> bool {
    let Some((signed, bits)) = crate::layout::int_bits(ty, target) else { return false };
    let (lo, hi) = if signed {
        (-(BigInt::one() << (bits - 1) as usize), (BigInt::one() << (bits - 1) as usize) - 1)
    } else {
        (BigInt::from(0), (BigInt::one() << bits as usize) - 1)
    };
    *v >= lo && *v <= hi
}

/// The lossless conversions of §6.5. `usize`/`isize` are checked against every
/// supported target (pointer width 32 or 64).
pub fn lossless(from: Ty, to: Ty) -> bool {
    use TyKind::*;
    // (signed, min bits, max bits) of an integer type over all targets.
    let range = |t: Ty| -> Option<(bool, u32, u32)> {
        match t.kind() {
            Int(IntTy { signed, bits }) => Some((*signed, *bits as u32, *bits as u32)),
            Usize => Some((false, 32, 64)),
            Isize => Some((true, 32, 64)),
            _ => None,
        }
    };
    match (from.kind(), to.kind()) {
        _ if from == to => true,
        (Bool, _) => to.is_integer(),
        (_, Float(f)) if from.is_integer() => {
            let (signed, _, max) = range(from).unwrap();
            let needed = if signed { max - 1 } else { max };
            needed <= f.significand_bits()
        }
        (Float(a), Float(b)) => b.bits() > a.bits(),
        _ if from.is_integer() && to.is_integer() => {
            let (fs, _, fmax) = range(from).unwrap();
            let (ts, tmin, _) = range(to).unwrap();
            match (fs, ts) {
                (false, false) | (true, true) => tmin >= fmax,
                (false, true) => tmin > fmax,
                (true, false) => false,
            }
        }
        _ => false,
    }
}

/// The type a literal coerced to `target` takes: `T` for `?T` and `E!T` (§6.1).
fn literal_core(target: Ty) -> Ty {
    let mut core = target;
    loop {
        match core.kind() {
            TyKind::Optional(t) => core = *t,
            TyKind::Result { ok, .. } => core = *ok,
            _ => return core,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lossless_conversions() {
        assert!(lossless(Ty::u8(), Ty::u32()));
        assert!(lossless(Ty::u8(), Ty::int(true, 9)));
        assert!(!lossless(Ty::u8(), Ty::int(true, 8)));
        assert!(!lossless(Ty::i32(), Ty::u32()));
        assert!(lossless(Ty::u32(), Ty::usize()));
        assert!(!lossless(Ty::int(false, 64), Ty::usize()));
        assert!(lossless(Ty::usize(), Ty::int(false, 64)));
        assert!(lossless(Ty::int(false, 24), Ty::intern(TyKind::Float(crate::types::FloatTy::F32))));
        assert!(!lossless(Ty::int(false, 25), Ty::intern(TyKind::Float(crate::types::FloatTy::F32))));
        assert!(lossless(Ty::int(true, 25), Ty::intern(TyKind::Float(crate::types::FloatTy::F32))));
        assert!(lossless(Ty::bool(), Ty::u8()));
        assert!(!lossless(Ty::u8(), Ty::bool()));
    }

    #[test]
    fn fits() {
        let t = crate::layout::Target::host();
        assert!(int_fits(&BigInt::from(255), Ty::u8(), &t));
        assert!(!int_fits(&BigInt::from(256), Ty::u8(), &t));
        assert!(int_fits(&BigInt::from(-128), Ty::int(true, 8), &t));
        assert!(!int_fits(&BigInt::from(-1), Ty::u8(), &t));
    }
}
