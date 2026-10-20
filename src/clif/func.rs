//! Functions: frames, locals and temporaries, the ladder of non-local exits, blocks and
//! statements, and the functions the back end generates besides those of Gamma functions
//! (the bodies of `with … on`, clause thunks, fiber start functions and entry points).
//!
//! A Gamma function's frame is one stack slot laid out as the C back end's frame struct (see
//! [`FrameLayout`]); its other locals are SSA variables. Non-local exits work as in the C
//! back end (`src/cgen/func.rs`): an exit sets `gm_unw` and jumps to the rung of the
//! *ladder* for the current position, the code that runs the pending `defer`s outwards and
//! tests, at each loop or `with`, whether it is the exit's target. Each scope's rung is a
//! block emitted once, when the scope ends, and only if an exit uses it.

use super::cabi::{CArg, CRet, CSig, parts_size};
use super::{CResult, FrameLayout, Gen, Rep, Work, unsupported};
use crate::source::Span;
use crate::tir::{Block as TBlock, Expr, ExprKind, FnDef, FnId, FnKind, LabelId, LocalId, Stmt};
use crate::types::{Ty, TyKind};
use cranelift_codegen::cursor::{Cursor, FuncCursor};
use cranelift_codegen::ir::condcodes::IntCC;
use cranelift_codegen::ir::{
    self, Block, FuncRef, Function, GlobalValue, InstBuilder, MemFlagsData, SigRef, Signature, StackSlotData,
    StackSlotKind, TrapCode, Type, UserFuncName, Value, types,
};
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext, Variable};
use cranelift_module::{DataId, FuncId, Module};
use std::collections::HashMap;
use std::rc::Rc;

/// The flags of every load and store: Gamma memory is untyped bytes, possibly unaligned.
pub fn mf() -> MemFlagsData {
    MemFlagsData::new()
}

/// A value as generated code holds it.
#[derive(Clone, Copy, Debug)]
pub enum V {
    /// A scalar in a register.
    S(Value),
    /// A value in memory: the address of its bytes, which nothing else refers to.
    M(Value),
    /// A value without bytes.
    Z,
}

/// Where a local lives.
#[derive(Clone, Copy, Debug)]
pub enum Loc {
    /// It has no bytes.
    None,
    Var(Variable),
    /// At this offset in the frame.
    Frame(u64),
}

pub enum Scope<'a> {
    /// A block: its `defer`s registered so far, `(is_errdefer, body)`, and the entries of
    /// its rung that exits use: entry `k` (index `k - 1`) runs defers `k - 1` down to 0.
    Block { defers: Vec<(bool, &'a Expr)>, rungs: Vec<Option<Block>> },
    /// A loop: where `break` and `continue` go.
    Loop { label: LabelId, brk: Block, cont: Block, rung: Option<Block> },
    /// A `with`: the block after it.
    With { label: LabelId, end: Block, rung: Option<Block> },
}

/// The frame an exit targets.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ExitFrame {
    /// This function's frame.
    Own,
    /// A frame given at run time (a `gm_frame *`, or null for `Env.exit`).
    At(Value),
}

/// The label an exit targets.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ExitLabel {
    Static(u32),
    At(Value),
}

/// Kinds of non-local exits, as `gm_unw.kind` holds them.
pub const NONE: i64 = 0;
pub const BREAK: i64 = 1;
pub const CONTINUE: i64 = 2;
pub const RETURN: i64 = 3;
pub const EXIT: i64 = 4;
pub const RESUME: i64 = 5;

pub struct FnGen<'f, 'g, 'a> {
    pub g: &'g mut Gen<'a>,
    pub b: FunctionBuilder<'f>,
    pub fid: FnId,
    pub def: &'a FnDef,
    pub frame: Rc<FrameLayout>,
    /// The address of the frame.
    pub fptr: Value,
    pub env: Value,
    pub cx: Value,
    pub inst: Value,
    /// Where a large result goes.
    pub sret: Option<Value>,
    entry: Block,
    /// The addresses of `gm_unw`, `gm_bt_top` and `gm_stack_limit`, once asked for.
    tls: [Option<Value>; 3],
    pub locals: Vec<Loc>,
    pub scopes: Vec<Scope<'a>>,
    /// Set while generating a `defer` body: it cannot exit non-locally.
    pub no_check: bool,
    /// In the body of a `with … on`, which runs on a fiber: the fiber (`gm_fiber *`).
    pub outlined: Option<Value>,
    /// The outermost rung of the ladder, once used.
    lad: Option<Block>,
    pub fret: Block,
    pub fprop: Block,
    funcs: HashMap<FuncId, FuncRef>,
    datas: HashMap<DataId, GlobalValue>,
    sigs: HashMap<Signature, SigRef>,
    /// In Debug builds, this function's backtrace record (`gm_bt`).
    pub bt: Option<Value>,
}

/// A function being built, before its [`FnGen`].
struct Shell {
    func: Function,
    fctx: FunctionBuilderContext,
}

impl Shell {
    fn new(g: &mut Gen, id: FuncId, sig: Signature) -> Shell {
        Shell {
            func: Function::with_name_signature(UserFuncName::user(0, id.as_u32()), sig),
            fctx: std::mem::take(&mut g.fctx),
        }
    }

    fn finish(self, g: &mut Gen, id: FuncId, name: &str) -> CResult<()> {
        g.fctx = self.fctx;
        g.define(id, self.func, name)
    }
}

/// The stack bytes of a frame, by the cost model of `@stack_size`.
fn frame_cost(g: &Gen, def: &FnDef) -> u64 {
    crate::tir::frame_cost(def, &mut |t| g.size(t))
}

/// Generates Gamma function `f`.
pub fn gen_fn(g: &mut Gen, f: FnId) -> CResult<()> {
    let prog = g.prog;
    let def = &prog.fns[f.index()];
    let Some(body) = &def.body else {
        return unsupported(def.span, format!("the function `{}` has no body", def.name));
    };
    let id = g.func(f)?;
    let sig = g.fn_sig(f);
    let name = def.name.as_str().to_string();
    let mut shell = Shell::new(g, id, sig);
    let r = gen_fn_body(g, f, def, body, &mut shell);
    match r {
        Ok(()) => shell.finish(g, id, &name),
        Err(e) => {
            g.fctx = FunctionBuilderContext::new();
            Err(e)
        }
    }
}

fn gen_fn_body<'a>(g: &mut Gen<'a>, f: FnId, def: &'a FnDef, body: &'a Expr, shell: &mut Shell) -> CResult<()> {
    let plain = g.is_plain(f);
    let cost = frame_cost(g, def);
    let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
    let entry = b.create_block();
    b.append_block_params_for_function_params(entry);
    b.switch_to_block(entry);
    b.ensure_inserted_block();
    let params = b.block_params(entry).to_vec();
    if cost > crate::tir::MAX_FRAME {
        // No stack can hold the frame: every call is a stack overflow, reported as the
        // interpreter reports it.
        let name = g.cstring(def.name.as_str().as_bytes())?;
        let gv = g.module.declare_data_in_func(name, b.func);
        let p = b.ins().symbol_value(types::I64, gv);
        let n = b.ins().iconst(types::I64, cost as i64);
        let fid = g.rt("gm_frame_overflow", "pl");
        let fr = g.module.declare_func_in_func(fid, b.func);
        b.ins().call(fr, &[p, n]);
        b.ins().trap(TrapCode::user(1).unwrap());
        b.seal_all_blocks();
        b.finalize(g.module.target_config());
        return Ok(());
    }
    let layout = g.frame(f);
    let mut it = params.into_iter();
    let sret = matches!(g.rep(def.ret), Rep::Mem { .. }).then(|| it.next().unwrap());
    let (env, cx, inst) = if plain {
        let z = b.ins().iconst(types::I64, 0);
        (z, z, z)
    } else {
        (it.next().unwrap(), it.next().unwrap(), it.next().unwrap())
    };
    let args: Vec<Value> = it.collect();
    let fptr = frame_slot(&mut b, layout.size, layout.align);
    let mut fg = FnGen::new(g, b, f, def, layout, fptr, env, cx, inst, sret, entry);
    fg.prologue(true)?;
    // The header: the enclosing frame, and whether this is a handler literal's clause.
    fg.b.ins().store(mf(), env, fptr, 0);
    let clause_lit = if def.kind == FnKind::Clause {
        fg.b.ins().icmp_imm_s(IntCC::NotEqual, env, 0)
    } else {
        fg.b.ins().iconst(types::I8, 0)
    };
    fg.b.ins().store(mf(), clause_lit, fptr, 8);
    // The parameters: values in memory are copied into the frame.
    let mut ai = 0;
    for p in &def.params {
        let t = def.locals[p.index()].ty;
        let v = match fg.g.rep(t) {
            Rep::Zero => V::Z,
            Rep::Mem { .. } => {
                let a = args[ai];
                ai += 1;
                V::M(a)
            }
            _ => {
                let a = args[ai];
                ai += 1;
                V::S(a)
            }
        };
        fg.set_local(*p, v);
    }
    if !body.ty.is_never() {
        let dst = fg.frame_addr(fg.frame.ret);
        fg.expr_into(body, dst, true)?;
    } else {
        fg.expr(body)?;
    }
    fg.goto(fg.fret);
    fg.epilogues()?;
    fg.finish();
    Ok(())
}

