//! Conversions, builtin methods and intrinsics (`tir::Builtin`).

use super::bytes::Bytes;
use super::machine::*;
use super::num::{self, Int};
use super::ops::fmt_int;
use super::value::*;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use num_bigint::BigInt;
use num_traits::{FromPrimitive, Zero};

enum Src {
    I(Int),
    F(f64),
}

fn fmin(x: f64, y: f64) -> f64 {
    if x.is_nan() {
        y
    } else if y.is_nan() {
        x
    } else if x == 0.0 && y == 0.0 {
        if x.is_sign_negative() { x } else { y }
    } else if x < y {
        x
    } else {
        y
    }
}

fn fmax(x: f64, y: f64) -> f64 {
    if x.is_nan() {
        y
    } else if y.is_nan() {
        x
    } else if x == 0.0 && y == 0.0 {
        if x.is_sign_negative() { y } else { x }
    } else if x > y {
        x
    } else {
        y
    }
}

impl<'a> Machine<'a> {
    fn decode_src(&self, ty: Ty, b: &[u8]) -> Result<Src, String> {
        match ty.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize => Ok(Src::I(self.int_of(ty, b))),
            TyKind::Float(f) => Ok(Src::F(self.float_of(*f, b))),
            TyKind::Bool => Ok(Src::I(Int::S((b[0] != 0) as i128))),
            TyKind::Adt(..) => {
                if self.union_def(ty).is_some() {
                    Ok(Src::I(self.tag_int(ty, b)?))
                } else if let Some(u) = self.bits_struct_ty(ty) {
                    Ok(Src::I(self.int_of_unsigned(u, b)))
                } else {
                    Err(format!("cannot convert from {}", ty))
                }
            }
            _ => Err(format!("cannot convert from {}", ty)),
        }
    }

    /// `Convert`, `Trunc`, `Round`, `Floor`, `Ceil`, `Checked` and `Sat`.
    pub fn convert(&self, span: Span, op: Builtin, src_ty: Ty, dst_ty: Ty, b: &[u8]) -> R<Bytes> {
        let target = if op == Builtin::Checked { self.opt_inner(dst_ty) } else { dst_ty };
        let src = self.tr(span, self.decode_src(src_ty, b))?;
        let tti = self.ti(target);
        let little = self.little();
        match (tti.kind, tti.int) {
            (_, Some((signed, bits))) => {
                let enc = |v: &Int| num::encode_int(v, tti.size, little);
                match src {
                    Src::I(v) => match op {
                        Builtin::Checked => {
                            if num::fits(&v, signed, bits) {
                                Ok(self.make_some(dst_ty, &enc(&v)))
                            } else {
                                Ok(self.make_none(dst_ty))
                            }
                        }
                        Builtin::Sat => Ok(enc(&num::saturate(&v, signed, bits))),
                        _ => Ok(enc(&num::wrap(&v, signed, bits))),
                    },
                    Src::F(x) => {
                        let rounded = match op {
                            Builtin::Round => x.round_ties_even(),
                            Builtin::Floor => x.floor(),
                            Builtin::Ceil => x.ceil(),
                            _ => x.trunc(),
                        };
                        match op {
                            Builtin::Checked => {
                                if !x.is_finite() || x != x.trunc() {
                                    return Ok(self.make_none(dst_ty));
                                }
                                let v = Int::from_big(BigInt::from_f64(x).unwrap_or_default());
                                if num::fits(&v, signed, bits) {
                                    Ok(self.make_some(dst_ty, &enc(&v)))
                                } else {
                                    Ok(self.make_none(dst_ty))
                                }
                            }
                            Builtin::Sat => {
                                if x.is_nan() {
                                    return Ok(enc(&Int::S(0)));
                                }
                                if x.is_infinite() {
                                    return Ok(enc(&if x > 0.0 {
                                        num::max_value(signed, bits)
                                    } else {
                                        num::min_value(signed, bits)
                                    }));
                                }
                                let v = Int::from_big(BigInt::from_f64(rounded.trunc()).unwrap_or_default());
                                Ok(enc(&num::saturate(&v, signed, bits)))
                            }
                            _ => {
                                let v = if x.is_finite() {
                                    Some(Int::from_big(BigInt::from_f64(rounded).unwrap_or_default()))
                                } else {
                                    None
                                };
                                match v {
                                    Some(v) if num::fits(&v, signed, bits) => Ok(enc(&v)),
                                    _ => {
                                        if self.fast() {
                                            Ok(enc(&Int::S(0)))
                                        } else {
                                            self.trap(span, "float to integer conversion out of range")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            (TyKind::Float(f), _) => {
                let x = match src {
                    Src::F(x) => num::round_float(x, *f),
                    Src::I(v) => match f {
                        FloatTy::F64 => v.to_f64(),
                        FloatTy::F32 => v.to_f32() as f64,
                        FloatTy::F16 => num::round_float(v.to_f64(), FloatTy::F16),
                    },
                };
                Ok(self.float_bytes(*f, x))
            }
            _ => {
                // Integer to `#bits` struct.
                if let (Some(u), Src::I(v)) = (self.bits_struct_ty(target), src) {
                    let ui = self.ti(u);
                    let (s, bits) = ui.int.unwrap();
                    return Ok(num::encode_int(&num::wrap(&v, s, bits), ui.size, little));
                }
                self.trap(span, format!("unsupported conversion to {}", target))
            }
        }
    }

    fn pointee_align(&self, ty: Ty) -> u64 {
        match ty.kind() {
            TyKind::Ptr { pointee: t, .. } | TyKind::ManyPtr { elem: t, .. } => {
                let ti = self.ti(*t);
                if ti.size == 0 { 1 } else { ti.align }
            }
            TyKind::Optional(t) => self.pointee_align(*t),
            _ => 1,
        }
    }

    /// Evaluates a builtin whose arguments are plain values.
    pub fn builtin(&self, span: Span, op: Builtin, res_ty: Ty, arg_tys: &[Ty], v: &[Bytes]) -> R<Bytes> {
        let little = self.little();
        match op {
            // Removed by monomorphization; generic bodies never run.
            Builtin::StaticAssert(_) => Ok(Bytes::from_slice(&[])),
            Builtin::Convert
            | Builtin::Trunc
            | Builtin::Round
            | Builtin::Floor
            | Builtin::Ceil
            | Builtin::Checked
            | Builtin::Sat => self.convert(span, op, arg_tys[0], res_ty, &v[0]),
            Builtin::Bits => Ok(v[0].resized(self.size_of(res_ty))),
            Builtin::Cast => {
                let a = self.ptr_of(&v[0]);
                let al = self.pointee_align(res_ty);
                if a != 0 && al > 1 && !a.is_multiple_of(al) && !self.fast() {
                    return self.trap(
                        span,
                        format!("misaligned pointer cast: address {:#x} is not aligned to {} bytes", a, al),
                    );
                }
                Ok(v[0].clone())
            }
            Builtin::Addr | Builtin::ToPtr => Ok(v[0].clone()),
            Builtin::PtrAdd | Builtin::PtrSub | Builtin::PtrDiff => {
                let TyKind::ManyPtr { elem, .. } = arg_tys[0].kind() else { unreachable!() };
                let stride = self.size_of(*elem) as i128;
                let p = self.ptr_of(&v[0]) as i128;
                let mask = if self.target.ptr_bits() >= 64 { u64::MAX } else { (1u64 << self.target.ptr_bits()) - 1 };
                if op == Builtin::PtrDiff {
                    let d = (p - self.ptr_of(&v[1]) as i128) as i64;
                    let d = if stride == 0 { 0 } else { d / stride as i64 };
                    return Ok(self.int_bytes(res_ty, &Int::from_big(num_bigint::BigInt::from(d))));
                }
                let n = self.int_of(arg_tys[1], &v[1]).to_i128().unwrap_or(0);
                let off = n.wrapping_mul(stride);
                let a = if op == Builtin::PtrAdd { p.wrapping_add(off) } else { p.wrapping_sub(off) };
                Ok(self.ptr_bytes(a as u64 & mask))
            }
            Builtin::Tag => {
                let (tag, _) = self.union_def(arg_tys[0]).ok_or_else(|| trap_flow(span, "tag() of a non-union"))?;
                Ok(Bytes::from_slice(&v[0][..self.size_of(tag)]))
            }
            Builtin::FromTag => {
                let u = self.opt_inner(res_ty);
                let (tag, variants) = self.union_def(u).ok_or_else(|| trap_flow(span, "from_tag on a non-union"))?;
                let x = self.int_of(arg_tys[0], &v[0]);
                let _ = tag;
                match variants.iter().position(|d| d.payload.is_none() && tag_eq(&x, &d.tag)) {
                    Some(i) => Ok(self.make_some(res_ty, &self.make_variant(u, i, &[]))),
                    None => Ok(self.make_none(res_ty)),
                }
            }
            Builtin::Min | Builtin::Max | Builtin::Clamp => self.minmax(span, op, arg_tys[0], v),
            Builtin::Abs => match arg_tys[0].kind() {
                TyKind::Float(f) => Ok(self.float_bytes(*f, self.float_of(*f, &v[0]).abs())),
                _ => {
                    let ti = self.ti(arg_tys[0]);
                    let (signed, bits) = ti.int.unwrap();
                    let x = self.decode_int_ty(ti, &v[0]);
                    let r = if x.is_neg() { x.neg() } else { x };
                    if !num::fits(&r, signed, bits) {
                        if self.fast() {
                            return Ok(num::encode_int(&num::wrap(&r, signed, bits), ti.size, little));
                        }
                        return self.trap(span, "integer overflow in `abs`");
                    }
                    Ok(num::encode_int(&r, ti.size, little))
                }
            },
            Builtin::AbsDiff => {
                let (x, y) = (self.int_of(arg_tys[0], &v[0]), self.int_of(arg_tys[1], &v[1]));
                let d = x.sub(&y);
                let d = if d.is_neg() { d.neg() } else { d };
                Ok(self.int_bytes(res_ty, &d))
            }
            Builtin::Pow(fl) => self.pow(span, fl, arg_tys[0], res_ty, arg_tys[1], v),
            Builtin::IsNan | Builtin::IsInf | Builtin::IsFinite | Builtin::SignBit | Builtin::Sqrt => {
                let TyKind::Float(f) = arg_tys[0].kind() else {
                    return self.trap(span, "float builtin on a non-float");
                };
                let x = self.float_of(*f, &v[0]);
                match op {
                    Builtin::IsNan => Ok(self.bool_bytes(x.is_nan())),
                    Builtin::IsInf => Ok(self.bool_bytes(x.is_infinite())),
                    Builtin::IsFinite => Ok(self.bool_bytes(x.is_finite())),
                    Builtin::SignBit => Ok(self.bool_bytes(x.is_sign_negative())),
                    _ => Ok(self.float_bytes(*f, num::round_float(x.sqrt(), *f))),
                }
            }
            Builtin::Fma => {
                let TyKind::Float(f) = arg_tys[0].kind() else {
                    return self.trap(span, "fma on a non-float");
                };
                let (x, y, z) = (self.float_of(*f, &v[0]), self.float_of(*f, &v[1]), self.float_of(*f, &v[2]));
                let r = match f {
                    FloatTy::F32 => (x as f32).mul_add(y as f32, z as f32) as f64,
                    _ => num::round_float(x.mul_add(y, z), *f),
                };
                Ok(self.float_bytes(*f, r))
            }
            Builtin::Rotl | Builtin::Rotr => {
                let ti = self.ti(arg_tys[0]);
                let Some((signed, bits)) = ti.int else {
                    return self.trap(span, "rotation of a non-integer");
                };
                if let (Some(si), Some(ai)) = (ti.small, self.ti(arg_tys[1]).small) {
                    // Integers of at most 64 bits rotate in a machine word, as below.
                    let n = bits as u128;
                    let mask = (1u128 << n) - 1;
                    let x = self.decode_small(si, &v[0]) as u128 & mask;
                    let amount = self.decode_small(ai, &v[1]);
                    let k = if amount < 0 { 0 } else { amount as u128 % n };
                    let k = if op == Builtin::Rotl { k } else { (n - k) % n };
                    let r = ((x << k) | (x >> (n - k))) & mask;
                    return Ok(self.encode_small(si, si.value(r as u64)));
                }
                let x = num::wrap(&self.decode_int_ty(ti, &v[0]), false, bits).to_big();
                let amount = self.decode_int_ty(self.ti(arg_tys[1]), &v[1]).to_big();
                let n = bits as u64;
                let k = num_traits::ToPrimitive::to_u64(&(amount % BigInt::from(n))).unwrap_or(0);
                let k = if op == Builtin::Rotl { k } else { (n - k) % n };
                // Bit i of x moves to bit (i + k) mod n.
                let mask = (BigInt::from(1) << n) - 1;
                let r = ((&x << k) | (&x >> (n - k))) & mask;
                let r = num::wrap(&Int::from_big(r), signed, bits);
                Ok(num::encode_int(&r, ti.size, little))
            }
            Builtin::Clz | Builtin::Ctz | Builtin::Popcount | Builtin::Bswap | Builtin::Bitreverse => {
                let ti = self.ti(arg_tys[0]);
                let Some((signed, bits)) = ti.int else {
                    return self.trap(span, "bit builtin on a non-integer");
                };
                let x = num::wrap(&self.decode_int_ty(ti, &v[0]), false, bits).to_big();
                let n = bits as u64;
                let count = |c: u64| Ok(self.int_bytes(res_ty, &Int::from_u128(c as u128)));
                match op {
                    Builtin::Clz => count(n - x.bits()),
                    Builtin::Ctz => count(if x.is_zero() { n } else { x.trailing_zeros().unwrap_or(0) }),
                    Builtin::Popcount => count(x.magnitude().count_ones()),
                    Builtin::Bswap => {
                        if bits % 8 != 0 {
                            return self
                                .trap(span, format!("bswap requires a whole number of bytes, not {} bits", bits));
                        }
                        // Reverse the value's bytes (not the padding bytes of its storage).
                        let n = (bits / 8) as usize;
                        let mut b = num::encode_int(&Int::from_big(x.clone()), ti.size, true).to_vec();
                        b[..n].reverse();
                        let r = num::decode_int(&b, false, bits, true);
                        let r = num::wrap(&r, signed, bits);
                        Ok(num::encode_int(&r, ti.size, little))
                    }
                    _ => {
                        let mut r = BigInt::zero();
                        for i in 0..n {
                            if x.bit(i) {
                                r.set_bit(n - 1 - i, true);
                            }
                        }
                        let r = num::wrap(&Int::from_big(r), signed, bits);
                        Ok(num::encode_int(&r, ti.size, little))
                    }
                }
            }
            Builtin::CstrLen => {
                let mut p = self.ptr_of(&v[0]);
                let mut n = 0u64;
                loop {
                    let b = self.mem_err(span, self.mem.read(p, 1, 1).map(|s| s[0]))?;
                    if b == 0 {
                        break;
                    }
                    p += 1;
                    n += 1;
                }
                Ok(self.usize_bytes(n))
            }
            Builtin::Unreachable => self.trap(span, "reached `@unreachable()`"),
            Builtin::Hash => {
                let seed = self.int_of(arg_tys[1], &v[1]);
                let mut h = 0xcbf29ce484222325u64
                    ^ (seed.to_u64().unwrap_or_else(|| seed.to_big().iter_u64_digits().next().unwrap_or(0)));
                self.tr(span, self.hash_val(arg_tys[0], &v[0], &mut h))?;
                Ok(self.int_bytes(res_ty, &Int::from_u128(h as u128)))
            }
            Builtin::Get
            | Builtin::GetPtr
            | Builtin::First
            | Builtin::Last
            | Builtin::RangeLen
            | Builtin::HeapAlloc
            | Builtin::AllocTag
            | Builtin::AllocCheck
            | Builtin::Fiber
            | Builtin::Sys(_)
            | Builtin::HeapFree
            | Builtin::Assert
            | Builtin::Expect
            | Builtin::AtomicLoad
            | Builtin::AtomicStore
            | Builtin::AtomicSwap
            | Builtin::AtomicCas
            | Builtin::AtomicRmw(_)
            | Builtin::Fence
            | Builtin::VolatileLoad
            | Builtin::VolatileStore => self.trap(span, "internal error: builtin needs special evaluation"),
            Builtin::Asm { .. } => self.trap(span, "inline assembly (`@asm`) runs only in compiled code"),
        }
    }

    fn minmax(&self, span: Span, op: Builtin, ty: Ty, v: &[Bytes]) -> R<Bytes> {
        match ty.kind() {
            TyKind::Float(f) => {
                let x = self.float_of(*f, &v[0]);
                let y = self.float_of(*f, &v[1]);
                let r = match op {
                    Builtin::Min => fmin(x, y),
                    Builtin::Max => fmax(x, y),
                    _ => {
                        let z = self.float_of(*f, &v[2]);
                        fmin(fmax(x, y), z)
                    }
                };
                Ok(self.float_bytes(*f, r))
            }
            _ => {
                let ti = self.ti(ty);
                if ti.int.is_none() {
                    return self.trap(span, format!("min/max/clamp on {}", ty));
                }
                let x = self.decode_int_ty(ti, &v[0]);
                let y = self.decode_int_ty(ti, &v[1]);
                let r = match op {
                    Builtin::Min => {
                        if x.cmp(&y).is_le() {
                            x
                        } else {
                            y
                        }
                    }
                    Builtin::Max => {
                        if x.cmp(&y).is_ge() {
                            x
                        } else {
                            y
                        }
                    }
                    _ => {
                        // clamp(lo, hi)
                        let hi = self.decode_int_ty(ti, &v[2]);
                        if x.cmp(&y).is_lt() {
                            y
                        } else if x.cmp(&hi).is_gt() {
                            hi
                        } else {
                            x
                        }
                    }
                };
                Ok(num::encode_int(&r, ti.size, self.little()))
            }
        }
    }

    fn pow(&self, span: Span, fl: Flavor, ty: Ty, res_ty: Ty, ety: Ty, v: &[Bytes]) -> R<Bytes> {
        let ti = self.ti(ty);
        let Some((signed, bits)) = ti.int else {
            return self.trap(span, "pow on a non-integer");
        };
        let base = self.decode_int_ty(ti, &v[0]);
        let n = self.int_of(ety, &v[1]).to_u64_sat().min(u32::MAX as u64) as u32;
        let enc = |x: &Int| num::encode_int(x, ti.size, self.little());
        let bl = base.bit_len();
        let exact: Option<Int> = if bl <= 1 || n == 0 {
            Some(base.pow(n))
        } else if (n as u64).saturating_mul(bl - 1) > bits as u64 {
            None
        } else {
            Some(base.pow(n))
        };
        if let Some(x) = &exact
            && num::fits(x, signed, bits)
        {
            return Ok(if fl == Flavor::Opt { self.make_some(res_ty, &enc(x)) } else { enc(x) });
        }
        // Overflow.
        match fl {
            Flavor::Opt => Ok(self.make_none(res_ty)),
            Flavor::Sat => {
                let neg = base.is_neg() && n % 2 == 1;
                Ok(enc(&if neg { num::min_value(signed, bits) } else { num::max_value(signed, bits) }))
            }
            Flavor::Wrap => Ok(enc(&self.pow_wrapped(&base, n, signed, bits))),
            Flavor::Checked => {
                if self.fast() {
                    Ok(enc(&self.pow_wrapped(&base, n, signed, bits)))
                } else {
                    self.trap(span, format!("integer overflow in `pow` ({}^{})", fmt_int(&base), n))
                }
            }
        }
    }

    fn pow_wrapped(&self, base: &Int, mut n: u32, signed: bool, bits: u32) -> Int {
        let mut result = Int::S(1);
        let mut b = num::wrap(base, signed, bits);
        while n > 0 {
            if n & 1 == 1 {
                result = num::wrap(&result.mul(&b), signed, bits);
            }
            b = num::wrap(&b.mul(&b), signed, bits);
            n >>= 1;
        }
        num::wrap(&result, signed, bits)
    }
}
