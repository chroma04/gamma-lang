//! Translation of statements, blocks, patterns and control flow.

use super::compile::*;
use super::*;

const DEFER_ESCAPE: &str = "internal error: control flow escaped a deferred statement";

/// True if compiling `e` straight into a local's registers is safe: the expression
/// writes its destination once, after reading everything else.
fn single_write(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Int(_)
        | ExprKind::Float(_)
        | ExprKind::Bool(_)
        | ExprKind::Null
        | ExprKind::ConstBytes(_)
        | ExprKind::Uninit
        | ExprKind::Str(_)
        | ExprKind::CStr(_)
        | ExprKind::Const(_)
        | ExprKind::Local(_)
        | ExprKind::Upvar { .. }
        | ExprKind::Global(_)
        | ExprKind::Deref(_)
        | ExprKind::Field { .. }
        | ExprKind::VariantPayload { .. }
        | ExprKind::Index { .. }
        | ExprKind::SliceLen(_)
        | ExprKind::SlicePtr(_)
        | ExprKind::Binary { .. }
        | ExprKind::Eq { .. }
        | ExprKind::In { .. }
        | ExprKind::Unary { .. }
        | ExprKind::Logic { .. }
        | ExprKind::Call { .. }
        | ExprKind::Unwrap { .. } => true,
        ExprKind::Builtin { op, .. } => !matches!(op, Builtin::Fiber | Builtin::Sys(_) | Builtin::Cover(_)),
        ExprKind::Coerce { kind, expr } => match kind {
            Coercion::PtrToPtr | Coercion::TupleRename | Coercion::FnPtrWiden => single_write(expr),
            Coercion::IntWiden | Coercion::FloatWiden | Coercion::ArrayPtrToSlice | Coercion::InOptional => true,
            _ => false,
        },
        _ => false,
    }
}

/// Where a tuple assignment stores one element.
enum Dest {
    Local(LocalId),
    Place(PlaceC),
}

impl<'m, 'a> Compiler<'m, 'a> {
    // --- statements and blocks -------------------------------------------------------------------

    /// Compiles `f` as one statement: the temporaries it makes die at its end.
    pub fn with_temps(&mut self, span: Span, f: impl FnOnce(&mut Self) -> C) -> C {
        let mark = self.next;
        let vmark = self.vnext;
        let nop = self.emit(Op::Nop, span) as usize;
        let r = self.alloc(8);
        self.temps.push((r, Vec::new()));
        let count = self.temp_count;
        let saved = std::mem::replace(&mut self.made_temp, false);
        f(self)?;
        let (_, kills) = self.temps.pop().unwrap();
        if self.made_temp {
            self.code.ops[nop] = Op::MarkTemps { dst: r };
            self.emit(Op::KillTemps { src: r }, span);
        } else if self.temp_count != count && !kills.is_empty() {
            // Only jumps out of the statement have temporaries to kill.
            self.code.ops[nop] = Op::MarkTemps { dst: r };
        } else {
            for k in kills {
                self.code.ops[k] = Op::Nop;
            }
        }
        self.made_temp = saved;
        self.next = mark;
        self.vnext = vmark;
        Ok(())
    }

    pub fn block(&mut self, b: &'a Block, dst: Arg) -> C {
        let base = self.defers.len();
        for s in &b.stmts {
            match s {
                Stmt::Defer { is_err, body } => {
                    let lo = self.pc();
                    self.defers.push(DeferCx {
                        is_err: *is_err,
                        body,
                        depth: self.depth,
                        regbase: self.next,
                        vregbase: self.vnext,
                        lo,
                    });
                }
                _ if self.tree_stmts.contains(&(s as *const Stmt)) => {
                    self.code.stmts.push(s);
                    let stmt = self.code.stmts.len() as u32 - 1;
                    self.emit(Op::TreeStmt { stmt }, stmt_span(s, b.span));
                }
                _ => {
                    let was = self.too_big;
                    let r = self.with_temps(stmt_span(s, b.span), |c| c.stmt(s));
                    if self.too_big && !was && self.retry.is_none() {
                        self.retry = Some(s as *const Stmt);
                        return Err(unsupported!());
                    }
                    r?;
                }
            }
        }
        if let Some(t) = &b.tail {
            self.expr_to(t, dst)?;
        }
        let hi = self.pc();
        let n = self.defers.len();
        if n == base {
            return Ok(());
        }
        // The value of the block must survive the deferred statements.
        for i in (base..n).rev() {
            if !self.defers[i].is_err {
                self.copy_defer(i)?;
            }
        }
        for i in (base..n).rev() {
            let d = &self.defers[i];
            let (is_err, body, depth, regbase, vregbase, lo) = (d.is_err, d.body, d.depth, d.regbase, d.vregbase, d.lo);
            let seg = self.label();
            self.pending.push(Segment { label: seg, body, depth, regbase, vregbase });
            self.code.scopes.push(Scope { lo, hi, kind: ScopeKind::Defer { is_err, seg, depth } });
        }
        self.defers.truncate(base);
        Ok(())
    }