/// The frame's stack slot, at an address aligned to `align` (stack slots are aligned to 16
/// bytes at most).
fn frame_slot(b: &mut FunctionBuilder, size: u64, align: u64) -> Value {
    aligned_slot(b, size, align)
}

fn aligned_slot(b: &mut FunctionBuilder, size: u64, align: u64) -> Value {
    let align = align.max(1);
    if align <= 16 {
        let ss = b.create_sized_stack_slot(StackSlotData::new(
            StackSlotKind::ExplicitSlot,
            size.max(1) as u32,
            align.trailing_zeros() as u8,
        ));
        return b.ins().stack_addr(types::I64, ss, 0);
    }
    let ss = b.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, (size + align) as u32, 4));
    let a = b.ins().stack_addr(types::I64, ss, 0);
    let a = b.ins().iadd_imm_s(a, align as i64 - 1);
    b.ins().band_imm_s(a, -(align as i64))
}

impl<'f, 'g, 'a> FnGen<'f, 'g, 'a> {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        g: &'g mut Gen<'a>,
        mut b: FunctionBuilder<'f>,
        fid: FnId,
        def: &'a FnDef,
        frame: Rc<FrameLayout>,
        fptr: Value,
        env: Value,
        cx: Value,
        inst: Value,
        sret: Option<Value>,
        entry: Block,
    ) -> Self {
        let fret = b.create_block();
        let fprop = b.create_block();
        let mut locals = vec![];
        for (i, l) in def.locals.iter().enumerate() {
            let rep = g.rep(l.ty);
            locals.push(match (frame.locals[i], rep.ty()) {
                (Some(off), _) => Loc::Frame(off),
                (None, Some(ty)) => Loc::Var(b.declare_var(ty)),
                (None, None) => Loc::None,
            });
        }
        FnGen {
            g,
            b,
            fid,
            def,
            frame,
            fptr,
            env,
            cx,
            inst,
            sret,
            entry,
            tls: [None; 3],
            locals,
            scopes: vec![],
            no_check: false,
            outlined: None,
            lad: None,
            fret,
            fprop,
            funcs: HashMap::new(),
            datas: HashMap::new(),
            sigs: HashMap::new(),
            bt: None,
        }
    }

    /// The checks at the start of a function: the stack, and in Debug builds the
    /// backtrace record. `own`: the function's own frame (not a `with … on` body's).
    fn prologue(&mut self, own: bool) -> CResult<()> {
        if self.g.opts.checks {
            // Running out of stack is checked (§14.1, §18): the thread's or the fiber's.
            let lim = self.tls(2);
            let lim = self.b.ins().load(types::I64, mf(), lim, 0);
            let sp = self.b.ins().get_stack_pointer(types::I64);
            let low = self.b.ins().icmp(IntCC::UnsignedLessThan, sp, lim);
            self.cold_if(low, |fg| {
                fg.rt_call("gm_stack_overflow", "", &[]);
                Ok(())
            })?;
        }
        if self.g.opts.debug {
            let bt = self.slot(32, 8);
            let top = self.tls(1);
            let prev = self.b.ins().load(types::I64, mf(), top, 0);
            let prev = if own {
                prev
            } else {
                // A `with … on` body belongs to the frame of the enclosing function, whose
                // record the driver's chain ends with: its record stands in for that one.
                let z = self.b.ins().iconst(types::I64, 0);
                self.var_select(types::I64, prev, |fg| fg.b.ins().load(types::I64, mf(), prev, 0), z)
            };
            self.b.ins().store(mf(), prev, bt, 0);
            let name = match self.g.prog.backtrace_name(self.fid, self.g.sources) {
                Some(n) => {
                    let d = self.g.cstring(n.as_bytes())?;
                    self.data_addr(d)
                }
                None => self.b.ins().iconst(types::I64, 0),
            };
            self.b.ins().store(mf(), name, bt, 8);
            let z = self.b.ins().iconst(types::I64, 0);
            self.b.ins().store(mf(), z, bt, 16);
            self.b.ins().store(mf(), z, bt, 24);
            self.b.ins().store(mf(), bt, top, 0);
            self.bt = Some(bt);
        }
        Ok(())
    }

    /// `if cond { then(p) } else { z }` for a pointer-sized `p` (used where a load must not
    /// happen when `p` is null).
    fn var_select(&mut self, ty: Type, p: Value, then: impl FnOnce(&mut Self) -> Value, z: Value) -> Value {
        let var = self.b.declare_var(ty);
        self.b.def_var(var, z);
        let t = self.b.create_block();
        let j = self.b.create_block();
        self.b.ins().brif(p, t, &[], j, &[]);
        self.b.switch_to_block(t);
        let v = then(self);
        self.b.def_var(var, v);
        self.b.ins().jump(j, &[]);
        self.b.switch_to_block(j);
        self.b.use_var(var)
    }

    /// Pops the backtrace record and in Debug builds fills the dead frame with 0xDD (§18.3).
    fn kill_frame(&mut self) {
        if let Some(bt) = self.bt {
            let top = self.tls(1);
            let prev = self.b.ins().load(types::I64, mf(), bt, 0);
            self.b.ins().store(mf(), prev, top, 0);
        }
        if self.g.opts.debug && self.outlined.is_none() {
            let c = self.b.ins().iconst(types::I32, 0xDD);
            let n = self.b.ins().iconst(types::I64, self.frame.size as i64);
            self.rt_call("gm_clif_poison", "pil", &[self.fptr, c, n]);
        }
    }

    /// The function's last blocks: the outermost rung of the ladder, `fret` (return the
    /// result) and `fprop` (return while an exit propagates).
    fn epilogues(&mut self) -> CResult<()> {
        if let Some(lad) = self.lad {
            self.switch(lad);
            let unw = self.tls(0);
            let kind = self.b.ins().load(types::I32, mf(), unw, 0);
            let is_ret = self.b.ins().icmp_imm_s(IntCC::Equal, kind, RETURN);
            let fr = self.b.ins().load(types::I64, mf(), unw, 8);
            let own = self.b.ins().icmp(IntCC::Equal, fr, self.fptr);
            let both = self.b.ins().band(is_ret, own);
            let clear = self.b.create_block();
            let fprop = self.fprop;
            self.brif(both, clear, fprop);
            self.switch(clear);
            let z = self.b.ins().iconst(types::I32, NONE);
            self.b.ins().store(mf(), z, unw, 0);
            self.goto(self.fret);
        }
        let ret = self.def.ret;
        let rep = self.g.rep(ret);
        for (k, blk) in [self.fret, self.fprop].into_iter().enumerate() {
            self.switch(blk);
            if let Some(fb) = self.outlined {
                self.rt_call("gm_fiber_finish", "p", &[fb]);
                self.unreachable();
                continue;
            }
            match rep {
                Rep::Mem { size, .. } => {
                    if k == 0 {
                        let src = self.frame_addr(self.frame.ret);
                        self.copy(self.sret.unwrap(), src, size);
                    }
                    self.kill_frame();
                    self.b.ins().return_(&[]);
                }
                Rep::Zero => {
                    self.kill_frame();
                    self.b.ins().return_(&[]);
                }
                r => {
                    let ty = r.ty().unwrap();
                    let v = if k == 0 {
                        let a = self.frame_addr(self.frame.ret);
                        let v = self.b.ins().load(ty, mf(), a, 0);
                        self.normalize(r, v)
                    } else {
                        self.zero_scalar(ty)
                    };
                    self.kill_frame();
                    self.b.ins().return_(&[v]);
                }
            }
            self.dead();
        }
        Ok(())
    }

    /// Completes the function: seals its blocks.
    pub fn finish(mut self) {
        self.b.ins().trap(TrapCode::user(1).unwrap());
        self.b.seal_all_blocks();
        let cfg = self.g.module.target_config();
        self.b.finalize(cfg);
    }

    // --- blocks ------------------------------------------------------------------------------

    /// Continues in a new block that nothing jumps to, after a terminator.
    pub fn dead(&mut self) {
        let d = self.b.create_block();
        self.b.switch_to_block(d);
    }

    /// Jumps to `blk` and continues in an unreachable block.
    pub fn goto(&mut self, blk: Block) {
        self.b.ins().jump(blk, &[]);
        self.dead();
    }

    /// Branches on `c` and continues in an unreachable block.
    pub fn brif(&mut self, c: Value, t: Block, f: Block) {
        self.b.ins().brif(c, t, &[], f, &[]);
        self.dead();
    }

    /// Continues in `blk`, which the current position falls through to.
    pub fn switch(&mut self, blk: Block) {
        self.b.ins().jump(blk, &[]);
        self.b.switch_to_block(blk);
    }

    /// Code that cannot be reached (after a call that does not return).
    pub fn unreachable(&mut self) {
        self.b.ins().trap(TrapCode::user(1).unwrap());
        self.dead();
    }

    /// Runs `then` when `c` holds; `then` ends in a call that does not return.
    pub fn cold_if(&mut self, c: Value, then: impl FnOnce(&mut Self) -> CResult<()>) -> CResult<()> {
        let t = self.b.create_block();
        let j = self.b.create_block();
        self.b.set_cold_block(t);
        self.b.ins().brif(c, t, &[], j, &[]);
        self.b.switch_to_block(t);
        then(self)?;
        self.unreachable();
        self.switch(j);
        Ok(())
    }

    /// Runs `then` when `c` holds.
    pub fn if_then(&mut self, c: Value, then: impl FnOnce(&mut Self) -> CResult<()>) -> CResult<()> {
        let t = self.b.create_block();
        let j = self.b.create_block();
        self.b.ins().brif(c, t, &[], j, &[]);
        self.b.switch_to_block(t);
        then(self)?;
        self.switch(j);
        Ok(())
    }

    /// Runs `then` when `c` holds, else `els`.
    pub fn if_else(
        &mut self,
        c: Value,
        then: impl FnOnce(&mut Self) -> CResult<()>,
        els: impl FnOnce(&mut Self) -> CResult<()>,
    ) -> CResult<()> {
        let t = self.b.create_block();
        let e = self.b.create_block();
        let j = self.b.create_block();
        self.b.ins().brif(c, t, &[], e, &[]);
        self.b.switch_to_block(t);
        then(self)?;
        self.goto(j);
        self.switch(e);
        els(self)?;
        self.switch(j);
        Ok(())
    }

    /// A scalar of type `ty` that is `then` when `c` holds, else `els`, each computed only
    /// on its branch.
    pub fn cond_value(
        &mut self,
        ty: Type,
        c: Value,
        then: impl FnOnce(&mut Self) -> CResult<Value>,
        els: impl FnOnce(&mut Self) -> CResult<Value>,
    ) -> CResult<Value> {
        let var = self.b.declare_var(ty);
        self.if_else(
            c,
            |fg| {
                let v = then(fg)?;
                fg.b.def_var(var, v);
                Ok(())
            },
            |fg| {
                let v = els(fg)?;
                fg.b.def_var(var, v);
                Ok(())
            },
        )?;
        Ok(self.b.use_var(var))
    }

    // --- references to functions and data -----------------------------------------------------

    pub fn fref(&mut self, id: FuncId) -> FuncRef {
        if let Some(&r) = self.funcs.get(&id) {
            return r;
        }
        let r = self.g.module.declare_func_in_func(id, self.b.func);
        self.funcs.insert(id, r);
        r
    }

    pub fn sigref(&mut self, sig: Signature) -> SigRef {
        if let Some(&r) = self.sigs.get(&sig) {
            return r;
        }
        let r = self.b.import_signature(sig.clone());
        self.sigs.insert(sig, r);
        r
    }

    fn gv(&mut self, id: DataId) -> GlobalValue {
        if let Some(&r) = self.datas.get(&id) {
            return r;
        }
        let r = self.g.module.declare_data_in_func(id, self.b.func);
        self.datas.insert(id, r);
        r
    }

    /// The address of static data `id`.
    pub fn data_addr(&mut self, id: DataId) -> Value {
        let gv = self.gv(id);
        self.b.ins().symbol_value(types::I64, gv)
    }

    /// The address of this thread's instance of thread-local data `id`.
    pub fn tls_addr(&mut self, id: DataId) -> Value {
        let gv = self.gv(id);
        self.b.ins().tls_value(types::I64, gv)
    }

    /// The address of a string's bytes, followed by a NUL byte, in static data.
    pub fn cstr(&mut self, bytes: &[u8]) -> CResult<Value> {
        let d = self.g.cstring(bytes)?;
        Ok(self.data_addr(d))
    }

    /// Calls function `id`.
    pub fn call(&mut self, id: FuncId, args: &[Value]) -> Vec<Value> {
        let fr = self.fref(id);
        let inst = self.b.ins().call(fr, args);
        self.b.inst_results(inst).to_vec()
    }

    /// Calls runtime function `name` of signature `sig` (see [`Gen::rt`]).
    pub fn rt_call(&mut self, name: &'static str, sig: &str, args: &[Value]) -> Vec<Value> {
        let id = self.g.rt(name, sig);
        self.call(id, args)
    }

    /// The address of `gm_unw` (0), `gm_bt_top` (1) or `gm_stack_limit` (2), computed once,
    /// at the start of the function (a fiber never moves to another thread).
    pub fn tls(&mut self, k: usize) -> Value {
        if let Some(v) = self.tls[k] {
            return v;
        }
        let name = ["gm_clif_unw", "gm_clif_bt_top", "gm_clif_stack_limit"][k];
        let id = self.g.rt(name, ">p");
        let fr = self.fref(id);
        let mut cur = FuncCursor::new(self.b.func).at_first_insertion_point(self.entry);
        let inst = cur.ins().call(fr, &[]);
        let v = cur.func.dfg.first_result(inst);
        self.tls[k] = Some(v);
        v
    }

    /// The file, line and column of `span`, for a trap.
    pub fn loc(&mut self, span: Span) -> CResult<[Value; 3]> {
        if span == Span::DUMMY {
            let z = self.b.ins().iconst(types::I64, 0);
            let z32 = self.b.ins().iconst(types::I32, 0);
            return Ok([z, z32, z32]);
        }
        let file = self.g.src_file(span.file.0)?;
        let f = self.data_addr(file);
        let (line, col) = self.g.sources.file(span.file).line_col(span.lo);
        let l = self.b.ins().iconst(types::I32, line as i64);
        let c = self.b.ins().iconst(types::I32, col as i64);
        Ok([f, l, c])
    }

    /// Stops the program with `msg` at `span` (§18).
    pub fn trap(&mut self, msg: &str, span: Span) -> CResult<()> {
        let m = self.cstr(msg.as_bytes())?;
        let [f, l, c] = self.loc(span)?;
        self.rt_call("gm_trap", "ppii", &[m, f, l, c]);
        self.unreachable();
        Ok(())
    }

    /// Traps with `msg` at `span` when `c` holds.
    pub fn trap_if(&mut self, c: Value, msg: &str, span: Span) -> CResult<()> {
        self.cold_if(c, |fg| {
            let m = fg.cstr(msg.as_bytes())?;
            let [f, l, cl] = fg.loc(span)?;
            fg.rt_call("gm_trap", "ppii", &[m, f, l, cl]);
            Ok(())
        })
    }

    /// In Debug builds, records `span` as the location of the call about to be made, for
    /// backtraces.
    pub fn call_site(&mut self, span: Span) -> CResult<()> {
        if let Some(bt) = self.bt
            && span != Span::DUMMY
        {
            let [f, l, c] = self.loc(span)?;
            self.b.ins().store(mf(), f, bt, 16);
            self.b.ins().store(mf(), l, bt, 24);
            self.b.ins().store(mf(), c, bt, 28);
        }
        Ok(())
    }

    // --- memory -------------------------------------------------------------------------------

    pub fn iconst(&mut self, ty: Type, v: i64) -> Value {
        if ty == types::I128 {
            let lo = self.b.ins().iconst(types::I64, v);
            let hi = self.b.ins().iconst(types::I64, if v < 0 { -1 } else { 0 });
            self.b.ins().iconcat(lo, hi)
        } else {
            self.b.ins().iconst(ty, v)
        }
    }

    /// The integer of type `ty` whose two's complement bits are `v` (modulo the width).
    pub fn iconst_bits(&mut self, ty: Type, v: u128) -> Value {
        if ty == types::I128 {
            let lo = self.b.ins().iconst(types::I64, v as u64 as i64);
            let hi = self.b.ins().iconst(types::I64, (v >> 64) as u64 as i64);
            self.b.ins().iconcat(lo, hi)
        } else {
            let bits = ty.bits();
            let m = if bits >= 64 { v as u64 } else { (v as u64) & ((1u64 << bits) - 1) };
            // `iconst` takes the bits of narrow types zero-extended.
            self.b.ins().iconst(ty, m as i64)
        }
    }

    pub fn zero_scalar(&mut self, ty: Type) -> Value {
        match ty {
            types::F32 => self.b.ins().f32const(0.0),
            types::F64 => self.b.ins().f64const(0.0),
            t => self.iconst(t, 0),
        }
    }

    /// A new stack slot of `size` bytes aligned to `align`; its address.
    pub fn slot(&mut self, size: u64, align: u64) -> Value {
        aligned_slot(&mut self.b, size, align)
    }

    /// A new temporary for a value of type `t` (in memory); its address.
    pub fn temp(&mut self, t: Ty) -> Value {
        let (size, align) = (self.g.size(t), self.g.align(t));
        self.slot(size, align)
    }

    /// The address at `off` in the frame.
    pub fn frame_addr(&mut self, off: u64) -> Value {
        self.b.ins().iadd_imm_s(self.fptr, off as i64)
    }

    pub fn add(&mut self, a: Value, off: u64) -> Value {
        if off == 0 { a } else { self.b.ins().iadd_imm_s(a, off as i64) }
    }

    /// Copies `size` bytes from `src` to `dst`, which may overlap.
    pub fn copy(&mut self, dst: Value, src: Value, size: u64) {
        if size == 0 {
            return;
        }
        if size <= 64 {
            let mut chunks = vec![];
            let mut off = 0;
            while off < size {
                let n = [8u64, 4, 2, 1].into_iter().find(|&n| off + n <= size).unwrap();
                let ty = super::int_type(n);
                chunks.push((off, self.b.ins().load(ty, mf(), src, off as i32)));
                off += n;
            }
            for (off, v) in chunks {
                self.b.ins().store(mf(), v, dst, off as i32);
            }
            return;
        }
        let n = self.b.ins().iconst(types::I64, size as i64);
        self.rt_call("gm_clif_memmove", "ppl>p", &[dst, src, n]);
    }

    /// Fills `size` bytes at `dst + off` with zeros.
    pub fn zero(&mut self, dst: Value, off: u64, size: u64) {
        if size == 0 {
            return;
        }
        if size <= 64 {
            let mut o = off;
            let end = off + size;
            while o < end {
                let n = [8u64, 4, 2, 1].into_iter().find(|&n| o + n <= end && o.is_multiple_of(n)).unwrap_or(1);
                let ty = super::int_type(n);
                let z = self.b.ins().iconst(ty, 0);
                self.b.ins().store(mf(), z, dst, o as i32);
                o += n;
            }
            return;
        }
        let p = self.add(dst, off);
        let z = self.b.ins().iconst(types::I32, 0);
        let n = self.b.ins().iconst(types::I64, size as i64);
        self.rt_call("gm_clif_memset", "pil>p", &[p, z, n]);
    }

    /// Fills `size` bytes at `dst` with the byte `c`.
    pub fn fill(&mut self, dst: Value, c: u8, size: u64) {
        if size == 0 {
            return;
        }
        let z = self.b.ins().iconst(types::I32, c as i64);
        let n = self.b.ins().iconst(types::I64, size as i64);
        self.rt_call("gm_clif_memset", "pil>p", &[dst, z, n]);
    }

    /// `v` (of representation `r`) reduced to its bits: the bits of memory beyond a narrow
    /// integer's width are unspecified, and values in registers are kept normalized.
    pub fn normalize(&mut self, r: Rep, v: Value) -> Value {
        match r {
            Rep::Int { ty, bits, signed } if bits < ty.bits() => {
                let shift = (ty.bits() - bits) as i64;
                if signed {
                    let x = self.b.ins().ishl_imm_s(v, shift);
                    self.b.ins().sshr_imm_s(x, shift)
                } else if ty == types::I128 {
                    let m = self.iconst_bits(ty, (1u128 << bits) - 1);
                    self.b.ins().band(v, m)
                } else {
                    self.b.ins().band_imm_s(v, ((1u64 << bits) - 1) as i64)
                }
            }
            Rep::Bool => {
                // Any nonzero byte is true.
                self.b.ins().icmp_imm_s(IntCC::NotEqual, v, 0)
            }
            _ => v,
        }
    }

    /// The value of type `t` at `addr`, as this function's own copy.
    pub fn read(&mut self, t: Ty, addr: Value) -> V {
        match self.g.rep(t) {
            Rep::Zero => V::Z,
            Rep::Mem { size, align } => {
                let tmp = self.slot(size, align);
                self.copy(tmp, addr, size);
                V::M(tmp)
            }
            r => {
                let v = self.b.ins().load(r.ty().unwrap(), mf(), addr, 0);
                V::S(self.normalize(r, v))
            }
        }
    }

    /// The scalar of type `t` at `addr + off`.
    pub fn load_s(&mut self, t: Ty, addr: Value, off: u64) -> Value {
        let r = self.g.rep(t);
        let v = self.b.ins().load(r.ty().expect("a scalar"), mf(), addr, off as i32);
        self.normalize(r, v)
    }

    /// Stores `v`, of type `t`, at `addr`.
    pub fn write(&mut self, t: Ty, v: V, addr: Value) {
        match v {
            V::Z => {}
            V::S(x) => {
                self.b.ins().store(mf(), x, addr, 0);
            }
            V::M(src) => {
                let size = self.g.size(t);
                self.copy(addr, src, size);
            }
        }
    }

    /// The address of a value: itself if in memory, else of a temporary holding it.
    pub fn spill(&mut self, t: Ty, v: V) -> Value {
        match v {
            V::M(a) => a,
            V::S(x) => {
                let a = self.temp(t);
                self.b.ins().store(mf(), x, a, 0);
                a
            }
            V::Z => self.slot(0, 1),
        }
    }

    /// Some value of type `t`, for code that cannot be reached or `---`.
    pub fn undef(&mut self, t: Ty) -> V {
        match self.g.rep(t) {
            Rep::Zero => V::Z,
            Rep::Mem { size, align } => V::M(self.slot(size, align)),
            r => V::S(self.zero_scalar(r.ty().unwrap())),
        }
    }

    /// A value of type `t` whose bytes are all zero.
    pub fn zeroed(&mut self, t: Ty) -> V {
        match self.g.rep(t) {
            Rep::Zero => V::Z,
            Rep::Mem { size, align } => {
                let a = self.slot(size, align);
                self.zero(a, 0, size);
                V::M(a)
            }
            r => V::S(self.zero_scalar(r.ty().unwrap())),
        }
    }

    // --- locals --------------------------------------------------------------------------------

    /// The address of local `l` (which lives in the frame).
    pub fn local_addr(&mut self, l: LocalId) -> CResult<Value> {
        match self.locals[l.index()] {
            Loc::Frame(off) => Ok(self.frame_addr(off)),
            Loc::None => Ok(self.slot(0, 1)),
            Loc::Var(_) => Err(super::Unsupported {
                message: format!(
                    "internal error: the Cranelift back end needs the address of local `{}`, kept in a register",
                    self.def.locals[l.index()].name
                ),
                span: self.def.locals[l.index()].span,
            }),
        }
    }

    pub fn get_local(&mut self, l: LocalId) -> CResult<V> {
        let t = self.def.locals[l.index()].ty;
        match self.locals[l.index()] {
            Loc::None => Ok(V::Z),
            Loc::Var(var) => Ok(V::S(self.b.use_var(var))),
            Loc::Frame(off) => {
                let a = self.frame_addr(off);
                Ok(self.read(t, a))
            }
        }
    }

    pub fn set_local(&mut self, l: LocalId, v: V) {
        let t = self.def.locals[l.index()].ty;
        match (self.locals[l.index()], v) {
            (Loc::None, _) | (_, V::Z) => {}
            (Loc::Var(var), V::S(x)) => self.b.def_var(var, x),
            (Loc::Var(var), V::M(a)) => {
                let x = self.load_s(t, a, 0);
                self.b.def_var(var, x)
            }
            (Loc::Frame(off), v) => {
                let a = self.frame_addr(off);
                self.write(t, v, a);
            }
        }
    }

    /// The frame `levels` up the capture chain: its address and its function.
    pub fn frame_up(&mut self, levels: u32) -> (Value, FnId) {
        let mut p = self.fptr;
        let mut f = self.fid;
        for _ in 0..levels {
            p = self.b.ins().load(types::I64, mf(), p, 0);
            f = self.g.prog.fns[f.index()].parent.unwrap_or(f);
        }
        (p, f)
    }

    // --- non-local exits ---------------------------------------------------------------------

    /// After a call that may exit non-locally: runs the ladder if an exit is in progress.
    pub fn check_unwind(&mut self) {
        if self.no_check {
            return;
        }
        let unw = self.tls(0);
        let kind = self.b.ins().load(types::I32, mf(), unw, 0);
        let target = self.ladder_target();
        let cont = self.b.create_block();
        self.b.ins().brif(kind, target, &[], cont, &[]);
        self.b.switch_to_block(cont);
    }

    /// Jumps to the rung of the ladder for the current position.
    pub fn ladder(&mut self) {
        let t = self.ladder_target();
        self.goto(t);
    }

    /// The block of the rung of the ladder for the current position.
    pub fn ladder_target(&mut self) -> Block {
        for i in (0..self.scopes.len()).rev() {
            let s = &mut self.scopes[i];
            match s {
                Scope::Block { defers, .. } if defers.is_empty() => {}
                Scope::Block { defers, rungs } => {
                    let k = defers.len();
                    if rungs.len() < k {
                        rungs.resize(k, None);
                    }
                    if let Some(b) = rungs[k - 1] {
                        return b;
                    }
                    let b = self.b.create_block();
                    let Scope::Block { rungs, .. } = &mut self.scopes[i] else { unreachable!() };
                    rungs[k - 1] = Some(b);
                    return b;
                }
                Scope::Loop { rung, .. } | Scope::With { rung, .. } => {
                    if let Some(b) = *rung {
                        return b;
                    }
                    let b = self.b.create_block();
                    match &mut self.scopes[i] {
                        Scope::Loop { rung, .. } | Scope::With { rung, .. } => *rung = Some(b),
                        _ => unreachable!(),
                    }
                    return b;
                }
            }
        }
        if self.outlined.is_some() {
            return self.fprop;
        }
        match self.lad {
            Some(b) => b,
            None => {
                let b = self.b.create_block();
                self.lad = Some(b);
                b
            }
        }
    }

    /// Emits the rung of the ladder for scope `s`, just popped, if an exit uses it: it runs
    /// the scope's part of the ladder and goes on to the rung of the enclosing scope. The
    /// code before it continues after it.
    pub fn end_scope(&mut self, s: Scope<'a>) {
        let used = match &s {
            Scope::Block { rungs, .. } => rungs.iter().any(|r| r.is_some()),
            Scope::Loop { rung, .. } | Scope::With { rung, .. } => rung.is_some(),
        };
        if !used {
            return;
        }
        let after = self.b.create_block();
        self.goto(after);
        match s {
            Scope::Block { defers, mut rungs } => {
                let top = rungs.iter().rposition(|r| r.is_some()).unwrap();
                for k in (1..=top + 1).rev() {
                    let blk = match rungs[k - 1] {
                        Some(b) => b,
                        None => {
                            let b = self.b.create_block();
                            rungs[k - 1] = Some(b);
                            b
                        }
                    };
                    self.switch(blk);
                    let (is_err, body) = defers[k - 1];
                    self.defer_body(is_err, body, true);
                }
            }
            Scope::Loop { label, brk, cont, rung } => {
                self.switch(rung.unwrap());
                let unw = self.tls(0);
                let fr = self.b.ins().load(types::I64, mf(), unw, 8);
                let own = self.b.ins().icmp(IntCC::Equal, fr, self.fptr);
                let lab = self.b.ins().load(types::I32, mf(), unw, 16);
                let mine = self.b.ins().icmp_imm_s(IntCC::Equal, lab, label.0 as i64);
                let ours = self.b.ins().band(own, mine);
                let kind = self.b.ins().load(types::I32, mf(), unw, 0);
                let is_brk = self.b.ins().icmp_imm_s(IntCC::Equal, kind, BREAK);
                let is_cont = self.b.ins().icmp_imm_s(IntCC::Equal, kind, CONTINUE);
                let b1 = self.b.ins().band(ours, is_brk);
                let c1 = self.b.ins().band(ours, is_cont);
                let (tb, tc, next) = (self.b.create_block(), self.b.create_block(), self.b.create_block());
                self.b.ins().brif(b1, tb, &[], next, &[]);
                self.b.switch_to_block(tb);
                let z = self.b.ins().iconst(types::I32, NONE);
                self.b.ins().store(mf(), z, unw, 0);
                self.goto(brk);
                self.b.switch_to_block(next);
                let next2 = self.b.create_block();
                self.b.ins().brif(c1, tc, &[], next2, &[]);
                self.b.switch_to_block(tc);
                let z = self.b.ins().iconst(types::I32, NONE);
                self.b.ins().store(mf(), z, unw, 0);
                self.goto(cont);
                self.b.switch_to_block(next2);
            }
            Scope::With { label, end, rung } => {
                self.switch(rung.unwrap());
                self.with_rung_test(label, end);
            }
        }
        self.ladder();
        self.switch(after);
    }

    /// The test of a `with`'s rung: a `break` to the `with` ends there.
    pub fn with_rung_test(&mut self, label: LabelId, end: Block) {
        let unw = self.tls(0);
        let fr = self.b.ins().load(types::I64, mf(), unw, 8);
        let own = self.b.ins().icmp(IntCC::Equal, fr, self.fptr);
        let lab = self.b.ins().load(types::I32, mf(), unw, 16);
        let mine = self.b.ins().icmp_imm_s(IntCC::Equal, lab, label.0 as i64);
        let kind = self.b.ins().load(types::I32, mf(), unw, 0);
        let is_brk = self.b.ins().icmp_imm_s(IntCC::Equal, kind, BREAK);
        let a = self.b.ins().band(own, mine);
        let c = self.b.ins().band(a, is_brk);
        let t = self.b.create_block();
        let next = self.b.create_block();
        self.b.ins().brif(c, t, &[], next, &[]);
        self.b.switch_to_block(t);
        let z = self.b.ins().iconst(types::I32, NONE);
        self.b.ins().store(mf(), z, unw, 0);
        self.goto(end);
        self.b.switch_to_block(next);
    }

    /// The code of a deferred statement, run by a ladder (`unwinding`) or at the end of its
    /// block; an errdefer only if the exit in progress runs errdefers. While it runs during
    /// an exit, the exit is set aside, so that the calls it makes return normally.
    fn defer_body(&mut self, is_err: bool, body: &'a Expr, unwinding: bool) {
        let saved_scopes = std::mem::take(&mut self.scopes);
        let saved_check = std::mem::replace(&mut self.no_check, true);
        let skip = self.b.create_block();
        if is_err {
            let r = self.rt_call("gm_errdefer_runs", "p>b", &[self.fptr])[0];
            let run = self.b.create_block();
            self.b.ins().brif(r, run, &[], skip, &[]);
            self.b.switch_to_block(run);
        }
        let saved = unwinding.then(|| {
            let unw = self.tls(0);
            let s = self.slot(24, 8);
            self.copy(s, unw, 24);
            let z = self.b.ins().iconst(types::I32, NONE);
            self.b.ins().store(mf(), z, unw, 0);
            s
        });
        // A defer body translated when its statement was first seen.
        let _ = self.expr(body);
        if let Some(s) = saved {
            let unw = self.tls(0);
            self.copy(unw, s, 24);
        }
        self.switch(skip);
        self.no_check = saved_check;
        self.scopes = saved_scopes;
    }

    /// Starts a non-local exit of `kind` to `frame` and `label`.
    pub fn start_exit(&mut self, kind: i64, frame: ExitFrame, label: ExitLabel, is_err: Option<Value>) {
        if frame == ExitFrame::Own && self.outlined.is_none() && self.local_exit(kind, label, is_err) {
            return;
        }
        let unw = self.tls(0);
        let k = self.b.ins().iconst(types::I32, kind);
        self.b.ins().store(mf(), k, unw, 0);
        let fr = match frame {
            ExitFrame::Own => self.fptr,
            ExitFrame::At(v) => v,
        };
        self.b.ins().store(mf(), fr, unw, 8);
        let l = match label {
            ExitLabel::Static(n) => self.b.ins().iconst(types::I32, n as i64),
            ExitLabel::At(v) => v,
        };
        self.b.ins().store(mf(), l, unw, 16);
        let e = match is_err {
            Some(v) => v,
            None => self.b.ins().iconst(types::I8, 0),
        };
        self.b.ins().store(mf(), e, unw, 20);
        let saved = std::mem::replace(&mut self.no_check, false);
        self.ladder();
        self.no_check = saved;
    }

    /// An exit whose target is in this function jumps there directly, running the defers
    /// it crosses without going through `gm_unw`, when it crosses none. Returns false
    /// (emitting nothing) otherwise.
    fn local_exit(&mut self, kind: i64, label: ExitLabel, is_err: Option<Value>) -> bool {
        let target = match (kind, label) {
            (RETURN, _) => None,
            (BREAK | CONTINUE, ExitLabel::Static(lab)) => {
                let found = self.scopes.iter().rposition(|s| match s {
                    Scope::Loop { label: l, .. } => l.0 == lab,
                    Scope::With { label: l, .. } => kind == BREAK && l.0 == lab,
                    Scope::Block { .. } => false,
                });
                match found {
                    Some(i) => Some(i),
                    None => return false,
                }
            }
            _ => return false,
        };
        let start = target.map_or(0, |i| i + 1);
        // Errdefers run on a `return` of an error only; an exit that crosses a `defer`
        // takes the ladder, so that each deferred body is emitted once.
        let err_possible = kind == RETURN && is_err.is_some();
        let crosses = self.scopes[start..].iter().any(|s| match s {
            Scope::Block { defers, .. } => defers.iter().any(|(e, _)| !e || err_possible),
            _ => false,
        });
        if crosses {
            return false;
        }
        let dest = match target.map(|i| &self.scopes[i]) {
            None => self.fret,
            Some(Scope::Loop { brk, cont, .. }) => {
                if kind == BREAK {
                    *brk
                } else {
                    *cont
                }
            }
            Some(Scope::With { end, .. }) => *end,
            Some(Scope::Block { .. }) => unreachable!(),
        };
        self.goto(dest);
        true
    }

    // --- blocks and statements ----------------------------------------------------------------

    /// A block's value, in an owned temporary or register.
    pub fn block(&mut self, b: &'a TBlock, ty: Ty) -> CResult<V> {
        self.scopes.push(Scope::Block { defers: vec![], rungs: vec![] });
        let r = self.block_inner(b, ty);
        let scope = self.scopes.pop().unwrap();
        let v = r?;
        // Leaving normally runs the plain defers (not the errdefers), last first.
        if let Scope::Block { defers, .. } = &scope {
            for &(is_err, body) in defers.iter().rev() {
                if !is_err {
                    self.defer_body(false, body, false);
                }
            }
        }
        self.end_scope(scope);
        Ok(v)
    }

    fn block_inner(&mut self, b: &'a TBlock, ty: Ty) -> CResult<V> {
        self.stmts(&b.stmts)?;
        match &b.tail {
            Some(t) => {
                let v = self.expr(t)?;
                if t.ty.is_never() { Ok(self.undef(ty)) } else { Ok(v) }
            }
            None => Ok(V::Z),
        }
    }

    /// A block whose value goes to `dst`.
    pub fn block_into(&mut self, b: &'a TBlock, dst: Value, fresh: bool) -> CResult<()> {
        self.scopes.push(Scope::Block { defers: vec![], rungs: vec![] });
        let r = self.block_into_inner(b, dst, fresh);
        let scope = self.scopes.pop().unwrap();
        let pending = r?;
        if let Scope::Block { defers, .. } = &scope {
            for &(is_err, body) in defers.iter().rev() {
                if !is_err {
                    self.defer_body(false, body, false);
                }
            }
        }
        if let Some((t, v)) = pending {
            self.write(t, v, dst);
        }
        self.end_scope(scope);
        Ok(())
    }

    /// The statements and tail of a block whose value goes to `dst`: the value is built
    /// there, unless defers run after it is computed: it is then returned, to be stored
    /// after them.
    fn block_into_inner(&mut self, b: &'a TBlock, dst: Value, fresh: bool) -> CResult<Option<(Ty, V)>> {
        self.stmts(&b.stmts)?;
        let t = b.tail.as_ref().unwrap();
        if self.no_defers_here() {
            self.expr_into(t, dst, fresh)?;
            Ok(None)
        } else {
            let v = self.expr(t)?;
            Ok((!t.ty.is_never()).then_some((t.ty, v)))
        }
    }

    fn no_defers_here(&self) -> bool {
        matches!(self.scopes.last(), Some(Scope::Block { defers, .. }) if defers.is_empty())
    }

    pub fn stmts(&mut self, stmts: &'a [Stmt]) -> CResult<()> {
        for s in stmts {
            self.stmt(s)?;
        }
        Ok(())
    }

    fn stmt(&mut self, s: &'a Stmt) -> CResult<()> {
        match s {
            // `---` leaves the local as it is (filled with 0xAA in Debug builds, §18.3).
            Stmt::Let { local, init: Some(Expr { kind: ExprKind::Uninit, ty, .. }) } => {
                if self.g.opts.debug {
                    match self.locals[local.index()] {
                        Loc::Frame(off) => {
                            let a = self.frame_addr(off);
                            let n = self.g.size(*ty);
                            self.fill(a, 0xAA, n);
                        }
                        Loc::Var(_) => {
                            let a = self.temp(*ty);
                            let n = self.g.size(*ty);
                            self.fill(a, 0xAA, n);
                            let v = self.read(*ty, a);
                            self.set_local(*local, v);
                        }
                        Loc::None => {}
                    }
                }
            }
            Stmt::Let { local, init: Some(init) } => match self.locals[local.index()] {
                Loc::Frame(off) => {
                    let a = self.frame_addr(off);
                    self.expr_into(init, a, true)?;
                }
                _ => {
                    let v = self.expr(init)?;
                    if !init.ty.is_never() {
                        self.set_local(*local, v);
                    }
                }
            },
            Stmt::Let { init: None, .. } => {}
            Stmt::LetTuple { locals, init } => {
                let v = self.expr(init)?;
                if init.ty.is_never() {
                    return Ok(());
                }
                let a = self.spill(init.ty, v);
                let offs = self.g.offsets(init.ty);
                for (i, l) in locals.iter().enumerate() {
                    if let Some(l) = l {
                        let lt = self.def.locals[l.index()].ty;
                        let p = self.add(a, offs[i]);
                        let x = self.read(lt, p);
                        self.set_local(*l, x);
                    }
                }
            }
            Stmt::Assign { place, value } => self.assign(place, value)?,
            Stmt::AssignTuple { places, value } => {
                let mut addrs = vec![];
                for p in places {
                    addrs.push(match p {
                        Some(p) if !self.is_var_local(p) => Some(self.place(p)?),
                        _ => None,
                    });
                }
                let v = self.expr(value)?;
                if value.ty.is_never() {
                    return Ok(());
                }
                let t = self.spill(value.ty, v);
                let offs = self.g.offsets(value.ty);
                // All values are read before any store.
                let mut vals = vec![];
                for (i, p) in places.iter().enumerate() {
                    if let Some(pe) = p {
                        let src = self.add(t, offs[i]);
                        vals.push(Some(self.read(pe.ty, src)));
                    } else {
                        vals.push(None);
                    }
                }
                for (i, p) in places.iter().enumerate() {
                    if let (Some(pe), Some(x)) = (p, vals[i]) {
                        match (addrs[i], &pe.kind) {
                            (Some(a), _) => self.write(pe.ty, x, a),
                            (None, ExprKind::Local(l)) => self.set_local(*l, x),
                            _ => {}
                        }
                    }
                }
            }
            Stmt::Expr(e) => {
                self.expr(e)?;
            }
            Stmt::Defer { is_err, body } => {
                // Check that it translates: its code is emitted where it runs. It is
                // generated into a block nothing reaches.
                let saved_scopes = std::mem::take(&mut self.scopes);
                let saved_check = std::mem::replace(&mut self.no_check, true);
                let after = self.b.create_block();
                self.goto(after);
                let r = self.expr(body);
                self.switch(after);
                self.no_check = saved_check;
                self.scopes = saved_scopes;
                r?;
                if let Some(Scope::Block { defers, .. }) = self.scopes.last_mut() {
                    defers.push((*is_err, body));
                }
            }
        }
        Ok(())
    }

    /// Whether `e` is a local kept in a register.
    pub fn is_var_local(&self, e: &Expr) -> bool {
        matches!(&e.kind, ExprKind::Local(l) if matches!(self.locals[l.index()], Loc::Var(_) | Loc::None))
    }
}

