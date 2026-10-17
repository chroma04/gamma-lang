//! Expression evaluation.

use super::bytes::Bytes;
use super::coverage::cover_hit;
use super::machine::*;
use super::memory::AllocKind;
use super::num::{self, Int};
use super::value::*;
use crate::layout;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;

impl<'a> Machine<'a> {
    pub fn value_bytes(&self, span: Span, v: Value) -> R<Bytes> {
        match v {
            Value::Bytes(b) => Ok(b),
            _ => self.trap(span, "internal error: a second-class value was used as data"),
        }
    }

    pub fn eval_bytes(&mut self, a: Act, e: &Expr) -> R<Bytes> {
        let v = self.eval(a, e)?;
        self.value_bytes(e.span, v)
    }

    pub fn truthy(&self, b: &[u8]) -> bool {
        b.first().is_some_and(|x| *x != 0)
    }

    /// Evaluates an expression. The arms only dispatch: each kind that needs more than a call has
    /// a function of its own so that this function's stack frame, which every nesting level of
    /// an expression pays for, stays small.
    pub fn eval(&mut self, a: Act, e: &Expr) -> R {
        self.step(e.span)?;
        match &e.kind {
            ExprKind::Int(v) => self.eval_int_literal(e, v),
            ExprKind::Bool(b) => Ok(Value::Bytes(self.bool_bytes(*b))),
            ExprKind::Local(id) => self.read_local(e.span, a.fr, *id, e.ty),
            ExprKind::Upvar { levels, local } => self.eval_upvar(a, e, *levels, *local),
            ExprKind::Global(_) | ExprKind::Deref(_) => self.eval_place_value(a, e),
            ExprKind::Field { base, index } => self.eval_field(a, e, base, *index),
            ExprKind::VariantPayload { base, variant: _ } => self.eval_variant_payload(a, e, base),
            ExprKind::Index { base, index, checked: _ } => self.eval_index_value(a, e, base, index),
            ExprKind::Slice { base, lo, hi, inclusive } => self.eval_slice(a, e, base, lo, hi, *inclusive),
            ExprKind::SliceLen(s) => self.eval_slice_len(a, s),
            ExprKind::AddrOf(inner) => self.eval_addr_of(a, e, inner),
            ExprKind::Unary { op, operand } => self.eval_unary(a, e, *op, operand),
            ExprKind::Binary { op, lhs, rhs } => self.eval_binary(a, e, *op, lhs, rhs),
            ExprKind::Logic { is_and, lhs, rhs } => self.eval_logic(a, *is_and, lhs, rhs),
            ExprKind::Coerce { kind, expr } => self.eval_coerce(a, e, *kind, expr),
            ExprKind::Builtin { op, args } => self.eval_builtin(a, e, *op, args),
            ExprKind::Call { callee, args } => self.eval_call(a, e, callee, args),
            ExprKind::Eq { lhs, rhs, negated } => self.eval_eq(a, e, lhs, rhs, *negated),
            ExprKind::Block(b) => self.eval_block(a, b),
            ExprKind::If { cond, then, else_ } => {
                if self.eval_conds(a, cond)? {
                    self.eval(a, then)
                } else if let Some(el) = else_ {
                    self.eval(a, el)
                } else {
                    Ok(Value::unit())
                }
            }
            ExprKind::Match { scrutinee, arms } => self.eval_match(a, e, scrutinee, arms),
            ExprKind::Else { lhs, capture, rhs } => self.eval_else(a, e, lhs, *capture, rhs),
            ExprKind::Try { expr, lift, levels } => self.eval_try(a, e, expr, *lift, *levels),
            ExprKind::Loop { label, cond, body, step } => self.eval_loop(a, *label, cond, body, step.as_deref()),
            ExprKind::Break { target, value } => self.eval_break(a, e, *target, value.as_deref()),
            ExprKind::Continue { target } => self.eval_continue(a, e, *target),
            ExprKind::Return { levels, value } => self.eval_return(a, e, *levels, value),
            _ => self.eval_cold(a, e),
        }
    }

    #[inline(never)]
    fn eval_int_literal(&mut self, e: &Expr, v: &num_bigint::BigInt) -> R {
        if let Some(si) = self.small_of(e.ty) {
            return Ok(Value::Bytes(self.encode_small(si, Self::int_literal(v, si))));
        }
        let v = Int::from_big(v.clone());
        match e.ty.kind() {
            TyKind::Float(f) => Ok(Value::Bytes(self.float_bytes(*f, num::round_float(v.to_f64(), *f)))),
            _ => {
                let ti = self.ti(e.ty);
                let (s, bits) = ti.int.unwrap_or((true, 64));
                Ok(Value::Bytes(num::encode_int(&num::wrap(&v, s, bits), ti.size, self.little())))
            }
        }
    }

    #[inline(never)]
    fn eval_upvar(&mut self, a: Act, e: &Expr, levels: u32, local: LocalId) -> R {
        let Some(fr) = self.up_frame(a.fr, levels) else {
            return self.trap(e.span, "internal error: upvar without an enclosing frame");
        };
        self.read_local(e.span, &fr, local, e.ty)
    }

    #[inline(never)]
    fn eval_place_value(&mut self, a: Act, e: &Expr) -> R {
        let p = self.eval_place(a, e)?;
        Ok(Value::Bytes(self.read_place(e.span, p, e.ty)?))
    }

    #[inline(never)]
    fn eval_field(&mut self, a: Act, e: &Expr, base: &Expr, index: u32) -> R {
        if is_place_expr(base) {
            let p = self.eval_place(a, e)?;
            Ok(Value::Bytes(self.read_place(e.span, p, e.ty)?))
        } else {
            let b = self.eval_bytes(a, base)?;
            let r = self.field_bytes(base.ty, &b, index as usize);
            Ok(Value::Bytes(self.tr(e.span, r)?.1))
        }
    }

    #[inline(never)]
    fn eval_variant_payload(&mut self, a: Act, e: &Expr, base: &Expr) -> R {
        if is_place_expr(base) {
            let p = self.eval_place(a, e)?;
            Ok(Value::Bytes(self.read_place(e.span, p, e.ty)?))
        } else {
            let b = self.eval_bytes(a, base)?;
            let off = self.payload_offset(base.ty);
            let sz = self.size_of(e.ty);
            Ok(Value::Bytes(Bytes::from_slice(&b[off..off + sz])))
        }
    }