    /// Emits a copy of deferred statement `i`, run as the code leaves its block.
    fn copy_defer(&mut self, i: usize) -> C {
        let (body, depth) = (self.defers[i].body, self.defers[i].depth);
        let outer = self.defers.split_off(i);
        let saved_guard = (self.guard, self.guard_labels);
        self.guard = Some(body.span);
        self.guard_labels = self.labels.len();
        let saved_depth = std::mem::replace(&mut self.depth, depth);
        let lo = self.pc();
        let r = self.with_temps(body.span, |c| c.expr(body).map(|_| ()));
        let hi = self.pc();
        self.code.scopes.push(Scope { lo, hi, kind: ScopeKind::Guard { span: body.span } });
        self.depth = saved_depth;
        (self.guard, self.guard_labels) = saved_guard;
        self.defers.extend(outer);
        r
    }

    /// Emits the code of the deferred statements for flows that pass through them, after
    /// the function's body.
    pub fn segments(&mut self) -> C {
        while let Some(s) = self.pending.pop() {
            self.bind(s.label);
            self.next = s.regbase;
            self.vnext = s.vregbase;
            self.depth = s.depth;
            self.labels.clear();
            self.defers.clear();
            self.temps.clear();
            self.guard = Some(s.body.span);
            self.guard_labels = 0;
            let lo = self.pc();
            self.with_temps(s.body.span, |c| c.expr(s.body).map(|_| ()))?;
            let hi = self.pc();
            self.emit(Op::EndSeg, s.body.span);
            self.code.scopes.push(Scope { lo, hi, kind: ScopeKind::Guard { span: s.body.span } });
        }
        Ok(())
    }

    /// Before a jump out of the scopes entered since there were `defers` deferred statements
    /// and `temps` statements: runs the deferred statements (not the `errdefer`s), kills the
    /// temporaries of the statements left if `kill`, and restores the handler context.
    fn exit_to(&mut self, defers: usize, temps: usize, depth: u32, kill: bool, span: Span) -> C {
        let mut cur = self.depth;
        for i in (defers..self.defers.len()).rev() {
            if self.defers[i].is_err {
                continue;
            }
            let d = self.defers[i].depth;
            if cur != d {
                self.emit(Op::RestoreCtx { depth: d }, span);
                cur = d;
            }
            self.copy_defer(i)?;
        }
        if kill && self.temps.len() > temps {
            let r = self.temps[temps].0;
            let k = self.emit(Op::KillTemps { src: r }, span) as usize;
            self.temps[temps].1.push(k);
        }
        if cur != depth {
            self.emit(Op::RestoreCtx { depth }, span);
        }
        Ok(())
    }

    /// Returns the value in `v` from the function, running the deferred statements it leaves.
    pub fn ret(&mut self, v: Arg, err: ErrTest, span: Span) -> C {
        if let Some(g) = self.guard {
            self.trap_const(g, DEFER_ESCAPE);
            return Ok(());
        }
        if self.defers.is_empty() {
            self.emit(Op::Ret { src: v }, span);
            return Ok(());
        }
        let v = match v {
            Arg::Data { reg, size } if reg < self.locals_end && size > 0 => {
                let t = self.alloc(size);
                let d = Arg::Data { reg: t, size };
                self.mov(d, v, span);
                d
            }
            _ => v,
        };
        let ret = self.f.ret;
        let flag = match ret.kind() {
            TyKind::Result { .. } => Some(self.m.ti(ret).offsets[0] as u32),
            _ => None,
        };
        let mut cur = self.depth;
        for i in (0..self.defers.len()).rev() {
            let d = &self.defers[i];
            let skip = if d.is_err {
                match (err, flag, v) {
                    (ErrTest::Yes, _, _) => None,
                    (ErrTest::Dynamic, Some(off), Arg::Data { reg, .. }) => {
                        let l = self.label();
                        self.emit(Op::JmpByte { c: reg, off, t: l, when: false }, span);
                        Some(l)
                    }
                    _ => continue,
                }
            } else {
                None
            };
            let d = self.defers[i].depth;
            if cur != d {
                self.emit(Op::RestoreCtx { depth: d }, span);
                cur = d;
            }
            self.copy_defer(i)?;
            if let Some(l) = skip {
                self.bind(l);
            }
        }
        self.emit(Op::Ret { src: v }, span);
        Ok(())
    }

