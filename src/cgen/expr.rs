//! Expressions, places, coercions, calls, effects and pattern matching.
//!
//! [`FnGen::expr`] emits the statements an expression needs and returns a C
//! expression for its value (usually a temporary). Operands are evaluated into
//! temporaries in order, so evaluation order is the program's.

use super::func::{FnGen, Spec, SpecKey};
use super::types::Repr;
use super::{CResult, conv_attr, unsupported};
use crate::tir::{
    AdtKind, Arm, Callee, Coercion, CondPart, Expr, ExprKind, FnId, FnKind, HandlerValue, LabelId, Lift, Pat, PatKind,
    Target, WriteSink,
};
use crate::types::{CallConv, EffectRef, FloatTy, Ty, TyKind};
use num_bigint::BigInt;

impl<'g, 'a> FnGen<'g, 'a> {
    fn ptr_bytes(&self) -> u64 {
        self.g.ty.lay.target.ptr_bytes
    }

    /// An addressable copy of `v` (of type `t`); returns its `uint8_t *` address.
    pub fn addr_of(&mut self, t: Ty, v: &str) -> String {
        let n = self.tmp(t, v);
        format!("((uint8_t *)&{})", n)
    }

    /// Loads a value of type `t` from address `a`.
    pub fn load(&mut self, t: Ty, a: &str) -> String {
        let c = self.g.ty.cty(t);
        self.tmp(t, &format!("GM_LD({}, {})", c, a))
    }

    /// A zero-filled blob temporary of type `t`.
    fn blob(&mut self, t: Ty) -> String {
        let n = self.tmp_uninit(t);
        self.line(&format!("memset(&{}, 0, sizeof {});", n, n));
        n
    }

    fn store(&mut self, t: Ty, addr: &str, v: &str) {
        let c = self.g.ty.cty(t);
        self.line(&format!("GM_ST({}, {}, {});", c, addr, v));
    }

    pub fn loc(&self, e: &Expr) -> String {
        self.g.loc(e.span)
    }

    // --- optionals, results, unions ---------------------------------------------------------------

    /// A C condition: the optional at `addr` (of type `t`) is present.
    pub fn opt_present(&mut self, t: Ty, addr: &str) -> String {
        if self.g.ty.repr(t) == Repr::Ptr {
            format!("(GM_LD(void *, {}) != 0)", addr)
        } else {
            let off = self.g.ty.offsets(t)[1];
            format!("(({})[{}] != 0)", addr, off)
        }
    }

    pub fn make_some(&mut self, t: Ty, payload: &str) -> String {
        if self.g.ty.repr(t) == Repr::Ptr {
            return payload.to_string();
        }
        let TyKind::Optional(inner) = t.kind() else { unreachable!() };
        let b = self.blob(t);
        let off = self.g.ty.offsets(t)[1];
        self.store(*inner, &format!("((uint8_t *)&{})", b), payload);
        self.line(&format!("((uint8_t *)&{})[{}] = 1;", b, off));
        b
    }

    pub fn make_none(&mut self, t: Ty) -> String {
        if self.g.ty.repr(t) == Repr::Ptr {
            return "((void *)0)".into();
        }
        self.blob(t)
    }

    /// The tag byte of the result at `addr` is set: it holds an error.
    pub fn res_is_err(&mut self, t: Ty, addr: &str) -> String {
        let off = self.g.ty.offsets(t)[0];
        format!("(({})[{}] != 0)", addr, off)
    }

    pub fn make_res(&mut self, t: Ty, is_err: bool, payload_ty: Ty, payload: &str) -> String {
        let b = self.blob(t);
        let offs = self.g.ty.offsets(t);
        self.line(&format!("((uint8_t *)&{})[{}] = {};", b, offs[0], is_err as u8));
        self.store(payload_ty, &format!("((uint8_t *)&{} + {})", b, offs[1]), payload);
        b
    }

    /// A union value of variant `v` with an optional payload.
    pub fn make_variant(&mut self, t: Ty, v: usize, payload: Option<&str>) -> String {
        let def = self.adt_def(t);
        let AdtKind::Union { tag, variants } = &def.kind else { unreachable!() };
        let (tag, variant) = (*tag, variants[v].clone());
        let b = self.blob(t);
        let offs = self.g.ty.offsets(t);
        let tag_val = self.int_lit(tag, &variant.tag);
        self.store(tag, &format!("((uint8_t *)&{} + {})", b, offs[0]), &tag_val);
        if let (Some(p), Some(pt)) = (payload, variant.payload) {
            self.store(pt, &format!("((uint8_t *)&{} + {})", b, offs[1]), p);
        }
        b
    }