    #[inline(never)]
    fn eval_index_value(&mut self, a: Act, e: &Expr, base: &Expr, index: &Expr) -> R {
        if matches!(base.ty.kind(), TyKind::Array { .. }) && !is_place_expr(base) {
            let b = self.eval_bytes(a, base)?;
            let i = self.eval_bytes(a, index)?;
            let TyKind::Array { len, .. } = base.ty.kind() else { unreachable!() };
            let i = self.int_of(index.ty, &i).to_u64_sat();
            if i >= *len {
                return self.trap(e.span, format!("index {} out of bounds for length {}", i, len));
            }
            let esz = self.size_of(e.ty);
            let off = i as usize * esz;
            Ok(Value::Bytes(Bytes::from_slice(&b[off..off + esz])))
        } else {
            let p = self.eval_place(a, e)?;
            Ok(Value::Bytes(self.read_place(e.span, p, e.ty)?))
        }
    }

    #[inline(never)]
    fn eval_slice_len(&mut self, a: Act, s: &Expr) -> R {
        let b = self.eval_bytes(a, s)?;
        Ok(Value::Bytes(self.usize_bytes(self.slice_parts(&b).1)))
    }

    #[inline(never)]
    fn eval_addr_of(&mut self, a: Act, e: &Expr, inner: &Expr) -> R {
        match self.eval_place(a, inner)? {
            Place::Mem(addr) | Place::Packed(addr) => Ok(Value::Bytes(self.ptr_bytes(addr))),
            Place::Bits { .. } => self.trap(e.span, "cannot take the address of a #bits field"),
        }
    }

    #[inline(never)]
    fn eval_unary(&mut self, a: Act, e: &Expr, op: UnOp, operand: &Expr) -> R {
        match op {
            UnOp::Neg | UnOp::BitNot if operand.ty == e.ty => {
                if let Some(si) = self.small_of(e.ty) {
                    let v = self.eval_unary_small(a, e, op, operand, si)?;
                    return Ok(Value::Bytes(self.encode_small(si, v)));
                }
            }
            UnOp::Not => {
                let v = self.eval_cond(a, operand)?;
                return Ok(Value::Bytes(self.bool_bytes(!v)));
            }
            _ => {}
        }
        let b = self.eval_bytes(a, operand)?;
        Ok(Value::Bytes(self.unop(e.span, op, operand.ty, &b)?))
    }

    #[inline(never)]
    fn eval_binary(&mut self, a: Act, e: &Expr, op: BinOp, lhs: &Expr, rhs: &Expr) -> R {
        if let Some((si, ri)) = self.small_binary_info(op, lhs, rhs, e.ty) {
            let v = self.eval_binary_small(a, e, op, lhs, rhs, si, ri)?;
            return Ok(Value::Bytes(if is_cmp(op) { self.bool_bytes(v != 0) } else { self.encode_small(si, v) }));
        }
        let l = self.eval_bytes(a, lhs)?;
        let r = self.eval_bytes(a, rhs)?;
        Ok(Value::Bytes(self.binop(e.span, op, lhs.ty, rhs.ty, e.ty, &l, &r)?))
    }

    #[inline(never)]
    fn eval_logic(&mut self, a: Act, is_and: bool, lhs: &Expr, rhs: &Expr) -> R {
        let lv = self.eval_cond(a, lhs)?;
        if lv != is_and {
            return Ok(Value::Bytes(self.bool_bytes(lv)));
        }
        let rv = self.eval_cond(a, rhs)?;
        Ok(Value::Bytes(self.bool_bytes(rv)))
    }

    #[inline(never)]
    fn eval_eq(&mut self, a: Act, e: &Expr, lhs: &Expr, rhs: &Expr, negated: bool) -> R {
        if lhs.ty == rhs.ty
            && let Some(si) = self.small_of(lhs.ty)
        {
            let l = self.eval_small(a, lhs, si)?;
            let r = self.eval_small(a, rhs, si)?;
            return Ok(Value::Bytes(self.bool_bytes((l == r) != negated)));
        }
        let l = self.eval_bytes(a, lhs)?;
        let r = self.eval_bytes(a, rhs)?;
        let v = self.tr(e.span, self.eq(lhs.ty, &l, &r))?;
        Ok(Value::Bytes(self.bool_bytes(v != negated)))
    }

    #[inline(never)]
    fn eval_break(&mut self, a: Act, e: &Expr, target: Target, value: Option<&Expr>) -> R {
        let v = match value {
            Some(x) => self.eval(a, x)?,
            None => Value::unit(),
        };
        let (frame, label) = self.resolve_target(e.span, a, target)?;
        Err(Box::new(Flow::Break { frame, label, value: v }))
    }

    #[inline(never)]
    fn eval_continue(&mut self, a: Act, e: &Expr, target: Target) -> R {
        match target {
            Target::InstallingWith => {
                Err(Box::new(Flow::Return { frame: a.fr.id, value: Value::unit(), is_err: false }))
            }
            t => {
                let (frame, label) = self.resolve_target(e.span, a, t)?;
                Err(Box::new(Flow::Continue { frame, label }))
            }
        }
    }

    #[inline(never)]
    fn eval_return(&mut self, a: Act, e: &Expr, levels: u32, value: &Expr) -> R {
        let v = self.eval(a, value)?;
        let Some(target) = self.up_frame(a.fr, levels) else {
            return self.trap(e.span, "internal error: return from a missing frame");
        };
        let is_err = self.is_err_return(&target, &v);
        Err(Box::new(Flow::Return { frame: target.id, value: v, is_err }))
    }

