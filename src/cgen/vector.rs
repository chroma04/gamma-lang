//! Vectors `#simd [N]T` (§5.6.2). A vector value is stored like the array of its lanes
//! (a typed struct aligned to its size); its operators load the operands into GCC/Clang
//! vector types, so that an operation without a check is one vector instruction where
//! the target has one. Operations whose scalar form has a check (`+` on integers, shifts,
//! `abs`), when checks are on, go lane by lane with the scalar code and its checks.

use super::CResult;
use super::func::FnGen;
use crate::tir::{BinOp, Builtin, Expr, ExprKind, Flavor, UnOp};
use crate::types::{Ty, TyKind};

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

impl<'g, 'a> FnGen<'g, 'a> {
    /// The C vector type of `n` lanes of `lane` (`bool` lanes are bytes). With `twin`,
    /// the integer type of the same lane width instead: `Some(false)` unsigned,
    /// `Some(true)` signed.
    fn vty(&mut self, n: u64, lane: Ty, twin: Option<bool>) -> String {
        let bytes = parts(Ty::vector(n, lane)).2;
        let (c, tag) = match (twin, lane.kind()) {
            (Some(signed), _) => {
                let bits = bytes * 8;
                if signed {
                    (format!("int{}_t", bits), format!("i{}", bits))
                } else {
                    (format!("uint{}_t", bits), format!("u{}", bits))
                }
            }
            (None, TyKind::Float(f)) => {
                (if f.bits() == 32 { "float".to_string() } else { "double".to_string() }, format!("f{}", f.bits()))
            }
            (None, TyKind::Int(i)) => {
                let s = if i.signed { "" } else { "u" };
                (format!("{}int{}_t", s, i.bits), format!("{}{}", if i.signed { "i" } else { "u" }, i.bits))
            }
            _ => ("uint8_t".to_string(), "u8".to_string()),
        };
        self.g.ty.vector_typedef(&c, &tag, n, bytes)
    }

    /// Loads the vector value `v` of type `t` into a C vector variable; returns its name.
    fn load_vec(&mut self, t: Ty, v: &str) -> String {
        let (n, lane, _) = parts(t);
        let vt = self.vty(n, lane, None);
        let p = self.addr_of(t, v);
        let name = format!("v{}", self.fresh());
        self.line(&format!("{} {}; memcpy(&{}, {}, sizeof {});", vt, name, name, p, name));
        name
    }

    /// Stores the C vector expression `x` (of the size of `t`) as a value of type `t`.
    fn store_vec(&mut self, t: Ty, x: &str) -> String {
        let res = self.tmp_uninit(t);
        let n = format!("v{}", self.fresh());
        self.line(&format!("__typeof__({x}) {n} = {x}; memcpy(&{res}, &{n}, sizeof {res});"));
        res
    }

    /// A value of type `res_t` (a vector or a scalar accumulator is built by the caller)
    /// whose lane `i` is `f(i)` for each of `n` lanes; `f` returns a C expression of the
    /// lane type of `res_t`, which may use the loop index.
    fn lane_loop(
        &mut self,
        res_t: Ty,
        n: u64,
        mut f: impl FnMut(&mut Self, &str) -> CResult<String>,
    ) -> CResult<String> {
        let (_, lane, bytes) = parts(res_t);
        let ce = self.g.ty.cty(lane);
        let res = self.tmp_uninit(res_t);
        let i = format!("i{}", self.fresh());
        self.line(&format!("for (size_t {i} = 0; {i} < {n}; {i}++) {{"));
        self.indent += 1;
        let v = f(self, &i)?;
        self.line(&format!("GM_ST({ce}, (uint8_t *)&{res} + {i} * {bytes}, {v});"));
        self.indent -= 1;
        self.line("}");
        Ok(res)
    }

