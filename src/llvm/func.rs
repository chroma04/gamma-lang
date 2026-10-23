//! Lowering of one function body from TIR to LLVM IR.
//!
//! Every local lives in an `alloca` of the entry block, and every place is a pointer, as
//! clang emits C at `-O0`; LLVM's `mem2reg`/SROA turns them into SSA values when
//! optimizing. A lowered expression is `Some(operand)` (an empty operand for `()`), or
//! `None` when it does not complete (`return`, `break`, a call of a `never` function…), in
//! which case the current block has been terminated.

use super::{Gen, LResult, unsupported};
use crate::tir::{
    Arm, BinOp, Block, Builtin, Callee, Coercion, CondPart, Expr, ExprKind, Flavor, FnDef, FnId, FnKind, LabelId, Pat,
    PatKind, Stmt, Target, UnOp, WriteSink,
};
use crate::types::{CallConv, FloatTy, Ty, TyKind};
use num_bigint::BigInt;
use std::fmt::Write as _;

type Val = Option<String>;

/// Writes the definition of function `id` into `g.fns`.
pub fn define(g: &mut Gen, id: FnId) -> LResult<()> {
    let prog = g.prog;
    let def = &prog.fns[id.index()];
    match def.kind {
        FnKind::Named | FnKind::Literal if def.parent.is_none() => {}
        FnKind::Foreign { .. } => return unsupported(def.span, "calls of foreign functions"),
        _ => return unsupported(def.span, "capturing function literals and handler clauses"),
    }
    if def.callconv != CallConv::C {
        return unsupported(def.span, "a calling convention other than C's");
    }
    let Some(body) = &def.body else { return unsupported(def.span, "a function without a body") };
    let lookup = |id| prog.adts.get(&id);
    let layouts = crate::layout::Layouts::new(prog.target.clone(), &lookup);
    let cost = crate::tir::frame_cost(def, &mut |t| layouts.size_of(t));
    if cost > crate::tir::MAX_FRAME {
        // No stack can hold the frame: every call is a stack overflow, reported as the
        // interpreter reports it (an LLVM `alloca` that size would be optimized away).
        let ret = g.llty(def.ret, def.span)?;
        let n = g.bytes(def.name.as_str().as_bytes());
        let name = super::fn_name(id);
        let params: Vec<String> = def
            .params
            .iter()
            .filter_map(|p| g.llty(def.locals[p.index()].ty, def.span).ok().filter(|t| t != "void"))
            .enumerate()
            .map(|(i, t)| format!("{t} %p{i}"))
            .collect();
        let _ = writeln!(
            g.fns,
            "define internal {ret} @{name}({}) {{\n  call void @gm_frame_overflow(ptr {n}, i64 {cost})\n  unreachable\n}}\n",
            params.join(", ")
        );
        return Ok(());
    }
    let mut f = FnGen {
        g,
        def,
        allocas: String::new(),
        body: String::new(),
        tmps: 0,
        labels: 0,
        cur: "start".into(),
        done: false,
        loops: vec![],
        has_slot: vec![false; def.locals.len()],
    };
    let ret = f.g.llty(def.ret, def.span)?;
    let mut params = vec![];
    for (i, decl) in def.locals.iter().enumerate() {
        let t = f.g.llty(decl.ty, decl.span)?;
        if t == "void" {
            continue;
        }
        let _ = writeln!(f.allocas, "  %l{i} = alloca {t}");
        f.has_slot[i] = true;
    }
    for &p in &def.params {
        if f.has_slot[p.index()] {
            let t = f.g.llty(def.locals[p.index()].ty, def.span)?;
            params.push(format!("{t} %p{}", p.0));
            let _ = writeln!(f.allocas, "  store {t} %p{}, ptr %l{}", p.0, p.0);
        }
    }
    if f.checks() {
        // Running out of stack is checked (§18), as the other back ends check it.
        let lim = f.value("load i64, ptr @gm_stack_limit".into());
        let fa = f.g.intrinsic("ptr", "frameaddress.p0", "i32");
        let sp = f.value(format!("call ptr {fa}(i32 0)"));
        let spi = f.value(format!("ptrtoint ptr {sp} to i64"));
        let low = f.value(format!("icmp ult i64 {spi}, {lim}"));
        f.trap_if(&low, "call void @gm_stack_overflow()".into());
    }
    match f.expr(body)? {
        Some(_) if ret == "void" => f.term("ret void".into()),
        Some(v) => f.term(format!("ret {ret} {v}")),
        None if !f.done => f.term("unreachable".into()),
        None => {}
    }
    let mut attrs = vec![];
    match def.hints.inline {
        Some(true) => attrs.push("alwaysinline"),
        Some(false) => attrs.push("noinline"),
        None => {}
    }
    if def.hints.cold {
        attrs.push("cold");
    }
    let name = super::fn_name(id);
    let (allocas, body) = (f.allocas, f.body);
    let _ = writeln!(
        g.fns,
        "; {}\ndefine internal {ret} @{name}({}) {} {{\nentry:\n{allocas}  br label %start\nstart:\n{body}}}\n",
        def.name,
        params.join(", "),
        attrs.join(" ")
    );
    Ok(())
}

struct FnGen<'g, 'a> {
    g: &'g mut Gen<'a>,
    def: &'a FnDef,
    /// The entry block's `alloca`s, written before the body.
    allocas: String,
    body: String,
    tmps: u32,
    labels: u32,
    /// The label of the block being written.
    cur: String,
    /// The block being written is terminated: what follows is unreachable.
    done: bool,
    /// The enclosing loops: label, the `continue` block and the `break` block.
    loops: Vec<(LabelId, String, String)>,
    /// Which locals have an `alloca` (those of type `()` do not).
    has_slot: Vec<bool>,
}