    /// The less frequent expression kinds, kept out of [`Machine::eval`] so that its stack frame stays small.
    #[inline(never)]
    fn eval_cold(&mut self, a: Act, e: &Expr) -> R {
        match &e.kind {
            ExprKind::Float(x) => match e.ty.kind() {
                TyKind::Float(f) => Ok(Value::Bytes(self.float_bytes(*f, num::round_float(*x, *f)))),
                _ => self.trap(e.span, "float literal of a non-float type"),
            },
            ExprKind::Str(s) => {
                let addr = self.string_literal(e.span, s, false)?;
                Ok(Value::Bytes(self.make_slice(addr, s.len() as u64)))
            }
            ExprKind::CStr(s) => {
                let addr = self.string_literal(e.span, s, true)?;
                Ok(Value::Bytes(self.ptr_bytes(addr)))
            }
            ExprKind::Null => Ok(Value::Bytes(self.make_none(e.ty))),
            ExprKind::ConstBytes(b) => Ok(Value::from_slice(b)),
            ExprKind::Uninit => {
                let fill = if self.mode == super::Mode::Debug { 0xAA } else { 0 };
                Ok(Value::Bytes(Bytes::filled(self.size_of(e.ty), fill)))
            }
            ExprKind::Const(id) => {
                let addr = self.const_addr(e.span, *id)?;
                Ok(Value::Bytes(self.load_ty(e.span, addr, e.ty)?))
            }
            ExprKind::SlicePtr(s) => {
                let b = self.eval_bytes(a, s)?;
                Ok(Value::Bytes(self.ptr_bytes(self.slice_parts(&b).0)))
            }
            ExprKind::AddrOfTemp(inner) => {
                let b = self.eval_bytes(a, inner)?;
                let align = self.ti(inner.ty).align;
                let addr = self.alloc_temp(e.span, &b, align)?;
                Ok(Value::Bytes(self.ptr_bytes(addr)))
            }
            ExprKind::FnRef(f) => match e.ty.kind() {
                TyKind::Fn(_) => Ok(Value::Closure(Closure { func: *f, env: None, ctx: a.cx.clone() })),
                _ => Ok(Value::Bytes(self.ptr_bytes(self.fn_addr(*f)))),
            },
            ExprKind::Closure(f) => {
                Ok(Value::Closure(Closure { func: *f, env: Some(a.fr.clone()), ctx: a.cx.clone() }))
            }
            ExprKind::HandlerRef(h) => {
                Ok(Value::Handler(std::rc::Rc::new(HandlerVal { id: *h, env: Some(a.fr.clone()), ctx: a.cx.clone() })))
            }
            ExprKind::Aggregate(items) => self.eval_aggregate(a, e, items),
            ExprKind::Array(items) => {
                let esz = items.first().map_or(0, |i| self.size_of(i.ty));
                let mut out = Bytes::zeroed(self.size_of(e.ty));
                for (i, it) in items.iter().enumerate() {
                    let b = self.eval_bytes(a, it)?;
                    if esz > 0 {
                        out[i * esz..(i + 1) * esz].copy_from_slice(&b);
                    }
                }
                Ok(Value::Bytes(out))
            }
            ExprKind::ArrayFill(x) => {
                let b = self.eval_bytes(a, x)?;
                let TyKind::Array { len, .. } = e.ty.kind() else {
                    return self.trap(e.span, "internal error: array fill of a non-array");
                };
                let mut out = Bytes::zeroed(self.size_of(e.ty));
                if !b.is_empty() {
                    for i in 0..*len as usize {
                        out[i * b.len()..(i + 1) * b.len()].copy_from_slice(&b);
                    }
                }
                Ok(Value::Bytes(out))
            }
            ExprKind::Variant { variant, payload } => {
                let p = match payload {
                    Some(p) => self.eval_bytes(a, p)?,
                    None => Bytes::empty(),
                };
                Ok(Value::Bytes(self.make_variant(e.ty, *variant as usize, &p)))
            }
            ExprKind::MakeErr(x) => {
                let b = self.eval_bytes(a, x)?;
                Ok(Value::Bytes(self.make_res(e.ty, true, &b)))
            }
            ExprKind::Unwrap { expr, is_result } => {
                let b = self.eval_bytes(a, expr)?;
                if *is_result {
                    if self.res_is_err(expr.ty, &b) {
                        if self.fast() {
                            return Ok(Value::Bytes(Bytes::zeroed(self.size_of(e.ty))));
                        }
                        let (err, _) = self.res_types(expr.ty);
                        let payload = self.res_payload(expr.ty, &b);
                        let mut s = Vec::new();
                        let _ = self.format_value(&mut s, err, &payload, false);
                        return self.trap(e.span, format!("unwrap of error value {}", String::from_utf8_lossy(&s)));
                    }
                    Ok(Value::Bytes(self.res_payload(expr.ty, &b)))
                } else {
                    if !self.opt_present(expr.ty, &b) {
                        if self.fast() {
                            return Ok(Value::Bytes(Bytes::zeroed(self.size_of(e.ty))));
                        }
                        return self.trap(e.span, "unwrap of null");
                    }
                    Ok(Value::Bytes(self.opt_payload(expr.ty, &b)))
                }
            }
            ExprKind::In { elem, container, negated } => {
                let x = self.eval_bytes(a, elem)?;
                let c = self.eval_bytes(a, container)?;
                let v = self.tr(e.span, self.contains(elem.ty, &x, container.ty, &c))?;
                Ok(Value::Bytes(self.bool_bytes(v != *negated)))
            }
            ExprKind::With { label, handlers, on, body } => self.eval_with(a, e, *label, handlers, on.as_deref(), body),
            ExprKind::Resume { cont, value } => self.eval_resume(a, e.span, cont, value),
            ExprKind::Cancel(k) => self.eval_cancel(a, e.span, k),
            ExprKind::Trap { message } => {
                let m = self.eval_bytes(a, message)?;
                let s = self.read_str(e.span, &m)?;
                self.trap(e.span, String::from_utf8_lossy(&s).into_owned())
            }
            ExprKind::Write { sink, pieces, specs, args } => self.eval_write(a, e, sink, pieces, specs, args),
            #[allow(unreachable_patterns)]
            _ => self.trap(e.span, "internal error: expression kind reached the interpreter"),
        }
    }

    pub fn is_err_return(&self, target: &Frame, v: &Value) -> bool {
        match (target.ret_ty.kind(), v) {
            (TyKind::Result { .. }, Value::Bytes(b)) => self.res_is_err(target.ret_ty, b),
            _ => false,
        }
    }

    pub fn resolve_target(&self, span: Span, a: Act, t: Target) -> R<(u64, LabelId)> {
        match t {
            Target::Label { levels, label } => match self.up_frame(a.fr, levels) {
                Some(f) => Ok((f.id, label)),
                None => self.trap(span, "internal error: jump target in a missing frame"),
            },
            Target::InstallingWith => match a.fr.installing {
                Some(x) => Ok(x),
                None => self.trap(span, "internal error: no installing `with` for this break"),
            },
        }
    }

