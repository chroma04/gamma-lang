//! Arbitrary-width integer and IEEE float helpers.
//!
//! Integer values are held as exact mathematical integers ([`Int`]); range
//! checks, wrapping and saturation are explicit operations parameterized by the
//! type's signedness and width.

use super::bytes::Bytes;
use crate::types::FloatTy;
use num_bigint::{BigInt, BigUint, Sign};
use num_traits::{One, Signed, ToPrimitive, Zero};
use std::cmp::Ordering;
use std::sync::OnceLock;

/// An exact integer: small values use `i128`, larger ones `BigInt`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Int {
    S(i128),
    B(BigInt),
}

impl Int {
    pub fn from_big(b: BigInt) -> Int {
        match b.to_i128() {
            Some(v) => Int::S(v),
            None => Int::B(b),
        }
    }

    pub fn from_u128(v: u128) -> Int {
        match i128::try_from(v) {
            Ok(s) => Int::S(s),
            Err(_) => Int::B(BigInt::from(v)),
        }
    }

    pub fn to_big(&self) -> BigInt {
        match self {
            Int::S(v) => BigInt::from(*v),
            Int::B(b) => b.clone(),
        }
    }

    pub fn is_neg(&self) -> bool {
        match self {
            Int::S(v) => *v < 0,
            Int::B(b) => b.is_negative(),
        }
    }

    pub fn is_zero(&self) -> bool {
        match self {
            Int::S(v) => *v == 0,
            Int::B(b) => b.is_zero(),
        }
    }

    pub fn to_i128(&self) -> Option<i128> {
        match self {
            Int::S(v) => Some(*v),
            Int::B(b) => b.to_i128(),
        }
    }

    pub fn to_u64(&self) -> Option<u64> {
        match self {
            Int::S(v) => u64::try_from(*v).ok(),
            Int::B(b) => b.to_u64(),
        }
    }

    /// Clamps to `u64::MAX` (for sizes and shift amounts that are checked afterwards).
    pub fn to_u64_sat(&self) -> u64 {
        if self.is_neg() { 0 } else { self.to_u64().unwrap_or(u64::MAX) }
    }

    pub fn add(&self, o: &Int) -> Int {
        if let (Int::S(a), Int::S(b)) = (self, o)
            && let Some(r) = a.checked_add(*b)
        {
            return Int::S(r);
        }
        Int::from_big(self.to_big() + o.to_big())
    }

    pub fn sub(&self, o: &Int) -> Int {
        if let (Int::S(a), Int::S(b)) = (self, o)
            && let Some(r) = a.checked_sub(*b)
        {
            return Int::S(r);
        }
        Int::from_big(self.to_big() - o.to_big())
    }

    pub fn mul(&self, o: &Int) -> Int {
        if let (Int::S(a), Int::S(b)) = (self, o)
            && let Some(r) = a.checked_mul(*b)
        {
            return Int::S(r);
        }
        Int::from_big(self.to_big() * o.to_big())
    }

    pub fn neg(&self) -> Int {
        match self {
            Int::S(a) if *a != i128::MIN => Int::S(-*a),
            _ => Int::from_big(-self.to_big()),
        }
    }

    /// Truncating division; `o` must be non-zero.
    pub fn div(&self, o: &Int) -> Int {
        if let (Int::S(a), Int::S(b)) = (self, o)
            && let Some(r) = a.checked_div(*b)
        {
            return Int::S(r);
        }
        Int::from_big(self.to_big() / o.to_big())
    }

    /// Remainder with the sign of the dividend; `o` must be non-zero.
    pub fn rem(&self, o: &Int) -> Int {
        if let (Int::S(a), Int::S(b)) = (self, o)
            && let Some(r) = a.checked_rem(*b)
        {
            return Int::S(r);
        }
        Int::from_big(self.to_big() % o.to_big())
    }

    pub fn shl(&self, n: u64) -> Int {
        if let Int::S(a) = self
            && n < 127
        {
            let r = a << n;
            if (r >> n) == *a {
                return Int::S(r);
            }
        }
        Int::from_big(self.to_big() << (n as usize))
    }