    pub fn adt_def(&self, t: Ty) -> &'a crate::tir::AdtDef {
        let TyKind::Adt(id, _) = t.kind() else { panic!("not a nominal type: {}", t) };
        &self.g.prog.adts[id]
    }

    // --- literals ---------------------------------------------------------------------------------

    /// A C literal of integer type `t` with value `v` (floats accept integer literals too).
    pub fn int_lit(&mut self, t: Ty, v: &BigInt) -> String {
        if let Some(w) = self.wide(t) {
            return self.wide_lit(t, w, v);
        }
        match self.g.ty.repr(t) {
            Repr::Float(f) => float_lit(f, v.to_string().parse::<f64>().unwrap_or(f64::NAN)),
            Repr::Int { signed, storage, .. } => {
                let c = super::types::int_cty(signed, storage);
                let (lo, hi) = split_u128(v);
                if storage == 16 {
                    format!("(({})(((unsigned __int128){}u << 64) | {}u))", c, hi as u64, lo as u64)
                } else if signed {
                    let x: i128 = i128::try_from(v.clone()).unwrap_or(0);
                    if x == i64::MIN as i128 {
                        format!("(({})(-9223372036854775807LL - 1))", c)
                    } else {
                        format!("(({}){}LL)", c, x)
                    }
                } else {
                    format!("(({}){}ULL)", c, lo as u64)
                }
            }
            Repr::Bool => {
                if v == &BigInt::from(0) {
                    "0".into()
                } else {
                    "1".into()
                }
            }
            Repr::Ptr => format!("((void *)(uintptr_t){}ULL)", split_u128(v).0 as u64),
            _ => "0".into(),
        }
    }

    /// A slice value `{ptr, len}` of type `t`.
    pub fn make_slice(&mut self, t: Ty, ptr: &str, len: &str) -> String {
        let b = self.tmp_uninit(t);
        let p = self.ptr_bytes();
        self.line(&format!("GM_ST(void *, (uint8_t *)&{}, (void *)({}));", b, ptr));
        self.line(&format!("GM_ST(uintptr_t, (uint8_t *)&{} + {}, (uintptr_t)({}));", b, p, len));
        b
    }

    pub fn slice_ptr(&mut self, addr: &str) -> String {
        format!("GM_LD(void *, {})", addr)
    }

    pub fn slice_len(&mut self, addr: &str) -> String {
        format!("GM_LD(uintptr_t, {} + {})", addr, self.ptr_bytes())
    }

    // --- expressions ------------------------------------------------------------------------------

    pub fn expr(&mut self, e: &'a Expr) -> CResult<String> {
        let t = e.ty;
        match &e.kind {
            ExprKind::Int(v) => Ok(self.int_lit(t, v)),
            ExprKind::Float(x) => match t.kind() {
                TyKind::Float(f) => Ok(float_lit(*f, *x)),
                _ => unsupported(e.span, "a float literal of a non-float type"),
            },
            ExprKind::Bool(b) => Ok(if *b { "((uint8_t)1)".into() } else { "((uint8_t)0)".into() }),
            ExprKind::Str(s) => {
                let a = self.g.static_bytes(s);
                Ok(self.make_slice(t, &a, &s.len().to_string()))
            }
            ExprKind::CStr(s) => Ok(format!("((void *){})", self.g.static_bytes(s))),
            ExprKind::Null => Ok(self.make_none(t)),
            ExprKind::ConstBytes(b) => {
                let a = self.g.static_bytes(b);
                Ok(self.load(t, &a))
            }
            ExprKind::Uninit => {
                let n = self.tmp_uninit(t);
                if self.g.opts.debug {
                    self.line(&format!("memset(&{}, 0xAA, sizeof {});", n, n));
                }
                Ok(n)
            }
            ExprKind::Const(id) => Ok(self.load(t, &format!("gm_c{}", id.0))),
            // A field of a `#bits` struct is a range of bits of its integer (§5.10.5).
            ExprKind::Field { base, index } if self.g.ty.bits_layout(base.ty).is_some() => {
                let (u, fields) = self.g.ty.bits_layout(base.ty).unwrap();
                let (off, w, fty) = fields[*index as usize];
                let v = self.expr(base)?;
                let a = self.addr_of(base.ty, &v);
                let uc = self.g.ty.cty(u);
                let raw = format!("((((unsigned __int128)GM_LD({uc}, {a})) >> {off}) & {})", mask(w));
                let raw = self.tmp(Ty::int(false, 128), &raw);
                Ok(self.bits_value(fty, &raw))
            }
            ExprKind::Local(_)
            | ExprKind::Upvar { .. }
            | ExprKind::Deref(_)
            | ExprKind::Field { .. }
            | ExprKind::VariantPayload { .. }
            | ExprKind::Index { .. } => {
                let a = self.place_addr(e)?;
                Ok(self.load(t, &a))
            }
            ExprKind::Global(_) => {
                let a = self.place_addr(e)?;
                Ok(self.load(t, &a))
            }
            ExprKind::Slice { base, lo, hi, inclusive } => {
                self.slice_expr(e, base, lo.as_deref(), hi.as_deref(), *inclusive)
            }
            ExprKind::SliceLen(x) => {
                let v = self.expr(x)?;
                let a = self.addr_of(x.ty, &v);
                let l = self.slice_len(&a);
                Ok(self.tmp(t, &l))
            }
            ExprKind::SlicePtr(x) => {
                let v = self.expr(x)?;
                let a = self.addr_of(x.ty, &v);
                let p = self.slice_ptr(&a);
                Ok(self.tmp(t, &p))
            }
            ExprKind::AddrOf(p) => {
                let a = self.place_addr(p)?;
                Ok(self.tmp(t, &format!("(void *){}", a)))
            }
            ExprKind::AddrOfTemp(x) => {
                let v = self.expr(x)?;
                let a = self.addr_of(x.ty, &v);
                Ok(self.tmp(t, &format!("(void *){}", a)))
            }
            ExprKind::FnRef(f) => {
                self.g.queue(*f);
                let name = self.g.addr_name(*f);
                let plain = self.g.is_plain(*f);
                // A plain function where hidden-convention code is expected is marked by
                // setting the low bit of its address (see `call_indirect`).
                let code = if plain && !self.ptr_type_plain(t) {
                    format!("GM_PTR_TAG({})", name)
                } else {
                    format!("((void *){})", name)
                };
                if matches!(t.kind(), TyKind::Fn(_)) {
                    let cx = self.cx.clone();
                    Ok(self.tmp(t, &format!("((gm_closure){{{}, NULL, {}}})", code, cx)))
                } else {
                    Ok(code)
                }
            }
            ExprKind::Closure(f) => {
                self.g.queue(*f);
                let name = self.g.fn_name(*f);
                let cx = self.cx.clone();
                Ok(self.tmp(t, &format!("((gm_closure){{(void *){}, (void *)&F.h, {}}})", name, cx)))
            }
            ExprKind::HandlerRef(_) => {
                let cx = self.cx.clone();
                Ok(self.tmp(t, &format!("((gm_hval){{(void *)&F.h, {}}})", cx)))
            }
            ExprKind::Unary { op, operand } if matches!(operand.ty.kind(), TyKind::Array { .. }) => {
                self.elementwise(e, Err(*op), None, operand)
            }
            ExprKind::Binary { op, lhs, rhs }
                if matches!(lhs.ty.kind(), TyKind::Array { .. }) && !matches!(e.ty.kind(), TyKind::Bool) =>
            {
                self.elementwise(e, Ok(*op), Some(lhs), rhs)
            }
            ExprKind::Unary { op, operand } => self.unary(e, *op, operand),
            ExprKind::Binary { op, lhs, rhs } => self.binary(e, *op, lhs, rhs),
            ExprKind::Logic { is_and, lhs, rhs } => {
                let r = self.tmp(Ty::bool(), "0");
                let l = self.expr(lhs)?;
                if *is_and {
                    self.line(&format!("if ({}) {{", l));
                } else {
                    self.line(&format!("if (!{}) {{", l));
                }
                self.indent += 1;
                let v = self.expr(rhs)?;
                self.line(&format!("{} = {};", r, v));
                self.indent -= 1;
                self.line(&format!("}} else {} = {};", r, if *is_and { 0 } else { 1 }));
                Ok(r)
            }
            ExprKind::Coerce { kind, expr } => self.coerce(e, *kind, expr),
            ExprKind::Builtin { op, args } => self.builtin(e, *op, args),
            ExprKind::Call { callee, args } => self.call(e, callee, args),
            ExprKind::Aggregate(elems) if self.g.ty.bits_layout(t).is_some() => {
                let (u, fields) = self.g.ty.bits_layout(t).unwrap();
                let mut vals = vec![];
                for x in elems {
                    vals.push(self.expr(x)?);
                }
                let acc = self.tmp(Ty::int(false, 128), "0");
                for (x, (v, (off, w, _))) in elems.iter().zip(vals.iter().zip(fields)) {
                    let raw = self.bits_raw(x.ty, v);
                    self.line(&format!("{acc} |= (({raw}) & {}) << {off};", mask(w)));
                }
                let b = self.blob(t);
                let uc = self.g.ty.cty(u);
                self.line(&format!("GM_ST({uc}, (uint8_t *)&{b}, ({uc}){acc});"));
                Ok(b)
            }
            ExprKind::Aggregate(elems) => {
                let mut vals = vec![];
                for x in elems {
                    vals.push(self.expr(x)?);
                }
                let b = self.blob(t);
                let offs = self.g.ty.offsets(t);
                for (i, (x, v)) in elems.iter().zip(&vals).enumerate() {
                    self.store(x.ty, &format!("((uint8_t *)&{} + {})", b, offs[i]), v);
                }
                Ok(b)
            }
            ExprKind::Array(elems) => {
                let mut vals = vec![];
                for x in elems {
                    vals.push(self.expr(x)?);
                }
                let b = self.blob(t);
                for (i, (x, v)) in elems.iter().zip(&vals).enumerate() {
                    let esz = self.g.ty.size(x.ty);
                    self.store(x.ty, &format!("((uint8_t *)&{} + {})", b, i as u64 * esz), v);
                }
                Ok(b)
            }
            ExprKind::ArrayFill(x) => {
                let v = self.expr(x)?;
                let TyKind::Array { len, elem } = t.kind() else { return unsupported(e.span, "this array fill") };
                let b = self.tmp_uninit(t);
                let esz = self.g.ty.size(*elem);
                let c = self.g.ty.cty(*elem);
                self.line(&format!(
                    "for (uint64_t i = 0; i < {}; i++) GM_ST({}, (uint8_t *)&{} + i * {}, {});",
                    len, c, b, esz, v
                ));
                Ok(b)
            }
            ExprKind::Variant { variant, payload } => {
                let p = match payload {
                    Some(p) => Some(self.expr(p)?),
                    None => None,
                };
                Ok(self.make_variant(t, *variant as usize, p.as_deref()))
            }
            ExprKind::MakeErr(x) => {
                let v = self.expr(x)?;
                Ok(self.make_res(t, true, x.ty, &v))
            }
            ExprKind::Unwrap { expr, is_result } => {
                let v = self.expr(expr)?;
                let a = self.addr_of(expr.ty, &v);
                let loc = self.loc(e);
                if *is_result {
                    let bad = self.res_is_err(expr.ty, &a);
                    let TyKind::Result { err, .. } = expr.ty.kind() else { unreachable!() };
                    let off = self.g.ty.offsets(expr.ty)[1];
                    let d = self.g.ty.desc(*err);
                    if self.g.opts.checks {
                        self.line(&format!("if ({}) {{", bad));
                        self.line(&format!(
                            "    gm_buf b = {{0}}; gm_buf_push(&b, \"unwrap of error value \", 22); gm_format(&b, {}, {} + {}, false); gm_trap_bytes(b.data, b.len, {});",
                            d, a, off, loc
                        ));
                        self.line("}");
                    }
                    Ok(self.load(t, &format!("({} + {})", a, off)))
                } else {
                    let present = self.opt_present(expr.ty, &a);
                    if self.g.opts.checks {
                        self.line(&format!("if (!{}) gm_trap(\"unwrap of null\", {});", present, loc));
                    }
                    Ok(self.load(t, &a))
                }
            }
            ExprKind::Eq { lhs, rhs, negated } => {
                let l = self.expr(lhs)?;
                let r = self.expr(rhs)?;
                let c = self.equal(lhs.ty, &l, &r);
                Ok(self.tmp(Ty::bool(), &format!("({}{})", if *negated { "!" } else { "" }, c)))
            }
            ExprKind::In { elem, container, negated } => self.in_expr(elem, container, *negated),
            ExprKind::Block(b) => self.block(b),
            ExprKind::If { cond, then, else_ } => {
                let res = (!t.is_never() && !t.is_unit()).then(|| self.tmp_uninit(t));
                let n = self.fresh();
                let (els, end) = (format!("ie{}", n), format!("ix{}", n));
                self.conds(cond, &els)?;
                let v = self.expr(then)?;
                if let Some(r) = &res
                    && !then.ty.is_never()
                {
                    self.line(&format!("{} = {};", r, v));
                }
                self.line(&format!("goto {};", end));
                self.label(&els);
                if let Some(x) = else_ {
                    let v = self.expr(x)?;
                    if let Some(r) = &res
                        && !x.ty.is_never()
                    {
                        self.line(&format!("{} = {};", r, v));
                    }
                }
                self.label(&end);
                Ok(match res {
                    Some(r) => r,
                    None => self.zero(t),
                })
            }
            ExprKind::Match { scrutinee, arms } => self.match_expr(e, scrutinee, arms),
            ExprKind::Else { lhs, capture, rhs } => {
                let l = self.expr(lhs)?;
                let a = self.addr_of(lhs.ty, &l);
                let res = (!t.is_never()).then(|| self.tmp_uninit(t));
                let (present, payload_ty, payload_off) = match lhs.ty.kind() {
                    TyKind::Optional(p) => (self.opt_present(lhs.ty, &a), *p, 0),
                    TyKind::Result { ok, .. } => {
                        let bad = self.res_is_err(lhs.ty, &a);
                        (format!("!{}", bad), *ok, self.g.ty.offsets(lhs.ty)[1])
                    }
                    _ => return unsupported(e.span, "`else` on this type"),
                };
                self.line(&format!("if ({}) {{", present));
                self.indent += 1;
                if let Some(r) = &res {
                    let v = if t == payload_ty {
                        self.load(payload_ty, &format!("({} + {})", a, payload_off))
                    } else if t == lhs.ty {
                        l.clone()
                    } else if matches!(t.kind(), TyKind::Optional(_)) {
                        let p = self.load(payload_ty, &format!("({} + {})", a, payload_off));
                        self.make_some(t, &p)
                    } else {
                        let p = self.load(payload_ty, &format!("({} + {})", a, payload_off));
                        self.make_res(t, false, payload_ty, &p)
                    };
                    self.line(&format!("{} = {};", r, v));
                }
                self.indent -= 1;
                self.line("} else {");
                self.indent += 1;
                if let (Some(c), TyKind::Result { err, .. }) = (capture, lhs.ty.kind()) {
                    let off = self.g.ty.offsets(lhs.ty)[1];
                    let ev = self.load(*err, &format!("({} + {})", a, off));
                    let lv = self.local_lv(*c);
                    self.line(&format!("{} = {};", lv, ev));
                }
                let v = self.expr(rhs)?;
                if let Some(r) = &res
                    && !rhs.ty.is_never()
                {
                    self.line(&format!("{} = {};", r, v));
                }
                self.indent -= 1;
                self.line("}");
                Ok(match res {
                    Some(r) => r,
                    None => self.zero(t),
                })
            }
            ExprKind::Try { expr, lift, levels } => self.try_expr(e, expr, *lift, *levels),
            ExprKind::Loop { label, cond, body, step } => {
                let n = self.fresh();
                let (brk, cont) = (format!("lb{}", n), format!("lc{}", n));
                self.line("for (;;) {");
                self.indent += 1;
                self.conds(cond, &brk)?;
                let scope = self.loop_scope(*label, brk.clone(), cont.clone());
                self.scopes.push(scope);
                let r = self.block(body);
                let scope = self.scopes.pop().unwrap();
                r?;
                self.label(&cont);
                if let Some(s) = step {
                    self.expr(s)?;
                }
                self.indent -= 1;
                self.line("}");
                // The loop is left only by jumping to `brk`.
                self.end_scope(scope, false);
                self.label(&brk);
                Ok(self.unit())
            }
            ExprKind::Break { target, value } => {
                let v = match value {
                    Some(x) => Some((self.expr(x)?, x.ty)),
                    None => None,
                };
                match target {
                    Target::Label { levels, label } => {
                        if let Some((v, vt)) = &v
                            && self.is_with_label(*levels, *label)
                        {
                            let slot = self.frame_field(*levels, &format!("w{}", label.0));
                            if !vt.is_never() {
                                self.line(&format!("{} = {};", slot, v));
                            }
                        }
                        let (fr, _) = self.frame_up(*levels);
                        self.start_exit("GM_BREAK", &fr, &label.0.to_string(), "0");
                    }
                    Target::InstallingWith => {
                        if let Some((v, vt)) = &v
                            && !vt.is_unit()
                            && !vt.is_never()
                        {
                            self.store(*vt, "inst->result", v);
                        }
                        self.start_exit("GM_BREAK", "inst->with_frame", "inst->with_label", "0");
                    }
                }
                Ok(self.zero(t))
            }
            ExprKind::Continue { target } => {
                match target {
                    Target::Label { levels, label } => {
                        let (fr, _) = self.frame_up(*levels);
                        self.start_exit("GM_CONTINUE", &fr, &label.0.to_string(), "0");
                    }
                    // In a clause, `continue` ends the clause (the next iteration of a `for`).
                    Target::InstallingWith => {
                        let u = self.unit();
                        self.line(&format!("F.ret = {};", u));
                        self.start_exit("GM_RETURN", "(&F.h)", "0", "0");
                    }
                }
                Ok(self.zero(t))
            }
            ExprKind::Return { levels, value } if self.builds_in_place(value) => {
                // A large value is built in the result slot, or straight in the caller's
                // destination when no deferred statement can run after it.
                let ct = self.g.ty.cty(value.ty);
                if *levels == 0 && !self.outlined && self.no_defers() && self.g.ty.by_ref(self.def.ret) {
                    self.expr_into(value, &format!("(*({} *)gm_out)", ct))?;
                    self.line("goto gm_fdone;");
                    return Ok(self.zero(t));
                }
                let slot = self.frame_field(*levels, "ret");
                self.expr_into(value, &slot)?;
                let target_ret = self.target_ret(*levels);
                let is_err = if matches!(target_ret.kind(), TyKind::Result { .. }) {
                    let a = format!("((uint8_t *)&{})", slot);
                    self.res_is_err(target_ret, &a)
                } else {
                    "0".into()
                };
                let is_err = self.tmp(Ty::bool(), &is_err);
                let (fr, _) = self.frame_up(*levels);
                self.start_exit("GM_RETURN", &fr, "0", &is_err);
                Ok(self.zero(t))
            }
            // A large place is copied straight to the caller's destination.
            ExprKind::Return { levels: 0, value }
                if self.g.ty.by_ref(value.ty)
                    && self.g.ty.by_ref(self.def.ret)
                    && !self.outlined
                    && self.no_defers()
                    && matches!(value.kind, ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Deref(_)) =>
            {
                let addr = self.place_addr(value)?;
                self.line(&format!("memmove(gm_out, {}, {});", addr, self.g.ty.size(value.ty)));
                self.line("goto gm_fdone;");
                Ok(self.zero(t))
            }
            ExprKind::Return { levels, value } => {
                let target_ret = self.target_ret(*levels);
                // A large value read from a place is copied into the slot directly.
                let v = if self.g.ty.by_ref(value.ty)
                    && !matches!(target_ret.kind(), TyKind::Result { .. })
                    && matches!(value.kind, ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Deref(_))
                {
                    let addr = self.place_addr(value)?;
                    format!("(*({} *){})", self.g.ty.cty(value.ty), addr)
                } else {
                    self.expr(value)?
                };
                let is_err = if matches!(target_ret.kind(), TyKind::Result { .. }) && !value.ty.is_never() {
                    let a = self.addr_of(value.ty, &v);
                    self.res_is_err(target_ret, &a)
                } else {
                    "0".into()
                };
                let is_err = self.tmp(Ty::bool(), &is_err);
                if !value.ty.is_never() {
                    let slot = self.frame_field(*levels, "ret");
                    self.line(&format!("{} = {};", slot, v));
                }
                let (fr, _) = self.frame_up(*levels);
                self.start_exit("GM_RETURN", &fr, "0", &is_err);
                Ok(self.zero(t))
            }
            ExprKind::With { label, handlers, on, body } => self.with_expr(e, *label, handlers, on.as_deref(), body),
            ExprKind::Resume { cont, value } => self.resume(e, cont, value),
            ExprKind::Discard(k) => self.discard(e, k),
            ExprKind::Trap { message } => {
                let m = self.expr(message)?;
                let a = self.addr_of(message.ty, &m);
                let (p, l) = (self.slice_ptr(&a), self.slice_len(&a));
                let loc = self.loc(e);
                self.line(&format!("gm_trap_bytes({}, {}, {});", p, l, loc));
                Ok(self.zero(t))
            }
            ExprKind::WriteDeferred { .. } => {
                unsupported(e.span, "a template in a generic function that was not instantiated")
            }
            ExprKind::Write { sink, pieces, specs, args } => {
                let sink_v = match sink {
                    WriteSink::Fn(x) => Some((self.expr(x)?, x.ty)),
                    WriteSink::Perform { .. } => None,
                };
                let mut vals = vec![];
                for x in args {
                    let v = self.expr(x)?;
                    vals.push(self.addr_of(x.ty, &v));
                }
                let string = Ty::string();
                for i in 0..pieces.len() {
                    if !pieces[i].is_empty() {
                        let s = self.g.static_bytes(&pieces[i]);
                        let sl = self.make_slice(string, &s, &pieces[i].len().to_string());
                        self.emit_to(sink, &sink_v, &sl, e)?;
                    }
                    if i < args.len() {
                        let d = self.g.ty.desc(args[i].ty);
                        let b = format!("b{}", self.fresh());
                        self.line(&format!("gm_buf {} = {{0}};", b));
                        match specs.get(i) {
                            Some(s) if !s.is_default() => {
                                let c = spec_literal(s);
                                self.line(&format!("gm_format_spec(&{}, {}, {}, &{});", b, d, vals[i], c));
                            }
                            _ => self.line(&format!("gm_format(&{}, {}, {}, true);", b, d, vals[i])),
                        }
                        let sl = self.make_slice(string, &format!("{}.data", b), &format!("{}.len", b));
                        self.emit_to(sink, &sink_v, &sl, e)?;
                        self.line(&format!("gm_buf_free(&{});", b));
                    }
                }
                Ok(self.unit())
            }
            ExprKind::SizeOf { .. }
            | ExprKind::TypeIs { .. }
            | ExprKind::IntLimit { .. }
            | ExprKind::UntypedFloat(_)
            | ExprKind::ValueParam(_) => unsupported(e.span, "an expression left over from generic checking"),
        }
    }

    /// Sends a `[]const u8` value to an `@write` sink.
    fn emit_to(&mut self, sink: &WriteSink, sv: &Option<(String, Ty)>, slice: &str, e: &Expr) -> CResult<()> {
        let unit = Ty::unit();
        match sink {
            WriteSink::Fn(_) => {
                let (cv, cty) = sv.clone().unwrap();
                self.call_closure(&cv, cty, &[slice.to_string()], &[Ty::string()], unit)?;
            }
            WriteSink::Perform { effect, op } => {
                self.perform(e, effect, *op, &[slice.to_string()], &[Ty::string()], unit)?;
            }
        }
        Ok(())
    }

    fn is_with_label(&self, levels: u32, label: LabelId) -> bool {
        let (_, f) = self.frame_up(levels);
        let mut found = false;
        if let Some(b) = &self.g.prog.fns[f.index()].body {
            find_with(b, label, &mut found);
        }
        found
    }

    fn target_ret(&self, levels: u32) -> Ty {
        let (_, f) = self.frame_up(levels);
        self.g.prog.fns[f.index()].ret
    }

    // --- places ---------------------------------------------------------------------------------

    /// The address (`uint8_t *`) of a place, or of a temporary holding a value.
    pub fn place_addr(&mut self, e: &'a Expr) -> CResult<String> {
        match &e.kind {
            ExprKind::Local(l) => Ok(self.local_addr(*l)),
            ExprKind::Global(id) => {
                let gl = &self.g.prog.globals[id.index()];
                if gl.thread_local && gl.link_name.is_none() {
                    Ok(format!("gm_ga{}()", id.0))
                } else {
                    Ok(format!("((uint8_t *)gm_g{})", id.0))
                }
            }
            // Constants live in static storage, so their address is stable.
            ExprKind::Const(id) => Ok(format!("((uint8_t *)gm_c{})", id.0)),
            ExprKind::Upvar { levels, local } => {
                let f = self.frame_field(*levels, &format!("l{}", local.0));
                Ok(format!("((uint8_t *)&{})", f))
            }
            ExprKind::Deref(p) => {
                let v = self.expr(p)?;
                Ok(format!("((uint8_t *){})", v))
            }
            ExprKind::Field { base, index } => {
                if matches!(base.ty.kind(), TyKind::Adt(..))
                    && let AdtKind::Struct { bits: Some(_), .. } = &self.adt_def(base.ty).kind
                {
                    return unsupported(e.span, "fields of #bits structs");
                }
                let b = self.base_addr(base)?;
                let off = self.g.ty.offsets(base.ty)[*index as usize];
                Ok(format!("({} + {})", b, off))
            }
            ExprKind::VariantPayload { base, .. } => {
                let b = self.base_addr(base)?;
                let off = self.g.ty.offsets(base.ty)[1];
                Ok(format!("({} + {})", b, off))
            }
            ExprKind::Index { base, index, checked } => {
                let i = self.expr(index)?;
                let i = self.usize_value(index.ty, &i);
                let i = self.tmp(Ty::usize(), &i);
                let loc = self.loc(e);
                let check = |fg: &mut Self, len: &str| {
                    if *checked {
                        fg.line(&format!(
                            "if ({} >= (uintptr_t)({})) {{ char m[96]; snprintf(m, sizeof m, \"index %llu out of bounds for length %llu\", (unsigned long long){}, (unsigned long long)({})); gm_trap(m, {}); }}",
                            i, len, i, len, loc
                        ));
                    }
                };
                match base.ty.kind() {
                    TyKind::Array { len, elem } => {
                        let (len, esz) = (*len, self.g.ty.size(*elem));
                        let b = self.base_addr(base)?;
                        check(self, &len.to_string());
                        Ok(format!("({} + {} * {})", b, i, esz))
                    }
                    TyKind::Slice { elem, .. } => {
                        let esz = self.g.ty.size(*elem);
                        let v = self.expr(base)?;
                        let a = self.addr_of(base.ty, &v);
                        let len = self.slice_len(&a);
                        let len = self.tmp(Ty::usize(), &len);
                        check(self, &len);
                        let p = self.slice_ptr(&a);
                        Ok(format!("((uint8_t *){} + {} * {})", p, i, esz))
                    }
                    TyKind::ManyPtr { elem, .. } => {
                        let esz = self.g.ty.size(*elem);
                        let v = self.expr(base)?;
                        Ok(format!("((uint8_t *){} + {} * {})", v, i, esz))
                    }
                    _ => unsupported(e.span, format!("indexing `{}`", base.ty)),
                }
            }
            _ => {
                let v = self.expr(e)?;
                Ok(self.addr_of(e.ty, &v))
            }
        }
    }

    /// The address of an aggregate operand: the place itself, or a temporary copy.
    fn base_addr(&mut self, base: &'a Expr) -> CResult<String> {
        self.place_addr(base)
    }

    fn slice_expr(
        &mut self,
        e: &'a Expr,
        base: &'a Expr,
        lo: Option<&'a Expr>,
        hi: Option<&'a Expr>,
        inclusive: bool,
    ) -> CResult<String> {
        let (ptr, len, esz) = match base.ty.kind() {
            TyKind::Array { len, elem } => {
                let a = self.place_addr(base)?;
                (a, Some(len.to_string()), self.g.ty.size(*elem))
            }
            TyKind::Slice { elem, .. } => {
                let v = self.expr(base)?;
                let a = self.addr_of(base.ty, &v);
                let p = self.slice_ptr(&a);
                let l = self.slice_len(&a);
                let l = self.tmp(Ty::usize(), &l);
                (format!("((uint8_t *){})", p), Some(l), self.g.ty.size(*elem))
            }
            TyKind::ManyPtr { elem, .. } => {
                let v = self.expr(base)?;
                (format!("((uint8_t *){})", v), None, self.g.ty.size(*elem))
            }
            _ => return unsupported(e.span, format!("slicing `{}`", base.ty)),
        };
        let ptr = self.tmp(Ty::ptr(false, Ty::u8()), &ptr);
        let lo_v = match lo {
            Some(x) => {
                let v = self.expr(x)?;
                let v = self.usize_value(x.ty, &v);
                self.tmp(Ty::usize(), &v)
            }
            None => "(uintptr_t)0".into(),
        };
        let hi_v = match hi {
            Some(x) => {
                let v = self.expr(x)?;
                let v = self.usize_value(x.ty, &v);
                let h = self.tmp(Ty::usize(), &v);
                if inclusive {
                    self.line(&format!("{} += 1;", h));
                }
                h
            }
            None => match &len {
                Some(l) => format!("(uintptr_t){}", l),
                None => return unsupported(e.span, "slicing a many-pointer without an upper bound"),
            },
        };
        if let Some(l) = &len {
            let loc = self.loc(e);
            self.line(&format!(
                "if ({lo} > {hi} || {hi} > (uintptr_t){l}) {{ char m[128]; snprintf(m, sizeof m, \"slice bounds %llu..%llu invalid for length %llu\", (unsigned long long){lo}, (unsigned long long){hi}, (unsigned long long){l}); gm_trap(m, {loc}); }}",
                lo = lo_v,
                hi = hi_v,
                l = l,
                loc = loc
            ));
        }
        Ok(self.make_slice(
            e.ty,
            &format!("((uint8_t *){} + {} * {})", ptr, lo_v, esz),
            &format!("{} - {}", hi_v, lo_v),
        ))
    }

    // --- coercions ------------------------------------------------------------------------------

    fn coerce(&mut self, e: &'a Expr, kind: Coercion, x: &'a Expr) -> CResult<String> {
        let v = self.expr(x)?;
        let t = e.ty;
        Ok(match kind {
            Coercion::IntWiden if self.wide(t).is_some() || self.wide(x.ty).is_some() => {
                self.wide_convert(e, crate::tir::Builtin::Convert, x.ty, &v, t)?
            }
            Coercion::IntWiden | Coercion::FloatWiden => {
                let c = self.g.ty.cty(t);
                self.tmp(t, &format!("({}){}", c, v))
            }
            Coercion::WrapOptional => self.make_some(t, &v),
            Coercion::WrapOk => self.make_res(t, false, x.ty, &v),
            Coercion::LiftErr { variant } => {
                let a = self.addr_of(x.ty, &v);
                let (TyKind::Result { err: e1, ok }, TyKind::Result { err: e2, .. }) = (x.ty.kind(), t.kind()) else {
                    return unsupported(e.span, "this error lifting");
                };
                let (e1, ok, e2) = (*e1, *ok, *e2);
                let res = self.tmp_uninit(t);
                let bad = self.res_is_err(x.ty, &a);
                let off = self.g.ty.offsets(x.ty)[1];
                self.line(&format!("if ({}) {{", bad));
                self.indent += 1;
                let ev = self.load(e1, &format!("({} + {})", a, off));
                let wrapped = self.make_variant(e2, variant as usize, Some(&ev));
                let r = self.make_res(t, true, e2, &wrapped);
                self.line(&format!("{} = {};", res, r));
                self.indent -= 1;
                self.line("} else {");
                self.indent += 1;
                let ov = self.load(ok, &format!("({} + {})", a, off));
                let r = self.make_res(t, false, ok, &ov);
                self.line(&format!("{} = {};", res, r));
                self.indent -= 1;
                self.line("}");
                res
            }
            Coercion::FromNever => self.zero(t),
            // Widening a plain function pointer to an effectful type marks it (see `call_indirect`).
            Coercion::FnPtrWiden if self.ptr_type_plain(x.ty) && !self.ptr_type_plain(t) => {
                self.tmp(t, &format!("GM_PTR_TAG({})", v))
            }
            Coercion::PtrToPtr | Coercion::FnPtrWiden | Coercion::TupleRename | Coercion::InOptional => {
                if self.g.ty.repr(t) == self.g.ty.repr(x.ty) && self.g.ty.repr(t) != Repr::Blob {
                    v
                } else {
                    let a = self.addr_of(x.ty, &v);
                    self.load(t, &a)
                }
            }
            Coercion::ArrayPtrToSlice => {
                let TyKind::Ptr { pointee, .. } = x.ty.kind() else {
                    return unsupported(e.span, "this array-to-slice coercion");
                };
                let TyKind::Array { len, .. } = pointee.kind() else {
                    return unsupported(e.span, "this array-to-slice coercion");
                };
                self.make_slice(t, &v, &len.to_string())
            }
            Coercion::FnToPtr => match (self.g.ty.repr(x.ty), self.g.ty.repr(t)) {
                (Repr::Ptr, Repr::Closure) => {
                    let cx = self.cx.clone();
                    let code = if self.ptr_type_plain(x.ty) { format!("GM_PTR_TAG({})", v) } else { v };
                    self.tmp(t, &format!("((gm_closure){{{}, NULL, {}}})", code, cx))
                }
                _ => v,
            },
            Coercion::Unresolved => return unsupported(e.span, "an unresolved coercion"),
        })
    }

    // --- calls and effects -----------------------------------------------------------------------

    fn call(&mut self, e: &'a Expr, callee: &'a Callee, args: &'a [Expr]) -> CResult<String> {
        let dest = match self.dest_call.take() {
            Some((c, d)) if std::ptr::eq(c, e) => Some(d),
            other => {
                self.dest_call = other;
                None
            }
        };
        let r = self.call_inner(e, callee, args, dest);
        self.call_dest = None;
        r
    }

    fn call_inner(
        &mut self,
        e: &'a Expr,
        callee: &'a Callee,
        args: &'a [Expr],
        dest: Option<String>,
    ) -> CResult<String> {
        // The callee is evaluated before the arguments (§7.1).
        let cv = match callee {
            Callee::Indirect(c) => Some(self.expr(c)?),
            _ => None,
        };
        let mut vals = vec![];
        let mut tys = vec![];
        for (i, a) in args.iter().enumerate() {
            // A large argument read from a place is passed as the place itself when
            // nothing evaluated before the call can change it (see `Types::by_ref`).
            let later_pure = || args[i + 1..].iter().all(no_writes);
            // A large constant is passed from static storage (see `constarg`).
            let k = if self.g.ty.by_ref(a.ty) { self.g.const_arg(a) } else { None };
            let v = if let Some(k) = k {
                format!("(*({} *){})", self.g.ty.cty(a.ty), k)
            } else if self.g.ty.by_ref(a.ty)
                && matches!(a.kind, ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Deref(_))
                && later_pure()
            {
                let addr = self.place_addr(a)?;
                format!("(*({} *){})", self.g.ty.cty(a.ty), addr)
            } else {
                self.expr(a)?
            };
            vals.push(v);
            tys.push(a.ty);
        }
        self.call_site(e.span);
        // Set only now: the arguments' own calls have their own destinations.
        self.call_dest = dest;
        match callee {
            Callee::Fn(f) => self.call_fn(e, *f, &vals),
            Callee::Indirect(c) => {
                let cv = cv.unwrap();
                match c.ty.kind() {
                    TyKind::Fn(_) => self.call_closure(&cv, c.ty, &vals, &tys, e.ty),
                    TyKind::FnPtr(sig) => {
                        let effectful = !self.g.within_sys(&sig.effects);
                        let cx = self.cx.clone();
                        let r = self.call_indirect(&cv, "NULL", &cx, &vals, &tys, e.ty, effectful, sig.callconv);
                        if effectful {
                            self.check_unwind();
                        }
                        Ok(r)
                    }
                    _ => unsupported(e.span, format!("calling a value of type `{}`", c.ty)),
                }
            }
            Callee::Perform { effect, op } => self.perform(e, effect, *op, &vals, &tys, e.ty),
        }
    }

    /// In Debug builds, records `span` as the location of the call about to be made, for
    /// backtraces.
    pub fn call_site(&mut self, span: crate::source::Span) {
        if self.g.opts.debug && span != crate::source::Span::DUMMY {
            let f = self.g.sources.file(span.file);
            let (line, col) = f.line_col(span.lo);
            let file = super::c_string(f.path.display().to_string().as_bytes());
            self.line(&format!("B.file = {file}; B.line = {line}; B.col = {col};"));
        }
    }

    /// The C function pointer type of a Gamma function returning `ret`.
    pub fn fn_ptr_type(&mut self, ret: Ty, params: &[Ty]) -> String {
        let (r, mut ps) = if self.g.ty.by_ref(ret) {
            ("void".to_string(), vec!["void *".to_string()])
        } else {
            (self.g.ty.cty(ret), vec![])
        };
        ps.extend(["void *".to_string(), "gm_node *".into(), "gm_node *".into()]);
        for p in params {
            ps.push(self.g.ty.param_cty(*p));
        }
        format!("{} (*)({})", r, ps.join(", "))
    }

    /// The value `v` of type `t` as the plain C convention passes it (its mirror).
    pub fn plain_value(&mut self, t: Ty, v: &str) -> String {
        match self.g.ty.mirror(t) {
            Some(m) => format!("GM_CONV({}, {})", m, v),
            None => v.to_string(),
        }
    }

    /// A value of type `t` received in the plain C convention, as a Gamma value.
    pub fn gamma_value(&mut self, t: Ty, v: &str) -> String {
        match self.g.ty.mirror(t) {
            Some(_) => format!("GM_CONV({}, {})", self.g.ty.cty(t), v),
            None => v.to_string(),
        }
    }

    /// The arguments `vals` of types `tys` in the plain C convention.
    fn plain_args(&mut self, vals: &[String], tys: &[Ty]) -> Vec<String> {
        vals.iter().zip(tys).map(|(v, t)| self.plain_value(*t, v)).collect()
    }

    /// Argument `v` of type `t` as Gamma functions take it: the address of a temporary
    /// holding it if the type is passed by address (`Types::by_ref`).
    pub fn gamma_arg(&mut self, t: Ty, v: &str) -> String {
        if !self.g.ty.by_ref(t) {
            return v.to_string();
        }
        // Temporaries and the places `call` passes are lvalues already.
        let is_temp = v.len() > 1 && v.starts_with('t') && v[1..].bytes().all(|b| b.is_ascii_digit());
        let n = if is_temp || v.starts_with("(*(") { v.to_string() } else { self.tmp(t, v) };
        format!("&{}", n)
    }

    /// The arguments `vals` of types `tys` of a call of a Gamma function, each preceded
    /// by a comma.
    pub fn gamma_args(&mut self, vals: &[String], tys: &[Ty]) -> String {
        vals.iter().zip(tys).map(|(v, t)| format!(", {}", self.gamma_arg(*t, v))).collect()
    }

    /// Emits a call whose value has type `ret`.
    fn call_result(&mut self, ret: Ty, call: &str) -> String {
        if ret.is_never() {
            self.line(&format!("{};", call));
            self.zero(ret)
        } else {
            self.tmp(ret, call)
        }
    }

    /// The variable a call writes its large result to (`Types::by_ref`): the call's
    /// destination if it has one (see `expr_into`), else a new temporary.
    pub fn result_var(&mut self, ret: Ty) -> String {
        match self.call_dest.take() {
            Some(d) => d,
            None => self.tmp_uninit(ret),
        }
    }

    /// Emits a call of function `callee` with arguments `args` (each preceded by a
    /// comma) whose large result it writes through its first parameter, `gm_out`.
    fn out_call(&mut self, ret: Ty, callee: &str, args: &str) -> String {
        let r = self.result_var(ret);
        self.line(&format!("{}((void *)&{}{});", callee, r, args));
        r
    }

    fn call_fn(&mut self, e: &Expr, f: FnId, vals: &[String]) -> CResult<String> {
        self.g.queue(f);
        let def = &self.g.prog.fns[f.index()];
        // A function with a parent captures it (a literal called directly).
        let env = match def.parent {
            Some(p) => match (0..64).find(|l| self.frame_up(*l).1 == p) {
                Some(l) => self.frame_up(l).0,
                None => "NULL".into(),
            },
            None => "NULL".into(),
        };
        let name = match self.spec_call {
            Some((call, s)) if std::ptr::eq(call, e) => {
                self.spec_call = None;
                self.g.queue_spec(f, s)
            }
            _ => match self.passed_spec(f) {
                Some(s) => self.g.queue_spec(f, s),
                None => self.g.callee_name(f),
            },
        };
        let r = if self.g.is_plain(f) {
            // C's `_Bool` crosses the boundary as 0 or 1 either way.
            let tys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
            let foreign = matches!(def.kind, FnKind::Foreign { .. });
            let n = tys.len().min(vals.len());
            let mut args = vec![];
            for (v, t) in vals[..n].iter().zip(&tys) {
                // Gamma's plain functions take large values by address (see `gen_body`).
                let a = if !foreign && self.g.ty.by_ref(*t) { self.gamma_arg(*t, v) } else { self.plain_value(*t, v) };
                args.push(a);
            }
            args.extend(vals[n..].iter().cloned()); // variadic
            let call = format!("{}({})", name, args.join(", "));
            if !foreign && self.g.ty.by_ref(def.ret) {
                let rest: String = args.iter().map(|a| format!(", {}", a)).collect();
                self.out_call(def.ret, &name, &rest)
            } else if def.ret.is_unit() || def.ret.is_never() {
                self.line(&format!("{};", call));
                self.zero(e.ty)
            } else {
                let call = if !foreign && self.g.ty.by_ref(def.ret) { call } else { self.gamma_value(def.ret, &call) };
                self.call_result(e.ty, &call)
            }
        } else {
            let cx = self.cx.clone();
            let tys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
            let args = self.gamma_args(vals, &tys);
            if self.g.ty.by_ref(def.ret) {
                self.out_call(def.ret, &name, &format!(", (void *){}, {}, NULL{}", env, cx, args))
            } else {
                let call = format!("{}((void *){}, {}, NULL{})", name, env, cx, args);
                self.call_result(e.ty, &call)
            }
        };
        if self.g.may_unwind(f) {
            self.check_unwind();
        }
        Ok(r)
    }

    /// Calls a second-class function value: its context plus the caller's handlers for
    /// the effects in its type (§11.6).
    pub fn call_closure(&mut self, cv: &str, cty: Ty, vals: &[String], tys: &[Ty], ret: Ty) -> CResult<String> {
        let TyKind::Fn(sig) = cty.kind() else { unreachable!() };
        let c = self.tmp(cty, cv);
        let ccx = format!("ccx{}", self.fresh());
        self.line(&format!("gm_node *{} = {}.cx;", ccx, c));
        for eff in sig.effects.effects.iter() {
            let id = self.g.effect_id(eff);
            let n = format!("cn{}", self.fresh());
            let cx = self.cx.clone();
            self.line(&format!(
                "gm_node {n}; {{ gm_node *f = gm_find({cx}, {id}); if (f) {{ {n} = *f; {n}.next = {ccx}; {ccx} = &{n}; }} }}"
            ));
        }
        let r =
            self.call_indirect(&format!("{}.code", c), &format!("{}.env", c), &ccx, vals, tys, ret, true, CallConv::C);
        self.check_unwind();
        Ok(r)
    }

    /// True if values of function pointer type `t` use the plain C convention: their
    /// effects are at most `{Sys}`.
    pub fn ptr_type_plain(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::FnPtr(sig) => self.g.within_sys(&sig.effects),
            TyKind::Optional(inner) => self.ptr_type_plain(*inner),
            _ => false,
        }
    }

    /// Calls code pointer `code`. With `hidden`, the code uses the hidden convention
    /// (env, cx, inst) unless its low bit marks a plain function; otherwise it is plain.
    #[allow(clippy::too_many_arguments)]
    pub fn call_indirect(
        &mut self,
        code: &str,
        env: &str,
        cx: &str,
        vals: &[String],
        tys: &[Ty],
        ret: Ty,
        hidden: bool,
        cc: CallConv,
    ) -> String {
        let plain_ret = if ret.is_unit() || ret.is_never() { "void".to_string() } else { self.g.ty.plain_cty(ret) };
        let mut ps = vec![];
        for p in tys {
            ps.push(self.g.ty.plain_cty(*p));
        }
        let plain_ty = format!(
            "{} ({}*)({})",
            plain_ret,
            conv_attr(cc),
            if ps.is_empty() { "void".into() } else { ps.join(", ") }
        );
        let p = self.tmp(Ty::intern(TyKind::Rawptr), code);
        let res = (!ret.is_never()).then(|| self.result_var(ret));
        let plain_args = self.plain_args(vals, tys);
        let plain_call = format!("(({})GM_PTR_UNTAG({}))({})", plain_ty, p, plain_args.join(", "));
        let plain_call = self.gamma_value(ret, &plain_call);
        let emit_plain = |fg: &mut Self| match &res {
            Some(r) if !ret.is_unit() => fg.line(&format!("{} = {};", r, plain_call)),
            _ => fg.line(&format!("{};", plain_call)),
        };
        if hidden {
            let fp = self.fn_ptr_type(ret, tys);
            let args = self.gamma_args(vals, tys);
            let out = match &res {
                Some(r) if self.g.ty.by_ref(ret) => format!("(void *)&{}, ", r),
                _ => String::new(),
            };
            let hidden_call = format!("(({}){})({}{}, {}, NULL{})", fp, p, out, env, cx, args);
            self.line(&format!("if ((uintptr_t){} & 1) {{", p));
            self.indent += 1;
            emit_plain(self);
            self.indent -= 1;
            self.line("} else {");
            self.indent += 1;
            match &res {
                Some(r) if out.is_empty() => self.line(&format!("{} = {};", r, hidden_call)),
                _ => self.line(&format!("{};", hidden_call)),
            }
            self.indent -= 1;
            self.line("}");
        } else {
            emit_plain(self);
        }
        match res {
            Some(r) if !ret.is_unit() => r,
            _ => self.zero(ret),
        }
    }

    /// Performs operation `op` of `effect` with argument values `vals`.
    pub fn perform(
        &mut self,
        e: &Expr,
        effect: &EffectRef,
        op: u32,
        vals: &[String],
        tys: &[Ty],
        ret: Ty,
    ) -> CResult<String> {
        let id = self.g.effect_id(effect);
        if let Some(r) = self.direct_perform(id, effect, op, vals, tys, ret) {
            return Ok(r);
        }
        let n = format!("pn{}", self.fresh());
        let cx = self.cx.clone();
        let loc = self.loc(e);
        let name = effect.effect.name();
        self.line(&format!("gm_node *{} = gm_find({}, {});", n, cx, id));
        self.line(&format!("if (!{}) gm_trap(\"no handler installed for effect `{}`\", {});", n, name, loc));
        if self.g.prog.ctl_ops.contains(&(effect.effect, op)) {
            return self.ctl_perform(&n, op, vals, tys, ret);
        }
        let res = (!ret.is_never()).then(|| self.result_var(ret));
        let fp = self.fn_ptr_type(ret, tys);
        let args = self.gamma_args(vals, tys);
        let out = match &res {
            Some(r) if self.g.ty.by_ref(ret) => format!("(void *)&{}, ", r),
            _ => String::new(),
        };
        let clause_call = format!("(({}){}->clauses[{}])({}{}->env, {}->hctx, {}{})", fp, n, op, out, n, n, n, args);
        let amb = self.g.prog.ambient.clone();
        let native = [amb.out, amb.err, amb.env].contains(&Some(effect.effect));
        if native {
            self.line(&format!("if ({}->kind == GM_NATIVE) {{", n));
            self.indent += 1;
            self.native_op(effect, op, vals, res.as_deref(), ret)?;
            self.indent -= 1;
            self.line("} else {");
            self.indent += 1;
        }
        match &res {
            Some(r) if out.is_empty() => self.line(&format!("{} = {};", r, clause_call)),
            _ => self.line(&format!("{};", clause_call)),
        }
        if native {
            self.indent -= 1;
            self.line("}");
        }
        self.check_unwind();
        Ok(match res {
            Some(r) => r,
            None => self.zero(ret),
        })
    }

    /// In a specialized clone, a perform of the effect it is specialized for, outside
    /// any `with` of the clone, calls the clause of the handler literal directly: the
    /// handler's node is `cx`. Returns `None` (emitting nothing) otherwise.
    fn direct_perform(
        &mut self,
        id: u32,
        effect: &EffectRef,
        op: u32,
        vals: &[String],
        tys: &[Ty],
        ret: Ty,
    ) -> Option<String> {
        let s = self.spec?;
        // In the body of a `with … on`, moved to a function of its own, `cx` is the
        // fiber's context instead.
        let key = self.g.spec_key(s);
        if key.effect != id || self.cx != "cx" || self.outlined || self.g.prog.ctl_ops.contains(&(effect.effect, op)) {
            return None;
        }
        let clause = *self.g.prog.handlers[key.handler as usize].clauses.get(op as usize)?;
        let def = &self.g.prog.fns[clause.index()];
        let ptys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
        if def.ret != ret || ptys != tys {
            return None;
        }
        let outer = self.g.spec_key(s).outer;
        let name = self.g.clause_name(clause, outer);
        let args = self.gamma_args(vals, tys);
        let r = if self.g.ty.by_ref(ret) {
            self.out_call(ret, &name, &format!(", cx->env, cx->hctx, cx{}", args))
        } else {
            let call = format!("{}(cx->env, cx->hctx, cx{})", name, args);
            self.call_result(ret, &call)
        };
        // A clause that cannot exit needs no test after it: the common loop body.
        if self.g.clause_may_exit(clause, outer) {
            self.check_unwind();
        }
        Some(r)
    }

    /// The specialization a direct call of `f` passes on: in a clone, a call made in
    /// the clone's own context to a function that performs the effect the clone is
    /// specialized for runs that function's clone for the same handler (a recursive
    /// iterator calling itself, or a helper of the loop).
    fn passed_spec(&mut self, f: FnId) -> Option<Spec> {
        let s = self.spec?;
        if self.cx != "cx" || self.outlined {
            return None;
        }
        let def = &self.g.prog.fns[f.index()];
        let handler = self.g.spec_key(s).handler as usize;
        let effect = &self.g.prog.handlers[handler].effect;
        (def.kind == FnKind::Named && def.body.is_some() && def.effects.effects.contains(effect)).then_some(s)
    }

    /// The specialization a `with` installing `handlers` passes to the clauses of its
    /// handler: in a clone, a single handler literal installed in the clone's own
    /// context whose clauses perform the clone's effect has them called as clones for
    /// the clone's handler (Appendix D.3, two levels and more). Nesting is limited, as a
    /// recursive iterator would otherwise nest without end.
    fn nested_spec(&mut self, handlers: &[HandlerValue], on: Option<&Expr>) -> Option<Spec> {
        const MAX_DEPTH: u32 = 3;
        let s = self.spec?;
        let [HandlerValue::Literal(h)] = handlers else { return None };
        let ExprKind::HandlerRef(hid) = h.kind else { return None };
        if on.is_some() || self.cx != "cx" || self.outlined || self.g.spec_depth(s) >= MAX_DEPTH {
            return None;
        }
        let prog = self.g.prog;
        let lit = &prog.handlers[hid.index()];
        if (0..lit.clauses.len()).any(|op| prog.ctl_ops.contains(&(lit.effect.effect, op as u32))) {
            return None;
        }
        let effect = &prog.handlers[self.g.spec_key(s).handler as usize].effect;
        let uses =
            lit.clauses.iter().any(|c| prog.fns[c.index()].body.as_ref().is_some_and(|b| uses_effect(prog, b, effect)));
        uses.then_some(s)
    }

    /// The call in `body` that a `with` installing `handlers` around it can make to a
    /// clone specialized for the handler (Appendix D.3): a single handler literal
    /// around a direct call to a function that performs its effect.
    fn specializable(
        &mut self,
        handlers: &[HandlerValue],
        body: &'a Expr,
        outer: Option<Spec>,
    ) -> Option<(*const Expr, Spec)> {
        let [HandlerValue::Literal(h)] = handlers else { return None };
        let TyKind::Handler(hid) = h.ty.kind() else { return None };
        let mut call = body;
        while let ExprKind::Block(b) = &call.kind
            && b.stmts.is_empty()
        {
            call = b.tail.as_ref()?;
        }
        let ExprKind::Call { callee: Callee::Fn(f), .. } = &call.kind else { return None };
        let def = &self.g.prog.fns[f.index()];
        let effect = &self.g.prog.handlers[*hid as usize].effect;
        if def.kind != FnKind::Named || def.body.is_none() || !def.effects.effects.contains(effect) {
            return None;
        }
        let effect = effect.clone();
        let effect = self.g.effect_id(&effect);
        let spec = self.g.spec(SpecKey { handler: *hid, effect, outer });
        Some((call as *const Expr, spec))
    }

    /// The runtime's handlers for the ambient `Out`, `Err` and `Env` (§12.9).
    fn native_op(&mut self, effect: &EffectRef, op: u32, vals: &[String], res: Option<&str>, ret: Ty) -> CResult<()> {
        let amb = self.g.prog.ambient.clone();
        let p = self.ptr_bytes();
        let set = |fg: &mut Self, v: String| {
            if let Some(r) = res {
                fg.line(&format!("{} = {};", r, v));
            }
        };
        if Some(effect.effect) == amb.out || Some(effect.effect) == amb.err {
            let f = if Some(effect.effect) == amb.out { "gm_write_out" } else { "gm_write_err" };
            self.line(&format!(
                "{}(GM_LD(void *, &{}), GM_LD(uintptr_t, (uint8_t *)&{} + {}));",
                f, vals[0], vals[0], p
            ));
            let u = self.unit();
            set(self, u);
        } else {
            match op {
                0 => {
                    self.line("size_t argn; const void *argp = gm_args(&argn);");
                    let s = self.make_slice(ret, "argp", "argn");
                    set(self, s);
                }
                1 => {
                    self.line("const uint8_t *ev; size_t evn;");
                    self.line(&format!(
                        "bool evok = gm_env_var(GM_LD(void *, &{v}), GM_LD(uintptr_t, (uint8_t *)&{v} + {p}), &ev, &evn);",
                        v = vals[0],
                        p = p
                    ));
                    let s = self.make_slice(Ty::string(), "ev", "evn");
                    let some = self.make_some(ret, &s);
                    let none = self.make_none(ret);
                    set(self, format!("evok ? {} : {}", some, none));
                }
                _ => {
                    self.line(&format!("gm_unw.code = (uint8_t){};", vals[0]));
                    self.start_exit("GM_EXIT", "NULL", "0", "1");
                }
            }
        }
        Ok(())
    }

    fn with_expr(
        &mut self,
        e: &'a Expr,
        label: LabelId,
        handlers: &'a [HandlerValue],
        on: Option<&'a Expr>,
        body: &'a Expr,
    ) -> CResult<String> {
        let mut cur = self.cx.clone();
        let result_slot = format!("(void *)&F.w{}", label.0);
        // A `with … on` (§14) has a driver state its handlers point at.
        let fw = match on {
            Some(_) => {
                let w = format!("gw{}", self.fresh());
                self.line(&format!("gm_with {w}; memset(&{w}, 0, sizeof {w});"));
                w
            }
            None => String::new(),
        };
        let fw_ref = if on.is_some() { format!("&{}", fw) } else { "NULL".into() };
        let nested = self.nested_spec(handlers, on);
        for h in handlers {
            match h {
                HandlerValue::Literal(x) => {
                    let hv = self.expr(x)?;
                    let TyKind::Handler(hid) = x.ty.kind() else {
                        return unsupported(x.span, "this handler value");
                    };
                    let table = match nested {
                        Some(s) => self.g.spec_handler_table(*hid as usize, s),
                        None => self.g.handler_table(*hid as usize),
                    };
                    let effect = self.g.prog.handlers[*hid as usize].effect.clone();
                    let id = self.g.effect_id(&effect);
                    let n = format!("wn{}", self.fresh());
                    self.line(&format!(
                        "gm_node {n} = {{.eff = {id}, .kind = GM_LIT, .clauses = {table}, .env = {hv}.env, .hctx = {hv}.cx, .with_frame = &F.h, .with_label = {l}, .result = {r}, .fw = {fw_ref}, .next = {cur}}};",
                        l = label.0,
                        r = result_slot
                    ));
                    cur = format!("(&{})", n);
                }
                HandlerValue::Decl { decls, value } => {
                    let v = self.expr(value)?;
                    let s = self.tmp(value.ty, &v);
                    let outer = cur.clone();
                    for d in decls {
                        let table = self.g.decl_table(d.index())?;
                        let effect = self.g.prog.handler_decls[d.index()].effect.clone();
                        let id = self.g.effect_id(&effect);
                        let n = format!("wn{}", self.fresh());
                        self.line(&format!(
                            "gm_node {n} = {{.eff = {id}, .kind = GM_DECL, .clauses = {table}, .env = (void *)&{s}, .hctx = {outer}, .with_frame = &F.h, .with_label = {l}, .result = {r}, .fw = {fw_ref}, .next = {cur}}};",
                            l = label.0,
                            r = result_slot
                        ));
                        cur = format!("(&{})", n);
                    }
                }
            }
        }
        if let Some(stack) = on {
            return self.with_on(e, label, &fw, &cur, stack, body);
        }
        let saved = std::mem::replace(&mut self.cx, cur);
        let end = format!("we{}", self.fresh());
        let scope = self.with_scope(label, end.clone());
        self.scopes.push(scope);
        let saved_spec = self.spec_call.take();
        self.spec_call = self.specializable(handlers, body, nested);
        let v = self.expr(body);
        self.spec_call = saved_spec;
        let scope = self.scopes.pop().unwrap();
        self.cx = saved;
        let v = v?;
        if !body.ty.is_never() && !e.ty.is_never() {
            self.line(&format!("F.w{} = {};", label.0, v));
        }
        self.end_scope(scope, true);
        self.label(&end);
        Ok(if e.ty.is_never() { self.zero(e.ty) } else { self.tmp(e.ty, &format!("F.w{}", label.0)) })
    }

    fn try_expr(&mut self, e: &'a Expr, x: &'a Expr, lift: Option<Lift>, levels: u32) -> CResult<String> {
        let v = self.expr(x)?;
        let a = self.addr_of(x.ty, &v);
        let ret = self.target_ret(levels);
        let slot = self.frame_field(levels, "ret");
        let (fr, _) = self.frame_up(levels);
        match x.ty.kind() {
            TyKind::Optional(_) => {
                let present = self.opt_present(x.ty, &a);
                self.line(&format!("if (!{}) {{", present));
                self.indent += 1;
                let none = self.make_none(ret);
                self.line(&format!("{} = {};", slot, none));
                self.start_exit("GM_RETURN", &fr, "0", "0");
                self.indent -= 1;
                self.line("}");
                Ok(self.load(e.ty, &a))
            }
            TyKind::Result { err, .. } => {
                let err = *err;
                let bad = self.res_is_err(x.ty, &a);
                let off = self.g.ty.offsets(x.ty)[1];
                self.line(&format!("if ({}) {{", bad));
                self.indent += 1;
                let TyKind::Result { err: rerr, .. } = ret.kind() else {
                    return unsupported(e.span, "`try` in this function");
                };
                let rerr = *rerr;
                let ev = self.load(err, &format!("({} + {})", a, off));
                let ev = match lift {
                    Some(Lift::Variant(i)) => self.make_variant(rerr, i as usize, Some(&ev)),
                    None => ev,
                };
                let r = self.make_res(ret, true, rerr, &ev);
                self.line(&format!("{} = {};", slot, r));
                self.start_exit("GM_RETURN", &fr, "0", "1");
                self.indent -= 1;
                self.line("}");
                Ok(self.load(e.ty, &format!("({} + {})", a, off)))
            }
            _ => unsupported(e.span, "`try` on this type"),
        }
    }

    // --- conditions and patterns -------------------------------------------------------------------

    /// Emits the parts of an `if`/loop condition, jumping to `fail` when one fails.
    fn conds(&mut self, cond: &'a [CondPart], fail: &str) -> CResult<()> {
        for c in cond {
            match c {
                CondPart::Bool(b) => {
                    let v = self.expr(b)?;
                    self.line(&format!("if (!{}) goto {};", v, fail));
                }
                CondPart::Match { pat, value } => {
                    let v = self.expr(value)?;
                    let a = self.addr_of(value.ty, &v);
                    self.pat(pat, &a, fail)?;
                }
            }
        }
        Ok(())
    }

    fn match_expr(&mut self, e: &'a Expr, scrutinee: &'a Expr, arms: &'a [Arm]) -> CResult<String> {
        let v = self.expr(scrutinee)?;
        let a = self.addr_of(scrutinee.ty, &v);
        let res = (!e.ty.is_never() && !e.ty.is_unit()).then(|| self.tmp_uninit(e.ty));
        let end = format!("me{}", self.fresh());
        for arm in arms {
            let next = format!("mn{}", self.fresh());
            self.pat(&arm.pat, &a, &next)?;
            if let Some(g) = &arm.guard {
                let gv = self.expr(g)?;
                self.line(&format!("if (!{}) goto {};", gv, next));
            }
            let v = self.expr(&arm.body)?;
            if let Some(r) = &res
                && !arm.body.ty.is_never()
            {
                self.line(&format!("{} = {};", r, v));
            }
            self.line(&format!("goto {};", end));
            self.label(&next);
        }
        let loc = self.loc(e);
        self.line(&format!("gm_trap(\"no match arm matched the scrutinee\", {});", loc));
        self.label(&end);
        Ok(match res {
            Some(r) => r,
            None => self.zero(e.ty),
        })
    }

    /// Tests the value at `addr` against `p`, binding its locals; jumps to `fail` on mismatch.
    fn pat(&mut self, p: &'a Pat, addr: &str, fail: &str) -> CResult<()> {
        let t = p.ty;
        match &p.kind {
            PatKind::Wild => {}
            PatKind::Bind(l) => {
                let v = self.load(t, addr);
                let lv = self.local_lv(*l);
                self.line(&format!("{} = {};", lv, v));
            }
            PatKind::Const(c) => {
                let cv = self.expr(c)?;
                let v = self.load(t, addr);
                let eq = self.equal(t, &v, &cv);
                self.line(&format!("if (!{}) goto {};", eq, fail));
            }
            PatKind::Range { lo, hi, inclusive } => {
                let v = self.load(t, addr);
                let (l, h) = (self.int_lit(t, lo), self.int_lit(t, hi));
                let op = if *inclusive { ">" } else { ">=" };
                let (below, above) = (self.int_cmp(t, &v, "<", &l), self.int_cmp(t, &v, op, &h));
                self.line(&format!("if ({below} || {above}) goto {fail};"));
            }
            PatKind::Null => {
                let present = self.opt_present(t, addr);
                self.line(&format!("if ({}) goto {};", present, fail));
            }
            PatKind::Some(q) => {
                let present = self.opt_present(t, addr);
                self.line(&format!("if (!{}) goto {};", present, fail));
                self.pat(q, addr, fail)?;
            }
            PatKind::Ok(q) | PatKind::Err(q) => {
                let bad = self.res_is_err(t, addr);
                let want_err = matches!(p.kind, PatKind::Err(_));
                self.line(&format!("if ({}{}) goto {};", if want_err { "!" } else { "" }, bad, fail));
                let off = self.g.ty.offsets(t)[1];
                self.pat(q, &format!("({} + {})", addr, off), fail)?;
            }
            PatKind::Variant { variant, payload } => {
                let def = self.adt_def(t);
                let AdtKind::Union { tag, variants } = &def.kind else {
                    return unsupported(p.span, "this variant pattern");
                };
                let (tag, vtag) = (*tag, variants[*variant as usize].tag.clone());
                let offs = self.g.ty.offsets(t);
                let tv = self.load(tag, &format!("({} + {})", addr, offs[0]));
                let want = self.int_lit(tag, &vtag);
                self.line(&format!("if ({} != {}) goto {};", tv, want, fail));
                if let Some(q) = payload {
                    self.pat(q, &format!("({} + {})", addr, offs[1]), fail)?;
                }
            }
            PatKind::Fields(fs) => {
                let offs = self.g.ty.offsets(t);
                for (i, q) in fs {
                    self.pat(q, &format!("({} + {})", addr, offs[*i as usize]), fail)?;
                }
            }
            PatKind::Or(alts) => {
                let ok = format!("po{}", self.fresh());
                for alt in alts {
                    let next = format!("pa{}", self.fresh());
                    self.pat(alt, addr, &next)?;
                    self.line(&format!("goto {};", ok));
                    self.label(&next);
                }
                self.line(&format!("goto {};", fail));
                self.label(&ok);
            }
        }
        Ok(())
    }

    /// `x in c` for arrays, slices, inline tuples and ranges (§7.7).
    fn in_expr(&mut self, elem: &'a Expr, container: &'a Expr, negated: bool) -> CResult<String> {
        let x = self.expr(elem)?;
        let c = self.expr(container)?;
        let ca = self.addr_of(container.ty, &c);
        let r = self.tmp(Ty::bool(), "0");
        match container.ty.kind() {
            TyKind::Array { .. } | TyKind::Slice { .. } => {
                let (et, n, p) = match container.ty.kind() {
                    TyKind::Array { len, elem } => (*elem, len.to_string(), ca.clone()),
                    TyKind::Slice { elem, .. } => {
                        let (p, l) = (self.slice_ptr(&ca), self.slice_len(&ca));
                        (*elem, l, format!("((uint8_t *){})", p))
                    }
                    _ => unreachable!(),
                };
                let esz = self.g.ty.size(et);
                let xa = self.addr_of(et, &x);
                let eq = self.g.helpers.eq_fn(&mut self.g.ty, et);
                self.line(&format!(
                    "for (uintptr_t i = 0; i < (uintptr_t)({n}); i++) if ({eq}({p} + i * {esz}, {xa})) {{ {r} = 1; break; }}"
                ));
            }
            TyKind::Tuple { elems, .. } => {
                let offs = self.g.ty.offsets(container.ty);
                let et = elem.ty;
                let xa = self.addr_of(et, &x);
                let eq = self.g.helpers.eq_fn(&mut self.g.ty, et);
                for (i, _) in elems.iter().enumerate() {
                    self.line(&format!("if ({eq}({ca} + {}, {xa})) {r} = 1;", offs[i]));
                }
            }
            TyKind::Adt(..) => {
                let def = self.adt_def(container.ty);
                let incl = match def.name.as_str() {
                    "Range" => false,
                    "RangeIncl" => true,
                    _ => return unsupported(container.span, "`in` on this type"),
                };
                let offs = self.g.ty.offsets(container.ty);
                let lt = elem.ty;
                let lo = self.load(lt, &format!("({ca} + {})", offs[0]));
                let hi = self.load(lt, &format!("({ca} + {})", offs[1]));
                let op = if incl { "<=" } else { "<" };
                let (ge, below) = (self.int_cmp(lt, &x, ">=", &lo), self.int_cmp(lt, &x, op, &hi));
                self.line(&format!("{r} = {ge} && {below};"));
            }
            _ => return unsupported(container.span, "`in` on this type"),
        }
        Ok(self.tmp(Ty::bool(), &format!("(uint8_t)({}{})", if negated { "!" } else { "" }, r)))
    }

    /// The bits of `v`, a value of `#bits` field type `t`, as an `unsigned __int128`.
    pub fn bits_raw(&mut self, t: Ty, v: &str) -> String {
        match self.g.ty.repr(t) {
            Repr::Bool => format!("(unsigned __int128)({} != 0)", v),
            Repr::Int { storage, .. } => {
                format!("(unsigned __int128)({})({})", super::types::int_cty(false, storage), v)
            }
            _ => {
                // An enum's tag, or a nested `#bits` struct's integer.
                let under = match &self.adt_def(t).kind {
                    AdtKind::Union { tag, .. } => *tag,
                    AdtKind::Struct { bits: Some(u), .. } => *u,
                    _ => Ty::u8(),
                };
                let a = self.addr_of(t, v);
                let uc = self.g.ty.cty(under);
                let us = super::types::int_cty(false, self.g.ty.size(under));
                format!("(unsigned __int128)({})GM_LD({}, {})", us, uc, a)
            }
        }
    }

    /// A value of `#bits` field type `t` from its bits `raw` (an `unsigned __int128`).
    pub fn bits_value(&mut self, t: Ty, raw: &str) -> String {
        let w = self.g.ty.bit_width(t);
        match self.g.ty.repr(t) {
            Repr::Bool => self.tmp(t, &format!("(uint8_t)({} != 0)", raw)),
            Repr::Int { signed, .. } => {
                let c = self.g.ty.cty(t);
                if signed && w < 128 {
                    self.tmp(t, &format!("({c})((__int128)({raw} << {s}) >> {s})", s = 128 - w))
                } else {
                    self.tmp(t, &format!("({c}){raw}"))
                }
            }
            _ => {
                let (under, signed) = match &self.adt_def(t).kind {
                    AdtKind::Union { tag, .. } => (*tag, tag.is_signed_integer()),
                    AdtKind::Struct { bits: Some(u), .. } => (*u, false),
                    _ => (Ty::u8(), false),
                };
                let uc = self.g.ty.cty(under);
                let v = if signed && w < 128 {
                    format!("({uc})((__int128)({raw} << {s}) >> {s})", s = 128 - w)
                } else {
                    format!("({uc}){raw}")
                };
                let b = self.blob(t);
                let off =
                    if matches!(self.adt_def(t).kind, AdtKind::Union { .. }) { self.g.ty.offsets(t)[0] } else { 0 };
                self.line(&format!("GM_ST({uc}, (uint8_t *)&{b} + {off}, {v});"));
                b
            }
        }
    }

    /// A C condition: values `a` and `b` of type `t` are `is`-equal (§7.6).
    pub fn equal(&mut self, t: Ty, a: &str, b: &str) -> String {
        match self.g.ty.repr(t) {
            Repr::Blob => {
                let f = self.g.helpers.eq_fn(&mut self.g.ty, t);
                let (pa, pb) = (self.addr_of(t, a), self.addr_of(t, b));
                format!("{}({}, {})", f, pa, pb)
            }
            _ => format!("({} == {})", a, b),
        }
    }
}

