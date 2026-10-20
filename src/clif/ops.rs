//! Operators, conversions and builtins on scalars and vectors, with the checks of §7.4
//! and §18, computing what the C back end computes.
//!
//! Integers are held normalized in a register of their storage size (see [`Rep::Int`]).
//! Arithmetic on up to 32-bit storage is done exactly in 64 bits and range-checked; on
//! 64-bit storage with Cranelift's overflow-reporting instructions; on 128-bit storage
//! by the runtime, which takes and returns such integers through memory.

use super::func::{FnGen, V, mf};
use super::{CResult, Rep, unsupported};
use crate::source::Span;
use crate::tir::{AdtKind, BinOp, Builtin, Expr, ExprKind, Flavor, RmwOp, UnOp};
use crate::types::{FloatTy, Ty, TyKind};
use cranelift_codegen::ir::condcodes::{FloatCC, IntCC};
use cranelift_codegen::ir::{AtomicRmwOp, InstBuilder, Type, Value, types};
use num_bigint::BigInt;
use num_traits::ToPrimitive;

/// The low 128 bits of `v` in two's complement.
pub fn bigint_bits(v: &BigInt) -> u128 {
    let bytes = v.to_signed_bytes_le();
    let fill = if v.sign() == num_bigint::Sign::Minus { 0xff } else { 0 };
    let mut b = [fill; 16];
    for (i, x) in bytes.iter().take(16).enumerate() {
        b[i] = *x;
    }
    u128::from_le_bytes(b)
}

/// `v` rounded to the nearest `f64`.
pub fn bigint_f64(v: &BigInt) -> f64 {
    v.to_f64().unwrap_or(0.0)
}

/// The bits of the `f16` nearest to `x` (ties to even).
pub fn f16_bits(x: f64) -> u16 {
    if x.is_nan() {
        return 0x7e00;
    }
    let sign = if x.is_sign_negative() { 0x8000 } else { 0 };
    if x.is_infinite() {
        return sign | 0x7c00;
    }
    match num_rational::BigRational::from_float(x) {
        Some(r) => match crate::float::round_rational(&r, FloatTy::F16) {
            Some(b) => b as u16 | sign,
            None => sign | 0x7c00,
        },
        None => sign,
    }
}

/// The smallest value of an integer of `bits` bits, as two's complement bits.
fn min_bits(bits: u32, signed: bool) -> u128 {
    if !signed {
        0
    } else if bits >= 128 {
        1u128 << 127
    } else {
        (-(1i128 << (bits - 1))) as u128
    }
}

