//! Calls, handler contexts, `with`, `perform`, native ambient handlers and `@write`.

use super::bytes::Bytes;
use super::machine::*;
use super::memory::AllocKind;
use super::value::*;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use std::rc::Rc;

impl<'a> Machine<'a> {
    pub fn eval_args(&mut self, a: Act, args: &[Expr]) -> R<Vec<Value>> {
        let mut vals = Vec::with_capacity(args.len());
        for x in args {
            vals.push(self.eval(a, x)?);
        }
        Ok(vals)
    }

    pub fn eval_call(&mut self, a: Act, e: &Expr, callee: &Callee, args: &[Expr]) -> R {
        match callee {
            Callee::Fn(f) => {
                let vals = self.eval_args(a, args)?;
                self.call_function(e.span, *f, vals, None, a.cx.clone(), None, false)
            }
            Callee::Indirect(c) => {
                let cv = self.eval(a, c)?;
                let vals = self.eval_args(a, args)?;
                self.call_value(a, e.span, cv, c.ty, vals)
            }
            Callee::Perform { effect, op } => {
                let vals = self.eval_args(a, args)?;
                self.perform(a, e.span, effect, *op, vals)
            }
        }
    }

    /// Calls a function value (second-class closure or function pointer) from the context `a.cx`.
    pub fn call_value(&mut self, a: Act, span: Span, callee: Value, callee_ty: Ty, args: Vec<Value>) -> R {
        match callee {
            Value::Closure(c) => {
                let mut ctx = c.ctx.clone();
                // Effects listed in the function type are handled where it is called (§11.6).
                if let TyKind::Fn(sig) = callee_ty.kind() {
                    for eff in sig.effects.effects.iter() {
                        if let Some(n) = a.cx.find(eff) {
                            ctx = ctx.push(n.effect.clone(), n.installed.clone(), n.with_frame, n.with_label);
                        }
                    }
                }
                self.call_function(span, c.func, args, c.env.clone(), ctx, None, false)
            }
            Value::Bytes(b) => {
                let addr = self.ptr_of(&b);
                if addr == 0 {
                    return self.trap(span, "call through a null function pointer");
                }
                match self.fn_from_addr(addr) {
                    Some(f) => self.call_function(span, f, args, None, a.cx.clone(), None, false),
                    None => self.trap(span, format!("call through an invalid function pointer {:#x}", addr)),
                }
            }
            Value::Handler(_) => self.trap(span, "internal error: a handler value was called"),
        }
    }

    #[allow(clippy::too_many_arguments)]
    #[inline(never)]
    pub fn call_function(
        &mut self,
        span: Span,
        fid: FnId,
        args: Vec<Value>,
        env: Option<Rc<Frame>>,
        ctx: Ctx,
        installing: Option<(u64, LabelId)>,
        is_clause_lit: bool,
    ) -> R {
        let prog = self.prog;
        let f = &prog.fns[fid.index()];
        let Some(body) = &f.body else {
            return self
                .trap(span, format!("not supported by the interpreter yet: call to foreign function `{}`", f.name));
        };
        if self.depth >= MAX_DEPTH {
            return self.trap(span, "stack overflow");
        }
        let marker = 0u8;
        if (&marker as *const u8 as usize) < self.stack_floor {
            return self.trap(span, "stack overflow");
        }
        if args.len() != f.params.len() {
            return self.trap(
                span,
                format!(
                    "internal error: `{}` called with {} arguments, expected {}",
                    f.name,
                    args.len(),
                    f.params.len()
                ),
            );
        }
        // Running out of a fiber's stack is checked (§14.1).
        let cost = if self.fiber_stack == u64::MAX { 0 } else { self.frame_cost(fid) };
        if cost > self.fiber_stack && !self.fast() {
            return self.trap(span, "stack overflow on a fiber stack");
        }
        self.fiber_stack = self.fiber_stack.saturating_sub(cost);
        let frame = self.new_frame(f.ret, Some(fid), env, f.locals.len(), installing, is_clause_lit);
        for (p, v) in f.params.iter().zip(args) {
            let ty = f.locals[p.index()].ty;
            self.bind_local(span, &frame, *p, Some(ty), v)?;
        }
        self.depth += 1;
        let mark = self.temps.len();
        let r = self.eval(Act { fr: &frame, cx: &ctx }, body);
        self.depth -= 1;
        if self.fiber_stack != u64::MAX {
            self.fiber_stack += cost;
        }
        self.kill_temps(mark);
        self.kill_frame(&frame);
        match r {
            Ok(v) => Ok(v),
            Err(flow) => {
                if let Flow::Return { frame: id, .. } = &*flow
                    && *id == frame.id
                {
                    let Flow::Return { value, .. } = *flow else { unreachable!() };
                    return Ok(value);
                }
                Err(flow)
            }
        }
    }

