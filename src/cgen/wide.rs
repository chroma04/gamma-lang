//! Integers wider than 128 bits (§5.1).
//!
//! These are blobs holding the two's-complement value in 64-bit words, least
//! significant first, each in the target's byte order, and every operation is a
//! call to one of the runtime's `gm_w*` routines with the address of each operand, the width and the signedness. The routines return the
//! overflow flag that the flavor of the operator (§7.4.2) needs, and they ignore
//! the padding bits above the width and always write them as the extension, so a
//! value is the same whichever way it was made.

use super::CResult;
use super::func::FnGen;
use super::types::{Repr, WideInfo};
use super::unsupported;
use crate::tir::{AdtKind, BinOp, Builtin, Expr, Flavor, UnOp};
use crate::types::{Ty, TyKind};
use num_bigint::BigInt;
use num_traits::Signed;

/// An integer operand of a conversion: the address of its bytes, storage size, width, signedness.
struct IntSrc {
    ptr: String,
    size: u64,
    bits: u32,
    signed: bool,
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
    pub fn wide(&self, t: Ty) -> Option<WideInfo> {
        self.g.ty.wide(t)
    }

    /// The `bits, signed` arguments of the runtime routines.
    fn wargs(w: WideInfo) -> String {
        format!("{}, {}", w.bits, w.signed as u8)
    }

    /// A C literal of the wide type `t` with value `v` (reduced to the type's width).
    pub fn wide_lit(&mut self, t: Ty, w: WideInfo, v: &BigInt) -> String {
        let modulus = BigInt::from(1u8) << (w.size as usize * 8);
        let mut x = v % &modulus;
        if x.is_negative() {
            x += &modulus;
        }
        let mut bytes = x.to_bytes_le().1;
        bytes.resize(w.size as usize, 0);
        let mut bytes = crate::layout::int_bytes_swap_order(&bytes, self.g.ty.lay.target.little_endian);
        while bytes.last() == Some(&0) {
            bytes.pop();
        }
        let c = self.g.ty.cty(t);
        if bytes.is_empty() {
            return format!("(({c}){{}})");
        }
        let list: Vec<String> = bytes.iter().map(|b| b.to_string()).collect();
        format!("(({c}){{{{{}}}}})", list.join(","))
    }

    /// The integer `v` of type `t` as a `uintptr_t` (an index or slice bound); a wide
    /// integer too large for 64 bits becomes the largest value, which no bounds check passes.
    pub fn usize_value(&mut self, t: Ty, v: &str) -> String {
        match self.wide(t) {
            Some(w) => {
                let p = self.addr_of(t, v);
                format!("(uintptr_t)gm_wshamt({p}, {})", w.bits)
            }
            None => format!("(uintptr_t){v}"),
        }
    }

    /// A C condition comparing two integers of type `t` (`op` is a C comparison operator).
    pub fn int_cmp(&mut self, t: Ty, a: &str, op: &str, b: &str) -> String {
        match self.wide(t) {
            Some(w) => {
                let (pa, pb) = (self.addr_of(t, a), self.addr_of(t, b));
                format!("(gm_wcmp({pa}, {pb}, {}) {op} 0)", Self::wargs(w))
            }
            None => format!("({a} {op} {b})"),
        }
    }

    /// Sets the blob or scalar `res` of the integer type `t` to its minimum or maximum.
    fn set_extreme(&mut self, t: Ty, res: &str, max: bool) {
        match self.wide(t) {
            Some(w) => {
                let f = if max { "gm_wmax" } else { "gm_wmin" };
                self.line(&format!("{f}((uint8_t *)&{res}, {});", Self::wargs(w)));
            }
            None => {
                let ii = self.int_info(t).unwrap();
                let v = self.int_lit(t, &if max { ii.max() } else { ii.min() });
                self.line(&format!("{res} = {v};"));
            }
        }
    }

    pub fn wide_unary(&mut self, e: &Expr, op: UnOp, w: WideInfo, xty: Ty, v: &str) -> CResult<String> {
        let t = e.ty;
        let p = self.addr_of(xty, v);
        let r = self.tmp_uninit(t);
        match op {
            UnOp::Neg => {
                let o = format!("o{}", self.fresh());
                self.line(&format!("bool {o} = gm_wneg((uint8_t *)&{r}, {p}, {});", Self::wargs(w)));
                if self.g.opts.checks {
                    let loc = self.loc(e);
                    self.line(&format!("if ({o}) gm_trap(\"integer overflow in `-`\", {loc});"));
                }
            }
            UnOp::BitNot => self.line(&format!("gm_wnot((uint8_t *)&{r}, {p}, {});", Self::wargs(w))),
            UnOp::Not => return unsupported(e.span, format!("`!` on `{xty}`")),
        }
        Ok(r)
    }