impl FnGen<'_, '_> {
    // --- writing instructions and blocks ---------------------------------------------------------

    fn tmp(&mut self) -> String {
        self.tmps += 1;
        format!("%t{}", self.tmps)
    }

    fn label(&mut self) -> String {
        self.labels += 1;
        format!("b{}", self.labels)
    }

    /// Writes an instruction into the current block (a new, unreachable one if the current
    /// block is terminated).
    fn emit(&mut self, inst: String) {
        if self.done {
            let l = self.label();
            self.start(&l);
        }
        let _ = writeln!(self.body, "  {inst}");
    }

    /// Writes an instruction giving a value; returns the value's name.
    fn value(&mut self, inst: String) -> String {
        let t = self.tmp();
        self.emit(format!("{t} = {inst}"));
        t
    }

    /// Terminates the current block with `inst`.
    fn term(&mut self, inst: String) {
        self.emit(inst);
        self.done = true;
    }

    /// Branches to `l` unless the current block is already terminated.
    fn goto(&mut self, l: &str) {
        if !self.done {
            self.term(format!("br label %{l}"));
        }
    }

    /// Starts block `l`, falling through into it from the current block.
    fn start(&mut self, l: &str) {
        self.goto(l);
        let _ = writeln!(self.body, "{l}:");
        self.cur = l.to_string();
        self.done = false;
    }

    /// Calls the `noreturn` runtime function `call` (a full call instruction) when `cond`
    /// holds.
    fn trap_if(&mut self, cond: &str, call: String) {
        let (bad, ok) = (self.label(), self.label());
        self.term(format!("br i1 {cond}, label %{bad}, label %{ok}"));
        self.start(&bad);
        self.emit(call);
        self.term("unreachable".into());
        self.start(&ok);
    }

    /// Traps with the constant message `msg` at `span` when `cond` holds.
    fn trap_msg_if(&mut self, cond: &str, msg: &str, span: crate::source::Span) {
        let m = self.g.bytes(msg.as_bytes());
        let loc = self.g.loc(span);
        self.trap_if(cond, format!("call void @gm_trap(ptr {m}, {loc})"));
    }

    fn checks(&self) -> bool {
        self.g.opts.checks
    }

    fn llty(&self, ty: Ty, e: &Expr) -> LResult<String> {
        self.g.llty(ty, e.span)
    }

    // --- statements and blocks ---------------------------------------------------------------

    fn block(&mut self, b: &Block) -> LResult<Val> {
        for s in &b.stmts {
            if !self.stmt(s)? {
                return Ok(None);
            }
        }
        match &b.tail {
            Some(e) => self.expr(e),
            None => Ok(Some(String::new())),
        }
    }

    /// Lowers a statement; false if it does not complete.
    fn stmt(&mut self, s: &Stmt) -> LResult<bool> {
        match s {
            Stmt::Let { local, init } => {
                let Some(init) = init else { return Ok(true) };
                if !self.has_slot[local.index()] {
                    return Ok(self.expr(init)?.is_some());
                }
                self.store_into(&format!("%l{}", local.0), init)
            }
            Stmt::Assign { place, value } => {
                let Some(p) = self.place(place)? else { return Ok(false) };
                self.store_into(&p, value)
            }
            Stmt::Expr(e) => Ok(self.expr(e)?.is_some()),
            Stmt::LetTuple { init, .. } | Stmt::AssignTuple { value: init, .. } => {
                unsupported(init.span, "tuple destructuring")
            }
            Stmt::Defer { body, .. } => unsupported(body.span, "`defer`"),
        }
    }