/// Generates the body of a `with … on` of function `fid` as the function `func`, which runs
/// on a fiber (§14): `void (gm_fiber *fb)`. It reaches the enclosing frame through the
/// fiber, and its ladder ends at the fiber's base.
pub fn gen_with_body<'a>(
    g: &mut Gen<'a>,
    func: FuncId,
    fid: FnId,
    label: LabelId,
    ty: Ty,
    body: &'a Expr,
) -> CResult<()> {
    let prog = g.prog;
    let def = &prog.fns[fid.index()];
    let mut sig = Signature::new(g.cc);
    sig.params.push(ir::AbiParam::new(types::I64));
    let mut shell = Shell::new(g, func, sig);
    let r = (|| {
        let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
        let entry = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        b.ensure_inserted_block();
        let fb = b.block_params(entry)[0];
        let fptr = b.ins().load(types::I64, mf(), fb, 120);
        let env = b.ins().load(types::I64, mf(), fb, 128);
        let cx = b.ins().load(types::I64, mf(), fb, 152);
        let inst = b.ins().load(types::I64, mf(), fb, 160);
        let layout = g.frame(fid);
        let mut fg = FnGen::new(g, b, fid, def, layout, fptr, env, cx, inst, None, entry);
        fg.outlined = Some(fb);
        fg.prologue(false)?;
        let v = fg.expr(body)?;
        if !body.ty.is_never() && !ty.is_never() {
            let off = fg.frame.withs[&label];
            let a = fg.frame_addr(off);
            fg.write(ty, v, a);
        }
        fg.goto(fg.fprop);
        fg.epilogues()?;
        fg.finish();
        Ok(())
    })();
    match r {
        Ok(()) => shell.finish(g, func, "with … on"),
        Err(e) => {
            g.fctx = FunctionBuilderContext::new();
            Err(e)
        }
    }
}

