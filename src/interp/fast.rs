//! Fast paths for integers of at most 64 bits.
//!
//! The general evaluator turns every operand into [`Bytes`] and every integer
//! into an arbitrary-precision [`super::num::Int`]. For the very common case of
//! integer types that fit a machine word, the functions here evaluate
//! expressions straight to an `i128` (the exact mathematical value, as in the
//! general path) and read and write such values in memory without building
//! intermediate byte strings. Every check, trap and message is the same as in
//! the general path, and so is the order in which operands are evaluated and
//! steps are counted.

use super::bytes::Bytes;
use super::eval::is_place_expr;
use super::machine::*;
use super::value::*;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;

impl SmallInt {
    /// The value of the integer whose low `bits` bits are those of `w` (`w` may have other bits set).
    #[inline(always)]
    pub fn value(self, w: u64) -> i128 {
        if self.signed {
            let sh = 64 - self.bits;
            (((w << sh) as i64) >> sh) as i128
        } else if self.bits == 64 {
            w as i128
        } else {
            (w & ((1u64 << self.bits) - 1)) as i128
        }
    }

    #[inline(always)]
    pub fn min(self) -> i128 {
        if self.signed { -(1i128 << (self.bits - 1)) } else { 0 }
    }

    #[inline(always)]
    pub fn max(self) -> i128 {
        if self.signed { (1i128 << (self.bits - 1)) - 1 } else { (1i128 << self.bits) - 1 }
    }

    #[inline(always)]
    pub fn fits(self, v: i128) -> bool {
        v >= self.min() && v <= self.max()
    }
}

/// Reads the first `n` (at most 8) bytes of `b` as a number in the given byte order.
#[inline(always)]
pub(super) fn word_of(b: &[u8], n: usize, little: bool) -> u64 {
    let mut buf = [0u8; 8];
    if little {
        match n {
            8 => return u64::from_le_bytes(b[..8].try_into().unwrap()),
            4 => return u32::from_le_bytes(b[..4].try_into().unwrap()) as u64,
            1 => return b[0] as u64,
            _ => buf[..n].copy_from_slice(&b[..n]),
        }
    } else {
        for i in 0..n {
            buf[i] = b[n - 1 - i];
        }
    }
    u64::from_le_bytes(buf)
}

fn is_comparison(op: BinOp) -> bool {
    matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge)
}

fn op_symbol(op: BinOp) -> &'static str {
    match op {
        BinOp::Add(_) => "+",
        BinOp::Sub(_) => "-",
        BinOp::Mul(_) => "*",
        BinOp::Shl(_) => "<<",
        BinOp::Div => "/",
        BinOp::Rem => "%",
        BinOp::Shr => ">>",
        BinOp::BitAnd => "&",
        BinOp::BitOr => "|",
        BinOp::BitXor => "~",
        BinOp::Lt => "<",
        BinOp::Le => "<=",
        BinOp::Gt => ">",
        BinOp::Ge => ">=",
    }
}

impl<'a> Machine<'a> {
    /// The value of an integer literal of the small type `si`.
    #[inline(always)]
    pub fn int_literal(v: &num_bigint::BigInt, si: SmallInt) -> i128 {
        match num_traits::ToPrimitive::to_i128(v) {
            Some(x) => si.value(x as u64),
            None => Self::int_literal_wide(v, si),
        }
    }

    #[cold]
    #[inline(never)]
    fn int_literal_wide(v: &num_bigint::BigInt, si: SmallInt) -> i128 {
        super::num::wrap(&super::num::Int::from_big(v.clone()), si.signed, si.bits).to_i128().unwrap_or(0)
    }

    /// Counts one evaluation step (`eval` does this on entry).
    #[inline(always)]
    pub fn step(&mut self, span: Span) -> R<()> {
        self.steps += 1;
        if let Some(b) = self.budget
            && self.steps > b
        {
            return self.trap(span, "compile-time evaluation exceeded its step budget");
        }
        Ok(())
    }