/// True if evaluating `e` cannot write memory or perform effects: it combines reads,
/// literals and pure operators (it may still trap).
fn no_writes(e: &Expr) -> bool {
    let pure = matches!(
        e.kind,
        ExprKind::Int(_)
            | ExprKind::Float(_)
            | ExprKind::SizeOf { .. }
            | ExprKind::TypeIs { .. }
            | ExprKind::IntLimit { .. }
            | ExprKind::UntypedFloat(_)
            | ExprKind::Bool(_)
            | ExprKind::Str(_)
            | ExprKind::CStr(_)
            | ExprKind::Null
            | ExprKind::ConstBytes(_)
            | ExprKind::ValueParam(_)
            | ExprKind::Uninit
            | ExprKind::Const(_)
            | ExprKind::Local(_)
            | ExprKind::Upvar { .. }
            | ExprKind::Global(_)
            | ExprKind::Deref(_)
            | ExprKind::Field { .. }
            | ExprKind::VariantPayload { .. }
            | ExprKind::Index { .. }
            | ExprKind::Slice { .. }
            | ExprKind::SliceLen(_)
            | ExprKind::SlicePtr(_)
            | ExprKind::AddrOf(_)
            | ExprKind::FnRef(_)
            | ExprKind::Unary { .. }
            | ExprKind::Binary { .. }
            | ExprKind::Logic { .. }
            | ExprKind::Coerce { .. }
            | ExprKind::Aggregate(_)
            | ExprKind::Array(_)
            | ExprKind::ArrayFill(_)
            | ExprKind::Variant { .. }
    );
    let mut ok = pure;
    if ok {
        e.for_each_child(&mut |c| ok = ok && no_writes(c));
    }
    ok
}

