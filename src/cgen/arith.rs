//! Operators and builtins on scalars, with the checks of §7.4 and §18.
//!
//! An integer of `bits` bits is held in a C integer of its storage size. Values
//! are kept normalized (zero- or sign-extended from `bits`), so comparisons and
//! bitwise operations work directly; arithmetic detects overflow of the Gamma
//! type, which may be narrower than its C type.

use super::func::FnGen;
use super::types::{Repr, int_cty};
use super::{CResult, unsupported};
use crate::tir::{AdtKind, BinOp, Builtin, Expr, Flavor, SysOp, UnOp};
use crate::types::{FloatTy, Ty, TyKind};
use num_bigint::BigInt;

/// Facts about an integer type.
#[derive(Clone, Copy)]
pub struct IntInfo {
    pub signed: bool,
    pub bits: u32,
    pub storage: u64,
}

impl IntInfo {
    fn exact(&self) -> bool {
        self.bits as u64 == self.storage * 8
    }
    fn cty(&self) -> &'static str {
        int_cty(self.signed, self.storage)
    }
    fn ucty(&self) -> &'static str {
        int_cty(false, self.storage)
    }
    pub fn min(&self) -> BigInt {
        if self.signed { -(BigInt::from(1) << (self.bits as usize - 1)) } else { BigInt::from(0) }
    }
    pub fn max(&self) -> BigInt {
        if self.signed {
            (BigInt::from(1) << (self.bits as usize - 1)) - 1
        } else {
            (BigInt::from(1) << self.bits as usize) - 1
        }
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

impl<'g, 'a> FnGen<'g, 'a> {
    pub fn int_info(&self, t: Ty) -> Option<IntInfo> {
        match self.g.ty.repr(t) {
            Repr::Int { signed, bits, storage } => Some(IntInfo { signed, bits, storage }),
            _ => None,
        }
    }

    /// `v` reduced to `bits` bits (two's complement wrap), as a C expression.
    fn normalize(&self, ii: IntInfo, v: &str) -> String {
        if ii.exact() {
            return v.to_string();
        }
        let shift = ii.storage * 8 - ii.bits as u64;
        if ii.signed {
            format!("(({c})((({u}){v}) << {s}) >> {s})", c = ii.cty(), u = ii.ucty(), v = v, s = shift)
        } else {
            format!("(({c})(({v}) & ((({c})1 << {b}) - 1)))", c = ii.cty(), v = v, b = ii.bits)
        }
    }

    fn checks(&self) -> bool {
        self.g.opts.checks
    }

    pub fn unary(&mut self, e: &'a Expr, op: UnOp, x: &'a Expr) -> CResult<String> {
        let v = self.expr(x)?;
        let t = e.ty;
        match op {
            UnOp::Not => Ok(self.tmp(t, &format!("(uint8_t)!{}", v))),
            UnOp::Neg => match self.g.ty.repr(x.ty) {
                Repr::Float(_) => Ok(self.tmp(t, &format!("-{}", v))),
                Repr::Int { .. } => {
                    let ii = self.int_info(x.ty).unwrap();
                    let min = self.int_lit(x.ty, &ii.min());
                    if self.checks() {
                        let loc = self.loc(e);
                        self.line(&format!("if ({} == {}) gm_trap(\"integer overflow in `-`\", {});", v, min, loc));
                    }
                    let r = format!("({})(({})0 - ({}){})", ii.cty(), ii.ucty(), ii.ucty(), v);
                    let r = self.normalize(ii, &r);
                    Ok(self.tmp(t, &r))
                }
                _ => unsupported(e.span, format!("negating `{}`", x.ty)),
            },
            UnOp::BitNot => match self.int_info(x.ty) {
                Some(ii) => {
                    let r = self.normalize(ii, &format!("({})~{}", ii.cty(), v));
                    Ok(self.tmp(t, &r))
                }
                None => unsupported(e.span, format!("`~` on `{}`", x.ty)),
            },
        }
    }

    pub fn binary(&mut self, e: &'a Expr, op: BinOp, l: &'a Expr, r: &'a Expr) -> CResult<String> {
        let a = self.expr(l)?;
        let b = self.expr(r)?;
        let t = e.ty;
        if matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge) {
            return match self.g.ty.repr(l.ty) {
                Repr::Int { .. } | Repr::Float(_) | Repr::Bool | Repr::Ptr => {
                    Ok(self.tmp(t, &format!("({} {} {})", a, op_symbol(op), b)))
                }
                _ => {
                    let lt = self.g.helpers.less_fn(&mut self.g.ty, l.ty);
                    let eq = self.g.helpers.eq_fn(&mut self.g.ty, l.ty);
                    let (pa, pb) = (self.addr_of(l.ty, &a), self.addr_of(l.ty, &b));
                    let c = match op {
                        BinOp::Lt => format!("{lt}({pa}, {pb})"),
                        BinOp::Gt => format!("{lt}({pb}, {pa})"),
                        BinOp::Le => format!("({lt}({pa}, {pb}) || {eq}({pa}, {pb}))"),
                        _ => format!("({lt}({pb}, {pa}) || {eq}({pb}, {pa}))"),
                    };
                    Ok(self.tmp(t, &format!("(uint8_t){c}")))
                }
            };
        }
        match self.g.ty.repr(l.ty) {
            Repr::Float(_) => {
                let sym = match op {
                    BinOp::Add(_) => "+",
                    BinOp::Sub(_) => "-",
                    BinOp::Mul(_) => "*",
                    BinOp::Div => "/",
                    _ => return unsupported(e.span, format!("operator `{}` on floats", op_symbol(op))),
                };
                Ok(self.tmp(t, &format!("({} {} {})", a, sym, b)))
            }
            Repr::Int { .. } => self.int_binary(e, op, l.ty, r.ty, &a, &b),
            _ => unsupported(e.span, format!("operator `{}` on `{}`", op_symbol(op), l.ty)),
        }
    }

    fn int_binary(&mut self, e: &Expr, op: BinOp, lt: Ty, rt: Ty, a: &str, b: &str) -> CResult<String> {
        let ii = self.int_info(lt).unwrap();
        let c = ii.cty();
        let loc = self.loc(e);
        let t = e.ty;
        match op {
            BinOp::Add(fl) | BinOp::Sub(fl) | BinOp::Mul(fl) => {
                let name = match op {
                    BinOp::Add(_) => "add",
                    BinOp::Sub(_) => "sub",
                    _ => "mul",
                };
                let r = format!("r{}", self.fresh());
                let o = format!("o{}", self.fresh());
                self.line(&format!("{c} {r}; bool {o} = __builtin_{name}_overflow({a}, {b}, &{r});"));
                if !ii.exact() {
                    let (min, max) = (self.int_lit(lt, &ii.min()), self.int_lit(lt, &ii.max()));
                    self.line(&format!("{o} = {o} || {r} < {min} || {r} > {max};"));
                }
                let sat = match op {
                    BinOp::Add(_) if ii.signed => format!("({b} > 0)"),
                    BinOp::Sub(_) if ii.signed => format!("({b} < 0)"),
                    BinOp::Mul(_) if ii.signed => format!("(({a} < 0) == ({b} < 0))"),
                    BinOp::Sub(_) => "0".into(),
                    _ => "1".into(),
                };
                self.finish_int(fl, ii, lt, t, &r, &o, &sat, &loc, op_symbol(op))
            }
            BinOp::Shl(_) | BinOp::Shr => {
                let fl = if let BinOp::Shl(f) = op { f } else { Flavor::Checked };
                let n = format!("n{}", self.fresh());
                self.line(&format!("unsigned __int128 {n} = (unsigned __int128){b};"));
                if self.checks() {
                    let rii = self.int_info(rt).unwrap();
                    self.line(&format!(
                        "if ({n} >= {bits}) {{ char m[128]; snprintf(m, sizeof m, \"shift amount %llu too large for {ty}\", (unsigned long long){n}); gm_trap(m, {loc}); }}",
                        bits = ii.bits,
                        ty = lt,
                        loc = loc
                    ));
                    let _ = rii;
                } else {
                    self.line(&format!("{n} %= {};", ii.bits));
                }
                if let BinOp::Shr = op {
                    return Ok(self.tmp(t, &format!("({c})({a} >> (int){n})")));
                }
                let r = format!("r{}", self.fresh());
                let raw = format!("({c})(({u}){a} << (int){n})", u = ii.ucty());
                let norm = self.normalize(ii, &raw);
                self.line(&format!("{c} {r} = {norm};"));
                let o = format!("o{}", self.fresh());
                self.line(&format!("bool {o} = ({c})({r} >> (int){n}) != {a};"));
                let sat = format!("({a} > 0)");
                self.finish_int(fl, ii, lt, t, &r, &o, &sat, &loc, "<<")
            }
            BinOp::Div | BinOp::Rem => {
                let sym = op_symbol(op);
                self.line(&format!("if ({b} == 0) gm_trap(\"division by zero\", {loc});"));
                if ii.signed {
                    let min = self.int_lit(lt, &ii.min());
                    self.line(&format!(
                        "if ({a} == {min} && {b} == ({c})-1) gm_trap(\"integer overflow in `{sym}`\", {loc});"
                    ));
                }
                let r = if let BinOp::Div = op { format!("({a} / {b})") } else { format!("({a} % {b})") };
                Ok(self.tmp(t, &format!("({c}){r}")))
            }
            BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor => {
                let s = match op {
                    BinOp::BitAnd => "&",
                    BinOp::BitOr => "|",
                    _ => "^",
                };
                Ok(self.tmp(t, &format!("({c})({a} {s} {b})")))
            }
            _ => unreachable!(),
        }
    }

    /// Completes an integer operation whose wrapped result is `r` and whose overflow
    /// flag is `o`, according to the flavor (§7.4.2). `sat_up` says which way a
    /// saturating result goes.
    #[allow(clippy::too_many_arguments)]
    fn finish_int(
        &mut self,
        fl: Flavor,
        ii: IntInfo,
        lt: Ty,
        t: Ty,
        r: &str,
        o: &str,
        sat_up: &str,
        loc: &str,
        sym: &str,
    ) -> CResult<String> {
        let wrapped = self.normalize(ii, r);
        match fl {
            Flavor::Checked => {
                if self.checks() {
                    self.line(&format!("if ({o}) gm_trap(\"integer overflow in `{sym}`\", {loc});"));
                }
                Ok(self.tmp(t, &wrapped))
            }
            Flavor::Wrap => Ok(self.tmp(t, &wrapped)),
            Flavor::Sat => {
                let (min, max) = (self.int_lit(lt, &ii.min()), self.int_lit(lt, &ii.max()));
                Ok(self.tmp(t, &format!("{o} ? ({sat_up} ? {max} : {min}) : {wrapped}")))
            }
            Flavor::Opt => {
                let res = self.tmp_uninit(t);
                self.line(&format!("if ({o}) {{"));
                let none = self.make_none(t);
                self.line(&format!("    {res} = {none};"));
                self.line("} else {");
                let w = self.tmp(lt, &wrapped);
                let some = self.make_some(t, &w);
                self.line(&format!("    {res} = {some};"));
                self.line("}");
                Ok(res)
            }
        }
    }

    // --- builtins -----------------------------------------------------------------------------------

    pub fn builtin(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<String> {
        let t = e.ty;
        let loc = self.loc(e);
        match op {
            Builtin::Assert | Builtin::Expect => {
                let c = self.expr(&args[0])?;
                if op == Builtin::Assert && !self.checks() {
                    return Ok(self.unit());
                }
                self.line(&format!("if (!{}) {{", c));
                self.indent += 1;
                let m = self.expr(&args[1])?;
                let a = self.addr_of(args[1].ty, &m);
                let (p, l) = (self.slice_ptr(&a), self.slice_len(&a));
                let prefix = if op == Builtin::Assert { "assertion failed" } else { "expectation failed" };
                self.line(&format!(
                    "gm_buf mb = {{0}}; size_t ml = {l}; const uint8_t *mp = {p}; if (ml == {n} && memcmp(mp, \"{prefix}\", {n}) == 0) gm_buf_push(&mb, mp, ml); else {{ gm_buf_push(&mb, \"{prefix}: \", {n2}); gm_buf_push(&mb, mp, ml); }} gm_trap_bytes(mb.data, mb.len, {loc});",
                    n = prefix.len(),
                    n2 = prefix.len() + 2
                ));
                self.indent -= 1;
                self.line("}");
                return Ok(self.unit());
            }
            Builtin::Fiber => return self.fiber_builtin(e, args),
            Builtin::Sys(op) => {
                let mut vals = vec![];
                for a in args {
                    let v = self.expr(a)?;
                    vals.push((self.addr_of(a.ty, &v), v));
                }
                let slice = |fg: &mut Self, i: usize| {
                    let a: &String = &vals[i].0;
                    format!("(uint8_t *){}, {}", fg.slice_ptr(a), fg.slice_len(a))
                };
                let call = match op {
                    SysOp::Read => format!("gm_sys_read({})", slice(self, 0)),
                    SysOp::ClockNow => "gm_clock_now()".into(),
                    SysOp::ClockWall => "gm_clock_wall()".into(),
                    SysOp::Sleep => format!("gm_clock_sleep({})", vals[0].1),
                    SysOp::RandFill => format!("gm_rand_fill({})", slice(self, 0)),
                };
                if t.is_unit() {
                    self.line(&format!("{};", call));
                    return Ok(self.unit());
                }
                return Ok(self.tmp(t, &call));
            }
            Builtin::Unreachable => {
                self.line(&format!("gm_trap(\"reached `@unreachable()`\", {});", loc));
                return Ok(self.zero(t));
            }
            Builtin::Get | Builtin::GetPtr | Builtin::First | Builtin::Last => return self.get(e, op, args),
            Builtin::HeapAlloc => {
                let s = self.expr(&args[0])?;
                let a = self.expr(&args[1])?;
                self.line(&format!(
                    "if (({a}) == 0 || (({a}) & (({a}) - 1)) != 0) gm_trap(\"allocation alignment is not a power of two\", {loc});"
                ));
                let p = self.tmp(Ty::intern(TyKind::Rawptr), &format!("gm_heap_alloc({}, {})", s, a));
                return Ok(self.tmp(t, &p));
            }
            Builtin::HeapFree => {
                let p = self.expr(&args[0])?;
                let s = self.expr(&args[1])?;
                let a = self.expr(&args[2])?;
                self.line(&format!("gm_heap_free({}, {}, {}, {});", p, s, a, loc));
                return Ok(self.unit());
            }
            _ => {}
        }
        let mut vals = vec![];
        for a in args {
            vals.push(self.expr(a)?);
        }
        let at = args[0].ty;
        match op {
            Builtin::Convert
            | Builtin::Trunc
            | Builtin::Round
            | Builtin::Floor
            | Builtin::Ceil
            | Builtin::Checked
            | Builtin::Sat => self.convert(e, op, at, &vals[0]),
            Builtin::Bits => {
                let a = self.addr_of(at, &vals[0]);
                let r = self.tmp_uninit(t);
                self.line(&format!(
                    "memset(&{r}, 0, sizeof {r}); memcpy(&{r}, {a}, sizeof {r} < {sz} ? sizeof {r} : {sz});",
                    sz = self.g.ty.size(at)
                ));
                Ok(r)
            }
            Builtin::Cast => {
                let al = pointee_align(self, t);
                if al > 1 && self.checks() {
                    self.line(&format!(
                        "if ((uintptr_t){v} % {al} != 0) {{ char m[128]; snprintf(m, sizeof m, \"misaligned pointer cast: address 0x%llx is not aligned to {al} bytes\", (unsigned long long)(uintptr_t){v}); gm_trap(m, {loc}); }}",
                        v = vals[0]
                    ));
                }
                Ok(self.tmp(t, &vals[0]))
            }
            Builtin::Addr => Ok(self.tmp(t, &format!("(uintptr_t){}", vals[0]))),
            Builtin::ToPtr => Ok(self.tmp(t, &format!("(void *)(uintptr_t){}", vals[0]))),
            Builtin::Tag => {
                let a = self.addr_of(at, &vals[0]);
                Ok(self.load(t, &a))
            }
            Builtin::FromTag => {
                let TyKind::Optional(u) = t.kind() else { return unsupported(e.span, "from_tag") };
                let u = *u;
                let def = self.adt_def(u);
                let AdtKind::Union { variants, tag } = &def.kind else { return unsupported(e.span, "from_tag") };
                let (variants, tag) = (variants.clone(), *tag);
                let res = self.make_none(t);
                let res = self.tmp(t, &res);
                for (i, v) in variants.iter().enumerate() {
                    if v.payload.is_some() {
                        continue;
                    }
                    let lit = self.int_lit(at, &v.tag);
                    self.line(&format!("if ({} == {}) {{", vals[0], lit));
                    let x = self.make_variant(u, i, None);
                    let s = self.make_some(t, &x);
                    self.line(&format!("    {} = {};", res, s));
                    self.line("}");
                }
                let _ = tag;
                Ok(res)
            }
            Builtin::Min | Builtin::Max => {
                let gt = if op == Builtin::Min { "<" } else { ">" };
                match self.g.ty.repr(at) {
                    Repr::Float(_) => {
                        let f = if op == Builtin::Min { "gm_fmin" } else { "gm_fmax" };
                        let c = self.g.ty.cty(t);
                        Ok(self.tmp(t, &format!("({})({}((double){}, (double){}))", c, f, vals[0], vals[1])))
                    }
                    _ => Ok(self.tmp(t, &format!("({a} {gt} {b} ? {a} : {b})", a = vals[0], b = vals[1]))),
                }
            }
            Builtin::Clamp => {
                let (x, lo, hi) = (&vals[0], &vals[1], &vals[2]);
                match self.g.ty.repr(at) {
                    Repr::Float(_) => {
                        let c = self.g.ty.cty(t);
                        Ok(self.tmp(t, &format!("({c})gm_fmin(gm_fmax((double){x}, (double){lo}), (double){hi})")))
                    }
                    _ => Ok(self.tmp(t, &format!("({x} < {lo} ? {lo} : ({x} > {hi} ? {hi} : {x}))"))),
                }
            }
            Builtin::Abs => match self.g.ty.repr(at) {
                Repr::Float(_) => Ok(self
                    .tmp(t, &format!("({v} < 0 || (({v}) == 0 && __builtin_signbit({v}))) ? -{v} : {v}", v = vals[0]))),
                _ => {
                    let ii = self.int_info(at).unwrap();
                    if ii.signed && self.checks() {
                        let min = self.int_lit(at, &ii.min());
                        self.line(&format!(
                            "if ({} == {}) gm_trap(\"integer overflow in `abs`\", {});",
                            vals[0], min, loc
                        ));
                    }
                    Ok(self.tmp(t, &format!("({v} < 0 ? -{v} : {v})", v = vals[0])))
                }
            },
            Builtin::AbsDiff => {
                let ii = self.int_info(at).unwrap();
                let u = ii.ucty();
                Ok(self.tmp(
                    t,
                    &format!("({a} > {b} ? ({u}){a} - ({u}){b} : ({u}){b} - ({u}){a})", a = vals[0], b = vals[1]),
                ))
            }
            Builtin::IsNan => Ok(self.tmp(t, &format!("(uint8_t)__builtin_isnan({})", vals[0]))),
            Builtin::IsInf => Ok(self.tmp(t, &format!("(uint8_t)__builtin_isinf({})", vals[0]))),
            Builtin::IsFinite => Ok(self.tmp(t, &format!("(uint8_t)__builtin_isfinite({})", vals[0]))),
            Builtin::SignBit => Ok(self.tmp(t, &format!("(uint8_t)(__builtin_signbit({}) != 0)", vals[0]))),
            Builtin::Sqrt => {
                let c = self.g.ty.cty(t);
                let f =
                    if matches!(at.kind(), TyKind::Float(FloatTy::F32)) { "__builtin_sqrtf" } else { "__builtin_sqrt" };
                Ok(self.tmp(t, &format!("({c}){f}({})", vals[0])))
            }
            Builtin::Fma => {
                let c = self.g.ty.cty(t);
                let f =
                    if matches!(at.kind(), TyKind::Float(FloatTy::F32)) { "__builtin_fmaf" } else { "__builtin_fma" };
                Ok(self.tmp(t, &format!("({c}){f}({}, {}, {})", vals[0], vals[1], vals[2])))
            }
            Builtin::Clz | Builtin::Ctz | Builtin::Popcount | Builtin::Bswap | Builtin::Bitreverse => {
                let ii = self.int_info(at).unwrap();
                let x = format!("(unsigned __int128)({})({})", ii.ucty(), vals[0]);
                let x = format!(
                    "({x} & (((unsigned __int128)1 << {b}) - 1 | ({b} == 128 ? ~(unsigned __int128)0 : 0)))",
                    b = ii.bits
                );
                let c = self.g.ty.cty(t);
                let call = match op {
                    Builtin::Clz => format!("gm_clz({x}, {})", ii.bits),
                    Builtin::Ctz => format!("gm_ctz({x}, {})", ii.bits),
                    Builtin::Popcount => format!("gm_popcount({x})"),
                    Builtin::Bswap => {
                        if !ii.bits.is_multiple_of(8) {
                            return unsupported(e.span, "bswap of a partial byte");
                        }
                        format!("gm_bswap({x}, {})", ii.bits)
                    }
                    _ => format!("gm_bitreverse({x}, {})", ii.bits),
                };
                let r = self.tmp(t, &format!("({c}){call}"));
                if matches!(op, Builtin::Bswap | Builtin::Bitreverse)
                    && let Some(ri) = self.int_info(t)
                {
                    let n = self.normalize(ri, &r);
                    return Ok(self.tmp(t, &n));
                }
                Ok(r)
            }
            Builtin::Pow(fl) => {
                let ii = self.int_info(at).ok_or_else(|| super::Unsupported {
                    message: "not supported by the C back end yet: float pow".into(),
                    span: e.span,
                })?;
                let c = ii.cty();
                let (r, o) = (format!("r{}", self.fresh()), format!("o{}", self.fresh()));
                self.line(&format!("{c} {r} = 1; bool {o} = false;"));
                self.line(&format!(
                    "for (unsigned __int128 i = 0; i < (unsigned __int128){}; i++) {{ {c} x; if (__builtin_mul_overflow({r}, {}, &x)) {o} = true; {r} = x; }}",
                    vals[1], vals[0]
                ));
                if !ii.exact() {
                    let (min, max) = (self.int_lit(at, &ii.min()), self.int_lit(at, &ii.max()));
                    self.line(&format!("{o} = {o} || {r} < {min} || {r} > {max};"));
                }
                let sat = format!("({} > 0 || ({} & 1) == 0)", vals[0], vals[1]);
                self.finish_int(fl, ii, at, t, &r, &o, &sat, &loc, "pow")
            }
            Builtin::RangeLen => {
                let (lo, hi) = (&vals[0], &vals[1]);
                Ok(self.tmp(t, &format!("({hi} > {lo} ? (uintptr_t)({hi} - {lo}) : 0)")))
            }
            Builtin::CstrLen => Ok(self.tmp(t, &format!("(uintptr_t)strlen((const char *){})", vals[0]))),
            Builtin::AtomicLoad
            | Builtin::AtomicStore
            | Builtin::AtomicSwap
            | Builtin::AtomicCas
            | Builtin::AtomicRmw(_)
            | Builtin::VolatileLoad
            | Builtin::VolatileStore => {
                let TyKind::Ptr { pointee, .. } = at.kind() else { return unsupported(e.span, "this atomic") };
                let elem = *pointee;
                let c = self.g.ty.cty(elem);
                let p = format!("(({} *){})", c, vals[0]);
                let mut ords = vec![];
                for (x, v) in args.iter().zip(&vals).skip(1) {
                    if !matches!(x.ty.kind(), TyKind::Adt(..)) {
                        continue;
                    }
                    let a = self.addr_of(x.ty, v);
                    ords.push(format!("gm_ord[GM_LD(uint8_t, {})]", a));
                }
                match op {
                    Builtin::VolatileLoad => Ok(self.tmp(t, &format!("*(volatile {} *){}", c, vals[0]))),
                    Builtin::VolatileStore => {
                        self.line(&format!("*(volatile {} *){} = {};", c, vals[0], vals[1]));
                        Ok(self.unit())
                    }
                    Builtin::AtomicLoad => Ok(self.tmp(t, &format!("__atomic_load_n({p}, {})", ords[0]))),
                    Builtin::AtomicStore => {
                        self.line(&format!("__atomic_store_n({p}, {}, {});", vals[1], ords[0]));
                        Ok(self.unit())
                    }
                    Builtin::AtomicSwap => {
                        Ok(self.tmp(t, &format!("__atomic_exchange_n({p}, {}, {})", vals[1], ords[0])))
                    }
                    Builtin::AtomicCas => {
                        let exp = self.tmp(elem, &vals[1]);
                        let ok = self.tmp(
                            Ty::bool(),
                            &format!(
                                "__atomic_compare_exchange_n({p}, &{exp}, {}, 0, {}, {})",
                                vals[2], ords[0], ords[1]
                            ),
                        );
                        let res = self.tmp_uninit(t);
                        let none = self.make_none(t);
                        let some = self.make_some(t, &exp);
                        self.line(&format!("{res} = {ok} ? {none} : {some};"));
                        Ok(res)
                    }
                    Builtin::AtomicRmw(rmw) => {
                        use crate::tir::RmwOp;
                        let f = match rmw {
                            RmwOp::Add => Some("__atomic_fetch_add"),
                            RmwOp::Sub => Some("__atomic_fetch_sub"),
                            RmwOp::And => Some("__atomic_fetch_and"),
                            RmwOp::Or => Some("__atomic_fetch_or"),
                            RmwOp::Xor => Some("__atomic_fetch_xor"),
                            RmwOp::Max | RmwOp::Min => None,
                        };
                        match f {
                            Some(f) => Ok(self.tmp(t, &format!("{f}({p}, {}, {})", vals[1], ords[0]))),
                            None => {
                                let cmp = if rmw == RmwOp::Max { ">" } else { "<" };
                                let old = self.tmp(elem, &format!("__atomic_load_n({p}, __ATOMIC_RELAXED)"));
                                let v = &vals[1];
                                self.line(&format!(
                                    "while (!__atomic_compare_exchange_n({p}, &{old}, {v} {cmp} {old} ? {v} : {old}, 1, {}, __ATOMIC_RELAXED)) {{}}",
                                    ords[0]
                                ));
                                Ok(old)
                            }
                        }
                    }
                    _ => unreachable!(),
                }
            }
            Builtin::Fence => {
                let a = self.addr_of(at, &vals[0]);
                self.line(&format!("__atomic_thread_fence(gm_ord[GM_LD(uint8_t, {})]);", a));
                Ok(self.unit())
            }
            Builtin::Hash => {
                let f = self.g.helpers.hash_fn(&mut self.g.ty, at);
                let a = self.addr_of(at, &vals[0]);
                Ok(self.tmp(t, &format!("{}({}, 0xcbf29ce484222325ULL ^ (uint64_t){})", f, a, vals[1])))
            }
            _ => unsupported(e.span, format!("the builtin {:?}", op)),
        }
    }

    /// `a.get(i)`, `a.get_ptr(i)`, `a.first()`, `a.last()`: `null` when out of range.
    fn get(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<String> {
        let t = e.ty;
        let base = &args[0];
        let (ptr, len) = match base.ty.kind() {
            TyKind::Slice { .. } => {
                let v = self.expr(base)?;
                let a = self.addr_of(base.ty, &v);
                let (p, l) = (self.slice_ptr(&a), self.slice_len(&a));
                (format!("((uint8_t *){})", p), l)
            }
            TyKind::Array { len, .. } => (self.place_addr(base)?, len.to_string()),
            TyKind::Ptr { pointee, .. } => match pointee.kind() {
                TyKind::Array { len, .. } => {
                    let v = self.expr(base)?;
                    (format!("((uint8_t *){})", v), len.to_string())
                }
                _ => return unsupported(e.span, "get on a pointer"),
            },
            _ => return unsupported(e.span, format!("get on `{}`", base.ty)),
        };
        let ptr = self.tmp(Ty::ptr(false, Ty::u8()), &ptr);
        let len = self.tmp(Ty::usize(), &format!("(uintptr_t){}", len));
        let i = match op {
            Builtin::First => "(uintptr_t)0".to_string(),
            Builtin::Last => format!("({} - 1)", len),
            _ => {
                let v = self.expr(&args[1])?;
                format!("(uintptr_t){}", v)
            }
        };
        let i = self.tmp(Ty::usize(), &i);
        let TyKind::Optional(inner) = t.kind() else { return unsupported(e.span, "get") };
        let inner = *inner;
        let elem = match (op, inner.kind()) {
            (Builtin::GetPtr, TyKind::Ptr { pointee, .. }) => *pointee,
            _ => inner,
        };
        let esz = self.g.ty.size(elem);
        let res = self.tmp_uninit(t);
        self.line(&format!("if ({} < {}) {{", i, len));
        let addr = format!("((uint8_t *){} + {} * {})", ptr, i, esz);
        let v = if op == Builtin::GetPtr { format!("(void *){}", addr) } else { self.load(inner, &addr) };
        let some = self.make_some(t, &v);
        self.line(&format!("    {} = {};", res, some));
        self.line("} else {");
        let none = self.make_none(t);
        self.line(&format!("    {} = {};", res, none));
        self.line("}");
        Ok(res)
    }

    /// The numeric conversions `T(x)`, `.trunc`, `.round`, `.floor`, `.ceil`, `.checked`, `.sat`.
    fn convert(&mut self, e: &Expr, op: Builtin, src: Ty, v: &str) -> CResult<String> {
        let t = e.ty;
        let target = if op == Builtin::Checked {
            match t.kind() {
                TyKind::Optional(x) => *x,
                _ => t,
            }
        } else {
            t
        };
        let loc = self.loc(e);
        // The source as a number: integers (and bools, enum tags) or a float.
        let (src_int, src_val) = match self.g.ty.repr(src) {
            Repr::Int { .. } => (true, v.to_string()),
            Repr::Bool => (true, format!("(int64_t){}", v)),
            Repr::Float(_) => (false, format!("(double){}", v)),
            Repr::Blob if matches!(src.kind(), TyKind::Adt(..)) => match &self.adt_def(src).kind {
                AdtKind::Union { tag, .. } => {
                    let tag = *tag;
                    let a = self.addr_of(src, v);
                    (true, self.load(tag, &a))
                }
                AdtKind::Struct { bits: Some(u), .. } => {
                    let u = *u;
                    let a = self.addr_of(src, v);
                    (true, self.load(u, &a))
                }
                _ => return unsupported(e.span, format!("converting from `{}`", src)),
            },
            _ => return unsupported(e.span, format!("converting from `{}`", src)),
        };
        match self.g.ty.repr(target) {
            Repr::Int { .. } => {
                let ii = self.int_info(target).unwrap();
                let c = ii.cty();
                if src_int {
                    let si = match self.g.ty.repr(src) {
                        Repr::Int { signed, .. } => signed,
                        _ => true,
                    };
                    let fits = self.fits(ii, &src_val, si);
                    let wrapped = self.normalize(ii, &format!("({c}){src_val}"));
                    match op {
                        Builtin::Checked => {
                            let res = self.tmp_uninit(t);
                            let w = self.tmp(target, &wrapped);
                            let some = self.make_some(t, &w);
                            let none = self.make_none(t);
                            self.line(&format!("{res} = {fits} ? {some} : {none};"));
                            Ok(res)
                        }
                        Builtin::Sat => {
                            let (min, max) = (self.int_lit(target, &ii.min()), self.int_lit(target, &ii.max()));
                            let neg = if si { format!("({src_val} < 0)") } else { "0".into() };
                            Ok(self.tmp(t, &format!("{fits} ? {wrapped} : ({neg} ? {min} : {max})")))
                        }
                        _ => Ok(self.tmp(t, &wrapped)),
                    }
                } else {
                    let x = self.tmp(Ty::f64(), &src_val);
                    let rounded = match op {
                        Builtin::Round => format!("__builtin_roundeven({x})"),
                        Builtin::Floor => format!("__builtin_floor({x})"),
                        Builtin::Ceil => format!("__builtin_ceil({x})"),
                        _ => format!("__builtin_trunc({x})"),
                    };
                    let r = self.tmp(Ty::f64(), &rounded);
                    let (lo, hi) =
                        (format!("{:e}", int_to_f64(&ii.min())), format!("{:e}", int_to_f64(&(ii.max() + 1))));
                    let in_range = format!("(__builtin_isfinite({r}) && {r} >= {lo} && {r} < {hi})");
                    match op {
                        Builtin::Checked => {
                            let res = self.tmp_uninit(t);
                            self.line(&format!("if ({in_range} && {x} == {r}) {{"));
                            let w = self.tmp(target, &format!("({c}){r}"));
                            let some = self.make_some(t, &w);
                            self.line(&format!("    {res} = {some};"));
                            self.line("} else {");
                            let none = self.make_none(t);
                            self.line(&format!("    {res} = {none};"));
                            self.line("}");
                            Ok(res)
                        }
                        Builtin::Sat => {
                            let (min, max) = (self.int_lit(target, &ii.min()), self.int_lit(target, &ii.max()));
                            Ok(self.tmp(
                                t,
                                &format!("__builtin_isnan({x}) ? ({c})0 : ({r} < {lo} ? {min} : ({r} >= {hi} ? {max} : ({c}){r}))"),
                            ))
                        }
                        _ => {
                            if self.checks() {
                                self.line(&format!(
                                    "if (!{in_range}) gm_trap(\"float to integer conversion out of range\", {loc});"
                                ));
                            }
                            Ok(self.tmp(t, &format!("({c}){r}")))
                        }
                    }
                }
            }
            Repr::Float(_) => {
                let c = self.g.ty.cty(target);
                Ok(self.tmp(t, &format!("({c}){src_val}")))
            }
            // An integer to a `#bits` struct: its bits, wrapped to the underlying type.
            _ if src_int && self.g.ty.bits_layout(target).is_some() => {
                let (u, _) = self.g.ty.bits_layout(target).unwrap();
                let ui = self.int_info(u).unwrap();
                let w = self.normalize(ui, &format!("({}){}", ui.cty(), src_val));
                let b = self.tmp_uninit(target);
                self.line(&format!("GM_ST({}, (uint8_t *)&{}, {});", ui.cty(), b, w));
                Ok(b)
            }
            _ => unsupported(e.span, format!("converting to `{}`", target)),
        }
    }

    /// A C condition: the integer `v` (signed if `signed`) fits the type `ii`.
    fn fits(&mut self, ii: IntInfo, v: &str, signed: bool) -> String {
        let max = ii.max();
        let (lo_hi, hi_lo) = super::expr::split_u128(&max);
        let max_lit = format!("(((unsigned __int128){}ULL << 64) | {}ULL)", hi_lo as u64, lo_hi as u64);
        if signed {
            let min = ii.min();
            let (ml, mh) = super::expr::split_u128(&min);
            let min_lit = format!("((__int128)(((unsigned __int128){}ULL << 64) | {}ULL))", mh as u64, ml as u64);
            format!("((__int128)({v}) >= {min_lit} && ((__int128)({v}) < 0 || (unsigned __int128)({v}) <= {max_lit}))")
        } else {
            format!("((unsigned __int128)({v}) <= {max_lit})")
        }
    }
}

fn pointee_align(fg: &FnGen, t: Ty) -> u64 {
    match t.kind() {
        TyKind::Ptr { pointee: p, .. } | TyKind::ManyPtr { elem: p, .. } => {
            if fg.g.ty.size(*p) == 0 {
                1
            } else {
                fg.g.ty.align(*p)
            }
        }
        TyKind::Optional(x) => pointee_align(fg, *x),
        _ => 1,
    }
}

fn int_to_f64(v: &BigInt) -> f64 {
    v.to_string().parse().unwrap_or(0.0)
}