    // --- places --------------------------------------------------------------------------------

    pub fn read_local(&self, span: Span, fr: &Rc<Frame>, id: LocalId, ty: Ty) -> R {
        let locals = fr.locals.borrow();
        match locals.get(id.index()) {
            Some(Slot::Mem(addr)) => {
                let addr = *addr;
                drop(locals);
                Ok(Value::Bytes(self.load_ty(span, addr, ty)?))
            }
            Some(Slot::Reg(r)) => Ok(Value::Bytes(Bytes::from_slice(&r[..self.ti(ty).size]))),
            Some(Slot::Val(v)) => Ok((**v).clone()),
            _ => self.local_uninit(span),
        }
    }

    pub fn read_place(&self, span: Span, p: Place, ty: Ty) -> R<Bytes> {
        match p {
            Place::Mem(addr) => self.load_ty(span, addr, ty),
            Place::Packed(addr) => self.load(span, addr, self.ti(ty).size, 1),
            Place::Bits { addr, whole, off, fty } => {
                let u = self.bits_struct_ty(whole).unwrap_or(whole);
                let b = self.load_ty(span, addr, whole)?;
                let raw = num::wrap(&self.int_of_unsigned(u, &b).shr(off as u64), false, self.bit_width(fty));
                Ok(self.bits_from_raw(fty, &raw))
            }
        }
    }

    pub fn write_place(&mut self, span: Span, p: Place, ty: Ty, data: &[u8]) -> R<()> {
        match p {
            Place::Mem(addr) => {
                let align = self.ti(ty).align;
                self.store(span, addr, data, align)
            }
            Place::Packed(addr) => self.store(span, addr, data, 1),
            Place::Bits { addr, whole: sty, off, fty } => {
                let u = self.bits_struct_ty(sty).unwrap_or(sty);
                let whole = self.load_ty(span, addr, sty)?;
                let cur = self.int_of_unsigned(u, &whole);
                let w = self.bit_width(fty);
                let raw = self.bits_raw(fty, data);
                let mask = Int::S(1).shl(w as u64).sub(&Int::S(1)).shl(off as u64);
                let new = cur.and(&mask.not()).or(&raw.shl(off as u64));
                let ui = self.ti(u);
                let enc = num::encode_int(&new, ui.size, self.little());
                self.store(span, addr, &enc, ui.align)
            }
        }
    }

    /// Evaluates a place expression to a location. Non-place expressions are materialized in a temporary.
    pub fn eval_place(&mut self, a: Act, e: &Expr) -> R<Place> {
        match &e.kind {
            ExprKind::Local(id) => {
                let slot = match a.fr.locals.borrow().get(id.index()) {
                    Some(Slot::Mem(addr)) => Some(Ok(*addr)),
                    Some(Slot::Val(_)) => Some(Err(())),
                    _ => None,
                };
                match slot {
                    Some(Ok(addr)) => Ok(Place::Mem(addr)),
                    Some(Err(())) => self.trap(e.span, "internal error: second-class value used as a place"),
                    None => {
                        let ti = self.ti(e.ty);
                        let addr = self.local_addr(e.span, a.fr, *id, ti.size, ti.align)?;
                        Ok(Place::Mem(addr))
                    }
                }
            }
            ExprKind::Upvar { levels, local } => {
                let Some(fr) = self.up_frame(a.fr, *levels) else {
                    return self.trap(e.span, "internal error: upvar without an enclosing frame");
                };
                let ti = self.ti(e.ty);
                let slot = match fr.locals.borrow().get(local.index()) {
                    Some(Slot::Mem(addr)) => Some(Ok(*addr)),
                    Some(Slot::Val(_)) => Some(Err(())),
                    _ => None,
                };
                match slot {
                    Some(Ok(addr)) => Ok(Place::Mem(addr)),
                    Some(Err(())) => self.trap(e.span, "internal error: second-class value used as a place"),
                    None => Ok(Place::Mem(self.local_addr(e.span, &fr, *local, ti.size, ti.align)?)),
                }
            }
            ExprKind::Global(g) => Ok(Place::Mem(self.global_addr(e.span, *g)?)),
            ExprKind::Const(id) => Ok(Place::Mem(self.const_addr(e.span, *id)?)),
            ExprKind::Deref(p) => {
                let b = self.eval_bytes(a, p)?;
                Ok(Place::Mem(self.ptr_of(&b)))
            }
            ExprKind::Field { base, index } => {
                if self.bits_struct_ty(base.ty).is_some() {
                    let (off, fty) = self.tr(e.span, self.bits_field(base.ty, *index as usize))?;
                    return Ok(match self.eval_place(a, base)? {
                        Place::Mem(addr) | Place::Packed(addr) => Place::Bits { addr, whole: base.ty, off, fty },
                        Place::Bits { addr, whole, off: o, .. } => Place::Bits { addr, whole, off: o + off, fty },
                    });
                }
                let (addr, packed) = match self.eval_place(a, base)? {
                    Place::Mem(addr) => (addr, self.is_packed(base.ty)),
                    Place::Packed(addr) => (addr, true),
                    Place::Bits { .. } => return self.trap(e.span, "internal error: field of a #bits field"),
                };
                let (_, off) = self.tr(e.span, self.field_layout(base.ty, *index as usize))?;
                Ok(if packed { Place::Packed(addr + off as u64) } else { Place::Mem(addr + off as u64) })
            }
            ExprKind::VariantPayload { base, .. } => {
                let (Place::Mem(addr) | Place::Packed(addr)) = self.eval_place(a, base)? else {
                    return self.trap(e.span, "internal error: payload of a #bits field");
                };
                Ok(Place::Mem(addr + self.payload_offset(base.ty) as u64))
            }
            ExprKind::Index { base, index, .. } => {
                let esz = self.size_of(e.ty) as u64;
                match base.ty.kind() {
                    TyKind::Array { len, .. } => {
                        let (addr, packed) = match self.eval_place(a, base)? {
                            Place::Mem(addr) => (addr, false),
                            Place::Packed(addr) => (addr, true),
                            Place::Bits { .. } => return self.trap(e.span, "internal error: index of a #bits field"),
                        };
                        let i = self.eval_index(a, index)?;
                        if i >= *len {
                            return self.trap(e.span, format!("index {} out of bounds for length {}", i, len));
                        }
                        Ok(if packed { Place::Packed(addr + i * esz) } else { Place::Mem(addr + i * esz) })
                    }
                    TyKind::Slice { .. } => {
                        let sv = self.eval_bytes(a, base)?;
                        let (p, n) = self.slice_parts(&sv);
                        let i = self.eval_index(a, index)?;
                        if i >= n {
                            return self.trap(e.span, format!("index {} out of bounds for length {}", i, n));
                        }
                        Ok(Place::Mem(p.wrapping_add(i * esz)))
                    }
                    TyKind::ManyPtr { .. } => {
                        let pv = self.eval_bytes(a, base)?;
                        let p = self.ptr_of(&pv);
                        let i = self.eval_index(a, index)?;
                        Ok(Place::Mem(p.wrapping_add(i.wrapping_mul(esz))))
                    }
                    TyKind::Ptr { pointee, .. } if matches!(pointee.kind(), TyKind::Array { .. }) => {
                        let TyKind::Array { len, .. } = pointee.kind() else { unreachable!() };
                        let pv = self.eval_bytes(a, base)?;
                        let p = self.ptr_of(&pv);
                        let i = self.eval_index(a, index)?;
                        if i >= *len {
                            return self.trap(e.span, format!("index {} out of bounds for length {}", i, len));
                        }
                        Ok(Place::Mem(p.wrapping_add(i * esz)))
                    }
                    _ => self.trap(e.span, format!("cannot index {}", base.ty)),
                }
            }
            _ => {
                let b = self.eval_bytes(a, e)?;
                let align = self.ti(e.ty).align;
                let addr = self.alloc_temp(e.span, &b, align)?;
                Ok(Place::Mem(addr))
            }
        }
    }

