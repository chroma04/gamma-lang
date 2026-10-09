//! Correctly rounded conversion of exact rationals to IEEE 754 binary formats,
//! used to give float literals their value at a type (§2.5.2, §6.1).

use crate::types::FloatTy;
use num_bigint::BigInt;
use num_integer::Integer;
use num_rational::BigRational;
use num_traits::{One, Signed, ToPrimitive, Zero};

struct Format {
    /// Significand bits including the implicit bit.
    precision: u32,
    /// Minimum normal exponent.
    emin: i64,
    /// Maximum exponent.
    emax: i64,
    exp_bits: u32,
}

fn format(f: FloatTy) -> Format {
    match f {
        FloatTy::F16 => Format { precision: 11, emin: -14, emax: 15, exp_bits: 5 },
        FloatTy::F32 => Format { precision: 24, emin: -126, emax: 127, exp_bits: 8 },
        FloatTy::F64 => Format { precision: 53, emin: -1022, emax: 1023, exp_bits: 11 },
    }
}

/// Rounds `r` to the nearest value of type `f` (ties to even). Returns the bit
/// pattern, or `None` if the rounded magnitude is infinite.
pub fn round_rational(r: &BigRational, f: FloatTy) -> Option<u64> {
    let fmt = format(f);
    let total_bits = f.bits();
    let sign_bit = if r.is_negative() { 1u64 << (total_bits - 1) } else { 0 };
    if r.is_zero() {
        return Some(sign_bit);
    }
    let n = r.numer().abs();
    let d = r.denom().abs();
    // e = floor(log2(n/d)).
    let mut e = n.bits() as i64 - d.bits() as i64;
    if cmp_pow2(&n, &d, e) == std::cmp::Ordering::Less {
        e -= 1;
    }
    let e_eff = e.max(fmt.emin);
    // m = round(n/d * 2^(precision-1-e_eff)), with ties to even.
    let k = fmt.precision as i64 - 1 - e_eff;
    let (num, den) = if k >= 0 { (n << k as usize, d) } else { (n, d << (-k) as usize) };
    let (mut m, rem) = num.div_rem(&den);
    let twice = rem << 1usize;
    if twice > den || (twice == den && m.is_odd()) {
        m += BigInt::one();
    }
    let mut exp = e_eff;
    if m.bits() > fmt.precision as u64 {
        m >>= 1usize;
        exp += 1;
    }
    if exp > fmt.emax {
        return None;
    }
    let m = m.to_u64().unwrap();
    let frac_bits = fmt.precision - 1;
    let frac = m & ((1u64 << frac_bits) - 1);
    let biased = if m >> frac_bits == 0 { 0 } else { (exp - fmt.emin + 1) as u64 };
    debug_assert!(biased < (1 << fmt.exp_bits) - 1);
    Some(sign_bit | (biased << frac_bits) | frac)
}

/// Compares n/d with 2^e.
fn cmp_pow2(n: &BigInt, d: &BigInt, e: i64) -> std::cmp::Ordering {
    if e >= 0 { n.cmp(&(d << e as usize)) } else { (n << (-e) as usize).cmp(d) }
}

/// The value of a rounded literal as `f64` (exact for every f16 and f32 value).
pub fn to_f64(bits: u64, f: FloatTy) -> f64 {
    match f {
        FloatTy::F64 => f64::from_bits(bits),
        FloatTy::F32 => f32::from_bits(bits as u32) as f64,
        FloatTy::F16 => {
            let b = bits as u16;
            let sign = if b >> 15 == 1 { -1.0 } else { 1.0 };
            let exp = ((b >> 10) & 0x1f) as i32;
            let frac = (b & 0x3ff) as f64;
            match exp {
                0 => sign * frac * 2f64.powi(-24),
                31 => {
                    if frac == 0.0 {
                        sign * f64::INFINITY
                    } else {
                        f64::NAN
                    }
                }
                _ => sign * (1.0 + frac / 1024.0) * 2f64.powi(exp - 15),
            }
        }
    }
}

/// True if the integer `i` is exactly representable in `f`.
pub fn int_fits_float(i: &BigInt, f: FloatTy) -> bool {
    let r = BigRational::from_integer(i.clone());
    match round_rational(&r, f) {
        Some(bits) => {
            let v = to_f64(bits, f);
            BigRational::from_float(v).is_some_and(|x| x == r)
        }
        None => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rat(n: i64, d: i64) -> BigRational {
        BigRational::new(BigInt::from(n), BigInt::from(d))
    }

    #[test]
    fn f64_rounding() {
        for (n, d) in [(1, 10), (1, 3), (2, 3), (-5, 7), (123456789, 1000), (1, 1 << 40)] {
            let bits = round_rational(&rat(n, d), FloatTy::F64).unwrap();
            assert_eq!(f64::from_bits(bits), n as f64 / d as f64, "{}/{}", n, d);
        }
        assert_eq!(round_rational(&rat(0, 1), FloatTy::F64), Some(0));
    }

    #[test]
    fn f32_rounding_is_direct() {
        for (n, d) in [(1, 10), (1, 3), (16777217, 1), (-7, 9)] {
            let bits = round_rational(&rat(n, d), FloatTy::F32).unwrap();
            assert_eq!(f32::from_bits(bits as u32), (n as f64 / d as f64) as f32);
        }
    }

    #[test]
    fn f16_values() {
        assert_eq!(round_rational(&rat(1, 1), FloatTy::F16), Some(0x3c00));
        assert_eq!(round_rational(&rat(65504, 1), FloatTy::F16), Some(0x7bff));
        assert_eq!(round_rational(&rat(65520, 1), FloatTy::F16), None);
        // Smallest subnormal 2^-24.
        assert_eq!(round_rational(&rat(1, 1 << 24), FloatTy::F16), Some(1));
        assert_eq!(to_f64(0x3555, FloatTy::F16), 0.333251953125);
    }

    #[test]
    fn subnormals_and_overflow() {
        let tiny = BigRational::new(BigInt::one(), BigInt::one() << 1074usize);
        assert_eq!(round_rational(&tiny, FloatTy::F64), Some(1));
        let huge = BigRational::from_integer(BigInt::one() << 1024usize);
        assert_eq!(round_rational(&huge, FloatTy::F64), None);
        assert!(int_fits_float(&BigInt::from(16777216), FloatTy::F32));
        assert!(!int_fits_float(&BigInt::from(16777217), FloatTy::F32));
    }
}
