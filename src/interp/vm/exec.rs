//! Running compiled code: activations, the dispatch loop and unwinding.

use super::*;
use crate::interp::num::Int;

const DEFER_ESCAPE: &str = "internal error: control flow escaped a deferred statement";

/// One activation of a compiled function.
pub struct St<'a> {
    code: Rc<Code<'a>>,
    regs: Vec<u64>,
    vals: Vec<Value>,
    fr: Rc<Frame>,
    /// The handler context, and the contexts outside the `with`s being run.
    cx: Ctx,
    cx_stack: Vec<Ctx>,
}

/// How running code stops.
pub enum Exit {
    Ret(Arg),
    /// A `return` that arrived as a flow from a call (or the tree-walker).
    Value(Value),
    /// The end of a deferred statement's code.
    Seg,
}

/// Where unwinding a flow ends in this activation.
enum Land {
    Pc(u32),
    Exit(Exit),
}

// --- registers ------------------------------------------------------------------------------------

/// The `size` bytes of the value at `r`.
#[inline]
fn get_bytes(regs: &[u64], r: Reg, size: u32) -> Bytes {
    let n = size as usize;
    let r = r as usize;
    if n <= 24 {
        let mut a = [0u8; 24];
        for i in 0..n.div_ceil(8) {
            a[i * 8..i * 8 + 8].copy_from_slice(&regs[r + i].to_le_bytes());
        }
        Bytes::Inline(n as u8, a)
    } else {
        let mut v = Vec::with_capacity(n.div_ceil(8) * 8);
        for i in 0..n.div_ceil(8) {
            v.extend_from_slice(&regs[r + i].to_le_bytes());
        }
        v.truncate(n);
        Bytes::Heap(v)
    }
}

/// Writes the value of `size` bytes whose first bytes are `b` (zeros after them) at `r`.
#[inline]
fn put_bytes(regs: &mut [u64], r: Reg, size: u32, b: &[u8]) {
    let n = (size as usize).min(b.len());
    let r = r as usize;
    for i in 0..words(size) as usize {
        let lo = i * 8;
        let w = if lo + 8 <= n {
            u64::from_le_bytes(b[lo..lo + 8].try_into().unwrap())
        } else if lo < n {
            let mut t = [0u8; 8];
            t[..n - lo].copy_from_slice(&b[lo..n]);
            u64::from_le_bytes(t)
        } else {
            0
        };
        regs[r + i] = w;
    }
}

#[inline(always)]
fn mask(size: u32) -> u64 {
    if size >= 8 { u64::MAX } else { (1u64 << (8 * size)) - 1 }
}

#[inline(always)]
fn byte_at(regs: &[u64], r: Reg, i: u32) -> u8 {
    (regs[(r + i / 8) as usize] >> (8 * (i % 8))) as u8
}

#[inline(always)]
fn set_byte(regs: &mut [u64], r: Reg, i: u32, b: u8) {
    let w = &mut regs[(r + i / 8) as usize];
    let sh = 8 * (i % 8);
    *w = (*w & !(0xffu64 << sh)) | ((b as u64) << sh);
}

/// Copies `size` bytes at byte `off` of the value at `src` to the value at `dst` (whole).
#[inline]
fn extract(regs: &mut [u64], dst: Reg, src: Reg, off: u32, size: u32) {
    if off % 8 == 0 {
        let s = (src + off / 8) as usize;
        let w = words(size) as usize;
        regs.copy_within(s..s + w, dst as usize);
        if size % 8 != 0 {
            regs[dst as usize + w - 1] &= mask(size % 8);
        }
    } else if off % 8 + size <= 8 {
        let w = regs[(src + off / 8) as usize] >> (8 * (off % 8));
        regs[dst as usize] = w & mask(size);
    } else {
        let mut t = [0u8; 64];
        let mut v = Vec::new();
        let buf: &mut [u8] = if size as usize <= t.len() {
            &mut t[..size as usize]
        } else {
            v.resize(size as usize, 0);
            &mut v
        };
        for (i, b) in buf.iter_mut().enumerate() {
            *b = byte_at(regs, src, off + i as u32);
        }
        put_bytes(regs, dst, size, buf);
    }
}

/// Copies the `size` bytes of the value at `src` to byte `off` of the value at `dst`.
#[inline]
fn insert(regs: &mut [u64], dst: Reg, off: u32, src: Reg, size: u32) {
    if off % 8 == 0 {
        let d = (dst + off / 8) as usize;
        let full = (size / 8) as usize;
        regs.copy_within(src as usize..src as usize + full, d);
        if size % 8 != 0 {
            let m = mask(size % 8);
            let v = regs[src as usize + full] & m;
            let w = &mut regs[d + full];
            *w = (*w & !m) | v;
        }
    } else if off % 8 + size <= 8 {
        let sh = 8 * (off % 8);
        let m = mask(size) << sh;
        let v = (regs[src as usize] << sh) & m;
        let w = &mut regs[(dst + off / 8) as usize];
        *w = (*w & !m) | v;
    } else {
        for i in 0..size {
            let b = byte_at(regs, src, i);
            set_byte(regs, dst, off + i, b);
        }
    }
}

/// The value of the small integer of kind `k` whose bytes are the word `w`.
#[inline(always)]
fn ival<const LE: bool>(k: IntK, w: u64) -> i128 {
    let w = if LE { w } else { w.swap_bytes() >> (64 - 8 * k.size as u32) };
    k.si().value(w)
}

/// The bytes of `v` as a small integer of kind `k`, as a word.
#[inline(always)]
fn iput<const LE: bool>(k: IntK, v: i128) -> u64 {
    let w = v as u64;
    if LE { w & mask(k.size as u32) } else { (w << (64 - 8 * k.size as u32)).swap_bytes() }
}

/// An index operand: negative values are 0 (and out of bounds of anything but an empty
/// many-pointer access, as in the tree-walker).
#[inline(always)]
fn index<const LE: bool>(k: IntK, w: u64) -> u64 {
    let v = ival::<LE>(k, w);
    if v < 0 { 0 } else { v as u64 }
}