    /// Arithmetic shift right (floor).
    pub fn shr(&self, n: u64) -> Int {
        match self {
            Int::S(a) => Int::S(if n >= 127 { if *a < 0 { -1 } else { 0 } } else { a >> n }),
            Int::B(b) => Int::from_big(b >> (n as usize)),
        }
    }

    pub fn and(&self, o: &Int) -> Int {
        match (self, o) {
            (Int::S(a), Int::S(b)) => Int::S(a & b),
            _ => Int::from_big(self.to_big() & o.to_big()),
        }
    }

    pub fn or(&self, o: &Int) -> Int {
        match (self, o) {
            (Int::S(a), Int::S(b)) => Int::S(a | b),
            _ => Int::from_big(self.to_big() | o.to_big()),
        }
    }

    pub fn xor(&self, o: &Int) -> Int {
        match (self, o) {
            (Int::S(a), Int::S(b)) => Int::S(a ^ b),
            _ => Int::from_big(self.to_big() ^ o.to_big()),
        }
    }

    /// Mathematical complement `-v - 1`.
    pub fn not(&self) -> Int {
        match self {
            Int::S(a) => Int::S(!*a),
            Int::B(b) => Int::from_big(-b - BigInt::one()),
        }
    }

    #[allow(clippy::should_implement_trait)]
    pub fn cmp(&self, o: &Int) -> Ordering {
        match (self, o) {
            (Int::S(a), Int::S(b)) => a.cmp(b),
            _ => self.to_big().cmp(&o.to_big()),
        }
    }

    pub fn pow(&self, n: u32) -> Int {
        Int::from_big(num_traits::pow::Pow::pow(self.to_big(), n))
    }

    /// Number of bits in the magnitude.
    pub fn bit_len(&self) -> u64 {
        match self {
            Int::S(a) => 128 - a.unsigned_abs().leading_zeros() as u64,
            Int::B(b) => b.bits(),
        }
    }

    pub fn to_f64(&self) -> f64 {
        match self {
            Int::S(a) => *a as f64,
            Int::B(b) => b.to_f64().unwrap_or(if b.is_negative() { f64::NEG_INFINITY } else { f64::INFINITY }),
        }
    }

    pub fn to_f32(&self) -> f32 {
        match self {
            Int::S(a) => *a as f32,
            Int::B(b) => b.to_f32().unwrap_or(if b.is_negative() { f32::NEG_INFINITY } else { f32::INFINITY }),
        }
    }
}

impl From<i128> for Int {
    fn from(v: i128) -> Int {
        Int::S(v)
    }
}

pub fn min_value(signed: bool, bits: u32) -> Int {
    if !signed {
        Int::S(0)
    } else if bits <= 127 {
        Int::S(-(1i128 << (bits - 1)))
    } else {
        Int::from_big(-(BigInt::one() << (bits - 1) as usize))
    }
}

pub fn max_value(signed: bool, bits: u32) -> Int {
    let n = if signed { bits - 1 } else { bits };
    if n <= 126 { Int::S((1i128 << n) - 1) } else { Int::from_big((BigInt::one() << n as usize) - BigInt::one()) }
}

pub fn fits(v: &Int, signed: bool, bits: u32) -> bool {
    if let Int::S(a) = v
        && bits <= 126
    {
        return if signed {
            *a >= -(1i128 << (bits - 1)) && *a < (1i128 << (bits - 1))
        } else {
            *a >= 0 && *a < (1i128 << bits)
        };
    }
    v.cmp(&min_value(signed, bits)) != Ordering::Less && v.cmp(&max_value(signed, bits)) != Ordering::Greater
}

/// Reduces `v` modulo 2^bits into the range of the type.
pub fn wrap(v: &Int, signed: bool, bits: u32) -> Int {
    if fits(v, signed, bits) {
        return v.clone();
    }
    if let Int::S(a) = v
        && bits <= 126
    {
        let m = a & ((1i128 << bits) - 1);
        return Int::S(if signed && m >= (1i128 << (bits - 1)) { m - (1i128 << bits) } else { m });
    }
    let modulus = BigInt::one() << bits as usize;
    let mut m = v.to_big() % &modulus;
    if m.is_negative() {
        m += &modulus;
    }
    if signed && m >= (BigInt::one() << (bits - 1) as usize) {
        m -= modulus;
    }
    Int::from_big(m)
}