/// Generates a thunk that calls clause `op` of a `ctl` operation for a pending perform
/// (§14.2): `void (void *req, gm_cont k)`, the continuation passed as its two words. The
/// request holds the node at 0 and the arguments at `offsets`.
pub fn gen_ctl_thunk(g: &mut Gen, func: FuncId, op: u32, tys: &[Ty], offsets: &[u64], ret: Ty) -> CResult<()> {
    let mut sig = Signature::new(g.cc);
    for _ in 0..3 {
        sig.params.push(ir::AbiParam::new(types::I64));
    }
    let cont = Ty::intern(TyKind::Cont(ret));
    let mut ptys = tys.to_vec();
    ptys.push(cont);
    let csig = g.sig_of(&ptys, Ty::never(), true, g.cc);
    let mut shell = Shell::new(g, func, sig);
    {
        let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
        let entry = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        let ps = b.block_params(entry).to_vec();
        let (req, k0, k1) = (ps[0], ps[1], ps[2]);
        let kslot = b.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 16, 3));
        let ka = b.ins().stack_addr(types::I64, kslot, 0);
        b.ins().store(mf(), k0, ka, 0);
        b.ins().store(mf(), k1, ka, 8);
        let n = b.ins().load(types::I64, mf(), req, 0);
        let env = b.ins().load(types::I64, mf(), n, 16);
        let hctx = b.ins().load(types::I64, mf(), n, 24);
        let clauses = b.ins().load(types::I64, mf(), n, 8);
        let code = b.ins().load(types::I64, mf(), clauses, (8 * op) as i32);
        let mut args = vec![env, hctx, n];
        for (t, off) in tys.iter().zip(offsets) {
            match g.rep(*t) {
                Rep::Zero => {}
                Rep::Mem { .. } => args.push(b.ins().iadd_imm_s(req, *off as i64)),
                r => {
                    let v = b.ins().load(r.ty().unwrap(), mf(), req, *off as i32);
                    args.push(v)
                }
            }
        }
        args.push(ka);
        let sr = b.import_signature(csig);
        b.ins().call_indirect(sr, code, &args);
        b.ins().return_(&[]);
        b.seal_all_blocks();
        b.finalize(g.module.target_config());
    }
    shell.finish(g, func, "ctl thunk")
}