    fn stmt(&mut self, s: &'a Stmt) -> C {
        match s {
            Stmt::Expr(e) => {
                self.expr(e)?;
            }
            Stmt::Let { local, init } => {
                let lt = self.f.locals[local.index()].ty;
                match (init, self.locals[local.index()]) {
                    (Some(e), LocalLoc::Reg { reg, size }) if self.size(e.ty) == size => {
                        self.expr_to(e, Arg::Data { reg, size })?;
                    }
                    (Some(e), _) => {
                        let v = self.expr(e)?;
                        self.write_local(*local, v, e.span)?;
                    }
                    (None, LocalLoc::Reg { reg, size }) => {
                        if size > 0 {
                            self.emit(Op::Zero { dst: reg, words: words(size) }, self.f.span);
                        }
                    }
                    (None, LocalLoc::Frame) => {
                        self.emit(Op::InitLocal { local: *local, ty: lt }, self.f.span);
                    }
                    (None, LocalLoc::Val) => {}
                }
            }
            Stmt::LetTuple { locals, init } => {
                let v = self.expr(init)?;
                let Arg::Data { reg, size } = v else { return Err(unsupported!()) };
                for (i, l) in locals.iter().enumerate() {
                    if let Some(l) = l {
                        let (fty, off) = self.m.field_layout(init.ty, i).map_err(|_| unsupported!())?;
                        let fsize = self.size(fty);
                        let r = self.sub(reg, size, off as u32, fsize, init.span);
                        self.write_local(*l, Arg::Data { reg: r, size: fsize }, init.span)?;
                    }
                }
            }
            Stmt::Assign { place, value } => {
                if let ExprKind::Local(id) = place.kind {
                    match self.locals[id.index()] {
                        LocalLoc::Reg { reg, size } if value.ty == place.ty && single_write(value) => {
                            self.expr_to(value, Arg::Data { reg, size })?;
                        }
                        _ => {
                            let v = self.expr(value)?;
                            self.write_local(id, v, place.span)?;
                        }
                    }
                    return Ok(());
                }
                let p = self.place(place)?;
                let p = self.stable_place(p, &[value], place.span);
                let v = self.expr(value)?;
                let Arg::Data { reg, size } = v else { return Err(unsupported!()) };
                self.store(p, place.ty, reg, size, place.span);
            }
            Stmt::AssignTuple { places, value } => {
                let mut dests = Vec::with_capacity(places.len());
                for p in places {
                    dests.push(match p {
                        Some(pe) => match pe.kind {
                            ExprKind::Local(id) => Some(Dest::Local(id)),
                            _ => {
                                let pl = self.place(pe)?;
                                Some(Dest::Place(self.stable_place(pl, &[value], pe.span)))
                            }
                        },
                        None => None,
                    });
                }
                let v = self.expr(value)?;
                let Arg::Data { reg, size } = v else { return Err(unsupported!()) };
                for (i, d) in dests.into_iter().enumerate() {
                    let (Some(d), Some(pe)) = (d, &places[i]) else { continue };
                    let (fty, off) = self.m.field_layout(value.ty, i).map_err(|_| unsupported!())?;
                    let fsize = self.size(fty);
                    let r = self.sub(reg, size, off as u32, fsize, value.span);
                    match d {
                        Dest::Local(id) => self.write_local(id, Arg::Data { reg: r, size: fsize }, pe.span)?,
                        Dest::Place(p) => self.store(p, pe.ty, r, fsize, pe.span),
                    }
                }
            }
            Stmt::Defer { .. } => {}
        }
        Ok(())
    }

    /// A copy of a place's address in a temporary if it is in a local's register that
    /// evaluating `later` may change.
    fn stable_place(&mut self, p: PlaceC, later: &[&Expr], span: Span) -> PlaceC {
        let fix = |c: &mut Self, addr: Reg| -> Reg {
            if addr < c.locals_end && later.iter().any(|x| writes_locals(x)) {
                let t = c.alloc(8);
                c.emit(Op::Mov1 { dst: t, src: addr }, span);
                t
            } else {
                addr
            }
        };
        match p {
            PlaceC::Mem { addr, packed } => PlaceC::Mem { addr: fix(self, addr), packed },
            PlaceC::Bits { addr, bits } => PlaceC::Bits { addr: fix(self, addr), bits },
        }
    }

    /// Stores the value in `v` in local `id`.
    pub fn write_local(&mut self, id: LocalId, v: Arg, span: Span) -> C {
        let ty = self.f.locals[id.index()].ty;
        match (self.locals[id.index()], v) {
            (LocalLoc::Reg { reg, size }, Arg::Data { reg: src, size: vs }) => {
                if vs == size || words(vs) == words(size) && vs > size {
                    self.mov(Arg::Data { reg, size }, v, span);
                } else if reg != src {
                    if size > 0 {
                        self.emit(Op::Zero { dst: reg, words: words(size) }, span);
                    }
                    let n = vs.min(size);
                    if n > 0 {
                        self.insert(reg, 0, src, n, span);
                    }
                }
            }
            (LocalLoc::Frame, Arg::Data { reg, .. }) => {
                self.emit(Op::StLocal { local: id, src: reg, ty }, span);
            }
            (LocalLoc::Val, Arg::Val(r)) => {
                self.emit(Op::StLocalVal { local: id, src: r }, span);
            }
            _ => return Err(unsupported!()),
        }
        Ok(())
    }

    /// The `size` bytes at byte `off` of the value of `total` bytes in `src`, as a register.
    pub fn sub(&mut self, src: Reg, total: u32, off: u32, size: u32, span: Span) -> Reg {
        if off == 0 && (size.is_multiple_of(8) || size == total) {
            return src;
        }
        let t = self.alloc(size);
        self.extract(t, src, off, size, span);
        t
    }

    // --- conditions ------------------------------------------------------------------------------

    /// Jumps to `f` unless every part of a condition holds (binding its patterns' locals).
    pub fn jconds(&mut self, conds: &'a [CondPart], f: u32) -> C {
        for c in conds {
            match c {
                CondPart::Bool(e) => self.jfalse(e, f)?,
                CondPart::Match { pat, value } => {
                    let mark = self.next;
                    let v = self.expr(value)?;
                    let Arg::Data { reg, .. } = v else { return Err(unsupported!()) };
                    self.pat(pat, reg, f)?;
                    self.next = mark;
                }
            }
        }
        Ok(())
    }

    /// Jumps to `t` if the condition `e` is false.
    pub fn jfalse(&mut self, e: &'a Expr, t: u32) -> C {
        self.jcond(e, t, false)
    }

    /// Jumps to `t` if the condition `e` is true.
    pub fn jtrue(&mut self, e: &'a Expr, t: u32) -> C {
        self.jcond(e, t, true)
    }