    #[inline(always)]
    pub fn small_of(&self, ty: Ty) -> Option<SmallInt> {
        self.ti(ty).small
    }

    /// Converts a word read from memory (first byte least significant) to the numeric value of
    /// its low `size` bytes in the target's byte order.
    #[inline(always)]
    fn raw_to_word(&self, raw: u64, size: usize) -> u64 {
        if self.little() { raw } else { raw.swap_bytes() >> (64 - 8 * size) }
    }

    /// The inverse of [`Machine::raw_to_word`].
    #[inline(always)]
    fn word_to_raw(&self, w: u64, size: usize) -> u64 {
        if self.little() { w } else { (w << (64 - 8 * size)).swap_bytes() }
    }

    #[inline(always)]
    pub fn decode_small(&self, si: SmallInt, b: &[u8]) -> i128 {
        si.value(word_of(b, si.size.min(b.len()), self.little()))
    }

    #[inline(always)]
    pub fn encode_small(&self, si: SmallInt, v: i128) -> Bytes {
        Bytes::from_word(self.word_to_raw(v as u64, si.size), si.size)
    }

    #[inline(always)]
    pub fn load_small(&self, span: Span, addr: u64, si: SmallInt, align: u64) -> R<i128> {
        if let Some(raw) = self.mem.try_read_word(addr, si.size, align) {
            return Ok(si.value(self.raw_to_word(raw, si.size)));
        }
        match self.mem.read_word(addr, si.size, align) {
            Ok(raw) => Ok(si.value(self.raw_to_word(raw, si.size))),
            Err(e) => Err(trap_flow(span, e.message())),
        }
    }

    #[inline(always)]
    pub fn store_small(&mut self, span: Span, addr: u64, v: i128, si: SmallInt, align: u64) -> R<()> {
        let raw = self.word_to_raw(v as u64, si.size);
        if self.mem.try_write_word(addr, raw, si.size, align) {
            return Ok(());
        }
        match self.mem.write_word(addr, raw, si.size, align) {
            Ok(()) => Ok(()),
            Err(e) => Err(trap_flow(span, e.message())),
        }
    }

    pub fn read_place_small(&self, span: Span, p: Place, ty: Ty, si: SmallInt) -> R<i128> {
        match p {
            Place::Mem(addr) => self.load_small(span, addr, si, si.align),
            Place::Packed(addr) => self.load_small(span, addr, si, 1),
            Place::Bits { .. } => {
                let b = self.read_place(span, p, ty)?;
                Ok(self.decode_small(si, &b))
            }
        }
    }

    pub fn write_place_small(&mut self, span: Span, p: Place, ty: Ty, si: SmallInt, v: i128) -> R<()> {
        match p {
            Place::Mem(addr) => self.store_small(span, addr, v, si, si.align),
            Place::Packed(addr) => self.store_small(span, addr, v, si, 1),
            Place::Bits { .. } => {
                let b = self.encode_small(si, v);
                self.write_place(span, p, ty, &b)
            }
        }
    }

    /// Reads a first-class local of small integer type.
    #[inline(always)]
    pub fn read_local_small(&self, span: Span, fr: &Frame, id: LocalId, si: SmallInt) -> R<i128> {
        let addr = {
            let locals = fr.locals.borrow();
            match locals.get(id.index()) {
                Some(Slot::Mem(addr)) => *addr,
                Some(Slot::Reg(r)) => {
                    let raw = u64::from_le_bytes(r[..8].try_into().unwrap());
                    return Ok(si.value(self.raw_to_word(raw, si.size)));
                }
                Some(Slot::Val(_)) => return self.local_not_data(span),
                _ => return self.local_uninit(span),
            }
        };
        self.load_small(span, addr, si, si.align)
    }

    /// Assigns the value `v` to a local of small integer type.
    #[inline(always)]
    pub fn set_local_small(&mut self, span: Span, fr: &Frame, id: LocalId, si: SmallInt, v: i128) -> R<()> {
        let raw = self.word_to_raw(v as u64, si.size);
        self.set_local(span, fr, id, &raw.to_le_bytes()[..si.size], si.align)
    }