    /// Stores the value of `e` at the pointer `p`; false if `e` does not complete.
    /// Array constructions are written element by element rather than built as values.
    fn store_into(&mut self, p: &str, e: &Expr) -> LResult<bool> {
        match &e.kind {
            ExprKind::Uninit => Ok(true),
            ExprKind::ArrayFill(x) => self.fill(p, e.ty, x),
            ExprKind::Array(elems) => {
                let at = self.llty(e.ty, e)?;
                for (i, x) in elems.iter().enumerate() {
                    let q = self.value(format!("getelementptr inbounds {at}, ptr {p}, i64 0, i64 {i}"));
                    if !self.store_into(&q, x)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            _ => {
                let t = self.llty(e.ty, e)?;
                let Some(v) = self.expr(e)? else { return Ok(false) };
                if t != "void" {
                    self.emit(format!("store {t} {v}, ptr {p}"));
                }
                Ok(true)
            }
        }
    }

    /// `[N]T(fill: x)` written at `p`: a `memset` when every byte is zero, otherwise a loop
    /// storing `x`, which is evaluated once.
    fn fill(&mut self, p: &str, ty: Ty, x: &Expr) -> LResult<bool> {
        let TyKind::Array { len, elem, .. } = *ty.kind() else { unreachable!("fill of a non-array") };
        if let Some(size) = self.zero_size(x) {
            let f = self.g.intrinsic("void", "memset.p0.i64", "ptr, i8, i64, i1");
            self.emit(format!("call void {f}(ptr {p}, i8 0, i64 {}, i1 false)", size * len));
            return Ok(true);
        }
        let at = self.g.llty(ty, x.span)?;
        let et = self.g.llty(elem, x.span)?;
        if et.starts_with('[') {
            return unsupported(x.span, "filling an array with a non-zero array");
        }
        let Some(v) = self.expr(x)? else { return Ok(false) };
        let (head, body, exit) = (self.label(), self.label(), self.label());
        let entry = self.cur.clone();
        self.start(&head);
        let i = self.tmp();
        let next = self.tmp();
        self.emit(format!("{i} = phi i64 [ 0, %{entry} ], [ {next}, %{body} ]"));
        let c = self.value(format!("icmp ult i64 {i}, {len}"));
        self.term(format!("br i1 {c}, label %{body}, label %{exit}"));
        self.start(&body);
        let q = self.value(format!("getelementptr inbounds {at}, ptr {p}, i64 0, i64 {i}"));
        self.emit(format!("store {et} {v}, ptr {q}"));
        self.emit(format!("{next} = add i64 {i}, 1"));
        self.term(format!("br label %{head}"));
        self.start(&exit);
        Ok(true)
    }

    /// The size in bytes of the value of `x` if it is all zero bytes (a zero scalar, or an
    /// array filled with one).
    fn zero_size(&self, x: &Expr) -> Option<u64> {
        match &x.kind {
            ExprKind::Int(v) if *v == BigInt::from(0) => Some(self.g.int(x.ty)?.1 as u64 / 8),
            ExprKind::Bool(false) => Some(1),
            ExprKind::Float(f) if f.to_bits() == 0 => match x.ty.kind() {
                TyKind::Float(FloatTy::F32) => Some(4),
                _ => Some(8),
            },
            ExprKind::ArrayFill(inner) => {
                let TyKind::Array { len, .. } = *x.ty.kind() else { return None };
                Some(self.zero_size(inner)? * len)
            }
            _ => None,
        }
    }

    // --- places ------------------------------------------------------------------------------

    /// The address of the place `e`.
    fn place(&mut self, e: &Expr) -> LResult<Val> {
        match &e.kind {
            ExprKind::Local(l) if self.has_slot[l.index()] => Ok(Some(format!("%l{}", l.0))),
            ExprKind::Deref(p) => self.expr(p),
            ExprKind::Index { base, index, checked } => {
                let TyKind::Array { len, .. } = *base.ty.kind() else {
                    return unsupported(e.span, format!("indexing a `{}`", base.ty));
                };
                let at = self.llty(base.ty, base)?;
                let Some(b) = self.place(base)? else { return Ok(None) };
                let Some(i) = self.expr(index)? else { return Ok(None) };
                let i = self.int_cast(&i, index.ty, Ty::usize())?;
                if *checked && self.checks() {
                    let bad = self.value(format!("icmp uge i64 {i}, {len}"));
                    let loc = self.g.loc(e.span);
                    self.trap_if(&bad, format!("call void @gm_ll_trap_index(i64 {i}, i64 {len}, {loc})"));
                }
                Ok(Some(self.value(format!("getelementptr inbounds {at}, ptr {b}, i64 0, i64 {i}"))))
            }
            _ => unsupported(e.span, format!("this kind of place ({})", kind_name(&e.kind))),
        }
    }

    /// The value at the place `e`.
    fn load(&mut self, e: &Expr) -> LResult<Val> {
        let t = self.llty(e.ty, e)?;
        let Some(p) = self.place(e)? else { return Ok(None) };
        if t == "void" {
            return Ok(Some(String::new()));
        }
        Ok(Some(self.value(format!("load {t}, ptr {p}"))))
    }

    // --- expressions -------------------------------------------------------------------------

    fn expr(&mut self, e: &Expr) -> LResult<Val> {
        Ok(Some(match &e.kind {
            ExprKind::Int(v) => {
                let bits = self.g.int(e.ty).map_or(64, |(_, b)| b);
                int_lit(v, bits)
            }
            ExprKind::Float(f) => format!("0x{:016X}", f.to_bits()),
            ExprKind::Bool(b) => b.to_string(),
            ExprKind::Uninit => {
                if self.llty(e.ty, e)? == "void" {
                    String::new()
                } else {
                    "zeroinitializer".into()
                }
            }
            ExprKind::Aggregate(xs) if xs.is_empty() => String::new(),
            ExprKind::Const(id) => self.constant(*id, e)?,
            ExprKind::Local(_) | ExprKind::Deref(_) | ExprKind::Index { .. } => return self.load(e),
            ExprKind::AddrOf(p) => return self.place(p),
            ExprKind::Unary { op, operand } => return self.unary(*op, operand, e),
            ExprKind::Binary { op, lhs, rhs } => return self.binary(*op, lhs, rhs, e),
            ExprKind::Logic { is_and, lhs, rhs } => return self.logic(*is_and, lhs, rhs),
            ExprKind::Eq { lhs, rhs, negated } => return self.eq(lhs, rhs, *negated),
            ExprKind::Coerce { kind, expr } => return self.coerce(*kind, expr, e),
            ExprKind::Builtin { op, args } => return self.builtin(*op, args, e),
            ExprKind::Call { callee: Callee::Fn(id), args } => return self.call(*id, args, e),
            ExprKind::Block(b) => return self.block(b),
            ExprKind::If { cond, then, else_ } => return self.if_(cond, then, else_.as_deref(), e),
            ExprKind::Match { scrutinee, arms } => return self.match_(scrutinee, arms, e),
            ExprKind::Loop { label, cond, body, step } => return self.loop_(*label, cond, body, step.as_deref()),
            ExprKind::Break { target, value } => {
                if let Some(v) = value {
                    return unsupported(v.span, "`break` with a value");
                }
                let (_, _, exit) = self.loop_target(target, e)?;
                self.term(format!("br label %{exit}"));
                return Ok(None);
            }
            ExprKind::Continue { target } => {
                let (_, step, _) = self.loop_target(target, e)?;
                self.term(format!("br label %{step}"));
                return Ok(None);
            }
            ExprKind::Return { levels: 0, value } => {
                let t = self.llty(value.ty, value)?;
                let Some(v) = self.expr(value)? else { return Ok(None) };
                let ret = self.g.llty(self.def.ret, e.span)?;
                if ret == "void" {
                    self.term("ret void".into());
                } else if t == "void" {
                    return unsupported(e.span, "returning `never`");
                } else {
                    self.term(format!("ret {ret} {v}"));
                }
                return Ok(None);
            }
            ExprKind::Write { sink, pieces, specs, args } => return self.write(sink, pieces, specs, args, e),
            ExprKind::Trap { message } => {
                let ExprKind::Str(msg) = &message.kind else {
                    return unsupported(message.span, "a trap message that is not a string literal");
                };
                if msg.contains(&0) {
                    return unsupported(message.span, "a trap message holding a NUL byte");
                }
                let m = self.g.bytes(msg);
                let loc = self.g.loc(e.span);
                self.emit(format!("call void @gm_trap(ptr {m}, {loc})"));
                self.term("unreachable".into());
                return Ok(None);
            }
            k => return unsupported(e.span, kind_name(k)),
        }))
    }

    /// A scalar top-level constant.
    fn constant(&mut self, id: crate::tir::ConstId, e: &Expr) -> LResult<String> {
        let c = &self.g.prog.consts[id.index()];
        let Some(v) = c.value.as_ref().filter(|v| v.relocs.is_empty()) else {
            return unsupported(e.span, "a constant holding pointers");
        };
        let b = &v.bytes;
        let mut word = [0u8; 8];
        let n = b.len().min(8);
        word[..n].copy_from_slice(&b[..n]);
        let raw = u64::from_le_bytes(word);
        Ok(match e.ty.kind() {
            TyKind::Bool => (raw != 0).to_string(),
            TyKind::Float(FloatTy::F64) => format!("0x{raw:016X}"),
            TyKind::Float(FloatTy::F32) => format!("0x{:016X}", (f32::from_bits(raw as u32) as f64).to_bits()),
            _ => match self.g.int(e.ty) {
                Some((_, bits)) if bits <= 64 => int_lit(&BigInt::from(raw), bits),
                _ => return unsupported(e.span, format!("a constant of type `{}`", e.ty)),
            },
        })
    }

    fn call(&mut self, id: FnId, args: &[Expr], e: &Expr) -> LResult<Val> {
        let name = self.g.func(id);
        let mut vals = vec![];
        for a in args {
            let t = self.llty(a.ty, a)?;
            let Some(v) = self.expr(a)? else { return Ok(None) };
            if t != "void" {
                vals.push(format!("{t} {v}"));
            }
        }
        let ret = self.llty(e.ty, e)?;
        let call = format!("call {ret} @{name}({})", vals.join(", "));
        if e.ty.is_never() {
            self.emit(call);
            self.term("unreachable".into());
            return Ok(None);
        }
        if ret == "void" {
            self.emit(call);
            return Ok(Some(String::new()));
        }
        Ok(Some(self.value(call)))
    }

    fn if_(&mut self, cond: &[CondPart], then: &Expr, else_: Option<&Expr>, e: &Expr) -> LResult<Val> {
        let (else_l, end) = (self.label(), self.label());
        for part in cond {
            let CondPart::Bool(c) = part else { return unsupported(e.span, "pattern matching in `if`") };
            let Some(v) = self.expr(c)? else { return Ok(None) };
            let next = self.label();
            self.term(format!("br i1 {v}, label %{next}, label %{else_l}"));
            self.start(&next);
        }
        let tv = self.expr(then)?;
        let then_end = self.cur.clone();
        self.goto(&end);
        self.start(&else_l);
        let ev = match else_ {
            Some(x) => self.expr(x)?,
            None => Some(String::new()),
        };
        let else_end = self.cur.clone();
        self.goto(&end);
        if tv.is_none() && ev.is_none() {
            return Ok(None);
        }
        self.start(&end);
        let t = self.llty(e.ty, e)?;
        Ok(Some(match (tv, ev) {
            _ if t == "void" => String::new(),
            (Some(a), Some(b)) => self.value(format!("phi {t} [ {a}, %{then_end} ], [ {b}, %{else_end} ]")),
            (Some(a), None) | (None, Some(a)) => a,
            (None, None) => unreachable!(),
        }))
    }

    /// `match` on a scalar; also how `for` binds its pattern. The arms are tried in order;
    /// the checker made them exhaustive.
    fn match_(&mut self, scrut: &Expr, arms: &[Arm], e: &Expr) -> LResult<Val> {
        let Some(v) = self.expr(scrut)? else { return Ok(None) };
        let end = self.label();
        let mut incoming = vec![];
        for arm in arms {
            let next = self.label();
            self.pat(&arm.pat, &v, &next)?;
            if let Some(g) = &arm.guard {
                let Some(c) = self.expr(g)? else {
                    self.start(&next);
                    continue;
                };
                let ok = self.label();
                self.term(format!("br i1 {c}, label %{ok}, label %{next}"));
                self.start(&ok);
            }
            if let Some(b) = self.expr(&arm.body)? {
                incoming.push((b, self.cur.clone()));
                self.goto(&end);
            }
            self.start(&next);
        }
        self.term("unreachable".into());
        if incoming.is_empty() {
            return Ok(None);
        }
        self.start(&end);
        let t = self.llty(e.ty, e)?;
        if t == "void" {
            return Ok(Some(String::new()));
        }
        if incoming.len() == 1 {
            return Ok(Some(incoming.pop().unwrap().0));
        }
        let ins: Vec<String> = incoming.iter().map(|(v, b)| format!("[ {v}, %{b} ]")).collect();
        Ok(Some(self.value(format!("phi {t} {}", ins.join(", ")))))
    }

    /// Tests the value `v` against `p`, binding its locals, and branches to `fail` if it
    /// does not match; continues in a new block if it does.
    fn pat(&mut self, p: &Pat, v: &str, fail: &str) -> LResult<()> {
        let t = self.g.llty(p.ty, p.span)?;
        let cond = match &p.kind {
            PatKind::Wild => return Ok(()),
            PatKind::Bind(l) => {
                if self.has_slot[l.index()] {
                    self.emit(format!("store {t} {v}, ptr %l{}", l.0));
                }
                return Ok(());
            }
            PatKind::Const(c) => {
                let Some(cv) = self.expr(c)? else { return Ok(()) };
                if matches!(p.ty.kind(), TyKind::Float(_)) {
                    self.value(format!("fcmp oeq {t} {v}, {cv}"))
                } else if t.starts_with('i') {
                    self.value(format!("icmp eq {t} {v}, {cv}"))
                } else {
                    return unsupported(p.span, format!("a constant pattern of type `{}`", p.ty));
                }
            }
            PatKind::Range { lo, hi, inclusive } => {
                let Some((signed, bits)) = self.g.int(p.ty) else {
                    return unsupported(p.span, "a range pattern on a non-integer");
                };
                let s = if signed { 's' } else { 'u' };
                let a = self.value(format!("icmp {s}ge {t} {v}, {}", int_lit(lo, bits)));
                let cc = if *inclusive { "le" } else { "lt" };
                let b = self.value(format!("icmp {s}{cc} {t} {v}, {}", int_lit(hi, bits)));
                self.value(format!("and i1 {a}, {b}"))
            }
            PatKind::Or(alts) => {
                let ok = self.label();
                for alt in alts {
                    let next = self.label();
                    self.pat(alt, v, &next)?;
                    self.goto(&ok);
                    self.start(&next);
                }
                self.term(format!("br label %{fail}"));
                self.start(&ok);
                return Ok(());
            }
            _ => return unsupported(p.span, "patterns on optionals, results, unions and structs"),
        };
        let ok = self.label();
        self.term(format!("br i1 {cond}, label %{ok}, label %{fail}"));
        self.start(&ok);
        Ok(())
    }

    fn loop_(&mut self, label: LabelId, cond: &[CondPart], body: &Block, step: Option<&Expr>) -> LResult<Val> {
        let (head, step_l, exit) = (self.label(), self.label(), self.label());
        self.start(&head);
        for part in cond {
            let CondPart::Bool(c) = part else { return unsupported(body.span, "pattern matching in a loop condition") };
            let Some(v) = self.expr(c)? else { return Ok(None) };
            let next = self.label();
            self.term(format!("br i1 {v}, label %{next}, label %{exit}"));
            self.start(&next);
        }
        self.loops.push((label, step_l.clone(), exit.clone()));
        self.block(body)?;
        self.loops.pop();
        self.start(&step_l);
        if let Some(s) = step {
            self.expr(s)?;
        }
        self.goto(&head);
        self.start(&exit);
        Ok(Some(String::new()))
    }

    fn loop_target(&self, target: &Target, e: &Expr) -> LResult<(LabelId, String, String)> {
        if let Target::Label { levels: 0, label } = target
            && let Some(l) = self.loops.iter().rev().find(|l| l.0 == *label)
        {
            return Ok(l.clone());
        }
        unsupported(e.span, "`break` or `continue` out of a `with` or a function literal")
    }

    fn logic(&mut self, is_and: bool, lhs: &Expr, rhs: &Expr) -> LResult<Val> {
        let Some(a) = self.expr(lhs)? else { return Ok(None) };
        let a_end = self.cur.clone();
        let (rhs_l, end) = (self.label(), self.label());
        if is_and {
            self.term(format!("br i1 {a}, label %{rhs_l}, label %{end}"));
        } else {
            self.term(format!("br i1 {a}, label %{end}, label %{rhs_l}"));
        }
        self.start(&rhs_l);
        let b = self.expr(rhs)?;
        let b_end = self.cur.clone();
        self.goto(&end);
        self.start(&end);
        let short = if is_and { "false" } else { "true" };
        Ok(Some(match b {
            Some(b) => self.value(format!("phi i1 [ {short}, %{a_end} ], [ {b}, %{b_end} ]")),
            None => short.into(),
        }))
    }

    // --- operators ---------------------------------------------------------------------------

    fn unary(&mut self, op: UnOp, x: &Expr, e: &Expr) -> LResult<Val> {
        let t = self.llty(x.ty, x)?;
        let Some(v) = self.expr(x)? else { return Ok(None) };
        Ok(Some(match op {
            UnOp::Not => self.value(format!("xor i1 {v}, true")),
            UnOp::BitNot => self.value(format!("xor {t} {v}, -1")),
            UnOp::Neg if matches!(x.ty.kind(), TyKind::Float(_)) => self.value(format!("fneg {t} {v}")),
            UnOp::Neg => {
                let (signed, _) = self.g.int(x.ty).expect("negating a non-number");
                if self.checks() {
                    let s = if signed { 's' } else { 'u' };
                    let f = self.g.intrinsic(
                        &format!("{{{t}, i1}}"),
                        &format!("{s}sub.with.overflow.{t}"),
                        &format!("{t}, {t}"),
                    );
                    let r = self.value(format!("call {{{t}, i1}} {f}({t} 0, {t} {v})"));
                    let o = self.value(format!("extractvalue {{{t}, i1}} {r}, 1"));
                    self.trap_msg_if(&o, "integer overflow in `-`", e.span);
                    self.value(format!("extractvalue {{{t}, i1}} {r}, 0"))
                } else {
                    self.value(format!("sub {t} 0, {v}"))
                }
            }
        }))
    }

    fn binary(&mut self, op: BinOp, lhs: &Expr, rhs: &Expr, e: &Expr) -> LResult<Val> {
        let t = self.llty(lhs.ty, lhs)?;
        let Some(a) = self.expr(lhs)? else { return Ok(None) };
        let Some(b) = self.expr(rhs)? else { return Ok(None) };
        if matches!(lhs.ty.kind(), TyKind::Float(_)) {
            let inst = match op {
                BinOp::Add(_) => "fadd",
                BinOp::Sub(_) => "fsub",
                BinOp::Mul(_) => "fmul",
                BinOp::Div => "fdiv",
                BinOp::Rem => "frem",
                BinOp::Lt => "fcmp olt",
                BinOp::Le => "fcmp ole",
                BinOp::Gt => "fcmp ogt",
                BinOp::Ge => "fcmp oge",
                _ => return unsupported(e.span, format!("`{}` on floats", op_symbol(op))),
            };
            let fm = if self.def.hints.fast_math && !inst.starts_with("fcmp") { " fast" } else { "" };
            return Ok(Some(self.value(format!("{inst}{fm} {t} {a}, {b}"))));
        }
        let signed = self.g.int(lhs.ty).is_some_and(|(s, _)| s);
        let bits = self.g.int(lhs.ty).map_or(1, |(_, b)| b);
        let s = if signed { 's' } else { 'u' };
        Ok(Some(match op {
            BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                let cc = match op {
                    BinOp::Lt => "lt",
                    BinOp::Le => "le",
                    BinOp::Gt => "gt",
                    _ => "ge",
                };
                self.value(format!("icmp {s}{cc} {t} {a}, {b}"))
            }
            BinOp::BitAnd => self.value(format!("and {t} {a}, {b}")),
            BinOp::BitOr => self.value(format!("or {t} {a}, {b}")),
            BinOp::BitXor => self.value(format!("xor {t} {a}, {b}")),
            BinOp::Add(fl) | BinOp::Sub(fl) | BinOp::Mul(fl) => {
                let name = match op {
                    BinOp::Add(_) => "add",
                    BinOp::Sub(_) => "sub",
                    _ => "mul",
                };
                match fl {
                    Flavor::Checked if self.checks() => {
                        let f = self.g.intrinsic(
                            &format!("{{{t}, i1}}"),
                            &format!("{s}{name}.with.overflow.{t}"),
                            &format!("{t}, {t}"),
                        );
                        let r = self.value(format!("call {{{t}, i1}} {f}({t} {a}, {t} {b})"));
                        let o = self.value(format!("extractvalue {{{t}, i1}} {r}, 1"));
                        self.trap_msg_if(&o, &format!("integer overflow in `{}`", op_symbol(op)), e.span);
                        self.value(format!("extractvalue {{{t}, i1}} {r}, 0"))
                    }
                    Flavor::Checked | Flavor::Wrap => self.value(format!("{name} {t} {a}, {b}")),
                    Flavor::Sat if name != "mul" => {
                        let f = self.g.intrinsic(&t, &format!("{s}{name}.sat.{t}"), &format!("{t}, {t}"));
                        self.value(format!("call {t} {f}({t} {a}, {t} {b})"))
                    }
                    _ => return unsupported(e.span, format!("`{}` with this overflow flavor", op_symbol(op))),
                }
            }
            BinOp::Div | BinOp::Rem => {
                // Never removed, even in Fast builds (§18.1).
                let zero = self.value(format!("icmp eq {t} {b}, 0"));
                self.trap_msg_if(&zero, "division by zero", e.span);
                if signed {
                    let min = int_lit(&-(BigInt::from(1) << (bits - 1)), bits);
                    let x = self.value(format!("icmp eq {t} {a}, {min}"));
                    let y = self.value(format!("icmp eq {t} {b}, -1"));
                    let o = self.value(format!("and i1 {x}, {y}"));
                    self.trap_msg_if(&o, &format!("integer overflow in `{}`", op_symbol(op)), e.span);
                }
                let inst = if op == BinOp::Div { "div" } else { "rem" };
                self.value(format!("{s}{inst} {t} {a}, {b}"))
            }
            BinOp::Shl(_) | BinOp::Shr => {
                let fl = if let BinOp::Shl(f) = op { f } else { Flavor::Checked };
                if matches!(fl, Flavor::Sat | Flavor::Opt) {
                    return unsupported(e.span, "`<<` with this overflow flavor");
                }
                let nt = self.llty(rhs.ty, rhs)?;
                let n = if self.checks() {
                    let big = self.value(format!("icmp uge {nt} {b}, {bits}"));
                    let n64 = self.int_cast(&b, rhs.ty, Ty::u64())?;
                    let name = self.g.bytes(lhs.ty.to_string().as_bytes());
                    let loc = self.g.loc(e.span);
                    self.trap_if(&big, format!("call void @gm_ll_trap_shift(i64 {n64}, ptr {name}, {loc})"));
                    b
                } else {
                    self.value(format!("urem {nt} {b}, {bits}"))
                };
                let n = self.int_cast(&n, rhs.ty, unsigned_of(bits))?;
                if op == BinOp::Shr {
                    let inst = if signed { "ashr" } else { "lshr" };
                    return Ok(Some(self.value(format!("{inst} {t} {a}, {n}"))));
                }
                let r = self.value(format!("shl {t} {a}, {n}"));
                if fl == Flavor::Checked && self.checks() {
                    let back = self.value(format!("{} {t} {r}, {n}", if signed { "ashr" } else { "lshr" }));
                    let o = self.value(format!("icmp ne {t} {back}, {a}"));
                    self.trap_msg_if(&o, "integer overflow in `<<`", e.span);
                }
                r
            }
        }))
    }

    fn eq(&mut self, lhs: &Expr, rhs: &Expr, negated: bool) -> LResult<Val> {
        let t = self.llty(lhs.ty, lhs)?;
        if t.starts_with('[') {
            return unsupported(lhs.span, "`is` on arrays");
        }
        let Some(a) = self.expr(lhs)? else { return Ok(None) };
        let Some(b) = self.expr(rhs)? else { return Ok(None) };
        let cmp = match (matches!(lhs.ty.kind(), TyKind::Float(_)), negated) {
            (true, false) => "fcmp oeq",
            (true, true) => "fcmp une",
            (false, false) => "icmp eq",
            (false, true) => "icmp ne",
        };
        Ok(Some(self.value(format!("{cmp} {t} {a}, {b}"))))
    }

    fn coerce(&mut self, kind: Coercion, x: &Expr, e: &Expr) -> LResult<Val> {
        match kind {
            Coercion::FromNever => {
                if self.expr(x)?.is_some() {
                    self.term("unreachable".into());
                }
                Ok(None)
            }
            Coercion::TupleRename | Coercion::PtrToPtr => self.expr(x),
            Coercion::IntWiden => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                Ok(Some(self.int_cast(&v, x.ty, e.ty)?))
            }
            Coercion::FloatWiden => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                self.float_cast(&v, x, e)
            }
            _ => unsupported(e.span, format!("the coercion {kind:?}")),
        }
    }

    /// Converts the integer (or `bool`) `v` of type `from` to the integer type `to`:
    /// extended by `from`'s signedness or truncated.
    fn int_cast(&mut self, v: &str, from: Ty, to: Ty) -> LResult<String> {
        let (fs, fb) = if from.is_bool() { (false, 1) } else { self.g.int(from).expect("an integer") };
        let (_, tb) = self.g.int(to).expect("an integer");
        Ok(if fb == tb {
            v.to_string()
        } else if fb < tb {
            let fty = if fb == 1 { "i1".to_string() } else { format!("i{fb}") };
            self.value(format!("{}ext {fty} {v} to i{tb}", if fs { 's' } else { 'z' }))
        } else {
            self.value(format!("trunc i{fb} {v} to i{tb}"))
        })
    }

    fn float_cast(&mut self, v: &str, x: &Expr, e: &Expr) -> LResult<Val> {
        let (ft, tt) = (self.llty(x.ty, x)?, self.llty(e.ty, e)?);
        Ok(Some(match (ft.as_str(), tt.as_str()) {
            (a, b) if a == b => v.to_string(),
            ("float", "double") => self.value(format!("fpext float {v} to double")),
            _ => return unsupported(e.span, format!("converting `{}` to `{}`", x.ty, e.ty)),
        }))
    }

    fn builtin(&mut self, op: Builtin, args: &[Expr], e: &Expr) -> LResult<Val> {
        let Some(x) = args.first() else { return unsupported(e.span, format!("the builtin {op:?}")) };
        let t = self.llty(e.ty, e)?;
        let xt = self.llty(x.ty, x)?;
        let is_float = |ty: Ty| matches!(ty.kind(), TyKind::Float(_));
        let is_int = |g: &Gen, ty: Ty| g.int(ty).is_some() || ty.is_bool();
        match op {
            Builtin::Convert | Builtin::Trunc if is_int(self.g, x.ty) && self.g.int(e.ty).is_some() => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                Ok(Some(self.int_cast(&v, x.ty, e.ty)?))
            }
            Builtin::Convert | Builtin::Round if self.g.int(x.ty).is_some() && is_float(e.ty) => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                let s = if self.g.int(x.ty).unwrap().0 { "si" } else { "ui" };
                Ok(Some(self.value(format!("{s}tofp {xt} {v} to {t}"))))
            }
            Builtin::Convert if is_float(x.ty) && is_float(e.ty) => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                self.float_cast(&v, x, e)
            }
            Builtin::Bits
                if xt == t
                    || matches!(
                        (xt.as_str(), t.as_str()),
                        ("double", "i64") | ("i64", "double") | ("float", "i32") | ("i32", "float")
                    ) =>
            {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                Ok(Some(if xt == t { v } else { self.value(format!("bitcast {xt} {v} to {t}")) }))
            }
            Builtin::Min | Builtin::Max if self.g.int(e.ty).is_some() => {
                let Some(a) = self.expr(x)? else { return Ok(None) };
                let Some(b) = self.expr(&args[1])? else { return Ok(None) };
                let s = if self.g.int(e.ty).unwrap().0 { 's' } else { 'u' };
                let m = if op == Builtin::Min { "min" } else { "max" };
                let f = self.g.intrinsic(&t, &format!("{s}{m}.{t}"), &format!("{t}, {t}"));
                Ok(Some(self.value(format!("call {t} {f}({t} {a}, {t} {b})"))))
            }
            Builtin::Abs if self.g.int(e.ty).is_some_and(|(s, _)| s) => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                let bits = self.g.int(e.ty).unwrap().1;
                if self.checks() {
                    let min = int_lit(&-(BigInt::from(1) << (bits - 1)), bits);
                    let o = self.value(format!("icmp eq {t} {v}, {min}"));
                    self.trap_msg_if(&o, "integer overflow in `abs`", e.span);
                }
                let f = self.g.intrinsic(&t, &format!("abs.{t}"), &format!("{t}, i1"));
                Ok(Some(self.value(format!("call {t} {f}({t} {v}, i1 false)"))))
            }
            Builtin::Abs if is_float(e.ty) => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                let f = self.g.intrinsic(&t, &format!("fabs.{}", float_suffix(&t)), &t);
                Ok(Some(self.value(format!("call {t} {f}({t} {v})"))))
            }
            Builtin::Sqrt if is_float(e.ty) => {
                let Some(v) = self.expr(x)? else { return Ok(None) };
                let f = self.g.intrinsic(&t, &format!("sqrt.{}", float_suffix(&t)), &t);
                Ok(Some(self.value(format!("call {t} {f}({t} {v})"))))
            }
            Builtin::BlackBox => {
                // Through memory the optimizer cannot see into, whatever the type.
                let Some(v) = self.expr(x)? else { return Ok(None) };
                if t == "void" {
                    return Ok(Some(v));
                }
                let slot = self.tmp();
                let _ = writeln!(self.allocas, "  {slot} = alloca {t}");
                self.emit(format!("store {t} {v}, ptr {slot}"));
                self.emit(format!("call void asm sideeffect \"\", \"r,~{{memory}}\"(ptr {slot})"));
                Ok(Some(self.value(format!("load {t}, ptr {slot}"))))
            }
            _ => unsupported(e.span, format!("the builtin {op:?} on `{}`", x.ty)),
        }
    }

    // --- output ------------------------------------------------------------------------------

    /// `print`: the pieces and formatted arguments go straight to the runtime's buffered
    /// standard output, as the ambient `Out` handler would write them (no `with` can
    /// intercept them: this back end does not support handlers).
    fn write(
        &mut self,
        sink: &WriteSink,
        pieces: &[std::rc::Rc<[u8]>],
        specs: &[crate::tir::FmtSpec],
        args: &[Expr],
        e: &Expr,
    ) -> LResult<Val> {
        let out = self.g.prog.ambient.out;
        if !matches!(sink, WriteSink::Perform { effect, op: 0 } if Some(effect.effect) == out) {
            return unsupported(e.span, "writing elsewhere than to the ambient `Out`");
        }
        if specs.iter().any(|s| !s.is_default()) {
            return unsupported(e.span, "format specs other than `{}`");
        }
        // The arguments are evaluated before anything is written.
        let mut vals = vec![];
        for a in args {
            if matches!(a.kind, ExprKind::Str(_)) {
                vals.push(String::new());
                continue;
            }
            if self.g.int(a.ty).is_none() && !matches!(a.ty.kind(), TyKind::Bool | TyKind::Float(_)) {
                return unsupported(a.span, format!("printing a `{}`", a.ty));
            }
            let Some(v) = self.expr(a)? else { return Ok(None) };
            vals.push(v);
        }
        for (i, piece) in pieces.iter().enumerate() {
            if !piece.is_empty() {
                self.write_bytes(piece);
            }
            let Some(a) = args.get(i) else { break };
            let v = &vals[i];
            match (&a.kind, a.ty.kind()) {
                (ExprKind::Str(s), _) => self.write_bytes(s),
                (_, TyKind::Bool) => {
                    let b = self.value(format!("zext i1 {v} to i8"));
                    self.emit(format!("call void @gm_ll_write_bool(i8 {b})"));
                }
                (_, TyKind::Float(FloatTy::F64)) => self.emit(format!("call void @gm_ll_write_f64(double {v})")),
                (_, TyKind::Float(FloatTy::F32)) => self.emit(format!("call void @gm_ll_write_f32(float {v})")),
                _ => {
                    let (signed, bits) = self.g.int(a.ty).expect("checked above");
                    let w = self.int_cast(v, a.ty, if signed { Ty::int(true, 64) } else { Ty::u64() })?;
                    self.emit(format!("call void @gm_ll_write_int(i64 {w}, i32 {bits}, i32 {})", signed as u8));
                }
            }
        }
        Ok(Some(String::new()))
    }

    fn write_bytes(&mut self, bytes: &[u8]) {
        let s = self.g.bytes(bytes);
        self.emit(format!("call void @gm_write_out(ptr {s}, i64 {})", bytes.len()));
    }
}