    fn jcond(&mut self, e: &'a Expr, t: u32, when: bool) -> C {
        let span = e.span;
        let mark = self.next;
        match &e.kind {
            ExprKind::Bool(b) => {
                if *b == when {
                    self.emit(Op::Jmp { t }, span);
                }
            }
            ExprKind::Logic { is_and, lhs, rhs } => {
                // `a and b` is false if `a` is; `a or b` is true if `a` is.
                if *is_and != when {
                    self.jcond(lhs, t, when)?;
                    self.jcond(rhs, t, when)?;
                } else {
                    let skip = self.label();
                    self.jcond(lhs, skip, !when)?;
                    self.jcond(rhs, t, when)?;
                    self.bind(skip);
                }
            }
            ExprKind::Unary { op: UnOp::Not, operand } => self.jcond(operand, t, !when)?,
            ExprKind::Binary { op, lhs, rhs }
                if Cmp::of(*op).is_some() && self.small_binary(*op, lhs, rhs, e.ty).is_some() =>
            {
                let (k, _) = self.small_binary(*op, lhs, rhs, e.ty).unwrap();
                let ops = self.operands(&[lhs, rhs], span)?;
                let (Arg::Data { reg: a, .. }, Arg::Data { reg: b, .. }) = (ops[0], ops[1]) else {
                    return Err(unsupported!());
                };
                let c = Cmp::of(*op).unwrap();
                let cmp = if when { c } else { negate(c) };
                self.emit(Op::BrI { cmp, a, b, k, t }, span);
            }
            ExprKind::Binary { op, lhs, rhs }
                if Cmp::of(*op).is_some() && self.float_binary(*op, lhs, rhs).is_some() =>
            {
                let k = self.float_binary(*op, lhs, rhs).unwrap();
                let ops = self.operands(&[lhs, rhs], span)?;
                let (Arg::Data { reg: a, .. }, Arg::Data { reg: b, .. }) = (ops[0], ops[1]) else {
                    return Err(unsupported!());
                };
                // Not `a < b` is not `a >= b` when either is a NaN: jump past instead.
                let c = Cmp::of(*op).unwrap();
                if when {
                    self.emit(Op::BrF { cmp: c, a, b, k, t }, span);
                } else {
                    let skip = self.label();
                    self.emit(Op::BrF { cmp: c, a, b, k, t: skip }, span);
                    self.emit(Op::Jmp { t }, span);
                    self.bind(skip);
                }
            }
            ExprKind::Eq { lhs, rhs, negated } if float_eq(lhs, rhs).is_some() => {
                let k = float_eq(lhs, rhs).unwrap();
                let ops = self.operands(&[lhs, rhs], span)?;
                let (Arg::Data { reg: a, .. }, Arg::Data { reg: b, .. }) = (ops[0], ops[1]) else {
                    return Err(unsupported!());
                };
                // `==` and `!=` negate each other also for NaNs.
                let cmp = if when != *negated { Cmp::Eq } else { Cmp::Ne };
                self.emit(Op::BrF { cmp, a, b, k, t }, span);
            }
            ExprKind::Eq { lhs, rhs, negated } if self.small_eq(lhs, rhs).is_some() => {
                let k = self.small_eq(lhs, rhs).unwrap();
                let ops = self.operands(&[lhs, rhs], span)?;
                let (Arg::Data { reg: a, .. }, Arg::Data { reg: b, .. }) = (ops[0], ops[1]) else {
                    return Err(unsupported!());
                };
                // Jumps when `(a is b) != negated` is `when`.
                let cmp = if when != *negated { Cmp::Eq } else { Cmp::Ne };
                self.emit(Op::BrI { cmp, a, b, k, t }, span);
            }
            _ => {
                let v = self.expr(e)?;
                let Arg::Data { reg, .. } = v else { return Err(unsupported!()) };
                if when {
                    self.emit(Op::JmpIf { c: reg, t }, span);
                } else {
                    self.emit(Op::JmpIfNot { c: reg, t }, span);
                }
            }
        }
        self.next = mark;
        Ok(())
    }

    // --- patterns ---------------------------------------------------------------------------------