    #[cold]
    #[inline(never)]
    pub fn local_not_data<T>(&self, span: Span) -> R<T> {
        self.trap(span, "internal error: a second-class value was used as data")
    }

    #[cold]
    #[inline(never)]
    pub fn local_uninit<T>(&self, span: Span) -> R<T> {
        self.trap(span, "internal error: read of an uninitialized local")
    }

    #[cold]
    #[inline(never)]
    fn overflow_trap(&self, span: Span, sym: &str) -> Box<Flow> {
        trap_flow(span, format!("integer overflow in `{}`", sym))
    }

    #[cold]
    #[inline(never)]
    fn shift_trap(&self, span: Span, amount: i128, lty: Ty) -> Box<Flow> {
        trap_flow(span, format!("shift amount {} too large for {}", amount, lty))
    }

    /// The result of an overflowing `+ - * <<` on small integers: `exact` is the exact result (or
    /// a value out of range on the correct side if it does not fit `i128`), `low` its low 64 bits.
    #[inline(always)]
    fn small_overflow(&self, span: Span, fl: Flavor, si: SmallInt, exact: i128, low: u64, sym: &str) -> R<i128> {
        match fl {
            Flavor::Wrap => Ok(si.value(low)),
            Flavor::Sat => Ok(if exact < 0 { si.min() } else { si.max() }),
            Flavor::Checked => {
                if self.fast() {
                    Ok(si.value(low))
                } else {
                    Err(self.overflow_trap(span, sym))
                }
            }
            Flavor::Opt => unreachable!("optional flavor on the small-integer path"),
        }
    }

    /// Applies an arithmetic or bitwise operator to operands of type `lty` (small, described by
    /// `si`). Comparisons give 0 or 1. Not for the optional flavor.
    #[inline(never)]
    #[allow(clippy::too_many_arguments)]
    pub fn small_binop(&self, span: Span, op: BinOp, si: SmallInt, lty: Ty, l: i128, r: i128) -> R<i128> {
        match op {
            BinOp::Add(fl) => {
                let exact = l + r;
                if si.fits(exact) { Ok(exact) } else { self.small_overflow(span, fl, si, exact, exact as u64, "+") }
            }
            BinOp::Sub(fl) => {
                let exact = l - r;
                if si.fits(exact) { Ok(exact) } else { self.small_overflow(span, fl, si, exact, exact as u64, "-") }
            }
            BinOp::Mul(fl) => {
                let (exact, low) = if si.signed {
                    let p = l * r;
                    (p, p as u64)
                } else {
                    // Both factors are below 2^64, so the product fits `u128`.
                    let p = (l as u128) * (r as u128);
                    (if p > i128::MAX as u128 { i128::MAX } else { p as i128 }, p as u64)
                };
                if si.fits(exact) { Ok(exact) } else { self.small_overflow(span, fl, si, exact, low, "*") }
            }
            BinOp::Shl(_) | BinOp::Shr => {
                let amount = if r >= 0 && r < si.bits as i128 {
                    r as u32
                } else if self.fast() {
                    (if r < 0 { 0 } else { r as u64 } % si.bits as u64) as u32
                } else {
                    return Err(self.shift_trap(span, r, lty));
                };
                if let BinOp::Shl(fl) = op {
                    // `l` is below 2^64 in magnitude and `amount` below 64, so this is exact.
                    let (exact, low) = if si.signed {
                        let p = l << amount;
                        (p, p as u64)
                    } else {
                        let p = (l as u128) << amount;
                        (p as i128, p as u64)
                    };
                    if si.fits(exact) { Ok(exact) } else { self.small_overflow(span, fl, si, exact, low, "<<") }
                } else {
                    Ok(l >> amount)
                }
            }
            BinOp::Div | BinOp::Rem => {
                if r == 0 {
                    return self.trap(span, "division by zero");
                }
                let is_div = matches!(op, BinOp::Div);
                let exact = if si.signed {
                    if r == -1 {
                        if is_div { -l } else { 0 }
                    } else if is_div {
                        ((l as i64) / (r as i64)) as i128
                    } else {
                        ((l as i64) % (r as i64)) as i128
                    }
                } else if is_div {
                    ((l as u64) / (r as u64)) as i128
                } else {
                    ((l as u64) % (r as u64)) as i128
                };
                if si.signed && !si.fits(exact) {
                    return Err(self.overflow_trap(span, op_symbol(op)));
                }
                if si.signed && !is_div && l == si.min() && r == -1 {
                    return Err(self.overflow_trap(span, "%"));
                }
                Ok(exact)
            }
            BinOp::BitAnd => Ok(l & r),
            BinOp::BitOr => Ok(l | r),
            BinOp::BitXor => Ok(l ^ r),
            BinOp::Lt => Ok((l < r) as i128),
            BinOp::Le => Ok((l <= r) as i128),
            BinOp::Gt => Ok((l > r) as i128),
            BinOp::Ge => Ok((l >= r) as i128),
        }
    }

