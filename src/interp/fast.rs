//! Integers of at most 64 bits as exact `i128` values.
//!
//! The general arithmetic works on byte strings and arbitrary-precision
//! [`super::num::Int`]s. For integer types that fit a machine word, the functions here
//! decode, encode and compute with the exact mathematical value in an `i128`, with the
//! same checks, traps and messages: the bytecode VM's integer instructions use them for
//! literals and when their 64-bit fast path overflows.

use super::bytes::Bytes;
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

    #[inline(always)]
    pub fn small_of(&self, ty: Ty) -> Option<SmallInt> {
        self.ti(ty).small
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
}