    /// Matches the value in `src` (of type `p.ty`) against `p`, binding its locals; jumps to
    /// `fail` if it does not match.
    pub fn pat(&mut self, p: &'a Pat, src: Reg, fail: u32) -> C {
        let span = p.span;
        let ty = p.ty;
        let total = self.size(ty);
        match &p.kind {
            PatKind::Wild => {}
            PatKind::Bind(l) => self.write_local(*l, Arg::Data { reg: src, size: total }, span)?,
            PatKind::Const(e) => {
                let c = self.expr(e)?;
                let Arg::Data { reg: c, .. } = c else { return Err(unsupported!()) };
                if e.ty != ty
                    && let TyKind::Optional(inner) = ty.kind()
                    && *inner == e.ty
                {
                    self.jmp_present(src, ty, fail, false, span);
                    let off =
                        if crate::layout::is_nullable_pointer(*inner) { 0 } else { self.m.ti(ty).offsets[0] as u32 };
                    let isz = self.size(*inner);
                    let pv = self.sub(src, total, off, isz, span);
                    self.jne(e.ty, pv, c, fail, span);
                } else {
                    self.jne(ty, src, c, fail, span);
                }
            }
            PatKind::Range { lo, hi, inclusive } => {
                let small = self.small(ty).filter(|k| {
                    let si = k.si();
                    let fit = |v: &num_bigint::BigInt| num_traits::ToPrimitive::to_i128(v).is_some_and(|x| si.fits(x));
                    fit(lo) && fit(hi)
                });
                match small {
                    Some(k) => {
                        let si = self.m.small_of(ty).unwrap();
                        let to = |v: &num_bigint::BigInt| num_traits::ToPrimitive::to_i128(v).unwrap();
                        let (l, h) = (to(lo), to(hi));
                        let lr = self.alloc(8);
                        let b = self.m.encode_small(si, l);
                        self.const_bytes(lr, &b, span);
                        self.emit(Op::BrI { cmp: Cmp::Lt, a: src, b: lr, k, t: fail }, span);
                        let hr = self.alloc(8);
                        let b = self.m.encode_small(si, h);
                        self.const_bytes(hr, &b, span);
                        let cmp = if *inclusive { Cmp::Gt } else { Cmp::Ge };
                        self.emit(Op::BrI { cmp, a: src, b: hr, k, t: fail }, span);
                    }
                    None => {
                        self.code.ranges.push((lo.clone(), hi.clone(), *inclusive));
                        let info = self.code.ranges.len() as u32 - 1;
                        let d = self.alloc(1);
                        self.emit(Op::PatRange { dst: d, src, ty, info }, span);
                        self.emit(Op::JmpIfNot { c: d, t: fail }, span);
                    }
                }
            }
            PatKind::Null => self.jmp_present(src, ty, fail, true, span),
            PatKind::Some(q) => {
                self.jmp_present(src, ty, fail, false, span);
                let inner = self.m.opt_inner(ty);
                let off = if crate::layout::is_nullable_pointer(inner) { 0 } else { self.m.ti(ty).offsets[0] as u32 };
                let isz = self.size(inner);
                let pv = self.sub(src, total, off, isz, span);
                self.pat(q, pv, fail)?;
            }
            PatKind::Ok(q) | PatKind::Err(q) => {
                let is_ok = matches!(p.kind, PatKind::Ok(_));
                let (err, ok) = self.m.res_types(ty);
                let ti = self.m.ti(ty);
                let (flag, off) = (ti.offsets[0] as u32, ti.offsets[1] as u32);
                self.emit(Op::JmpByte { c: src, off: flag, t: fail, when: is_ok }, span);
                let psize = self.size(if is_ok { ok } else { err });
                let pv = self.sub(src, total, off, psize, span);
                self.pat(q, pv, fail)?;
            }
            PatKind::Variant { variant, payload } => {
                let test = self.tag_test(ty, *variant)?;
                self.emit(Op::JmpTag { src, test, t: fail, when: false }, span);
                if let Some(q) = payload {
                    let pt = self.m.variant_payload_ty(ty, *variant as usize).unwrap_or(q.ty);
                    let off = self.m.payload_offset(ty) as u32;
                    let psize = self.size(pt);
                    let pv = self.sub(src, total, off, psize, span);
                    self.pat(q, pv, fail)?;
                }
            }
            PatKind::Or(alts) => {
                let ok = self.label();
                for (i, q) in alts.iter().enumerate() {
                    if i + 1 == alts.len() {
                        self.pat(q, src, fail)?;
                    } else {
                        let next = self.label();
                        self.pat(q, src, next)?;
                        self.emit(Op::Jmp { t: ok }, span);
                        self.bind(next);
                    }
                }
                self.bind(ok);
            }
            PatKind::Fields(fs) => {
                let bits = self.m.bits_struct_ty(ty).is_some();
                for (i, q) in fs {
                    let fv = if bits {
                        let (_, fty) = self.m.bits_field(ty, *i as usize).map_err(|_| unsupported!())?;
                        let d = self.alloc(self.size(fty));
                        self.emit(Op::BitsField { dst: d, src, ty, idx: *i }, span);
                        d
                    } else {
                        let (fty, off) = self.m.field_layout(ty, *i as usize).map_err(|_| unsupported!())?;
                        let fsize = self.size(fty);
                        self.sub(src, total, off as u32, fsize, span)
                    };
                    self.pat(q, fv, fail)?;
                }
            }
        }
        Ok(())
    }

    /// Jumps to `t` unless the values of type `ty` in `a` and `b` are equal (`is`).
    fn jne(&mut self, ty: Ty, a: Reg, b: Reg, t: u32, span: Span) {
        if let Some(k) = self.small(ty) {
            self.emit(Op::BrI { cmp: Cmp::Ne, a, b, k, t }, span);
            return;
        }
        let d = self.alloc(1);
        match ty.kind() {
            TyKind::Bool => self.emit(Op::EqBool { dst: d, a, b, neg: false }, span),
            _ => self.emit(Op::EqG { dst: d, a, b, ty, neg: false }, span),
        };
        self.emit(Op::JmpIfNot { c: d, t }, span);
    }

    // --- structured control flow ---------------------------------------------------------------------

    pub fn match_(&mut self, e: &'a Expr, scrut: &'a Expr, arms: &'a [Arm], want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        let v = self.expr(scrut)?;
        let guards: Vec<&Expr> = arms.iter().filter_map(|a| a.guard.as_ref()).collect();
        let v = self.stable(v, &guards, span);
        let Arg::Data { reg, .. } = v else { return Err(unsupported!()) };
        let dst = self.dst(e, want);
        let end = self.label();
        for arm in arms {
            let next = self.label();
            let mark = self.next;
            self.pat(&arm.pat, reg, next)?;
            if let Some(g) = &arm.guard {
                self.jfalse(g, next)?;
            }
            self.next = mark;
            self.expr_to(&arm.body, dst)?;
            self.emit(Op::Jmp { t: end }, span);
            self.bind(next);
        }
        self.trap_const(span, "no match arm matched the scrutinee");
        self.bind(end);
        Ok(dst)
    }