    /// True if `e` (a `Binary`) can take the small-integer path: its operands are small integers
    /// of the same type (for shifts, the amount may be any small integer type) and the flavor is
    /// not the optional one.
    #[inline(always)]
    pub fn small_binary_info(&self, op: BinOp, lhs: &Expr, rhs: &Expr, res: Ty) -> Option<(SmallInt, SmallInt)> {
        let si = self.ti(lhs.ty).small?;
        let ri = if matches!(op, BinOp::Shl(_) | BinOp::Shr) {
            self.ti(rhs.ty).small?
        } else {
            if rhs.ty != lhs.ty {
                return None;
            }
            si
        };
        // Arithmetic gives the operand type (the optional flavor gives `?T`); comparisons give `bool`.
        if !is_comparison(op) && res != lhs.ty {
            return None;
        }
        Some((si, ri))
    }

    /// Evaluates the operands of a `Binary` node that has already been counted and applies `op`.
    #[inline(always)]
    #[allow(clippy::too_many_arguments)]
    pub fn eval_binary_small(
        &mut self,
        a: Act,
        e: &Expr,
        op: BinOp,
        lhs: &Expr,
        rhs: &Expr,
        si: SmallInt,
        ri: SmallInt,
    ) -> R<i128> {
        let l = self.eval_small(a, lhs, si)?;
        let r = self.eval_small(a, rhs, ri)?;
        self.small_binop(e.span, op, si, lhs.ty, l, r)
    }

    /// Evaluates the operand of a counted `-x` or `~x` node of small integer type and applies the operator.
    pub fn eval_unary_small(&mut self, a: Act, e: &Expr, op: UnOp, operand: &Expr, si: SmallInt) -> R<i128> {
        let x = self.eval_small(a, operand, si)?;
        if let UnOp::Neg = op {
            let v = -x;
            if !si.fits(v) {
                if self.fast() {
                    return Ok(si.value(v as u64));
                }
                return Err(self.overflow_trap(e.span, "-"));
            }
            Ok(v)
        } else {
            Ok(si.value(!x as u64))
        }
    }

    /// Evaluates `e`, an expression whose type is the small integer type `si`, to its value.
    ///
    /// Only the most frequent kinds are handled here, the rest in [`Machine::eval_small_other`],
    /// so that the frame of this recursive function stays small.
    pub fn eval_small(&mut self, a: Act, e: &Expr, si: SmallInt) -> R<i128> {
        match &e.kind {
            ExprKind::Int(v) => {
                self.step(e.span)?;
                Ok(Self::int_literal(v, si))
            }
            ExprKind::Local(id) => {
                self.step(e.span)?;
                self.read_local_small(e.span, a.fr, *id, si)
            }
            ExprKind::Binary { op, lhs, rhs } => {
                if let Some((si2, ri)) = self.small_binary_info(*op, lhs, rhs, e.ty)
                    && !is_comparison(*op)
                {
                    self.step(e.span)?;
                    return self.eval_binary_small(a, e, *op, lhs, rhs, si2, ri);
                }
                self.eval_small_generic(a, e, si)
            }
            _ => self.eval_small_other(a, e, si),
        }
    }