impl St<'_> {
    fn value_of(&self, a: Arg) -> Value {
        match a {
            Arg::Data { reg, size } => Value::Bytes(get_bytes(&self.regs, reg, size)),
            Arg::Val(r) => self.vals[r as usize].clone(),
        }
    }

    fn values(&self, list: &[Arg]) -> Vec<Value> {
        list.iter().map(|a| self.value_of(*a)).collect()
    }

    fn bytes(&self, list: &[Arg]) -> Vec<Bytes> {
        list.iter()
            .map(|a| match a {
                Arg::Data { reg, size } => get_bytes(&self.regs, *reg, *size),
                Arg::Val(_) => Bytes::empty(),
            })
            .collect()
    }

    fn restore_ctx(&mut self, depth: u32) {
        let d = depth as usize;
        if self.cx_stack.len() > d {
            self.cx = self.cx_stack[d].clone();
            self.cx_stack.truncate(d);
        }
    }
}

fn write_value(st: &mut St, out: Arg, v: Value, span: Span) -> R<()> {
    match (out, v) {
        (Arg::Data { reg, size }, Value::Bytes(b)) => {
            put_bytes(&mut st.regs, reg, size, &b);
            Ok(())
        }
        (Arg::Data { size: 0, .. }, _) => Ok(()),
        (Arg::Val(r), v) => {
            st.vals[r as usize] = v;
            Ok(())
        }
        _ => Err(trap_flow(span, "internal error: a second-class value was used as data")),
    }
}

impl<'a> Machine<'a> {
    // --- calls ----------------------------------------------------------------------------------------

    /// The checks of a call of compiled function `fid` with `nargs` arguments; returns
    /// the stack it is charged on a fiber.
    fn vm_prologue(&mut self, span: Span, fid: FnId, nargs: usize) -> R<u64> {
        let f = &self.prog.fns[fid.index()];
        if self.depth >= MAX_DEPTH {
            return self.trap(span, "stack overflow");
        }
        let marker = 0u8;
        if (&marker as *const u8 as usize) < self.stack_floor {
            return self.trap(span, "stack overflow");
        }
        self.tick()?;
        self.check_budget(span)?;
        if nargs != f.params.len() {
            return self.trap(
                span,
                format!("internal error: `{}` called with {} arguments, expected {}", f.name, nargs, f.params.len()),
            );
        }
        let cost = self.frame_cost(fid);
        if cost > crate::interp::calls::MAX_FRAME {
            return self.trap(span, format!("stack overflow: `{}` needs a frame of {} bytes", f.name, cost));
        }
        let cost = if self.fiber_stack == u64::MAX { 0 } else { cost };
        if cost > self.fiber_stack && !self.fast() {
            return self.trap(span, "stack overflow on a fiber stack");
        }
        Ok(cost)
    }

    #[inline(always)]
    fn check_budget(&self, span: Span) -> R<()> {
        if let Some(b) = self.budget
            && self.steps > b
        {
            return self.trap(span, "compile-time evaluation exceeded its step budget");
        }
        Ok(())
    }