    /// Completes an operation whose wrapped result is the blob `res` and whose overflow flag is `o`.
    /// `up` is a C condition saying which way a saturating result goes.
    #[allow(clippy::too_many_arguments)]
    fn finish_wide(
        &mut self,
        fl: Flavor,
        lt: Ty,
        t: Ty,
        res: &str,
        o: &str,
        up: &str,
        loc: &str,
        sym: &str,
    ) -> CResult<String> {
        match fl {
            Flavor::Checked => {
                if self.g.opts.checks {
                    self.line(&format!("if ({o}) gm_trap(\"integer overflow in `{sym}`\", {loc});"));
                }
                Ok(res.to_string())
            }
            Flavor::Wrap => Ok(res.to_string()),
            Flavor::Sat => {
                self.line(&format!("if ({o}) {{"));
                self.indent += 1;
                self.line(&format!("if ({up}) {{"));
                self.set_extreme(lt, res, true);
                self.line("} else {");
                self.set_extreme(lt, res, false);
                self.line("}");
                self.indent -= 1;
                self.line("}");
                Ok(res.to_string())
            }
            Flavor::Opt => {
                let out = self.tmp_uninit(t);
                self.line(&format!("if ({o}) {{"));
                let none = self.make_none(t);
                self.line(&format!("    {out} = {none};"));
                self.line("} else {");
                let some = self.make_some(t, res);
                self.line(&format!("    {out} = {some};"));
                self.line("}");
                Ok(out)
            }
        }
    }