    /// Lane `i` of the vector whose address is `p`, as a temporary of the lane type.
    fn lane_at(&mut self, lane: Ty, bytes: u64, p: &str, i: &str) -> String {
        let ce = self.g.ty.cty(lane);
        self.tmp(lane, &format!("GM_LD({ce}, {p} + {i} * {bytes})"))
    }

    pub fn vector_unary(&mut self, e: &'a Expr, op: UnOp, x: &'a Expr) -> CResult<String> {
        let t = e.ty;
        let (n, lane, bytes) = parts(t);
        let v = self.expr(x)?;
        if op == UnOp::Neg && lane.is_integer() && self.g.opts.checks {
            return self.elementwise(e, Err(op), None, x);
        }
        let a = self.load_vec(t, &v);
        let r = match op {
            UnOp::Neg if lane.is_float() => format!("-{a}"),
            UnOp::Neg => {
                let u = self.vty(n, lane, Some(false));
                let s = self.vty(n, lane, None);
                format!("({s})(({u}){{0}} - ({u}){a})")
            }
            _ if lane.is_bool() => format!("({a} ^ 1)"),
            _ => format!("~{a}"),
        };
        let _ = bytes;
        Ok(self.store_vec(t, &r))
    }

    pub fn vector_binary(&mut self, e: &'a Expr, op: BinOp, l: &'a Expr, r: &'a Expr) -> CResult<String> {
        let t = e.ty;
        let (n, lane, bytes) = parts(t);
        let checks = self.g.opts.checks;
        let checked = match op {
            BinOp::Add(f) | BinOp::Sub(f) | BinOp::Mul(f) => {
                f == Flavor::Sat || (f == Flavor::Checked && lane.is_integer() && checks)
            }
            BinOp::Shl(_) | BinOp::Shr => checks,
            _ => false,
        };
        if checked {
            if matches!(op, BinOp::Shl(_) | BinOp::Shr) {
                // Every lane shifts by the one scalar amount, with the scalar checks.
                let a = self.expr(l)?;
                let b = self.expr(r)?;
                let pa = self.addr_of(t, &a);
                return self.lane_loop(t, n, |cx, i| {
                    let x = cx.lane_at(lane, bytes, &pa, i);
                    let at = Expr { ty: lane, kind: ExprKind::Uninit, span: e.span };
                    cx.int_binary(&at, op, lane, &x, &b)
                });
            }
            return self.elementwise(e, Ok(op), Some(l), r);
        }
        let a = self.expr(l)?;
        let b = self.expr(r)?;
        let va = self.load_vec(t, &a);
        if let BinOp::Shl(_) | BinOp::Shr = op {
            // Without checks an amount of the lane width or more gives an unspecified value:
            // here the amount modulo the width, never undefined behavior in C.
            let sym = if op == BinOp::Shr { ">>" } else { "<<" };
            let mask = bytes * 8 - 1;
            let x = if op == BinOp::Shr {
                format!("({va} >> (int)(({b}) & {mask}))")
            } else {
                let u = self.vty(n, lane, Some(false));
                let s = self.vty(n, lane, None);
                format!("({s})(({u}){va} {sym} (int)(({b}) & {mask}))")
            };
            return Ok(self.store_vec(t, &x));
        }
        let vb = self.load_vec(t, &b);
        let sym = match op {
            BinOp::Add(_) => "+",
            BinOp::Sub(_) => "-",
            BinOp::Mul(_) => "*",
            BinOp::Div => "/",
            BinOp::BitAnd => "&",
            BinOp::BitOr => "|",
            _ => "^",
        };
        let x = if lane.is_integer() && matches!(op, BinOp::Add(_) | BinOp::Sub(_) | BinOp::Mul(_)) {
            // Wrapping arithmetic, in unsigned lanes (signed overflow is undefined in C).
            let u = self.vty(n, lane, Some(false));
            let s = self.vty(n, lane, None);
            format!("({s})(({u}){va} {sym} ({u}){vb})")
        } else {
            format!("({va} {sym} {vb})")
        };
        Ok(self.store_vec(t, &x))
    }

