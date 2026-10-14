//! Running compiled code: activations, the dispatch loop and unwinding.

use super::*;
use crate::interp::num::Int;

const DEFER_ESCAPE: &str = "internal error: control flow escaped a deferred statement";

/// One activation of a compiled function.
pub struct St<'a> {
    code: Rc<Code<'a>>,
    regs: Vec<u64>,
    vals: Vec<Value>,
    /// The frame of the activation, made when something first needs it (most functions
    /// keep everything in registers and never do).
    fr: Option<Rc<Frame>>,
    /// The id the frame has (or will have).
    id: u64,
    func: FnId,
    /// What the frame is made with, for the activations that are not plain calls.
    extra: Option<FrameExtra>,
    /// The handler context, and the contexts outside the `with`s being run.
    cx: Ctx,
    cx_stack: Vec<Ctx>,
}

/// What the frame of a closure or clause call is made with besides the function.
struct FrameExtra {
    env: Option<Rc<Frame>>,
    installing: Option<(u64, LabelId)>,
    is_clause_lit: bool,
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

/// Copies `n` words; most values are one or two.
#[inline(always)]
fn copy_words(dst: &mut [u64], d: usize, src: &[u64], s: usize, n: usize) {
    let dst = &mut dst[d..d + n];
    let src = &src[s..s + n];
    match n {
        1 => dst[0] = src[0],
        2 => {
            dst[0] = src[0];
            dst[1] = src[1];
        }
        _ => dst.copy_from_slice(src),
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
    if off.is_multiple_of(8) {
        let s = (src + off / 8) as usize;
        let w = words(size) as usize;
        regs.copy_within(s..s + w, dst as usize);
        if !size.is_multiple_of(8) {
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
    if off.is_multiple_of(8) {
        let d = (dst + off / 8) as usize;
        let full = (size / 8) as usize;
        regs.copy_within(src as usize..src as usize + full, d);
        if !size.is_multiple_of(8) {
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

/// The value of a small integer of kind `k` whose bytes are the word `w`, as a 64-bit
/// pattern: sign-extended if the type is signed, zero-extended if not.
#[inline(always)]
fn nval<const LE: bool>(k: IntK, w: u64) -> u64 {
    let w = if LE { w } else { w.swap_bytes() >> (64 - 8 * k.size as u32) };
    let sh = 64 - k.bits as u32;
    if k.signed { (((w << sh) as i64) >> sh) as u64 } else { (w << sh) >> sh }
}

/// The word holding the integer whose 64-bit pattern is `v` (which fits the type).
#[inline(always)]
fn nput<const LE: bool>(k: IntK, v: u64) -> u64 {
    let sh = 64 - 8 * k.size as u32;
    if LE { v & (u64::MAX >> sh) } else { (v << sh).swap_bytes() }
}

/// Whether the pattern `v` is a value of the type: unsigned values have no bits above
/// the type's, signed ones are the sign extension of their low bits.
#[inline(always)]
fn nfits(k: IntK, v: u64) -> bool {
    let sh = 64 - k.bits as u32;
    if k.signed { (((v << sh) as i64) >> sh) as u64 == v } else { (v << sh) >> sh == v }
}

#[inline(always)]
fn n_add(k: IntK, l: u64, r: u64) -> Option<u64> {
    let v = if k.signed { (l as i64).checked_add(r as i64)? as u64 } else { l.checked_add(r)? };
    nfits(k, v).then_some(v)
}

#[inline(always)]
fn n_sub(k: IntK, l: u64, r: u64) -> Option<u64> {
    let v = if k.signed { (l as i64).checked_sub(r as i64)? as u64 } else { l.checked_sub(r)? };
    nfits(k, v).then_some(v)
}

#[inline(always)]
fn n_mul(k: IntK, l: u64, r: u64) -> Option<u64> {
    let v = if k.signed { (l as i64).checked_mul(r as i64)? as u64 } else { l.checked_mul(r)? };
    nfits(k, v).then_some(v)
}

/// A comparison of two small integers of kind `k` given as words.
#[inline(always)]
fn n_cmp<const LE: bool>(cmp: Cmp, k: IntK, x: u64, y: u64) -> bool {
    // Flipping the top bit of signed patterns makes unsigned order the order of values.
    let bias = (k.signed as u64) << 63;
    let (x, y) = (nval::<LE>(k, x) ^ bias, nval::<LE>(k, y) ^ bias);
    match cmp {
        Cmp::Lt => x < y,
        Cmp::Le => x <= y,
        Cmp::Gt => x > y,
        Cmp::Ge => x >= y,
        Cmp::Eq => x == y,
        Cmp::Ne => x != y,
    }
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
    #[inline(always)]
    fn vm_prologue(&mut self, span: Span, fid: FnId, code: &Code<'a>, nargs: usize) -> R<u64> {
        let marker = 0u8;
        if self.depth >= MAX_DEPTH || (&marker as *const u8 as usize) < self.stack_floor {
            return self.trap(span, "stack overflow");
        }
        self.tick()?;
        self.check_budget(span)?;
        if nargs != code.params.len() {
            return self.bad_arg_count(span, fid, nargs);
        }
        let cost = self.frame_cost(fid);
        if cost > crate::interp::calls::MAX_FRAME {
            return self.trap(
                span,
                format!("stack overflow: `{}` needs a frame of {} bytes", self.prog.fns[fid.index()].name, cost),
            );
        }
        if self.fiber_stack == u64::MAX {
            return Ok(0);
        }
        if cost > self.fiber_stack && !self.fast() {
            return self.trap(span, "stack overflow on a fiber stack");
        }
        Ok(cost)
    }

    #[cold]
    #[inline(never)]
    fn bad_arg_count(&self, span: Span, fid: FnId, nargs: usize) -> R<u64> {
        let f = &self.prog.fns[fid.index()];
        self.trap(
            span,
            format!("internal error: `{}` called with {} arguments, expected {}", f.name, nargs, f.params.len()),
        )
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

    #[inline(always)]
    fn vm_state(&mut self, code: Rc<Code<'a>>, fid: FnId, extra: Option<FrameExtra>, cx: Ctx) -> St<'a> {
        let id = self.next_frame;
        self.next_frame += 1;
        let mut regs = self.vm_regs.pop().unwrap_or_default();
        let n = code.nregs as usize;
        regs.clear();
        regs.reserve(n);
        let init = &code.init[..code.init.len().min(n)];
        // SAFETY: the buffer has room for `n` words, and each of them is written below.
        unsafe {
            let p = regs.as_mut_ptr();
            for (i, w) in init.iter().enumerate() {
                p.add(i).write(*w);
            }
            for i in init.len()..n {
                p.add(i).write(0);
            }
            regs.set_len(n);
        }
        let vals = if code.nvals == 0 {
            Vec::new()
        } else {
            let mut vals = self.vm_vals.pop().unwrap_or_default();
            vals.resize(code.nvals as usize, Value::unit());
            vals
        };
        St { code, regs, vals, fr: None, id, func: fid, extra, cx, cx_stack: Vec::new() }
    }

    /// The frame of activation `st`, made on first use.
    fn vm_frame(&mut self, st: &mut St<'a>) -> Rc<Frame> {
        if let Some(fr) = &st.fr {
            return fr.clone();
        }
        let ret_ty = self.prog.fns[st.func.index()].ret;
        let (env, installing, is_clause_lit) = match st.extra.take() {
            Some(x) => (x.env, x.installing, x.is_clause_lit),
            None => (None, None, false),
        };
        let fr = self.new_frame_with_id(
            st.id,
            ret_ty,
            Some(st.func),
            env,
            st.code.nslots as usize,
            installing,
            is_clause_lit,
        );
        st.fr = Some(fr.clone());
        fr
    }

    /// Takes back the buffers and frame of a finished activation.
    #[inline(always)]
    fn vm_finish(&mut self, st: St<'a>) {
        let St { mut regs, mut vals, fr, .. } = st;
        regs.clear();
        if let Some(fr) = fr {
            self.recycle_frame(fr);
        }
        if self.vm_regs.len() < 256 {
            self.vm_regs.push(regs);
            if vals.capacity() > 0 {
                vals.clear();
                self.vm_vals.push(vals);
            }
        }
    }

    /// Runs the body of an activation whose parameters are bound.
    #[inline(always)]
    fn vm_body(&mut self, st: &mut St<'a>, fid: FnId, span: Span, cost: u64) -> R<Exit> {
        if cost != 0 {
            self.fiber_stack = self.fiber_stack.saturating_sub(cost);
        }
        self.depth += 1;
        let mark = self.temps.len();
        let r = self.vm_run_from(st, 0);
        self.depth -= 1;
        if self.fiber_stack != u64::MAX {
            self.fiber_stack += cost;
        }
        self.kill_temps(mark);
        if let Some(fr) = &st.fr {
            self.kill_frame(fr);
        }
        match r {
            Ok(x) => Ok(x),
            Err(flow) => Err(self.vm_trace(flow, fid, span)),
        }
    }

    #[cold]
    #[inline(never)]
    fn vm_trace(&mut self, mut flow: Box<Flow>, fid: FnId, span: Span) -> Box<Flow> {
        if let Flow::Trap(t) = &mut *flow
            && (self.mode == crate::interp::Mode::Debug || self.const_mode)
        {
            t.trace.push((fid, span));
        }
        flow
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
        let cost = self.vm_prologue(span, fid, &code, args.len())?;
        let extra = if env.is_some() || installing.is_some() || is_clause_lit {
            Some(FrameExtra { env, installing, is_clause_lit })
        } else {
            None
        };
        let mut st = self.vm_state(code, fid, extra, cx);
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
            ParamLoc::Frame { local, ty } => {
                let fr = self.vm_frame(st);
                self.bind_local(span, &fr, local, Some(ty), v)
            }
            ParamLoc::Val { local } => {
                let fr = self.vm_frame(st);
                self.bind_local(span, &fr, local, None, v)
            }
        }
    }

    /// Calls compiled function `fid` from compiled code, with the operands `list` of the
    /// caller `cs`, and writes the result to `out`.
    #[inline(always)]
    fn vm_call_regs(
        &mut self,
        span: Span,
        fid: FnId,
        code: Rc<Code<'a>>,
        cs: &mut St<'a>,
        list: &[Arg],
        out: Arg,
    ) -> R<()> {
        let cx = cs.cx.clone();
        self.vm_call_list(span, fid, code, cs, list, out, None, cx, None)
    }

    /// Like [`Self::vm_call_regs`] for an activation that has a handler context of its own
    /// and, perhaps, a frame description and a first argument that is not in a register of
    /// the caller (the value of a handler declaration).
    #[allow(clippy::too_many_arguments)]
    fn vm_call_list(
        &mut self,
        span: Span,
        fid: FnId,
        code: Rc<Code<'a>>,
        cs: &mut St<'a>,
        list: &[Arg],
        out: Arg,
        extra: Option<FrameExtra>,
        cx: Ctx,
        first: Option<&Bytes>,
    ) -> R<()> {
        let cost = self.vm_prologue(span, fid, &code, list.len() + first.is_some() as usize)?;
        let mut st = self.vm_state(code, fid, extra, cx);
        // SAFETY: `st.code` keeps the code alive for the whole activation and is never replaced.
        let code: &Code<'a> = unsafe { &*Rc::as_ptr(&st.code) };
        let mut bound = Ok(());
        let mut params = code.params.iter();
        if let Some(b) = first
            && let Some(p) = params.next()
        {
            bound = self.vm_bind(&mut st, *p, Value::Bytes(b.clone()), span);
        }
        for (p, a) in params.zip(list) {
            match (*p, *a) {
                (ParamLoc::Reg { reg, size }, Arg::Data { reg: src, size: ss }) => {
                    if ss == size {
                        copy_words(&mut st.regs, reg as usize, &cs.regs, src as usize, words(size) as usize);
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
                        copy_words(&mut cs.regs, reg as usize, &st.regs, src as usize, words(size) as usize);
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
                    if matches!(*flow, Flow::Trap(_)) {
                        continue;
                    }
                    if *is_err {
                        let fr = self.vm_frame(st);
                        if !self.errdefers_run(&fr, &flow) {
                            continue;
                        }
                    }
                    st.restore_ctx(*depth);
                    let mark = self.temps.len();
                    let r = self.vm_run_from(st, *seg);
                    self.kill_temps(mark);
                    if let Err(f) = r {
                        flow = f;
                    }
                }
                ScopeKind::Label { label, brk, cont, depth, result, temps } => {
                    let id = st.id;
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
            && *frame == st.id
        {
            let Flow::Return { value, .. } = *flow else { unreachable!() };
            return Ok(Land::Exit(Exit::Value(value)));
        }
        Err(flow)
    }

    // --- the dispatch loop ------------------------------------------------------------------------------

    /// Runs activation `st` from instruction `pc`, with the loop made for the target's byte
    /// order and for whether steps are counted (only with a step budget).
    fn vm_run_from(&mut self, st: &mut St<'a>, pc: u32) -> R<Exit> {
        match (self.little(), self.budget.is_some()) {
            (true, false) => self.vm_run::<true, false>(st, pc),
            (true, true) => self.vm_run::<true, true>(st, pc),
            (false, false) => self.vm_run::<false, false>(st, pc),
            (false, true) => self.vm_run::<false, true>(st, pc),
        }
    }

    /// Runs the code of activation `st` from instruction `start`.
    fn vm_run<const LE: bool, const COUNT: bool>(&mut self, st: &mut St<'a>, start: u32) -> R<Exit> {
        // SAFETY: `st.code` keeps the code alive for the whole activation and is never replaced.
        let code: &Code<'a> = unsafe { &*Rc::as_ptr(&st.code) };
        // The instruction pointer and the registers are raw so that dispatch costs little.
        // `Code::valid` has checked every jump target and, for the instructions that use
        // `r!` and `w!`, every register against the activation's `nregs` registers, which
        // `vm_state` allocates and nothing resizes while the code runs.
        let base: *const Op = code.ops.as_ptr();
        // SAFETY: `start` is the entry or a segment of the code, both checked.
        let mut ip: *const Op = unsafe { base.add(start as usize) };
        debug_assert_eq!(st.regs.len(), code.nregs as usize);
        let rp: *mut u64 = st.regs.as_mut_ptr();
        macro_rules! jump {
            ($t:expr) => {
                // SAFETY: targets are checked by `Code::valid`.
                {
                    let t = $t as usize;
                    ip = unsafe { base.add(t) }
                }
            };
        }
        // The index of the running instruction.
        macro_rules! at {
            () => {
                // SAFETY: `ip` is inside the code, one past the running instruction.
                (unsafe { ip.offset_from(base) } as u32 - 1)
            };
        }
        macro_rules! r {
            ($i:expr) => {
                // SAFETY: checked by `Code::valid` for the instructions that use it.
                unsafe { *rp.add($i as usize) }
            };
        }
        macro_rules! w {
            ($i:expr, $v:expr) => {
                // SAFETY: as for `r!`.
                {
                    let v = $v;
                    unsafe { *rp.add($i as usize) = v }
                }
            };
        }
        macro_rules! tri {
            ($at:expr, $e:expr) => {
                match $e {
                    Ok(v) => v,
                    Err(f) => match self.vm_unwind(st, $at, f) {
                        Ok(Land::Pc(p)) => {
                            jump!(p);
                            continue;
                        }
                        Ok(Land::Exit(x)) => return Ok(x),
                        Err(f) => return Err(f),
                    },
                }
            };
        }
        loop {
            // SAFETY: the code ends with an instruction that does not continue.
            let op = unsafe { &*ip };
            ip = unsafe { ip.add(1) };
            if COUNT {
                self.steps += 1;
            }
            #[cfg(feature = "vm-profile")]
            profile::count(op);
            let regs = &mut st.regs;
            match *op {
                Op::Nop => {}
                Op::Mov { dst, src, words } => {
                    regs.copy_within(src as usize..(src + words) as usize, dst as usize);
                }
                Op::Mov1 { dst, src } => w!(dst, r!(src)),
                Op::Imm { dst, w } => w!(dst, w),
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
                                    at!(),
                                    self.mem.read_word(a, s, align as u64).map_err(|e| self.mem_trap(code, at!(), e))
                                );
                                st.regs[dst as usize] = w;
                            }
                        }
                    } else if size > 0 {
                        let r = self.mem.read(a, size as u64, align as u64).map(Bytes::from_slice);
                        let b = tri!(at!(), r.map_err(|e| self.mem_trap(code, at!(), e)));
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
                            tri!(at!(), r.map_err(|e| self.mem_trap(code, at!(), e)));
                        }
                    } else if size > 0 {
                        let b = get_bytes(regs, src, size);
                        let r = self.mem.write(a, &b, align as u64);
                        tri!(at!(), r.map_err(|e| self.mem_trap(code, at!(), e)));
                    }
                }
                Op::AddImm { dst, src, imm } => w!(dst, r!(src).wrapping_add(imm)),
                Op::IndexAddr { dst, base, idx, ik, esz, len } => {
                    let i = index::<LE>(ik, regs[idx as usize]);
                    let b = regs[base as usize];
                    if len == NO_LEN {
                        regs[dst as usize] = b.wrapping_add(i.wrapping_mul(esz as u64));
                    } else if i < len as u64 {
                        regs[dst as usize] = b.wrapping_add(i * esz as u64);
                    } else {
                        tri!(at!(), Err(self.oob(code, at!(), i, len as u64)));
                    }
                }
                Op::SliceIndexAddr { dst, slice, idx, ik, esz } => {
                    let i = index::<LE>(ik, regs[idx as usize]);
                    let (p, n) = self.vm_slice_parts::<LE>(regs, slice);
                    if i < n {
                        regs[dst as usize] = p.wrapping_add(i * esz as u64);
                    } else {
                        tri!(at!(), Err(self.oob(code, at!(), i, n)));
                    }
                }
                Op::AddI { fl, dst, a, b, k } => {
                    let (l, r) = (nval::<LE>(k, r!(a)), nval::<LE>(k, r!(b)));
                    match n_add(k, l, r) {
                        Some(v) => w!(dst, nput::<LE>(k, v)),
                        None => {
                            let v = tri!(at!(), self.arith_overflow::<LE>(code, at!(), BinOp::Add(fl), k, l, r));
                            w!(dst, v);
                        }
                    }
                }
                Op::SubI { fl, dst, a, b, k } => {
                    let (l, r) = (nval::<LE>(k, r!(a)), nval::<LE>(k, r!(b)));
                    match n_sub(k, l, r) {
                        Some(v) => w!(dst, nput::<LE>(k, v)),
                        None => {
                            let v = tri!(at!(), self.arith_overflow::<LE>(code, at!(), BinOp::Sub(fl), k, l, r));
                            w!(dst, v);
                        }
                    }
                }
                Op::MulI { fl, dst, a, b, k } => {
                    let (l, r) = (nval::<LE>(k, r!(a)), nval::<LE>(k, r!(b)));
                    match n_mul(k, l, r) {
                        Some(v) => w!(dst, nput::<LE>(k, v)),
                        None => {
                            let v = tri!(at!(), self.arith_overflow::<LE>(code, at!(), BinOp::Mul(fl), k, l, r));
                            w!(dst, v);
                        }
                    }
                }
                Op::AndW { dst, a, b } => w!(dst, r!(a) & r!(b)),
                Op::OrW { dst, a, b } => w!(dst, r!(a) | r!(b)),
                Op::XorW { dst, a, b } => w!(dst, r!(a) ^ r!(b)),
                Op::BinI { op, dst, a, b, k, info } => {
                    let bi = &code.bins[info as usize];
                    let l = ival::<LE>(k, regs[a as usize]);
                    let r = ival::<LE>(bi.rk, regs[b as usize]);
                    let v = tri!(at!(), self.small_binop(code.spans[at!() as usize], op, k.si(), bi.lty, l, r));
                    st.regs[dst as usize] = iput::<LE>(k, v);
                }
                Op::CmpI { cmp, dst, a, b, k } => {
                    w!(dst, n_cmp::<LE>(cmp, k, r!(a), r!(b)) as u64);
                }
                Op::BrI { cmp, a, b, k, t } => {
                    if n_cmp::<LE>(cmp, k, r!(a), r!(b)) {
                        jump!(t);
                    }
                }
                Op::TickBr { cmp, a, b, k, exit, body } => {
                    tri!(at!(), self.tick());
                    if COUNT {
                        tri!(at!(), self.check_budget(code.spans[at!() as usize]));
                    }
                    jump!(if n_cmp::<LE>(cmp, k, r!(a), r!(b)) { exit } else { body });
                }
                Op::Widen { dst, src, from, to } => {
                    regs[dst as usize] = iput::<LE>(to, ival::<LE>(from, regs[src as usize]));
                }
                Op::Trunc { dst, src, from, to } => {
                    let v = ival::<LE>(from, regs[src as usize]);
                    regs[dst as usize] = iput::<LE>(to, to.si().value(v as u64));
                }
                Op::EqW { dst, a, b, neg } => {
                    regs[dst as usize] = ((regs[a as usize] == regs[b as usize]) != neg) as u64
                }
                Op::EqBool { dst, a, b, neg } => {
                    let x = regs[a as usize] & 0xff != 0;
                    let y = regs[b as usize] & 0xff != 0;
                    regs[dst as usize] = ((x == y) != neg) as u64;
                }
                Op::Jmp { t } => jump!(t),
                Op::JmpIf { c, t } => {
                    if regs[c as usize] & 0xff != 0 {
                        jump!(t);
                    }
                }
                Op::JmpIfNot { c, t } => {
                    if regs[c as usize] & 0xff == 0 {
                        jump!(t);
                    }
                }
                Op::JmpNZ { c, t } => {
                    if regs[c as usize] != 0 {
                        jump!(t);
                    }
                }
                Op::JmpZ { c, t } => {
                    if regs[c as usize] == 0 {
                        jump!(t);
                    }
                }
                Op::JmpByte { c, off, t, when } => {
                    if (byte_at(regs, c, off) != 0) == when {
                        jump!(t);
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
                        tri!(at!(), Err(trap_flow(code.spans[at!() as usize], msg)));
                    }
                    if (v == tt.want) == when {
                        jump!(t);
                    }
                }
                Op::JmpFast { t } => {
                    if self.fast() {
                        jump!(t);
                    }
                }
                Op::JmpTick { t } => {
                    tri!(at!(), self.tick());
                    if COUNT {
                        tri!(at!(), self.check_budget(code.spans[at!() as usize]));
                    }
                    jump!(t);
                }
                Op::MarkTemps { dst } => regs[dst as usize] = self.temps.len() as u64,
                Op::KillTemps { src } => {
                    let m = regs[src as usize] as usize;
                    self.kill_temps(m);
                }
                Op::Call { f, args, out } => {
                    let span = code.spans[at!() as usize];
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
                    tri!(at!(), r);
                }
                Op::CallInd { callee, cty, args, out } => {
                    tri!(at!(), self.vm_call_ind(st, code, at!(), callee, cty, args, out));
                }
                Op::Perform { eff, op, args, out } => {
                    tri!(at!(), self.vm_perform(st, code, at!(), eff, op, args, out));
                }
                Op::Ret { src } => return Ok(Exit::Ret(src)),
                Op::EndSeg => return Ok(Exit::Seg),
                _ => tri!(at!(), self.vm_slow::<LE>(st, code, *op, at!())),
            }
        }
    }

    /// The result of an addition, subtraction or multiplication whose exact value does not
    /// fit the type (the operands are given as 64-bit patterns), as its flavor defines it.
    #[cold]
    #[inline(never)]
    fn arith_overflow<const LE: bool>(&self, code: &Code, at: u32, op: BinOp, k: IntK, l: u64, r: u64) -> R<u64> {
        let wide = |v: u64| if k.signed { v as i64 as i128 } else { v as i128 };
        // The operand type is only used to report a bad shift amount.
        let v = self.small_binop(code.spans[at as usize], op, k.si(), Ty::bool(), wide(l), wide(r))?;
        Ok(iput::<LE>(k, v))
    }

    /// Calls a function pointer or closure: compiled functions are called straight from the
    /// registers, others through values.
    #[allow(clippy::too_many_arguments)]
    #[inline(never)]
    fn vm_call_ind(
        &mut self,
        st: &mut St<'a>,
        code: &Code<'a>,
        at: u32,
        callee: u32,
        cty: Ty,
        args: u32,
        out: u32,
    ) -> R<()> {
        let span = code.spans[at as usize];
        let list = &code.args[args as usize];
        let (callee, out) = (code.operands[callee as usize], code.operands[out as usize]);
        match callee {
            Arg::Data { reg, size } => {
                let ptr = get_bytes(&st.regs, reg, size);
                let addr = self.ptr_of(&ptr);
                if addr == 0 {
                    return self.trap(span, "call through a null function pointer");
                }
                if let Some(f) = self.fn_from_addr(addr)
                    && let Some(c2) = self.code_of(f)
                {
                    let cx = st.cx.clone();
                    return self.vm_call_list(span, f, c2, st, list, out, None, cx, None);
                }
            }
            Arg::Val(r) => {
                if let Value::Closure(c) = &st.vals[r as usize]
                    && let Some(c2) = self.code_of(c.func)
                {
                    let c = c.clone();
                    let mut ctx = c.ctx.clone();
                    // Effects listed in the function type are handled where it is called (§11.6).
                    if let TyKind::Fn(sig) = cty.kind() {
                        for eff in sig.effects.effects.iter() {
                            if let Some(n) = st.cx.find(eff) {
                                ctx = ctx.push(n.effect.clone(), n.installed.clone(), n.with_frame, n.with_label);
                            }
                        }
                    }
                    let extra = c.env.map(|env| FrameExtra { env: Some(env), installing: None, is_clause_lit: false });
                    return self.vm_call_list(span, c.func, c2, st, list, out, extra, ctx, None);
                }
            }
        }
        let cv = st.value_of(callee);
        let vals = st.values(list);
        let cx = st.cx.clone();
        let v = self.call_value(&cx, span, cv, cty, vals)?;
        write_value(st, out, v, span)
    }

    /// Performs an operation: the clause of the handler is called straight from the
    /// registers when it is compiled, and through values otherwise.
    #[allow(clippy::too_many_arguments)]
    #[inline(never)]
    fn vm_perform(
        &mut self,
        st: &mut St<'a>,
        code: &Code<'a>,
        at: u32,
        eff: u32,
        op: u32,
        args: u32,
        out: u32,
    ) -> R<()> {
        let span = code.spans[at as usize];
        let effect = &code.effects[eff as usize];
        let list = &code.args[args as usize];
        let out = code.operands[out as usize];
        if !code.effect_ctl[eff as usize]
            && let Some(node) = st.cx.find(effect)
        {
            let node = node.clone();
            let installing = Some((node.with_frame, node.with_label));
            let prog = self.prog;
            match &node.installed {
                Installed::Lit(hv) => {
                    let clause = prog.handlers[hv.id.index()].clauses[op as usize];
                    if let Some(c2) = self.code_of(clause) {
                        let extra = FrameExtra { env: hv.env.clone(), installing, is_clause_lit: true };
                        return self.vm_call_list(span, clause, c2, st, list, out, Some(extra), hv.ctx.clone(), None);
                    }
                }
                Installed::Decl { decl, value, outer } => {
                    let clause = prog.handler_decls[decl.index()].clauses[op as usize];
                    if let Some(c2) = self.code_of(clause) {
                        let extra = FrameExtra { env: None, installing, is_clause_lit: false };
                        return self.vm_call_list(
                            span,
                            clause,
                            c2,
                            st,
                            list,
                            out,
                            Some(extra),
                            outer.clone(),
                            Some(value),
                        );
                    }
                }
                Installed::Native(_) => {}
            }
        }
        let vals = st.values(list);
        let cx = st.cx.clone();
        let v = self.perform(&cx, span, effect, op, vals)?;
        write_value(st, out, v, span)
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
                let fr = self.vm_frame(st);
                let slot = match fr.locals.borrow().get(local.index()) {
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
                let fr = self.vm_frame(st);
                self.bind_local(span, &fr, local, Some(ty), Value::Bytes(b))?;
            }
            Op::InitLocal { local, ty } => {
                let ti = self.ti(ty);
                let (size, align) = (ti.size, ti.align);
                let fr = self.vm_frame(st);
                if size <= REG_MAX {
                    self.set_local(span, &fr, local, &[], 1)?;
                } else {
                    self.local_addr(span, &fr, local, size, align)?;
                }
            }
            Op::AddrLocal { dst, local, ty } => {
                let fr = self.vm_frame(st);
                st.regs[dst as usize] = self.vm_local_addr(span, &fr, local, ty)?;
            }
            Op::LdUpvar { dst, levels, local, ty } => {
                let fr = self.vm_frame(st);
                let fr = self.vm_up(span, &fr, levels)?;
                let v = self.read_local(span, &fr, local, ty)?;
                write_value(st, Arg::Data { reg: dst, size: self.ti(ty).size as u32 }, v, span)?;
            }
            Op::AddrUpvar { dst, levels, local, ty } => {
                let fr = self.vm_frame(st);
                let fr = self.vm_up(span, &fr, levels)?;
                st.regs[dst as usize] = self.vm_local_addr(span, &fr, local, ty)?;
            }
            Op::LdLocalVal { dst, local } => {
                let fr = self.vm_frame(st);
                let v = match fr.locals.borrow().get(local.index()) {
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
                let fr = self.vm_frame(st);
                self.bind_local(span, &fr, local, None, v)?;
            }
            Op::LdUpvarVal { dst, levels, local } => {
                let fr = self.vm_frame(st);
                let fr = self.vm_up(span, &fr, levels)?;
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
                if i >= len as u64 {
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
                let kind = code.coercions[kind as usize];
                let b = get_bytes(&st.regs, src, self.ti(from).size as u32);
                let v = self.coerce_bytes(span, kind, from, to, b)?;
                put_bytes(&mut st.regs, dst, self.ti(to).size as u32, &v);
            }
            Op::FnToPtr { dst, src, to } => {
                let v = st.value_of(code.operands[src as usize]);
                let v = self.fn_to_ptr(&st.cx, span, to, v)?;
                write_value(st, code.operands[dst as usize], v, span)?;
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
            Op::Builtin { info } => {
                let BuiltinInfo { op, args, res, out } = code.builtins[info as usize];
                let list = &code.args[args as usize];
                let tys = &code.arg_tys[args as usize];
                let v = if list.len() <= 4 {
                    let mut a = [Bytes::empty(), Bytes::empty(), Bytes::empty(), Bytes::empty()];
                    for (i, x) in list.iter().enumerate() {
                        if let Arg::Data { reg, size } = x {
                            a[i] = get_bytes(&st.regs, *reg, *size);
                        }
                    }
                    self.builtin_vals(span, op, res, tys, &a[..list.len()])?
                } else {
                    let vals = st.bytes(list);
                    self.builtin_vals(span, op, res, tys, &vals)?
                };
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
                let fr = self.vm_frame(st);
                let cx = st.cx.clone();
                let v = self.eval(Act { fr: &fr, cx: &cx }, e)?;
                write_value(st, out, v, span)?;
            }
            Op::TreeStmt { stmt } => {
                let fr = self.vm_frame(st);
                let cx = st.cx.clone();
                self.exec_stmt(Act { fr: &fr, cx: &cx }, code.stmts[stmt as usize])?;
            }
            Op::ThrowBreak { levels, label, v } => {
                let fr = self.vm_frame(st);
                let target = self.vm_up(span, &fr, levels)?;
                let value = st.value_of(v);
                return Err(Box::new(Flow::Break { frame: target.id, label, value }));
            }
            Op::ThrowBreakInst { v } => {
                let fr = self.vm_frame(st);
                let Some((frame, label)) = fr.installing else {
                    return self.trap(span, "internal error: no installing `with` for this break");
                };
                let value = st.value_of(v);
                return Err(Box::new(Flow::Break { frame, label, value }));
            }
            Op::ThrowContinue { levels, label } => {
                let fr = self.vm_frame(st);
                let target = self.vm_up(span, &fr, levels)?;
                return Err(Box::new(Flow::Continue { frame: target.id, label }));
            }
            Op::ThrowReturn { levels, v } => {
                let fr = self.vm_frame(st);
                let Some(target) = self.up_frame(&fr, levels) else {
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
                st.cx = st.cx.push(eff, Installed::Lit(hv), st.id, label);
            }
            Op::InstallDecl { info, src, size, label } => {
                let b = get_bytes(&st.regs, src, size);
                let outer = st.cx.clone();
                for d in code.decls[info as usize].iter() {
                    let eff = self.prog.handler_decls[d.index()].effect.clone();
                    let inst = Installed::Decl { decl: *d, value: b.clone(), outer: outer.clone() };
                    st.cx = st.cx.push(eff, inst, st.id, label);
                }
            }
            Op::MkClosure { dst, f, env } => {
                let env = if env { Some(self.vm_frame(st)) } else { None };
                st.vals[dst as usize] = Value::Closure(Closure { func: f, env, ctx: st.cx.clone() });
            }
            Op::MkHandler { dst, h } => {
                let hv = HandlerVal { id: h, env: Some(self.vm_frame(st)), ctx: st.cx.clone() };
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

/// Counts of the instructions run, by kind (`--features vm-profile`; printed at exit
/// when `GAMMA_VM_PROFILE` is set).
#[cfg(feature = "vm-profile")]
pub mod profile {
    use super::Op;
    use std::sync::Mutex;

    static COUNTS: Mutex<Vec<(u64, String)>> = Mutex::new(Vec::new());
    static PAIRS: std::sync::LazyLock<Mutex<(String, std::collections::HashMap<String, u64>)>> =
        std::sync::LazyLock::new(|| Mutex::new((String::new(), std::collections::HashMap::new())));

    pub fn count(op: &Op) {
        {
            let name = format!("{:?}", op).split(' ').next().unwrap_or("").to_string();
            let mut p = PAIRS.lock().unwrap();
            let key = format!("{} {}", p.0, name);
            *p.1.entry(key).or_insert(0) += 1;
            p.0 = name;
        }
        // SAFETY: reads the discriminant byte of the enum, for statistics only.
        let d = unsafe { *(op as *const Op as *const u8) } as usize;
        let mut c = COUNTS.lock().unwrap();
        if c.len() <= d {
            c.resize(d + 1, (0, String::new()));
        }
        if c[d].0 == 0 {
            c[d].1 = format!("{:?}", op).split(' ').next().unwrap_or("").to_string();
        }
        c[d].0 += 1;
    }

    pub fn report() {
        if std::env::var_os("GAMMA_VM_PROFILE").is_none() {
            return;
        }
        let mut c = COUNTS.lock().unwrap().clone();
        c.sort_by_key(|x| std::cmp::Reverse(x.0));
        let total: u64 = c.iter().map(|x| x.0).sum();
        if std::env::var_os("GAMMA_VM_PAIRS").is_some() {
            let mut v: Vec<_> = PAIRS.lock().unwrap().1.iter().map(|(k, n)| (*n, k.clone())).collect();
            v.sort_by_key(|x| std::cmp::Reverse(x.0));
            for (n, k) in v.iter().take(25) {
                eprintln!("{:12} {:5.1}% {}", n, 100.0 * *n as f64 / total as f64, k);
            }
        }
        for (n, name) in c.iter().filter(|x| x.0 > 0) {
            eprintln!("{:12} {:5.1}% {}", n, 100.0 * *n as f64 / total as f64, name);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn kinds() -> Vec<IntK> {
        let mut v = Vec::new();
        for bits in [1u8, 2, 7, 8, 9, 15, 16, 17, 24, 31, 32, 33, 48, 63, 64] {
            for signed in [false, true] {
                if signed && bits < 2 {
                    continue;
                }
                let size = (bits as usize).div_ceil(8).next_power_of_two().min(8) as u8;
                v.push(IntK { signed, bits, size });
                let tight = bits.div_ceil(8);
                if tight != size {
                    v.push(IntK { signed, bits, size: tight });
                }
            }
        }
        v
    }

    fn samples(k: IntK) -> Vec<i128> {
        let si = k.si();
        let (lo, hi) = (si.min(), si.max());
        let mut v =
            vec![0, 1, 2, 3, -1, -2, 7, 100, lo, lo + 1, lo + 2, hi, hi - 1, hi - 2, hi / 2, hi / 2 + 1, lo / 2];
        v.retain(|x| si.fits(*x));
        v.sort();
        v.dedup();
        v
    }

    fn check<const LE: bool>() {
        for k in kinds() {
            let si = k.si();
            for &a in &samples(k) {
                // Round trip through the word format.
                let wa = iput::<LE>(k, a);
                assert_eq!(nput::<LE>(k, a as u64), wa, "{k:?} {a}");
                assert_eq!(nval::<LE>(k, wa), a as u64, "{k:?} {a}");
                assert_eq!(ival::<LE>(k, wa), a, "{k:?} {a}");
                assert!(nfits(k, a as u64));
                for &b in &samples(k) {
                    let (wb, la, lb) = (iput::<LE>(k, b), a as u64, b as u64);
                    for (name, got, exact) in [
                        ("+", n_add(k, la, lb), a.checked_add(b)),
                        ("-", n_sub(k, la, lb), a.checked_sub(b)),
                        // A product of two u64 values can exceed even i128.
                        ("*", n_mul(k, la, lb), a.checked_mul(b)),
                    ] {
                        // An overflow in the 64-bit domain is reported as one too.
                        let exact = exact.filter(|x| si.fits(*x));
                        assert_eq!(got.is_some(), exact.is_some(), "{k:?} {a} {name} {b}");
                        if let (Some(v), Some(x)) = (got, exact) {
                            assert_eq!(v, x as u64, "{k:?} {a} {name} {b}");
                        }
                    }
                    for cmp in [Cmp::Lt, Cmp::Le, Cmp::Gt, Cmp::Ge, Cmp::Eq, Cmp::Ne] {
                        let want = match cmp {
                            Cmp::Lt => a < b,
                            Cmp::Le => a <= b,
                            Cmp::Gt => a > b,
                            Cmp::Ge => a >= b,
                            Cmp::Eq => a == b,
                            Cmp::Ne => a != b,
                        };
                        assert_eq!(n_cmp::<LE>(cmp, k, wa, wb), want, "{k:?} {a} {cmp:?} {b}");
                    }
                }
            }
        }
    }

    #[test]
    fn integer_kernels_match_exact_arithmetic() {
        check::<true>();
        check::<false>();
    }
}