/// Generates the start function of a fiber made by `@fiber` (§14.6): `void (gm_fiber *fb)`,
/// which calls the function pointer `fb->fn` with the argument at `fb->arg`.
pub fn gen_fiber_start(g: &mut Gen, func: FuncId, aty: Ty, hidden: bool) -> CResult<()> {
    let mut sig = Signature::new(g.cc);
    sig.params.push(ir::AbiParam::new(types::I64));
    let plain_sig = g.sig_of(&[aty], Ty::never(), false, g.cc);
    let hidden_sig = g.sig_of(&[aty], Ty::never(), true, g.cc);
    let mut shell = Shell::new(g, func, sig);
    {
        let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
        let entry = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        let fb = b.block_params(entry)[0];
        let code = b.ins().load(types::I64, mf(), fb, 136);
        let cx = b.ins().load(types::I64, mf(), fb, 152);
        let argp = b.ins().load(types::I64, mf(), fb, 144);
        let arg = match g.rep(aty) {
            Rep::Zero => None,
            Rep::Mem { .. } => Some(argp),
            r => Some(b.ins().load(r.ty().unwrap(), mf(), argp, 0)),
        };
        let untagged = b.ins().band_imm_s(code, -2);
        let plain_call = |b: &mut FunctionBuilder| {
            let sr = b.import_signature(plain_sig.clone());
            b.ins().call_indirect(sr, untagged, arg.as_slice());
        };
        if hidden {
            let tag = b.ins().band_imm_s(code, 1);
            let (p, h, j) = (b.create_block(), b.create_block(), b.create_block());
            b.ins().brif(tag, p, &[], h, &[]);
            b.switch_to_block(p);
            plain_call(&mut b);
            b.ins().jump(j, &[]);
            b.switch_to_block(h);
            let z = b.ins().iconst(types::I64, 0);
            let mut args = vec![z, cx, z];
            args.extend(arg);
            let sr = b.import_signature(hidden_sig);
            b.ins().call_indirect(sr, code, &args);
            b.ins().jump(j, &[]);
            b.switch_to_block(j);
        } else {
            plain_call(&mut b);
        }
        b.ins().return_(&[]);
        b.seal_all_blocks();
        b.finalize(g.module.target_config());
    }
    shell.finish(g, func, "fiber start")
}