/// The largest value of an integer of `bits` bits.
fn max_bits(bits: u32, signed: bool) -> u128 {
    match (signed, bits) {
        (true, b) => (1u128 << (b - 1)) - 1,
        (false, b) if b >= 128 => u128::MAX,
        (false, b) => (1u128 << b) - 1,
    }
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

/// The lanes of a vector type: count, lane type and lane size in bytes.
fn parts(t: Ty) -> (u64, Ty, u64) {
    let (n, lane) = t.vector_parts().expect("a vector type");
    let bytes = match lane.kind() {
        TyKind::Int(i) => i.bits as u64 / 8,
        TyKind::Float(f) => f.bits() as u64 / 8,
        _ => 1,
    };
    (n, lane, bytes)
}

impl<'f, 'g, 'a> FnGen<'f, 'g, 'a> {
    fn checks(&self) -> bool {
        self.g.opts.checks
    }

    /// The register type, width and signedness of integer type `t`.
    fn int_info(&self, t: Ty) -> Option<(Type, u32, bool)> {
        match self.g.rep(t) {
            Rep::Int { ty, bits, signed } => Some((ty, bits, signed)),
            _ => None,
        }
    }

    pub fn int_min(&mut self, ty: Type, bits: u32, signed: bool) -> Value {
        self.iconst_bits(ty, min_bits(bits, signed))
    }

    pub fn int_max(&mut self, ty: Type, bits: u32, signed: bool) -> Value {
        self.iconst_bits(ty, max_bits(bits, signed))
    }

    /// `v` (of register type `from`) as an integer of register type `to`, sign- or
    /// zero-extended, or truncated.
    pub fn resize(&mut self, v: Value, from: Type, to: Type, signed: bool) -> Value {
        if from == to {
            v
        } else if from.bits() > to.bits() {
            self.b.ins().ireduce(to, v)
        } else if signed {
            self.b.ins().sextend(to, v)
        } else {
            self.b.ins().uextend(to, v)
        }
    }

    /// The integer `v` of type `t` (or a bool) as an `i128`, sign- or zero-extended.
    pub fn widen_i128(&mut self, t: Ty, v: Value, signed: bool) -> Value {
        let ty = self.g.rep(t).ty().unwrap_or(types::I64);
        self.resize(v, ty, types::I128, signed)
    }

    /// The `i128` whose low `w` bits are set.
    pub fn mask128(&mut self, w: u32) -> Value {
        let m = if w >= 128 { u128::MAX } else { (1u128 << w) - 1 };
        self.iconst_bits(types::I128, m)
    }

    /// A temporary holding the 16 bytes of `v`, an `i128`; its address.
    fn spill128(&mut self, v: Value) -> Value {
        let a = self.slot(16, 16);
        self.b.ins().store(mf(), v, a, 0);
        a
    }

    fn load128(&mut self, a: Value) -> Value {
        self.b.ins().load(types::I128, mf(), a, 0)
    }

    /// The shift or exponent amount `v` of integer type `t` as a `u64`; a negative amount
    /// is huge, and one beyond 64 bits is `u64::MAX`.
    pub(super) fn amount64(&mut self, t: Ty, v: Value) -> Value {
        let (ty, _, signed) = self.int_info(t).unwrap_or((types::I64, 64, false));
        if ty == types::I128 {
            let (lo, hi) = self.b.ins().isplit(v);
            let big = self.b.ins().icmp_imm_s(IntCC::NotEqual, hi, 0);
            let max = self.b.ins().iconst(types::I64, -1);
            self.pick(big, max, lo)
        } else {
            self.resize(v, ty, types::I64, signed)
        }
    }

    /// The integer inside a value of a nominal type that has one: a union's tag, a `#bits`
    /// struct's integer. Its type and offset.
    pub fn adt_int(&self, t: Ty) -> Option<(Ty, u64)> {
        let TyKind::Adt(id, _) = t.kind() else { return None };
        match &self.g.prog.adts[id].kind {
            AdtKind::Union { tag, .. } => Some((*tag, self.g.offsets(t)[0])),
            AdtKind::Struct { bits: Some(u), .. } => Some((*u, 0)),
            _ => None,
        }
    }

    /// A value of `t` (a type with [`FnGen::adt_int`]) holding just the integer `x`.
    fn adt_from_int(&mut self, t: Ty, x: Value) -> V {
        let (it, off) = self.adt_int(t).unwrap();
        let a = self.temp(t);
        let size = self.g.size(t);
        self.zero(a, 0, size);
        let r = self.g.rep(it);
        let x = self.normalize(r, x);
        self.b.ins().store(mf(), x, a, off as i32);
        V::M(a)
    }

    /// A value of the optional type `t` that holds `payload` when `present` (an `i8`),
    /// else `null`; `payload` is computed only when present.
    pub fn opt_cond(&mut self, t: Ty, present: Value, payload: impl FnOnce(&mut Self) -> CResult<V>) -> CResult<V> {
        match self.g.rep(t) {
            Rep::Mem { size, align } => {
                let res = self.slot(size, align);
                self.zero(res, 0, size);
                self.if_then(present, |fg| {
                    let v = payload(fg)?;
                    let s = fg.make_some(t, v);
                    fg.write(t, s, res);
                    Ok(())
                })?;
                Ok(V::M(res))
            }
            r => {
                let ty = r.ty().unwrap_or(types::I64);
                let var = self.b.declare_var(ty);
                let z = self.zero_scalar(ty);
                self.b.def_var(var, z);
                self.if_then(present, |fg| {
                    let v = payload(fg)?;
                    let x = fg.as_scalar(t, v);
                    fg.b.def_var(var, x);
                    Ok(())
                })?;
                Ok(V::S(self.b.use_var(var)))
            }
        }
    }

    // --- #bits structs ------------------------------------------------------------------------

    /// The value of a `#bits` field of type `t` from its bits `raw` (an `i128`).
    pub fn bits_value(&mut self, t: Ty, raw: Value) -> V {
        match self.g.rep(t) {
            Rep::Bool => V::S(self.b.ins().ireduce(types::I8, raw)),
            r @ Rep::Int { ty, .. } => {
                let x = self.resize(raw, types::I128, ty, false);
                V::S(self.normalize(r, x))
            }
            Rep::Mem { .. } if self.adt_int(t).is_some() => {
                let (it, _) = self.adt_int(t).unwrap();
                let ity = self.g.rep(it).ty().unwrap();
                let x = self.resize(raw, types::I128, ity, false);
                self.adt_from_int(t, x)
            }
            _ => self.undef(t),
        }
    }

    /// The bits of the `#bits` field value `v` of type `t`, as an `i128` (only the field's
    /// width is meaningful).
    pub fn bits_raw(&mut self, t: Ty, v: V) -> Value {
        match self.g.rep(t) {
            Rep::Bool | Rep::Int { .. } => {
                let x = self.as_scalar(t, v);
                self.widen_i128(t, x, false)
            }
            Rep::Mem { .. } if self.adt_int(t).is_some() => {
                let (it, off) = self.adt_int(t).unwrap();
                let a = self.spill(t, v);
                let x = self.load_s(it, a, off);
                self.widen_i128(it, x, false)
            }
            _ => self.iconst_bits(types::I128, 0),
        }
    }

    /// A `#bits` struct of type `t` from its field values.
    pub fn bits_aggregate(&mut self, t: Ty, elems: &'a [Expr]) -> CResult<V> {
        let (u, fields) = self.g.bits_layout(t).unwrap();
        let mut acc = self.iconst_bits(types::I128, 0);
        for (x, (off, w, _)) in elems.iter().zip(fields) {
            let v = self.expr(x)?;
            let raw = self.bits_raw(x.ty, v);
            let m = self.mask128(w);
            let raw = self.b.ins().band(raw, m);
            let raw = self.b.ins().ishl_imm_s(raw, off as i64);
            acc = self.b.ins().bor(acc, raw);
        }
        let uty = self.g.rep(u).ty().unwrap();
        let x = self.resize(acc, types::I128, uty, false);
        Ok(self.adt_from_int(t, x))
    }

    /// `place = value` (§7.7).
    pub fn assign(&mut self, place: &'a Expr, value: &'a Expr) -> CResult<()> {
        // A field of a `#bits` struct (perhaps nested): bits of the outermost integer.
        if let ExprKind::Field { base, index } = &place.kind
            && let Some((_, fields)) = self.g.bits_layout(base.ty)
        {
            let (mut off, w, _) = fields[*index as usize];
            let mut outer: &'a Expr = base;
            while let ExprKind::Field { base: b, index: i } = &outer.kind
                && let Some((_, fs)) = self.g.bits_layout(b.ty)
            {
                off += fs[*i as usize].0;
                outer = b;
            }
            let (u, _) = self.g.bits_layout(outer.ty).unwrap();
            let a = self.place(outer)?;
            let v = self.expr(value)?;
            if value.ty.is_never() {
                return Ok(());
            }
            let raw = self.bits_raw(value.ty, v);
            let cur = self.load_s(u, a, 0);
            let cur = self.widen_i128(u, cur, false);
            let m = self.mask128(w);
            let m = self.b.ins().ishl_imm_s(m, off as i64);
            let raw = self.b.ins().ishl_imm_s(raw, off as i64);
            let raw = self.b.ins().band(raw, m);
            let keep = self.b.ins().band_not(cur, m);
            let new = self.b.ins().bor(keep, raw);
            let uty = self.g.rep(u).ty().unwrap();
            let new = self.resize(new, types::I128, uty, false);
            let new = self.normalize(self.g.rep(u), new);
            self.b.ins().store(mf(), new, a, 0);
            // The variable holding the struct, if `outer` is one, now has stale bits.
            if let ExprKind::Local(l) = &outer.kind
                && matches!(self.locals[l.index()], super::func::Loc::Var(_))
            {
                let v = self.read(outer.ty, a);
                self.set_local(*l, v);
            }
            return Ok(());
        }
        if let ExprKind::Local(l) = &place.kind
            && matches!(self.locals[l.index()], super::func::Loc::Var(_) | super::func::Loc::None)
        {
            let v = self.expr(value)?;
            if !value.ty.is_never() {
                self.set_local(*l, v);
            }
            return Ok(());
        }
        let p = self.place(place)?;
        match self.g.rep(value.ty) {
            Rep::Mem { .. } => self.expr_into(value, p, false),
            _ => {
                let v = self.expr(value)?;
                if !value.ty.is_never() {
                    self.write(value.ty, v, p);
                }
                Ok(())
            }
        }
    }

    // --- operators ------------------------------------------------------------------------------

    pub fn unary(&mut self, e: &'a Expr, op: UnOp, x: &'a Expr) -> CResult<V> {
        let v = self.expr(x)?;
        if x.ty.is_never() {
            return Ok(self.undef(e.ty));
        }
        let r = self.g.rep(x.ty);
        match op {
            UnOp::Not => {
                let s = self.as_scalar(x.ty, v);
                Ok(V::S(self.b.ins().bxor_imm_s(s, 1)))
            }
            UnOp::Neg => {
                let s = self.as_scalar(x.ty, v);
                match r {
                    Rep::F32 | Rep::F64 => Ok(V::S(self.b.ins().fneg(s))),
                    Rep::F16 => {
                        let m = self.iconst_bits(types::I16, 0x8000);
                        Ok(V::S(self.b.ins().bxor(s, m)))
                    }
                    Rep::Int { ty, bits, signed } => {
                        if self.checks() {
                            let min = self.int_min(ty, bits, signed);
                            let bad = self.b.ins().icmp(IntCC::Equal, s, min);
                            self.trap_if(bad, "integer overflow in `-`", e.span)?;
                        }
                        let n = self.b.ins().ineg(s);
                        Ok(V::S(self.normalize(r, n)))
                    }
                    _ => unsupported(e.span, format!("negating `{}`", x.ty)),
                }
            }
            UnOp::BitNot => match r {
                Rep::Int { .. } => {
                    let s = self.as_scalar(x.ty, v);
                    let n = self.b.ins().bnot(s);
                    Ok(V::S(self.normalize(r, n)))
                }
                Rep::Mem { .. } if self.adt_int(x.ty).is_some() => {
                    let (it, off) = self.adt_int(x.ty).unwrap();
                    let a = self.spill(x.ty, v);
                    let s = self.load_s(it, a, off);
                    let n = self.b.ins().bnot(s);
                    Ok(self.adt_from_int(e.ty, n))
                }
                _ => unsupported(e.span, format!("`~` on `{}`", x.ty)),
            },
        }
    }

    pub fn binary(&mut self, e: &'a Expr, op: BinOp, l: &'a Expr, r: &'a Expr) -> CResult<V> {
        let a = self.expr(l)?;
        let b = self.expr(r)?;
        if l.ty.is_never() || r.ty.is_never() {
            return Ok(self.undef(e.ty));
        }
        let t = l.ty;
        if matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge) {
            return Ok(V::S(match self.g.rep(t) {
                Rep::Zero => self.b.ins().iconst(types::I8, matches!(op, BinOp::Le | BinOp::Ge) as i64),
                Rep::Mem { .. } => {
                    let d = self.g.desc(t)?;
                    let d = self.data_addr(d);
                    let (pa, pb) = (self.spill(t, a), self.spill(t, b));
                    let (x, y) = if matches!(op, BinOp::Lt | BinOp::Le) { (pa, pb) } else { (pb, pa) };
                    let lt = self.rt_call("gm_clif_lt", "ppp>b", &[d, x, y])[0];
                    if matches!(op, BinOp::Le | BinOp::Ge) {
                        let eq = self.rt_call("gm_clif_eq", "ppp>b", &[d, x, y])[0];
                        self.b.ins().bor(lt, eq)
                    } else {
                        lt
                    }
                }
                _ => {
                    let (x, y) = (self.as_scalar(t, a), self.as_scalar(t, b));
                    self.compare(op, t, x, y)
                }
            }));
        }
        match self.g.rep(t) {
            Rep::F16 | Rep::F32 | Rep::F64 => {
                let (x, y) = (self.as_scalar(t, a), self.as_scalar(t, b));
                Ok(V::S(self.float_op(e.span, op, t, x, y)?))
            }
            Rep::Int { .. } => {
                let (x, y) = (self.as_scalar(t, a), self.as_scalar(t, b));
                self.int_binary(e.span, op, t, e.ty, x, y, r.ty)
            }
            Rep::Bool if matches!(op, BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor) => {
                let (x, y) = (self.as_scalar(t, a), self.as_scalar(t, b));
                Ok(V::S(self.bitwise(op, x, y)))
            }
            Rep::Mem { .. }
                if self.adt_int(t).is_some() && matches!(op, BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor) =>
            {
                let (it, off) = self.adt_int(t).unwrap();
                let (pa, pb) = (self.spill(t, a), self.spill(t, b));
                let x = self.load_s(it, pa, off);
                let y = self.load_s(it, pb, off);
                let z = self.bitwise(op, x, y);
                Ok(self.adt_from_int(e.ty, z))
            }
            _ => unsupported(e.span, format!("operator `{}` on `{}`", op_symbol(op), t)),
        }
    }

    fn bitwise(&mut self, op: BinOp, x: Value, y: Value) -> Value {
        match op {
            BinOp::BitAnd => self.b.ins().band(x, y),
            BinOp::BitOr => self.b.ins().bor(x, y),
            _ => self.b.ins().bxor(x, y),
        }
    }

    /// `a op b` on scalars of type `t` (integers, floats, bools or pointers): an `i8`.
    pub fn compare(&mut self, op: BinOp, t: Ty, a: Value, b: Value) -> Value {
        let r = self.g.rep(t);
        let float = |op: BinOp| match op {
            BinOp::Lt => FloatCC::LessThan,
            BinOp::Le => FloatCC::LessThanOrEqual,
            BinOp::Gt => FloatCC::GreaterThan,
            _ => FloatCC::GreaterThanOrEqual,
        };
        match r {
            Rep::F32 | Rep::F64 => self.b.ins().fcmp(float(op), a, b),
            Rep::F16 => {
                let (x, y) = (self.f16_to_f64(a), self.f16_to_f64(b));
                self.b.ins().fcmp(float(op), x, y)
            }
            _ => {
                let signed = matches!(r, Rep::Int { signed: true, .. });
                let cc = match (op, signed) {
                    (BinOp::Lt, true) => IntCC::SignedLessThan,
                    (BinOp::Le, true) => IntCC::SignedLessThanOrEqual,
                    (BinOp::Gt, true) => IntCC::SignedGreaterThan,
                    (BinOp::Ge, true) => IntCC::SignedGreaterThanOrEqual,
                    (BinOp::Lt, false) => IntCC::UnsignedLessThan,
                    (BinOp::Le, false) => IntCC::UnsignedLessThanOrEqual,
                    (BinOp::Gt, false) => IntCC::UnsignedGreaterThan,
                    _ => IntCC::UnsignedGreaterThanOrEqual,
                };
                self.b.ins().icmp(cc, a, b)
            }
        }
    }

    /// `a op b` on floats of type `t` (`f16` goes through `f64`, which rounds each
    /// operation correctly).
    fn float_op(&mut self, span: Span, op: BinOp, t: Ty, a: Value, b: Value) -> CResult<Value> {
        let r = self.g.rep(t);
        let (x, y) = if r == Rep::F16 { (self.f16_to_f64(a), self.f16_to_f64(b)) } else { (a, b) };
        let z = match op {
            BinOp::Add(_) => self.b.ins().fadd(x, y),
            BinOp::Sub(_) => self.b.ins().fsub(x, y),
            BinOp::Mul(_) => self.b.ins().fmul(x, y),
            BinOp::Div => self.b.ins().fdiv(x, y),
            _ => return unsupported(span, format!("operator `{}` on floats", op_symbol(op))),
        };
        Ok(if r == Rep::F16 { self.f64_to_f16(z) } else { z })
    }

    /// `a op b` on integers of type `lt` giving a `t` (`lt`, or `?lt` for `+?` and the
    /// like); `bt` is the type of `b` (a shift amount may have another type).
    #[allow(clippy::too_many_arguments)]
    pub fn int_binary(&mut self, span: Span, op: BinOp, lt: Ty, t: Ty, a: Value, b: Value, bt: Ty) -> CResult<V> {
        let (ty, bits, signed) = self.int_info(lt).unwrap();
        let rep = self.g.rep(lt);
        match op {
            BinOp::Add(fl) | BinOp::Sub(fl) | BinOp::Mul(fl) => {
                let k = match op {
                    BinOp::Add(_) => 0,
                    BinOp::Sub(_) => 1,
                    _ => 2,
                };
                let (r, o) = if ty == types::I128 {
                    let (pa, pb) = (self.spill128(a), self.spill128(b));
                    let out = self.slot(16, 16);
                    let (kv, sv, bv) = (
                        self.b.ins().iconst(types::I32, k),
                        self.b.ins().iconst(types::I32, signed as i64),
                        self.b.ins().iconst(types::I32, bits as i64),
                    );
                    let o = self.rt_call("gm_clif_arith128", "iiippp>b", &[kv, sv, bv, pa, pb, out])[0];
                    (self.load128(out), o)
                } else if ty.bits() <= 32 {
                    let x = self.resize(a, ty, types::I64, signed);
                    let y = self.resize(b, ty, types::I64, signed);
                    let w = match k {
                        0 => self.b.ins().iadd(x, y),
                        1 => self.b.ins().isub(x, y),
                        _ => self.b.ins().imul(x, y),
                    };
                    let lo = self.b.ins().iconst(types::I64, min_bits(bits, signed) as i128 as i64);
                    let hi = self.b.ins().iconst(types::I64, max_bits(bits, signed) as i64);
                    let below = self.b.ins().icmp(IntCC::SignedLessThan, w, lo);
                    let above = self.b.ins().icmp(IntCC::SignedGreaterThan, w, hi);
                    let o = self.b.ins().bor(below, above);
                    let r = self.resize(w, types::I64, ty, false);
                    (self.normalize(rep, r), o)
                } else {
                    let (w, o) = match (k, signed) {
                        (0, true) => self.b.ins().sadd_overflow(a, b),
                        (0, false) => self.b.ins().uadd_overflow(a, b),
                        (1, true) => self.b.ins().ssub_overflow(a, b),
                        (1, false) => self.b.ins().usub_overflow(a, b),
                        (_, true) => self.b.ins().smul_overflow(a, b),
                        (_, false) => self.b.ins().umul_overflow(a, b),
                    };
                    let o = if bits < 64 {
                        let (lo, hi) = (self.int_min(ty, bits, signed), self.int_max(ty, bits, signed));
                        let (cl, ch) = if signed {
                            (IntCC::SignedLessThan, IntCC::SignedGreaterThan)
                        } else {
                            (IntCC::UnsignedLessThan, IntCC::UnsignedGreaterThan)
                        };
                        let below = self.b.ins().icmp(cl, w, lo);
                        let above = self.b.ins().icmp(ch, w, hi);
                        let x = self.b.ins().bor(below, above);
                        self.b.ins().bor(o, x)
                    } else {
                        o
                    };
                    (self.normalize(rep, w), o)
                };
                let up = match (k, signed) {
                    (0, true) => self.b.ins().icmp_imm_s(IntCC::SignedGreaterThan, b, 0),
                    (1, true) => self.b.ins().icmp_imm_s(IntCC::SignedLessThan, b, 0),
                    (_, true) => {
                        let x = self.b.ins().icmp_imm_s(IntCC::SignedLessThan, a, 0);
                        let y = self.b.ins().icmp_imm_s(IntCC::SignedLessThan, b, 0);
                        self.b.ins().icmp(IntCC::Equal, x, y)
                    }
                    (1, false) => self.b.ins().iconst(types::I8, 0),
                    _ => self.b.ins().iconst(types::I8, 1),
                };
                self.finish_int(fl, lt, t, r, o, up, span, op_symbol(op))
            }
            BinOp::Shl(_) | BinOp::Shr => {
                let fl = if let BinOp::Shl(f) = op { f } else { Flavor::Checked };
                let n = self.amount64(bt, b);
                let n = if self.checks() {
                    let big = self.b.ins().icmp_imm_u(IntCC::UnsignedGreaterThanOrEqual, n, bits as i64);
                    let name = format!("{}", lt);
                    self.cold_if(big, |fg| {
                        let tn = fg.cstr(name.as_bytes())?;
                        let [f, l, c] = fg.loc(span)?;
                        fg.rt_call("gm_clif_trap_shift", "lppii", &[n, tn, f, l, c]);
                        Ok(())
                    })?;
                    n
                } else {
                    self.b.ins().urem_imm_u(n, bits as i64)
                };
                if op == BinOp::Shr {
                    return Ok(V::S(if signed { self.b.ins().sshr(a, n) } else { self.b.ins().ushr(a, n) }));
                }
                let raw = self.b.ins().ishl(a, n);
                let r = self.normalize(rep, raw);
                let back = if signed { self.b.ins().sshr(r, n) } else { self.b.ins().ushr(r, n) };
                let o = self.b.ins().icmp(IntCC::NotEqual, back, a);
                let up = self.b.ins().icmp_imm_s(
                    if signed { IntCC::SignedGreaterThan } else { IntCC::UnsignedGreaterThan },
                    a,
                    0,
                );
                self.finish_int(fl, lt, t, r, o, up, span, "<<")
            }
            BinOp::Div | BinOp::Rem => {
                let sym = op_symbol(op);
                let zero = self.b.ins().icmp_imm_s(IntCC::Equal, b, 0);
                self.trap_if(zero, "division by zero", span)?;
                if signed {
                    let min = self.int_min(ty, bits, signed);
                    let x = self.b.ins().icmp(IntCC::Equal, a, min);
                    let y = self.b.ins().icmp_imm_s(IntCC::Equal, b, -1);
                    let bad = self.b.ins().band(x, y);
                    self.trap_if(bad, &format!("integer overflow in `{sym}`"), span)?;
                }
                let div = op == BinOp::Div;
                let r = if ty == types::I128 {
                    let (pa, pb) = (self.spill128(a), self.spill128(b));
                    let out = self.slot(16, 16);
                    let kv = self.b.ins().iconst(types::I32, (!div) as i64);
                    let sv = self.b.ins().iconst(types::I32, signed as i64);
                    self.rt_call("gm_clif_divrem128", "iippp", &[kv, sv, pa, pb, out]);
                    self.load128(out)
                } else {
                    match (div, signed) {
                        (true, true) => self.b.ins().sdiv(a, b),
                        (true, false) => self.b.ins().udiv(a, b),
                        (false, true) => self.b.ins().srem(a, b),
                        (false, false) => self.b.ins().urem(a, b),
                    }
                };
                Ok(V::S(r))
            }
            BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor => Ok(V::S(self.bitwise(op, a, b))),
            _ => unreachable!("comparisons are handled by `binary`"),
        }
    }

    /// Completes an integer operation whose wrapped result is `r` and whose overflow flag
    /// is `o`, according to the flavor (§7.4.2). `up` says which way a saturating result
    /// goes.
    #[allow(clippy::too_many_arguments)]
    fn finish_int(
        &mut self,
        fl: Flavor,
        lt: Ty,
        t: Ty,
        r: Value,
        o: Value,
        up: Value,
        span: Span,
        sym: &str,
    ) -> CResult<V> {
        let (ty, bits, signed) = self.int_info(lt).unwrap();
        match fl {
            Flavor::Checked => {
                if self.checks() {
                    self.trap_if(o, &format!("integer overflow in `{sym}`"), span)?;
                }
                Ok(V::S(r))
            }
            Flavor::Wrap => Ok(V::S(r)),
            Flavor::Sat => {
                let (min, max) = (self.int_min(ty, bits, signed), self.int_max(ty, bits, signed));
                let s = self.pick(up, max, min);
                Ok(V::S(self.pick(o, s, r)))
            }
            Flavor::Opt => {
                let ok = self.b.ins().bxor_imm_s(o, 1);
                self.opt_cond(t, ok, |_| Ok(V::S(r)))
            }
        }
    }

    // --- conversions ----------------------------------------------------------------------------

    /// The `f16` bits `v` as an `f64`.
    pub fn f16_to_f64(&mut self, v: Value) -> Value {
        self.rt_call("gm_clif_f16_to_f64", "h>d", &[v])[0]
    }

    /// The `f64` `v` rounded to an `f16`'s bits.
    pub fn f64_to_f16(&mut self, v: Value) -> Value {
        self.rt_call("gm_clif_f64_to_f16", "d>h", &[v])[0]
    }

    /// The float `v` of type `t` as an `f64`.
    fn float_to_f64(&mut self, t: Ty, v: Value) -> Value {
        match self.g.rep(t) {
            Rep::F16 => self.f16_to_f64(v),
            Rep::F32 => self.b.ins().fpromote(types::F64, v),
            _ => v,
        }
    }

    /// The `f64` `v` rounded to the float type `t`.
    fn f64_to_float(&mut self, t: Ty, v: Value) -> Value {
        match self.g.rep(t) {
            Rep::F16 => self.f64_to_f16(v),
            Rep::F32 => self.b.ins().fdemote(types::F32, v),
            _ => v,
        }
    }

    /// Integer widening: `v` of integer type `from` as a `to`.
    pub fn int_convert(&mut self, from: Ty, to: Ty, v: Value) -> Value {
        let fr = self.g.rep(from);
        let tr = self.g.rep(to);
        let signed = matches!(fr, Rep::Int { signed: true, .. });
        let (Some(fty), Some(tty)) = (fr.ty(), tr.ty()) else { return v };
        let x = self.resize(v, fty, tty, signed);
        self.normalize(tr, x)
    }

    /// Float widening or narrowing: `v` of float type `from` as a `to`.
    pub fn float_convert(&mut self, from: Ty, to: Ty, v: Value) -> Value {
        let (fr, tr) = (self.g.rep(from), self.g.rep(to));
        if fr == tr {
            return v;
        }
        match (fr, tr) {
            (Rep::F32, Rep::F64) => self.b.ins().fpromote(types::F64, v),
            (Rep::F64, Rep::F32) => self.b.ins().fdemote(types::F32, v),
            _ => {
                let d = self.float_to_f64(from, v);
                self.f64_to_float(to, d)
            }
        }
    }

    /// The integer `v` (of register type `ty`, signed if `signed`) as a float of type `to`.
    fn int_to_float(&mut self, v: Value, ty: Type, signed: bool, to: Ty) -> Value {
        let tr = self.g.rep(to);
        let fty = if tr == Rep::F32 { types::F32 } else { types::F64 };
        let f = if ty == types::I128 {
            let p = self.spill128(v);
            let s = self.b.ins().iconst(types::I32, signed as i64);
            if fty == types::F32 {
                self.rt_call("gm_clif_int_to_f32", "pi>f", &[p, s])[0]
            } else {
                self.rt_call("gm_clif_int_to_f64", "pi>d", &[p, s])[0]
            }
        } else {
            let x = self.resize(v, ty, types::I64, signed);
            if signed { self.b.ins().fcvt_from_sint(fty, x) } else { self.b.ins().fcvt_from_uint(fty, x) }
        };
        if tr == Rep::F16 { self.f64_to_f16(f) } else { f }
    }

    /// The `f64` `r`, an integer value in range of integer type `to`, as a `to`.
    fn f64_to_int(&mut self, r: Value, to: Ty) -> Value {
        let (ty, bits, signed) = self.int_info(to).unwrap();
        if ty == types::I128 {
            let out = self.slot(16, 16);
            let s = self.b.ins().iconst(types::I32, signed as i64);
            self.rt_call("gm_clif_f64_to_int", "dip", &[r, s, out]);
            return self.load128(out);
        }
        let x = if !signed && bits > 63 {
            self.b.ins().fcvt_to_uint_sat(types::I64, r)
        } else {
            self.b.ins().fcvt_to_sint_sat(types::I64, r)
        };
        self.resize(x, types::I64, ty, false)
    }

    /// The numeric conversions `T(x)`, `.trunc`, `.round`, `.floor`, `.ceil`, `.checked`,
    /// `.sat` of `v` of type `src` (§6.5).
    fn convert(&mut self, e: &'a Expr, op: Builtin, src: Ty, v: V) -> CResult<V> {
        let t = e.ty;
        let target = if op == Builtin::Checked { t.optional_inner().unwrap_or(t) } else { t };
        // The source as a number: an integer (bools and nominal types' integers too) or a
        // float.
        let (sv, sty, ssigned, float) = match self.g.rep(src) {
            Rep::Int { ty, signed, .. } => (self.as_scalar(src, v), ty, signed, false),
            Rep::Bool => (self.as_scalar(src, v), types::I8, false, false),
            Rep::F16 | Rep::F32 | Rep::F64 => {
                let x = self.as_scalar(src, v);
                (self.float_to_f64(src, x), types::F64, true, true)
            }
            Rep::Mem { .. } if self.adt_int(src).is_some() => {
                let (it, off) = self.adt_int(src).unwrap();
                let a = self.spill(src, v);
                let x = self.load_s(it, a, off);
                let (ty, _, signed) = self.int_info(it).unwrap();
                (x, ty, signed, false)
            }
            _ => return unsupported(e.span, format!("converting from `{}`", src)),
        };
        match self.g.rep(target) {
            Rep::Int { ty, bits, signed } if !float => {
                let rep = self.g.rep(target);
                // Compare in a width that holds both types.
                let w = if ty == types::I128 || sty == types::I128 { types::I128 } else { types::I64 };
                let x = self.resize(sv, sty, w, ssigned);
                let neg = if ssigned {
                    self.b.ins().icmp_imm_s(IntCC::SignedLessThan, x, 0)
                } else {
                    self.b.ins().iconst(types::I8, 0)
                };
                let max = self.iconst_bits(w, max_bits(bits, signed));
                let le_max = self.b.ins().icmp(IntCC::UnsignedLessThanOrEqual, x, max);
                let neg_ok = if signed {
                    let min = self.iconst_bits(w, min_bits(bits, signed));
                    self.b.ins().icmp(IntCC::SignedGreaterThanOrEqual, x, min)
                } else {
                    self.b.ins().iconst(types::I8, 0)
                };
                let fits = self.pick(neg, neg_ok, le_max);
                let r = self.resize(x, w, ty, false);
                let wrapped = self.normalize(rep, r);
                match op {
                    Builtin::Checked => self.opt_cond(t, fits, |_| Ok(V::S(wrapped))),
                    Builtin::Sat => {
                        let (min, max) = (self.int_min(ty, bits, signed), self.int_max(ty, bits, signed));
                        let s = self.pick(neg, min, max);
                        Ok(V::S(self.pick(fits, wrapped, s)))
                    }
                    _ => Ok(V::S(wrapped)),
                }
            }
            Rep::Int { ty, bits, signed } => {
                let x = sv;
                let r = match op {
                    Builtin::Round => self.b.ins().nearest(x),
                    Builtin::Floor => self.b.ins().floor(x),
                    Builtin::Ceil => self.b.ins().ceil(x),
                    _ => self.b.ins().trunc(x),
                };
                let lo = if signed { -(2f64.powi(bits as i32 - 1)) } else { 0.0 };
                let hi = 2f64.powi(if signed { bits as i32 - 1 } else { bits as i32 });
                let (lov, hiv) = (self.b.ins().f64const(lo), self.b.ins().f64const(hi));
                let ge = self.b.ins().fcmp(FloatCC::GreaterThanOrEqual, r, lov);
                let lt = self.b.ins().fcmp(FloatCC::LessThan, r, hiv);
                let in_range = self.b.ins().band(ge, lt);
                match op {
                    Builtin::Checked => {
                        let exact = self.b.ins().fcmp(FloatCC::Equal, x, r);
                        let ok = self.b.ins().band(in_range, exact);
                        self.opt_cond(t, ok, |fg| Ok(V::S(fg.f64_to_int(r, target))))
                    }
                    Builtin::Sat => {
                        let nan = self.b.ins().fcmp(FloatCC::Unordered, x, x);
                        let (min, max) = (self.int_min(ty, bits, signed), self.int_max(ty, bits, signed));
                        let zero = self.iconst(ty, 0);
                        let below = self.b.ins().fcmp(FloatCC::LessThan, r, lov);
                        let v = self.cond_value(ty, in_range, |fg| Ok(fg.f64_to_int(r, target)), |_| Ok(zero))?;
                        let out = self.pick(below, min, max);
                        let v = self.pick(in_range, v, out);
                        Ok(V::S(self.pick(nan, zero, v)))
                    }
                    _ => {
                        if self.checks() {
                            let bad = self.b.ins().bxor_imm_s(in_range, 1);
                            self.trap_if(bad, "float to integer conversion out of range", e.span)?;
                            Ok(V::S(self.f64_to_int(r, target)))
                        } else {
                            let zero = self.iconst(ty, 0);
                            Ok(V::S(self.cond_value(ty, in_range, |fg| Ok(fg.f64_to_int(r, target)), |_| Ok(zero))?))
                        }
                    }
                }
            }
            Rep::F16 | Rep::F32 | Rep::F64 => {
                let r = if float { self.f64_to_float(target, sv) } else { self.int_to_float(sv, sty, ssigned, target) };
                if op == Builtin::Checked {
                    let one = self.b.ins().iconst(types::I8, 1);
                    return self.opt_cond(t, one, |_| Ok(V::S(r)));
                }
                Ok(V::S(r))
            }
            // An integer to a `#bits` struct: its bits, wrapped to the integer type.
            Rep::Mem { .. } if !float && self.g.bits_layout(target).is_some() => {
                let (u, _) = self.g.bits_layout(target).unwrap();
                let uty = self.g.rep(u).ty().unwrap();
                let x = self.resize(sv, sty, uty, ssigned);
                Ok(self.adt_from_int(target, x))
            }
            _ => unsupported(e.span, format!("converting to `{}`", target)),
        }
    }

    // --- builtins -----------------------------------------------------------------------------

    /// `@asm(template, outputs, inputs, clobbers)` (§19.5). Cranelift has no inline
    /// assembly, so each use becomes a C function holding the `__asm__` statement, compiled
    /// with the runtime; it is called with an array of the addresses of the outputs and of
    /// the inputs, and its operands are those places.
    fn asm(&mut self, args: &'a [Expr], outputs: usize, inputs: usize) -> CResult<V> {
        let text = |e: &Expr| match &e.kind {
            ExprKind::Str(s) => crate::cgen::c_string(s),
            _ => unreachable!("`@asm` strings are literals"),
        };
        let n = outputs + inputs;
        let table = self.slot(8 * n.max(1) as u64, 8);
        let mut operands = [vec![], vec![]];
        for i in 0..n {
            let (c, x) = (&args[1 + 2 * i], &args[2 + 2 * i]);
            let (addr, t) = if i < outputs {
                let TyKind::Ptr { pointee, .. } = x.ty.kind() else { unreachable!("an `@asm` output is a pointer") };
                (self.expr_s(x)?, *pointee)
            } else {
                let v = self.expr(x)?;
                (self.spill(x.ty, v), x.ty)
            };
            self.b.ins().store(mf(), addr, table, 8 * i as i32);
            let ct = self.asm_c_type(t, x.span)?;
            operands[(i >= outputs) as usize].push(format!("{}(*({} *)a[{}])", text(c), ct, i));
        }
        let clobbers: Vec<String> = args[1 + 2 * n..].iter().map(text).collect();
        let name = format!("gm_clif_asm{}", self.g.asm.len());
        self.g.asm.push(format!(
            "void {name}(void **a) {{\n    __asm__ volatile ({} : {} : {} : {});\n}}\n",
            text(&args[0]),
            operands[0].join(", "),
            operands[1].join(", "),
            clobbers.join(", ")
        ));
        let mut sig = cranelift_codegen::ir::Signature::new(self.g.cc);
        sig.params.push(cranelift_codegen::ir::AbiParam::new(types::I64));
        let id = cranelift_module::Module::declare_function(
            &mut self.g.module,
            &name,
            cranelift_module::Linkage::Import,
            &sig,
        )
        .map_err(super::module_err)?;
        self.call(id, &[table]);
        Ok(V::Z)
    }

    /// The C type of an `@asm` operand of type `t`.
    fn asm_c_type(&self, t: Ty, span: Span) -> CResult<String> {
        Ok(match self.g.rep(t) {
            Rep::Int { bits: 128, signed, .. } => if signed { "__int128" } else { "unsigned __int128" }.into(),
            Rep::Int { ty, signed, .. } => format!("{}int{}_t", if signed { "" } else { "u" }, ty.bits()),
            Rep::Bool => "_Bool".into(),
            Rep::F32 => "float".into(),
            Rep::F64 => "double".into(),
            Rep::Ptr => "void *".into(),
            Rep::Mem { size, align } => format!("struct {{ _Alignas({align}) unsigned char b[{size}]; }}"),
            Rep::F16 | Rep::Zero => return unsupported(span, format!("an `@asm` operand of type `{}`", t)),
        })
    }

    pub fn builtin(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<V> {
        if let Some(r) = self.vector_builtin(e, op, args) {
            return r;
        }
        let t = e.ty;
        let span = e.span;
        match op {
            Builtin::Assert => {
                // Fast builds remove `assert`, argument and all (§21.6).
                if !self.checks() {
                    return Ok(V::Z);
                }
                let c = self.expr_s(&args[0])?;
                let bad = self.b.ins().icmp_imm_s(IntCC::Equal, c, 0);
                let msg = &args[1];
                self.cold_if(bad, |fg| {
                    let m = fg.expr_m(msg)?;
                    let (p, l) = fg.slice_parts(m);
                    let [f, ln, c] = fg.loc(span)?;
                    fg.rt_call("gm_clif_trap_assert", "plpii", &[p, l, f, ln, c]);
                    Ok(())
                })?;
                return Ok(V::Z);
            }
            Builtin::Fiber => return self.fiber_builtin(e, args),
            Builtin::Asm { outputs, inputs } => return self.asm(args, outputs as usize, inputs as usize),
            Builtin::Cover(_) => return unsupported(span, "coverage counters"),
            Builtin::StaticAssert(_) => return unsupported(span, "a deferred `@static_assert`"),
            Builtin::Sys(sop) => return self.sys(e, sop, args),
            Builtin::Unreachable => {
                self.trap("reached `@unreachable()`", span)?;
                return Ok(self.undef(t));
            }
            Builtin::Get | Builtin::GetPtr | Builtin::First | Builtin::Last => return self.get(e, op, args),
            Builtin::HeapAlloc => {
                let s = self.expr_s(&args[0])?;
                let a = self.expr_s(&args[1])?;
                let z = self.b.ins().icmp_imm_s(IntCC::Equal, a, 0);
                let m = self.b.ins().iadd_imm_s(a, -1);
                let m = self.b.ins().band(a, m);
                let nz = self.b.ins().icmp_imm_s(IntCC::NotEqual, m, 0);
                let bad = self.b.ins().bor(z, nz);
                self.trap_if(bad, "allocation alignment is not a power of two", span)?;
                return Ok(V::S(self.rt_call("gm_heap_alloc", "ll>p", &[s, a])[0]));
            }
            Builtin::HeapFree => {
                let p = self.expr_s(&args[0])?;
                let s = self.expr_s(&args[1])?;
                let a = self.expr_s(&args[2])?;
                let [f, l, c] = self.loc(span)?;
                self.rt_call("gm_heap_free", "pllpii", &[p, s, a, f, l, c]);
                return Ok(V::Z);
            }
            Builtin::AllocTag | Builtin::AllocCheck => {
                if !self.g.opts.debug {
                    return Ok(V::Z);
                }
                let mut v = vec![];
                for x in &args[..3] {
                    v.push(self.expr_s(x)?);
                }
                let fa = self.expr_m(&args[3])?;
                let (fp, fl) = self.slice_parts(fa);
                let line = self.expr_s(&args[4])?;
                let line = self.int_convert(args[4].ty, Ty::u32(), line);
                let col = self.expr_s(&args[5])?;
                let col = self.int_convert(args[5].ty, Ty::u32(), col);
                if op == Builtin::AllocTag {
                    self.rt_call("gm_alloc_tag", "ppppl ii", &[v[0], v[1], v[2], fp, fl, line, col]);
                } else {
                    // The check traps at its own location, like the interpreter.
                    let [f, l, c] = self.loc(span)?;
                    self.rt_call("gm_alloc_check", "ppppliipii", &[v[0], v[1], v[2], fp, fl, line, col, f, l, c]);
                }
                return Ok(V::Z);
            }
            _ => {}
        }
        let mut vals = vec![];
        for a in args {
            vals.push(self.expr(a)?);
        }
        if args.iter().any(|a| a.ty.is_never()) {
            return Ok(self.undef(t));
        }
        let at = args[0].ty;
        let ar = self.g.rep(at);
        let s = |fg: &mut Self, i: usize| fg.as_scalar(args[i].ty, vals[i]);
        match op {
            Builtin::Convert
            | Builtin::Trunc
            | Builtin::Round
            | Builtin::Floor
            | Builtin::Ceil
            | Builtin::Checked
            | Builtin::Sat => self.convert(e, op, at, vals[0]),
            Builtin::Bits => {
                let a = self.spill(at, vals[0]);
                let size = self.g.size(t);
                let r = self.temp(t);
                self.zero(r, 0, size);
                let n = size.min(self.g.size(at));
                self.copy(r, a, n);
                Ok(self.read(t, r))
            }
            Builtin::Cast => {
                let v = s(self, 0);
                let al = self.pointee_align(t);
                if al > 1 && self.checks() {
                    let m = self.b.ins().band_imm_u(v, (al - 1) as i64);
                    let bad = self.b.ins().icmp_imm_s(IntCC::NotEqual, m, 0);
                    self.cold_if(bad, |fg| {
                        let alv = fg.b.ins().iconst(types::I32, al as i64);
                        let [f, l, c] = fg.loc(span)?;
                        fg.rt_call("gm_trap_misaligned", "lipii", &[v, alv, f, l, c]);
                        Ok(())
                    })?;
                }
                Ok(V::S(v))
            }
            Builtin::Addr | Builtin::ToPtr => Ok(V::S(s(self, 0))),
            Builtin::PtrAdd | Builtin::PtrSub | Builtin::PtrDiff => {
                let TyKind::ManyPtr { elem, .. } = at.kind() else { return unsupported(span, "pointer arithmetic") };
                let stride = self.g.size(*elem);
                let (p, x) = (s(self, 0), s(self, 1));
                Ok(V::S(match op {
                    Builtin::PtrDiff if stride == 0 => self.b.ins().iconst(types::I64, 0),
                    Builtin::PtrDiff => {
                        let d = self.b.ins().isub(p, x);
                        let k = self.b.ins().iconst(types::I64, stride as i64);
                        self.b.ins().sdiv(d, k)
                    }
                    _ => {
                        let x = self.as_index(args[1].ty, x);
                        let off = self.b.ins().imul_imm_u(x, stride as i64);
                        if op == Builtin::PtrAdd { self.b.ins().iadd(p, off) } else { self.b.ins().isub(p, off) }
                    }
                }))
            }
            Builtin::Tag => {
                let (it, off) = self.adt_int(at).expect("a union");
                let a = self.spill(at, vals[0]);
                let x = self.load_s(it, a, off);
                Ok(V::S(self.int_convert(it, t, x)))
            }
            Builtin::FromTag => {
                let TyKind::Optional(u) = t.kind() else { return unsupported(span, "from_tag") };
                let u = *u;
                let AdtKind::Union { variants, .. } = &self.adt_def(u).kind else {
                    return unsupported(span, "from_tag");
                };
                let x = s(self, 0);
                let res = self.temp(t);
                let size = self.g.size(t);
                self.zero(res, 0, size);
                for (i, v) in variants.iter().enumerate() {
                    if v.payload.is_some() {
                        continue;
                    }
                    let lit = self.int_lit(at, &v.tag);
                    let eq = self.b.ins().icmp(IntCC::Equal, x, lit);
                    self.if_then(eq, |fg| {
                        let var = fg.make_variant(u, i, None);
                        let some = fg.make_some(t, var);
                        fg.write(t, some, res);
                        Ok(())
                    })?;
                }
                Ok(V::M(res))
            }
            Builtin::Min | Builtin::Max => {
                let (a, b) = (s(self, 0), s(self, 1));
                Ok(V::S(self.min_max(op == Builtin::Min, at, a, b)))
            }
            Builtin::Clamp => {
                let (x, lo, hi) = (s(self, 0), s(self, 1), s(self, 2));
                if matches!(ar, Rep::F16 | Rep::F32 | Rep::F64) {
                    let m = self.min_max(false, at, x, lo);
                    Ok(V::S(self.min_max(true, at, m, hi)))
                } else {
                    let below = self.compare(BinOp::Lt, at, x, lo);
                    let above = self.compare(BinOp::Gt, at, x, hi);
                    let y = self.pick(above, hi, x);
                    Ok(V::S(self.pick(below, lo, y)))
                }
            }
            Builtin::Abs => {
                let v = s(self, 0);
                Ok(V::S(self.abs(span, at, v, self.checks())?))
            }
            Builtin::AbsDiff => {
                let (a, b) = (s(self, 0), s(self, 1));
                let gt = self.compare(BinOp::Gt, at, a, b);
                let d1 = self.b.ins().isub(a, b);
                let d2 = self.b.ins().isub(b, a);
                let d = self.pick(gt, d1, d2);
                Ok(V::S(self.normalize(self.g.rep(t), d)))
            }
            Builtin::IsNan | Builtin::IsInf | Builtin::IsFinite => {
                let v = s(self, 0);
                let x = self.float_to_f64(at, v);
                let inf = self.b.ins().f64const(f64::INFINITY);
                let a = self.b.ins().fabs(x);
                Ok(V::S(match op {
                    Builtin::IsNan => self.b.ins().fcmp(FloatCC::Unordered, x, x),
                    Builtin::IsInf => self.b.ins().fcmp(FloatCC::Equal, a, inf),
                    _ => self.b.ins().fcmp(FloatCC::LessThan, a, inf),
                }))
            }
            Builtin::SignBit => {
                let v = s(self, 0);
                Ok(V::S(self.sign_bit(ar, v)))
            }
            Builtin::Sqrt | Builtin::Fma => {
                let x = s(self, 0);
                Ok(V::S(if ar == Rep::F16 {
                    let x = self.f16_to_f64(x);
                    let r = if op == Builtin::Sqrt {
                        self.b.ins().sqrt(x)
                    } else {
                        let (y, z) = (s(self, 1), s(self, 2));
                        let (y, z) = (self.f16_to_f64(y), self.f16_to_f64(z));
                        self.b.ins().fma(x, y, z)
                    };
                    self.f64_to_f16(r)
                } else if op == Builtin::Sqrt {
                    self.b.ins().sqrt(x)
                } else {
                    let (y, z) = (s(self, 1), s(self, 2));
                    self.b.ins().fma(x, y, z)
                }))
            }
            Builtin::Rotl | Builtin::Rotr => {
                let (x, k) = (s(self, 0), s(self, 1));
                let (ty, bits, _) = self.int_info(at).unwrap();
                if bits == ty.bits() && ty != types::I128 {
                    let k = self.amount64(args[1].ty, k);
                    return Ok(V::S(if op == Builtin::Rotl {
                        self.b.ins().rotl(x, k)
                    } else {
                        self.b.ins().rotr(x, k)
                    }));
                }
                let k = self.amount64(args[1].ty, k);
                // A right rotation by k is a left rotation by bits - k mod bits.
                let k = if op == Builtin::Rotl {
                    k
                } else {
                    let m = self.b.ins().urem_imm_u(k, bits as i64);
                    let b = self.b.ins().iconst(types::I64, bits as i64);
                    self.b.ins().isub(b, m)
                };
                Ok(V::S(self.bitop_glue(5, at, t, x, k)))
            }
            Builtin::Clz | Builtin::Ctz | Builtin::Popcount | Builtin::Bswap | Builtin::Bitreverse => {
                let x = s(self, 0);
                let (ty, bits, _) = self.int_info(at).unwrap();
                if op == Builtin::Bswap && !bits.is_multiple_of(8) {
                    return unsupported(span, "bswap of a partial byte");
                }
                let native = bits == ty.bits() && ty != types::I128 && !(op == Builtin::Bswap && bits == 8);
                if native {
                    let r = match op {
                        Builtin::Clz => self.b.ins().clz(x),
                        Builtin::Ctz => self.b.ins().ctz(x),
                        Builtin::Popcount => self.b.ins().popcnt(x),
                        Builtin::Bswap => self.b.ins().bswap(x),
                        _ => self.b.ins().bitrev(x),
                    };
                    let tr = self.g.rep(t);
                    let r = self.resize(r, ty, tr.ty().unwrap(), false);
                    return Ok(V::S(self.normalize(tr, r)));
                }
                let k = match op {
                    Builtin::Clz => 0,
                    Builtin::Ctz => 1,
                    Builtin::Popcount => 2,
                    Builtin::Bswap => 3,
                    _ => 4,
                };
                let z = self.b.ins().iconst(types::I64, 0);
                Ok(V::S(self.bitop_glue(k, at, t, x, z)))
            }
            Builtin::Pow(fl) => {
                let Some((ty, bits, signed)) = self.int_info(at) else {
                    return unsupported(span, "float pow");
                };
                let base = s(self, 0);
                let n = s(self, 1);
                let n = self.amount64(args[1].ty, n);
                let b128 = self.resize(base, ty, types::I128, signed);
                let pb = self.spill128(b128);
                let out = self.slot(16, 16);
                let (sv, bv, stv) = (
                    self.b.ins().iconst(types::I32, signed as i64),
                    self.b.ins().iconst(types::I32, bits as i64),
                    self.b.ins().iconst(types::I32, (ty.bits() / 8) as i64),
                );
                let o = self.rt_call("gm_clif_pow", "iiipl p>b", &[sv, bv, stv, pb, n, out])[0];
                let r = self.load128(out);
                let r = self.resize(r, types::I128, ty, false);
                let fl = if fl == Flavor::Checked {
                    if self.checks() {
                        // The message names the operands, as the interpreter's does.
                        self.cold_if(o, |fg| {
                            let [f, l, c] = fg.loc(span)?;
                            fg.rt_call("gm_clif_trap_pow", "piilpii", &[pb, bv, sv, n, f, l, c]);
                            Ok(())
                        })?;
                    }
                    Flavor::Wrap
                } else {
                    fl
                };
                let pos = self.b.ins().icmp_imm_s(IntCC::SignedGreaterThan, base, 0);
                let pos = if signed { pos } else { self.b.ins().icmp_imm_s(IntCC::NotEqual, base, 0) };
                let odd = self.b.ins().band_imm_u(n, 1);
                let even = self.b.ins().icmp_imm_s(IntCC::Equal, odd, 0);
                let up = self.b.ins().bor(pos, even);
                self.finish_int(fl, at, t, r, o, up, span, "pow")
            }
            Builtin::RangeLen => {
                let (lo, hi) = (s(self, 0), s(self, 1));
                let (ty, _, signed) = self.int_info(at).unwrap();
                let gt = self.compare(BinOp::Gt, at, hi, lo);
                let w = if ty == types::I128 { types::I128 } else { types::I64 };
                let (l, h) = (self.resize(lo, ty, w, signed), self.resize(hi, ty, w, signed));
                let d = self.b.ins().isub(h, l);
                let d = self.resize(d, w, types::I64, false);
                let z = self.b.ins().iconst(types::I64, 0);
                Ok(V::S(self.pick(gt, d, z)))
            }
            Builtin::CstrLen => {
                let p = s(self, 0);
                Ok(V::S(self.rt_call("gm_clif_strlen", "p>l", &[p])[0]))
            }
            Builtin::AtomicLoad
            | Builtin::AtomicStore
            | Builtin::AtomicSwap
            | Builtin::AtomicCas
            | Builtin::AtomicRmw(_)
            | Builtin::VolatileLoad
            | Builtin::VolatileStore => self.atomic(e, op, args, &vals),
            Builtin::Fence => {
                self.b.ins().fence();
                Ok(V::Z)
            }
            Builtin::BlackBox => {
                if matches!(self.g.rep(t), Rep::Zero) {
                    return Ok(vals[0]);
                }
                let a = self.temp(t);
                self.write(t, vals[0], a);
                self.rt_call("gm_clif_black_box", "p", &[a]);
                Ok(self.read(t, a))
            }
            Builtin::Hash => {
                let d = self.g.desc(at)?;
                let d = self.data_addr(d);
                let a = self.spill(at, vals[0]);
                let seed = s(self, 1);
                let seed = self.int_convert(args[1].ty, Ty::u64(), seed);
                let k = self.b.ins().iconst(types::I64, 0xcbf29ce484222325u64 as i64);
                let seed = self.b.ins().bxor(seed, k);
                Ok(V::S(self.rt_call("gm_clif_hash", "ppl>l", &[d, a, seed])[0]))
            }
            _ => unsupported(span, format!("the builtin {:?}", op)),
        }
    }

    /// The alignment the pointer type `t` needs (a zero-sized pointee needs none).
    fn pointee_align(&self, t: Ty) -> u64 {
        match t.kind() {
            TyKind::Ptr { pointee: p, .. } | TyKind::ManyPtr { elem: p, .. } => {
                if self.g.size(*p) == 0 {
                    1
                } else {
                    self.g.align(*p)
                }
            }
            TyKind::Optional(x) => self.pointee_align(*x),
            _ => 1,
        }
    }

    /// `min(a, b)` or `max(a, b)` of scalars of type `t`; floats follow the C back end's
    /// rules for NaN and signed zeros.
    fn min_max(&mut self, min: bool, t: Ty, a: Value, b: Value) -> Value {
        match self.g.rep(t) {
            Rep::F16 | Rep::F32 | Rep::F64 => {
                let (x, y) = (self.float_to_f64(t, a), self.float_to_f64(t, b));
                let f = if min { "gm_clif_fmin" } else { "gm_clif_fmax" };
                let r = self.rt_call(f, "dd>d", &[x, y])[0];
                self.f64_to_float(t, r)
            }
            _ => {
                let c = self.compare(if min { BinOp::Lt } else { BinOp::Gt }, t, a, b);
                self.pick(c, a, b)
            }
        }
    }

    /// `c ? a : b` of integers. A 128-bit choice is a branch: Cranelift turns a `select`
    /// of a comparison into a minimum or maximum, which it cannot emit for 128 bits.
    pub(super) fn pick(&mut self, c: Value, a: Value, b: Value) -> Value {
        let ty = self.b.func.dfg.value_type(a);
        if ty != types::I128 {
            return self.b.ins().select(c, a, b);
        }
        self.cond_value(ty, c, |_| Ok(a), |_| Ok(b)).expect("no errors")
    }

    /// Whether the sign bit of the float `v` (of representation `r`) is set: an `i8`.
    fn sign_bit(&mut self, r: Rep, v: Value) -> Value {
        let bits = match r {
            Rep::F32 => self.b.ins().bitcast(types::I32, mf(), v),
            Rep::F64 => self.b.ins().bitcast(types::I64, mf(), v),
            _ => v,
        };
        self.b.ins().icmp_imm_s(IntCC::SignedLessThan, bits, 0)
    }

    /// `|v|` of type `t`; `check` traps on the most negative integer.
    fn abs(&mut self, span: Span, t: Ty, v: Value, check: bool) -> CResult<Value> {
        let r = self.g.rep(t);
        match r {
            Rep::F16 | Rep::F32 | Rep::F64 => {
                // -v when v < 0, or v is -0.0; NaN keeps its sign, as in C.
                let x = self.float_to_f64(t, v);
                let z = self.b.ins().f64const(0.0);
                let neg = self.b.ins().fcmp(FloatCC::LessThan, x, z);
                let zero = self.b.ins().fcmp(FloatCC::Equal, x, z);
                let sb = self.sign_bit(r, v);
                let nz = self.b.ins().band(zero, sb);
                let c = self.b.ins().bor(neg, nz);
                let n = if r == Rep::F16 {
                    let m = self.iconst_bits(types::I16, 0x8000);
                    self.b.ins().bxor(v, m)
                } else {
                    self.b.ins().fneg(v)
                };
                Ok(self.pick(c, n, v))
            }
            Rep::Int { ty, bits, signed: true } => {
                if check {
                    let min = self.int_min(ty, bits, true);
                    let bad = self.b.ins().icmp(IntCC::Equal, v, min);
                    self.trap_if(bad, "integer overflow in `abs`", span)?;
                }
                let neg = self.b.ins().icmp_imm_s(IntCC::SignedLessThan, v, 0);
                let n = self.b.ins().ineg(v);
                let n = self.normalize(r, n);
                Ok(self.pick(neg, n, v))
            }
            _ => Ok(v),
        }
    }

    /// Bit operation `k` of the runtime's `gm_clif_bitop` on `x` of integer type `at`,
    /// giving a `t`.
    fn bitop_glue(&mut self, k: i64, at: Ty, t: Ty, x: Value, amount: Value) -> Value {
        let (ty, bits, _) = self.int_info(at).unwrap();
        let x = self.resize(x, ty, types::I128, false);
        let p = self.spill128(x);
        let out = self.slot(16, 16);
        let kv = self.b.ins().iconst(types::I32, k);
        let bv = self.b.ins().iconst(types::I32, bits as i64);
        self.rt_call("gm_clif_bitop", "ipilp", &[kv, p, bv, amount, out]);
        let r = self.load128(out);
        let tr = self.g.rep(t);
        let r = self.resize(r, types::I128, tr.ty().unwrap(), false);
        self.normalize(tr, r)
    }

    /// `@name(args)` of a system service: calls the runtime's `gm_name`, with slices passed
    /// as a pointer and a length.
    fn sys(&mut self, e: &'a Expr, op: crate::tir::SysOp, args: &'a [Expr]) -> CResult<V> {
        let mut sig = String::new();
        let mut cargs = vec![];
        for a in args {
            if matches!(a.ty.kind(), TyKind::Slice { .. }) {
                let m = self.expr_m(a)?;
                let (p, l) = self.slice_parts(m);
                cargs.extend([p, l]);
                sig.push_str("pl");
            } else {
                let v = self.expr_s(a)?;
                let ty = self.g.rep(a.ty).ty().unwrap_or(types::I64);
                sig.push(match ty.bits() {
                    8 => 'b',
                    16 => 'h',
                    32 => 'i',
                    _ => 'l',
                });
                cargs.push(v);
            }
        }
        let unit = matches!(self.g.rep(e.ty), Rep::Zero);
        if !unit {
            // The result is as wide as its type: `@cpu_count` returns 32 bits.
            sig.push('>');
            sig.push(match self.g.rep(e.ty).ty().map_or(64, |t| t.bits()) {
                8 => 'b',
                16 => 'h',
                32 => 'i',
                _ => 'l',
            });
        }
        let r = self.rt_call(sys_symbol(op), &sig, &cargs);
        Ok(if unit { V::Z } else { V::S(r[0]) })
    }

    /// `a.get(i)`, `a.get_ptr(i)`, `a.first()`, `a.last()`: `null` when out of range.
    fn get(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<V> {
        let t = e.ty;
        let base = &args[0];
        let (ptr, len) = match base.ty.kind() {
            TyKind::Slice { .. } => {
                let a = self.expr_m(base)?;
                self.slice_parts(a)
            }
            TyKind::Array { len, .. } => {
                let len = *len;
                let p = self.place(base)?;
                (p, self.b.ins().iconst(types::I64, len as i64))
            }
            TyKind::Ptr { pointee, .. } => match pointee.kind() {
                TyKind::Array { len, .. } => {
                    let len = *len;
                    let p = self.expr_s(base)?;
                    (p, self.b.ins().iconst(types::I64, len as i64))
                }
                _ => return unsupported(e.span, "get on a pointer"),
            },
            _ => return unsupported(e.span, format!("get on `{}`", base.ty)),
        };
        let i = match op {
            Builtin::First => self.b.ins().iconst(types::I64, 0),
            Builtin::Last => self.b.ins().iadd_imm_s(len, -1),
            _ => {
                let v = self.expr_s(&args[1])?;
                self.as_bound(args[1].ty, v)
            }
        };
        let Some(inner) = t.optional_inner() else { return unsupported(e.span, "get") };
        let elem = match (op, inner.kind()) {
            (Builtin::GetPtr, TyKind::Ptr { pointee, .. }) => *pointee,
            _ => inner,
        };
        let esz = self.g.size(elem);
        let present = self.b.ins().icmp(IntCC::UnsignedLessThan, i, len);
        self.opt_cond(t, present, |fg| {
            let off = fg.b.ins().imul_imm_u(i, esz as i64);
            let addr = fg.b.ins().iadd(ptr, off);
            Ok(if op == Builtin::GetPtr { V::S(addr) } else { fg.read(inner, addr) })
        })
    }

    /// The atomic and volatile accesses (§19.5, §19.6). Cranelift's atomics are
    /// sequentially consistent, which is at least as strong as any order asked for.
    fn atomic(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr], vals: &[V]) -> CResult<V> {
        let span = e.span;
        let TyKind::Ptr { pointee, .. } = args[0].ty.kind() else { return unsupported(span, "this atomic") };
        let elem = *pointee;
        let r = self.g.rep(elem);
        let p = self.as_scalar(args[0].ty, vals[0]);
        let v = |fg: &mut Self, i: usize| fg.as_scalar(args[i].ty, vals[i]);
        if matches!(op, Builtin::VolatileLoad | Builtin::VolatileStore)
            && let Rep::Mem { size: n @ (1 | 2 | 4 | 8), .. } = r
        {
            // A small aggregate (a `#bits` struct, say) moves as one integer of its size.
            let size = self.b.ins().iconst(types::I32, n as i64);
            let it = super::int_type(n);
            if op == Builtin::VolatileLoad {
                let x = self.rt_call("gm_clif_vload", "pi>l", &[p, size])[0];
                let x = self.resize(x, types::I64, it, false);
                let a = self.temp(elem);
                self.b.ins().store(mf(), x, a, 0);
                return Ok(V::M(a));
            }
            let src = self.spill(elem, vals[1]);
            let x = self.b.ins().load(it, mf(), src, 0);
            let x = self.resize(x, it, types::I64, false);
            self.rt_call("gm_clif_vstore", "pil", &[p, size, x]);
            return Ok(V::Z);
        }
        if matches!(op, Builtin::VolatileLoad | Builtin::VolatileStore) {
            let Some(ty) = r.ty() else { return unsupported(span, format!("a volatile access to `{}`", elem)) };
            let size = self.b.ins().iconst(types::I32, (ty.bits() / 8) as i64);
            if ty == types::I128 {
                return unsupported(span, format!("a volatile access to `{}`", elem));
            }
            if op == Builtin::VolatileLoad {
                let x = self.rt_call("gm_clif_vload", "pi>l", &[p, size])[0];
                let x = if ty.is_float() {
                    let it = if ty == types::F32 { types::I32 } else { types::I64 };
                    let x = self.resize(x, types::I64, it, false);
                    self.b.ins().bitcast(ty, mf(), x)
                } else {
                    self.resize(x, types::I64, ty, false)
                };
                return Ok(V::S(self.normalize(r, x)));
            }
            let x = v(self, 1);
            let x = if ty.is_float() {
                let it = if ty == types::F32 { types::I32 } else { types::I64 };
                let b = self.b.ins().bitcast(it, mf(), x);
                self.resize(b, it, types::I64, false)
            } else {
                self.resize(x, ty, types::I64, false)
            };
            self.rt_call("gm_clif_vstore", "pil", &[p, size, x]);
            return Ok(V::Z);
        }
        let ty = match r.ty() {
            Some(ty) if ty.is_int() && ty != types::I128 => ty,
            _ => return unsupported(span, format!("atomic operations on `{}`", elem)),
        };
        let fl = mf();
        Ok(match op {
            Builtin::AtomicLoad => V::S(self.b.ins().atomic_load(ty, fl, p)),
            Builtin::AtomicStore => {
                let x = v(self, 1);
                self.b.ins().atomic_store(fl, x, p);
                V::Z
            }
            Builtin::AtomicSwap => {
                let x = v(self, 1);
                V::S(self.b.ins().atomic_rmw(ty, fl, AtomicRmwOp::Xchg, p, x))
            }
            Builtin::AtomicCas => {
                let (exp, new) = (v(self, 1), v(self, 2));
                let old = self.b.ins().atomic_cas(fl, p, exp, new);
                let failed = self.b.ins().icmp(IntCC::NotEqual, old, exp);
                self.opt_cond(e.ty, failed, |_| Ok(V::S(old)))?
            }
            Builtin::AtomicRmw(rmw) => {
                let signed = matches!(r, Rep::Int { signed: true, .. });
                let k = match rmw {
                    RmwOp::Add => AtomicRmwOp::Add,
                    RmwOp::Sub => AtomicRmwOp::Sub,
                    RmwOp::And => AtomicRmwOp::And,
                    RmwOp::Or => AtomicRmwOp::Or,
                    RmwOp::Xor => AtomicRmwOp::Xor,
                    RmwOp::Max if signed => AtomicRmwOp::Smax,
                    RmwOp::Max => AtomicRmwOp::Umax,
                    RmwOp::Min if signed => AtomicRmwOp::Smin,
                    RmwOp::Min => AtomicRmwOp::Umin,
                };
                let x = v(self, 1);
                let old = self.b.ins().atomic_rmw(ty, fl, k, p, x);
                V::S(self.normalize(r, old))
            }
            _ => unreachable!(),
        })
    }

    // --- vectors (§5.6.2): lane by lane ---------------------------------------------------------

    pub fn vector_unary(&mut self, e: &'a Expr, op: UnOp, x: &'a Expr) -> CResult<V> {
        let t = e.ty;
        let (n, lane, bytes) = parts(t);
        let v = self.expr(x)?;
        let pa = self.spill(t, v);
        let res = self.temp(t);
        let lr = self.g.rep(lane);
        for i in 0..n {
            let y = self.load_s(lane, pa, i * bytes);
            let r = match (op, lr) {
                (UnOp::Neg, Rep::Int { ty, .. }) if self.checks() => {
                    let z = self.iconst(ty, 0);
                    let r = self.int_binary(e.span, BinOp::Sub(Flavor::Checked), lane, lane, z, y, lane)?;
                    self.as_scalar(lane, r)
                }
                (UnOp::Neg, Rep::Int { .. }) => {
                    let r = self.b.ins().ineg(y);
                    self.normalize(lr, r)
                }
                (UnOp::Neg, Rep::F16) => {
                    let m = self.iconst_bits(types::I16, 0x8000);
                    self.b.ins().bxor(y, m)
                }
                (UnOp::Neg, _) => self.b.ins().fneg(y),
                (_, Rep::Bool) => self.b.ins().bxor_imm_s(y, 1),
                _ => {
                    let r = self.b.ins().bnot(y);
                    self.normalize(lr, r)
                }
            };
            self.b.ins().store(mf(), r, res, (i * bytes) as i32);
        }
        Ok(V::M(res))
    }

    pub fn vector_binary(&mut self, e: &'a Expr, op: BinOp, l: &'a Expr, r: &'a Expr) -> CResult<V> {
        let t = e.ty;
        let (n, lane, bytes) = parts(t);
        let a = self.expr(l)?;
        let b = self.expr(r)?;
        let lr = self.g.rep(lane);
        let checks = self.checks();
        let pa = self.spill(t, a);
        let shift = matches!(op, BinOp::Shl(_) | BinOp::Shr);
        // A shift moves every lane by the one scalar amount.
        let (pb, amount) = if shift { (None, Some(self.as_scalar(r.ty, b))) } else { (Some(self.spill(t, b)), None) };
        let res = self.temp(t);
        for i in 0..n {
            let off = i * bytes;
            let x = self.load_s(lane, pa, off);
            let y = match pb {
                Some(pb) => self.load_s(lane, pb, off),
                None => amount.unwrap(),
            };
            let z = match lr {
                Rep::F16 | Rep::F32 | Rep::F64 => self.float_op(e.span, op, lane, x, y)?,
                Rep::Bool => self.bitwise(op, x, y),
                Rep::Int { bits, signed, .. } => {
                    let checked = match op {
                        BinOp::Add(f) | BinOp::Sub(f) | BinOp::Mul(f) => {
                            f == Flavor::Sat || (f == Flavor::Checked && checks)
                        }
                        BinOp::Shl(_) | BinOp::Shr => checks,
                        BinOp::Div | BinOp::Rem => true,
                        _ => false,
                    };
                    if checked {
                        let rt = if shift { r.ty } else { lane };
                        let v = self.int_binary(e.span, op, lane, lane, x, y, rt)?;
                        self.as_scalar(lane, v)
                    } else {
                        match op {
                            BinOp::Add(_) => self.b.ins().iadd(x, y),
                            BinOp::Sub(_) => self.b.ins().isub(x, y),
                            BinOp::Mul(_) => self.b.ins().imul(x, y),
                            BinOp::Shl(_) | BinOp::Shr => {
                                // Without checks an amount of the lane width or more shifts by
                                // the amount modulo the width.
                                let k = self.amount64(r.ty, y);
                                let k = self.b.ins().band_imm_u(k, (bits - 1) as i64);
                                match (op, signed) {
                                    (BinOp::Shr, true) => self.b.ins().sshr(x, k),
                                    (BinOp::Shr, false) => self.b.ins().ushr(x, k),
                                    _ => self.b.ins().ishl(x, k),
                                }
                            }
                            _ => self.bitwise(op, x, y),
                        }
                    }
                }
                _ => return unsupported(e.span, format!("operator `{}` on `{}`", op_symbol(op), t)),
            };
            self.b.ins().store(mf(), z, res, off as i32);
        }
        Ok(V::M(res))
    }

    /// The vector builtins (§21.3), or `None` if `op` is not one.
    fn vector_builtin(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> Option<CResult<V>> {
        let r = match op {
            Builtin::LaneEq
            | Builtin::LaneNe
            | Builtin::LaneLt
            | Builtin::LaneLe
            | Builtin::LaneGt
            | Builtin::LaneGe => self.vec_compare(e, op, args),
            Builtin::ReduceSum | Builtin::ReduceMin | Builtin::ReduceMax => self.vec_reduce(e, op, &args[0]),
            Builtin::MaskAll | Builtin::MaskAny | Builtin::MaskCount => self.vec_mask_test(op, &args[0]),
            Builtin::Select => self.vec_select(e, args),
            Builtin::LaneMin | Builtin::LaneMax | Builtin::LaneAbs => self.vec_minmax_abs(e, op, args),
            Builtin::LaneConvert => self.vec_convert(e, &args[0]),
            Builtin::Shuffle => self.vec_shuffle(e, args),
            Builtin::VecLoad | Builtin::VecStore => self.vec_load_store(e, op, args),
            _ => return None,
        };
        Some(r)
    }

    fn vec_compare(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<V> {
        let t = args[0].ty;
        let (n, lane, bytes) = parts(t);
        let a = self.expr(&args[0])?;
        let b = self.expr(&args[1])?;
        let (pa, pb) = (self.spill(t, a), self.spill(t, b));
        let res = self.temp(e.ty);
        for i in 0..n {
            let x = self.load_s(lane, pa, i * bytes);
            let y = self.load_s(lane, pb, i * bytes);
            let c = match op {
                Builtin::LaneEq | Builtin::LaneNe => {
                    let eq = self.equal(lane, V::S(x), V::S(y))?;
                    if op == Builtin::LaneNe { self.b.ins().bxor_imm_s(eq, 1) } else { eq }
                }
                _ => {
                    let bop = match op {
                        Builtin::LaneLt => BinOp::Lt,
                        Builtin::LaneLe => BinOp::Le,
                        Builtin::LaneGt => BinOp::Gt,
                        _ => BinOp::Ge,
                    };
                    self.compare(bop, lane, x, y)
                }
            };
            self.b.ins().store(mf(), c, res, i as i32);
        }
        Ok(V::M(res))
    }

    fn vec_reduce(&mut self, e: &'a Expr, op: Builtin, x: &'a Expr) -> CResult<V> {
        let t = x.ty;
        let (n, lane, bytes) = parts(t);
        let v = self.expr(x)?;
        let p = self.spill(t, v);
        let mut acc = self.load_s(lane, p, 0);
        let float = lane.is_float();
        for i in 1..n {
            let y = self.load_s(lane, p, i * bytes);
            acc = match op {
                Builtin::ReduceSum if float => self.float_op(e.span, BinOp::Add(Flavor::Checked), lane, acc, y)?,
                Builtin::ReduceSum => {
                    let r = self.int_binary(e.span, BinOp::Add(Flavor::Checked), lane, lane, acc, y, lane)?;
                    self.as_scalar(lane, r)
                }
                _ => self.min_max(op == Builtin::ReduceMin, lane, acc, y),
            };
        }
        Ok(V::S(acc))
    }

    fn vec_mask_test(&mut self, op: Builtin, x: &'a Expr) -> CResult<V> {
        let (n, _, _) = parts(x.ty);
        let v = self.expr(x)?;
        let p = self.spill(x.ty, v);
        let mut count = self.b.ins().iconst(types::I64, 0);
        for i in 0..n {
            let b = self.b.ins().uload8(types::I64, mf(), p, i as i32);
            count = self.b.ins().iadd(count, b);
        }
        Ok(V::S(match op {
            Builtin::MaskAll => self.b.ins().icmp_imm_s(IntCC::Equal, count, n as i64),
            Builtin::MaskAny => self.b.ins().icmp_imm_s(IntCC::NotEqual, count, 0),
            _ => count,
        }))
    }

    fn vec_select(&mut self, e: &'a Expr, args: &'a [Expr]) -> CResult<V> {
        let t = e.ty;
        let (n, lane, bytes) = parts(t);
        let m = self.expr(&args[0])?;
        let a = self.expr(&args[1])?;
        let b = self.expr(&args[2])?;
        let pm = self.spill(args[0].ty, m);
        let (pa, pb) = (self.spill(t, a), self.spill(t, b));
        let res = self.temp(t);
        for i in 0..n {
            let c = self.b.ins().load(types::I8, mf(), pm, i as i32);
            let x = self.load_s(lane, pa, i * bytes);
            let y = self.load_s(lane, pb, i * bytes);
            let z = self.pick(c, x, y);
            self.b.ins().store(mf(), z, res, (i * bytes) as i32);
        }
        Ok(V::M(res))
    }

    fn vec_minmax_abs(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<V> {
        let t = e.ty;
        let (n, lane, bytes) = parts(t);
        let a = self.expr(&args[0])?;
        let pa = self.spill(t, a);
        let pb = match args.get(1) {
            Some(x) => {
                let b = self.expr(x)?;
                Some(self.spill(t, b))
            }
            None => None,
        };
        let res = self.temp(t);
        let lr = self.g.rep(lane);
        for i in 0..n {
            let off = i * bytes;
            let x = self.load_s(lane, pa, off);
            let z = match pb {
                Some(pb) => {
                    let y = self.load_s(lane, pb, off);
                    self.min_max(op == Builtin::LaneMin, lane, x, y)
                }
                // A float lane's sign bit is cleared, NaN included.
                None if lr == Rep::F16 => self.b.ins().band_imm_u(x, 0x7fff),
                None if lane.is_float() => self.b.ins().fabs(x),
                None => self.abs(e.span, lane, x, self.checks())?,
            };
            self.b.ins().store(mf(), z, res, off as i32);
        }
        Ok(V::M(res))
    }

    fn vec_convert(&mut self, e: &'a Expr, x: &'a Expr) -> CResult<V> {
        let (n, to, tb) = parts(e.ty);
        let (_, from, fb) = parts(x.ty);
        let v = self.expr(x)?;
        let pa = self.spill(x.ty, v);
        let res = self.temp(e.ty);
        let (fr, tr) = (self.g.rep(from), self.g.rep(to));
        for i in 0..n {
            let y = self.load_s(from, pa, i * fb);
            let z = match (fr, tr) {
                (Rep::Int { ty, signed, .. }, Rep::F16 | Rep::F32 | Rep::F64) => self.int_to_float(y, ty, signed, to),
                (Rep::F16 | Rep::F32 | Rep::F64, Rep::Int { ty, signed, .. }) => {
                    let d = self.float_to_f64(from, y);
                    let k = if signed {
                        self.b.ins().fcvt_to_sint_sat(types::I64, d)
                    } else {
                        self.b.ins().fcvt_to_uint_sat(types::I64, d)
                    };
                    self.resize(k, types::I64, ty, false)
                }
                (Rep::F16 | Rep::F32 | Rep::F64, _) => self.float_convert(from, to, y),
                (Rep::Bool, Rep::Int { ty, .. }) => self.resize(y, types::I8, ty, false),
                _ => self.int_convert(from, to, y),
            };
            self.b.ins().store(mf(), z, res, (i * tb) as i32);
        }
        Ok(V::M(res))
    }

    fn vec_shuffle(&mut self, e: &'a Expr, args: &'a [Expr]) -> CResult<V> {
        let t = args[0].ty;
        let (_, _, bytes) = parts(t);
        let v = self.expr(&args[0])?;
        let pa = self.spill(t, v);
        let res = self.temp(e.ty);
        for (j, a) in args[1..].iter().enumerate() {
            let k = match &a.kind {
                ExprKind::Int(i) => i.to_u64().unwrap_or(0),
                _ => 0,
            };
            let src = self.add(pa, k * bytes);
            let dst = self.add(res, j as u64 * bytes);
            self.copy(dst, src, bytes);
        }
        Ok(V::M(res))
    }

    fn vec_load_store(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<V> {
        let (vt, sl) = if op == Builtin::VecLoad { (e.ty, &args[0]) } else { (args[0].ty, &args[1]) };
        let (n, _, bytes) = parts(vt);
        let v = if op == Builtin::VecStore { Some(self.expr(&args[0])?) } else { None };
        let s = self.expr_m(sl)?;
        let (p, len) = self.slice_parts(s);
        if self.checks() {
            let short = self.b.ins().icmp_imm_u(IntCC::UnsignedLessThan, len, n as i64);
            let span = e.span;
            self.cold_if(short, |fg| {
                let st = fg.b.ins().iconst(types::I32, (op == Builtin::VecStore) as i64);
                let nv = fg.b.ins().iconst(types::I64, n as i64);
                let [f, l, c] = fg.loc(span)?;
                fg.rt_call("gm_clif_trap_vec", "illpii", &[st, nv, len, f, l, c]);
                Ok(())
            })?;
        }
        let size = n * bytes;
        match v {
            None => {
                let res = self.temp(vt);
                self.copy(res, p, size);
                Ok(V::M(res))
            }
            Some(v) => {
                let pv = self.spill(vt, v);
                self.copy(p, pv, size);
                Ok(V::Z)
            }
        }
    }
}

/// The runtime function that implements system service `op`, `gm_<name>`, interned so that
/// each name is allocated once however often the service is used.
fn sys_symbol(op: crate::tir::SysOp) -> &'static str {
    static NAMES: std::sync::Mutex<Vec<(crate::tir::SysOp, &'static str)>> = std::sync::Mutex::new(Vec::new());
    let mut names = NAMES.lock().unwrap_or_else(|e| e.into_inner());
    if let Some((_, n)) = names.iter().find(|(o, _)| *o == op) {
        return n;
    }
    let name: &'static str = Box::leak(format!("gm_{}", op.signature().0).into_boxed_str());
    names.push((op, name));
    name
}