    /// A lane-wise comparison of `a` and `b` (C vectors of type `t`) as a mask value.
    fn compare(&mut self, t: Ty, sym: &str, a: &str, b: &str) -> String {
        let (n, lane, _) = parts(t);
        let wide = self.vty(n, lane, Some(true));
        let m8 = self.vty(n, Ty::bool(), None);
        let c = format!("c{}", self.fresh());
        self.line(&format!("{wide} {c} = ({wide})({a} {sym} {b});"));
        self.store_vec(Ty::vector(n, Ty::bool()), &format!("(__builtin_convertvector({c}, {m8}) & 1)"))
    }

    /// The lanes of `a` where the mask `m` (a C vector of bytes) is set, else of `b`
    /// (C vectors of type `t`), as a C expression of `t`'s vector type.
    fn blend(&mut self, t: Ty, m: &str, a: &str, b: &str) -> String {
        let (n, lane, _) = parts(t);
        let w = self.vty(n, lane, Some(true));
        let u = self.vty(n, lane, Some(false));
        let s = self.vty(n, lane, None);
        let mm = format!("m{}", self.fresh());
        self.line(&format!("{u} {mm} = ({u})-__builtin_convertvector({m}, {w});"));
        format!("({s})((({u}){a} & {mm}) | (({u}){b} & ~{mm}))")
    }