    #[inline(never)]
    fn eval_small_other(&mut self, a: Act, e: &Expr, si: SmallInt) -> R<i128> {
        match &e.kind {
            ExprKind::Upvar { levels, local } => {
                self.step(e.span)?;
                let mut fr: &Frame = a.fr;
                for _ in 0..*levels {
                    match fr.parent_env.as_deref() {
                        Some(p) => fr = p,
                        None => return self.trap(e.span, "internal error: upvar without an enclosing frame"),
                    }
                }
                self.read_local_small(e.span, fr, *local, si)
            }
            ExprKind::Global(_) | ExprKind::Deref(_) => {
                self.step(e.span)?;
                let p = self.eval_place(a, e)?;
                self.read_place_small(e.span, p, e.ty, si)
            }
            ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } if is_place_expr(base) => {
                self.step(e.span)?;
                let p = self.eval_place(a, e)?;
                self.read_place_small(e.span, p, e.ty, si)
            }
            ExprKind::Index { base, .. }
                if !(matches!(base.ty.kind(), TyKind::Array { .. }) && !is_place_expr(base)) =>
            {
                self.step(e.span)?;
                let p = self.eval_place(a, e)?;
                self.read_place_small(e.span, p, e.ty, si)
            }
            ExprKind::Unary { op: op @ (UnOp::Neg | UnOp::BitNot), operand } if operand.ty == e.ty => {
                self.step(e.span)?;
                self.eval_unary_small(a, e, *op, operand, si)
            }
            ExprKind::Coerce { kind: Coercion::IntWiden, expr } => {
                if let Some(isi) = self.small_of(expr.ty) {
                    self.step(e.span)?;
                    return self.eval_small(a, expr, isi);
                }
                self.eval_small_generic(a, e, si)
            }
            ExprKind::SliceLen(s) => {
                self.step(e.span)?;
                let b = self.eval_bytes(a, s)?;
                Ok(self.slice_parts(&b).1 as i128)
            }
            _ => self.eval_small_generic(a, e, si),
        }
    }

    #[inline(never)]
    fn eval_small_generic(&mut self, a: Act, e: &Expr, si: SmallInt) -> R<i128> {
        let b = self.eval_bytes(a, e)?;
        Ok(self.decode_small(si, &b))
    }

    /// Evaluates a condition of type `bool`.
    pub fn eval_cond(&mut self, a: Act, e: &Expr) -> R<bool> {
        if let ExprKind::Binary { op, lhs, rhs } = &e.kind
            && is_comparison(*op)
            && let Some((si, ri)) = self.small_binary_info(*op, lhs, rhs, e.ty)
        {
            self.step(e.span)?;
            return Ok(self.eval_binary_small(a, e, *op, lhs, rhs, si, ri)? != 0);
        }
        self.eval_cond_other(a, e)
    }

    #[inline(never)]
    fn eval_cond_other(&mut self, a: Act, e: &Expr) -> R<bool> {
        match &e.kind {
            ExprKind::Logic { is_and, lhs, rhs } => {
                self.step(e.span)?;
                let lv = self.eval_cond(a, lhs)?;
                if lv != *is_and {
                    return Ok(lv);
                }
                return self.eval_cond(a, rhs);
            }
            ExprKind::Eq { lhs, rhs, negated } if lhs.ty == rhs.ty => {
                if let Some(si) = self.small_of(lhs.ty) {
                    self.step(e.span)?;
                    let l = self.eval_small(a, lhs, si)?;
                    let r = self.eval_small(a, rhs, si)?;
                    return Ok((l == r) != *negated);
                }
            }
            ExprKind::Unary { op: UnOp::Not, operand } => {
                self.step(e.span)?;
                return Ok(!self.eval_cond(a, operand)?);
            }
            _ => {}
        }
        let b = self.eval_bytes(a, e)?;
        Ok(self.truthy(&b))
    }
}
