//! Calls, handler contexts, `with`, `perform`, native ambient handlers and `@write`.

use super::bytes::Bytes;
use super::machine::*;
use super::memory::AllocKind;
use super::value::*;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use std::rc::Rc;

pub(super) use crate::tir::MAX_FRAME;

impl<'a> Machine<'a> {
    /// Calls a function value (second-class closure or function pointer) from the context `cx`.
    pub fn call_value(&mut self, cx: &Ctx, span: Span, callee: Value, callee_ty: Ty, args: Vec<Value>) -> R {
        match callee {
            Value::Closure(c) => {
                let mut ctx = c.ctx.clone();
                // Effects listed in the function type are handled where it is called (§11.6).
                if let TyKind::Fn(sig) = callee_ty.kind() {
                    for eff in sig.effects.effects.iter() {
                        if let Some(n) = cx.find(eff) {
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
                    Some(f) => self.call_function(span, f, args, None, cx.clone(), None, false),
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
        match self.code_of(fid) {
            Some(code) => self.vm_call_values(span, fid, code, args, env, ctx, installing, is_clause_lit),
            None => self.call_foreign(span, fid, args, &[]),
        }
    }

    // --- effects ------------------------------------------------------------------------------------

    pub fn perform(&mut self, cx: &Ctx, span: Span, effect: &EffectRef, op: u32, args: Vec<Value>) -> R {
        let Some(node) = cx.find(effect).cloned() else {
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

    /// `@write` once the sink and the arguments are evaluated: passes the pieces of the
    /// template and the formatted arguments `vals` (of the types of `args`) to the sink.
    #[allow(clippy::too_many_arguments)]
    pub fn write_pieces(
        &mut self,
        cx: &Ctx,
        span: Span,
        sink: &WriteSink,
        sink_val: Option<&(Value, Ty)>,
        pieces: &[Rc<[u8]>],
        specs: &[FmtSpec],
        args: &[Expr],
        vals: &[Bytes],
    ) -> R<()> {
        for i in 0..pieces.len() {
            if !pieces[i].is_empty() {
                self.emit(cx, span, sink, sink_val, &pieces[i])?;
            }
            if i < args.len() {
                let mut out = Vec::new();
                let r = match specs.get(i) {
                    Some(s) if !s.is_default() => self.format_spec(&mut out, args[i].ty, &vals[i], s),
                    _ => self.format_value(&mut out, args[i].ty, &vals[i], true),
                };
                self.tr(args[i].span, r)?;
                if !out.is_empty() {
                    self.emit(cx, span, sink, sink_val, &out)?;
                }
            }
        }
        Ok(())
    }

    fn emit(&mut self, cx: &Ctx, span: Span, sink: &WriteSink, sv: Option<&(Value, Ty)>, data: &[u8]) -> R<()> {
        let addr = self.alloc_mem(span, data.len(), 1, AllocKind::Temp, true)?;
        let r = self.mem.init(addr, data);
        self.mem_err(span, r)?;
        let slice = Value::Bytes(self.make_slice(addr, data.len() as u64));
        let r = match sink {
            WriteSink::Fn(_) => {
                let (cv, cty) = sv.unwrap();
                self.call_value(cx, span, cv.clone(), *cty, vec![slice])
            }
            WriteSink::Perform { effect, op } => self.perform(cx, span, effect, *op, vec![slice]),
        };
        self.mem.kill(addr);
        r.map(|_| ())
    }
}