pub fn saturate(v: &Int, signed: bool, bits: u32) -> Int {
    let lo = min_value(signed, bits);
    if v.cmp(&lo) == Ordering::Less {
        return lo;
    }
    let hi = max_value(signed, bits);
    if v.cmp(&hi) == Ordering::Greater {
        return hi;
    }
    v.clone()
}

/// Decodes the integer stored in `bytes` (target byte order); padding bits are ignored.
pub fn decode_int(bytes: &[u8], signed: bool, bits: u32, little: bool) -> Int {
    let n = bytes.len();
    if n <= 8 && little && bits <= 64 {
        let mut buf = [0u8; 8];
        buf[..n].copy_from_slice(bytes);
        let mut v = u64::from_le_bytes(buf);
        if bits < 64 {
            v &= (1u64 << bits) - 1;
        }
        return if signed {
            let shift = 64 - bits;
            Int::S((((v << shift) as i64) >> shift) as i128)
        } else {
            Int::S(v as i128)
        };
    }
    if n <= 16 {
        let mut buf = [0u8; 16];
        if little {
            buf[..n].copy_from_slice(bytes);
        } else {
            for i in 0..n {
                buf[i] = bytes[n - 1 - i];
            }
        }
        let mut v = u128::from_le_bytes(buf);
        if bits < 128 {
            v &= (1u128 << bits) - 1;
        }
        if signed {
            if bits == 128 {
                return Int::S(v as i128);
            }
            let s = v as i128;
            if s >= (1i128 << (bits - 1)) { Int::S(s - (1i128 << bits)) } else { Int::S(s) }
        } else {
            Int::from_u128(v)
        }
    } else {
        let le: Vec<u8> = if little { bytes.to_vec() } else { bytes.iter().rev().copied().collect() };
        let mut u = BigUint::from_bytes_le(&le);
        let mask = (BigUint::one() << bits as usize) - BigUint::one();
        u &= mask;
        let mut b = BigInt::from_biguint(Sign::Plus, u);
        if signed && b >= (BigInt::one() << (bits - 1) as usize) {
            b -= BigInt::one() << bits as usize;
        }
        Int::from_big(b)
    }
}

/// Encodes `v` (already in range) into `size` bytes in two's complement.
pub fn encode_int(v: &Int, size: usize, little: bool) -> Bytes {
    let mut out = Bytes::zeroed(size);
    let neg = v.is_neg();
    match v {
        Int::S(a) if size <= 16 => {
            out.copy_from_slice(&a.to_le_bytes()[..size]);
        }
        Int::S(a) => {
            let le = a.to_le_bytes();
            for i in 0..size {
                out[i] = if i < 16 {
                    le[i]
                } else if neg {
                    0xff
                } else {
                    0
                };
            }
        }
        Int::B(b) => {
            let le = b.to_signed_bytes_le();
            for i in 0..size {
                out[i] = if i < le.len() {
                    le[i]
                } else if neg {
                    0xff
                } else {
                    0
                };
            }
        }
    }
    if !little {
        out.reverse();
    }
    out
}

// ---------------------------------------------------------------------------
// Floats
// ---------------------------------------------------------------------------

pub fn f16_to_f64(h: u16) -> f64 {
    let sign = if h & 0x8000 != 0 { -1.0 } else { 1.0 };
    let exp = ((h >> 10) & 0x1f) as i32;
    let man = (h & 0x3ff) as f64;
    match exp {
        0 => sign * man * 2f64.powi(-24),
        0x1f => {
            if man == 0.0 {
                sign * f64::INFINITY
            } else {
                f64::NAN
            }
        }
        _ => sign * (1.0 + man / 1024.0) * 2f64.powi(exp - 15),
    }
}