/// The literal of the integer `v` in an `i{bits}`, which LLVM writes signed.
fn int_lit(v: &BigInt, bits: u32) -> String {
    let m = BigInt::from(1) << bits;
    let mut x = ((v % &m) + &m) % &m;
    if x >= (&m >> 1) {
        x -= &m;
    }
    x.to_string()
}

fn unsigned_of(bits: u32) -> Ty {
    Ty::int(false, bits as u16)
}

fn float_suffix(t: &str) -> &'static str {
    if t == "float" { "f32" } else { "f64" }
}

fn op_symbol(op: BinOp) -> &'static str {
    match op {
        BinOp::Add(_) => "+",
        BinOp::Sub(_) => "-",
        BinOp::Mul(_) => "*",
        BinOp::Shl(_) => "<<",
        BinOp::Div => "/",
        BinOp::Rem => "%",
        BinOp::Shr => ">>",
        BinOp::BitAnd => "&",
        BinOp::BitOr => "|",
        BinOp::BitXor => "~",
        BinOp::Lt => "<",
        BinOp::Le => "<=",
        BinOp::Gt => ">",
        BinOp::Ge => ">=",
    }
}

/// The variant name of an expression kind, for error messages.
fn kind_name(k: &ExprKind) -> String {
    let dbg = format!("{k:?}");
    let name: String = dbg.chars().take_while(|c| c.is_alphanumeric()).collect();
    format!("the expression kind `{name}`")
}