    fn eval_index(&mut self, a: Act, index: &Expr) -> R<u64> {
        if let Some(si) = self.small_of(index.ty) {
            let v = self.eval_small(a, index, si)?;
            return Ok(if v < 0 { 0 } else { v as u64 });
        }
        let b = self.eval_bytes(a, index)?;
        Ok(self.int_of(index.ty, &b).to_u64_sat())
    }

    fn eval_slice(
        &mut self,
        a: Act,
        e: &Expr,
        base: &Expr,
        lo: &Option<Box<Expr>>,
        hi: &Option<Box<Expr>>,
        inclusive: bool,
    ) -> R {
        let esz = match e.ty.kind() {
            TyKind::Slice { elem, .. } => self.size_of(*elem) as u64,
            _ => return self.trap(e.span, "internal error: slice expression of a non-slice type"),
        };
        let (ptr, len): (u64, Option<u64>) = match base.ty.kind() {
            TyKind::Array { len, .. } => {
                let (Place::Mem(addr) | Place::Packed(addr)) = self.eval_place(a, base)? else {
                    return self.trap(e.span, "internal error: slice of a #bits field");
                };
                (addr, Some(*len))
            }
            TyKind::Slice { .. } => {
                let sv = self.eval_bytes(a, base)?;
                let (p, n) = self.slice_parts(&sv);
                (p, Some(n))
            }
            TyKind::ManyPtr { .. } => {
                let pv = self.eval_bytes(a, base)?;
                (self.ptr_of(&pv), None)
            }
            TyKind::Ptr { pointee, .. } => match pointee.kind() {
                TyKind::Array { len, .. } => {
                    let pv = self.eval_bytes(a, base)?;
                    (self.ptr_of(&pv), Some(*len))
                }
                _ => return self.trap(e.span, "cannot slice this pointer"),
            },
            _ => return self.trap(e.span, format!("cannot slice {}", base.ty)),
        };
        let lo_v = match lo {
            Some(x) => self.eval_index(a, x)?,
            None => 0,
        };
        let mut hi_v = match hi {
            Some(x) => self.eval_index(a, x)?,
            None => len.unwrap_or(0),
        };
        if inclusive {
            hi_v = hi_v.saturating_add(1);
        }
        if lo_v > hi_v || len.is_some_and(|n| hi_v > n) {
            let n = len.map_or("?".to_string(), |n| n.to_string());
            return self.trap(e.span, format!("slice bounds {}..{} invalid for length {}", lo_v, hi_v, n));
        }
        Ok(Value::Bytes(self.make_slice(ptr.wrapping_add(lo_v * esz), hi_v - lo_v)))
    }

    // --- aggregates ---------------------------------------------------------------------------------

    fn eval_aggregate(&mut self, a: Act, e: &Expr, items: &[Expr]) -> R {
        let mut vals = Vec::with_capacity(items.len());
        for it in items {
            vals.push(self.eval_bytes(a, it)?);
        }
        let size = self.size_of(e.ty);
        if let Some(u) = self.bits_struct_ty(e.ty) {
            let mut acc = Int::S(0);
            for (i, v) in vals.iter().enumerate() {
                let (off, fty) = self.tr(e.span, self.bits_field(e.ty, i))?;
                acc = acc.or(&self.bits_raw(fty, v).shl(off as u64));
            }
            return Ok(Value::Bytes(self.int_bytes(u, &acc)));
        }
        let mut out = Bytes::zeroed(size);
        if let Some(AdtDef { kind: AdtKind::CUnion { fields }, .. }) = self.adt(e.ty) {
            if let (Some(v), Some(it)) = (vals.first(), items.first()) {
                let _ = fields.iter().position(|f| f.ty == it.ty);
                out[..v.len()].copy_from_slice(v);
            }
            return Ok(Value::Bytes(out));
        }
        for (i, v) in vals.iter().enumerate() {
            let (_, off) = self.tr(e.span, self.field_layout(e.ty, i))?;
            out[off..off + v.len()].copy_from_slice(v);
        }
        Ok(Value::Bytes(out))
    }

    // --- coercions -------------------------------------------------------------------------------------