/// Rounds an `f64` to the nearest binary16 (ties to even).
pub fn f64_to_f16(x: f64) -> u16 {
    let bits = x.to_bits();
    let sign = ((bits >> 63) as u16) << 15;
    let exp = ((bits >> 52) & 0x7ff) as i32;
    let man = bits & ((1u64 << 52) - 1);
    if exp == 0x7ff {
        return if man == 0 { sign | 0x7c00 } else { sign | 0x7e00 };
    }
    if exp == 0 {
        return sign;
    }
    let e = exp - 1023;
    let he = e + 15;
    if he >= 1 {
        if he >= 0x1f {
            return sign | 0x7c00;
        }
        let keep = (man >> 42) as u32;
        let rem = man & ((1u64 << 42) - 1);
        let half = 1u64 << 41;
        let mut r = ((he as u32) << 10) | keep;
        if rem > half || (rem == half && keep & 1 == 1) {
            r += 1;
        }
        sign | r as u16
    } else {
        let m = (1u128 << 52) | man as u128;
        let shift = 28 - e;
        if shift >= 120 {
            return sign;
        }
        let q = m >> shift;
        let rem = m & ((1u128 << shift) - 1);
        let half = 1u128 << (shift - 1);
        let mut r = q as u32;
        if rem > half || (rem == half && r & 1 == 1) {
            r += 1;
        }
        sign | r as u16
    }
}

/// Rounds an `f64` to the precision of `ty`, returning it as an `f64`.
pub fn round_float(x: f64, ty: FloatTy) -> f64 {
    match ty {
        FloatTy::F64 => x,
        FloatTy::F32 => x as f32 as f64,
        FloatTy::F16 => f16_to_f64(f64_to_f16(x)),
    }
}

pub fn decode_float(bytes: &[u8], ty: FloatTy, little: bool) -> f64 {
    let mut b = [0u8; 8];
    let n = bytes.len().min(8);
    if little {
        b[..n].copy_from_slice(&bytes[..n]);
    } else {
        for i in 0..n {
            b[i] = bytes[n - 1 - i];
        }
    }
    match ty {
        FloatTy::F16 => f16_to_f64(u16::from_le_bytes([b[0], b[1]])),
        FloatTy::F32 => f32::from_le_bytes([b[0], b[1], b[2], b[3]]) as f64,
        FloatTy::F64 => f64::from_le_bytes(b),
    }
}

pub fn encode_float(x: f64, ty: FloatTy, little: bool) -> Bytes {
    let mut out = match ty {
        FloatTy::F16 => Bytes::from_slice(&f64_to_f16(x).to_le_bytes()),
        FloatTy::F32 => Bytes::from_slice(&(x as f32).to_le_bytes()),
        FloatTy::F64 => Bytes::from_slice(&x.to_le_bytes()),
    };
    if !little {
        out.reverse();
    }
    out
}

