//! Expressions, places, coercions, calls, effects, fibers and pattern matching.
//!
//! [`FnGen::expr`] emits the code of an expression and returns its value: a scalar in a
//! register, or the address of a temporary the expression owns. Operands are evaluated in
//! order, so evaluation order is the program's (§7.1).

use super::cabi::{CArg, CRet, CSig, parts_size};
use super::func::{BREAK, CONTINUE, EXIT, ExitFrame, ExitLabel, FnGen, Loc, RESUME, RETURN, Scope, V, mf};
use super::{CResult, Rep, Work, unsupported};
use crate::source::Span;
use crate::tir::{
    AdtKind, Arm, Callee, Coercion, CondPart, Expr, ExprKind, FnId, FnKind, HandlerValue, LabelId, Lift, Pat, PatKind,
    Target, WriteSink,
};
use crate::types::{CallConv, EffectRef, Ty, TyKind};
use cranelift_codegen::ir::condcodes::{FloatCC, IntCC};
use cranelift_codegen::ir::{self, InstBuilder, Signature, Value, types};
use cranelift_frontend::Variable;

/// The ambient effects whose writes the runtime handles natively.
const NATIVE: u8 = 2;

impl<'f, 'g, 'a> FnGen<'f, 'g, 'a> {
    // --- values ---------------------------------------------------------------------------------

    /// Evaluates `e`.
    pub fn expr(&mut self, e: &'a Expr) -> CResult<V> {
        let v = self.expr_node(e)?;
        if e.ty.is_never() {
            return Ok(V::Z);
        }
        Ok(v)
    }

    /// Evaluates a scalar expression.
    pub fn expr_s(&mut self, e: &'a Expr) -> CResult<Value> {
        let v = self.expr(e)?;
        Ok(self.as_scalar(e.ty, v))
    }

    /// The scalar of `v`, of type `t`.
    pub fn as_scalar(&mut self, t: Ty, v: V) -> Value {
        match v {
            V::S(x) => x,
            V::M(a) => self.load_s(t, a, 0),
            V::Z => {
                let ty = self.g.rep(t).ty().unwrap_or(types::I64);
                self.zero_scalar(ty)
            }
        }
    }

    /// Evaluates `e` and returns the address of its value.
    pub fn expr_m(&mut self, e: &'a Expr) -> CResult<Value> {
        let v = self.expr(e)?;
        Ok(self.spill(e.ty, v))
    }

    /// Evaluates `e` into memory at `dst`, which has its type. When `fresh`, nothing can
    /// observe `dst` while `e` is evaluated, so an aggregate is built there element by
    /// element; otherwise `dst` is written once `e` has been evaluated, as an assignment is.
    pub fn expr_into(&mut self, e: &'a Expr, dst: Value, fresh: bool) -> CResult<()> {
        match &e.kind {
            ExprKind::Call { callee, args } if matches!(self.g.rep(e.ty), Rep::Mem { .. }) => {
                // The callee writes its result last, after reading its arguments.
                self.call_expr(e, callee, args, Some(dst))?;
            }
            ExprKind::Aggregate(elems) | ExprKind::Array(elems)
                if matches!(self.g.rep(e.ty), Rep::Mem { .. }) && self.g.bits_layout(e.ty).is_none() =>
            {
                let offs = self.elem_offsets(e);
                let size = self.g.size(e.ty);
                if fresh {
                    let parts: Vec<(u64, u64)> =
                        elems.iter().zip(&offs).map(|(x, o)| (*o, self.g.size(x.ty))).collect();
                    self.zero_gaps(dst, size, &parts);
                    for (x, off) in elems.iter().zip(&offs) {
                        let p = self.add(dst, *off);
                        self.expr_into(x, p, true)?;
                    }
                } else {
                    let mut vals = vec![];
                    for x in elems {
                        vals.push(self.expr(x)?);
                    }
                    let parts: Vec<(u64, u64)> =
                        elems.iter().zip(&offs).map(|(x, o)| (*o, self.g.size(x.ty))).collect();
                    self.zero_gaps(dst, size, &parts);
                    for ((x, v), off) in elems.iter().zip(vals).zip(&offs) {
                        let p = self.add(dst, *off);
                        self.write(x.ty, v, p);
                    }
                }
            }
            ExprKind::Block(b) if b.tail.is_some() && !e.ty.is_never() => self.block_into(b, dst, fresh)?,
            ExprKind::ArrayFill(x) if matches!(self.g.rep(e.ty), Rep::Mem { .. }) => {
                let v = self.expr(x)?;
                if !x.ty.is_never() {
                    self.fill_array(e, v, dst)?;
                }
            }
            // A large value read from a place is copied straight from it.
            ExprKind::Local(_)
            | ExprKind::Upvar { .. }
            | ExprKind::Deref(_)
            | ExprKind::Field { .. }
            | ExprKind::VariantPayload { .. }
            | ExprKind::Index { .. }
            | ExprKind::Global(_)
            | ExprKind::Const(_)
                if fresh && matches!(self.g.rep(e.ty), Rep::Mem { .. }) =>
            {
                let a = self.place(e)?;
                let size = self.g.size(e.ty);
                self.copy(dst, a, size);
            }
            _ => {
                let v = self.expr(e)?;
                if !e.ty.is_never() {
                    self.write(e.ty, v, dst);
                }
            }
        }
        Ok(())
    }

    /// Stores `v` in each element of the array of type `e.ty` at `dst`.
    fn fill_array(&mut self, e: &Expr, v: V, dst: Value) -> CResult<()> {
        let TyKind::Array { len, elem, .. } = e.ty.kind() else { return unsupported(e.span, "this array fill") };
        let (len, elem) = (*len, *elem);
        let esz = self.g.size(elem);
        if len <= 16 {
            for i in 0..len {
                let p = self.add(dst, i * esz);
                self.write(elem, v, p);
            }
            return Ok(());
        }
        self.counted_loop(len, |fg, i| {
            let off = fg.b.ins().imul_imm_s(i, esz as i64);
            let p = fg.b.ins().iadd(dst, off);
            fg.write(elem, v, p);
            Ok(())
        })
    }

    /// The offsets of the elements of an aggregate or array expression.
    fn elem_offsets(&mut self, e: &Expr) -> Vec<u64> {
        match &e.kind {
            ExprKind::Aggregate(_) => self.g.offsets(e.ty),
            ExprKind::Array(elems) => elems.iter().enumerate().map(|(i, x)| i as u64 * self.g.size(x.ty)).collect(),
            _ => vec![],
        }
    }

    /// Fills the bytes of `[dst, dst + size)` outside `parts` (offset and length of each
    /// piece about to be stored) with zeros: padding is zero, as in the interpreter.
    pub fn zero_gaps(&mut self, dst: Value, size: u64, parts: &[(u64, u64)]) {
        let mut covered = vec![false; size as usize];
        for &(off, len) in parts {
            for c in covered.iter_mut().skip(off as usize).take(len as usize) {
                *c = true;
            }
        }
        let gaps = covered.iter().filter(|c| !**c).count();
        if gaps > 64 {
            self.zero(dst, 0, size);
            return;
        }
        let mut i = 0;
        while i < covered.len() {
            if covered[i] {
                i += 1;
                continue;
            }
            let mut j = i;
            while j < covered.len() && !covered[j] {
                j += 1;
            }
            self.zero(dst, i as u64, (j - i) as u64);
            i = j;
        }
    }

    /// The integer of type `t` (or float, bool or pointer) with value `v`.
    pub fn int_lit(&mut self, t: Ty, v: &num_bigint::BigInt) -> Value {
        match self.g.rep(t) {
            Rep::F32 => self.b.ins().f32const(super::ops::bigint_f64(v) as f32),
            Rep::F64 => self.b.ins().f64const(super::ops::bigint_f64(v)),
            Rep::F16 => {
                let bits = super::ops::f16_bits(super::ops::bigint_f64(v));
                self.b.ins().iconst(types::I16, bits as i64)
            }
            Rep::Bool => {
                let nz = *v != num_bigint::BigInt::from(0);
                self.b.ins().iconst(types::I8, nz as i64)
            }
            Rep::Int { ty, .. } => self.iconst_bits(ty, super::ops::bigint_bits(v)),
            _ => self.iconst_bits(types::I64, super::ops::bigint_bits(v)),
        }
    }

    /// A slice value `{ptr, len}` of type `t`.
    pub fn make_slice(&mut self, t: Ty, ptr: Value, len: Value) -> V {
        let a = self.temp(t);
        self.b.ins().store(mf(), ptr, a, 0);
        self.b.ins().store(mf(), len, a, 8);
        V::M(a)
    }

    pub fn slice_parts(&mut self, a: Value) -> (Value, Value) {
        let p = self.b.ins().load(types::I64, mf(), a, 0);
        let l = self.b.ins().load(types::I64, mf(), a, 8);
        (p, l)
    }

    // --- optionals, results, unions --------------------------------------------------------------

    /// Whether the optional of type `t` at `addr` is present.
    pub fn opt_present(&mut self, t: Ty, addr: Value) -> Value {
        if self.g.rep(t) == Rep::Ptr {
            let p = self.b.ins().load(types::I64, mf(), addr, 0);
            self.b.ins().icmp_imm_s(IntCC::NotEqual, p, 0)
        } else {
            let off = self.g.offsets(t)[1];
            let f = self.b.ins().load(types::I8, mf(), addr, off as i32);
            self.b.ins().icmp_imm_s(IntCC::NotEqual, f, 0)
        }
    }

    pub fn make_some(&mut self, t: Ty, payload: V) -> V {
        if self.g.rep(t) == Rep::Ptr {
            return payload;
        }
        let TyKind::Optional(inner) = t.kind() else { unreachable!() };
        let inner = *inner;
        let off = self.g.offsets(t)[1];
        let a = self.temp(t);
        let size = self.g.size(t);
        let isz = self.g.size(inner);
        self.zero_gaps(a, size, &[(0, isz), (off, 1)]);
        self.write(inner, payload, a);
        let one = self.b.ins().iconst(types::I8, 1);
        self.b.ins().store(mf(), one, a, off as i32);
        V::M(a)
    }

    pub fn make_none(&mut self, t: Ty) -> V {
        self.zeroed(t)
    }

    /// Whether the result of type `t` at `addr` holds an error.
    pub fn res_is_err(&mut self, t: Ty, addr: Value) -> Value {
        let off = self.g.offsets(t)[0];
        let f = self.b.ins().load(types::I8, mf(), addr, off as i32);
        self.b.ins().icmp_imm_s(IntCC::NotEqual, f, 0)
    }

    pub fn make_res(&mut self, t: Ty, is_err: bool, payload_ty: Ty, payload: V) -> V {
        let offs = self.g.offsets(t);
        let a = self.temp(t);
        let size = self.g.size(t);
        let psz = self.g.size(payload_ty);
        self.zero_gaps(a, size, &[(offs[0], 1), (offs[1], psz)]);
        let tag = self.b.ins().iconst(types::I8, is_err as i64);
        self.b.ins().store(mf(), tag, a, offs[0] as i32);
        let p = self.add(a, offs[1]);
        self.write(payload_ty, payload, p);
        V::M(a)
    }