    // --- effects ------------------------------------------------------------------------------------

    pub fn perform(&mut self, a: Act, span: Span, effect: &EffectRef, op: u32, args: Vec<Value>) -> R {
        let Some(node) = a.cx.find(effect).cloned() else {
            return self.trap(span, format!("no handler installed for effect `{}`", effect.effect.name()));
        };
        if self.prog.ctl_ops.contains(&(effect.effect, op)) {
            return self.ctl_perform(span, node, op, args);
        }
        self.call_clause(span, &node, op, args)
    }

    /// Calls the clause for operation `op` of the handler installed at `node`.
    pub fn call_clause(&mut self, span: Span, node: &CtxNode, op: u32, mut args: Vec<Value>) -> R {
        let installing = Some((node.with_frame, node.with_label));
        let prog = self.prog;
        match &node.installed {
            Installed::Lit(hv) => {
                let clause = prog.handlers[hv.id.index()].clauses[op as usize];
                self.call_function(span, clause, args, hv.env.clone(), hv.ctx.clone(), installing, true)
            }
            Installed::Decl { decl, value, outer } => {
                let clause = prog.handler_decls[decl.index()].clauses[op as usize];
                args.insert(0, Value::Bytes(value.clone()));
                self.call_function(span, clause, args, None, outer.clone(), installing, false)
            }
            Installed::Native(kind) => self.perform_native(span, *kind, op, args),
        }
    }

    #[inline(never)]
    pub fn eval_with(
        &mut self,
        a: Act,
        e: &Expr,
        label: LabelId,
        handlers: &[HandlerValue],
        on: Option<&Expr>,
        body: &Expr,
    ) -> R {
        let prog = self.prog;
        let mut cx = a.cx.clone();
        for h in handlers {
            match h {
                HandlerValue::Literal(x) => {
                    let v = self.eval(Act { fr: a.fr, cx: &cx }, x)?;
                    let Value::Handler(hv) = v else {
                        return self.trap(x.span, "internal error: `with` of a non-handler value");
                    };
                    let eff = prog.handlers[hv.id.index()].effect.clone();
                    cx = cx.push(eff, Installed::Lit(hv), a.fr.id, label);
                }
                HandlerValue::Decl { decls, value } => {
                    let b = self.eval_bytes(Act { fr: a.fr, cx: &cx }, value)?;
                    let outer = cx.clone();
                    for d in decls {
                        let eff = prog.handler_decls[d.index()].effect.clone();
                        let inst = Installed::Decl { decl: *d, value: b.clone(), outer: outer.clone() };
                        cx = cx.push(eff, inst, a.fr.id, label);
                    }
                }
            }
        }
        if let Some(stack) = on {
            let sb = self.eval_bytes(Act { fr: a.fr, cx: a.cx }, stack)?;
            return self.eval_with_on(a, e.span, label, cx, &sb, body);
        }
        match self.eval(Act { fr: a.fr, cx: &cx }, body) {
            Err(f) => match *f {
                Flow::Break { frame, label: l, value } if frame == a.fr.id && l == label => Ok(value),
                other => Err(Box::new(other)),
            },
            ok => ok,
        }
    }

    // --- native ambient handlers ---------------------------------------------------------------------