    fn eval_coerce(&mut self, a: Act, e: &Expr, kind: Coercion, inner: &Expr) -> R {
        let v = self.eval(a, inner)?;
        match kind {
            Coercion::FromNever => self.trap(e.span, "internal error: a `never` expression produced a value"),
            Coercion::FnToPtr => self.fn_to_ptr(a.cx, e.span, e.ty, v),
            _ => {
                let b = self.value_bytes(e.span, v)?;
                Ok(Value::Bytes(self.coerce_bytes(e.span, kind, inner.ty, e.ty, b)?))
            }
        }
    }

    /// The `FnToPtr` coercion of `v` to type `to`: between second-class function values
    /// and function pointers.
    pub fn fn_to_ptr(&self, cx: &Ctx, span: Span, to: Ty, v: Value) -> R {
        let to_second_class = matches!(to.kind(), TyKind::Fn(_));
        match v {
            Value::Closure(c) => {
                if to_second_class {
                    Ok(Value::Closure(c))
                } else {
                    Ok(Value::Bytes(self.ptr_bytes(self.fn_addr(c.func))))
                }
            }
            Value::Bytes(b) => {
                if to_second_class {
                    match self.fn_from_addr(self.ptr_of(&b)) {
                        Some(f) => Ok(Value::Closure(Closure { func: f, env: None, ctx: cx.clone() })),
                        None => self.trap(span, "call through an invalid function pointer"),
                    }
                } else {
                    Ok(Value::Bytes(b))
                }
            }
            other => Ok(other),
        }
    }

    /// Applies a coercion of plain data (not `FromNever` or `FnToPtr`) to the bytes `b` of a
    /// value of type `from`, giving a value of type `to`.
    pub fn coerce_bytes(&self, span: Span, kind: Coercion, from: Ty, to: Ty, b: Bytes) -> R<Bytes> {
        Ok(match kind {
            Coercion::IntWiden => {
                if let (Some(isi), Some(osi)) = (self.small_of(from), self.small_of(to)) {
                    let x = self.decode_small(isi, &b);
                    self.encode_small(osi, x)
                } else {
                    let x = self.int_of(from, &b);
                    self.int_bytes(to, &x)
                }
            }
            Coercion::FloatWiden => {
                let (TyKind::Float(sf), TyKind::Float(df)) = (from.kind(), to.kind()) else {
                    return self.trap(span, "internal error: float widening of non-floats");
                };
                self.float_bytes(*df, self.float_of(*sf, &b))
            }
            Coercion::WrapOptional => self.make_some(to, &b),
            Coercion::WrapOk => self.make_res(to, false, &b),
            Coercion::LiftErr { variant } => {
                if self.res_is_err(from, &b) {
                    let ev = self.res_payload(from, &b);
                    let (derr, _) = self.res_types(to);
                    let lifted = self.make_variant(derr, variant as usize, &ev);
                    self.make_res(to, true, &lifted)
                } else {
                    self.make_res(to, false, &self.res_payload(from, &b))
                }
            }
            Coercion::ArrayPtrToSlice => self.array_ptr_to_slice(from, &b),
            Coercion::InOptional => {
                if !self.opt_present(from, &b) {
                    self.make_none(to)
                } else {
                    let (src_in, dst_in) = (self.opt_inner(from), self.opt_inner(to));
                    let payload = self.opt_payload(from, &b);
                    let converted = match (src_in.kind(), dst_in.kind()) {
                        (TyKind::Ptr { pointee, .. }, TyKind::Slice { .. })
                            if matches!(pointee.kind(), TyKind::Array { .. }) =>
                        {
                            self.array_ptr_to_slice(src_in, &payload)
                        }
                        _ => payload,
                    };
                    self.make_some(to, &converted)
                }
            }
            Coercion::PtrToPtr | Coercion::TupleRename | Coercion::FnPtrWiden => b,
            Coercion::FromNever | Coercion::FnToPtr | Coercion::Unresolved => {
                return self.trap(span, "internal error: unresolved coercion reached the interpreter");
            }
        })
    }

    fn array_ptr_to_slice(&self, ptr_ty: Ty, b: &[u8]) -> Bytes {
        let len = match ptr_ty.kind() {
            TyKind::Ptr { pointee, .. } => match pointee.kind() {
                TyKind::Array { len, .. } => *len,
                _ => 0,
            },
            _ => 0,
        };
        self.make_slice(self.ptr_of(b), len)
    }

    // --- builtins ---------------------------------------------------------------------------------------