    fn vm_state(
        &mut self,
        code: Rc<Code<'a>>,
        fid: FnId,
        env: Option<Rc<Frame>>,
        cx: Ctx,
        installing: Option<(u64, LabelId)>,
        is_clause_lit: bool,
    ) -> St<'a> {
        let f = &self.prog.fns[fid.index()];
        let fr = self.new_frame(f.ret, Some(fid), env, f.locals.len(), installing, is_clause_lit);
        let mut regs = self.vm_regs.pop().unwrap_or_default();
        regs.clear();
        regs.resize(code.nregs as usize, 0);
        let mut vals = self.vm_vals.pop().unwrap_or_default();
        vals.resize(code.nvals as usize, Value::unit());
        St { code, regs, vals, fr, cx, cx_stack: Vec::new() }
    }

    /// Takes back the buffers and frame of a finished activation.
    fn vm_finish(&mut self, st: St<'a>) {
        let St { mut regs, mut vals, fr, .. } = st;
        vals.clear();
        regs.clear();
        self.recycle_frame(fr);
        if self.vm_regs.len() < 256 {
            self.vm_regs.push(regs);
            self.vm_vals.push(vals);
        }
    }

    /// Runs the body of an activation whose parameters are bound.
    fn vm_body(&mut self, st: &mut St<'a>, fid: FnId, span: Span, cost: u64) -> R<Exit> {
        self.fiber_stack = self.fiber_stack.saturating_sub(cost);
        self.depth += 1;
        let mark = self.temps.len();
        let r = if self.little() { self.vm_run::<true>(st, 0) } else { self.vm_run::<false>(st, 0) };
        self.depth -= 1;
        if self.fiber_stack != u64::MAX {
            self.fiber_stack += cost;
        }
        self.kill_temps(mark);
        self.kill_frame(&st.fr);
        match r {
            Ok(x) => Ok(x),
            Err(mut flow) => {
                if let Flow::Trap(t) = &mut *flow
                    && (self.mode == crate::interp::Mode::Debug || self.const_mode)
                {
                    t.trace.push((fid, span));
                }
                Err(flow)
            }
        }
    }

    /// Calls compiled function `fid` with argument values (from the tree-walker or the runtime).
    #[allow(clippy::too_many_arguments)]
    pub fn vm_call_values(
        &mut self,
        span: Span,
        fid: FnId,
        code: Rc<Code<'a>>,
        mut args: Vec<Value>,
        env: Option<Rc<Frame>>,
        cx: Ctx,
        installing: Option<(u64, LabelId)>,
        is_clause_lit: bool,
    ) -> R {
        let cost = self.vm_prologue(span, fid, args.len())?;
        let mut st = self.vm_state(code, fid, env, cx, installing, is_clause_lit);
        let mut bound = Ok(());
        let code = st.code.clone();
        for (p, v) in code.params.iter().zip(args.drain(..)) {
            bound = self.vm_bind(&mut st, *p, v, span);
            if bound.is_err() {
                break;
            }
        }
        if self.arg_pool.len() < 64 {
            self.arg_pool.push(args);
        }
        let r = match bound {
            Ok(()) => self.vm_body(&mut st, fid, span, cost),
            Err(f) => Err(f),
        };
        let out = match r {
            Ok(Exit::Ret(a)) => Ok(match a {
                Arg::Val(r) => std::mem::replace(&mut st.vals[r as usize], Value::unit()),
                a => st.value_of(a),
            }),
            Ok(Exit::Value(v)) => Ok(v),
            Ok(Exit::Seg) => self.trap(span, "internal error: a deferred statement's code ended a function"),
            Err(f) => Err(f),
        };
        self.vm_finish(st);
        out
    }

    fn vm_bind(&mut self, st: &mut St<'a>, p: ParamLoc, v: Value, span: Span) -> R<()> {
        match p {
            ParamLoc::Reg { reg, size } => write_value(st, Arg::Data { reg, size }, v, span),
            ParamLoc::Frame { local, ty } => self.bind_local(span, &st.fr, local, Some(ty), v),
            ParamLoc::Val { local } => self.bind_local(span, &st.fr, local, None, v),
        }
    }

    /// Calls compiled function `fid` from compiled code, with the operands `list` of the
    /// caller `cs`, and writes the result to `out`.
    fn vm_call_regs(
        &mut self,
        span: Span,
        fid: FnId,
        code: Rc<Code<'a>>,
        cs: &mut St<'a>,
        list: &[Arg],
        out: Arg,
    ) -> R<()> {
        let cost = self.vm_prologue(span, fid, list.len())?;
        let mut st = self.vm_state(code, fid, None, cs.cx.clone(), None, false);
        let code = st.code.clone();
        let mut bound = Ok(());
        for (p, a) in code.params.iter().zip(list) {
            match (*p, *a) {
                (ParamLoc::Reg { reg, size }, Arg::Data { reg: src, size: ss }) => {
                    if ss == size {
                        let w = words(size) as usize;
                        st.regs[reg as usize..reg as usize + w]
                            .copy_from_slice(&cs.regs[src as usize..src as usize + w]);
                    } else {
                        let b = get_bytes(&cs.regs, src, ss);
                        put_bytes(&mut st.regs, reg, size, &b);
                    }
                }
                (p, a) => {
                    bound = self.vm_bind(&mut st, p, cs.value_of(a), span);
                    if bound.is_err() {
                        break;
                    }
                }
            }
        }
        let r = match bound {
            Ok(()) => self.vm_body(&mut st, fid, span, cost),
            Err(f) => Err(f),
        };
        let res = match r {
            Ok(Exit::Ret(a)) => match (a, out) {
                (Arg::Data { reg: src, size: ss }, Arg::Data { reg, size }) => {
                    if ss == size {
                        let w = words(size) as usize;
                        cs.regs[reg as usize..reg as usize + w]
                            .copy_from_slice(&st.regs[src as usize..src as usize + w]);
                    } else {
                        let b = get_bytes(&st.regs, src, ss);
                        put_bytes(&mut cs.regs, reg, size, &b);
                    }
                    Ok(())
                }
                (Arg::Val(src), Arg::Val(d)) => {
                    cs.vals[d as usize] = std::mem::replace(&mut st.vals[src as usize], Value::unit());
                    Ok(())
                }
                (a, out) => {
                    let v = st.value_of(a);
                    write_value(cs, out, v, span)
                }
            },
            Ok(Exit::Value(v)) => write_value(cs, out, v, span),
            Ok(Exit::Seg) => self.trap(span, "internal error: a deferred statement's code ended a function"),
            Err(f) => Err(f),
        };
        self.vm_finish(st);
        res
    }

    // --- unwinding ----------------------------------------------------------------------------------

    /// Handles `flow`, raised by the instruction at `at`: runs the deferred statements it
    /// leaves and lands at a loop or `with` of this function it targets, or ends the
    /// activation if it is this function's `return`.
    #[inline(never)]
    fn vm_unwind(&mut self, st: &mut St<'a>, at: u32, mut flow: Box<Flow>) -> R<Land> {
        let code = st.code.clone();
        for s in code.scopes.iter() {
            if at < s.lo || at >= s.hi {
                continue;
            }
            match &s.kind {
                ScopeKind::Guard { span } => {
                    if !matches!(*flow, Flow::Trap(_)) {
                        flow = trap_flow(*span, DEFER_ESCAPE);
                    }
                }
                ScopeKind::Defer { is_err, seg, depth } => {
                    if matches!(*flow, Flow::Trap(_)) || (*is_err && !self.errdefers_run(&st.fr, &flow)) {
                        continue;
                    }
                    st.restore_ctx(*depth);
                    let mark = self.temps.len();
                    let r = if self.little() { self.vm_run::<true>(st, *seg) } else { self.vm_run::<false>(st, *seg) };
                    self.kill_temps(mark);
                    if let Err(f) = r {
                        flow = f;
                    }
                }
                ScopeKind::Label { label, brk, cont, depth, result, temps } => {
                    let id = st.fr.id;
                    match &*flow {
                        Flow::Break { frame, label: l, .. } if *frame == id && l == label => {
                            let Flow::Break { value, .. } = *flow else { unreachable!() };
                            if let Some(t) = temps {
                                self.kill_temps(st.regs[*t as usize] as usize);
                            }
                            st.restore_ctx(*depth);
                            write_value(st, *result, value, code.spans[at as usize])?;
                            return Ok(Land::Pc(*brk));
                        }
                        Flow::Continue { frame, label: l } if *frame == id && l == label && cont != brk => {
                            st.restore_ctx(*depth);
                            return Ok(Land::Pc(*cont));
                        }
                        _ => {}
                    }
                }
            }
        }
        if let Flow::Return { frame, .. } = &*flow
            && *frame == st.fr.id
        {
            let Flow::Return { value, .. } = *flow else { unreachable!() };
            return Ok(Land::Exit(Exit::Value(value)));
        }
        Err(flow)
    }

    // --- the dispatch loop ------------------------------------------------------------------------------

    /// Runs the code of activation `st` from instruction `pc`.
    fn vm_run<const LE: bool>(&mut self, st: &mut St<'a>, mut pc: u32) -> R<Exit> {
        let code = st.code.clone();
        let ops: &[Op] = &code.ops;
        macro_rules! tri {
            ($at:expr, $e:expr) => {
                match $e {
                    Ok(v) => v,
                    Err(f) => match self.vm_unwind(st, $at, f) {
                        Ok(Land::Pc(p)) => {
                            pc = p;
                            continue;
                        }
                        Ok(Land::Exit(x)) => return Ok(x),
                        Err(f) => return Err(f),
                    },
                }
            };
        }
        loop {
            let at = pc;
            let op = ops[pc as usize];
            pc += 1;
            self.steps += 1;
            let regs = &mut st.regs;
            match op {
                Op::Nop => {}
                Op::Mov { dst, src, words } => {
                    regs.copy_within(src as usize..(src + words) as usize, dst as usize);
                }
                Op::Mov1 { dst, src } => regs[dst as usize] = regs[src as usize],
                Op::Imm { dst, w } => regs[dst as usize] = w,
                Op::Zero { dst, words } => regs[dst as usize..(dst + words) as usize].fill(0),
                Op::Extract { dst, src, off, size } => extract(regs, dst, src, off, size),
                Op::Insert { dst, off, src, size } => insert(regs, dst, off, src, size),
                Op::Load { dst, addr, size, align } => {
                    let a = regs[addr as usize];
                    if size <= 8 && size > 0 {
                        let s = size as usize;
                        match self.mem.try_read_word(a, s, align as u64) {
                            Some(w) => regs[dst as usize] = w,
                            None => {
                                let w = tri!(
                                    at,
                                    self.mem.read_word(a, s, align as u64).map_err(|e| self.mem_trap(&code, at, e))
                                );
                                st.regs[dst as usize] = w;
                            }
                        }
                    } else if size > 0 {
                        let r = self.mem.read(a, size as u64, align as u64).map(|s| Bytes::from_slice(s));
                        let b = tri!(at, r.map_err(|e| self.mem_trap(&code, at, e)));
                        put_bytes(&mut st.regs, dst, size, &b);
                    }
                }
                Op::Store { addr, src, size, align } => {
                    let a = regs[addr as usize];
                    if size <= 8 && size > 0 {
                        let s = size as usize;
                        let w = regs[src as usize];
                        if !self.mem.try_write_word(a, w, s, align as u64) {
                            let r = self.mem.write_word(a, w, s, align as u64);
                            tri!(at, r.map_err(|e| self.mem_trap(&code, at, e)));
                        }
                    } else if size > 0 {
                        let b = get_bytes(regs, src, size);
                        let r = self.mem.write(a, &b, align as u64);
                        tri!(at, r.map_err(|e| self.mem_trap(&code, at, e)));
                    }
                }
                Op::AddImm { dst, src, imm } => regs[dst as usize] = regs[src as usize].wrapping_add(imm),
                Op::IndexAddr { dst, base, idx, ik, esz, len } => {
                    let i = index::<LE>(ik, regs[idx as usize]);
                    let b = regs[base as usize];
                    if len == u64::MAX {
                        regs[dst as usize] = b.wrapping_add(i.wrapping_mul(esz as u64));
                    } else if i < len {
                        regs[dst as usize] = b.wrapping_add(i * esz as u64);
                    } else {
                        tri!(at, Err(self.oob(&code, at, i, len)));
                    }
                }
                Op::SliceIndexAddr { dst, slice, idx, ik, esz } => {
                    let i = index::<LE>(ik, regs[idx as usize]);
                    let (p, n) = self.vm_slice_parts::<LE>(regs, slice);
                    if i < n {
                        regs[dst as usize] = p.wrapping_add(i * esz as u64);
                    } else {
                        tri!(at, Err(self.oob(&code, at, i, n)));
                    }
                }
                Op::BinI { op, dst, a, b, k, rk, ty } => {
                    let l = ival::<LE>(k, regs[a as usize]);
                    let r = ival::<LE>(rk, regs[b as usize]);
                    let si = k.si();
                    let v = match op {
                        BinOp::Add(_) if si.fits(l + r) => l + r,
                        BinOp::Sub(_) if si.fits(l - r) => l - r,
                        BinOp::Mul(_) if k.signed && si.fits(l * r) => l * r,
                        BinOp::BitAnd => l & r,
                        BinOp::BitOr => l | r,
                        BinOp::BitXor => l ^ r,
                        _ => tri!(at, self.small_binop(code.spans[at as usize], op, si, ty, l, r)),
                    };
                    st.regs[dst as usize] = iput::<LE>(k, v);
                }
                Op::CmpI { cmp, dst, a, b, k } => {
                    let l = ival::<LE>(k, regs[a as usize]);
                    let r = ival::<LE>(k, regs[b as usize]);
                    regs[dst as usize] = cmp.eval(l, r) as u64;
                }
                Op::BrI { cmp, a, b, k, t } => {
                    let l = ival::<LE>(k, regs[a as usize]);
                    let r = ival::<LE>(k, regs[b as usize]);
                    if cmp.eval(l, r) {
                        pc = t;
                    }
                }
                Op::Widen { dst, src, from, to } => {
                    regs[dst as usize] = iput::<LE>(to, ival::<LE>(from, regs[src as usize]));
                }
                Op::EqW { dst, a, b, neg } => {
                    regs[dst as usize] = ((regs[a as usize] == regs[b as usize]) != neg) as u64
                }
                Op::EqBool { dst, a, b, neg } => {
                    let x = regs[a as usize] & 0xff != 0;
                    let y = regs[b as usize] & 0xff != 0;
                    regs[dst as usize] = ((x == y) != neg) as u64;
                }
                Op::Jmp { t } => pc = t,
                Op::JmpIf { c, t } => {
                    if regs[c as usize] & 0xff != 0 {
                        pc = t;
                    }
                }
                Op::JmpIfNot { c, t } => {
                    if regs[c as usize] & 0xff == 0 {
                        pc = t;
                    }
                }
                Op::JmpNZ { c, t } => {
                    if regs[c as usize] != 0 {
                        pc = t;
                    }
                }
                Op::JmpZ { c, t } => {
                    if regs[c as usize] == 0 {
                        pc = t;
                    }
                }
                Op::JmpByte { c, off, t, when } => {
                    if (byte_at(regs, c, off) != 0) == when {
                        pc = t;
                    }
                }
                Op::JmpTag { src, test, t, when } => {
                    let tt = &code.tags[test as usize];
                    let mut w = regs[(src + tt.off / 8) as usize] >> (8 * (tt.off % 8));
                    if tt.off % 8 + tt.k.size as u32 > 8 {
                        w = 0;
                        for i in 0..tt.k.size as u32 {
                            w |= (byte_at(regs, src, tt.off + i) as u64) << (8 * i);
                        }
                    }
                    let v = ival::<LE>(tt.k, w & mask(tt.k.size as u32));
                    if v != tt.want && tt.valid.binary_search(&v).is_err() {
                        let msg = format!("invalid tag value {:?} for union {}", Int::S(v), tt.ty);
                        tri!(at, Err(trap_flow(code.spans[at as usize], msg)));
                    }
                    if (v == tt.want) == when {
                        pc = t;
                    }
                }
                Op::JmpFast { t } => {
                    if self.fast() {
                        pc = t;
                    }
                }
                Op::Tick => {
                    tri!(at, self.tick());
                    tri!(at, self.check_budget(code.spans[at as usize]));
                }
                Op::MarkTemps { dst } => regs[dst as usize] = self.temps.len() as u64,
                Op::KillTemps { src } => {
                    let m = regs[src as usize] as usize;
                    self.kill_temps(m);
                }
                Op::Call { f, args, out } => {
                    let span = code.spans[at as usize];
                    let list = &code.args[args as usize];
                    let r = match self.code_of(f) {
                        Some(c2) => self.vm_call_regs(span, f, c2, st, list, out),
                        None => {
                            let vals = st.values(list);
                            let cx = st.cx.clone();
                            self.call_function(span, f, vals, None, cx, None, false)
                                .and_then(|v| write_value(st, out, v, span))
                        }
                    };
                    tri!(at, r);
                }
                Op::Ret { src } => return Ok(Exit::Ret(src)),
                Op::EndSeg => return Ok(Exit::Seg),
                _ => tri!(at, self.vm_slow::<LE>(st, &code, op, at)),
            }
        }
    }

    #[cold]
    fn mem_trap(&self, code: &Code, at: u32, e: crate::interp::memory::MemError) -> Box<Flow> {
        trap_flow(code.spans[at as usize], e.message())
    }

    #[cold]
    fn oob(&self, code: &Code, at: u32, i: u64, len: u64) -> Box<Flow> {
        trap_flow(code.spans[at as usize], format!("index {} out of bounds for length {}", i, len))
    }

    /// The pointer and length of the slice value at `r`.
    #[inline(always)]
    fn vm_slice_parts<const LE: bool>(&self, regs: &[u64], r: Reg) -> (u64, u64) {
        if LE && self.target.ptr_bytes == 8 {
            (regs[r as usize], regs[r as usize + 1])
        } else {
            let ps = self.ptr_size() as u32;
            let b = get_bytes(regs, r, 2 * ps);
            self.slice_parts(&b)
        }
    }

    /// The instructions that are not worth a place in the dispatch loop.
    #[inline(never)]
    fn vm_slow<const LE: bool>(&mut self, st: &mut St<'a>, code: &Code<'a>, op: Op, at: u32) -> R<()> {
        let span = code.spans[at as usize];
        let ps = self.ptr_size() as u32;
        match op {
            Op::MovVal { dst, src } => st.vals[dst as usize] = st.vals[src as usize].clone(),
            Op::Bytes { dst, idx } => {
                let b = &code.bytes[idx as usize];
                put_bytes(&mut st.regs, dst, b.len() as u32, b);
            }
            Op::Fill { dst, src, esz, n } => {
                let e = get_bytes(&st.regs, src, esz);
                let mut v = Vec::with_capacity((esz * n) as usize);
                for _ in 0..n {
                    v.extend_from_slice(&e);
                }
                put_bytes(&mut st.regs, dst, esz * n, &v);
            }
            Op::LdLocal { dst, local, ty } => {
                let ti = self.ti(ty);
                let (size, align) = (ti.size, ti.align);
                let slot = match st.fr.locals.borrow().get(local.index()) {
                    Some(Slot::Mem(a)) => Ok(*a),
                    Some(Slot::Reg(r)) => {
                        put_bytes(&mut st.regs, dst, size as u32, &r[..size.min(r.len())]);
                        return Ok(());
                    }
                    Some(Slot::Val(_)) => Err(true),
                    _ => Err(false),
                };
                match slot {
                    Ok(a) => {
                        let b = self.load(span, a, size, align)?;
                        put_bytes(&mut st.regs, dst, size as u32, &b);
                    }
                    Err(true) => return self.local_not_data(span),
                    Err(false) => return self.local_uninit(span),
                }
            }
            Op::StLocal { local, src, ty } => {
                let size = self.ti(ty).size as u32;
                let b = get_bytes(&st.regs, src, size);
                self.bind_local(span, &st.fr, local, Some(ty), Value::Bytes(b))?;
            }
            Op::InitLocal { local, ty } => {
                let ti = self.ti(ty);
                let (size, align) = (ti.size, ti.align);
                if size <= REG_MAX {
                    self.set_local(span, &st.fr, local, &[], 1)?;
                } else {
                    self.local_addr(span, &st.fr, local, size, align)?;
                }
            }
            Op::AddrLocal { dst, local, ty } => {
                let fr = st.fr.clone();
                st.regs[dst as usize] = self.vm_local_addr(span, &fr, local, ty)?;
            }
            Op::LdUpvar { dst, levels, local, ty } => {
                let fr = self.vm_up(span, &st.fr, levels)?;
                let v = self.read_local(span, &fr, local, ty)?;
                write_value(st, Arg::Data { reg: dst, size: self.ti(ty).size as u32 }, v, span)?;
            }
            Op::AddrUpvar { dst, levels, local, ty } => {
                let fr = self.vm_up(span, &st.fr, levels)?;
                st.regs[dst as usize] = self.vm_local_addr(span, &fr, local, ty)?;
            }
            Op::LdLocalVal { dst, local } => {
                let v = match st.fr.locals.borrow().get(local.index()) {
                    Some(Slot::Val(v)) => Some((**v).clone()),
                    _ => None,
                };
                match v {
                    Some(v) => st.vals[dst as usize] = v,
                    None => return self.local_uninit(span),
                }
            }
            Op::StLocalVal { local, src } => {
                let v = st.vals[src as usize].clone();
                self.bind_local(span, &st.fr, local, None, v)?;
            }
            Op::LdUpvarVal { dst, levels, local } => {
                let fr = self.vm_up(span, &st.fr, levels)?;
                let v = match fr.locals.borrow().get(local.index()) {
                    Some(Slot::Val(v)) => Some((**v).clone()),
                    _ => None,
                };
                match v {
                    Some(v) => st.vals[dst as usize] = v,
                    None => return self.local_uninit(span),
                }
            }
            Op::LoadBits { dst, addr, bits } => {
                let p = Place::Bits { addr: st.regs[addr as usize], whole: bits.whole, off: bits.off, fty: bits.fty };
                let b = self.read_place(span, p, bits.fty)?;
                put_bytes(&mut st.regs, dst, self.ti(bits.fty).size as u32, &b);
            }
            Op::StoreBits { addr, src, bits } => {
                let p = Place::Bits { addr: st.regs[addr as usize], whole: bits.whole, off: bits.off, fty: bits.fty };
                let b = get_bytes(&st.regs, src, self.ti(bits.fty).size as u32);
                self.write_place(span, p, bits.fty, &b)?;
            }
            Op::PtrToAddr { dst, src } => {
                let b = get_bytes(&st.regs, src, ps);
                st.regs[dst as usize] = self.ptr_of(&b);
            }
            Op::AddrToPtr { dst, src } => {
                let b = self.ptr_bytes(st.regs[src as usize]);
                put_bytes(&mut st.regs, dst, ps, &b);
            }
            Op::IndexVal { dst, src, idx, ik, esz, len } => {
                let i = index::<LE>(ik, st.regs[idx as usize]);
                if i >= len {
                    return self.trap(span, format!("index {} out of bounds for length {}", i, len));
                }
                extract(&mut st.regs, dst, src, i as u32 * esz, esz);
            }
            Op::MakeSlice { dst, info } => {
                let si = &code.slices[info as usize];
                let ptr = st.regs[si.ptr as usize];
                let len = match si.len {
                    SliceLen::Static(n) => Some(n),
                    SliceLen::OfSlice(r) => Some(self.vm_slice_parts::<LE>(&st.regs, r).1),
                    SliceLen::Unknown => None,
                };
                let lo = si.lo.map_or(0, |(r, k)| index::<LE>(k, st.regs[r as usize]));
                let mut hi = match si.hi {
                    Some((r, k)) => index::<LE>(k, st.regs[r as usize]),
                    None => len.unwrap_or(0),
                };
                if si.inclusive {
                    hi = hi.saturating_add(1);
                }
                if lo > hi || len.is_some_and(|n| hi > n) {
                    let n = len.map_or("?".to_string(), |n| n.to_string());
                    return self.trap(span, format!("slice bounds {}..{} invalid for length {}", lo, hi, n));
                }
                let b = self.make_slice(ptr.wrapping_add(lo * si.esz), hi - lo);
                put_bytes(&mut st.regs, dst, 2 * ps, &b);
            }
            Op::AllocTemp { dst, src, size, align } => {
                let b = get_bytes(&st.regs, src, size);
                let a = self.alloc_temp(span, &b, align as u64)?;
                let p = self.ptr_bytes(a);
                put_bytes(&mut st.regs, dst, ps, &p);
            }
            Op::Str { dst, idx } => {
                let s = &code.strs[idx as usize];
                let mut a = s.addr.get();
                if a == 0 {
                    a = self.string_literal(span, &s.bytes, s.nul)?;
                    s.addr.set(a);
                }
                if s.nul {
                    let p = self.ptr_bytes(a);
                    put_bytes(&mut st.regs, dst, ps, &p);
                } else {
                    let b = self.make_slice(a, s.bytes.len() as u64);
                    put_bytes(&mut st.regs, dst, 2 * ps, &b);
                }
            }
            Op::ConstLoad { dst, id, ty } => {
                let a = self.const_addr(span, id)?;
                let b = self.load_ty(span, a, ty)?;
                put_bytes(&mut st.regs, dst, b.len() as u32, &b);
            }
            Op::ConstAddr { dst, id } => st.regs[dst as usize] = self.const_addr(span, id)?,
            Op::GlobalAddr { dst, g } => st.regs[dst as usize] = self.global_addr(span, g)?,
            Op::NegI { dst, a, k } => {
                let x = ival::<LE>(k, st.regs[a as usize]);
                let si = k.si();
                let v = -x;
                let v = if si.fits(v) {
                    v
                } else if self.fast() {
                    si.value(v as u64)
                } else {
                    return self.trap(span, "integer overflow in `-`");
                };
                st.regs[dst as usize] = iput::<LE>(k, v);
            }
            Op::NotI { dst, a, k } => {
                let x = ival::<LE>(k, st.regs[a as usize]);
                st.regs[dst as usize] = iput::<LE>(k, k.si().value(!x as u64));
            }
            Op::ToIndex { dst, src, ty } => {
                let b = get_bytes(&st.regs, src, self.ti(ty).size as u32);
                let v = self.int_of(ty, &b).to_u64_sat();
                st.regs[dst as usize] = iput::<LE>(IntK::U64, v as i128);
            }
            Op::BinG { dst, a, b, info } => {
                let bi = code.bins[info as usize];
                let l = get_bytes(&st.regs, a, bi.lsize);
                let r = get_bytes(&st.regs, b, bi.rsize);
                let v = self.binop(span, bi.op, bi.lty, bi.rty, bi.res, &l, &r)?;
                put_bytes(&mut st.regs, dst, self.ti(bi.res).size as u32, &v);
            }
            Op::UnG { dst, a, op, ty } => {
                let x = get_bytes(&st.regs, a, self.ti(ty).size as u32);
                let v = self.unop(span, op, ty, &x)?;
                put_bytes(&mut st.regs, dst, v.len() as u32, &v);
            }
            Op::EqG { dst, a, b, ty, neg } => {
                let size = self.ti(ty).size as u32;
                let l = get_bytes(&st.regs, a, size);
                let r = get_bytes(&st.regs, b, size);
                let v = self.tr(span, self.eq(ty, &l, &r))?;
                st.regs[dst as usize] = (v != neg) as u64;
            }
            Op::InG { dst, elem, cont, ety, cty, neg } => {
                let x = get_bytes(&st.regs, elem, self.ti(ety).size as u32);
                let c = get_bytes(&st.regs, cont, self.ti(cty).size as u32);
                let v = self.tr(span, self.contains(ety, &x, cty, &c))?;
                st.regs[dst as usize] = (v != neg) as u64;
            }
            Op::CoerceG { dst, src, kind, from, to } => {
                let b = get_bytes(&st.regs, src, self.ti(from).size as u32);
                let v = self.coerce_bytes(span, kind, from, to, b)?;
                put_bytes(&mut st.regs, dst, self.ti(to).size as u32, &v);
            }
            Op::FnToPtr { dst, src, to } => {
                let v = st.value_of(src);
                let v = self.fn_to_ptr(&st.cx, span, to, v)?;
                write_value(st, dst, v, span)?;
            }
            Op::BitsField { dst, src, ty, idx } => {
                let b = get_bytes(&st.regs, src, self.ti(ty).size as u32);
                let (fty, v) = self.tr(span, self.field_bytes(ty, &b, idx as usize))?;
                put_bytes(&mut st.regs, dst, self.ti(fty).size as u32, &v);
            }
            Op::BitsAggregate { dst, ty, args } => {
                let vals = st.bytes(&code.args[args as usize]);
                let u = self.bits_struct_ty(ty).unwrap_or(ty);
                let mut acc = Int::S(0);
                for (i, v) in vals.iter().enumerate() {
                    let (off, fty) = self.tr(span, self.bits_field(ty, i))?;
                    acc = acc.or(&self.bits_raw(fty, v).shl(off as u64));
                }
                let b = self.int_bytes(u, &acc);
                put_bytes(&mut st.regs, dst, b.len() as u32, &b);
            }
            Op::UnwrapOpt { dst, src, ty } => {
                let inner = self.opt_inner(ty);
                let isz = self.ti(inner).size as u32;
                let nullable = crate::layout::is_nullable_pointer(inner);
                let ti = self.ti(ty);
                let present = if nullable {
                    st.regs[src as usize] != 0
                } else {
                    byte_at(&st.regs, src, ti.offsets[1] as u32) != 0
                };
                let off = if nullable { 0 } else { ti.offsets[0] as u32 };
                if !present {
                    if self.fast() {
                        st.regs[dst as usize..(dst + words(isz)) as usize].fill(0);
                        return Ok(());
                    }
                    return self.trap(span, "unwrap of null");
                }
                extract(&mut st.regs, dst, src, off, isz);
            }
            Op::UnwrapRes { dst, src, ty } => {
                let (err, ok) = self.res_types(ty);
                let ti = self.ti(ty);
                let (flag, off) = (ti.offsets[0] as u32, ti.offsets[1] as u32);
                let osz = self.ti(ok).size as u32;
                if byte_at(&st.regs, src, flag) != 0 {
                    if self.fast() {
                        st.regs[dst as usize..(dst + words(osz)) as usize].fill(0);
                        return Ok(());
                    }
                    let b = get_bytes(&st.regs, src, self.ti(ty).size as u32);
                    let payload = self.res_payload(ty, &b);
                    let mut s = Vec::new();
                    let _ = self.format_value(&mut s, err, &payload, false);
                    return self.trap(span, format!("unwrap of error value {}", String::from_utf8_lossy(&s)));
                }
                extract(&mut st.regs, dst, src, off, osz);
            }
            Op::PatRange { dst, src, ty, info } => {
                let b = get_bytes(&st.regs, src, self.ti(ty).size as u32);
                let v = self.int_of(ty, &b);
                let (lo, hi, inclusive) = &code.ranges[info as usize];
                let lo = Int::from_big(lo.clone());
                let hi = Int::from_big(hi.clone());
                let ok_hi = if *inclusive { v.cmp(&hi).is_le() } else { v.cmp(&hi).is_lt() };
                st.regs[dst as usize] = (v.cmp(&lo).is_ge() && ok_hi) as u64;
            }
            Op::Get { dst, info } => {
                let g = code.gets[info as usize];
                let ptr = st.regs[g.addr as usize];
                let len = match g.len {
                    SliceLen::Static(n) => n,
                    SliceLen::OfSlice(r) => self.vm_slice_parts::<LE>(&st.regs, r).1,
                    SliceLen::Unknown => 0,
                };
                let i = match (g.op, g.idx) {
                    (Builtin::First, _) => 0,
                    (Builtin::Last, _) => len.wrapping_sub(1),
                    (_, Some((r, k))) => index::<LE>(k, st.regs[r as usize]),
                    _ => 0,
                };
                let rsize = self.ti(g.res).size as u32;
                if i >= len {
                    st.regs[dst as usize..(dst + words(rsize)) as usize].fill(0);
                    return Ok(());
                }
                let p = ptr.wrapping_add(i.wrapping_mul(g.esz));
                let v = if g.op == Builtin::GetPtr {
                    let pb = self.ptr_bytes(p);
                    self.make_some(g.res, &pb)
                } else {
                    let e = self.load(span, p, g.esz as usize, g.align)?;
                    self.make_some(g.res, &e)
                };
                put_bytes(&mut st.regs, dst, rsize, &v);
            }
            Op::TrapConst { msg } => return self.trap(span, code.msgs[msg as usize].clone()),
            Op::TrapMsg { src, kind } => {
                let b = get_bytes(&st.regs, src, 2 * ps);
                let s = String::from_utf8_lossy(&self.read_str(span, &b)?).into_owned();
                let msg = match kind {
                    TrapKind::Plain => s,
                    TrapKind::Assert if s == "assertion failed" => s,
                    TrapKind::Assert => format!("assertion failed: {}", s),
                    TrapKind::Expect if s == "expectation failed" => s,
                    TrapKind::Expect => format!("expectation failed: {}", s),
                };
                return self.trap(span, msg);
            }
            Op::CallInd { callee, cty, args, out } => {
                let cv = st.value_of(callee);
                let vals = st.values(&code.args[args as usize]);
                let cx = st.cx.clone();
                let v = self.call_value(&cx, span, cv, cty, vals)?;
                write_value(st, out, v, span)?;
            }
            Op::Perform { eff, op, args, out } => {
                let vals = st.values(&code.args[args as usize]);
                let cx = st.cx.clone();
                let v = self.perform(&cx, span, &code.effects[eff as usize], op, vals)?;
                write_value(st, out, v, span)?;
            }
            Op::Builtin { op, args, res, out } => {
                let vals = st.bytes(&code.args[args as usize]);
                let v = self.builtin_vals(span, op, res, &code.arg_tys[args as usize], &vals)?;
                write_value(st, out, v, span)?;
            }
            Op::Write { info, sink, args } => {
                let e = code.writes[info as usize];
                let ExprKind::Write { sink: ws, pieces, specs, args: aexprs } = &e.kind else {
                    return self.trap(span, "internal error: bad write instruction");
                };
                let sv = match ws {
                    WriteSink::Fn(x) => Some((st.value_of(sink), x.ty)),
                    WriteSink::Perform { .. } => None,
                };
                let vals = st.bytes(&code.args[args as usize]);
                let cx = st.cx.clone();
                self.write_pieces(&cx, span, ws, sv.as_ref(), pieces, specs, aexprs, &vals)?;
            }
            Op::EvalTree { expr, out } => {
                let e = code.trees[expr as usize];
                let fr = st.fr.clone();
                let cx = st.cx.clone();
                let v = self.eval(Act { fr: &fr, cx: &cx }, e)?;
                write_value(st, out, v, span)?;
            }
            Op::TreeStmt { stmt } => {
                let fr = st.fr.clone();
                let cx = st.cx.clone();
                self.exec_stmt(Act { fr: &fr, cx: &cx }, code.stmts[stmt as usize])?;
            }
            Op::ThrowBreak { levels, label, v } => {
                let target = self.vm_up(span, &st.fr, levels)?;
                let value = st.value_of(v);
                return Err(Box::new(Flow::Break { frame: target.id, label, value }));
            }
            Op::ThrowBreakInst { v } => {
                let Some((frame, label)) = st.fr.installing else {
                    return self.trap(span, "internal error: no installing `with` for this break");
                };
                let value = st.value_of(v);
                return Err(Box::new(Flow::Break { frame, label, value }));
            }
            Op::ThrowContinue { levels, label } => {
                let target = self.vm_up(span, &st.fr, levels)?;
                return Err(Box::new(Flow::Continue { frame: target.id, label }));
            }
            Op::ThrowReturn { levels, v } => {
                let Some(target) = self.up_frame(&st.fr, levels) else {
                    return self.trap(span, "internal error: return from a missing frame");
                };
                let value = st.value_of(v);
                let is_err = self.is_err_return(&target, &value);
                return Err(Box::new(Flow::Return { frame: target.id, value, is_err }));
            }
            Op::PushCtx => st.cx_stack.push(st.cx.clone()),
            Op::PopCtx => {
                if let Some(c) = st.cx_stack.pop() {
                    st.cx = c;
                }
            }
            Op::RestoreCtx { depth } => st.restore_ctx(depth),
            Op::InstallLit { v, label } => {
                let Value::Handler(hv) = &st.vals[v as usize] else {
                    return self.trap(span, "internal error: `with` of a non-handler value");
                };
                let hv = hv.clone();
                let eff = self.prog.handlers[hv.id.index()].effect.clone();
                st.cx = st.cx.push(eff, Installed::Lit(hv), st.fr.id, label);
            }
            Op::InstallDecl { info, src, size, label } => {
                let b = get_bytes(&st.regs, src, size);
                let outer = st.cx.clone();
                for d in code.decls[info as usize].iter() {
                    let eff = self.prog.handler_decls[d.index()].effect.clone();
                    let inst = Installed::Decl { decl: *d, value: b.clone(), outer: outer.clone() };
                    st.cx = st.cx.push(eff, inst, st.fr.id, label);
                }
            }
            Op::MkClosure { dst, f, env } => {
                let env = if env { Some(st.fr.clone()) } else { None };
                st.vals[dst as usize] = Value::Closure(Closure { func: f, env, ctx: st.cx.clone() });
            }
            Op::MkHandler { dst, h } => {
                let hv = HandlerVal { id: h, env: Some(st.fr.clone()), ctx: st.cx.clone() };
                st.vals[dst as usize] = Value::Handler(Rc::new(hv));
            }
            _ => return self.trap(span, format!("internal error: instruction {:?} out of place", op)),
        }
        Ok(())
    }

    fn vm_up(&self, span: Span, fr: &Rc<Frame>, levels: u32) -> R<Rc<Frame>> {
        match self.up_frame(fr, levels) {
            Some(f) => Ok(f),
            None => self.trap(span, "internal error: upvar without an enclosing frame"),
        }
    }

    /// The address of a frame local's storage (allocating it on first use).
    fn vm_local_addr(&mut self, span: Span, fr: &Rc<Frame>, local: LocalId, ty: Ty) -> R<u64> {
        let slot = match fr.locals.borrow().get(local.index()) {
            Some(Slot::Mem(a)) => Some(Ok(*a)),
            Some(Slot::Val(_)) => Some(Err(())),
            _ => None,
        };
        match slot {
            Some(Ok(a)) => Ok(a),
            Some(Err(())) => self.trap(span, "internal error: second-class value used as a place"),
            None => {
                let ti = self.ti(ty);
                let (size, align) = (ti.size, ti.align);
                self.local_addr(span, fr, local, size, align)
            }
        }
    }
}