/// Generates the clause thunk of a handler declaration's clause (§12.6): it has the uniform
/// clause signature, and calls the clause with `self`, which the node's `env` points at.
pub fn gen_decl_thunk(g: &mut Gen, func: FuncId, clause: FnId) -> CResult<()> {
    let prog = g.prog;
    let def = &prog.fns[clause.index()];
    let ptys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
    let sig = g.sig_of(&ptys[1..], def.ret, true, g.cc);
    let target = g.func(clause)?;
    let self_rep = g.rep(ptys[0]);
    let ret_mem = matches!(g.rep(def.ret), Rep::Mem { .. });
    let mut shell = Shell::new(g, func, sig);
    {
        let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
        let entry = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        let ps = b.block_params(entry).to_vec();
        let mut it = ps.into_iter();
        let mut args = vec![];
        if ret_mem {
            args.push(it.next().unwrap());
        }
        let (env, cx, inst) = (it.next().unwrap(), it.next().unwrap(), it.next().unwrap());
        let z = b.ins().iconst(types::I64, 0);
        args.extend([z, cx, inst]);
        match self_rep {
            Rep::Zero => {}
            Rep::Mem { .. } => args.push(env),
            r => args.push(b.ins().load(r.ty().unwrap(), mf(), env, 0)),
        }
        args.extend(it);
        let fr = g.module.declare_func_in_func(target, b.func);
        let call = b.ins().call(fr, &args);
        let rs = b.inst_results(call).to_vec();
        b.ins().return_(&rs);
        b.seal_all_blocks();
        b.finalize(g.module.target_config());
    }
    shell.finish(g, func, "handler declaration thunk")
}