/// True if `e` performs `effect` or calls a function that does.
fn uses_effect(prog: &crate::tir::Program, e: &Expr, effect: &EffectRef) -> bool {
    let found = match &e.kind {
        ExprKind::Call { callee: Callee::Perform { effect: x, .. }, .. } => x == effect,
        ExprKind::Call { callee: Callee::Fn(f), .. } => prog.fns[f.index()].effects.effects.contains(effect),
        _ => false,
    };
    let mut found = found;
    e.for_each_child(&mut |c| found = found || uses_effect(prog, c, effect));
    found
}

fn find_with(e: &Expr, label: LabelId, found: &mut bool) {
    if let ExprKind::With { label: l, .. } = &e.kind
        && *l == label
    {
        *found = true;
    }
    e.for_each_child(&mut |c| find_with(c, label, found));
}

/// The low and high 64-bit halves of `v` modulo 2^128 (two's complement).
pub fn split_u128(v: &BigInt) -> (u128, u128) {
    let m: BigInt = BigInt::from(1u8) << 128usize;
    let mut x = v % &m;
    if x < BigInt::from(0) {
        x += &m;
    }
    let x: u128 = u128::try_from(x).unwrap_or(0);
    (x & 0xffff_ffff_ffff_ffff, x >> 64)
}

