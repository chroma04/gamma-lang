//! Arithmetic, comparison, structural equality and hashing.

use super::bytes::Bytes;
use super::machine::*;
use super::num::{self, Int};
use super::value::*;
use crate::layout;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use std::cmp::Ordering;

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

fn is_comparison(op: BinOp) -> bool {
    matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge)
}

impl<'a> Machine<'a> {
    /// Applies a binary operator to operand bytes. `lty` is the left operand type, `rty` the right,
    /// `res_ty` the result type (`?T` for the optional flavor).
    #[allow(clippy::too_many_arguments)]
    pub fn binop(&self, span: Span, op: BinOp, lty: Ty, rty: Ty, res_ty: Ty, l: &[u8], r: &[u8]) -> R<Bytes> {
        if is_comparison(op) {
            let v = self.tr(span, self.cmp_op(op, lty, l, r))?;
            return Ok(self.bool_bytes(v));
        }
        match lty.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize => self.int_binop(span, op, lty, rty, res_ty, l, r),
            TyKind::Float(f) => {
                let (x, y) = (self.float_of(*f, l), self.float_of(*f, r));
                let v = match op {
                    BinOp::Add(_) => x + y,
                    BinOp::Sub(_) => x - y,
                    BinOp::Mul(_) => x * y,
                    BinOp::Div => x / y,
                    _ => return self.trap(span, format!("operator `{}` is not defined on floats", op_symbol(op))),
                };
                Ok(self.float_bytes(*f, num::round_float(v, *f)))
            }
            TyKind::Array { len, elem, .. } => {
                let esz = self.size_of(*elem);
                let mut out = Bytes::zeroed(esz * *len as usize);
                for i in 0..*len as usize {
                    let v = self.binop(
                        span,
                        op,
                        *elem,
                        *elem,
                        *elem,
                        &l[i * esz..(i + 1) * esz],
                        &r[i * esz..(i + 1) * esz],
                    )?;
                    out[i * esz..(i + 1) * esz].copy_from_slice(&v);
                }
                Ok(out)
            }
            TyKind::Adt(..) => match self.bits_struct_ty(lty) {
                Some(u) => self.int_binop(span, op, u, u, u, l, r),
                None => self.trap(span, format!("operator `{}` is not defined on {}", op_symbol(op), lty)),
            },
            _ => self.trap(span, format!("operator `{}` is not defined on {}", op_symbol(op), lty)),
        }
    }

    fn finish_int(&self, span: Span, fl: Flavor, exact: Int, lty: Ty, res_ty: Ty, sym: &str) -> R<Bytes> {
        let ti = self.ti(lty);
        let (signed, bits) = ti.int.unwrap();
        let ok = num::fits(&exact, signed, bits);
        let enc = |v: &Int| num::encode_int(v, ti.size, self.little());
        match fl {
            Flavor::Opt => {
                if ok {
                    Ok(self.make_some(res_ty, &enc(&exact)))
                } else {
                    Ok(self.make_none(res_ty))
                }
            }
            _ if ok => Ok(enc(&exact)),
            Flavor::Wrap => Ok(enc(&num::wrap(&exact, signed, bits))),
            Flavor::Sat => Ok(enc(&num::saturate(&exact, signed, bits))),
            Flavor::Checked => {
                if self.fast() {
                    Ok(enc(&num::wrap(&exact, signed, bits)))
                } else {
                    self.trap(span, format!("integer overflow in `{}`", sym))
                }
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn int_binop(&self, span: Span, op: BinOp, lty: Ty, rty: Ty, res_ty: Ty, l: &[u8], r: &[u8]) -> R<Bytes> {
        let ti = self.ti(lty);
        let (signed, bits) = ti.int.unwrap();
        let a = self.decode_int_ty(ti, l);
        let enc = |v: &Int| num::encode_int(v, ti.size, self.little());
        match op {
            BinOp::Add(fl) | BinOp::Sub(fl) | BinOp::Mul(fl) => {
                let b = self.decode_int_ty(ti, r);
                let exact = match op {
                    BinOp::Add(_) => a.add(&b),
                    BinOp::Sub(_) => a.sub(&b),
                    _ => a.mul(&b),
                };
                self.finish_int(span, fl, exact, lty, res_ty, op_symbol(op))
            }
            BinOp::Shl(_) | BinOp::Shr => {
                let fl = if let BinOp::Shl(f) = op { f } else { Flavor::Checked };
                let n = self.int_of(rty, r);
                let amount = match n.to_u64() {
                    Some(n) if n < bits as u64 => n,
                    _ => {
                        if self.fast() {
                            n.to_u64_sat() % bits as u64
                        } else {
                            return self.trap(span, format!("shift amount {} too large for {}", fmt_int(&n), lty));
                        }
                    }
                };
                if let BinOp::Shl(_) = op {
                    self.finish_int(span, fl, a.shl(amount), lty, res_ty, "<<")
                } else {
                    Ok(enc(&a.shr(amount)))
                }
            }
            BinOp::Div | BinOp::Rem => {
                let b = self.decode_int_ty(ti, r);
                if b.is_zero() {
                    return self.trap(span, "division by zero");
                }
                let exact = if let BinOp::Div = op { a.div(&b) } else { a.rem(&b) };
                if signed && !num::fits(&exact, true, bits) {
                    return self.trap(span, format!("integer overflow in `{}`", op_symbol(op)));
                }
                if signed && matches!(op, BinOp::Rem) && a == num::min_value(true, bits) && b == Int::S(-1) {
                    return self.trap(span, "integer overflow in `%`");
                }
                Ok(enc(&exact))
            }
            BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor => {
                let b = self.decode_int_ty(ti, r);
                let v = match op {
                    BinOp::BitAnd => a.and(&b),
                    BinOp::BitOr => a.or(&b),
                    _ => a.xor(&b),
                };
                Ok(enc(&num::wrap(&v, signed, bits)))
            }
            _ => unreachable!(),
        }
    }

    pub fn unop(&self, span: Span, op: UnOp, ty: Ty, b: &[u8]) -> R<Bytes> {
        match op {
            UnOp::Not => Ok(self.bool_bytes(b[0] == 0)),
            UnOp::Neg => match ty.kind() {
                TyKind::Float(f) => Ok(self.float_bytes(*f, -self.float_of(*f, b))),
                TyKind::Array { len, elem, .. } => {
                    let esz = self.size_of(*elem);
                    let mut out = Bytes::zeroed(esz * *len as usize);
                    for i in 0..*len as usize {
                        let v = self.unop(span, op, *elem, &b[i * esz..(i + 1) * esz])?;
                        out[i * esz..(i + 1) * esz].copy_from_slice(&v);
                    }
                    Ok(out)
                }
                _ => {
                    let ti = self.ti(ty);
                    let Some((signed, bits)) = ti.int else {
                        return self.trap(span, format!("cannot negate {}", ty));
                    };
                    let v = self.decode_int_ty(ti, b).neg();
                    if !num::fits(&v, signed, bits) {
                        if self.fast() {
                            return Ok(num::encode_int(&num::wrap(&v, signed, bits), ti.size, self.little()));
                        }
                        return self.trap(span, "integer overflow in `-`");
                    }
                    Ok(num::encode_int(&v, ti.size, self.little()))
                }
            },
            UnOp::BitNot => {
                let ty = self.bits_struct_ty(ty).unwrap_or(ty);
                let ti = self.ti(ty);
                let Some((signed, bits)) = ti.int else {
                    return self.trap(span, format!("cannot complement {}", ty));
                };
                let v = num::wrap(&self.decode_int_ty(ti, b).not(), signed, bits);
                Ok(num::encode_int(&v, ti.size, self.little()))
            }
        }
    }

    // --- comparison ------------------------------------------------------------------------------

    pub fn cmp_op(&self, op: BinOp, ty: Ty, a: &[u8], b: &[u8]) -> Res<bool> {
        Ok(match op {
            BinOp::Lt => self.less(ty, a, b)?,
            BinOp::Gt => self.less(ty, b, a)?,
            BinOp::Le => self.less(ty, a, b)? || self.eq(ty, a, b)?,
            BinOp::Ge => self.less(ty, b, a)? || self.eq(ty, b, a)?,
            _ => unreachable!(),
        })
    }

    /// The contents of an array or slice value as `(element type, element bytes)`.
    pub fn seq_elems(&self, ty: Ty, b: &[u8]) -> Res<(Ty, usize, Bytes)> {
        match ty.kind() {
            TyKind::Array { elem, .. } => Ok((*elem, self.size_of(*elem), Bytes::from_slice(b))),
            TyKind::Slice { elem, .. } => {
                let esz = self.size_of(*elem);
                let (p, n) = self.slice_parts(b);
                let total = (n as usize).checked_mul(esz).ok_or("slice too large")?;
                let data = if total == 0 {
                    Bytes::empty()
                } else {
                    Bytes::from_slice(self.mem.read(p, total as u64, 1).map_err(|e| e.message())?)
                };
                Ok((*elem, esz, data))
            }
            _ => Err(format!("{} is not an array or slice", ty)),
        }
    }

    pub fn less(&self, ty: Ty, a: &[u8], b: &[u8]) -> Res<bool> {
        match ty.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize => {
                Ok(self.int_of(ty, a).cmp(&self.int_of(ty, b)) == Ordering::Less)
            }
            TyKind::Float(f) => Ok(self.float_of(*f, a) < self.float_of(*f, b)),
            TyKind::Bool => Ok(a[0] < b[0]),
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => {
                Ok(self.ptr_of(a) < self.ptr_of(b))
            }
            TyKind::Tuple { elems, .. } => {
                for i in 0..elems.len() {
                    let (fty, fa) = self.field_bytes(ty, a, i)?;
                    let (_, fb) = self.field_bytes(ty, b, i)?;
                    if !self.eq(fty, &fa, &fb)? {
                        return self.less(fty, &fa, &fb);
                    }
                }
                Ok(false)
            }
            TyKind::Array { .. } | TyKind::Slice { .. } => {
                let (elem, esz, da) = self.seq_elems(ty, a)?;
                let (_, _, db) = self.seq_elems(ty, b)?;
                let (na, nb) = (da.len().checked_div(esz).unwrap_or(0), db.len().checked_div(esz).unwrap_or(0));
                let (na, nb) = if esz == 0 {
                    match ty.kind() {
                        TyKind::Array { len, .. } => (*len as usize, *len as usize),
                        _ => (self.slice_parts(a).1 as usize, self.slice_parts(b).1 as usize),
                    }
                } else {
                    (na, nb)
                };
                for i in 0..na.min(nb) {
                    let (x, y) = (&da[i * esz..(i + 1) * esz], &db[i * esz..(i + 1) * esz]);
                    if !self.eq(elem, x, y)? {
                        return self.less(elem, x, y);
                    }
                }
                Ok(na < nb)
            }
            TyKind::Adt(..) => {
                if self.union_def(ty).is_some() {
                    let (ta, tb) = (self.tag_int(ty, a)?, self.tag_int(ty, b)?);
                    Ok(ta.cmp(&tb) == Ordering::Less)
                } else if let Some(u) = self.bits_struct_ty(ty) {
                    self.less(u, a, b)
                } else {
                    Err(format!("{} cannot be ordered", ty))
                }
            }
            _ => Err(format!("{} cannot be ordered", ty)),
        }
    }

    // --- equality ------------------------------------------------------------------------------------

    pub fn eq(&self, ty: Ty, a: &[u8], b: &[u8]) -> Res<bool> {
        match ty.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize => Ok(self.int_of(ty, a) == self.int_of(ty, b)),
            TyKind::Float(f) => Ok(self.float_of(*f, a) == self.float_of(*f, b)),
            TyKind::Bool => Ok((a[0] != 0) == (b[0] != 0)),
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => {
                Ok(self.ptr_of(a) == self.ptr_of(b))
            }
            TyKind::Never => Ok(true),
            TyKind::Tuple { elems, .. } => {
                for i in 0..elems.len() {
                    let (fty, fa) = self.field_bytes(ty, a, i)?;
                    let (_, fb) = self.field_bytes(ty, b, i)?;
                    if !self.eq(fty, &fa, &fb)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            TyKind::Array { len, elem, .. } => {
                let esz = self.size_of(*elem);
                for i in 0..*len as usize {
                    if !self.eq(*elem, &a[i * esz..(i + 1) * esz], &b[i * esz..(i + 1) * esz])? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            TyKind::Slice { elem, .. } => {
                let (pa, la) = self.slice_parts(a);
                let (pb, lb) = self.slice_parts(b);
                if la != lb {
                    return Ok(false);
                }
                if pa == pb || la == 0 {
                    // Same elements (NaN elements still differ from themselves).
                    if la == 0 || !matches!(elem.kind(), TyKind::Float(_)) {
                        return Ok(true);
                    }
                }
                let (_, esz, da) = self.seq_elems(ty, a)?;
                let (_, _, db) = self.seq_elems(ty, b)?;
                if esz == 1 && matches!(elem.kind(), TyKind::Int(_)) {
                    return Ok(*da == *db);
                }
                for i in 0..la as usize {
                    if !self.eq(*elem, &da[i * esz..(i + 1) * esz], &db[i * esz..(i + 1) * esz])? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            TyKind::Optional(inner) => {
                if layout::is_nullable_pointer(*inner) {
                    return Ok(self.ptr_of(a) == self.ptr_of(b));
                }
                match (self.opt_present(ty, a), self.opt_present(ty, b)) {
                    (false, false) => Ok(true),
                    (true, true) => self.eq(*inner, &self.opt_payload(ty, a), &self.opt_payload(ty, b)),
                    _ => Ok(false),
                }
            }
            TyKind::Result { err, ok } => {
                let (ea, eb) = (self.res_is_err(ty, a), self.res_is_err(ty, b));
                if ea != eb {
                    return Ok(false);
                }
                let t = if ea { *err } else { *ok };
                self.eq(t, &self.res_payload(ty, a), &self.res_payload(ty, b))
            }
            TyKind::Adt(..) => {
                let def = self.adt(ty).ok_or_else(|| format!("unknown type {}", ty))?;
                match &def.kind {
                    AdtKind::Struct { bits: Some(u), .. } => self.eq(*u, a, b),
                    AdtKind::Struct { fields, .. } => {
                        for i in 0..fields.len() {
                            let (fty, fa) = self.field_bytes(ty, a, i)?;
                            let (_, fb) = self.field_bytes(ty, b, i)?;
                            if !self.eq(fty, &fa, &fb)? {
                                return Ok(false);
                            }
                        }
                        Ok(true)
                    }
                    AdtKind::Union { .. } => {
                        let (va, vb) = (self.variant_index(ty, a)?, self.variant_index(ty, b)?);
                        if va != vb {
                            return Ok(false);
                        }
                        match self.variant_payload_ty(ty, va) {
                            None => Ok(true),
                            Some(pt) => {
                                let off = self.payload_offset(ty);
                                let sz = self.size_of(pt);
                                self.eq(pt, &a[off..off + sz], &b[off..off + sz])
                            }
                        }
                    }
                    AdtKind::CUnion { .. } => Err(format!("{} cannot be compared with `is`", ty)),
                }
            }
            _ => Err(format!("{} cannot be compared with `is`", ty)),
        }
    }

    // --- hashing ------------------------------------------------------------------------------------------

    fn mix(h: &mut u64, bytes: &[u8]) {
        for &b in bytes {
            *h ^= b as u64;
            *h = h.wrapping_mul(0x100000001b3);
        }
    }

    /// Structural hash consistent with [`Machine::eq`].
    pub fn hash_val(&self, ty: Ty, b: &[u8], h: &mut u64) -> Res<()> {
        match ty.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize => {
                Self::mix(h, &self.int_of(ty, b).to_big().to_signed_bytes_le());
            }
            TyKind::Float(f) => {
                let x = self.float_of(*f, b);
                let x = if x == 0.0 { 0.0 } else { x };
                Self::mix(h, &x.to_bits().to_le_bytes());
            }
            TyKind::Bool => Self::mix(h, &[(b[0] != 0) as u8]),
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => {
                Self::mix(h, &self.ptr_of(b).to_le_bytes())
            }
            TyKind::Never => {}
            TyKind::Tuple { elems, .. } => {
                for i in 0..elems.len() {
                    let (fty, fb) = self.field_bytes(ty, b, i)?;
                    self.hash_val(fty, &fb, h)?;
                }
            }
            TyKind::Array { len, elem, .. } => {
                let esz = self.size_of(*elem);
                for i in 0..*len as usize {
                    self.hash_val(*elem, &b[i * esz..(i + 1) * esz], h)?;
                }
            }
            TyKind::Slice { elem, .. } => {
                let (_, esz, d) = self.seq_elems(ty, b)?;
                let n = self.slice_parts(b).1;
                Self::mix(h, &n.to_le_bytes());
                for i in 0..n as usize {
                    self.hash_val(*elem, &d[i * esz..(i + 1) * esz], h)?;
                }
            }
            TyKind::Optional(inner) => {
                if layout::is_nullable_pointer(*inner) {
                    Self::mix(h, &self.ptr_of(b).to_le_bytes());
                } else if self.opt_present(ty, b) {
                    Self::mix(h, &[1]);
                    self.hash_val(*inner, &self.opt_payload(ty, b), h)?;
                } else {
                    Self::mix(h, &[0]);
                }
            }
            TyKind::Result { err, ok } => {
                let e = self.res_is_err(ty, b);
                Self::mix(h, &[e as u8]);
                self.hash_val(if e { *err } else { *ok }, &self.res_payload(ty, b), h)?;
            }
            TyKind::Adt(..) => {
                let def = self.adt(ty).ok_or_else(|| format!("unknown type {}", ty))?;
                match &def.kind {
                    AdtKind::Struct { bits: Some(u), .. } => self.hash_val(*u, b, h)?,
                    AdtKind::Struct { fields, .. } => {
                        for i in 0..fields.len() {
                            let (fty, fb) = self.field_bytes(ty, b, i)?;
                            self.hash_val(fty, &fb, h)?;
                        }
                    }
                    AdtKind::Union { .. } => {
                        let v = self.variant_index(ty, b)?;
                        Self::mix(h, &(v as u64).to_le_bytes());
                        if let Some(pt) = self.variant_payload_ty(ty, v) {
                            let off = self.payload_offset(ty);
                            self.hash_val(pt, &b[off..off + self.size_of(pt)], h)?;
                        }
                    }
                    AdtKind::CUnion { .. } => return Err(format!("{} cannot be hashed", ty)),
                }
            }
            _ => return Err(format!("{} cannot be hashed", ty)),
        }
        Ok(())
    }
}

pub fn fmt_int(i: &Int) -> String {
    match i {
        Int::S(v) => v.to_string(),
        Int::B(b) => b.to_string(),
    }
}