/// Shortest decimal digits and exponent such that the value is `d1.d2d3… × 10^exp`, reading
/// back as `x` at type `ty`. Among the shortest digit strings that read back, the one nearest
/// the exact value of `x` is chosen, and on an exact tie the one with an even last digit.
/// `x` must be finite and positive and exactly representable at `ty`.
///
/// This is the Schubfach algorithm (R. Giulietti, "The Schubfach way to render doubles"), as
/// in the C runtime. The standard library's shortest formatting is not used because it rounds
/// an exact tie up rather than to the even digit.
pub fn shortest(x: f64, ty: FloatTy) -> (String, i32) {
    // x = c * 2^q; cmin is the smallest normal significand and qmin the exponent of the
    // subnormals.
    let (c, q, cmin, qmin): (u64, i32, u64, i32) = match ty {
        FloatTy::F64 => {
            let u = x.to_bits();
            let be = ((u >> 52) & 0x7ff) as i32;
            let c = (u & ((1 << 52) - 1)) | if be != 0 { 1 << 52 } else { 0 };
            (c, if be != 0 { be - 1075 } else { -1074 }, 1 << 52, -1074)
        }
        FloatTy::F32 => {
            let u = (x as f32).to_bits();
            let be = ((u >> 23) & 0xff) as i32;
            let c = (u & ((1 << 23) - 1)) as u64 | if be != 0 { 1 << 23 } else { 0 };
            (c, if be != 0 { be - 150 } else { -149 }, 1 << 23, -149)
        }
        FloatTy::F16 => {
            let u = f64_to_f16(x);
            let be = ((u >> 10) & 0x1f) as i32;
            let c = (u & 0x3ff) as u64 | if be != 0 { 1 << 10 } else { 0 };
            (c, if be != 0 { be - 25 } else { -24 }, 1 << 10, -24)
        }
    };
    let out = c & 1; // the interval ends belong to it iff c is even
    let (cb, cbr) = (c << 2, (c << 2) + 2);
    let (cbl, mut k) = if c != cmin || q == qmin {
        // floor(log10(2^q))
        (cb - 2, ((q as i64 * 661_971_961_083) >> 41) as i32)
    } else {
        // The interval below is half as wide: floor(log10(3/4 * 2^q)).
        (cb - 1, ((q as i64 * 661_971_961_083 - 274_743_187_321) >> 41) as i32)
    };
    let h = q + (((-k) as i64 * 913_124_641_741) >> 38) as i32 + 2; // floor(-k * log2(10))
    let g = pow10_table()[(k - G10_KMIN) as usize];
    let (vb, vbl, vbr) = (rop(g, cb << h), rop(g, cbl << h), rop(g, cbr << h));
    let (s, t) = (vb >> 2, (vb >> 2) + 1);
    // A candidate with one digit fewer is a multiple of ten around s; the interval is
    // narrower than ten units, so at most one of them lies in it.
    let (sp10, tp10) = (s / 10 * 10, s / 10 * 10 + 10);
    let (upin, wpin) = (vbl + out <= sp10 << 2, (tp10 << 2) + out <= vbr);
    let r = if upin != wpin {
        if upin { sp10 } else { tp10 }
    } else {
        let (uin, win) = (vbl + out <= s << 2, (t << 2) + out <= vbr);
        if uin != win {
            if uin { s } else { t }
        } else {
            let cmp = vb.wrapping_sub((s + t) << 1) as i64;
            if cmp < 0 || (cmp == 0 && s & 1 == 0) { s } else { t }
        }
    };
    // The digits of r * 10^k, without trailing zeros.
    let all = r.to_string();
    let digits = all.trim_end_matches('0');
    k += (all.len() - digits.len()) as i32;
    (digits.to_string(), k + digits.len() as i32 - 1)
}

/// The smallest decimal exponent k of `pow10_table`, which covers k = -324..=292.
const G10_KMIN: i32 = -324;

/// For each k, g(k) = floor(10^-k * 2^r) + 1 with 2^125 <= g < 2^126, as { g >> 63, g & (2^63 - 1) }.
fn pow10_table() -> &'static [[u64; 2]] {
    static TABLE: OnceLock<Vec<[u64; 2]>> = OnceLock::new();
    TABLE.get_or_init(|| {
        (G10_KMIN..=292)
            .map(|k| {
                let p = BigUint::from(10u32).pow(k.unsigned_abs());
                let len = p.bits() as i32;
                let g = if k <= 0 {
                    // 10^-k scaled so that its top bit is bit 125.
                    let r = 126 - len;
                    if r >= 0 { p << r as usize } else { p >> (-r) as usize }
                } else {
                    (BigUint::one() << (len + 125) as usize) / p
                };
                let g = g.to_u128().unwrap() + 1;
                [(g >> 63) as u64, (g & ((1 << 63) - 1)) as u64]
            })
            .collect()
    })
}

/// g * cp scaled down by 2^125, rounded to odd so that comparisons with the scaled interval
/// ends stay exact.
fn rop(g: [u64; 2], cp: u64) -> u64 {
    const M63: u64 = (1 << 63) - 1;
    let mulhi = |a: u64, b: u64| ((a as u128 * b as u128) >> 64) as u64;
    let x1 = mulhi(g[1], cp);
    let y0 = g[0].wrapping_mul(cp);
    let y1 = mulhi(g[0], cp);
    let z = (y0 >> 1) + x1;
    let vbp = y1 + (z >> 63);
    vbp | (((z & M63) + M63) >> 63)
}