    fn perform_native(&mut self, span: Span, kind: NativeKind, op: u32, args: Vec<Value>) -> R {
        match (kind, op) {
            (NativeKind::Out | NativeKind::Err, 0) => {
                let Some(Value::Bytes(s)) = args.first() else {
                    return self.trap(span, "internal error: bad argument to a write operation");
                };
                let data = self.read_str(span, s)?;
                let w = if kind == NativeKind::Out { self.out.as_mut() } else { self.errw.as_mut() };
                if let Some(w) = w {
                    let _ = w.write_all(&data);
                    if kind == NativeKind::Err {
                        let _ = w.flush();
                    }
                }
                Ok(Value::unit())
            }
            (NativeKind::Env, 0) => {
                let strs: Vec<String> = self.args.clone();
                let ps = self.ptr_size();
                let mut table = Bytes::zeroed(strs.len() * 2 * ps);
                for (i, s) in strs.iter().enumerate() {
                    let rc: Rc<[u8]> = s.as_bytes().to_vec().into();
                    let addr = self.static_bytes(span, &rc)?;
                    let slice = self.make_slice(addr, rc.len() as u64);
                    table[i * 2 * ps..(i + 1) * 2 * ps].copy_from_slice(&slice);
                }
                let taddr = self.alloc_mem(span, table.len(), 16, AllocKind::Heap, false)?;
                let r = self.mem.init(taddr, &table);
                self.mem_err(span, r)?;
                Ok(Value::Bytes(self.make_slice(taddr, strs.len() as u64)))
            }
            (NativeKind::Env, 1) => {
                let Some(Value::Bytes(s)) = args.first() else {
                    return self.trap(span, "internal error: bad argument to Env.var");
                };
                let name = self.read_str(span, s)?;
                let name = String::from_utf8_lossy(&name).into_owned();
                let opt_ty = Ty::optional(Ty::string());
                match std::env::var(&name) {
                    Ok(v) => {
                        let rc: Rc<[u8]> = v.into_bytes().into();
                        let addr = self.static_bytes(span, &rc)?;
                        let slice = self.make_slice(addr, rc.len() as u64);
                        Ok(Value::Bytes(self.make_some(opt_ty, &slice)))
                    }
                    Err(_) => Ok(Value::Bytes(self.make_none(opt_ty))),
                }
            }
            (NativeKind::Env, 2) => {
                let code = match args.first() {
                    Some(Value::Bytes(b)) => b.first().copied().unwrap_or(0),
                    _ => 0,
                };
                Err(Box::new(Flow::Exit(code)))
            }
            _ => self.trap(span, format!("not supported by the interpreter yet: native operation {}", op)),
        }
    }

    // --- @write --------------------------------------------------------------------------------------------

    #[inline(never)]
    pub fn eval_write(&mut self, a: Act, e: &Expr, sink: &WriteSink, pieces: &[Rc<[u8]>], args: &[Expr]) -> R {
        let sink_val = match sink {
            WriteSink::Fn(x) => Some((self.eval(a, x)?, x.ty)),
            WriteSink::Perform { .. } => None,
        };
        let mut vals = Vec::with_capacity(args.len());
        for x in args {
            vals.push(self.eval_bytes(a, x)?);
        }
        for i in 0..pieces.len() {
            if !pieces[i].is_empty() {
                self.emit(a, e.span, sink, &sink_val, &pieces[i])?;
            }
            if i < args.len() {
                let mut out = Vec::new();
                let r = self.format_value(&mut out, args[i].ty, &vals[i], true);
                self.tr(args[i].span, r)?;
                if !out.is_empty() {
                    self.emit(a, e.span, sink, &sink_val, &out)?;
                }
            }
        }
        Ok(Value::unit())
    }

    fn emit(&mut self, a: Act, span: Span, sink: &WriteSink, sv: &Option<(Value, Ty)>, data: &[u8]) -> R<()> {
        let addr = self.alloc_mem(span, data.len(), 1, AllocKind::Temp, true)?;
        let r = self.mem.init(addr, data);
        self.mem_err(span, r)?;
        let slice = Value::Bytes(self.make_slice(addr, data.len() as u64));
        let r = match sink {
            WriteSink::Fn(_) => {
                let (cv, cty) = sv.as_ref().unwrap();
                self.call_value(a, span, cv.clone(), *cty, vec![slice])
            }
            WriteSink::Perform { effect, op } => self.perform(a, span, effect, *op, vec![slice]),
        };
        self.mem.kill(addr);
        r.map(|_| ())
    }
}