/// Generates the entry function of `entry`: `void (gm_node *cx, void *out)`, which calls it
/// under handler context `cx` and stores its result at `out`.
pub fn gen_entry(g: &mut Gen, entry: FnId) -> CResult<FuncId> {
    let mut sig = Signature::new(g.cc);
    sig.params.push(ir::AbiParam::new(types::I64));
    sig.params.push(ir::AbiParam::new(types::I64));
    let id = g.local_fn("gm_entry", &sig)?;
    let target = g.func(entry)?;
    let plain = g.is_plain(entry);
    let ret = g.prog.fns[entry.index()].ret;
    let rep = g.rep(ret);
    let mut shell = Shell::new(g, id, sig);
    {
        let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
        let blk = b.create_block();
        b.append_block_params_for_function_params(blk);
        b.switch_to_block(blk);
        let ps = b.block_params(blk).to_vec();
        let (cx, out) = (ps[0], ps[1]);
        let mut args = vec![];
        if matches!(rep, Rep::Mem { .. }) {
            args.push(out);
        }
        if !plain {
            let z = b.ins().iconst(types::I64, 0);
            args.extend([z, cx, z]);
        }
        let fr = g.module.declare_func_in_func(target, b.func);
        let call = b.ins().call(fr, &args);
        if let Some(&r) = b.inst_results(call).first() {
            b.ins().store(mf(), r, out, 0);
        }
        b.ins().return_(&[]);
        b.seal_all_blocks();
        b.finalize(g.module.target_config());
    }
    shell.finish(g, id, "entry")?;
    Ok(id)
}