    fn eval_builtin(&mut self, a: Act, e: &Expr, op: Builtin, args: &[Expr]) -> R {
        match op {
            Builtin::Assert => {
                if self.fast() {
                    return Ok(Value::unit());
                }
                let c = self.eval_bytes(a, &args[0])?;
                if self.truthy(&c) {
                    // Remaining arguments (message, location) are only needed on failure.
                    return Ok(Value::unit());
                }
                let msg = match args.get(1) {
                    Some(m) => {
                        let mb = self.eval_bytes(a, m)?;
                        String::from_utf8_lossy(&self.read_str(e.span, &mb)?).into_owned()
                    }
                    None => "assertion failed".to_string(),
                };
                let msg = if msg == "assertion failed" { msg } else { format!("assertion failed: {}", msg) };
                self.trap(e.span, msg)
            }
            Builtin::Cover(id) => {
                cover_hit(id);
                Ok(Value::unit())
            }
            Builtin::Fiber => self.eval_fiber(a, e.span, args),
            Builtin::Sys(op) => self.eval_sys(a, e, op, args),
            Builtin::Get | Builtin::GetPtr | Builtin::First | Builtin::Last => {
                let (esz, elem_ty) = match e.ty.kind() {
                    TyKind::Optional(t) => match (op, t.kind()) {
                        (Builtin::GetPtr, TyKind::Ptr { pointee, .. }) => (self.size_of(*pointee), *pointee),
                        _ => (self.size_of(*t), *t),
                    },
                    _ => return self.trap(e.span, "internal error: get on a non-optional result"),
                };
                let _ = elem_ty;
                let base = &args[0];
                let (ptr, len, owned): (u64, u64, Option<Bytes>) = match base.ty.kind() {
                    TyKind::Slice { .. } => {
                        let sv = self.eval_bytes(a, base)?;
                        let (p, n) = self.slice_parts(&sv);
                        (p, n, None)
                    }
                    TyKind::Array { len, .. } => {
                        if op == Builtin::GetPtr || is_place_expr(base) {
                            let (Place::Mem(addr) | Place::Packed(addr)) = self.eval_place(a, base)? else {
                                return self.trap(e.span, "internal error: get on a #bits field");
                            };
                            (addr, *len, None)
                        } else {
                            (0, *len, Some(self.eval_bytes(a, base)?))
                        }
                    }
                    TyKind::Ptr { pointee, .. } => match pointee.kind() {
                        TyKind::Array { len, .. } => {
                            let pv = self.eval_bytes(a, base)?;
                            (self.ptr_of(&pv), *len, None)
                        }
                        _ => return self.trap(e.span, "get on a pointer"),
                    },
                    _ => return self.trap(e.span, format!("get on {}", base.ty)),
                };
                let i = match op {
                    Builtin::First => 0,
                    Builtin::Last => len.wrapping_sub(1),
                    _ => {
                        let ib = self.eval_bytes(a, &args[1])?;
                        self.int_of(args[1].ty, &ib).to_u64_sat()
                    }
                };
                if i >= len {
                    return Ok(Value::Bytes(self.make_none(e.ty)));
                }
                if op == Builtin::GetPtr {
                    let p = ptr.wrapping_add(i * esz as u64);
                    return Ok(Value::Bytes(self.make_some(e.ty, &self.ptr_bytes(p))));
                }
                let elem = match owned {
                    Some(b) => Bytes::from_slice(&b[i as usize * esz..(i as usize + 1) * esz]),
                    None => {
                        let align = self.ti(self.opt_inner(e.ty)).align;
                        self.load(e.span, ptr.wrapping_add(i * esz as u64), esz, align)?
                    }
                };
                Ok(Value::Bytes(self.make_some(e.ty, &elem)))
            }
            _ => {
                let mut vals = Vec::with_capacity(args.len());
                let mut tys = Vec::with_capacity(args.len());
                for x in args {
                    vals.push(self.eval_bytes(a, x)?);
                    tys.push(x.ty);
                }
                self.builtin_vals(e.span, op, e.ty, &tys, &vals)
            }
        }
    }

    /// `file:line:column` of an allocation site.
    fn site_text(&mut self, span: Span, site: &AllocSite) -> R<String> {
        let file = String::from_utf8_lossy(&self.read_str(span, &site.0)?).into_owned();
        Ok(format!("{}:{}:{}", file, site.1, site.2))
    }

    /// Applies a builtin whose arguments are all evaluated first (not `assert`, `expect`,
    /// `@fiber`, the `Sys` operations or `get`/`get_ptr`/`first`/`last`) to the argument
    /// values `v` of types `tys`.
    pub fn builtin_vals(&mut self, span: Span, op: Builtin, res_ty: Ty, tys: &[Ty], v: &[Bytes]) -> R {
        match op {
            Builtin::HeapAlloc => {
                let size = self.int_of(tys[0], &v[0]).to_u64_sat();
                let align = self.int_of(tys[1], &v[1]).to_u64_sat().max(1);
                if !align.is_power_of_two() {
                    return self.trap(span, format!("allocation alignment {} is not a power of two", align));
                }
                // An allocation the host cannot provide is a null result, not a trap.
                let Ok(addr) = self.mem.alloc(size, align, super::memory::AllocKind::Heap, true) else {
                    return Ok(Value::Bytes(self.make_none(res_ty)));
                };
                // Fresh storage is unspecified; Debug builds fill it with a pattern (§18.3).
                if self.mode == super::Mode::Debug && size > 0 {
                    let fill = vec![0xAAu8; size as usize];
                    self.mem.init(addr, &fill).map_err(|m| super::value::trap_flow(span, m.message()))?;
                }
                Ok(Value::Bytes(self.make_some(res_ty, &self.ptr_bytes(addr))))
            }
            Builtin::AllocTag | Builtin::AllocCheck => {
                if self.mode != super::Mode::Debug {
                    return Ok(Value::unit());
                }
                let (p, owner, vt) = (self.ptr_of(&v[0]), self.ptr_of(&v[1]), self.ptr_of(&v[2]));
                let line = self.int_of(tys[4], &v[4]).to_u64_sat();
                let col = self.int_of(tys[5], &v[5]).to_u64_sat();
                let site = (v[3].clone(), line, col);
                if op == Builtin::AllocTag {
                    self.alloc_tags.insert(p, (owner, vt, site));
                } else if let Some((o, t, from)) = self.alloc_tags.remove(&p)
                    && (o, t) != (owner, vt)
                {
                    let (from, site) = (self.site_text(span, &from)?, self.site_text(span, &site)?);
                    return self.trap(
                        span,
                        format!("memory allocated at {} is freed at {} through a different allocator", from, site),
                    );
                }
                Ok(Value::unit())
            }
            Builtin::HeapFree => {
                let p = self.ptr_of(&v[0]);
                let size = self.int_of(tys[1], &v[1]).to_u64_sat();
                match self.mem.describe(p) {
                    Some((base, sz, super::memory::AllocKind::Heap, true)) if base == p && sz == size => {
                        self.mem.kill(base);
                        Ok(Value::unit())
                    }
                    Some((_, _, super::memory::AllocKind::Heap, false)) => {
                        self.trap(span, format!("double free of heap block {:#x}", p))
                    }
                    // A whole block freed with the wrong size, such as a slice of it.
                    Some((base, sz, super::memory::AllocKind::Heap, true)) if base == p => self.trap(
                        span,
                        format!("invalid free of {:#x}: the heap block there has {} bytes, not {}", p, sz, size),
                    ),
                    _ => self.trap(
                        span,
                        format!("invalid free of {:#x}: not the start of a live heap block of {} bytes", p, size),
                    ),
                }
            }
            // Threads take turns under a global lock (see threads.rs), so atomics are plain memory accesses (§19.6).
            Builtin::AtomicLoad
            | Builtin::AtomicStore
            | Builtin::AtomicSwap
            | Builtin::AtomicCas
            | Builtin::AtomicRmw(_)
            | Builtin::VolatileLoad
            | Builtin::VolatileStore => {
                let vals = v;
                let addr = self.ptr_of(&vals[0]);
                let ty = match tys[0].kind() {
                    TyKind::Ptr { pointee, .. } => *pointee,
                    _ => return self.trap(span, "internal error: atomic access through a non-pointer"),
                };
                let align = self.ti(ty).align;
                let old = self.load_ty(span, addr, ty)?;
                match op {
                    Builtin::AtomicLoad | Builtin::VolatileLoad => Ok(Value::Bytes(old)),
                    Builtin::AtomicStore | Builtin::VolatileStore => {
                        self.store(span, addr, &vals[1], align)?;
                        Ok(Value::unit())
                    }
                    Builtin::AtomicSwap => {
                        self.store(span, addr, &vals[1], align)?;
                        Ok(Value::Bytes(old))
                    }
                    Builtin::AtomicCas => {
                        if self.tr(span, self.eq(ty, &old, &vals[1]))? {
                            self.store(span, addr, &vals[2], align)?;
                            Ok(Value::Bytes(self.make_none(res_ty)))
                        } else {
                            Ok(Value::Bytes(self.make_some(res_ty, &old)))
                        }
                    }
                    Builtin::AtomicRmw(rmw) => {
                        let (x, y) = (self.int_of(ty, &old), self.int_of(ty, &vals[1]));
                        let (signed, bits) = self.ti(ty).int.unwrap_or((false, 8 * old.len() as u32));
                        let r = match rmw {
                            RmwOp::Add => x.add(&y),
                            RmwOp::Sub => x.sub(&y),
                            RmwOp::And => x.and(&y),
                            RmwOp::Or => x.or(&y),
                            RmwOp::Xor => x.xor(&y),
                            RmwOp::Max => {
                                if y.cmp(&x) == std::cmp::Ordering::Greater {
                                    y
                                } else {
                                    x
                                }
                            }
                            RmwOp::Min => {
                                if y.cmp(&x) == std::cmp::Ordering::Less {
                                    y
                                } else {
                                    x
                                }
                            }
                        };
                        let new = self.int_bytes(ty, &super::num::wrap(&r, signed, bits));
                        self.store(span, addr, &new, align)?;
                        Ok(Value::Bytes(old))
                    }
                    _ => unreachable!(),
                }
            }
            Builtin::Fence => Ok(Value::unit()),
            _ => {
                if let Some(r) = self.vector_builtin(span, op, res_ty, tys, v) {
                    return Ok(Value::Bytes(r?));
                }
                Ok(Value::Bytes(self.builtin(span, op, res_ty, tys, v)?))
            }
        }
    }

