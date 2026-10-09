//! The end of literal inference (§6.1): decide pending coercions, apply
//! fallbacks, check obligations, then rewrite the body with final types and
//! literal values checked against them.

use super::expr::{apply_steps, base_coercion};
use super::func::{FnCx, Obligation};
use crate::float;
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
                let mut core = p.target;
                loop {
                    match core.kind() {
                        TyKind::Optional(t) => core = *t,
                        TyKind::Result { ok, .. } => core = *ok,
                        _ => break,
                    }
                }
                if self.infer.unify(src, core).is_err() && self.infer.unify(src, p.target).is_err() {
                    let kind = if self.infer.var_kind(src) == Some(super::infer::VarKind::Float) {
                        "a float"
                    } else {
                        "an integer"
                    };
                    self.error(p.span, format!("expected `{}`, found {} literal", p.target, kind));
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
                    self.error(
                        span,
                        format!(
                            "the value of this expression (of type `{}`) is unused; write `_ = …` to discard it",
                            t
                        ),
                    );
                }
            }
            Obligation::Conversion { from, to, span } => {
                let from = self.infer.resolve(from);
                if !from.is_error() && !self.sema.conversion_ok(from, to) {
                    self.error(
                        span,
                        format!(
                            "`{}({})` is not a lossless conversion; use `.trunc`, `.checked`, `.sat` or `.round` (§6.6)",
                            to, from
                        ),
                    );
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
                let mut t = shown;
                // `+ - * /` (not the `?` flavors) and negation apply to arrays of numbers,
                // elementwise (§7.4.6).
                let base = op.trim_end_matches(['%', '|']);
                if matches!(base, "+" | "-" | "*" | "/") && (base.len() == op.len() || base != "/") {
                    while let TyKind::Array { elem, .. } = t.kind() {
                        t = *elem;
                    }
                }
                if t.is_error() {
                    return;
                }
                use super::generics::Constraint as C;
                let ok = if let TyKind::Param(p) = t.kind() {
                    let p = *p;
                    (if int_only { self.sema.param_has(p, C::Int) } else { self.sema.param_has(p, C::Num) })
                        && (!signed_only || self.sema.param_has(p, C::Signed))
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
            tir::PatKind::Const(c) => self.fin_expr(c),
            tir::PatKind::Some(q) | tir::PatKind::Ok(q) | tir::PatKind::Err(q) => self.fin_pat(q),
            tir::PatKind::Variant { payload: Some(q), .. } => self.fin_pat(q),
            tir::PatKind::Fields(fs) => fs.iter_mut().for_each(|(_, q)| self.fin_pat(q)),
            tir::PatKind::Or(ps) => ps.iter_mut().for_each(|q| self.fin_pat(q)),
            _ => {}
        }
    }

    fn fin_expr(&mut self, e: &mut Expr) {
        e.for_each_child_mut(&mut |c| self.fin_expr(c));
        match &mut e.kind {
            ExprKind::Match { arms, .. } => arms.iter_mut().for_each(|a| self.fin_pat(&mut a.pat)),
            ExprKind::If { cond, .. } | ExprKind::Loop { cond, .. } => {
                for c in cond.iter_mut() {
                    if let tir::CondPart::Match { pat, .. } = c {
                        self.fin_pat(pat);
                    }
                }
            }
            _ => {}
        }
        e.ty = self.infer.resolve(e.ty);
        match &mut e.kind {
            // Literals at a type parameter are checked per instantiation (§16.1).
            ExprKind::Int(_) | ExprKind::UntypedFloat(_) if matches!(e.ty.kind(), TyKind::Param(_)) => {}
            ExprKind::Int(v) => {
                if e.ty.is_float() {
                    let r = BigRational::from_integer(v.clone());
                    self.fin_float(e, r, true);
                } else if !e.ty.is_error() && !int_fits(v, e.ty, &self.sema.target) {
                    self.error(e.span, format!("the literal {} does not fit in `{}`", v, e.ty));
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
                            inner
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