impl<'a> Gen<'a> {
    /// Queues the body of a `with … on` as a function of its own.
    pub fn queue_with_body(&mut self, fid: FnId, label: LabelId, ty: Ty, body: &'a Expr) -> CResult<FuncId> {
        let mut sig = Signature::new(self.cc);
        sig.params.push(ir::AbiParam::new(types::I64));
        let func = self.local_fn("gm_wb", &sig)?;
        self.queue.push_back(Work::WithBody { func, fid, label, ty, body });
        Ok(func)
    }
}

/// Generates the exported symbol `func` of plain function `f` whose C signature `cs`
/// passes aggregates differently from the plain convention: it gathers each aggregate
/// argument into memory, calls `f`, and returns the result as C expects it.
pub fn gen_export_thunk(g: &mut Gen, func: FuncId, f: FnId, cs: &CSig) -> CResult<()> {
    let target = g.func(f)?;
    let def = &g.prog.fns[f.index()];
    let tys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
    let ret = def.ret;
    let ret_size = g.size(ret);
    let mut shell = Shell::new(g, func, cs.sig.clone());
    {
        let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
        let entry = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        let ps = b.block_params(entry).to_vec();
        let mut it = ps.into_iter();
        let mut args = vec![];
        let dest = match &cs.ret {
            CRet::Sret => Some(it.next().unwrap()),
            CRet::Parts(parts) => Some(aligned_slot(&mut b, parts_size(parts).max(ret_size), 16)),
            _ => None,
        };
        args.extend(dest);
        for (a, t) in cs.args.iter().zip(&tys) {
            match a {
                CArg::None => {}
                CArg::Scalar | CArg::Stack(_) | CArg::Indirect => args.push(it.next().unwrap()),
                CArg::Parts { pad, parts } => {
                    for _ in pad {
                        it.next();
                    }
                    let tmp = aligned_slot(&mut b, parts_size(parts).max(g.size(*t)), 16);
                    for (_, off) in parts {
                        let v = it.next().unwrap();
                        b.ins().store(mf(), v, tmp, *off as i32);
                    }
                    args.push(tmp);
                }
            }
        }
        let fr = g.module.declare_func_in_func(target, b.func);
        let inst = b.ins().call(fr, &args);
        let rs = b.inst_results(inst).to_vec();
        match &cs.ret {
            CRet::Scalar => {
                b.ins().return_(&rs);
            }
            CRet::Parts(parts) => {
                let d = dest.unwrap();
                let vs: Vec<Value> = parts.iter().map(|(ty, off)| b.ins().load(*ty, mf(), d, *off as i32)).collect();
                b.ins().return_(&vs);
            }
            CRet::None | CRet::Sret => {
                b.ins().return_(&[]);
            }
        }
        b.seal_all_blocks();
        b.finalize(g.module.target_config());
    }
    shell.finish(g, func, "export thunk")
}

/// Generates `func`, the code of a second-class value made from a C-convention function
/// pointer (see `FnGen::c_abi_ptr`), which is the value's environment: it takes the
/// arguments of a call in the hidden convention and calls the pointer as C does, with
/// parameters `tys`, result `ret` and C signature `cs`.
pub fn gen_c_shim(g: &mut Gen, func: FuncId, tys: &[Ty], ret: Ty, cs: &CSig) -> CResult<()> {
    let sig = g.sig_of(tys, ret, true, g.cc);
    let memmove = g.rt("gm_clif_memmove", "ppl>p");
    let ret_mem = matches!(g.rep(ret), Rep::Mem { .. });
    let reps: Vec<Rep> = tys.iter().map(|t| g.rep(*t)).collect();
    let sizes: Vec<u64> = tys.iter().map(|t| g.size(*t)).collect();
    let ret_size = g.size(ret);
    let mut shell = Shell::new(g, func, sig);
    {
        let mut b = FunctionBuilder::new(&mut shell.func, &mut shell.fctx);
        let entry = b.create_block();
        b.append_block_params_for_function_params(entry);
        b.switch_to_block(entry);
        let mm = g.module.declare_func_in_func(memmove, b.func);
        // A copy the callee may change: C passes some aggregates by address.
        let copy = |b: &mut FunctionBuilder, src: Value, size: u64, room: u64| {
            let tmp = aligned_slot(b, room.max(size), 16);
            let n = b.ins().iconst(types::I64, size as i64);
            b.ins().call(mm, &[tmp, src, n]);
            tmp
        };
        let mut it = b.block_params(entry).to_vec().into_iter();
        let dest = if ret_mem { it.next() } else { None };
        let code = it.next().unwrap();
        it.next(); // the context
        it.next(); // the unused third hidden parameter
        let mut args = vec![];
        if cs.ret == CRet::Sret {
            args.push(dest.unwrap());
        }
        for ((a, rep), size) in cs.args.iter().zip(&reps).zip(&sizes) {
            let v = if *rep == Rep::Zero { None } else { it.next() };
            match a {
                CArg::None => {}
                CArg::Scalar => args.push(v.unwrap()),
                CArg::Parts { pad, parts } => {
                    for ty in pad {
                        args.push(b.ins().iconst(*ty, 0));
                    }
                    let tmp = copy(&mut b, v.unwrap(), *size, parts_size(parts));
                    for (ty, off) in parts {
                        args.push(b.ins().load(*ty, mf(), tmp, *off as i32));
                    }
                }
                CArg::Stack(n) => args.push(copy(&mut b, v.unwrap(), *size, *n as u64)),
                CArg::Indirect => args.push(copy(&mut b, v.unwrap(), *size, *size)),
            }
        }
        let sr = b.import_signature(cs.sig.clone());
        let inst = b.ins().call_indirect(sr, code, &args);
        let rs = b.inst_results(inst).to_vec();
        match &cs.ret {
            CRet::Scalar => {
                b.ins().return_(&rs);
            }
            CRet::Parts(parts) => {
                let tmp = aligned_slot(&mut b, parts_size(parts).max(ret_size), 16);
                for ((_, off), v) in parts.iter().zip(&rs) {
                    b.ins().store(mf(), *v, tmp, *off as i32);
                }
                let n = b.ins().iconst(types::I64, ret_size as i64);
                b.ins().call(mm, &[dest.unwrap(), tmp, n]);
                b.ins().return_(&[]);
            }
            CRet::None | CRet::Sret => {
                b.ins().return_(&[]);
            }
        }
        b.seal_all_blocks();
        b.finalize(g.module.target_config());
    }
    shell.finish(g, func, "C shim")
}