/// Formats a float per the `@write` display rule (§21.6.1).
pub fn format_float(x: f64, ty: FloatTy) -> String {
    if x.is_nan() {
        return "nan".into();
    }
    if x.is_infinite() {
        return if x > 0.0 { "inf".into() } else { "-inf".into() };
    }
    if x == 0.0 {
        return if x.is_sign_negative() { "-0".into() } else { "0".into() };
    }
    let neg = x < 0.0;
    let (digits, exp) = shortest(x.abs(), ty);
    let mut out = String::new();
    if neg {
        out.push('-');
    }
    if (-6..=20).contains(&exp) {
        if exp >= 0 {
            let int_len = exp as usize + 1;
            if digits.len() <= int_len {
                out.push_str(&digits);
                out.push_str(&"0".repeat(int_len - digits.len()));
            } else {
                out.push_str(&digits[..int_len]);
                out.push('.');
                out.push_str(&digits[int_len..]);
            }
        } else {
            out.push_str("0.");
            out.push_str(&"0".repeat((-exp - 1) as usize));
            out.push_str(&digits);
        }
    } else {
        out.push_str(&digits[..1]);
        if digits.len() > 1 {
            out.push('.');
            out.push_str(&digits[1..]);
        }
        out.push('e');
        out.push_str(&exp.to_string());
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shortest_ties() {
        // Nearest to the exact value; an exact tie goes to the even last digit.
        let f = |x: f64, ty| format_float(x, ty);
        assert_eq!(f((-12227261.0f32 / 4.0) as f64, FloatTy::F32), "-3056815.2");
        assert_eq!(f((487257.0f32 / 16.0) as f64, FloatTy::F32), "30453.562");
        assert_eq!(f(0.1, FloatTy::F64), "0.1");
        assert_eq!(f(5e-324, FloatTy::F64), "5e-324");
        assert_eq!(f(9007199254740993.0, FloatTy::F64), "9007199254740992");
        assert_eq!(f(f64::MAX, FloatTy::F64), "1.7976931348623157e308");
        assert_eq!(f(1e23, FloatTy::F64), "1e23");
        assert_eq!(f(f32::MAX as f64, FloatTy::F32), "3.4028235e38");
        assert_eq!(f(65504.0, FloatTy::F16), "65500");
    }

    /// `shortest` by searching the digits of the exact decimal expansion of `x`.
    fn shortest_by_expansion(x: f64, ty: FloatTy) -> (String, i32) {
        let reads_back = |digits: &str, exp: i32| -> bool {
            let s = format!("{}.{}e{}", &digits[..1], &digits[1..], exp);
            match ty {
                FloatTy::F64 => s.parse::<f64>().ok() == Some(x),
                FloatTy::F32 => s.parse::<f32>().ok() == Some(x as f32),
                FloatTy::F16 => f64_to_f16(s.parse::<f64>().unwrap()) == f64_to_f16(x),
            }
        };
        // An f64 has at most 767 significant digits, so this expansion is exact.
        let exact = format!("{:.800e}", x);
        let (m, e) = exact.split_once('e').unwrap();
        let all: Vec<u8> = m.bytes().filter(|c| c.is_ascii_digit()).collect();
        let exp: i32 = e.parse().unwrap();
        let max = match ty {
            FloatTy::F16 => 6,
            FloatTy::F32 => 9,
            FloatTy::F64 => 17,
        };
        for d in 1..=max {
            let floor = String::from_utf8(all[..d].to_vec()).unwrap();
            let rest = &all[d..];
            let first_nonzero = rest.iter().position(|&c| c != b'0');
            let ceil = first_nonzero.map(|_| {
                let mut v = all[..d].to_vec();
                let mut i = d;
                loop {
                    if i == 0 {
                        v.insert(0, b'1');
                        v.truncate(d);
                        break (String::from_utf8(v).unwrap(), exp + 1);
                    }
                    i -= 1;
                    if v[i] == b'9' {
                        v[i] = b'0';
                    } else {
                        v[i] += 1;
                        break (String::from_utf8(v).unwrap(), exp);
                    }
                }
            });
            let down_ok = reads_back(&floor, exp);
            let up_ok = ceil.as_ref().is_some_and(|(c, ce)| reads_back(c, *ce));
            let pick_up = match (down_ok, up_ok) {
                (false, false) => continue,
                (true, false) => false,
                (false, true) => true,
                (true, true) => match rest[0].cmp(&b'5') {
                    Ordering::Less => false,
                    Ordering::Greater => true,
                    Ordering::Equal => {
                        if rest[1..].iter().any(|&c| c != b'0') {
                            true
                        } else {
                            (floor.as_bytes()[d - 1] - b'0') % 2 == 1
                        }
                    }
                },
            };
            let (digits, e) = if pick_up { ceil.unwrap() } else { (floor, exp) };
            return (digits.trim_end_matches('0').to_string(), e);
        }
        unreachable!("a float reads back from its full-precision digits")
    }

    /// The fast `shortest` against the exact search, on edge cases and a pseudo-random sample
    /// (`GAMMA_FLOAT_SAMPLES` sets its size per type; the default keeps the test quick).
    #[test]
    fn shortest_matches_exact_search() {
        let check64 = |x: f64| {
            if x.is_finite() && x > 0.0 {
                assert_eq!(shortest(x, FloatTy::F64), shortest_by_expansion(x, FloatTy::F64), "f64 {:e}", x);
            }
        };
        let check32 = |x: f32| {
            if x.is_finite() && x > 0.0 {
                let (a, b) = (shortest(x as f64, FloatTy::F32), shortest_by_expansion(x as f64, FloatTy::F32));
                assert_eq!(a, b, "f32 {:e}", x);
            }
        };
        for m in 1..2000u64 {
            check64(f64::from_bits(m));
            check32(f32::from_bits(m as u32));
        }
        for e in 0..2047u64 {
            for d in 0..4u64 {
                check64(f64::from_bits((e << 52) + d));
                check64(f64::from_bits(((e + 1) << 52) - 1 - d));
            }
        }
        for e in 0..255u32 {
            for d in 0..4u32 {
                check32(f32::from_bits((e << 23) + d));
                check32(f32::from_bits(((e + 1) << 23) - 1 - d));
            }
        }
        for p in -324..=308 {
            let x: f64 = format!("1e{}", p).parse().unwrap();
            for d in [-1i64, 0, 1] {
                check64(f64::from_bits((x.to_bits() as i64 + d) as u64));
            }
        }
        for p in -45..=38 {
            let x: f32 = format!("1e{}", p).parse().unwrap();
            for d in [-1i32, 0, 1] {
                check32(f32::from_bits((x.to_bits() as i32 + d) as u32));
            }
        }
        for x in [
            6474206544261693.0 / 4.0,
            7362033134866219.0 / 4.0,
            8625083667684517.0 / 4.0,
            2500985162256915.0 / 4.0,
            166929767991651.0 / 16.0,
        ] {
            check64(x);
        }
        check32(12227261.0f32 / 4.0);
        check32(487257.0f32 / 16.0);
        for h in 1..0x7c00u16 {
            let x = f16_to_f64(h);
            assert_eq!(shortest(x, FloatTy::F16), shortest_by_expansion(x, FloatTy::F16));
        }
        let n: u64 = std::env::var("GAMMA_FLOAT_SAMPLES").ok().and_then(|s| s.parse().ok()).unwrap_or(20_000);
        let mut state = 0x9E3779B97F4A7C15u64;
        let mut next = move || {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            state
        };
        for _ in 0..n {
            check64(f64::from_bits(next() & !(1 << 63)));
            check32(f32::from_bits(next() as u32 & !(1 << 31)));
            // Short decimals and dyadic fractions, where exact ties arise.
            let r = next();
            check64((r % 100_000_000) as f64 / (1u64 << (next() % 30)) as f64);
            check32((r % 100_000_000) as f32 / (1u32 << (next() % 20)) as f32);
        }
    }

    #[test]
    fn wrap_and_fit() {
        assert_eq!(wrap(&Int::S(256), false, 8), Int::S(0));
        assert_eq!(wrap(&Int::S(128), true, 8), Int::S(-128));
        assert_eq!(wrap(&Int::S(-1), false, 200), Int::from_big((BigInt::one() << 200usize) - BigInt::one()));
        assert!(fits(&Int::S(255), false, 8));
        assert!(!fits(&Int::S(256), false, 8));
        assert!(fits(&Int::S(-128), true, 8));
        assert_eq!(saturate(&Int::S(300), false, 8), Int::S(255));
        assert_eq!(saturate(&Int::S(-300), true, 8), Int::S(-128));
    }

    #[test]
    fn encode_decode_roundtrip() {
        for &(v, signed, bits, size) in
            &[(-5i128, true, 8u32, 1usize), (200, false, 8, 1), (-70000, true, 24, 4), (1 << 100, false, 128, 16)]
        {
            for little in [true, false] {
                let e = encode_int(&Int::S(v), size, little);
                assert_eq!(decode_int(&e, signed, bits, little), Int::S(v));
            }
        }
        let big = Int::from_big((BigInt::one() << 199usize) + BigInt::from(7));
        let e = encode_int(&big, 32, true);
        assert_eq!(decode_int(&e, false, 200, true), big);
        let neg = Int::from_big(-(BigInt::one() << 190usize));
        let e = encode_int(&neg, 32, false);
        assert_eq!(decode_int(&e, true, 200, false), neg);
        // u128 max
        let m = Int::from_u128(u128::MAX);
        let e = encode_int(&m, 16, true);
        assert_eq!(decode_int(&e, false, 128, true), m);
    }

    #[test]
    fn f16_conversions() {
        assert_eq!(f64_to_f16(1.0), 0x3c00);
        assert_eq!(f64_to_f16(65504.0), 0x7bff);
        assert_eq!(f64_to_f16(65520.0), 0x7c00);
        assert_eq!(f64_to_f16(65519.0), 0x7bff);
        assert_eq!(f64_to_f16(2f64.powi(-24)), 1);
        assert_eq!(f64_to_f16(2f64.powi(-25)), 0);
        assert_eq!(f64_to_f16(2f64.powi(-25) * 1.5), 1);
        assert_eq!(f64_to_f16(-0.0), 0x8000);
        // 1 + 2^-11 is a tie between 1.0 and 1 + 2^-10: rounds to even (1.0)
        assert_eq!(f64_to_f16(1.0 + 2f64.powi(-11)), 0x3c00);
        assert_eq!(f64_to_f16(1.0 + 3.0 * 2f64.powi(-11)), 0x3c02);
        for h in 0..0x7c00u16 {
            assert_eq!(f64_to_f16(f16_to_f64(h)), h);
        }
        assert_eq!(round_float(0.1, FloatTy::F16), f16_to_f64(0x2e66));
    }

    #[test]
    fn float_display() {
        assert_eq!(format_float(1.5, FloatTy::F64), "1.5");
        assert_eq!(format_float(-2.0, FloatTy::F64), "-2");
        assert_eq!(format_float(0.001, FloatTy::F64), "0.001");
        assert_eq!(format_float(1e21, FloatTy::F64), "1e21");
        assert_eq!(format_float(1e20, FloatTy::F64), "100000000000000000000");
        assert_eq!(format_float(2.5e-7, FloatTy::F64), "2.5e-7");
        assert_eq!(format_float(1e-6, FloatTy::F64), "0.000001");
        assert_eq!(format_float(-0.0, FloatTy::F64), "-0");
        assert_eq!(format_float(f64::NAN, FloatTy::F64), "nan");
        assert_eq!(format_float(f64::NEG_INFINITY, FloatTy::F64), "-inf");
        assert_eq!(format_float(0.1f32 as f64, FloatTy::F32), "0.1");
        assert_eq!(format_float(0.1f32 as f64, FloatTy::F64), "0.10000000149011612");
        assert_eq!(format_float(123456.789, FloatTy::F64), "123456.789");
        assert_eq!(format_float(f16_to_f64(f64_to_f16(0.1)), FloatTy::F16), "0.1");
        assert_eq!(format_float(65504.0, FloatTy::F16), "65500");
        assert_eq!(format_float(1.2345e25, FloatTy::F64), "1.2345e25");
    }
}