    /// Writes the value of `lhs else …` when `lhs` (in `v`) has a value: its payload of type
    /// `pty` at byte `off`, as the type of `e` requires.
    #[allow(clippy::too_many_arguments)]
    fn else_value(&mut self, e: &Expr, lhs: &Expr, v: Reg, pty: Ty, off: u32, dst: Arg, span: Span) {
        let Arg::Data { reg: d, size } = dst else { return };
        let total = self.size(lhs.ty);
        let psize = self.size(pty);
        if e.ty == pty {
            self.extract(d, v, off, size.min(psize), span);
        } else if e.ty == lhs.ty {
            self.mov(dst, Arg::Data { reg: v, size: total }, span);
        } else {
            let p = self.sub(v, total, off, psize, span);
            let pa = Arg::Data { reg: p, size: psize };
            if matches!(e.ty.kind(), TyKind::Optional(_)) {
                self.make_some(d, e.ty, pa, span);
            } else {
                self.make_res(d, e.ty, false, pa, span);
            }
        }
    }

    pub fn else_(
        &mut self,
        e: &'a Expr,
        lhs: &'a Expr,
        capture: Option<LocalId>,
        rhs: &'a Expr,
        want: Option<Arg>,
    ) -> C<Arg> {
        let span = e.span;
        let v = self.expr(lhs)?;
        let Arg::Data { reg: v, .. } = v else { return Err(unsupported!()) };
        let dst = self.dst(e, want);
        let other = self.label();
        let end = self.label();
        match lhs.ty.kind() {
            TyKind::Optional(t) => {
                self.jmp_present(v, lhs.ty, other, false, span);
                let off = if crate::layout::is_nullable_pointer(*t) { 0 } else { self.m.ti(lhs.ty).offsets[0] as u32 };
                self.else_value(e, lhs, v, *t, off, dst, span);
                self.emit(Op::Jmp { t: end }, span);
                self.bind(other);
            }
            TyKind::Result { err, ok } => {
                let ti = self.m.ti(lhs.ty);
                let (flag, off) = (ti.offsets[0] as u32, ti.offsets[1] as u32);
                self.emit(Op::JmpByte { c: v, off: flag, t: other, when: true }, span);
                self.else_value(e, lhs, v, *ok, off, dst, span);
                self.emit(Op::Jmp { t: end }, span);
                self.bind(other);
                if let Some(c) = capture {
                    let total = self.size(lhs.ty);
                    let esize = self.size(*err);
                    let p = self.sub(v, total, off, esize, span);
                    self.write_local(c, Arg::Data { reg: p, size: esize }, span)?;
                }
            }
            _ => return Err(unsupported!()),
        }
        self.expr_to(rhs, dst)?;
        self.bind(end);
        Ok(dst)
    }

    /// The return type of the function `levels` up the chain of enclosing functions.
    fn ret_ty_up(&self, levels: u32) -> C<Ty> {
        let mut f = self.f;
        for _ in 0..levels {
            f = &self.prog.fns[f.parent.ok_or(unsupported!())?.index()];
        }
        Ok(f.ret)
    }

    pub fn try_(&mut self, e: &'a Expr, x: &'a Expr, lift: Option<Lift>, levels: u32, want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        let v = self.expr(x)?;
        let Arg::Data { reg: v, .. } = v else { return Err(unsupported!()) };
        let rty = self.ret_ty_up(levels)?;
        let total = self.size(x.ty);
        let dst = self.dst(e, want);
        let ok = self.label();
        let (off, psize, out) = match x.ty.kind() {
            TyKind::Optional(t) => {
                self.jmp_present(v, x.ty, ok, true, span);
                let rsize = self.size(rty);
                let r = self.alloc(rsize);
                if rsize > 0 {
                    self.emit(Op::Zero { dst: r, words: words(rsize) }, span);
                }
                self.leave(Arg::Data { reg: r, size: rsize }, levels, ErrTest::No, span)?;
                let off = if crate::layout::is_nullable_pointer(*t) { 0 } else { self.m.ti(x.ty).offsets[0] as u32 };
                (off, self.size(*t), dst)
            }
            TyKind::Result { err, ok: okt } => {
                let ti = self.m.ti(x.ty);
                let (flag, off) = (ti.offsets[0] as u32, ti.offsets[1] as u32);
                self.emit(Op::JmpByte { c: v, off: flag, t: ok, when: false }, span);
                let esize = self.size(*err);
                let ev = self.sub(v, total, off, esize, span);
                let mut ev = Arg::Data { reg: ev, size: esize };
                let (rerr, _) = self.m.res_types(rty);
                if let Some(Lift::Variant(var)) = lift {
                    let l = self.alloc(self.size(rerr));
                    self.make_variant(l, rerr, var, Some(ev), span)?;
                    ev = Arg::Data { reg: l, size: self.size(rerr) };
                }
                let rsize = self.size(rty);
                let r = self.alloc(rsize);
                self.make_res(r, rty, true, ev, span);
                self.leave(Arg::Data { reg: r, size: rsize }, levels, ErrTest::Yes, span)?;
                (off, self.size(*okt), dst)
            }
            _ => return Err(unsupported!()),
        };
        self.bind(ok);
        if let Arg::Data { reg, size } = out {
            self.extract(reg, v, off, size.min(psize), span);
        }
        Ok(out)
    }