    /// The vector builtins (§21.3), or `None` if `op` is not one.
    pub fn vector_builtin(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> Option<CResult<String>> {
        let r = match op {
            Builtin::LaneEq
            | Builtin::LaneNe
            | Builtin::LaneLt
            | Builtin::LaneLe
            | Builtin::LaneGt
            | Builtin::LaneGe => self.vec_compare(op, args),
            Builtin::ReduceSum | Builtin::ReduceMin | Builtin::ReduceMax => self.vec_reduce(e, op, &args[0]),
            Builtin::MaskAll | Builtin::MaskAny | Builtin::MaskCount => self.vec_mask_test(e, op, &args[0]),
            Builtin::Select => self.vec_select(e, args),
            Builtin::LaneMin | Builtin::LaneMax | Builtin::LaneAbs => self.vec_minmax_abs(e, op, args),
            Builtin::LaneConvert => self.vec_convert(e, &args[0]),
            Builtin::Shuffle => self.vec_shuffle(e, args),
            Builtin::VecLoad | Builtin::VecStore => self.vec_load_store(e, op, args),
            _ => return None,
        };
        Some(r)
    }

    fn vec_compare(&mut self, op: Builtin, args: &'a [Expr]) -> CResult<String> {
        let t = args[0].ty;
        let a = self.expr(&args[0])?;
        let b = self.expr(&args[1])?;
        let (va, vb) = (self.load_vec(t, &a), self.load_vec(t, &b));
        let sym = match op {
            Builtin::LaneEq => "==",
            Builtin::LaneNe => "!=",
            Builtin::LaneLt => "<",
            Builtin::LaneLe => "<=",
            Builtin::LaneGt => ">",
            _ => ">=",
        };
        Ok(self.compare(t, sym, &va, &vb))
    }

    fn vec_reduce(&mut self, e: &'a Expr, op: Builtin, x: &'a Expr) -> CResult<String> {
        let t = x.ty;
        let (n, lane, bytes) = parts(t);
        let v = self.expr(x)?;
        let p = self.addr_of(t, &v);
        let acc = self.lane_at(lane, bytes, &p, "0");
        let i = format!("i{}", self.fresh());
        self.line(&format!("for (size_t {i} = 1; {i} < {n}; {i}++) {{"));
        self.indent += 1;
        let y = self.lane_at(lane, bytes, &p, &i);
        let at = Expr { ty: lane, kind: ExprKind::Uninit, span: e.span };
        let r = match op {
            Builtin::ReduceSum if lane.is_float() => self.tmp(lane, &format!("({acc} + {y})")),
            Builtin::ReduceSum => self.int_binary(&at, BinOp::Add(Flavor::Checked), lane, &acc, &y)?,
            _ if lane.is_float() => {
                let f = if op == Builtin::ReduceMin { "gm_fmin" } else { "gm_fmax" };
                let r = self.float_from(lane, &format!("{f}((double){acc}, (double){y})"));
                self.tmp(lane, &r)
            }
            _ => {
                let gt = if op == Builtin::ReduceMin { "<" } else { ">" };
                self.tmp(lane, &format!("({acc} {gt} {y} ? {acc} : {y})"))
            }
        };
        self.line(&format!("{acc} = {r};"));
        self.indent -= 1;
        self.line("}");
        Ok(acc)
    }

    fn vec_mask_test(&mut self, e: &'a Expr, op: Builtin, x: &'a Expr) -> CResult<String> {
        let (n, _, _) = parts(x.ty);
        let v = self.expr(x)?;
        let p = self.addr_of(x.ty, &v);
        let count = self.tmp(Ty::usize(), "0");
        self.line(&format!("for (size_t i = 0; i < {n}; i++) {count} += {p}[i];"));
        let r = match op {
            Builtin::MaskAll => format!("(uint8_t)({count} == {n})"),
            Builtin::MaskAny => format!("(uint8_t)({count} != 0)"),
            _ => count,
        };
        Ok(self.tmp(e.ty, &r))
    }

    fn vec_select(&mut self, e: &'a Expr, args: &'a [Expr]) -> CResult<String> {
        let t = e.ty;
        let m = self.expr(&args[0])?;
        let a = self.expr(&args[1])?;
        let b = self.expr(&args[2])?;
        let vm = self.load_vec(args[0].ty, &m);
        let (va, vb) = (self.load_vec(t, &a), self.load_vec(t, &b));
        let r = self.blend(t, &vm, &va, &vb);
        Ok(self.store_vec(t, &r))
    }

    fn vec_minmax_abs(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<String> {
        let t = e.ty;
        let (n, lane, bytes) = parts(t);
        let a = self.expr(&args[0])?;
        let b = match args.get(1) {
            Some(x) => Some(self.expr(x)?),
            None => None,
        };
        // Float `min` and `max` follow the scalar rules for NaN and signed zeros, and an
        // integer `abs` checks for the most negative value: lane by lane.
        if (op != Builtin::LaneAbs && lane.is_float())
            || (op == Builtin::LaneAbs && lane.is_integer() && self.g.opts.checks)
        {
            let pa = self.addr_of(t, &a);
            let pb = b.as_ref().map(|b| self.addr_of(t, b));
            let loc = self.loc(e);
            return self.lane_loop(t, n, |cx, i| {
                let x = cx.lane_at(lane, bytes, &pa, i);
                Ok(match &pb {
                    Some(pb) => {
                        let y = cx.lane_at(lane, bytes, pb, i);
                        let f = if op == Builtin::LaneMin { "gm_fmin" } else { "gm_fmax" };
                        cx.float_from(lane, &format!("{f}((double){x}, (double){y})"))
                    }
                    None => {
                        let ii = cx.int_info(lane).unwrap();
                        let min = cx.int_lit(lane, &ii.min());
                        cx.line(&format!("if ({x} == {min}) gm_trap(\"integer overflow in `abs`\", {loc});"));
                        format!("({x} < 0 ? -{x} : {x})")
                    }
                })
            });
        }
        let va = self.load_vec(t, &a);
        let u = self.vty(n, lane, Some(false));
        let s = self.vty(n, lane, None);
        let r = match op {
            Builtin::LaneAbs if lane.is_float() => {
                let mask = if bytes == 4 { "0x7fffffffu".to_string() } else { "0x7fffffffffffffffull".to_string() };
                format!("({s})(({u}){va} & {mask})")
            }
            Builtin::LaneAbs => {
                let sh = format!("h{}", self.fresh());
                self.line(&format!("{s} {sh} = {va} >> {};", bytes * 8 - 1));
                format!("({s})((({u})({va} ^ {sh})) - ({u}){sh})")
            }
            _ => {
                let vb = self.load_vec(t, b.as_deref().unwrap());
                let sym = if op == Builtin::LaneMin { "<" } else { ">" };
                let w = self.vty(n, lane, Some(true));
                let c = format!("c{}", self.fresh());
                self.line(&format!("{u} {c} = ({u})({w})({va} {sym} {vb});"));
                format!("({s})((({u}){va} & {c}) | (({u}){vb} & ~{c}))")
            }
        };
        Ok(self.store_vec(t, &r))
    }

    fn vec_convert(&mut self, e: &'a Expr, x: &'a Expr) -> CResult<String> {
        let (n, to, _) = parts(e.ty);
        let v = self.expr(x)?;
        let va = self.load_vec(x.ty, &v);
        let tv = self.vty(n, to, None);
        Ok(self.store_vec(e.ty, &format!("__builtin_convertvector({va}, {tv})")))
    }

    fn vec_shuffle(&mut self, e: &'a Expr, args: &'a [Expr]) -> CResult<String> {
        let t = args[0].ty;
        let (_, lane, bytes) = parts(t);
        let (m, _, _) = parts(e.ty);
        let idx: Vec<String> = args[1..]
            .iter()
            .map(|a| match &a.kind {
                ExprKind::Int(i) => i.to_string(),
                _ => "0".into(),
            })
            .collect();
        let v = self.expr(&args[0])?;
        let va = self.load_vec(t, &v);
        let tv = self.vty(m, lane, None);
        let res = self.tmp_uninit(e.ty);
        let r = format!("v{}", self.fresh());
        self.line("#if defined(__clang__) || __GNUC__ >= 12");
        self.line(&format!("{tv} {r} = __builtin_shufflevector({va}, {va}, {});", idx.join(", ")));
        self.line(&format!("memcpy(&{res}, &{r}, sizeof {res});"));
        self.line("#else");
        for (j, k) in idx.iter().enumerate() {
            self.line(&format!(
                "memcpy((uint8_t *)&{res} + {}, (uint8_t *)&{va} + {k} * {bytes}, {bytes});",
                j as u64 * bytes
            ));
        }
        self.line("#endif");
        Ok(res)
    }

    fn vec_load_store(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr]) -> CResult<String> {
        let (vt, sl) = if op == Builtin::VecLoad { (e.ty, &args[0]) } else { (args[0].ty, &args[1]) };
        let (n, _, bytes) = parts(vt);
        let v = if op == Builtin::VecStore { Some(self.expr(&args[0])?) } else { None };
        let s = self.expr(sl)?;
        let ps = self.addr_of(sl.ty, &s);
        let (p, len) = (self.slice_ptr(&ps), self.slice_len(&ps));
        if self.g.opts.checks {
            let loc = self.loc(e);
            let what = if op == Builtin::VecLoad { "load" } else { "store" };
            self.line(&format!(
                "if ((uintptr_t)({len}) < {n}) {{ char m[96]; snprintf(m, sizeof m, \"vector {what} of {n} lanes with a slice of %llu elements\", (unsigned long long)({len})); gm_trap(m, {loc}); }}"
            ));
        }
        let size = n * bytes;
        match v {
            None => {
                let res = self.tmp_uninit(vt);
                self.line(&format!("memcpy(&{res}, {p}, {size});"));
                Ok(res)
            }
            Some(v) => {
                let pv = self.addr_of(vt, &v);
                self.line(&format!("memcpy({p}, {pv}, {size});"));
                Ok(self.unit())
            }
        }
    }
}