/// A C literal of float type `f` with value `x` (exactly representable in it).
pub fn float_lit(f: FloatTy, x: f64) -> String {
    let c = match f {
        FloatTy::F16 => "_Float16",
        FloatTy::F32 => "float",
        FloatTy::F64 => "double",
    };
    if x.is_nan() {
        return format!("(({})__builtin_nan(\"\"))", c);
    }
    if x.is_infinite() {
        return format!("(({}){}__builtin_inf())", c, if x < 0.0 { "-" } else { "" });
    }
    // Rust's `{:e}` is the shortest form that reads back as the same f64.
    format!("(({}){:e})", c, x)
}

/// The mask of the low `w` bits, as an `unsigned __int128` C expression.
pub fn mask(w: u32) -> String {
    if w >= 128 { "(~(unsigned __int128)0)".into() } else { format!("((((unsigned __int128)1) << {}) - 1)", w) }
}

/// A C compound literal for a format spec (`gm_spec` in the runtime).
fn spec_literal(s: &crate::tir::FmtSpec) -> String {
    use crate::tir::{FmtAlign, FmtKind};
    let align = match s.align {
        FmtAlign::Auto => 0,
        FmtAlign::Left => 1,
        FmtAlign::Right => 2,
        FmtAlign::Center => 3,
    };
    let kind = match s.kind {
        FmtKind::Display => 0,
        FmtKind::Hex => 1,
        FmtKind::HexUpper => 2,
        FmtKind::Oct => 3,
        FmtKind::Bin => 4,
        FmtKind::Exp => 5,
    };
    format!(
        "(gm_spec){{{}, {}, {}, {}, {}, {}, {}u, {}}}",
        s.fill,
        align,
        s.plus as u8,
        s.alt as u8,
        s.zero as u8,
        kind,
        s.width,
        s.precision.map_or(-1, |p| p as i64)
    )
}