    /// Returns `v` from the function `levels` up.
    fn leave(&mut self, v: Arg, levels: u32, err: ErrTest, span: Span) -> C {
        if levels == 0 {
            self.ret(v, err, span)
        } else {
            self.emit(Op::ThrowReturn { levels, v }, span);
            Ok(())
        }
    }

    pub fn loop_(
        &mut self,
        e: &'a Expr,
        label: LabelId,
        cond: &'a [CondPart],
        body: &'a Block,
        step: Option<&'a Expr>,
        want: Option<Arg>,
    ) -> C<Arg> {
        let span = e.span;
        let dst = self.dst(e, want);
        // The mark lets flows that land here from calls kill the temporaries of the
        // statements they leave; each iteration also kills those of the last one.
        let mreg = self.alloc(8);
        let mark_op = self.emit(Op::MarkTemps { dst: mreg }, span) as usize;
        let count = self.temp_count;
        let head = self.label();
        self.bind(head);
        let kill_nop = self.emit(Op::Nop, span) as usize;
        let brk = self.label();
        let cont = self.label();
        let saved = std::mem::replace(&mut self.made_temp, false);
        self.jconds(cond, brk)?;
        self.labels.push(LabelCx {
            label,
            brk,
            cont,
            result: dst,
            depth: self.depth,
            defers: self.defers.len(),
            temps: self.temps.len(),
            is_loop: true,
        });
        let lo = self.pc();
        let mark = self.next;
        let unit = self.alloc(0);
        self.block(body, Arg::Data { reg: unit, size: 0 })?;
        self.next = mark;
        let hi = self.pc();
        self.labels.pop();
        self.bind(cont);
        if let Some(s) = step {
            self.with_temps(s.span, |c| c.expr(s).map(|_| ()))?;
        }
        // The jump back lets other threads run and checks the step budget.
        self.emit(Op::JmpTick { t: head }, span);
        self.bind(brk);
        if self.made_temp {
            self.code.ops[kill_nop] = Op::KillTemps { src: mreg };
        }
        let temps = if self.temp_count != count {
            Some(mreg)
        } else {
            self.code.ops[mark_op] = Op::Nop;
            None
        };
        self.made_temp |= saved;
        self.code.scopes.push(Scope {
            lo,
            hi,
            kind: ScopeKind::Label { label, brk, cont, depth: self.depth, result: dst, temps },
        });
        Ok(dst)
    }

    pub fn with(
        &mut self,
        e: &'a Expr,
        label: LabelId,
        handlers: &'a [HandlerValue],
        body: &'a Expr,
        want: Option<Arg>,
    ) -> C<Arg> {
        let span = e.span;
        let dst = self.dst(e, want);
        let outside = self.depth;
        self.emit(Op::PushCtx, span);
        self.depth += 1;
        for h in handlers {
            let mark = self.next;
            let vmark = self.vnext;
            match h {
                HandlerValue::Literal(x) => {
                    let v = self.expr(x)?;
                    let Arg::Val(v) = v else { return Err(unsupported!()) };
                    self.emit(Op::InstallLit { v, label }, x.span);
                }
                HandlerValue::Decl { decls, value } => {
                    let v = self.expr(value)?;
                    let Arg::Data { reg, size } = v else { return Err(unsupported!()) };
                    self.code.decls.push(decls.clone().into());
                    let info = self.code.decls.len() as u32 - 1;
                    self.emit(Op::InstallDecl { info, src: reg, size, label }, value.span);
                }
            }
            self.next = mark;
            self.vnext = vmark;
        }
        let brk = self.label();
        self.labels.push(LabelCx {
            label,
            brk,
            cont: brk,
            result: dst,
            depth: outside,
            defers: self.defers.len(),
            temps: self.temps.len(),
            is_loop: false,
        });
        let lo = self.pc();
        let r = self.expr_to(body, dst);
        let hi = self.pc();
        self.labels.pop();
        r?;
        self.emit(Op::PopCtx, span);
        self.depth = outside;
        self.bind(brk);
        self.code.scopes.push(Scope {
            lo,
            hi,
            kind: ScopeKind::Label { label, brk, cont: brk, depth: outside, result: dst, temps: None },
        });
        Ok(dst)
    }

    /// The label of this function's loop or `with` `label`, if a jump can reach it.
    fn find_label(&self, label: LabelId) -> C<usize> {
        self.labels.iter().rposition(|l| l.label == label).ok_or(unsupported!())
    }

    pub fn break_(&mut self, e: &'a Expr, target: Target, value: Option<&'a Expr>) -> C {
        let span = e.span;
        match target {
            Target::InstallingWith => {
                let v = match value {
                    Some(x) => self.expr(x)?,
                    None => Arg::UNIT,
                };
                self.emit(Op::ThrowBreakInst { v }, span);
            }
            Target::Label { levels, label } if levels > 0 => {
                let v = match value {
                    Some(x) => self.expr(x)?,
                    None => Arg::UNIT,
                };
                self.emit(Op::ThrowBreak { levels, label, v }, span);
            }
            Target::Label { label, .. } => {
                let i = self.find_label(label)?;
                let (result, defers, temps, depth, brk) = {
                    let l = &self.labels[i];
                    (l.result, l.defers, l.temps, l.depth, l.brk)
                };
                if let Some(x) = value {
                    self.expr_to(x, result)?;
                }
                if let Some(g) = self.guard
                    && i < self.guard_labels
                {
                    self.trap_const(g, DEFER_ESCAPE);
                    return Ok(());
                }
                self.exit_to(defers, temps, depth, true, span)?;
                self.emit(Op::Jmp { t: brk }, span);
            }
        }
        Ok(())
    }