    /// A binary operator on `lt`, which is wide. `rt` is the type of `b` (it differs for shifts).
    pub fn wide_binary(&mut self, e: &Expr, op: BinOp, lt: Ty, rt: Ty, a: &str, b: &str) -> CResult<String> {
        let w = self.wide(lt).expect("a wide integer type");
        let t = e.ty;
        let loc = self.loc(e);
        let args = Self::wargs(w);
        let pa = self.addr_of(lt, a);
        if matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge) {
            let pb = self.addr_of(lt, b);
            return Ok(self.tmp(t, &format!("(uint8_t)(gm_wcmp({pa}, {pb}, {args}) {} 0)", op_symbol(op))));
        }
        if let BinOp::Shl(_) | BinOp::Shr = op {
            return self.wide_shift(e, op, w, lt, rt, &pa, b);
        }
        let pb = self.addr_of(lt, b);
        let r = self.tmp_uninit(lt);
        match op {
            BinOp::Add(fl) | BinOp::Sub(fl) | BinOp::Mul(fl) => {
                let name = match op {
                    BinOp::Add(_) => "add",
                    BinOp::Sub(_) => "sub",
                    _ => "mul",
                };
                let o = format!("o{}", self.fresh());
                self.line(&format!("bool {o} = gm_w{name}((uint8_t *)&{r}, {pa}, {pb}, {args});"));
                let up = match op {
                    _ if !w.signed => {
                        if matches!(op, BinOp::Sub(_)) {
                            "0".to_string()
                        } else {
                            "1".to_string()
                        }
                    }
                    BinOp::Add(_) => format!("gm_wsign({pb}, {args}) > 0"),
                    BinOp::Sub(_) => format!("gm_wsign({pb}, {args}) < 0"),
                    _ => format!("((gm_wsign({pa}, {args}) < 0) == (gm_wsign({pb}, {args}) < 0))"),
                };
                self.finish_wide(fl, lt, t, &r, &o, &up, &loc, op_symbol(op))
            }
            BinOp::Div | BinOp::Rem => {
                let sym = op_symbol(op);
                let d = format!("d{}", self.fresh());
                let (q, m) = if let BinOp::Div = op {
                    (format!("(uint8_t *)&{r}"), "NULL".to_string())
                } else {
                    ("NULL".to_string(), format!("(uint8_t *)&{r}"))
                };
                self.line(&format!("int {d} = gm_wdiv({q}, {m}, {pa}, {pb}, {args});"));
                self.line(&format!("if ({d} == 1) gm_trap(\"division by zero\", {loc});"));
                self.line(&format!("if ({d} == 2) gm_trap(\"integer overflow in `{sym}`\", {loc});"));
                Ok(r)
            }
            BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor => {
                let f = match op {
                    BinOp::BitAnd => "gm_wand",
                    BinOp::BitOr => "gm_wor",
                    _ => "gm_wxor",
                };
                self.line(&format!("{f}((uint8_t *)&{r}, {pa}, {pb}, {args});"));
                Ok(r)
            }
            _ => unreachable!(),
        }
    }

    /// `a << n` and `a >> n` (`pa` is the address of `a`, `b` the amount, of type `rt`).
    #[allow(clippy::too_many_arguments)]
    fn wide_shift(&mut self, e: &Expr, op: BinOp, w: WideInfo, lt: Ty, rt: Ty, pa: &str, b: &str) -> CResult<String> {
        let t = e.ty;
        let loc = self.loc(e);
        let args = Self::wargs(w);
        // The amount as the address of an unsigned integer, and its width.
        let (amt, abits) = match self.wide(rt) {
            Some(rw) => (self.addr_of(rt, b), rw.bits),
            None => {
                let n = format!("a{}", self.fresh());
                self.line(&format!("unsigned __int128 {n} = (unsigned __int128){b};"));
                (format!("((uint8_t *)&{n})"), 128)
            }
        };
        let n = format!("n{}", self.fresh());
        self.line(&format!("uint64_t {n} = gm_wshamt({amt}, {abits});"));
        if self.g.opts.checks {
            let name = lt.to_string();
            self.line(&format!("if ({n} >= {}) gm_trap_shift({amt}, {abits}, \"{name}\", {loc});", w.bits));
        } else {
            self.line(&format!("{n} %= {};", w.bits));
        }
        if let BinOp::Shr = op {
            let r = self.tmp_uninit(t);
            self.line(&format!("gm_wshr((uint8_t *)&{r}, {pa}, {n}, {args});"));
            return Ok(r);
        }
        let BinOp::Shl(fl) = op else { unreachable!() };
        let r = self.tmp_uninit(lt);
        let o = format!("o{}", self.fresh());
        self.line(&format!("bool {o} = gm_wshl((uint8_t *)&{r}, {pa}, {n}, {args});"));
        let up = format!("gm_wsign({pa}, {args}) > 0");
        self.finish_wide(fl, lt, t, &r, &o, &up, &loc, "<<")
    }

    // --- conversions --------------------------------------------------------------------------------

    /// The integer `v` of type `src` as the address and shape of its bytes; `None` for a float.
    fn int_source(&mut self, e: &Expr, src: Ty, v: &str) -> CResult<Option<IntSrc>> {
        if let Some(w) = self.wide(src) {
            let ptr = self.addr_of(src, v);
            return Ok(Some(IntSrc { ptr, size: w.size, bits: w.bits, signed: w.signed }));
        }
        match self.g.ty.repr(src) {
            Repr::Int { signed, bits, storage } => {
                let ptr = self.addr_of(src, v);
                Ok(Some(IntSrc { ptr, size: storage, bits, signed }))
            }
            Repr::Bool => {
                let ptr = self.addr_of(src, v);
                Ok(Some(IntSrc { ptr, size: 1, bits: 8, signed: false }))
            }
            Repr::Float(_) => Ok(None),
            Repr::Blob if matches!(src.kind(), TyKind::Adt(..)) => {
                let inner = match &self.adt_def(src).kind {
                    AdtKind::Union { tag, .. } => *tag,
                    AdtKind::Struct { bits: Some(u), .. } => *u,
                    _ => return unsupported(e.span, format!("converting from `{src}`")),
                };
                let a = self.addr_of(src, v);
                let x = self.load(inner, &a);
                self.int_source(e, inner, &x)
            }
            _ => unsupported(e.span, format!("converting from `{src}`")),
        }
    }

    /// `T(x)`, `.trunc`, `.round`, `.checked`, `.sat`, … when the source or the target is a wide integer.
    pub fn wide_convert(&mut self, e: &Expr, op: Builtin, src: Ty, v: &str, target: Ty) -> CResult<String> {
        let t = e.ty;
        let Some(s) = self.int_source(e, src, v)? else {
            return self.wide_from_float(e, op, src, v, target);
        };
        if let Repr::Float(f) = self.g.ty.repr(target) {
            let mant = if matches!(f, crate::types::FloatTy::F32) { 24 } else { 53 };
            let r = self.float_from(target, &format!("gm_wtof({}, {}, {}, {mant})", s.ptr, s.bits, s.signed as u8));
            return Ok(self.tmp(target, &r));
        }
        // Integer to integer.
        let (rsize, rbits, rsigned) = if let Some(w) = self.wide(target) {
            (w.size, w.bits, w.signed)
        } else if let Repr::Int { signed, bits, storage } = self.g.ty.repr(target) {
            (storage, bits, signed)
        } else {
            return unsupported(e.span, format!("converting to `{target}`"));
        };
        let res = self.tmp_uninit(target);
        let f = format!("f{}", self.fresh());
        self.line(&format!(
            "int {f} = gm_wconv((uint8_t *)&{res}, {rsize}, {rbits}, {}, {}, {}, {}, {});",
            rsigned as u8, s.ptr, s.size, s.bits, s.signed as u8
        ));
        match op {
            Builtin::Checked => {
                let out = self.tmp_uninit(t);
                self.line(&format!("if ({f} & 1) {{"));
                let some = self.make_some(t, &res);
                self.line(&format!("    {out} = {some};"));
                self.line("} else {");
                let none = self.make_none(t);
                self.line(&format!("    {out} = {none};"));
                self.line("}");
                Ok(out)
            }
            Builtin::Sat => {
                self.line(&format!("if (!({f} & 1)) {{"));
                self.indent += 1;
                self.line(&format!("if ({f} & 2) {{"));
                self.set_extreme(target, &res, false);
                self.line("} else {");
                self.set_extreme(target, &res, true);
                self.line("}");
                self.indent -= 1;
                self.line("}");
                Ok(res)
            }
            _ => Ok(res),
        }
    }

    /// A float converted to a wide integer.
    fn wide_from_float(&mut self, e: &Expr, op: Builtin, src: Ty, v: &str, target: Ty) -> CResult<String> {
        let t = e.ty;
        let loc = self.loc(e);
        let Some(w) = self.wide(target) else { return unsupported(e.span, format!("converting from `{src}`")) };
        let x = self.tmp(Ty::f64(), &format!("(double){v}"));
        let rounded = match op {
            Builtin::Round => format!("GM_ROUNDEVEN({x})"),
            Builtin::Floor => format!("__builtin_floor({x})"),
            Builtin::Ceil => format!("__builtin_ceil({x})"),
            _ => format!("__builtin_trunc({x})"),
        };
        let r = self.tmp(Ty::f64(), &rounded);
        let res = self.tmp_uninit(target);
        let st = format!("s{}", self.fresh());
        self.line(&format!("int {st} = gm_wfromd((uint8_t *)&{res}, {}, {r});", Self::wargs(w)));
        match op {
            Builtin::Checked => {
                let out = self.tmp_uninit(t);
                self.line(&format!("if ({st} == 0 && {x} == {r}) {{"));
                let some = self.make_some(t, &res);
                self.line(&format!("    {out} = {some};"));
                self.line("} else {");
                let none = self.make_none(t);
                self.line(&format!("    {out} = {none};"));
                self.line("}");
                Ok(out)
            }
            Builtin::Sat => {
                self.line(&format!("if ({st} == 1) {{"));
                self.set_extreme(target, &res, false);
                self.line(&format!("}} else if ({st} == 2) {{"));
                self.set_extreme(target, &res, true);
                self.line("}");
                Ok(res)
            }
            _ => {
                if self.g.opts.checks {
                    self.line(&format!("if ({st} != 0) gm_trap(\"float to integer conversion out of range\", {loc});"));
                }
                Ok(res)
            }
        }
    }

    // --- builtins ---------------------------------------------------------------------------------

    /// Builtins on a wide integer: `min`, `max`, `clamp`, `abs`, `abs_diff`, `pow` and bit counting.
    pub fn wide_builtin(&mut self, e: &Expr, op: Builtin, w: WideInfo, at: Ty, vals: &[String]) -> CResult<String> {
        let t = e.ty;
        let loc = self.loc(e);
        let args = Self::wargs(w);
        let p0 = self.addr_of(at, &vals[0]);
        match op {
            Builtin::Min | Builtin::Max => {
                let p1 = self.addr_of(at, &vals[1]);
                let cmp = if op == Builtin::Min { "<=" } else { ">=" };
                Ok(self.tmp(t, &format!("(gm_wcmp({p0}, {p1}, {args}) {cmp} 0 ? {} : {})", vals[0], vals[1])))
            }
            Builtin::Clamp => {
                let (p1, p2) = (self.addr_of(at, &vals[1]), self.addr_of(at, &vals[2]));
                Ok(self.tmp(
                    t,
                    &format!(
                        "(gm_wcmp({p0}, {p1}, {args}) < 0 ? {lo} : (gm_wcmp({p0}, {p2}, {args}) > 0 ? {hi} : {x}))",
                        lo = vals[1],
                        hi = vals[2],
                        x = vals[0]
                    ),
                ))
            }
            Builtin::Abs => {
                let r = self.tmp_uninit(t);
                let o = format!("o{}", self.fresh());
                self.line(&format!("bool {o} = false;"));
                self.line(&format!(
                    "if (gm_wsign({p0}, {args}) < 0) {o} = gm_wneg((uint8_t *)&{r}, {p0}, {args}); else {r} = {};",
                    vals[0]
                ));
                if self.g.opts.checks {
                    self.line(&format!("if ({o}) gm_trap(\"integer overflow in `abs`\", {loc});"));
                }
                Ok(r)
            }
            Builtin::AbsDiff => {
                let p1 = self.addr_of(at, &vals[1]);
                let r = self.tmp_uninit(t);
                self.line(&format!(
                    "if (gm_wcmp({p0}, {p1}, {args}) > 0) gm_wsub((uint8_t *)&{r}, {p0}, {p1}, {}, 0); else gm_wsub((uint8_t *)&{r}, {p1}, {p0}, {}, 0);",
                    w.bits, w.bits
                ));
                Ok(r)
            }
            Builtin::Pow(fl) => {
                let n = format!("n{}", self.fresh());
                self.line(&format!("uint32_t {n} = (uint32_t){};", vals[1]));
                let r = self.tmp_uninit(at);
                let o = format!("o{}", self.fresh());
                self.line(&format!("bool {o} = gm_wpow((uint8_t *)&{r}, {p0}, {n}, {args});"));
                if fl == Flavor::Checked && self.g.opts.checks {
                    self.line(&format!(
                        "if ({o}) {{ gm_buf m = {{0}}; gm_buf_push(&m, \"integer overflow in `pow` (\", sizeof(\"integer overflow in `pow` (\") - 1); gm_wfmt(&m, {p0}, {args}); char s[16]; int k = snprintf(s, sizeof s, \"^%u)\", (unsigned){n}); gm_buf_push(&m, s, (size_t)k); gm_trap_bytes(m.data, m.len, {loc}); }}"
                    ));
                    return Ok(r);
                }
                let up = format!("!(gm_wsign({p0}, {args}) < 0 && ({n} & 1))");
                self.finish_wide(fl, at, t, &r, &o, &up, &loc, "pow")
            }
            Builtin::Clz | Builtin::Ctz | Builtin::Popcount => {
                let f = match op {
                    Builtin::Clz => "gm_wclz",
                    Builtin::Ctz => "gm_wctz",
                    _ => "gm_wpopcount",
                };
                Ok(self.tmp(t, &format!("{f}({p0}, {})", w.bits)))
            }
            Builtin::Bswap => {
                let r = self.tmp_uninit(t);
                self.line(&format!("gm_wbswap((uint8_t *)&{r}, {p0}, {});", w.bits));
                Ok(r)
            }
            Builtin::Bitreverse => {
                let r = self.tmp_uninit(t);
                self.line(&format!("gm_wbitreverse((uint8_t *)&{r}, {p0}, {args});"));
                Ok(r)
            }
            Builtin::Rotl | Builtin::Rotr => {
                let r = self.tmp_uninit(t);
                let k = format!("(uint64_t){}", vals[1]);
                let k = if op == Builtin::Rotl { k } else { format!("({b} - {k} % {b})", b = w.bits) };
                self.line(&format!("gm_wrotl((uint8_t *)&{r}, {p0}, {args}, {k});"));
                Ok(r)
            }
            _ => unsupported(e.span, format!("the builtin {op:?} on `{at}`")),
        }
    }
}