    pub fn adt_def(&self, t: Ty) -> &'a crate::tir::AdtDef {
        let TyKind::Adt(id, _) = t.kind() else { panic!("not a nominal type: {}", t) };
        &self.g.prog.adts[id]
    }

    /// A union value of variant `v` with an optional payload.
    pub fn make_variant(&mut self, t: Ty, v: usize, payload: Option<V>) -> V {
        let def = self.adt_def(t);
        let AdtKind::Union { tag, variants } = &def.kind else { unreachable!() };
        let (tag, variant) = (*tag, &variants[v]);
        let offs = self.g.offsets(t);
        let mut parts = vec![(offs[0], self.g.size(tag))];
        if let (Some(_), Some(pt)) = (payload, variant.payload) {
            parts.push((offs[1], self.g.size(pt)));
        }
        let a = self.temp(t);
        let size = self.g.size(t);
        self.zero_gaps(a, size, &parts);
        let tv = self.int_lit(tag, &variant.tag);
        self.b.ins().store(mf(), tv, a, offs[0] as i32);
        if let (Some(p), Some(pt)) = (payload, variant.payload) {
            let pa = self.add(a, offs[1]);
            self.write(pt, p, pa);
        }
        V::M(a)
    }

    // --- expressions ------------------------------------------------------------------------------

    fn expr_node(&mut self, e: &'a Expr) -> CResult<V> {
        let t = e.ty;
        match &e.kind {
            ExprKind::Int(v) => Ok(V::S(self.int_lit(t, v))),
            ExprKind::Float(x) => Ok(match self.g.rep(t) {
                Rep::F16 => V::S(self.b.ins().iconst(types::I16, super::ops::f16_bits(*x) as i64)),
                Rep::F32 => V::S(self.b.ins().f32const(*x as f32)),
                Rep::F64 => V::S(self.b.ins().f64const(*x)),
                _ => return unsupported(e.span, "a float literal of a non-float type"),
            }),
            ExprKind::Bool(b) => Ok(V::S(self.b.ins().iconst(types::I8, *b as i64))),
            ExprKind::Str(s) => {
                let p = self.cstr(s)?;
                let n = self.b.ins().iconst(types::I64, s.len() as i64);
                Ok(self.make_slice(t, p, n))
            }
            ExprKind::CStr(s) => Ok(V::S(self.cstr(s)?)),
            ExprKind::Null => Ok(self.make_none(t)),
            ExprKind::ConstBytes(b) => {
                let a = self.cstr(b)?;
                Ok(self.read(t, a))
            }
            ExprKind::Uninit => {
                if self.g.opts.debug {
                    let a = self.temp(t);
                    let n = self.g.size(t);
                    self.fill(a, 0xAA, n);
                    Ok(self.read(t, a))
                } else {
                    Ok(self.undef(t))
                }
            }
            ExprKind::Const(id) => {
                let d = self.g.const_data(id.0)?;
                let a = self.data_addr(d);
                Ok(self.read(t, a))
            }
            // A field of a `#bits` struct is a range of bits of its integer (§5.10.5).
            ExprKind::Field { base, index } if self.g.bits_layout(base.ty).is_some() => {
                let (u, fields) = self.g.bits_layout(base.ty).unwrap();
                let (off, w, fty) = fields[*index as usize];
                let a = self.expr_m(base)?;
                let raw = self.load_s(u, a, 0);
                let raw = self.widen_i128(u, raw, false);
                let sh = self.b.ins().ushr_imm_s(raw, off as i64);
                let m = self.mask128(w);
                let raw = self.b.ins().band(sh, m);
                Ok(self.bits_value(fty, raw))
            }
            ExprKind::Local(l) if matches!(self.locals[l.index()], Loc::Var(_) | Loc::None) => self.get_local(*l),
            ExprKind::Local(_)
            | ExprKind::Upvar { .. }
            | ExprKind::Deref(_)
            | ExprKind::Field { .. }
            | ExprKind::VariantPayload { .. }
            | ExprKind::Index { .. }
            | ExprKind::Global(_) => {
                let a = self.place(e)?;
                Ok(self.read(t, a))
            }
            ExprKind::Slice { base, lo, hi, inclusive } => {
                self.slice_expr(e, base, lo.as_deref(), hi.as_deref(), *inclusive)
            }
            ExprKind::SliceLen(x) => {
                let a = self.place(x)?;
                Ok(V::S(self.b.ins().load(types::I64, mf(), a, 8)))
            }
            ExprKind::SlicePtr(x) => {
                let a = self.place(x)?;
                Ok(V::S(self.b.ins().load(types::I64, mf(), a, 0)))
            }
            ExprKind::AddrOf(p) => Ok(V::S(self.place(p)?)),
            ExprKind::AddrOfTemp(x) => Ok(V::S(self.expr_m(x)?)),
            ExprKind::FnRef(f) => self.fn_ref(*f, t),
            ExprKind::Closure(f) => {
                let id = self.g.func(*f)?;
                let fr = self.fref(id);
                let code = self.b.ins().func_addr(types::I64, fr);
                let a = self.temp(t);
                self.b.ins().store(mf(), code, a, 0);
                self.b.ins().store(mf(), self.fptr, a, 8);
                self.b.ins().store(mf(), self.cx, a, 16);
                Ok(V::M(a))
            }
            ExprKind::HandlerRef(_) => {
                let a = self.temp(t);
                self.b.ins().store(mf(), self.fptr, a, 0);
                self.b.ins().store(mf(), self.cx, a, 8);
                Ok(V::M(a))
            }
            ExprKind::Unary { op, operand } if operand.ty.is_vector() => self.vector_unary(e, *op, operand),
            ExprKind::Binary { op, lhs, rhs } if lhs.ty.is_vector() => self.vector_binary(e, *op, lhs, rhs),
            ExprKind::Unary { op, operand } => self.unary(e, *op, operand),
            ExprKind::Binary { op, lhs, rhs } => self.binary(e, *op, lhs, rhs),
            ExprKind::Logic { is_and, lhs, rhs } => {
                let var = self.b.declare_var(types::I8);
                let l = self.expr_s(lhs)?;
                self.b.def_var(var, l);
                let c = if *is_and { l } else { self.b.ins().bxor_imm_s(l, 1) };
                self.if_then(c, |fg| {
                    let r = fg.expr_s(rhs)?;
                    fg.b.def_var(var, r);
                    Ok(())
                })?;
                Ok(V::S(self.b.use_var(var)))
            }
            ExprKind::Coerce { kind, expr } => self.coerce(e, *kind, expr),
            ExprKind::Builtin { op, args } => self.builtin(e, *op, args),
            ExprKind::Call { callee, args } => self.call_expr(e, callee, args, None),
            ExprKind::Aggregate(elems) if self.g.bits_layout(t).is_some() => self.bits_aggregate(t, elems),
            ExprKind::Aggregate(_) | ExprKind::Array(_) => match self.g.rep(t) {
                Rep::Mem { .. } => {
                    let a = self.temp(t);
                    self.expr_into(e, a, true)?;
                    Ok(V::M(a))
                }
                Rep::Zero => {
                    if let ExprKind::Aggregate(xs) | ExprKind::Array(xs) = &e.kind {
                        for x in xs {
                            self.expr(x)?;
                        }
                    }
                    Ok(V::Z)
                }
                _ => {
                    // A one-element aggregate held in a register.
                    let (ExprKind::Aggregate(xs) | ExprKind::Array(xs)) = &e.kind else { unreachable!() };
                    let a = self.temp(t);
                    let size = self.g.size(t);
                    self.zero(a, 0, size);
                    let offs = self.elem_offsets(e);
                    for (x, off) in xs.iter().zip(offs) {
                        let v = self.expr(x)?;
                        let p = self.add(a, off);
                        self.write(x.ty, v, p);
                    }
                    Ok(self.read(t, a))
                }
            },
            ExprKind::ArrayFill(x) => {
                let v = self.expr(x)?;
                let a = match self.g.rep(t) {
                    Rep::Zero => return Ok(V::Z),
                    _ => self.temp(t),
                };
                self.fill_array(e, v, a)?;
                Ok(self.read_owned(t, a))
            }
            ExprKind::Variant { variant, payload } => {
                let p = match payload {
                    Some(p) => Some(self.expr(p)?),
                    None => None,
                };
                Ok(self.make_variant(t, *variant as usize, p))
            }
            ExprKind::MakeErr(x) => {
                let v = self.expr(x)?;
                Ok(self.make_res(t, true, x.ty, v))
            }
            ExprKind::Unwrap { expr, is_result } => {
                let a = self.expr_m(expr)?;
                if *is_result {
                    let TyKind::Result { err, .. } = expr.ty.kind() else { unreachable!() };
                    let err = *err;
                    let off = self.g.offsets(expr.ty)[1];
                    if self.g.opts.checks {
                        let bad = self.res_is_err(expr.ty, a);
                        let span = e.span;
                        self.cold_if(bad, |fg| {
                            let d = fg.g.desc(err)?;
                            let d = fg.data_addr(d);
                            let p = fg.add(a, off);
                            let [f, l, c] = fg.loc(span)?;
                            fg.rt_call("gm_clif_trap_unwrap", "pppii", &[d, p, f, l, c]);
                            Ok(())
                        })?;
                    }
                    let p = self.add(a, off);
                    Ok(self.read(t, p))
                } else {
                    if self.g.opts.checks {
                        let present = self.opt_present(expr.ty, a);
                        let absent = self.b.ins().bxor_imm_s(present, 1);
                        self.trap_if(absent, "unwrap of null", e.span)?;
                    }
                    Ok(self.read(t, a))
                }
            }
            ExprKind::Eq { lhs, rhs, negated } => {
                let l = self.expr(lhs)?;
                let r = self.expr(rhs)?;
                let c = self.equal(lhs.ty, l, r)?;
                Ok(V::S(if *negated { self.b.ins().bxor_imm_s(c, 1) } else { c }))
            }
            ExprKind::In { elem, container, negated } => self.in_expr(elem, container, *negated),
            ExprKind::Block(b) => self.block(b, t),
            ExprKind::If { cond, then, else_ } => {
                let res = self.result_holder(t);
                let els = self.b.create_block();
                let end = self.b.create_block();
                self.conds(cond, els)?;
                let v = self.expr(then)?;
                self.hold(&res, then.ty, v);
                self.goto(end);
                self.switch(els);
                if let Some(x) = else_ {
                    let v = self.expr(x)?;
                    self.hold(&res, x.ty, v);
                }
                self.switch(end);
                Ok(self.held(res))
            }
            ExprKind::Match { scrutinee, arms } => self.match_expr(e, scrutinee, arms),
            ExprKind::Else { lhs, capture, rhs } => self.else_expr(e, lhs, *capture, rhs),
            ExprKind::Try { expr, lift, levels } => self.try_expr(e, expr, *lift, *levels),
            ExprKind::Loop { label, cond, body, step } => {
                let head = self.b.create_block();
                let brk = self.b.create_block();
                let cont = self.b.create_block();
                self.switch(head);
                self.conds(cond, brk)?;
                self.scopes.push(Scope::Loop { label: *label, brk, cont, rung: None });
                let r = self.block(body, Ty::unit());
                let scope = self.scopes.pop().unwrap();
                r?;
                self.switch(cont);
                if let Some(s) = step {
                    self.expr(s)?;
                }
                self.goto(head);
                self.end_scope(scope);
                self.switch(brk);
                Ok(V::Z)
            }
            ExprKind::Break { target, value } => {
                let v = match value {
                    Some(x) => Some((self.expr(x)?, x.ty)),
                    None => None,
                };
                match target {
                    Target::Label { levels, label } => {
                        let (fr, f) = self.frame_up(*levels);
                        if let Some((v, vt)) = v
                            && !vt.is_never()
                            && let Some(&off) = self.g.frame(f).withs.get(label)
                        {
                            let slot = self.add(fr, off);
                            self.write(vt, v, slot);
                        }
                        let frame = if *levels == 0 { ExitFrame::Own } else { ExitFrame::At(fr) };
                        self.start_exit(BREAK, frame, ExitLabel::Static(label.0), None);
                    }
                    Target::InstallingWith => {
                        if let Some((v, vt)) = v
                            && !vt.is_unit()
                            && !vt.is_never()
                        {
                            let r = self.b.ins().load(types::I64, mf(), self.inst, 48);
                            self.write(vt, v, r);
                        }
                        let fr = self.b.ins().load(types::I64, mf(), self.inst, 32);
                        let l = self.b.ins().load(types::I32, mf(), self.inst, 40);
                        self.start_exit(BREAK, ExitFrame::At(fr), ExitLabel::At(l), None);
                    }
                }
                Ok(V::Z)
            }
            ExprKind::Continue { target } => {
                match target {
                    Target::Label { levels, label } => {
                        let (fr, _) = self.frame_up(*levels);
                        let frame = if *levels == 0 { ExitFrame::Own } else { ExitFrame::At(fr) };
                        self.start_exit(CONTINUE, frame, ExitLabel::Static(label.0), None);
                    }
                    // In a clause, `continue` ends the clause (the next iteration of a `for`).
                    Target::InstallingWith => {
                        self.start_exit(RETURN, ExitFrame::Own, ExitLabel::Static(0), None);
                    }
                }
                Ok(V::Z)
            }
            ExprKind::Return { levels, value } => {
                let (fr, f) = self.frame_up(*levels);
                let layout = self.g.frame(f);
                let target_ret = self.g.prog.fns[f.index()].ret;
                let slot = self.add(fr, layout.ret);
                if value.ty.is_never() {
                    self.expr(value)?;
                } else {
                    self.expr_into(value, slot, true)?;
                }
                let is_err = if matches!(target_ret.kind(), TyKind::Result { .. }) && !value.ty.is_never() {
                    let off = self.g.offsets(target_ret)[0];
                    Some(self.b.ins().load(types::I8, mf(), slot, off as i32))
                } else {
                    None
                };
                let frame = if *levels == 0 { ExitFrame::Own } else { ExitFrame::At(fr) };
                self.start_exit(RETURN, frame, ExitLabel::Static(0), is_err);
                Ok(V::Z)
            }
            ExprKind::With { label, handlers, on, body } => self.with_expr(e, *label, handlers, on.as_deref(), body),
            ExprKind::Resume { cont, value } => {
                let k = self.expr_m(cont)?;
                let v = self.expr(value)?;
                let fw = self.b.ins().load(types::I64, mf(), self.inst, 56);
                let [f, l, c] = self.loc(e.span)?;
                let rp = self.rt_call("gm_resume", "pppii>p", &[fw, k, f, l, c])[0];
                if !value.ty.is_unit() && !value.ty.is_never() {
                    self.write(value.ty, v, rp);
                }
                self.start_exit(RESUME, ExitFrame::Own, ExitLabel::Static(0), None);
                Ok(V::Z)
            }
            ExprKind::Cancel(k) => {
                let ka = self.expr_m(k)?;
                let fw = self.b.ins().load(types::I64, mf(), self.inst, 56);
                let [f, l, c] = self.loc(e.span)?;
                self.rt_call("gm_cancel_cont", "pppii", &[fw, ka, f, l, c]);
                self.check_unwind();
                Ok(V::Z)
            }
            ExprKind::Trap { message } => {
                let m = self.expr_m(message)?;
                let (p, n) = self.slice_parts(m);
                let [f, l, c] = self.loc(e.span)?;
                self.rt_call("gm_trap_bytes", "plpii", &[p, n, f, l, c]);
                self.unreachable();
                Ok(V::Z)
            }
            ExprKind::Write { sink, pieces, specs, args } => self.write_expr(e, sink, pieces, specs, args),
            ExprKind::WriteDeferred { .. } => {
                unsupported(e.span, "a template in a generic function that was not instantiated")
            }
            ExprKind::SizeOf { .. }
            | ExprKind::TypeIs { .. }
            | ExprKind::IntLimit { .. }
            | ExprKind::TypeName { .. }
            | ExprKind::UntypedFloat(_)
            | ExprKind::ValueParam(_) => unsupported(e.span, "an expression left over from generic checking"),
        }
    }

    /// The value in the temporary `a` of type `t`, which the expression owns.
    pub fn read_owned(&mut self, t: Ty, a: Value) -> V {
        match self.g.rep(t) {
            Rep::Mem { .. } => V::M(a),
            _ => self.read(t, a),
        }
    }

    /// Runs `body` with `i` from 0 to `n - 1`.
    pub fn counted_loop(&mut self, n: u64, body: impl FnOnce(&mut Self, Value) -> CResult<()>) -> CResult<()> {
        let nv = self.b.ins().iconst(types::I64, n as i64);
        self.dyn_loop(nv, |fg, i| {
            body(fg, i)?;
            Ok(None)
        })
    }

    /// Runs `body` with `i` from 0 while `i < n` (unsigned); `body` may return a condition
    /// that ends the loop early.
    pub fn dyn_loop(&mut self, n: Value, body: impl FnOnce(&mut Self, Value) -> CResult<Option<Value>>) -> CResult<()> {
        let var = self.b.declare_var(types::I64);
        let z = self.b.ins().iconst(types::I64, 0);
        self.b.def_var(var, z);
        let head = self.b.create_block();
        let bodyb = self.b.create_block();
        let exit = self.b.create_block();
        self.switch(head);
        let i = self.b.use_var(var);
        let c = self.b.ins().icmp(IntCC::UnsignedLessThan, i, n);
        self.b.ins().brif(c, bodyb, &[], exit, &[]);
        self.b.switch_to_block(bodyb);
        let stop = body(self, i)?;
        let next = self.b.ins().iadd_imm_s(i, 1);
        self.b.def_var(var, next);
        match stop {
            Some(s) => self.brif(s, exit, head),
            None => self.goto(head),
        }
        self.switch(exit);
        Ok(())
    }

    /// Where an `if` or `match` keeps its value: a variable for a scalar, a temporary for a
    /// value in memory, created before branching.
    fn result_holder(&mut self, t: Ty) -> Holder {
        if t.is_never() {
            return Holder::None;
        }
        match self.g.rep(t) {
            Rep::Zero => Holder::None,
            Rep::Mem { .. } => Holder::Mem(t, self.temp(t)),
            r => {
                let var = self.b.declare_var(r.ty().unwrap());
                Holder::Var(t, var)
            }
        }
    }

    fn hold(&mut self, h: &Holder, vt: Ty, v: V) {
        if vt.is_never() {
            return;
        }
        match h {
            Holder::None => {}
            Holder::Mem(t, a) => self.write(*t, v, *a),
            Holder::Var(t, var) => {
                let x = self.as_scalar(*t, v);
                self.b.def_var(*var, x);
            }
        }
    }

    fn held(&mut self, h: Holder) -> V {
        match h {
            Holder::None => V::Z,
            Holder::Mem(_, a) => V::M(a),
            Holder::Var(_, var) => V::S(self.b.use_var(var)),
        }
    }

    // --- places ---------------------------------------------------------------------------------

    /// The address of a place, or of a temporary holding a value.
    pub fn place(&mut self, e: &'a Expr) -> CResult<Value> {
        match &e.kind {
            ExprKind::Local(l) => match self.locals[l.index()] {
                Loc::Var(_) => {
                    let v = self.get_local(*l)?;
                    Ok(self.spill(e.ty, v))
                }
                _ => self.local_addr(*l),
            },
            ExprKind::Global(id) => {
                let (d, tls) = self.g.global_data(id.0)?;
                Ok(if tls { self.tls_addr(d) } else { self.data_addr(d) })
            }
            // Constants live in static storage, so their address is stable.
            ExprKind::Const(id) => {
                let d = self.g.const_data(id.0)?;
                Ok(self.data_addr(d))
            }
            ExprKind::Upvar { levels, local } => {
                let (p, f) = self.frame_up(*levels);
                let layout = self.g.frame(f);
                match layout.locals[local.index()] {
                    Some(off) => Ok(self.add(p, off)),
                    None => Ok(self.slot(0, 1)),
                }
            }
            ExprKind::Deref(p) => self.expr_s(p),
            ExprKind::Field { base, index } => {
                if self.g.bits_layout(base.ty).is_some() {
                    let v = self.expr(e)?;
                    return Ok(self.spill(e.ty, v));
                }
                let b = self.place(base)?;
                let off = self.g.offsets(base.ty)[*index as usize];
                Ok(self.add(b, off))
            }
            ExprKind::VariantPayload { base, .. } => {
                let b = self.place(base)?;
                let off = self.g.offsets(base.ty)[1];
                Ok(self.add(b, off))
            }
            ExprKind::Index { base, index, checked } => self.index_addr(e, base, index, *checked),
            _ => self.expr_m(e),
        }
    }

    fn index_addr(&mut self, e: &'a Expr, base: &'a Expr, index: &'a Expr, checked: bool) -> CResult<Value> {
        // The base is evaluated before the index (§7.1): when the index has effects, the
        // base's address or value is computed first.
        let early = if no_writes(index) {
            None
        } else {
            Some(match base.ty.kind() {
                TyKind::Array { .. } => self.place(base)?,
                _ => self.expr_m(base)?,
            })
        };
        let i = self.expr_s(index)?;
        let i = self.as_index(index.ty, i);
        let span = e.span;
        let check = |fg: &mut Self, len: Value| -> CResult<()> {
            if checked {
                let bad = fg.b.ins().icmp(IntCC::UnsignedGreaterThanOrEqual, i, len);
                fg.cold_if(bad, |fg| {
                    let [f, l, c] = fg.loc(span)?;
                    fg.rt_call("gm_clif_trap_index", "llpii", &[i, len, f, l, c]);
                    Ok(())
                })?;
            }
            Ok(())
        };
        match base.ty.kind() {
            TyKind::Array { len, elem, .. } => {
                let (len, esz) = (*len, self.g.size(*elem));
                let b = match early {
                    Some(b) => b,
                    None => self.place(base)?,
                };
                let n = self.b.ins().iconst(types::I64, len as i64);
                check(self, n)?;
                let off = self.b.ins().imul_imm_s(i, esz as i64);
                Ok(self.b.ins().iadd(b, off))
            }
            TyKind::Slice { elem, .. } => {
                let esz = self.g.size(*elem);
                let a = match early {
                    Some(a) => a,
                    None => self.place(base)?,
                };
                let (p, len) = self.slice_parts(a);
                check(self, len)?;
                let off = self.b.ins().imul_imm_s(i, esz as i64);
                Ok(self.b.ins().iadd(p, off))
            }
            TyKind::ManyPtr { elem, .. } => {
                let esz = self.g.size(*elem);
                let p = match early {
                    Some(a) => self.b.ins().load(types::I64, mf(), a, 0),
                    None => self.expr_s(base)?,
                };
                let off = self.b.ins().imul_imm_s(i, esz as i64);
                Ok(self.b.ins().iadd(p, off))
            }
            _ => unsupported(e.span, format!("indexing `{}`", base.ty)),
        }
    }

    /// An index or length of integer type `t` as a `usize`.
    pub fn as_index(&mut self, t: Ty, v: Value) -> Value {
        match self.g.rep(t) {
            Rep::Int { ty, signed, .. } if ty.bits() < 64 => {
                if signed {
                    self.b.ins().sextend(types::I64, v)
                } else {
                    self.b.ins().uextend(types::I64, v)
                }
            }
            Rep::Int { ty, .. } if ty == types::I128 => self.b.ins().ireduce(types::I64, v),
            _ => v,
        }
    }

    fn slice_expr(
        &mut self,
        e: &'a Expr,
        base: &'a Expr,
        lo: Option<&'a Expr>,
        hi: Option<&'a Expr>,
        inclusive: bool,
    ) -> CResult<V> {
        let (ptr, len, esz) = match base.ty.kind() {
            TyKind::Array { len, elem, .. } => {
                let a = self.place(base)?;
                let n = self.b.ins().iconst(types::I64, *len as i64);
                (a, Some(n), self.g.size(*elem))
            }
            TyKind::Slice { elem, .. } => {
                // The slice is read in place when the bounds cannot change it; otherwise it
                // is copied before they are evaluated.
                let a = if lo.is_none_or(no_writes) && hi.is_none_or(no_writes) {
                    self.place(base)?
                } else {
                    self.expr_m(base)?
                };
                let (p, l) = self.slice_parts(a);
                (p, Some(l), self.g.size(*elem))
            }
            TyKind::ManyPtr { elem, .. } => {
                let v = self.expr_s(base)?;
                (v, None, self.g.size(*elem))
            }
            _ => return unsupported(e.span, format!("slicing `{}`", base.ty)),
        };
        let lo_v = match lo {
            Some(x) => {
                let v = self.expr_s(x)?;
                self.as_index(x.ty, v)
            }
            None => self.b.ins().iconst(types::I64, 0),
        };
        let hi_v = match hi {
            Some(x) => {
                let v = self.expr_s(x)?;
                let h = self.as_index(x.ty, v);
                if inclusive { self.b.ins().iadd_imm_s(h, 1) } else { h }
            }
            None => match len {
                Some(l) => l,
                None => return unsupported(e.span, "slicing a many-pointer without an upper bound"),
            },
        };
        if let Some(l) = len {
            let a = self.b.ins().icmp(IntCC::UnsignedGreaterThan, lo_v, hi_v);
            let b2 = self.b.ins().icmp(IntCC::UnsignedGreaterThan, hi_v, l);
            let bad = self.b.ins().bor(a, b2);
            let span = e.span;
            self.cold_if(bad, |fg| {
                let [f, ln, c] = fg.loc(span)?;
                fg.rt_call("gm_clif_trap_slice", "lllpii", &[lo_v, hi_v, l, f, ln, c]);
                Ok(())
            })?;
        }
        let off = self.b.ins().imul_imm_s(lo_v, esz as i64);
        let p = self.b.ins().iadd(ptr, off);
        let n = self.b.ins().isub(hi_v, lo_v);
        Ok(self.make_slice(e.ty, p, n))
    }

    // --- coercions ------------------------------------------------------------------------------

    fn coerce(&mut self, e: &'a Expr, kind: Coercion, x: &'a Expr) -> CResult<V> {
        let v = self.expr(x)?;
        let t = e.ty;
        if x.ty.is_never() && kind != Coercion::FromNever {
            return Ok(self.undef(t));
        }
        Ok(match kind {
            Coercion::IntWiden => {
                let s = self.as_scalar(x.ty, v);
                V::S(self.int_convert(x.ty, t, s))
            }
            Coercion::FloatWiden => {
                let s = self.as_scalar(x.ty, v);
                V::S(self.float_convert(x.ty, t, s))
            }
            Coercion::WrapOptional => self.make_some(t, v),
            Coercion::WrapOk => self.make_res(t, false, x.ty, v),
            Coercion::LiftErr { variant } => {
                let a = self.spill(x.ty, v);
                let (TyKind::Result { err: e1, ok }, TyKind::Result { err: e2, .. }) = (x.ty.kind(), t.kind()) else {
                    return unsupported(e.span, "this error lifting");
                };
                let (e1, ok, e2) = (*e1, *ok, *e2);
                let res = self.temp(t);
                let bad = self.res_is_err(x.ty, a);
                let off = self.g.offsets(x.ty)[1];
                self.if_else(
                    bad,
                    |fg| {
                        let p = fg.add(a, off);
                        let ev = fg.read(e1, p);
                        let wrapped = fg.make_variant(e2, variant as usize, Some(ev));
                        let r = fg.make_res(t, true, e2, wrapped);
                        fg.write(t, r, res);
                        Ok(())
                    },
                    |fg| {
                        let p = fg.add(a, off);
                        let ov = fg.read(ok, p);
                        let r = fg.make_res(t, false, ok, ov);
                        fg.write(t, r, res);
                        Ok(())
                    },
                )?;
                V::M(res)
            }
            Coercion::FromNever => self.undef(t),
            // Widening a plain function pointer to an effectful type marks it (see
            // `call_indirect`).
            Coercion::FnPtrWiden if self.ptr_type_plain(x.ty) && !self.ptr_type_plain(t) => {
                let s = self.as_scalar(x.ty, v);
                V::S(self.b.ins().bor_imm_s(s, 1))
            }
            Coercion::PtrToPtr | Coercion::FnPtrWiden | Coercion::TupleRename | Coercion::InOptional => {
                let (rs, rt) = (self.g.rep(x.ty), self.g.rep(t));
                if rs == rt {
                    v
                } else {
                    let a = self.spill(x.ty, v);
                    self.read(t, a)
                }
            }
            Coercion::ArrayPtrToSlice => {
                let TyKind::Ptr { pointee, .. } = x.ty.kind() else {
                    return unsupported(e.span, "this array-to-slice coercion");
                };
                let TyKind::Array { len, .. } = pointee.kind() else {
                    return unsupported(e.span, "this array-to-slice coercion");
                };
                let p = self.as_scalar(x.ty, v);
                let n = self.b.ins().iconst(types::I64, *len as i64);
                self.make_slice(t, p, n)
            }
            Coercion::FnToPtr => match (self.g.rep(x.ty), t.kind()) {
                (Rep::Ptr, TyKind::Fn(_)) => {
                    let s = self.as_scalar(x.ty, v);
                    let code = if self.ptr_type_plain(x.ty) { self.b.ins().bor_imm_s(s, 1) } else { s };
                    self.closure_value(t, code, None)
                }
                _ => v,
            },
            Coercion::Unresolved => return unsupported(e.span, "an unresolved coercion"),
        })
    }

    /// A second-class function value `{code, env, cx}`; a null environment for `None`.
    fn closure_value(&mut self, t: Ty, code: Value, env: Option<Value>) -> V {
        let a = self.temp(t);
        let env = match env {
            Some(e) => e,
            None => self.b.ins().iconst(types::I64, 0),
        };
        self.b.ins().store(mf(), code, a, 0);
        self.b.ins().store(mf(), env, a, 8);
        self.b.ins().store(mf(), self.cx, a, 16);
        V::M(a)
    }

    /// A named function or non-capturing literal as a value of type `t`.
    fn fn_ref(&mut self, f: FnId, t: Ty) -> CResult<V> {
        let id = self.g.func(f)?;
        let fr = self.fref(id);
        let addr = self.b.ins().func_addr(types::I64, fr);
        // A plain function where hidden-convention code is expected is marked by setting
        // the low bit of its address (see `call_indirect`).
        let code = if self.g.is_plain(f) && !self.ptr_type_plain(t) { self.b.ins().bor_imm_s(addr, 1) } else { addr };
        if matches!(t.kind(), TyKind::Fn(_)) { Ok(self.closure_value(t, code, None)) } else { Ok(V::S(code)) }
    }

    /// True if values of function pointer type `t` use the plain convention: their effects
    /// are at most `{Sys}`.
    pub fn ptr_type_plain(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::FnPtr(sig) => self.g.within_sys(&sig.effects),
            TyKind::Optional(inner) => self.ptr_type_plain(*inner),
            _ => false,
        }
    }

    // --- calls and effects -----------------------------------------------------------------------

    /// A call; a large result goes to `dest` if given.
    pub fn call_expr(&mut self, e: &'a Expr, callee: &'a Callee, args: &'a [Expr], dest: Option<Value>) -> CResult<V> {
        // The callee is evaluated before the arguments (§7.1).
        let cv = match callee {
            Callee::Indirect(c) => Some(self.expr(c)?),
            _ => None,
        };
        let mut vals = vec![];
        let mut tys = vec![];
        for (i, a) in args.iter().enumerate() {
            // A large argument read from a place is passed as the place itself when nothing
            // evaluated before the call can change it: the callee copies its parameters
            // before anything else.
            let in_place = matches!(self.g.rep(a.ty), Rep::Mem { .. })
                && self.in_place_root(a)
                && args[i + 1..].iter().all(no_writes);
            let v = if in_place { V::M(self.place(a)?) } else { self.expr(a)? };
            vals.push(v);
            tys.push(a.ty);
        }
        self.call_site(e.span)?;
        match callee {
            Callee::Fn(f) => self.call_fn(e, *f, &vals, &tys, dest),
            Callee::Indirect(c) => {
                let cv = cv.unwrap();
                match c.ty.kind() {
                    TyKind::Fn(_) => {
                        let ca = self.spill(c.ty, cv);
                        self.call_closure(ca, c.ty, &vals, &tys, e.ty, dest)
                    }
                    TyKind::FnPtr(sig) => {
                        let effectful = !self.g.within_sys(&sig.effects);
                        let code = self.as_scalar(c.ty, cv);
                        let z = self.b.ins().iconst(types::I64, 0);
                        let cx = self.cx;
                        let r = self.call_indirect(code, z, cx, &vals, &tys, e.ty, effectful, sig.callconv, dest)?;
                        // A second-class argument may leave the callee by a non-local exit.
                        if effectful || super::takes_fn(tys.iter().copied()) {
                            self.check_unwind();
                        }
                        Ok(r)
                    }
                    _ => unsupported(e.span, format!("calling a value of type `{}`", c.ty)),
                }
            }
            Callee::Perform { effect, op } => self.perform(e, effect, *op, &vals, &tys, e.ty, dest),
        }
    }

    /// Whether a large argument `a` can be passed as the place it is read from: a local, an
    /// upvar or a dereference, or a field or array element of one.
    fn in_place_root(&self, a: &Expr) -> bool {
        let mut e = a;
        loop {
            match &e.kind {
                ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Deref(_) => return true,
                ExprKind::Field { base, .. } if self.g.bits_layout(base.ty).is_none() => e = base,
                ExprKind::Index { base, index, .. } if no_writes(index) => match base.ty.kind() {
                    TyKind::Array { .. } => e = base,
                    TyKind::Slice { .. } | TyKind::ManyPtr { .. } if no_writes(base) => return true,
                    _ => return false,
                },
                _ => return false,
            }
        }
    }

    /// The arguments of a call: scalars, addresses of values in memory.
    fn push_args(&mut self, out: &mut Vec<Value>, vals: &[V], tys: &[Ty]) {
        for (v, t) in vals.iter().zip(tys) {
            match (self.g.rep(*t), *v) {
                (Rep::Zero, _) => {}
                (Rep::Mem { .. }, v) => {
                    let a = self.spill(*t, v);
                    out.push(a)
                }
                (_, v) => {
                    let s = self.as_scalar(*t, v);
                    out.push(s)
                }
            }
        }
    }

    /// The value a call returns: its result register, or `dest`.
    fn call_result(&mut self, ret: Ty, rs: &[Value], dest: Option<Value>) -> V {
        match self.g.rep(ret) {
            Rep::Zero => V::Z,
            Rep::Mem { .. } => V::M(dest.unwrap()),
            r => V::S(self.normalize_ret(r, rs[0])),
        }
    }

    fn normalize_ret(&mut self, _r: Rep, v: Value) -> Value {
        v
    }

    /// Where a call's large result goes: `dest`, or a new temporary.
    fn result_dest(&mut self, ret: Ty, dest: Option<Value>) -> Option<Value> {
        match self.g.rep(ret) {
            Rep::Mem { .. } => Some(match dest {
                Some(d) => d,
                None => self.temp(ret),
            }),
            _ => None,
        }
    }

    fn call_fn(&mut self, e: &Expr, f: FnId, vals: &[V], tys: &[Ty], dest: Option<Value>) -> CResult<V> {
        let def = &self.g.prog.fns[f.index()];
        let id = self.g.func(f)?;
        let ret = def.ret;
        let dest = self.result_dest(ret, dest);
        let mut args = vec![];
        if let FnKind::Foreign { variadic, .. } = def.kind {
            let n = def.params.len();
            let cs = self.g.c_sig_of(f)?;
            self.check_c_args(e, &tys[..n])?;
            let args = self.c_args(&cs, &vals[..n], &tys[..n], dest);
            if variadic && vals.len() > n {
                let rs = self.variadic_call(e, cs.sig.clone(), id, args, &vals[n..], &tys[n..])?;
                return Ok(self.c_result(&cs, ret, &rs, dest));
            }
            let rs = self.call_c(id, &cs.sig, &args);
            return Ok(self.c_result(&cs, ret, &rs, dest));
        }
        if let Some(d) = dest {
            args.push(d);
        }
        if !self.g.is_plain(f) {
            // A function with a parent captures it (a literal called directly).
            let env = match def.parent {
                Some(p) => match (0..64).find(|l| self.frame_up(*l).1 == p) {
                    Some(l) => self.frame_up(l).0,
                    None => self.b.ins().iconst(types::I64, 0),
                },
                None => self.b.ins().iconst(types::I64, 0),
            };
            let z = self.b.ins().iconst(types::I64, 0);
            args.extend([env, self.cx, z]);
        }
        let ptys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
        self.push_args(&mut args, vals, &ptys);
        let rs = self.call(id, &args);
        let r = self.call_result(ret, &rs, dest);
        if self.g.may_unwind(f) {
            self.check_unwind();
        }
        Ok(r)
    }

    /// A call of variadic foreign function `id`, whose fixed parameters `sig` describes,
    /// with the arguments `extra` after them, as C passes them. Returns the results.
    fn variadic_call(
        &mut self,
        e: &Expr,
        mut sig: ir::Signature,
        id: cranelift_module::FuncId,
        mut args: Vec<Value>,
        extra: &[V],
        tys: &[Ty],
    ) -> CResult<Vec<Value>> {
        let target = &self.g.prog.target;
        // Apple's AArch64 ABI passes the variadic arguments on the stack, 8 bytes each: the
        // registers the fixed parameters leave are filled with unused values first.
        let apple = target.os == "MacOS" && target.arch == "Aarch64";
        if apple {
            let normal = sig.params.iter().filter(|p| p.purpose == ir::ArgumentPurpose::Normal);
            let (mut ints, mut floats) = (0, 0);
            for p in normal {
                match p.value_type {
                    t if t.is_float() => floats += 1,
                    types::I128 => ints += 2 + ints % 2,
                    _ => ints += 1,
                }
            }
            for _ in ints..8 {
                args.push(self.b.ins().iconst(types::I64, 0));
                sig.params.push(ir::AbiParam::new(types::I64));
            }
            for _ in floats..8 {
                args.push(self.b.ins().f64const(0.0));
                sig.params.push(ir::AbiParam::new(types::F64));
            }
        }
        let wide = if apple { types::I64 } else { types::I32 };
        for (v, t) in extra.iter().zip(tys) {
            let r = self.g.rep(*t);
            let x = self.as_scalar(*t, *v);
            match r {
                Rep::F32 => {
                    // C promotes a float argument to double.
                    args.push(self.b.ins().fpromote(types::F64, x));
                    sig.params.push(ir::AbiParam::new(types::F64));
                }
                Rep::Int { ty, signed, .. } if ty.bits() < wide.bits() => {
                    let w = if signed { self.b.ins().sextend(wide, x) } else { self.b.ins().uextend(wide, x) };
                    args.push(w);
                    sig.params.push(ir::AbiParam::new(wide));
                }
                Rep::Bool => {
                    args.push(self.b.ins().uextend(wide, x));
                    sig.params.push(ir::AbiParam::new(wide));
                }
                Rep::Mem { .. } | Rep::Zero | Rep::F16 => {
                    return unsupported(e.span, format!("passing `{}` to a variadic C function", t));
                }
                r => {
                    args.push(x);
                    sig.params.push(ir::AbiParam::new(r.ty().unwrap()));
                }
            }
        }
        // On x86-64 the callee learns from `al` whether vector registers hold arguments.
        let floats = sig.params.iter().any(|p| p.value_type.is_float());
        let id = if floats && target.arch == "X86_64" { self.g.va_trampoline(id)? } else { id };
        let fr = self.fref(id);
        let addr = self.b.ins().func_addr(types::I64, fr);
        let sr = self.sigref(sig);
        let inst = self.b.ins().call_indirect(sr, addr, &args);
        Ok(self.b.inst_results(inst).to_vec())
    }

    /// Calls C function `id` with signature `sig`: directly, or through its address when
    /// the symbol was declared with another signature.
    fn call_c(&mut self, id: cranelift_module::FuncId, sig: &Signature, args: &[Value]) -> Vec<Value> {
        use cranelift_module::Module;
        if self.g.module.declarations().get_function_decl(id).signature == *sig {
            return self.call(id, args);
        }
        let fr = self.fref(id);
        let addr = self.b.ins().func_addr(types::I64, fr);
        let sr = self.sigref(sig.clone());
        let inst = self.b.ins().call_indirect(sr, addr, args);
        self.b.inst_results(inst).to_vec()
    }

    /// The arguments of a call with C signature `cs`: the values `vals` of types `tys` as
    /// the C ABI passes them, after the address of the result (`dest`) if it is returned
    /// through memory.
    fn c_args(&mut self, cs: &CSig, vals: &[V], tys: &[Ty], dest: Option<Value>) -> Vec<Value> {
        let mut out = vec![];
        if cs.ret == CRet::Sret {
            out.push(dest.unwrap());
        }
        for ((a, v), t) in cs.args.iter().zip(vals).zip(tys) {
            match a {
                CArg::None => {}
                CArg::Scalar => {
                    let s = self.as_scalar(*t, *v);
                    out.push(s)
                }
                CArg::Parts { pad, parts } => {
                    for ty in pad {
                        out.push(self.b.ins().iconst(*ty, 0));
                    }
                    let src = self.spill(*t, *v);
                    let tmp = self.slot(parts_size(parts), 16);
                    self.copy(tmp, src, self.g.size(*t));
                    for (ty, off) in parts {
                        out.push(self.b.ins().load(*ty, mf(), tmp, *off as i32));
                    }
                }
                CArg::Stack(n) => {
                    let src = self.spill(*t, *v);
                    let tmp = self.slot(*n as u64, 16);
                    self.copy(tmp, src, self.g.size(*t));
                    out.push(tmp);
                }
                CArg::Indirect => {
                    // The callee may change its copy.
                    let src = self.spill(*t, *v);
                    let tmp = self.temp(*t);
                    self.copy(tmp, src, self.g.size(*t));
                    out.push(tmp);
                }
            }
        }
        out
    }

    /// The value of type `ret` that a call with C signature `cs` returned in `rs` (or in
    /// `dest`).
    fn c_result(&mut self, cs: &CSig, ret: Ty, rs: &[Value], dest: Option<Value>) -> V {
        match &cs.ret {
            CRet::None => V::Z,
            CRet::Scalar => {
                let r = self.g.rep(ret);
                V::S(self.normalize(r, rs[0]))
            }
            CRet::Parts(parts) => {
                let tmp = self.slot(parts_size(parts), 16);
                for ((_, off), v) in parts.iter().zip(rs) {
                    self.b.ins().store(mf(), *v, tmp, *off as i32);
                }
                let d = dest.unwrap();
                self.copy(d, tmp, self.g.size(ret));
                V::M(d)
            }
            CRet::Sret => V::M(dest.unwrap()),
        }
    }

    /// Rejects a function pointer passed to C whose parameters or result the plain
    /// convention passes differently from C (structs by value).
    fn check_c_args(&mut self, e: &Expr, tys: &[Ty]) -> CResult<()> {
        for t in tys {
            let t = match t.kind() {
                TyKind::Optional(inner) => *inner,
                _ => *t,
            };
            if let TyKind::FnPtr(sig) = t.kind() {
                let mut tys: Vec<Ty> = sig.params.to_vec();
                tys.push(sig.ret);
                if tys.iter().any(|t| matches!(self.g.rep(*t), Rep::Mem { .. })) {
                    return unsupported(
                        e.span,
                        format!(
                            "passing a function pointer of type `{}` to C (it takes or returns a value by value that C passes differently)",
                            t
                        ),
                    );
                }
            }
        }
        Ok(())
    }

    /// Calls a second-class function value (at `ca`): its context plus the caller's handlers
    /// for the effects in its type (§11.6).
    pub fn call_closure(
        &mut self,
        ca: Value,
        cty: Ty,
        vals: &[V],
        tys: &[Ty],
        ret: Ty,
        dest: Option<Value>,
    ) -> CResult<V> {
        let TyKind::Fn(sig) = cty.kind() else { unreachable!() };
        let code = self.b.ins().load(types::I64, mf(), ca, 0);
        let env = self.b.ins().load(types::I64, mf(), ca, 8);
        let ccx0 = self.b.ins().load(types::I64, mf(), ca, 16);
        let ccx = self.b.declare_var(types::I64);
        self.b.def_var(ccx, ccx0);
        for eff in sig.effects.effects.iter() {
            let id = self.g.effect_id(eff);
            let cx = self.cx;
            let found = self.find_handler(cx, id);
            let node = self.slot(72, 8);
            self.if_then(found, |fg| {
                fg.copy(node, found, 72);
                let cur = fg.b.use_var(ccx);
                fg.b.ins().store(mf(), cur, node, 64);
                fg.b.def_var(ccx, node);
                Ok(())
            })?;
        }
        let ccx = self.b.use_var(ccx);
        let r = self.call_indirect(code, env, ccx, vals, tys, ret, true, CallConv::C, dest)?;
        self.check_unwind();
        Ok(r)
    }

    /// The innermost handler of effect `id` in context `cx`, or null.
    pub fn find_handler(&mut self, cx: Value, id: u32) -> Value {
        // The innermost node, most often the one, is looked at inline.
        let var = self.b.declare_var(types::I64);
        self.b.def_var(var, cx);
        let slow = self.b.create_block();
        let check = self.b.create_block();
        let done = self.b.create_block();
        self.b.ins().brif(cx, check, &[], done, &[]);
        self.b.switch_to_block(check);
        let eff = self.b.ins().load(types::I32, mf(), cx, 0);
        let same = self.b.ins().icmp_imm_s(IntCC::Equal, eff, id as i64);
        self.b.ins().brif(same, done, &[], slow, &[]);
        self.b.switch_to_block(slow);
        let n = self.b.ins().iconst(types::I32, id as i64);
        let r = self.rt_call("gm_find_outer", "pi>p", &[cx, n])[0];
        self.b.def_var(var, r);
        self.b.ins().jump(done, &[]);
        self.b.switch_to_block(done);
        self.b.use_var(var)
    }

    /// Calls code pointer `code`. With `hidden`, the code uses the hidden convention (env,
    /// cx, inst) unless its low bit marks a plain function; otherwise it is plain.
    #[allow(clippy::too_many_arguments)]
    pub fn call_indirect(
        &mut self,
        code: Value,
        env: Value,
        cx: Value,
        vals: &[V],
        tys: &[Ty],
        ret: Ty,
        hidden: bool,
        cc: CallConv,
        dest: Option<Value>,
    ) -> CResult<V> {
        let dest = self.result_dest(ret, dest);
        let rep = self.g.rep(ret);
        let var = rep.ty().filter(|_| !matches!(rep, Rep::Mem { .. })).map(|ty| self.b.declare_var(ty));
        let mut pargs = vec![];
        if let Some(d) = dest {
            pargs.push(d);
        }
        self.push_args(&mut pargs, vals, tys);
        let cc_plain = self.g.callconv(cc);
        let plain_sig = self.g.sig_of(tys, ret, false, cc_plain);
        let emit_plain = |fg: &mut Self| {
            let untagged = fg.b.ins().band_imm_s(code, -2);
            let sr = fg.sigref(plain_sig.clone());
            let inst = fg.b.ins().call_indirect(sr, untagged, &pargs);
            if let Some(var) = var {
                let r = fg.b.inst_results(inst)[0];
                fg.b.def_var(var, r);
            }
        };
        if hidden {
            let mut hargs = vec![];
            if let Some(d) = dest {
                hargs.push(d);
            }
            let z = self.b.ins().iconst(types::I64, 0);
            hargs.extend([env, cx, z]);
            self.push_args(&mut hargs, vals, tys);
            let hidden_sig = self.g.sig_of(tys, ret, true, self.g.cc);
            let tag = self.b.ins().band_imm_s(code, 1);
            self.if_else(
                tag,
                |fg| {
                    emit_plain(fg);
                    Ok(())
                },
                |fg| {
                    let sr = fg.sigref(hidden_sig);
                    let inst = fg.b.ins().call_indirect(sr, code, &hargs);
                    if let Some(var) = var {
                        let r = fg.b.inst_results(inst)[0];
                        fg.b.def_var(var, r);
                    }
                    Ok(())
                },
            )?;
        } else {
            emit_plain(self);
        }
        Ok(match rep {
            Rep::Zero => V::Z,
            Rep::Mem { .. } => V::M(dest.unwrap()),
            _ => V::S(self.b.use_var(var.unwrap())),
        })
    }

    /// Performs operation `op` of `effect` with arguments `vals`.
    #[allow(clippy::too_many_arguments)]
    pub fn perform(
        &mut self,
        e: &Expr,
        effect: &EffectRef,
        op: u32,
        vals: &[V],
        tys: &[Ty],
        ret: Ty,
        dest: Option<Value>,
    ) -> CResult<V> {
        let id = self.g.effect_id(effect);
        let amb = self.g.prog.ambient.clone();
        let is_ctl = self.g.prog.ctl_ops.contains(&(effect.effect, op));
        // A write to the ambient output or error goes through the runtime.
        if let Some(native) = self.out_writer(effect, op, tys, ret) {
            let a = self.spill(tys[0], vals[0]);
            let (p, n) = self.slice_parts(a);
            let r = self.write_call(e.span, effect, id, op, native, p, n)?;
            self.unwind_if(r);
            return Ok(V::Z);
        }
        let cx = self.cx;
        let n = self.find_handler(cx, id);
        let absent = self.b.ins().icmp_imm_s(IntCC::Equal, n, 0);
        let name = effect.effect.name();
        self.trap_if(absent, &format!("no handler installed for effect `{}`", name), e.span)?;
        if is_ctl {
            // The result is written when the clause resumes the fiber, so it lands in a
            // temporary: the destination is assigned only once the perform returns.
            let v = self.ctl_perform(e, n, op, vals, tys, ret)?;
            if let (Some(d), Rep::Mem { .. }) = (dest, self.g.rep(ret)) {
                self.write(ret, v, d);
                return Ok(V::M(d));
            }
            return Ok(v);
        }
        let dest = self.result_dest(ret, dest);
        let rep = self.g.rep(ret);
        let var = rep.ty().filter(|_| !matches!(rep, Rep::Mem { .. })).map(|ty| self.b.declare_var(ty));
        let sig = self.g.sig_of(tys, ret, true, self.g.cc);
        let clause_call = |fg: &mut Self| {
            let clauses = fg.b.ins().load(types::I64, mf(), n, 8);
            let code = fg.b.ins().load(types::I64, mf(), clauses, (8 * op) as i32);
            let env = fg.b.ins().load(types::I64, mf(), n, 16);
            let hctx = fg.b.ins().load(types::I64, mf(), n, 24);
            let mut args = vec![];
            if let Some(d) = dest {
                args.push(d);
            }
            args.extend([env, hctx, n]);
            fg.push_args(&mut args, vals, tys);
            let sr = fg.sigref(sig.clone());
            let inst = fg.b.ins().call_indirect(sr, code, &args);
            if let Some(var) = var {
                let r = fg.b.inst_results(inst)[0];
                fg.b.def_var(var, r);
            }
        };
        let native = [amb.out, amb.err, amb.env].contains(&Some(effect.effect));
        if native {
            let k = self.b.ins().load(types::I8, mf(), n, 4);
            let is_native = self.b.ins().icmp_imm_s(IntCC::Equal, k, NATIVE as i64);
            self.if_else(
                is_native,
                |fg| fg.native_op(effect, op, vals, tys, ret, dest, var),
                |fg| {
                    clause_call(fg);
                    Ok(())
                },
            )?;
        } else {
            clause_call(self);
        }
        self.check_unwind();
        Ok(match rep {
            Rep::Zero => V::Z,
            Rep::Mem { .. } => V::M(dest.unwrap()),
            _ => V::S(self.b.use_var(var.unwrap())),
        })
    }

    /// For a perform of the ambient output's or error's `write`, which the runtime does: 1
    /// for the output, 2 for the error.
    fn out_writer(&self, effect: &EffectRef, op: u32, tys: &[Ty], ret: Ty) -> Option<u32> {
        let amb = &self.g.prog.ambient;
        let native = if Some(effect.effect) == amb.out {
            1
        } else if Some(effect.effect) == amb.err {
            2
        } else {
            return None;
        };
        let ok = ret.is_unit()
            && tys.len() == 1
            && matches!(tys[0].kind(), TyKind::Slice { elem, .. } if *elem == Ty::u8())
            && !self.g.prog.ctl_ops.contains(&(effect.effect, op));
        ok.then_some(native)
    }

    /// `gm_clif_write`: performs `op` of `effect` with the bytes `[p, p + n)`; returns whether
    /// an exit is in progress.
    #[allow(clippy::too_many_arguments)]
    fn write_call(
        &mut self,
        span: Span,
        effect: &EffectRef,
        id: u32,
        op: u32,
        native: u32,
        p: Value,
        n: Value,
    ) -> CResult<Value> {
        let name = self.cstr(effect.effect.name().as_str().as_bytes())?;
        let [f, l, c] = self.loc(span)?;
        let eff = self.b.ins().iconst(types::I32, id as i64);
        let opv = self.b.ins().iconst(types::I32, op as i64);
        let nat = self.b.ins().iconst(types::I32, native as i64);
        let cx = self.cx;
        Ok(self.rt_call("gm_clif_write", "piiipplpii>b", &[cx, eff, opv, nat, name, p, n, f, l, c])[0])
    }

    /// Runs the ladder when `r` (a call's result) says an exit is in progress.
    fn unwind_if(&mut self, r: Value) {
        if self.no_check {
            return;
        }
        let t = self.ladder_target();
        let cont = self.b.create_block();
        self.b.ins().brif(r, t, &[], cont, &[]);
        self.b.switch_to_block(cont);
    }

    /// The runtime's handlers for the ambient `Out`, `Err` and `Env` (§12.9).
    #[allow(clippy::too_many_arguments)]
    fn native_op(
        &mut self,
        effect: &EffectRef,
        op: u32,
        vals: &[V],
        tys: &[Ty],
        ret: Ty,
        dest: Option<Value>,
        var: Option<Variable>,
    ) -> CResult<()> {
        let amb = self.g.prog.ambient.clone();
        if Some(effect.effect) == amb.out || Some(effect.effect) == amb.err {
            let f = if Some(effect.effect) == amb.out { "gm_write_out" } else { "gm_write_err" };
            let a = self.spill(tys[0], vals[0]);
            let (p, n) = self.slice_parts(a);
            self.rt_call(f, "pl", &[p, n]);
            return Ok(());
        }
        match op {
            0 => {
                let cnt = self.slot(8, 8);
                let p = self.rt_call("gm_args", "p>p", &[cnt])[0];
                let n = self.b.ins().load(types::I64, mf(), cnt, 0);
                let s = self.make_slice(ret, p, n);
                self.store_result(ret, s, dest, var);
            }
            1 => {
                let a = self.spill(tys[0], vals[0]);
                let (p, n) = self.slice_parts(a);
                let out = self.slot(16, 8);
                let outn = self.add(out, 8);
                let ok = self.rt_call("gm_env_var", "plpp>b", &[p, n, out, outn])[0];
                let r = self.temp(ret);
                self.if_else(
                    ok,
                    |fg| {
                        let s = V::M(out);
                        let some = fg.make_some(ret, s);
                        fg.write(ret, some, r);
                        Ok(())
                    },
                    |fg| {
                        let none = fg.make_none(ret);
                        fg.write(ret, none, r);
                        Ok(())
                    },
                )?;
                let v = self.read_owned(ret, r);
                self.store_result(ret, v, dest, var);
            }
            _ => {
                let unw = self.tls(0);
                let code = self.as_scalar(tys[0], vals[0]);
                let code = match self.g.rep(tys[0]) {
                    Rep::Int { ty, .. } if ty != types::I8 => {
                        if ty == types::I128 {
                            let lo = self.b.ins().ireduce(types::I64, code);
                            self.b.ins().ireduce(types::I8, lo)
                        } else {
                            self.b.ins().ireduce(types::I8, code)
                        }
                    }
                    _ => code,
                };
                self.b.ins().store(mf(), code, unw, 21);
                let z = self.b.ins().iconst(types::I64, 0);
                let one = self.b.ins().iconst(types::I8, 1);
                self.start_exit(EXIT, ExitFrame::At(z), ExitLabel::Static(0), Some(one));
            }
        }
        Ok(())
    }

    /// Stores `v`, the result of an operation, where the caller expects it.
    fn store_result(&mut self, ret: Ty, v: V, dest: Option<Value>, var: Option<Variable>) {
        match (dest, var) {
            (Some(d), _) => self.write(ret, v, d),
            (None, Some(var)) => {
                let x = self.as_scalar(ret, v);
                self.b.def_var(var, x);
            }
            _ => {}
        }
    }

    /// Performs `ctl` operation `op` through node `n` (§14.2): the arguments go into a
    /// request on the fiber's stack, and a thunk calls the clause on the driver.
    fn ctl_perform(&mut self, e: &Expr, n: Value, op: u32, vals: &[V], tys: &[Ty], ret: Ty) -> CResult<V> {
        let _ = e;
        // The request is laid out as a C struct: the node, then the arguments.
        let mut size = 8u64;
        let mut align = 8u64;
        let mut offsets = vec![];
        for t in tys {
            let (s, a) = (self.g.size(*t), self.g.align(*t));
            let off = size.next_multiple_of(a);
            offsets.push(off);
            size = off + s;
            align = align.max(a);
        }
        let req = self.slot(size.next_multiple_of(align), align);
        self.b.ins().store(mf(), n, req, 0);
        for ((v, t), off) in vals.iter().zip(tys).zip(&offsets) {
            let p = self.add(req, *off);
            self.write(*t, *v, p);
        }
        let mut sig = Signature::new(self.g.cc);
        for _ in 0..3 {
            sig.params.push(ir::AbiParam::new(types::I64));
        }
        let func = self.g.local_fn("gm_pt", &sig)?;
        self.g.queue.push_back(Work::CtlThunk { func, op, tys: tys.to_vec(), offsets, ret });
        let fr = self.fref(func);
        let thunk = self.b.ins().func_addr(types::I64, fr);
        let res = if ret.is_unit() || ret.is_never() || self.g.rep(ret) == Rep::Zero {
            self.b.ins().iconst(types::I64, 0)
        } else {
            self.temp(ret)
        };
        let fw = self.b.ins().load(types::I64, mf(), n, 56);
        self.rt_call("gm_ctl_perform", "pppp", &[fw, thunk, req, res]);
        self.check_unwind();
        if ret.is_unit() || ret.is_never() || self.g.rep(ret) == Rep::Zero {
            return Ok(V::Z);
        }
        Ok(self.read_owned(ret, res))
    }

    fn with_expr(
        &mut self,
        e: &'a Expr,
        label: LabelId,
        handlers: &'a [HandlerValue],
        on: Option<&'a Expr>,
        body: &'a Expr,
    ) -> CResult<V> {
        let mut cur = self.cx;
        let result_off = self.frame.withs[&label];
        let result_slot = self.frame_addr(result_off);
        // A `with … on` (§14) has a driver state its handlers point at.
        let fw = match on {
            Some(_) => {
                let w = self.slot(64, 8);
                self.zero(w, 0, 64);
                w
            }
            None => self.b.ins().iconst(types::I64, 0),
        };
        for h in handlers {
            match h {
                HandlerValue::Literal(x) => {
                    let hv = self.expr_m(x)?;
                    let TyKind::Handler(hid) = x.ty.kind() else {
                        return unsupported(x.span, "this handler value");
                    };
                    let hid = *hid as usize;
                    let table = self.g.handler_table(hid)?;
                    let effect = self.g.prog.handlers[hid].effect.clone();
                    let id = self.g.effect_id(&effect);
                    let env = self.b.ins().load(types::I64, mf(), hv, 0);
                    let hctx = self.b.ins().load(types::I64, mf(), hv, 8);
                    cur = self.node(id, 0, table, env, hctx, label, result_slot, fw, cur);
                }
                HandlerValue::Decl { decls, value } => {
                    let v = self.expr(value)?;
                    let s = match v {
                        V::M(a) => a,
                        other => self.spill(value.ty, other),
                    };
                    let outer = cur;
                    for d in decls {
                        let table = self.g.decl_table(d.index())?;
                        let effect = self.g.prog.handler_decls[d.index()].effect.clone();
                        let id = self.g.effect_id(&effect);
                        cur = self.node(id, 1, table, s, outer, label, result_slot, fw, cur);
                    }
                }
            }
        }
        if let Some(stack) = on {
            return self.with_on(e, label, fw, cur, stack, body);
        }
        let saved = std::mem::replace(&mut self.cx, cur);
        let end = self.b.create_block();
        self.scopes.push(Scope::With { label, end, rung: None });
        let v = self.expr(body);
        let scope = self.scopes.pop().unwrap();
        self.cx = saved;
        let v = v?;
        if !body.ty.is_never() && !e.ty.is_never() {
            self.write(e.ty, v, result_slot);
        }
        self.end_scope(scope);
        self.switch(end);
        Ok(if e.ty.is_never() { V::Z } else { self.read(e.ty, result_slot) })
    }

    /// A handler node (`gm_node`) installed by a `with`; its address.
    #[allow(clippy::too_many_arguments)]
    fn node(
        &mut self,
        eff: u32,
        kind: u8,
        table: cranelift_module::DataId,
        env: Value,
        hctx: Value,
        label: LabelId,
        result: Value,
        fw: Value,
        next: Value,
    ) -> Value {
        let n = self.slot(72, 8);
        let e = self.b.ins().iconst(types::I32, eff as i64);
        self.b.ins().store(mf(), e, n, 0);
        let k = self.b.ins().iconst(types::I8, kind as i64);
        self.b.ins().store(mf(), k, n, 4);
        let t = self.data_addr(table);
        self.b.ins().store(mf(), t, n, 8);
        self.b.ins().store(mf(), env, n, 16);
        self.b.ins().store(mf(), hctx, n, 24);
        self.b.ins().store(mf(), self.fptr, n, 32);
        let l = self.b.ins().iconst(types::I32, label.0 as i64);
        self.b.ins().store(mf(), l, n, 40);
        self.b.ins().store(mf(), result, n, 48);
        self.b.ins().store(mf(), fw, n, 56);
        self.b.ins().store(mf(), next, n, 64);
        n
    }

    /// The rest of `with h on stack { body }` (§14), once its handlers are installed in the
    /// context `cx`, whose nodes point at the driver state `w`. The body runs on a fiber;
    /// this function drives it.
    fn with_on(
        &mut self,
        e: &'a Expr,
        label: LabelId,
        w: Value,
        cx: Value,
        stack: &'a Expr,
        body: &'a Expr,
    ) -> CResult<V> {
        let sa = self.expr_m(stack)?;
        let (p, l) = self.slice_parts(sa);
        let body_fn = self.g.queue_with_body(self.fid, label, e.ty, body)?;
        let fr = self.fref(body_fn);
        let start = self.b.ins().func_addr(types::I64, fr);
        let end = self.b.create_block();
        self.b.ins().store(mf(), cx, w, 32);
        let z = self.b.ins().iconst(types::I64, 0);
        let [f, ln, c] = self.loc(e.span)?;
        let fb = self.rt_call("gm_fiber_new", "pplplpii>p", &[w, p, l, start, z, f, ln, c])[0];
        self.b.ins().store(mf(), self.fptr, fb, 120);
        self.b.ins().store(mf(), self.env, fb, 128);
        self.b.ins().store(mf(), self.inst, fb, 160);
        self.b.ins().store(mf(), cx, fb, 152);
        // The clauses of its `ctl` operations run here, as if called by the `with`.
        self.call_site(e.span)?;
        self.rt_call("gm_with_go", "pp", &[w, fb]);
        let head = self.b.create_block();
        self.switch(head);
        let unw = self.tls(0);
        let ev = self.b.ins().load(types::I8, mf(), w, 40);
        let is_perform = self.b.ins().icmp_imm_s(IntCC::Equal, ev, 1);
        let (perf, other) = (self.b.create_block(), self.b.create_block());
        let after = self.b.create_block();
        self.b.ins().brif(is_perform, perf, &[], other, &[]);
        // A perform: the clause runs here.
        self.b.switch_to_block(perf);
        self.rt_call("gm_with_clause", "p", &[w]);
        let kind = self.b.ins().load(types::I32, mf(), unw, 0);
        let is_resume = self.b.ins().icmp_imm_s(IntCC::Equal, kind, RESUME);
        let (res_b, not_res) = (self.b.create_block(), self.b.create_block());
        self.b.ins().brif(is_resume, res_b, &[], not_res, &[]);
        self.b.switch_to_block(res_b);
        let zz = self.b.ins().iconst(types::I32, 0);
        self.b.ins().store(mf(), zz, unw, 0);
        let next = self.b.ins().load(types::I64, mf(), w, 56);
        self.rt_call("gm_with_go", "pp", &[w, next]);
        self.goto(head);
        self.b.switch_to_block(not_res);
        // A clause's exit crosses the `with` unless it is a `break` to the `with` (§14.4).
        let kind = self.b.ins().load(types::I32, mf(), unw, 0);
        let is_brk = self.b.ins().icmp_imm_s(IntCC::Equal, kind, BREAK);
        let frv = self.b.ins().load(types::I64, mf(), unw, 8);
        let own = self.b.ins().icmp(IntCC::Equal, frv, self.fptr);
        let labv = self.b.ins().load(types::I32, mf(), unw, 16);
        let mine = self.b.ins().icmp_imm_s(IntCC::Equal, labv, label.0 as i64);
        let a = self.b.ins().band(is_brk, own);
        let a = self.b.ins().band(a, mine);
        let crossing = self.b.ins().bxor_imm_s(a, 1);
        let [f, ln, c] = self.loc(e.span)?;
        self.rt_call("gm_with_end", "pbpii", &[w, crossing, f, ln, c]);
        self.goto(after);
        // The fiber finished, or an exit left it.
        self.b.switch_to_block(other);
        let evf = self.b.ins().load(types::I64, mf(), w, 48);
        let is_fb = self.b.ins().icmp(IntCC::Equal, evf, fb);
        let kind = self.b.ins().load(types::I32, mf(), unw, 0);
        let no_exit = self.b.ins().icmp_imm_s(IntCC::Equal, kind, 0);
        let done = self.b.ins().band(is_fb, no_exit);
        let (done_b, exit_b) = (self.b.create_block(), self.b.create_block());
        self.b.ins().brif(done, done_b, &[], exit_b, &[]);
        self.b.switch_to_block(done_b);
        let fls = self.b.ins().iconst(types::I8, 0);
        let [f, ln, c] = self.loc(e.span)?;
        self.rt_call("gm_with_end", "pbpii", &[w, fls, f, ln, c]);
        self.goto(end);
        self.b.switch_to_block(exit_b);
        let kind = self.b.ins().load(types::I32, mf(), unw, 0);
        let none = self.b.ins().icmp_imm_s(IntCC::Equal, kind, 0);
        self.trap_if(none, "a fiber's function returned", e.span)?;
        let tru = self.b.ins().iconst(types::I8, 1);
        let [f, ln, c] = self.loc(e.span)?;
        self.rt_call("gm_with_end", "pbpii", &[w, tru, f, ln, c]);
        self.switch(after);
        let kind = self.b.ins().load(types::I32, mf(), unw, 0);
        let none = self.b.ins().icmp_imm_s(IntCC::Equal, kind, 0);
        self.trap_if(none, "internal error: a `ctl` clause finished normally", e.span)?;
        self.scopes.push(Scope::With { label, end, rung: None });
        self.ladder();
        let scope = self.scopes.pop().unwrap();
        self.end_scope(scope);
        self.switch(end);
        if e.ty.is_never() {
            return Ok(V::Z);
        }
        let off = self.frame.withs[&label];
        let slot = self.frame_addr(off);
        Ok(self.read(e.ty, slot))
    }

    /// `@fiber(f, arg, stack)` in a `ctl` clause (§14.6): a fiber whose start function calls
    /// `f(arg)`.
    pub fn fiber_builtin(&mut self, e: &'a Expr, args: &'a [Expr]) -> CResult<V> {
        let TyKind::FnPtr(sig) = args[0].ty.kind() else {
            return unsupported(e.span, "`@fiber` with this function");
        };
        let hidden = !self.g.within_sys(&sig.effects);
        let f = self.expr_s(&args[0])?;
        let a = self.expr(&args[1])?;
        let sa = self.expr_m(&args[2])?;
        let (p, l) = self.slice_parts(sa);
        let aty = args[1].ty;
        let size = self.g.size(aty);
        let mut ssig = Signature::new(self.g.cc);
        ssig.params.push(ir::AbiParam::new(types::I64));
        let start = self.g.local_fn("gm_fs", &ssig)?;
        self.g.queue.push_back(Work::FiberStart { func: start, aty, hidden });
        let fr = self.fref(start);
        let start = self.b.ins().func_addr(types::I64, fr);
        let fw = self.b.ins().load(types::I64, mf(), self.inst, 56);
        let n = self.b.ins().iconst(types::I64, size as i64);
        let [fl, ln, c] = self.loc(e.span)?;
        let nf = self.rt_call("gm_fiber_new", "pplplpii>p", &[fw, p, l, start, n, fl, ln, c])[0];
        self.b.ins().store(mf(), f, nf, 136);
        let bcx = self.b.ins().load(types::I64, mf(), fw, 32);
        self.b.ins().store(mf(), bcx, nf, 152);
        if size > 0 {
            let argp = self.b.ins().load(types::I64, mf(), nf, 144);
            self.write(aty, a, argp);
        }
        let k = self.temp(e.ty);
        self.rt_call("gm_clif_fiber_cont", "ppp", &[fw, nf, k]);
        Ok(self.read_owned(e.ty, k))
    }

    fn try_expr(&mut self, e: &'a Expr, x: &'a Expr, lift: Option<Lift>, levels: u32) -> CResult<V> {
        let a = self.expr_m(x)?;
        let (fr, f) = self.frame_up(levels);
        let ret = self.g.prog.fns[f.index()].ret;
        let ret_off = self.g.frame(f).ret;
        let slot = self.add(fr, ret_off);
        let frame = if levels == 0 { ExitFrame::Own } else { ExitFrame::At(fr) };
        match x.ty.kind() {
            TyKind::Optional(_) => {
                let present = self.opt_present(x.ty, a);
                let absent = self.b.ins().bxor_imm_s(present, 1);
                self.if_then(absent, |fg| {
                    let none = fg.make_none(ret);
                    fg.write(ret, none, slot);
                    fg.start_exit(RETURN, frame, ExitLabel::Static(0), None);
                    Ok(())
                })?;
                Ok(self.read(e.ty, a))
            }
            TyKind::Result { err, .. } => {
                let err = *err;
                let bad = self.res_is_err(x.ty, a);
                let off = self.g.offsets(x.ty)[1];
                let TyKind::Result { err: rerr, .. } = ret.kind() else {
                    return unsupported(e.span, "`try` in this function");
                };
                let rerr = *rerr;
                self.if_then(bad, |fg| {
                    let p = fg.add(a, off);
                    let ev = fg.read(err, p);
                    let ev = match lift {
                        Some(Lift::Variant(i)) => fg.make_variant(rerr, i as usize, Some(ev)),
                        None => ev,
                    };
                    let r = fg.make_res(ret, true, rerr, ev);
                    fg.write(ret, r, slot);
                    let one = fg.b.ins().iconst(types::I8, 1);
                    fg.start_exit(RETURN, frame, ExitLabel::Static(0), Some(one));
                    Ok(())
                })?;
                let p = self.add(a, off);
                Ok(self.read(e.ty, p))
            }
            _ => unsupported(e.span, "`try` on this type"),
        }
    }

    fn else_expr(
        &mut self,
        e: &'a Expr,
        lhs: &'a Expr,
        capture: Option<crate::tir::LocalId>,
        rhs: &'a Expr,
    ) -> CResult<V> {
        let t = e.ty;
        let l = self.expr(lhs)?;
        let a = self.spill(lhs.ty, l);
        let res = self.result_holder(t);
        let (present, payload_ty, payload_off) = match lhs.ty.kind() {
            TyKind::Optional(p) => (self.opt_present(lhs.ty, a), *p, 0),
            TyKind::Result { ok, .. } => {
                let bad = self.res_is_err(lhs.ty, a);
                (self.b.ins().bxor_imm_s(bad, 1), *ok, self.g.offsets(lhs.ty)[1])
            }
            _ => return unsupported(e.span, "`else` on this type"),
        };
        let (yes, no, end) = (self.b.create_block(), self.b.create_block(), self.b.create_block());
        self.b.ins().brif(present, yes, &[], no, &[]);
        self.b.switch_to_block(yes);
        if !t.is_never() {
            let p = self.add(a, payload_off);
            let v = if t == payload_ty {
                self.read(payload_ty, p)
            } else if t == lhs.ty {
                self.read(lhs.ty, a)
            } else if matches!(t.kind(), TyKind::Optional(_)) {
                let x = self.read(payload_ty, p);
                self.make_some(t, x)
            } else {
                let x = self.read(payload_ty, p);
                self.make_res(t, false, payload_ty, x)
            };
            self.hold(&res, t, v);
        }
        self.goto(end);
        self.b.switch_to_block(no);
        if let (Some(c), TyKind::Result { err, .. }) = (capture, lhs.ty.kind()) {
            let off = self.g.offsets(lhs.ty)[1];
            let p = self.add(a, off);
            let ev = self.read(*err, p);
            self.set_local(c, ev);
        }
        let v = self.expr(rhs)?;
        self.hold(&res, rhs.ty, v);
        self.switch(end);
        Ok(self.held(res))
    }

    // --- conditions and patterns -------------------------------------------------------------------

    /// The parts of an `if` or loop condition, jumping to `fail` when one fails.
    fn conds(&mut self, cond: &'a [CondPart], fail: ir::Block) -> CResult<()> {
        for c in cond {
            match c {
                CondPart::Bool(b) => {
                    let v = self.expr_s(b)?;
                    let next = self.b.create_block();
                    self.b.ins().brif(v, next, &[], fail, &[]);
                    self.b.switch_to_block(next);
                }
                CondPart::Match { pat, value } => {
                    let a = self.expr_m(value)?;
                    self.pat(pat, a, fail)?;
                }
            }
        }
        Ok(())
    }

    fn match_expr(&mut self, e: &'a Expr, scrutinee: &'a Expr, arms: &'a [Arm]) -> CResult<V> {
        let a = self.expr_m(scrutinee)?;
        let res = self.result_holder(e.ty);
        let end = self.b.create_block();
        for arm in arms {
            let next = self.b.create_block();
            self.pat(&arm.pat, a, next)?;
            if let Some(g) = &arm.guard {
                let gv = self.expr_s(g)?;
                let ok = self.b.create_block();
                self.b.ins().brif(gv, ok, &[], next, &[]);
                self.b.switch_to_block(ok);
            }
            let v = self.expr(&arm.body)?;
            self.hold(&res, arm.body.ty, v);
            self.goto(end);
            self.switch(next);
        }
        self.trap("no match arm matched the scrutinee", e.span)?;
        self.switch(end);
        Ok(self.held(res))
    }

    /// Tests the value at `addr` against `p`, binding its locals; jumps to `fail` on a
    /// mismatch.
    fn pat(&mut self, p: &'a Pat, addr: Value, fail: ir::Block) -> CResult<()> {
        let t = p.ty;
        let branch = |fg: &mut Self, ok: Value| {
            let next = fg.b.create_block();
            fg.b.ins().brif(ok, next, &[], fail, &[]);
            fg.b.switch_to_block(next);
        };
        match &p.kind {
            PatKind::Wild => {}
            PatKind::Bind(l) => {
                let v = self.read(t, addr);
                self.set_local(*l, v);
            }
            PatKind::Const(c) => {
                let cv = self.expr(c)?;
                let v = self.read(t, addr);
                let eq = self.equal(t, v, cv)?;
                branch(self, eq);
            }
            PatKind::Range { lo, hi, inclusive } => {
                let v = self.load_s(t, addr, 0);
                let (l, h) = (self.int_lit(t, lo), self.int_lit(t, hi));
                let ge = self.compare(crate::tir::BinOp::Ge, t, v, l);
                let op = if *inclusive { crate::tir::BinOp::Le } else { crate::tir::BinOp::Lt };
                let below = self.compare(op, t, v, h);
                let ok = self.b.ins().band(ge, below);
                branch(self, ok);
            }
            PatKind::Null => {
                let present = self.opt_present(t, addr);
                let absent = self.b.ins().bxor_imm_s(present, 1);
                branch(self, absent);
            }
            PatKind::Some(q) => {
                let present = self.opt_present(t, addr);
                branch(self, present);
                self.pat(q, addr, fail)?;
            }
            PatKind::Ok(q) | PatKind::Err(q) => {
                let bad = self.res_is_err(t, addr);
                let ok = if matches!(p.kind, PatKind::Err(_)) { bad } else { self.b.ins().bxor_imm_s(bad, 1) };
                branch(self, ok);
                let off = self.g.offsets(t)[1];
                let pa = self.add(addr, off);
                self.pat(q, pa, fail)?;
            }
            PatKind::Variant { variant, payload } => {
                let def = self.adt_def(t);
                let AdtKind::Union { tag, variants } = &def.kind else {
                    return unsupported(p.span, "this variant pattern");
                };
                let (tag, vtag) = (*tag, variants[*variant as usize].tag.clone());
                let offs = self.g.offsets(t);
                let tv = self.load_s(tag, addr, offs[0]);
                let want = self.int_lit(tag, &vtag);
                let ok = self.b.ins().icmp(IntCC::Equal, tv, want);
                branch(self, ok);
                if let Some(q) = payload {
                    let pa = self.add(addr, offs[1]);
                    self.pat(q, pa, fail)?;
                }
            }
            PatKind::Fields(fs) => {
                let offs = self.g.offsets(t);
                for (i, q) in fs {
                    let pa = self.add(addr, offs[*i as usize]);
                    self.pat(q, pa, fail)?;
                }
            }
            PatKind::Or(alts) => {
                let ok = self.b.create_block();
                for alt in alts {
                    let next = self.b.create_block();
                    self.pat(alt, addr, next)?;
                    self.goto(ok);
                    self.switch(next);
                }
                self.goto(fail);
                self.switch(ok);
            }
        }
        Ok(())
    }

    /// `x in c` for arrays, slices, inline tuples and ranges (§7.7).
    fn in_expr(&mut self, elem: &'a Expr, container: &'a Expr, negated: bool) -> CResult<V> {
        let x = self.expr(elem)?;
        let ca = self.expr_m(container)?;
        let et = elem.ty;
        let r = match container.ty.kind() {
            TyKind::Array { .. } | TyKind::Slice { .. } => {
                let (p, n, ety) = match container.ty.kind() {
                    TyKind::Array { len, elem, .. } => (ca, self.b.ins().iconst(types::I64, *len as i64), *elem),
                    TyKind::Slice { elem, .. } => {
                        let (p, l) = self.slice_parts(ca);
                        (p, l, *elem)
                    }
                    _ => unreachable!(),
                };
                let esz = self.g.size(ety);
                let found = self.b.declare_var(types::I8);
                let z = self.b.ins().iconst(types::I8, 0);
                self.b.def_var(found, z);
                self.dyn_loop(n, |fg, i| {
                    let off = fg.b.ins().imul_imm_s(i, esz as i64);
                    let ea = fg.b.ins().iadd(p, off);
                    let y = fg.read(ety, ea);
                    let eq = fg.equal(ety, y, x)?;
                    fg.b.def_var(found, eq);
                    Ok(Some(eq))
                })?;
                self.b.use_var(found)
            }
            TyKind::Tuple { elems, .. } => {
                let offs = self.g.offsets(container.ty);
                let mut acc = self.b.ins().iconst(types::I8, 0);
                for (i, _) in elems.iter().enumerate() {
                    let ea = self.add(ca, offs[i]);
                    let y = self.read(et, ea);
                    let eq = self.equal(et, y, x)?;
                    acc = self.b.ins().bor(acc, eq);
                }
                acc
            }
            TyKind::Adt(..) => {
                let def = self.adt_def(container.ty);
                let incl = match def.name.as_str() {
                    "Range" => false,
                    "RangeIncl" => true,
                    _ => return unsupported(container.span, "`in` on this type"),
                };
                let offs = self.g.offsets(container.ty);
                let lo = self.load_s(et, ca, offs[0]);
                let hi = self.load_s(et, ca, offs[1]);
                let xv = self.as_scalar(et, x);
                let ge = self.compare(crate::tir::BinOp::Ge, et, xv, lo);
                let op = if incl { crate::tir::BinOp::Le } else { crate::tir::BinOp::Lt };
                let below = self.compare(op, et, xv, hi);
                self.b.ins().band(ge, below)
            }
            _ => return unsupported(container.span, "`in` on this type"),
        };
        Ok(V::S(if negated { self.b.ins().bxor_imm_s(r, 1) } else { r }))
    }

    /// Whether values `a` and `b` of type `t` are `is`-equal (§7.6): an `i8` 0 or 1.
    pub fn equal(&mut self, t: Ty, a: V, b: V) -> CResult<Value> {
        Ok(match self.g.rep(t) {
            Rep::Zero => self.b.ins().iconst(types::I8, 1),
            Rep::Mem { .. } => {
                let d = self.g.desc(t)?;
                let d = self.data_addr(d);
                let (pa, pb) = (self.spill(t, a), self.spill(t, b));
                self.rt_call("gm_clif_eq", "ppp>b", &[d, pa, pb])[0]
            }
            Rep::F32 | Rep::F64 => {
                let (x, y) = (self.as_scalar(t, a), self.as_scalar(t, b));
                self.b.ins().fcmp(FloatCC::Equal, x, y)
            }
            Rep::F16 => {
                let (x, y) = (self.as_scalar(t, a), self.as_scalar(t, b));
                let (x, y) = (self.f16_to_f64(x), self.f16_to_f64(y));
                self.b.ins().fcmp(FloatCC::Equal, x, y)
            }
            _ => {
                let (x, y) = (self.as_scalar(t, a), self.as_scalar(t, b));
                self.b.ins().icmp(IntCC::Equal, x, y)
            }
        })
    }

    // --- @write ---------------------------------------------------------------------------------

    fn write_expr(
        &mut self,
        e: &'a Expr,
        sink: &'a WriteSink,
        pieces: &'a [std::rc::Rc<[u8]>],
        specs: &'a [crate::tir::FmtSpec],
        args: &'a [Expr],
    ) -> CResult<V> {
        let sink_v = match sink {
            WriteSink::Fn(x) => Some((self.expr_m(x)?, x.ty)),
            WriteSink::Perform { .. } => None,
        };
        let mut vals = vec![];
        for x in args {
            vals.push(self.expr_m(x)?);
        }
        let string = Ty::string();
        // A write to the ambient output or error is one call of the runtime per piece.
        if let WriteSink::Perform { effect, op } = sink
            && let Some(native) = self.out_writer(effect, *op, &[string], Ty::unit())
        {
            let id = self.g.effect_id(effect);
            for i in 0..pieces.len() {
                if !pieces[i].is_empty() {
                    let p = self.cstr(&pieces[i])?;
                    let n = self.b.ins().iconst(types::I64, pieces[i].len() as i64);
                    let r = self.write_call(e.span, effect, id, *op, native, p, n)?;
                    self.unwind_if(r);
                }
                if i < args.len() {
                    let d = self.g.desc(args[i].ty)?;
                    let d = self.data_addr(d);
                    let spec = match specs.get(i) {
                        Some(s) if !s.is_default() => {
                            let sd = self.g.spec_data(s)?;
                            self.data_addr(sd)
                        }
                        _ => self.b.ins().iconst(types::I64, 0),
                    };
                    let name = self.cstr(effect.effect.name().as_str().as_bytes())?;
                    let [f, l, c] = self.loc(e.span)?;
                    let eff = self.b.ins().iconst(types::I32, id as i64);
                    let opv = self.b.ins().iconst(types::I32, *op as i64);
                    let nat = self.b.ins().iconst(types::I32, native as i64);
                    let cx = self.cx;
                    let r = self.rt_call(
                        "gm_clif_write_fmt",
                        "piiipppppii>b",
                        &[cx, eff, opv, nat, name, d, vals[i], spec, f, l, c],
                    )[0];
                    self.unwind_if(r);
                }
            }
            return Ok(V::Z);
        }
        for i in 0..pieces.len() {
            if !pieces[i].is_empty() {
                let p = self.cstr(&pieces[i])?;
                let n = self.b.ins().iconst(types::I64, pieces[i].len() as i64);
                let s = self.make_slice(string, p, n);
                self.emit_to(e, sink, sink_v, s)?;
            }
            if i < args.len() {
                let d = self.g.desc(args[i].ty)?;
                let d = self.data_addr(d);
                let spec = match specs.get(i) {
                    Some(s) if !s.is_default() => {
                        let sd = self.g.spec_data(s)?;
                        self.data_addr(sd)
                    }
                    _ => self.b.ins().iconst(types::I64, 0),
                };
                let buf = self.slot(24, 8);
                self.zero(buf, 0, 24);
                self.rt_call("gm_clif_format", "pppp", &[buf, d, vals[i], spec]);
                let n = self.b.ins().load(types::I64, mf(), buf, 8);
                // A formatted value that comes out empty is not written (as by the
                // interpreter).
                self.if_then(n, |fg| {
                    let p = fg.b.ins().load(types::I64, mf(), buf, 0);
                    let s = fg.make_slice(string, p, n);
                    fg.emit_to(e, sink, sink_v, s)
                })?;
                self.rt_call("gm_buf_free", "p", &[buf]);
            }
        }
        Ok(V::Z)
    }

    /// Sends a `[]const u8` value to an `@write` sink.
    fn emit_to(&mut self, e: &Expr, sink: &WriteSink, sv: Option<(Value, Ty)>, slice: V) -> CResult<()> {
        let unit = Ty::unit();
        match sink {
            WriteSink::Fn(_) => {
                let (ca, cty) = sv.unwrap();
                self.call_closure(ca, cty, &[slice], &[Ty::string()], unit, None)?;
            }
            WriteSink::Perform { effect, op } => {
                self.perform(e, effect, *op, &[slice], &[Ty::string()], unit, None)?;
            }
        }
        Ok(())
    }
}

/// Where an `if`, `else` or `match` keeps its value.
enum Holder {
    None,
    Mem(Ty, Value),
    Var(Ty, Variable),
}

/// True if evaluating `e` cannot write memory or perform effects: it combines reads,
/// literals and pure operators (it may still trap).
pub fn no_writes(e: &Expr) -> bool {
    let pure = matches!(
        e.kind,
        ExprKind::Int(_)
            | ExprKind::Float(_)
            | ExprKind::Bool(_)
            | ExprKind::Str(_)
            | ExprKind::CStr(_)
            | ExprKind::Null
            | ExprKind::ConstBytes(_)
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