    pub fn continue_(&mut self, e: &'a Expr, target: Target) -> C {
        let span = e.span;
        match target {
            Target::InstallingWith => self.ret(Arg::UNIT, ErrTest::No, span)?,
            Target::Label { levels, label } if levels > 0 => {
                self.emit(Op::ThrowContinue { levels, label }, span);
            }
            Target::Label { label, .. } => {
                let i = self.find_label(label)?;
                if !self.labels[i].is_loop {
                    return Err(unsupported!());
                }
                if let Some(g) = self.guard
                    && i < self.guard_labels
                {
                    self.trap_const(g, DEFER_ESCAPE);
                    return Ok(());
                }
                let (defers, temps, depth, cont) = {
                    let l = &self.labels[i];
                    (l.defers, l.temps, l.depth, l.cont)
                };
                self.exit_to(defers, temps, depth, false, span)?;
                self.emit(Op::Jmp { t: cont }, span);
            }
        }
        Ok(())
    }

    /// Resolves jumps and drops the `Nop`s left by statements that make no temporaries.
    pub fn finish(mut self) -> C<Code<'a>> {
        let n = self.code.ops.len();
        let mut map = Vec::with_capacity(n + 1);
        let mut j = 0u32;
        for op in &self.code.ops {
            map.push(j);
            if !matches!(op, Op::Nop) {
                j += 1;
            }
        }
        map.push(j);
        let at = |l: u32| map[self.targets[l as usize] as usize];
        for op in self.code.ops.iter_mut() {
            let t = match op {
                Op::Jmp { t }
                | Op::JmpIf { t, .. }
                | Op::JmpIfNot { t, .. }
                | Op::JmpNZ { t, .. }
                | Op::JmpZ { t, .. }
                | Op::JmpByte { t, .. }
                | Op::JmpTag { t, .. }
                | Op::JmpFast { t }
                | Op::JmpTick { t }
                | Op::BrI { t, .. }
                | Op::BrF { t, .. } => t,
                _ => continue,
            };
            *t = at(*t);
        }
        for s in self.code.scopes.iter_mut() {
            s.lo = map[s.lo as usize];
            s.hi = map[s.hi as usize];
            match &mut s.kind {
                ScopeKind::Label { brk, cont, .. } => {
                    *brk = at(*brk);
                    *cont = at(*cont);
                }
                ScopeKind::Defer { seg, .. } => *seg = at(*seg),
                ScopeKind::Guard { .. } => {}
            }
        }
        let mut ops = Vec::with_capacity(j as usize);
        let mut spans = Vec::with_capacity(j as usize);
        for (op, sp) in self.code.ops.iter().zip(&self.code.spans) {
            if !matches!(op, Op::Nop) {
                ops.push(*op);
                spans.push(*sp);
            }
        }
        // A loop's jump back to a test that leaves the loop becomes one instruction.
        for i in 0..ops.len() {
            if let Op::JmpTick { t } = ops[i]
                && let Op::BrI { cmp, a, b, k, t: exit } = ops[t as usize]
            {
                ops[i] = Op::TickBr { cmp, a, b, k, exit, body: t + 1 };
            }
        }
        // Running past the end of the code is a bug in the compiler; this catches it.
        self.code.msgs.push("internal error: ran past the end of a function's code".to_string());
        ops.push(Op::TrapConst { msg: self.code.msgs.len() as u32 - 1 });
        spans.push(spans.last().copied().unwrap_or(Span::DUMMY));
        self.code.ops = ops;
        self.code.spans = spans;
        self.code.nregs = self.high;
        self.code.nslots =
            self.locals.iter().rposition(|l| !matches!(l, LocalLoc::Reg { .. })).map_or(0, |i| i as u32 + 1);
        let mut init = vec![0u64; self.consts_end as usize];
        for (w, r) in &self.consts {
            init[*r as usize] = *w;
        }
        self.code.init = init;
        self.code.nvals = self.vhigh;
        if !self.code.valid() {
            return Err(unsupported!());
        }
        Ok(self.code)
    }
}

fn negate(c: Cmp) -> Cmp {
    match c {
        Cmp::Lt => Cmp::Ge,
        Cmp::Le => Cmp::Gt,
        Cmp::Gt => Cmp::Le,
        Cmp::Ge => Cmp::Lt,
        Cmp::Eq => Cmp::Ne,
        Cmp::Ne => Cmp::Eq,
    }
}

/// A span for a statement (of its main expression).
fn stmt_span(s: &Stmt, fallback: Span) -> Span {
    match s {
        Stmt::Expr(e) | Stmt::Defer { body: e, .. } => e.span,
        Stmt::Let { init: Some(e), .. } | Stmt::LetTuple { init: e, .. } => e.span,
        Stmt::Assign { place, .. } => place.span,
        Stmt::AssignTuple { value, .. } => value.span,
        Stmt::Let { init: None, .. } => fallback,
    }
}