    // --- membership ------------------------------------------------------------------------------------------

    pub fn contains(&self, elem_ty: Ty, x: &[u8], cty: Ty, c: &[u8]) -> Res<bool> {
        match cty.kind() {
            TyKind::Array { .. } | TyKind::Slice { .. } => {
                let (et, esz, data) = self.seq_elems(cty, c)?;
                let n = data.len().checked_div(esz).unwrap_or(0);
                for i in 0..n {
                    if self.eq(et, &data[i * esz..(i + 1) * esz], x)? {
                        return Ok(true);
                    }
                }
                Ok(false)
            }
            TyKind::Tuple { elems, .. } => {
                for i in 0..elems.len() {
                    let (ft, fb) = self.field_bytes(cty, c, i)?;
                    if self.eq(ft, &fb, x)? {
                        return Ok(true);
                    }
                }
                Ok(false)
            }
            TyKind::Adt(..) => {
                let def = self.adt(cty).ok_or("unknown container type")?;
                let incl = match def.name.as_str() {
                    "Range" => false,
                    "RangeIncl" => true,
                    other => return Err(format!("`in` is not defined for {}", other)),
                };
                let (lt, lo) = self.field_bytes(cty, c, 0)?;
                let (_, hi) = self.field_bytes(cty, c, 1)?;
                let ge_lo = !self.less(lt, x, &lo)?;
                let below = if incl { !self.less(lt, &hi, x)? } else { self.less(lt, x, &hi)? };
                let _ = elem_ty;
                Ok(ge_lo && below)
            }
            _ => Err(format!("`in` is not defined for {}", cty)),
        }
    }

    // --- globals -----------------------------------------------------------------------------------------------

    pub fn global_addr(&mut self, span: Span, g: GlobalId) -> R<u64> {
        if self.const_mode {
            return self.trap(span, "a #global cannot be accessed at compile time");
        }
        let prog = self.prog;
        let def = &prog.globals[g.index()];
        let key = (g, if def.thread_local { self.thread_id } else { 0 });
        if let Some(a) = self.globals.get(&key) {
            return Ok(*a);
        }
        let Some(init) = &def.init else {
            let name = def.link_name.map_or_else(|| def.name.as_str().to_string(), |n| n.as_str().to_string());
            let addr = self.foreign_global_addr(span, &name)?;
            self.globals.insert(key, addr);
            return Ok(addr);
        };
        let bytes = self.with_root_frame(def.ty, |m, act| m.eval_bytes(act, init))?;
        let ti = self.ti(def.ty);
        let align = ti.align.max(def.align.unwrap_or(1));
        let addr = self.alloc_mem(span, ti.size, align, AllocKind::Global(g.0), true)?;
        let r = self.mem.init(addr, &bytes);
        self.mem_err(span, r)?;
        self.globals.insert(key, addr);
        Ok(addr)
    }

    /// Runs `f` in a fresh root frame with an empty handler context.
    pub fn with_root_frame<T>(&mut self, ret_ty: Ty, f: impl FnOnce(&mut Self, Act) -> R<T>) -> R<T> {
        let fr = self.new_frame(ret_ty, None, None, 0, None, false);
        let cx = Ctx::empty();
        let mark = self.temps.len();
        let r = f(self, Act { fr: &fr, cx: &cx });
        self.kill_temps(mark);
        self.kill_frame(&fr);
        r
    }
}

use std::rc::Rc;

fn is_cmp(op: BinOp) -> bool {
    matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge)
}

pub fn is_place_expr(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Global(_) | ExprKind::Deref(_) => true,
        ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => is_place_expr(base),
        ExprKind::Index { base, .. } => match base.ty.kind() {
            TyKind::Array { .. } => is_place_expr(base),
            _ => true,
        },
        _ => false,
    }
}

#[allow(dead_code)]
fn _unused(_: &dyn Fn()) {
    let _ = layout::is_nullable_pointer;
}
