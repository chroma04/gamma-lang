//! Translation of a function body to [`Code`]: where locals live, and expressions.
//!
//! Statements, blocks, patterns and control flow are in `compile_flow.rs`.

use super::*;
use crate::interp::eval::is_place_expr;
use crate::layout;
use std::collections::HashSet;

/// The compiler met something it does not translate (at this line of the compiler);
/// the function runs on the tree-walker.
#[derive(Debug)]
pub struct Unsupported(pub &'static str, pub u32);

macro_rules! unsupported {
    () => {
        Unsupported(file!(), line!())
    };
}
pub(crate) use unsupported;

pub type C<T = ()> = Result<T, Unsupported>;

/// The largest local, in bytes, kept in registers.
const REG_LOCAL_MAX: usize = 64;

/// The largest value compiled code holds in registers; a function with larger
/// temporaries runs on the tree-walker.
const REG_VALUE_MAX: u32 = 1 << 16;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LocalLoc {
    Reg {
        reg: Reg,
        size: u32,
    },
    /// In the frame's slots (data).
    Frame,
    /// In the frame's slots (a second-class value).
    Val,
}

/// A place once its address is computed.
#[derive(Clone, Copy, Debug)]
pub enum PlaceC {
    Mem { addr: Reg, packed: bool },
    Bits { addr: Reg, bits: BitsPlace },
}

/// A loop or `with` that `break` (and, for loops, `continue`) can reach by a jump.
pub struct LabelCx {
    pub label: LabelId,
    pub brk: u32,
    pub cont: u32,
    pub result: Arg,
    /// The handler context depth outside it.
    pub depth: u32,
    /// The deferred statements and temporary marks active outside it.
    pub defers: usize,
    pub temps: usize,
    pub is_loop: bool,
}

/// A deferred statement of an enclosing block.
pub struct DeferCx<'a> {
    pub is_err: bool,
    pub body: &'a Expr,
    pub depth: u32,
    /// Registers below this were in use when it was registered.
    pub regbase: Reg,
    pub vregbase: Reg,
    /// The first instruction it covers.
    pub lo: u32,
}

/// The code of a deferred statement to emit after the function's body.
pub struct Segment<'a> {
    pub label: u32,
    pub body: &'a Expr,
    pub depth: u32,
    pub regbase: Reg,
    pub vregbase: Reg,
}

/// What a `return` knows about whether its value is an error result (which decides
/// whether `errdefer`s run).
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum ErrTest {
    Dynamic,
    Yes,
    No,
}

pub struct Compiler<'m, 'a> {
    pub m: &'m Machine<'a>,
    pub prog: &'a Program,
    pub f: &'a FnDef,
    pub code: Code<'a>,
    /// The next free word register and the highest used.
    pub next: Reg,
    pub high: Reg,
    pub vnext: Reg,
    pub vhigh: Reg,
    pub locals: Vec<LocalLoc>,
    /// Registers below this belong to locals.
    pub locals_end: Reg,
    /// Jump labels: their instruction, once placed.
    pub targets: Vec<u32>,
    pub labels: Vec<LabelCx>,
    pub defers: Vec<DeferCx<'a>>,
    /// The mark registers of the enclosing statements, and whether a jump out of the
    /// statement needs its mark.
    pub temps: Vec<(Reg, Vec<usize>)>,
    /// While compiling a deferred statement: its span, and the labels outside it.
    pub guard: Option<Span>,
    pub guard_labels: usize,
    /// The code of deferred statements still to emit for flows that pass through them.
    pub pending: Vec<Segment<'a>>,
    /// The handler context depth (the number of `with`s around).
    pub depth: u32,
    /// A temporary was made since the innermost statement began.
    pub made_temp: bool,
    /// A pointer's bytes, read as a word, are its address.
    pub raw_ptr: bool,
    pub ptr_size: u32,
    /// A value too large for registers was met.
    pub too_big: bool,
    /// The number of instructions emitted so far that may make temporaries.
    pub temp_count: u32,
    /// Registers below this are locals and constants.
    pub consts_end: Reg,
    /// The registers holding constant words, set up when an activation starts.
    pub consts: std::collections::HashMap<u64, Reg>,
    /// The statements run by the tree-walker, and one to add to them.
    pub tree_stmts: HashSet<*const Stmt>,
    pub retry: Option<*const Stmt>,
}

/// Compiles function `f`; `Err` if it should run on the tree-walker.
pub fn compile<'a>(m: &Machine<'a>, f: FnId) -> Result<Code<'a>, Unsupported> {
    let prog = m.prog;
    let def = &prog.fns[f.index()];
    let body = def.body.as_ref().ok_or(unsupported!())?;
    // A statement with a value too large for registers is left to the tree-walker:
    // compile again with it on the list until none is left.
    let mut tree_stmts = HashSet::new();
    loop {
        let mut c = Compiler::new(m, f);
        c.tree_stmts = tree_stmts;
        c.classify(body);
        let r = c.function(body);
        match (r, c.retry) {
            (Err(_), Some(s)) if c.tree_stmts.len() < 64 => {
                tree_stmts = c.tree_stmts;
                tree_stmts.insert(s);
            }
            (Err(u), _) => return Err(u),
            (Ok(()), _) if c.too_big => return Err(unsupported!()),
            (Ok(()), _) => return c.finish(),
        }
    }
}

/// True if the parts `(offset, size)` are whole words that follow one another from byte 0 to byte `size`.
fn tiles(parts: &mut [(u32, u32)], size: u32) -> bool {
    parts.sort_unstable();
    let mut at = 0;
    for &(off, n) in parts.iter() {
        if n == 0 {
            continue;
        }
        if off != at || !off.is_multiple_of(8) || !n.is_multiple_of(8) {
            return false;
        }
        at += n;
    }
    at == size
}

/// An array length as an instruction field; arrays too long for it are left to the tree-walker.
fn len32(len: u64) -> C<u32> {
    if len < NO_LEN as u64 { Ok(len as u32) } else { Err(unsupported!()) }
}

/// True if `ty` is a second-class type, whose values are not bytes.
/// The kind of a float type that has native instructions.
pub fn float_kind(ty: Ty) -> Option<FloatK> {
    match ty.kind() {
        TyKind::Float(crate::types::FloatTy::F32) => Some(FloatK::F32),
        TyKind::Float(crate::types::FloatTy::F64) => Some(FloatK::F64),
        _ => None,
    }
}

/// The fast form of `lhs is rhs` when the operands are floats of one type.
pub fn float_eq(lhs: &Expr, rhs: &Expr) -> Option<FloatK> {
    if lhs.ty == rhs.ty { float_kind(lhs.ty) } else { None }
}

pub fn second_class(ty: Ty) -> bool {
    matches!(ty.kind(), TyKind::Fn(_) | TyKind::Handler(_))
}

/// True for expressions compiled code leaves to the tree-walker.
pub fn tree_only(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::With { on: Some(_), .. } | ExprKind::Resume { .. } | ExprKind::Cancel(_) => true,
        ExprKind::Builtin { op: Builtin::Fiber | Builtin::Sys(_) | Builtin::Cover(_), .. } => true,
        ExprKind::Builtin { op: Builtin::Get | Builtin::GetPtr | Builtin::First | Builtin::Last, args } => {
            let base = &args[0];
            match base.ty.kind() {
                TyKind::Slice { .. } => false,
                TyKind::Array { .. } => {
                    !(matches!(&e.kind, ExprKind::Builtin { op: Builtin::GetPtr, .. }) || is_place_expr(base))
                }
                TyKind::Ptr { pointee, .. } => !matches!(pointee.kind(), TyKind::Array { .. }),
                _ => true,
            }
        }
        _ => false,
    }
}

/// The local at the root of a place made of fields, payloads and array elements.
fn place_root(e: &Expr) -> Option<LocalId> {
    match &e.kind {
        ExprKind::Local(id) => Some(*id),
        ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => place_root(base),
        ExprKind::Index { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) => place_root(base),
        _ => None,
    }
}

/// The block whose statements are directly part of `e`.
fn block_of(e: &Expr) -> Option<&Block> {
    match &e.kind {
        ExprKind::Block(b) | ExprKind::Loop { body: b, .. } => Some(b),
        _ => None,
    }
}

/// Calls `f` on every local `e` mentions, as a value or as a binding.
fn mentioned_locals(e: &Expr, f: &mut dyn FnMut(LocalId)) {
    fn pat(p: &Pat, f: &mut dyn FnMut(LocalId)) {
        match &p.kind {
            PatKind::Bind(l) => f(*l),
            PatKind::Some(q) | PatKind::Ok(q) | PatKind::Err(q) => pat(q, f),
            PatKind::Variant { payload: Some(q), .. } => pat(q, f),
            PatKind::Fields(fs) => fs.iter().for_each(|(_, q)| pat(q, f)),
            PatKind::Or(ps) => ps.iter().for_each(|q| pat(q, f)),
            _ => {}
        }
    }
    fn conds(cs: &[CondPart], f: &mut dyn FnMut(LocalId)) {
        for c in cs {
            if let CondPart::Match { pat: p, .. } = c {
                pat(p, f);
            }
        }
    }
    match &e.kind {
        ExprKind::Local(l) => f(*l),
        ExprKind::Else { capture: Some(c), .. } => f(*c),
        ExprKind::WriteDeferred { tmp, .. } => f(*tmp),
        ExprKind::If { cond, .. } | ExprKind::Loop { cond, .. } => conds(cond, f),
        ExprKind::Match { arms, .. } => arms.iter().for_each(|a| pat(&a.pat, f)),
        _ => {}
    }
    if let Some(b) = block_of(e) {
        for s in &b.stmts {
            match s {
                Stmt::Let { local, .. } => f(*local),
                Stmt::LetTuple { locals, .. } => locals.iter().flatten().for_each(|l| f(*l)),
                _ => {}
            }
        }
    }
    e.for_each_child(&mut |c| mentioned_locals(c, f));
}

/// Calls `f` on every local statement `s` mentions.
fn stmt_locals(s: &Stmt, f: &mut dyn FnMut(LocalId)) {
    match s {
        Stmt::Let { local, .. } => f(*local),
        Stmt::LetTuple { locals, .. } => locals.iter().flatten().for_each(|l| f(*l)),
        _ => {}
    }
    match s {
        Stmt::Let { init: Some(e), .. }
        | Stmt::LetTuple { init: e, .. }
        | Stmt::Expr(e)
        | Stmt::Defer { body: e, .. } => mentioned_locals(e, f),
        Stmt::Assign { place, value } => {
            mentioned_locals(place, f);
            mentioned_locals(value, f);
        }
        Stmt::AssignTuple { places, value } => {
            places.iter().flatten().for_each(|p| mentioned_locals(p, f));
            mentioned_locals(value, f);
        }
        Stmt::Let { init: None, .. } => {}
    }
}

/// True if evaluating `e` may assign a local of the function (so that a value read from
/// a local before it is not still in the local's register).
pub fn writes_locals(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Block(b) => {
            if b.stmts.iter().any(|s| !matches!(s, Stmt::Expr(_) | Stmt::Defer { .. })) {
                return true;
            }
        }
        ExprKind::Match { .. } | ExprKind::Loop { .. } | ExprKind::Else { capture: Some(_), .. } => return true,
        ExprKind::If { cond, .. } if cond.iter().any(|c| matches!(c, CondPart::Match { .. })) => return true,
        _ => {}
    }
    let mut any = false;
    e.for_each_child(&mut |c| any = any || writes_locals(c));
    any
}

impl<'m, 'a> Compiler<'m, 'a> {
    fn new(m: &'m Machine<'a>, fid: FnId) -> Self {
        let prog = m.prog;
        let f = &prog.fns[fid.index()];
        let little = m.target.little_endian;
        let ptr_size = m.target.ptr_bytes as u32;
        Compiler {
            m,
            prog,
            f,
            code: Code {
                ops: Vec::new(),
                spans: Vec::new(),
                nregs: 0,
                nslots: 0,
                init: Vec::new(),
                nvals: 0,
                params: Vec::new(),
                scopes: Vec::new(),
                bytes: Vec::new(),
                strs: Vec::new(),
                msgs: Vec::new(),
                args: Vec::new(),
                operands: Vec::new(),
                arg_tys: Vec::new(),
                bins: Vec::new(),
                builtins: Vec::new(),
                coercions: Vec::new(),
                slices: Vec::new(),
                tags: Vec::new(),
                gets: Vec::new(),
                ranges: Vec::new(),
                effects: Vec::new(),
                effect_ctl: Vec::new(),
                decls: Vec::new(),
                trees: Vec::new(),
                stmts: Vec::new(),
                writes: Vec::new(),
            },
            next: 0,
            high: 0,
            vnext: 0,
            vhigh: 0,
            locals: Vec::new(),
            locals_end: 0,
            targets: Vec::new(),
            labels: Vec::new(),
            defers: Vec::new(),
            temps: Vec::new(),
            guard: None,
            guard_labels: 0,
            pending: Vec::new(),
            depth: 0,
            made_temp: false,
            raw_ptr: little && ptr_size == 8,
            ptr_size,
            too_big: false,
            temp_count: 0,
            consts_end: 0,
            consts: Default::default(),
            tree_stmts: HashSet::new(),
            retry: None,
        }
    }

    // --- where locals live ---------------------------------------------------------------

    /// Decides for each local whether it lives in registers or in the frame.
    fn classify(&mut self, body: &'a Expr) {
        let n = self.f.locals.len();
        let mut frame = vec![false; n];
        let mut val = vec![false; n];
        for (i, l) in self.f.locals.iter().enumerate() {
            if second_class(l.ty) {
                val[i] = true;
            } else if self.m.size_of(l.ty) > REG_LOCAL_MAX {
                frame[i] = true;
            }
        }
        // Locals that nested function literals and handler clauses capture.
        let mut captured = HashSet::new();
        self.find_captures(body, 0, &mut captured);
        for l in captured {
            if l < n {
                frame[l] = true;
            }
        }
        self.scan(body, &mut frame);
        let mut reg = 0;
        for i in 0..n {
            let loc = if val[i] {
                LocalLoc::Val
            } else if frame[i] {
                LocalLoc::Frame
            } else {
                let size = self.m.size_of(self.f.locals[i].ty) as u32;
                let r = reg;
                reg += words(size);
                LocalLoc::Reg { reg: r, size }
            };
            self.locals.push(loc);
        }
        self.locals_end = reg;
        // Registers holding the function's small constants, set when an activation starts.
        let mut words = Vec::new();
        self.find_consts(body, &mut words);
        for w in words {
            if let std::collections::hash_map::Entry::Vacant(e) = self.consts.entry(w) {
                e.insert(reg);
                reg += 1;
            }
        }
        self.consts_end = reg;
        self.next = reg;
        self.high = reg;
    }

    /// The words of the literals in `e` of at most 8 bytes.
    fn find_consts(&self, e: &'a Expr, out: &mut Vec<u64>) {
        if matches!(
            e.kind,
            ExprKind::Int(_) | ExprKind::Float(_) | ExprKind::Bool(_) | ExprKind::Null | ExprKind::Uninit
        ) && !second_class(e.ty)
            && self.m.size_of(e.ty) <= 8
            && let Ok(b) = self.literal(e)
        {
            let mut w = [0u8; 8];
            w[..b.len()].copy_from_slice(&b);
            out.push(u64::from_le_bytes(w));
        }
        e.for_each_child(&mut |c| self.find_consts(c, out));
    }

    /// Records the locals of this function (`depth` levels up from the code in `e`) that
    /// `e` refers to as upvars.
    fn find_captures(&self, e: &'a Expr, depth: u32, out: &mut HashSet<usize>) {
        match &e.kind {
            ExprKind::Upvar { levels, local } if *levels == depth && depth > 0 => {
                out.insert(local.index());
            }
            ExprKind::Closure(g) => {
                if let Some(b) = &self.prog.fns[g.index()].body {
                    self.find_captures(b, depth + 1, out);
                }
            }
            ExprKind::HandlerRef(h) => {
                for c in &self.prog.handlers[h.index()].clauses {
                    if let Some(b) = &self.prog.fns[c.index()].body {
                        self.find_captures(b, depth + 1, out);
                    }
                }
            }
            _ => {}
        }
        e.for_each_child(&mut |c| self.find_captures(c, depth, out));
    }

    /// Marks the locals that must live in the frame because of how `e` uses them.
    fn scan(&self, e: &'a Expr, frame: &mut [bool]) {
        let pin = |l: Option<LocalId>, frame: &mut [bool]| {
            if let Some(l) = l
                && l.index() < frame.len()
            {
                frame[l.index()] = true;
            }
        };
        if tree_only(e) {
            mentioned_locals(e, &mut |l| pin(Some(l), frame));
            return;
        }
        match &e.kind {
            ExprKind::AddrOf(inner) => pin(place_root(inner), frame),
            ExprKind::Slice { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) => {
                pin(place_root(base), frame)
            }
            ExprKind::Index { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) && is_place_expr(base) => {
                pin(place_root(base), frame)
            }
            ExprKind::Builtin { op: Builtin::Get | Builtin::GetPtr | Builtin::First | Builtin::Last, args }
                if matches!(args[0].ty.kind(), TyKind::Array { .. }) =>
            {
                pin(place_root(&args[0]), frame)
            }
            _ => {}
        }
        if let Some(b) = block_of(e) {
            for s in &b.stmts {
                if self.tree_stmts.contains(&(s as *const Stmt)) {
                    stmt_locals(s, &mut |l| pin(Some(l), frame));
                    continue;
                }
                match s {
                    Stmt::Assign { place, .. } if !matches!(place.kind, ExprKind::Local(_)) => {
                        pin(place_root(place), frame)
                    }
                    Stmt::AssignTuple { places, .. } => {
                        for p in places.iter().flatten() {
                            if !matches!(p.kind, ExprKind::Local(_)) {
                                pin(place_root(p), frame);
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
        e.for_each_child(&mut |c| self.scan(c, frame));
    }

    // --- registers, labels, emission ---------------------------------------------------------

    pub fn alloc(&mut self, size: u32) -> Reg {
        if size > REG_VALUE_MAX {
            self.too_big = true;
            return 0;
        }
        let r = self.next;
        self.next += words(size);
        self.high = self.high.max(self.next);
        r
    }

    /// Notes that the code just emitted may make a temporary.
    pub fn temp_made(&mut self) {
        self.made_temp = true;
        self.temp_count += 1;
    }

    pub fn valloc(&mut self) -> Reg {
        let r = self.vnext;
        self.vnext += 1;
        self.vhigh = self.vhigh.max(self.vnext);
        r
    }

    /// A fresh location for a value of type `ty`.
    pub fn fresh(&mut self, ty: Ty) -> Arg {
        if second_class(ty) {
            Arg::Val(self.valloc())
        } else {
            let size = self.size(ty);
            Arg::Data { reg: self.alloc(size), size }
        }
    }

    pub fn size(&self, ty: Ty) -> u32 {
        self.m.size_of(ty).min(u32::MAX as usize) as u32
    }

    pub fn align(&self, ty: Ty) -> u32 {
        self.m.ti(ty).align as u32
    }

    pub fn emit(&mut self, op: Op, span: Span) -> u32 {
        self.code.ops.push(op);
        self.code.spans.push(span);
        self.code.ops.len() as u32 - 1
    }

    /// Adds an operand list (and, for builtins, the operands' types) to the pool.
    pub fn arg_list(&mut self, list: Vec<Arg>, tys: Vec<Ty>) -> u32 {
        self.code.args.push(list.into());
        self.code.arg_tys.push(tys.into());
        self.code.args.len() as u32 - 1
    }

    pub fn coercion(&mut self, c: Coercion) -> u32 {
        self.code.coercions.push(c);
        self.code.coercions.len() as u32 - 1
    }

    /// Adds a single operand to the pool.
    pub fn operand(&mut self, a: Arg) -> u32 {
        self.code.operands.push(a);
        self.code.operands.len() as u32 - 1
    }

    pub fn pc(&self) -> u32 {
        self.code.ops.len() as u32
    }

    pub fn label(&mut self) -> u32 {
        self.targets.push(u32::MAX);
        self.targets.len() as u32 - 1
    }

    pub fn bind(&mut self, l: u32) {
        self.targets[l as usize] = self.pc();
    }

    pub fn msg(&mut self, s: impl Into<String>) -> u32 {
        self.code.msgs.push(s.into());
        self.code.msgs.len() as u32 - 1
    }

    pub fn trap_const(&mut self, span: Span, s: impl Into<String>) {
        let msg = self.msg(s);
        self.emit(Op::TrapConst { msg }, span);
    }

    pub fn small(&self, ty: Ty) -> Option<IntK> {
        self.m.small_of(ty).map(IntK::of)
    }

    /// Moves a value between locations of the same type.
    pub fn mov(&mut self, dst: Arg, src: Arg, span: Span) {
        match (dst, src) {
            (Arg::Data { reg: d, size }, Arg::Data { reg: s, size: ss }) => {
                let size = size.min(ss);
                if d == s || size == 0 {
                    return;
                }
                let w = words(size);
                if w == 1 {
                    self.emit(Op::Mov1 { dst: d, src: s }, span);
                } else {
                    self.emit(Op::Mov { dst: d, src: s, words: w }, span);
                }
            }
            (Arg::Val(d), Arg::Val(s)) if d != s => {
                self.emit(Op::MovVal { dst: d, src: s }, span);
            }
            _ => {}
        }
    }

    /// The address in the pointer value at `v` (the same register on little-endian targets).
    pub fn addr_of_arg(&mut self, v: Arg, span: Span) -> Reg {
        let Arg::Data { reg, .. } = v else { return 0 };
        if self.raw_ptr {
            return reg;
        }
        let dst = self.alloc(8);
        self.emit(Op::PtrToAddr { dst, src: reg }, span);
        dst
    }

    /// Writes the pointer to address `addr` into `dst`.
    pub fn load_from_addr(&mut self, dst: Reg, addr: Reg, span: Span) {
        if self.raw_ptr {
            if dst != addr {
                self.emit(Op::Mov1 { dst, src: addr }, span);
            }
        } else {
            self.emit(Op::AddrToPtr { dst, src: addr }, span);
        }
    }

    pub fn const_bytes(&mut self, dst: Reg, b: &[u8], span: Span) {
        if b.is_empty() {
            return;
        }
        if b.len() <= 8 {
            let mut w = [0u8; 8];
            w[..b.len()].copy_from_slice(b);
            self.emit(Op::Imm { dst, w: u64::from_le_bytes(w) }, span);
        } else {
            self.code.bytes.push(Bytes::from_slice(b));
            let idx = self.code.bytes.len() as u32 - 1;
            self.emit(Op::Bytes { dst, idx }, span);
        }
    }

    /// Writes `size` (≤ 8) bytes of the word `w` at byte `off` of the value in `dst`.
    pub fn insert_imm(&mut self, dst: Reg, off: u32, b: &[u8], span: Span) {
        if b.iter().all(|x| *x == 0) {
            return;
        }
        let t = self.alloc(b.len() as u32);
        self.const_bytes(t, b, span);
        self.insert(dst, off, t, b.len() as u32, span);
    }

    // --- the function ----------------------------------------------------------------------------

    fn function(&mut self, body: &'a Expr) -> C {
        for p in &self.f.params {
            let loc = match self.locals[p.index()] {
                LocalLoc::Reg { reg, size } => ParamLoc::Reg { reg, size },
                LocalLoc::Frame => ParamLoc::Frame { local: *p, ty: self.f.locals[p.index()].ty },
                LocalLoc::Val => ParamLoc::Val { local: *p },
            };
            self.code.params.push(loc);
        }
        let v = self.expr(body)?;
        self.ret(v, ErrTest::Dynamic, body.span)?;
        self.segments()
    }

    // --- expressions -------------------------------------------------------------------------------

    /// Compiles `e` into a fresh location, or returns where it already is (a local's registers,
    /// which the caller must not write).
    pub fn expr(&mut self, e: &'a Expr) -> C<Arg> {
        self.expr_in(e, None)
    }

    /// Compiles `e` into `dst`.
    pub fn expr_to(&mut self, e: &'a Expr, dst: Arg) -> C {
        self.expr_in(e, Some(dst))?;
        Ok(())
    }

    /// The location for the value of `e`: `want`, or a fresh one.
    pub fn dst(&mut self, e: &Expr, want: Option<Arg>) -> Arg {
        match want {
            Some(d) => d,
            None => self.fresh(e.ty),
        }
    }

    /// Releases the temporaries above `mark`, keeping `keep` if it was allocated after it.
    pub fn release(&mut self, mark: Reg, keep: Arg) {
        let end = match keep {
            Arg::Data { reg, size } if reg >= mark => reg + words(size),
            _ => mark,
        };
        self.next = end.max(mark);
    }

    pub fn expr_in(&mut self, e: &'a Expr, want: Option<Arg>) -> C<Arg> {
        let mark = self.next;
        let vmark = self.vnext;
        let r = self.expr_inner(e, want)?;
        self.release(mark, r);
        if let Arg::Val(v) = r {
            self.vnext = vmark.max(if v >= vmark { v + 1 } else { vmark });
        } else {
            self.vnext = vmark;
        }
        Ok(r)
    }

    fn expr_inner(&mut self, e: &'a Expr, want: Option<Arg>) -> C<Arg> {
        if tree_only(e) {
            return self.eval_tree(e, want);
        }
        let span = e.span;
        match &e.kind {
            ExprKind::Int(_)
            | ExprKind::Float(_)
            | ExprKind::Bool(_)
            | ExprKind::Null
            | ExprKind::ConstBytes(_)
            | ExprKind::Uninit => {
                if self.size(e.ty) > REG_VALUE_MAX {
                    self.too_big = true;
                    return Ok(Arg::UNIT);
                }
                if want.is_none()
                    && self.size(e.ty) <= 8
                    && let Ok(b) = self.literal(e)
                {
                    let mut w = [0u8; 8];
                    w[..b.len()].copy_from_slice(&b);
                    if let Some(r) = self.consts.get(&u64::from_le_bytes(w)) {
                        return Ok(Arg::Data { reg: *r, size: self.size(e.ty) });
                    }
                }
                let dst = self.dst(e, want);
                match self.literal(e) {
                    Ok(b) => {
                        if let Arg::Data { reg, size } = dst {
                            if b.iter().all(|x| *x == 0) {
                                if size > 0 {
                                    self.emit(Op::Zero { dst: reg, words: words(size) }, span);
                                }
                            } else {
                                self.const_bytes(reg, &b, span);
                            }
                        }
                    }
                    Err(m) => self.trap_const(span, m),
                }
                Ok(dst)
            }
            ExprKind::Str(s) | ExprKind::CStr(s) => {
                let dst = self.dst(e, want);
                let nul = matches!(e.kind, ExprKind::CStr(_));
                self.code.strs.push(StrLit { bytes: s.clone(), nul, addr: Cell::new(0) });
                let idx = self.code.strs.len() as u32 - 1;
                if let Arg::Data { reg, .. } = dst {
                    self.emit(Op::Str { dst: reg, idx }, span);
                }
                Ok(dst)
            }
            ExprKind::Const(id) => {
                let dst = self.dst(e, want);
                if let Arg::Data { reg, .. } = dst {
                    self.emit(Op::ConstLoad { dst: reg, id: *id, ty: e.ty }, span);
                }
                Ok(dst)
            }
            ExprKind::Local(id) => self.read_local(e, *id, want),
            ExprKind::Upvar { levels, local } => {
                let dst = self.dst(e, want);
                match dst {
                    Arg::Data { reg, .. } => {
                        self.emit(Op::LdUpvar { dst: reg, levels: *levels, local: *local, ty: e.ty }, span)
                    }
                    Arg::Val(v) => self.emit(Op::LdUpvarVal { dst: v, levels: *levels, local: *local }, span),
                };
                Ok(dst)
            }
            ExprKind::Global(_) | ExprKind::Deref(_) => self.place_value(e, want),
            ExprKind::Field { base, index } => {
                if is_place_expr(base) && !self.reg_rooted(base) {
                    return self.place_value(e, want);
                }
                let v = self.expr(base)?;
                let Arg::Data { reg: src, .. } = v else { return Err(unsupported!()) };
                if self.m.bits_struct_ty(base.ty).is_some() {
                    let dst = self.dst(e, want);
                    let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                    self.emit(Op::BitsField { dst: reg, src, ty: base.ty, idx: *index }, span);
                    return Ok(dst);
                }
                let (_, off) = self.m.field_layout(base.ty, *index as usize).map_err(|_| unsupported!())?;
                self.part(e, src, off as u32, want)
            }
            ExprKind::VariantPayload { base, .. } => {
                if is_place_expr(base) && !self.reg_rooted(base) {
                    return self.place_value(e, want);
                }
                let v = self.expr(base)?;
                let Arg::Data { reg: src, .. } = v else { return Err(unsupported!()) };
                let off = self.m.payload_offset(base.ty) as u32;
                self.part(e, src, off, want)
            }
            ExprKind::Index { base, index, .. } => {
                if let TyKind::Array { len, .. } = base.ty.kind()
                    && !is_place_expr(base)
                {
                    let b = self.expr(base)?;
                    let b = self.stable(b, &[index], span);
                    let (idx, ik) = self.index_operand(index)?;
                    let dst = self.dst(e, want);
                    let (Arg::Data { reg: src, .. }, Arg::Data { reg, size }) = (b, dst) else {
                        return Err(unsupported!());
                    };
                    let len = len32(*len)?;
                    self.emit(Op::IndexVal { dst: reg, src, idx, ik, esz: size, len }, span);
                    return Ok(dst);
                }
                self.place_value(e, want)
            }
            ExprKind::Slice { base, lo, hi, inclusive } => self.slice(e, base, lo, hi, *inclusive, want),
            ExprKind::SliceLen(s) | ExprKind::SlicePtr(s) => {
                let v = self.expr(s)?;
                let Arg::Data { reg: src, .. } = v else { return Err(unsupported!()) };
                let off = if matches!(e.kind, ExprKind::SliceLen(_)) { self.ptr_size } else { 0 };
                self.part(e, src, off, want)
            }
            ExprKind::AddrOf(inner) => {
                let p = self.place(inner)?;
                let dst = self.dst(e, want);
                match (p, dst) {
                    (PlaceC::Mem { addr, .. }, Arg::Data { reg, .. }) => self.load_from_addr(reg, addr, span),
                    (PlaceC::Bits { .. }, _) => self.trap_const(span, "cannot take the address of a #bits field"),
                    _ => return Err(unsupported!()),
                }
                Ok(dst)
            }
            ExprKind::AddrOfTemp(inner) => {
                let v = self.expr(inner)?;
                let dst = self.dst(e, want);
                let (Arg::Data { reg: src, size }, Arg::Data { reg, .. }) = (v, dst) else {
                    return Err(unsupported!());
                };
                let align = self.align(inner.ty);
                self.emit(Op::AllocTemp { dst: reg, src, size, align }, span);
                self.temp_made();
                Ok(dst)
            }
            ExprKind::FnRef(f) => {
                let dst = self.dst(e, want);
                match dst {
                    Arg::Val(v) => {
                        self.emit(Op::MkClosure { dst: v, f: *f, env: false }, span);
                    }
                    Arg::Data { reg, .. } => {
                        let b = self.m.ptr_bytes(self.m.fn_addr(*f));
                        self.const_bytes(reg, &b, span);
                    }
                }
                Ok(dst)
            }
            ExprKind::Closure(f) => {
                let dst = self.dst(e, want);
                let Arg::Val(v) = dst else { return Err(unsupported!()) };
                self.emit(Op::MkClosure { dst: v, f: *f, env: true }, span);
                Ok(dst)
            }
            ExprKind::HandlerRef(h) => {
                let dst = self.dst(e, want);
                let Arg::Val(v) = dst else { return Err(unsupported!()) };
                self.emit(Op::MkHandler { dst: v, h: *h }, span);
                Ok(dst)
            }
            ExprKind::Unary { op, operand } => self.unary(e, *op, operand, want),
            ExprKind::Binary { op, lhs, rhs } => self.binary(e, *op, lhs, rhs, want),
            ExprKind::Logic { .. } => {
                let dst = self.dst(e, want);
                let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                let f = self.label();
                let end = self.label();
                self.jfalse(e, f)?;
                self.emit(Op::Imm { dst: reg, w: 1 }, span);
                self.emit(Op::Jmp { t: end }, span);
                self.bind(f);
                self.emit(Op::Imm { dst: reg, w: 0 }, span);
                self.bind(end);
                Ok(dst)
            }
            ExprKind::Coerce { kind, expr } => self.coerce(e, *kind, expr, want),
            ExprKind::Builtin { op, args } => self.builtin(e, *op, args, want),
            ExprKind::Call { callee, args } => self.call(e, callee, args, want),
            ExprKind::Aggregate(items) => self.aggregate(e, items, want),
            ExprKind::Array(items) => {
                let esz = items.first().map_or(0, |i| self.size(i.ty));
                if want.is_none()
                    && esz > 0
                    && esz.is_multiple_of(8)
                    && self.size(e.ty) == esz * items.len() as u32
                    && !items.iter().any(|i| second_class(i.ty))
                {
                    let offs = (0..items.len() as u32).map(|i| i * esz).collect();
                    return self.aggregate_in_place(items, offs, e.ty, span);
                }
                let dst = self.dst(e, want);
                let Arg::Data { reg, size } = dst else { return Err(unsupported!()) };
                let esz = items.first().map_or(0, |i| self.size(i.ty));
                let mut vals = Vec::new();
                for (i, it) in items.iter().enumerate() {
                    let v = self.expr(it)?;
                    let v = self.stable(v, &items[i + 1..].iter().collect::<Vec<_>>(), span);
                    vals.push(v);
                }
                if size > 0 && !(esz > 0 && esz.is_multiple_of(8) && size == esz * vals.len() as u32) {
                    self.emit(Op::Zero { dst: reg, words: words(size) }, span);
                }
                for (i, v) in vals.into_iter().enumerate() {
                    if let Arg::Data { reg: src, .. } = v
                        && esz > 0
                    {
                        self.insert(reg, i as u32 * esz, src, esz, span);
                    }
                }
                Ok(dst)
            }
            ExprKind::ArrayFill(x) => {
                let v = self.expr(x)?;
                let dst = self.dst(e, want);
                let TyKind::Array { len, .. } = e.ty.kind() else { return Err(unsupported!()) };
                let (Arg::Data { reg: src, size: esz }, Arg::Data { reg, size }) = (v, dst) else {
                    return Err(unsupported!());
                };
                if size > 0 {
                    self.emit(Op::Fill { dst: reg, src, esz, n: *len as u32 }, span);
                }
                Ok(dst)
            }
            ExprKind::Variant { variant, payload } => {
                let p = match payload {
                    Some(p) => Some(self.expr(p)?),
                    None => None,
                };
                let dst = self.dst(e, want);
                let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                self.make_variant(reg, e.ty, *variant, p, span)?;
                Ok(dst)
            }
            ExprKind::MakeErr(x) => {
                let v = self.expr(x)?;
                let dst = self.dst(e, want);
                let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                self.make_res(reg, e.ty, true, v, span);
                Ok(dst)
            }
            ExprKind::Unwrap { expr, is_result } => {
                let v = self.expr(expr)?;
                let dst = self.dst(e, want);
                let (Arg::Data { reg: src, .. }, Arg::Data { reg, .. }) = (v, dst) else { return Err(unsupported!()) };
                if *is_result {
                    self.emit(Op::UnwrapRes { dst: reg, src, ty: expr.ty }, span);
                } else {
                    self.emit(Op::UnwrapOpt { dst: reg, src, ty: expr.ty }, span);
                }
                Ok(dst)
            }
            ExprKind::Eq { lhs, rhs, negated } => self.eq(e, lhs, rhs, *negated, want),
            ExprKind::In { elem, container, negated } => {
                let ops = self.operands(&[elem, container], span)?;
                let dst = self.dst(e, want);
                let (Arg::Data { reg: a, .. }, Arg::Data { reg: b, .. }, Arg::Data { reg, .. }) = (ops[0], ops[1], dst)
                else {
                    return Err(unsupported!());
                };
                self.emit(Op::InG { dst: reg, elem: a, cont: b, ety: elem.ty, cty: container.ty, neg: *negated }, span);
                Ok(dst)
            }
            ExprKind::Block(b) => {
                let dst = self.dst(e, want);
                self.block(b, dst)?;
                Ok(dst)
            }
            ExprKind::If { cond, then, else_ } => {
                let dst = self.dst(e, want);
                let el = self.label();
                let end = self.label();
                self.jconds(cond, el)?;
                self.expr_to(then, dst)?;
                self.emit(Op::Jmp { t: end }, span);
                self.bind(el);
                if let Some(x) = else_ {
                    self.expr_to(x, dst)?;
                }
                self.bind(end);
                Ok(dst)
            }
            ExprKind::Match { scrutinee, arms } => self.match_(e, scrutinee, arms, want),
            ExprKind::Else { lhs, capture, rhs } => self.else_(e, lhs, *capture, rhs, want),
            ExprKind::Try { expr, lift, levels } => self.try_(e, expr, *lift, *levels, want),
            ExprKind::Loop { label, cond, body, step } => self.loop_(e, *label, cond, body, step.as_deref(), want),
            ExprKind::Break { target, value } => {
                self.break_(e, *target, value.as_deref())?;
                Ok(self.dst(e, want))
            }
            ExprKind::Continue { target } => {
                self.continue_(e, *target)?;
                Ok(self.dst(e, want))
            }
            ExprKind::Return { levels, value } => {
                let v = self.expr(value)?;
                if *levels == 0 {
                    self.ret(v, ErrTest::Dynamic, span)?;
                } else {
                    self.emit(Op::ThrowReturn { levels: *levels, v }, span);
                }
                Ok(self.dst(e, want))
            }
            ExprKind::With { label, handlers, on: None, body } => self.with(e, *label, handlers, body, want),
            ExprKind::Trap { message } => {
                let v = self.expr(message)?;
                let Arg::Data { reg, .. } = v else { return Err(unsupported!()) };
                self.emit(Op::TrapMsg { src: reg, kind: TrapKind::Plain }, span);
                Ok(self.dst(e, want))
            }
            ExprKind::Write { sink, args, .. } => {
                let sv = match sink {
                    WriteSink::Fn(x) => self.expr(x)?,
                    WriteSink::Perform { .. } => Arg::UNIT,
                };
                let refs: Vec<&'a Expr> = args.iter().collect();
                let sv = self.stable(sv, &refs, span);
                let list = self.operands(&refs, span)?;
                let a = self.arg_list(list, Vec::new());
                self.code.writes.push(e);
                let info = self.code.writes.len() as u32 - 1;
                self.emit(Op::Write { info, sink: sv, args: a }, span);
                Ok(self.dst(e, want))
            }
            _ => Err(unsupported!()),
        }
    }

    /// The bytes of a literal (or the message of a trap it causes).
    fn literal(&self, e: &Expr) -> Result<Bytes, String> {
        let m = self.m;
        Ok(match &e.kind {
            ExprKind::Int(v) => {
                if let Some(si) = m.small_of(e.ty) {
                    m.encode_small(si, Machine::int_literal(v, si))
                } else {
                    let v = crate::interp::num::Int::from_big(v.clone());
                    match e.ty.kind() {
                        TyKind::Float(f) => m.float_bytes(*f, crate::interp::num::round_float(v.to_f64(), *f)),
                        _ => {
                            let ti = m.ti(e.ty);
                            let (s, bits) = ti.int.unwrap_or((true, 64));
                            crate::interp::num::encode_int(&crate::interp::num::wrap(&v, s, bits), ti.size, m.little())
                        }
                    }
                }
            }
            ExprKind::Float(x) => match e.ty.kind() {
                TyKind::Float(f) => m.float_bytes(*f, crate::interp::num::round_float(*x, *f)),
                _ => return Err("float literal of a non-float type".into()),
            },
            ExprKind::Bool(b) => m.bool_bytes(*b),
            ExprKind::Null => m.make_none(e.ty),
            ExprKind::ConstBytes(b) => Bytes::from_slice(b),
            ExprKind::Uninit => {
                let fill = if m.mode == crate::interp::Mode::Debug { 0xAA } else { 0 };
                Bytes::filled(m.size_of(e.ty), fill)
            }
            _ => unreachable!(),
        })
    }

    fn eval_tree(&mut self, e: &'a Expr, want: Option<Arg>) -> C<Arg> {
        let dst = self.dst(e, want);
        self.code.trees.push(e);
        let expr = self.code.trees.len() as u32 - 1;
        self.emit(Op::EvalTree { expr, out: dst }, e.span);
        self.temp_made();
        Ok(dst)
    }

    /// A copy of `v` in a temporary if it is a local's register that evaluating `later` may
    /// change.
    pub fn stable(&mut self, v: Arg, later: &[&Expr], span: Span) -> Arg {
        if let Arg::Data { reg, size } = v
            && reg < self.locals_end
            && size > 0
            && later.iter().any(|x| writes_locals(x))
        {
            let t = self.alloc(size);
            let d = Arg::Data { reg: t, size };
            self.mov(d, v, span);
            return d;
        }
        v
    }

    /// Compiles operands in order, keeping each one's value safe from the later ones.
    pub fn operands(&mut self, es: &[&'a Expr], span: Span) -> C<Vec<Arg>> {
        let mut out = Vec::with_capacity(es.len());
        for i in 0..es.len() {
            let v = self.expr(es[i])?;
            let v = self.stable(v, &es[i + 1..], span);
            out.push(v);
        }
        Ok(out)
    }

    fn read_local(&mut self, e: &Expr, id: LocalId, want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        match self.locals[id.index()] {
            LocalLoc::Reg { reg, size } => {
                let src = Arg::Data { reg, size: size.min(self.size(e.ty)) };
                match want {
                    None => Ok(src),
                    Some(d) => {
                        self.mov(d, src, span);
                        Ok(d)
                    }
                }
            }
            LocalLoc::Frame => {
                let dst = self.dst(e, want);
                let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                self.emit(Op::LdLocal { dst: reg, local: id, ty: e.ty }, span);
                Ok(dst)
            }
            LocalLoc::Val => {
                let dst = self.dst(e, want);
                let Arg::Val(v) = dst else { return Err(unsupported!()) };
                self.emit(Op::LdLocalVal { dst: v, local: id }, span);
                Ok(dst)
            }
        }
    }

    /// True if `e`, a place of fields and payloads, is part of a local kept in registers.
    fn reg_rooted(&self, e: &Expr) -> bool {
        match &e.kind {
            ExprKind::Local(id) => matches!(self.locals[id.index()], LocalLoc::Reg { .. }),
            ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => self.reg_rooted(base),
            _ => false,
        }
    }

    /// The part of type `e.ty` at byte `off` of the value at `src`: the same registers if
    /// it fills whole words, else a copy.
    fn part(&mut self, e: &Expr, src: Reg, off: u32, want: Option<Arg>) -> C<Arg> {
        let size = self.size(e.ty);
        if want.is_none() && off.is_multiple_of(8) && size.is_multiple_of(8) {
            return Ok(Arg::Data { reg: src + off / 8, size });
        }
        let dst = self.dst(e, want);
        let Arg::Data { reg, size } = dst else { return Err(unsupported!()) };
        self.extract(reg, src, off, size, e.span);
        Ok(dst)
    }

    pub fn extract(&mut self, dst: Reg, src: Reg, off: u32, size: u32, span: Span) {
        if size == 0 {
            return;
        }
        if off == 0 && dst == src {
            return;
        }
        if off.is_multiple_of(8) && size.is_multiple_of(8) {
            self.mov(Arg::Data { reg: dst, size }, Arg::Data { reg: src + off / 8, size }, span);
        } else {
            self.emit(Op::Extract { dst, src, off, size }, span);
        }
    }

    /// Copies the `size` bytes of the value at `src` to byte `off` of the value at `dst`.
    pub fn insert(&mut self, dst: Reg, off: u32, src: Reg, size: u32, span: Span) {
        if size == 0 {
            return;
        }
        if off.is_multiple_of(8) && size.is_multiple_of(8) {
            self.mov(Arg::Data { reg: dst + off / 8, size }, Arg::Data { reg: src, size }, span);
        } else {
            self.emit(Op::Insert { dst, off, src, size }, span);
        }
    }

    // --- places ------------------------------------------------------------------------------------

    /// The integer in `index` as an index word (§7.9: negative indexes are out of bounds).
    pub fn index_operand(&mut self, index: &'a Expr) -> C<(Reg, IntK)> {
        let v = self.expr(index)?;
        let Arg::Data { reg, .. } = v else { return Err(unsupported!()) };
        if let Some(k) = self.small(index.ty) {
            return Ok((reg, k));
        }
        let dst = self.alloc(8);
        self.emit(Op::ToIndex { dst, src: reg, ty: index.ty }, index.span);
        Ok((dst, IntK::U64))
    }

    /// Computes the address of a place expression.
    pub fn place(&mut self, e: &'a Expr) -> C<PlaceC> {
        let span = e.span;
        match &e.kind {
            ExprKind::Local(id) => match self.locals[id.index()] {
                LocalLoc::Frame => {
                    let dst = self.alloc(8);
                    self.emit(Op::AddrLocal { dst, local: *id, ty: e.ty }, span);
                    Ok(PlaceC::Mem { addr: dst, packed: false })
                }
                _ => Err(unsupported!()),
            },
            ExprKind::Upvar { levels, local } => {
                if second_class(e.ty) {
                    return Err(unsupported!());
                }
                let dst = self.alloc(8);
                self.emit(Op::AddrUpvar { dst, levels: *levels, local: *local, ty: e.ty }, span);
                Ok(PlaceC::Mem { addr: dst, packed: false })
            }
            ExprKind::Global(g) => {
                let dst = self.alloc(8);
                self.emit(Op::GlobalAddr { dst, g: *g }, span);
                Ok(PlaceC::Mem { addr: dst, packed: false })
            }
            ExprKind::Const(id) => {
                let dst = self.alloc(8);
                self.emit(Op::ConstAddr { dst, id: *id }, span);
                Ok(PlaceC::Mem { addr: dst, packed: false })
            }
            ExprKind::Deref(p) => {
                let v = self.expr(p)?;
                let addr = self.addr_of_arg(v, span);
                Ok(PlaceC::Mem { addr, packed: false })
            }
            ExprKind::Field { base, index } => {
                if self.m.bits_struct_ty(base.ty).is_some() {
                    let (off, fty) = self.m.bits_field(base.ty, *index as usize).map_err(|_| unsupported!())?;
                    return Ok(match self.place(base)? {
                        PlaceC::Mem { addr, .. } => PlaceC::Bits { addr, bits: BitsPlace { whole: base.ty, off, fty } },
                        PlaceC::Bits { addr, bits } => {
                            PlaceC::Bits { addr, bits: BitsPlace { whole: bits.whole, off: bits.off + off, fty } }
                        }
                    });
                }
                let (addr, packed) = match self.place(base)? {
                    PlaceC::Mem { addr, packed } => (addr, packed || self.m.is_packed(base.ty)),
                    PlaceC::Bits { .. } => return Err(unsupported!()),
                };
                let (_, off) = self.m.field_layout(base.ty, *index as usize).map_err(|_| unsupported!())?;
                Ok(PlaceC::Mem { addr: self.add_imm(addr, off as u64, span), packed })
            }
            ExprKind::VariantPayload { base, .. } => {
                let PlaceC::Mem { addr, .. } = self.place(base)? else { return Err(unsupported!()) };
                let off = self.m.payload_offset(base.ty) as u64;
                Ok(PlaceC::Mem { addr: self.add_imm(addr, off, span), packed: false })
            }
            ExprKind::Index { base, index, .. } => {
                let esz = self.size(e.ty);
                match base.ty.kind() {
                    TyKind::Array { len, .. } => {
                        let (addr, packed) = match self.place(base)? {
                            PlaceC::Mem { addr, packed } => (addr, packed),
                            PlaceC::Bits { .. } => return Err(unsupported!()),
                        };
                        let (idx, ik) = self.index_operand(index)?;
                        let dst = self.alloc(8);
                        let len = len32(*len)?;
                        self.emit(Op::IndexAddr { dst, base: addr, idx, ik, esz, len }, span);
                        Ok(PlaceC::Mem { addr: dst, packed })
                    }
                    TyKind::Slice { .. } => {
                        let sv = self.expr(base)?;
                        let sv = self.stable(sv, &[index], span);
                        let Arg::Data { reg: slice, .. } = sv else { return Err(unsupported!()) };
                        let (idx, ik) = self.index_operand(index)?;
                        let dst = self.alloc(8);
                        self.emit(Op::SliceIndexAddr { dst, slice, idx, ik, esz }, span);
                        Ok(PlaceC::Mem { addr: dst, packed: false })
                    }
                    TyKind::ManyPtr { .. } => {
                        let pv = self.expr(base)?;
                        let pv = self.stable(pv, &[index], span);
                        let p = self.addr_of_arg(pv, span);
                        let (idx, ik) = self.index_operand(index)?;
                        let dst = self.alloc(8);
                        self.emit(Op::IndexAddr { dst, base: p, idx, ik, esz, len: NO_LEN }, span);
                        Ok(PlaceC::Mem { addr: dst, packed: false })
                    }
                    TyKind::Ptr { pointee, .. } if matches!(pointee.kind(), TyKind::Array { .. }) => {
                        let TyKind::Array { len, .. } = pointee.kind() else { unreachable!() };
                        let pv = self.expr(base)?;
                        let pv = self.stable(pv, &[index], span);
                        let p = self.addr_of_arg(pv, span);
                        let (idx, ik) = self.index_operand(index)?;
                        let dst = self.alloc(8);
                        let len = len32(*len)?;
                        self.emit(Op::IndexAddr { dst, base: p, idx, ik, esz, len }, span);
                        Ok(PlaceC::Mem { addr: dst, packed: false })
                    }
                    _ => Err(unsupported!()),
                }
            }
            _ => {
                let v = self.expr(e)?;
                let Arg::Data { reg: src, size } = v else { return Err(unsupported!()) };
                let dst = self.alloc(8);
                let align = self.align(e.ty);
                self.emit(Op::AllocTemp { dst, src, size, align }, span);
                self.temp_made();
                let addr = self.addr_of_arg(Arg::Data { reg: dst, size: self.ptr_size }, span);
                Ok(PlaceC::Mem { addr, packed: false })
            }
        }
    }

    fn add_imm(&mut self, addr: Reg, off: u64, span: Span) -> Reg {
        if off == 0 {
            return addr;
        }
        let dst = self.alloc(8);
        self.emit(Op::AddImm { dst, src: addr, imm: off }, span);
        dst
    }

    /// Reads a value of type `ty` from a place.
    pub fn load(&mut self, p: PlaceC, ty: Ty, dst: Reg, span: Span) {
        match p {
            PlaceC::Mem { addr, packed } => {
                let size = self.size(ty);
                let align = if packed { 1 } else { self.align(ty) };
                self.emit(Op::Load { dst, addr, size, align }, span);
            }
            PlaceC::Bits { addr, bits } => {
                self.emit(Op::LoadBits { dst, addr, bits }, span);
            }
        }
    }

    /// Writes the value in `src` (`size` bytes, of type `ty`) to a place.
    pub fn store(&mut self, p: PlaceC, ty: Ty, src: Reg, size: u32, span: Span) {
        match p {
            PlaceC::Mem { addr, packed } => {
                let align = if packed { 1 } else { self.align(ty) };
                self.emit(Op::Store { addr, src, size, align }, span);
            }
            PlaceC::Bits { addr, bits } => {
                self.emit(Op::StoreBits { addr, src, bits }, span);
            }
        }
    }

    fn place_value(&mut self, e: &'a Expr, want: Option<Arg>) -> C<Arg> {
        let dst = self.dst(e, want);
        let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
        let p = self.place(e)?;
        self.load(p, e.ty, reg, e.span);
        Ok(dst)
    }

    fn slice(
        &mut self,
        e: &'a Expr,
        base: &'a Expr,
        lo: &'a Option<Box<Expr>>,
        hi: &'a Option<Box<Expr>>,
        inclusive: bool,
        want: Option<Arg>,
    ) -> C<Arg> {
        let span = e.span;
        let esz = match e.ty.kind() {
            TyKind::Slice { elem, .. } => self.size(*elem) as u64,
            _ => return Err(unsupported!()),
        };
        let dst = self.dst(e, want);
        let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
        let later: Vec<&Expr> = lo.iter().chain(hi.iter()).map(|b| &**b).collect();
        let (ptr, len) = match base.ty.kind() {
            TyKind::Array { len, .. } => match self.place(base)? {
                PlaceC::Mem { addr, .. } => (addr, SliceLen::Static(*len)),
                PlaceC::Bits { .. } => {
                    self.trap_const(span, "internal error: slice of a #bits field");
                    return Ok(dst);
                }
            },
            TyKind::Slice { .. } => {
                let sv = self.expr(base)?;
                let sv = self.stable(sv, &later, span);
                let Arg::Data { reg: s, .. } = sv else { return Err(unsupported!()) };
                let p = self.addr_of_arg(sv, span);
                (p, SliceLen::OfSlice(s))
            }
            TyKind::ManyPtr { .. } => {
                let pv = self.expr(base)?;
                let pv = self.stable(pv, &later, span);
                (self.addr_of_arg(pv, span), SliceLen::Unknown)
            }
            TyKind::Ptr { pointee, .. } => match pointee.kind() {
                TyKind::Array { len, .. } => {
                    let pv = self.expr(base)?;
                    let pv = self.stable(pv, &later, span);
                    (self.addr_of_arg(pv, span), SliceLen::Static(*len))
                }
                _ => {
                    self.trap_const(span, "cannot slice this pointer");
                    return Ok(dst);
                }
            },
            _ => return Err(unsupported!()),
        };
        let lo = match lo {
            Some(x) => Some(self.index_operand(x)?),
            None => None,
        };
        let hi = match hi {
            Some(x) => Some(self.index_operand(x)?),
            None => None,
        };
        self.code.slices.push(SliceInfo { ptr, len, lo, hi, inclusive, esz });
        let info = self.code.slices.len() as u32 - 1;
        self.emit(Op::MakeSlice { dst: reg, info }, span);
        Ok(dst)
    }

    // --- operators -------------------------------------------------------------------------------------

    fn unary(&mut self, e: &'a Expr, op: UnOp, operand: &'a Expr, want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        if op == UnOp::Not {
            let dst = self.dst(e, want);
            let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
            let f = self.label();
            let end = self.label();
            self.jtrue(operand, f)?;
            self.emit(Op::Imm { dst: reg, w: 1 }, span);
            self.emit(Op::Jmp { t: end }, span);
            self.bind(f);
            self.emit(Op::Imm { dst: reg, w: 0 }, span);
            self.bind(end);
            return Ok(dst);
        }
        let v = self.expr(operand)?;
        let dst = self.dst(e, want);
        let (Arg::Data { reg: a, .. }, Arg::Data { reg, .. }) = (v, dst) else { return Err(unsupported!()) };
        if operand.ty == e.ty
            && let Some(k) = self.small(e.ty)
        {
            match op {
                UnOp::Neg => self.emit(Op::NegI { dst: reg, a, k }, span),
                _ => self.emit(Op::NotI { dst: reg, a, k }, span),
            };
        } else if op == UnOp::Neg
            && operand.ty == e.ty
            && let Some(k) = float_kind(e.ty)
        {
            self.emit(Op::NegF { dst: reg, a, k }, span);
        } else {
            self.emit(Op::UnG { dst: reg, a, op, ty: operand.ty }, span);
        }
        Ok(dst)
    }

    /// The float kind of a binary operation on two floats of one type that has native
    /// instructions.
    pub fn float_binary(&self, op: BinOp, lhs: &Expr, rhs: &Expr) -> Option<FloatK> {
        let arith = matches!(op, BinOp::Add(_) | BinOp::Sub(_) | BinOp::Mul(_) | BinOp::Div);
        if lhs.ty != rhs.ty || !(arith || Cmp::of(op).is_some()) {
            return None;
        }
        float_kind(lhs.ty)
    }

    /// The small-integer kinds of a binary operation that can use the fast instructions.
    pub fn small_binary(&self, op: BinOp, lhs: &Expr, rhs: &Expr, res: Ty) -> Option<(IntK, IntK)> {
        self.m.small_binary_info(op, lhs, rhs, res).map(|(a, b)| (IntK::of(a), IntK::of(b)))
    }

    fn binary(&mut self, e: &'a Expr, op: BinOp, lhs: &'a Expr, rhs: &'a Expr, want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        let ops = self.operands(&[lhs, rhs], span)?;
        let dst = self.dst(e, want);
        let (Arg::Data { reg: a, size: lsize }, Arg::Data { reg: b, size: rsize }, Arg::Data { reg, .. }) =
            (ops[0], ops[1], dst)
        else {
            return Err(unsupported!());
        };
        if let Some((k, rk)) = self.small_binary(op, lhs, rhs, e.ty) {
            match (Cmp::of(op), op) {
                (Some(cmp), _) => self.emit(Op::CmpI { cmp, dst: reg, a, b, k }, span),
                (_, BinOp::Add(fl)) => self.emit(Op::AddI { fl, dst: reg, a, b, k }, span),
                (_, BinOp::Sub(fl)) => self.emit(Op::SubI { fl, dst: reg, a, b, k }, span),
                (_, BinOp::Mul(fl)) => self.emit(Op::MulI { fl, dst: reg, a, b, k }, span),
                (_, BinOp::BitAnd) => self.emit(Op::AndW { dst: reg, a, b }, span),
                (_, BinOp::BitOr) => self.emit(Op::OrW { dst: reg, a, b }, span),
                (_, BinOp::BitXor) => self.emit(Op::XorW { dst: reg, a, b }, span),
                _ => {
                    self.code.bins.push(BinInfo { op, lty: lhs.ty, rty: rhs.ty, res: e.ty, lsize, rsize, rk });
                    let info = self.code.bins.len() as u32 - 1;
                    self.emit(Op::BinI { op, dst: reg, a, b, k, info }, span)
                }
            };
        } else if let Some(k) = self.float_binary(op, lhs, rhs) {
            let fop = match op {
                BinOp::Add(_) => Some(FArith::Add),
                BinOp::Sub(_) => Some(FArith::Sub),
                BinOp::Mul(_) => Some(FArith::Mul),
                BinOp::Div => Some(FArith::Div),
                _ => None,
            };
            match (Cmp::of(op), fop) {
                (Some(cmp), _) => self.emit(Op::CmpF { cmp, dst: reg, a, b, k }, span),
                (_, Some(op)) => self.emit(Op::BinF { op, dst: reg, a, b, k }, span),
                _ => unreachable!(),
            };
        } else {
            let rk = IntK::U64;
            self.code.bins.push(BinInfo { op, lty: lhs.ty, rty: rhs.ty, res: e.ty, lsize, rsize, rk });
            let info = self.code.bins.len() as u32 - 1;
            self.emit(Op::BinG { dst: reg, a, b, info }, span);
        }
        Ok(dst)
    }

    /// The fast form of `lhs is rhs` (`Ne` if negated) when the operands are small integers.
    pub fn small_eq(&self, lhs: &Expr, rhs: &Expr) -> Option<IntK> {
        if lhs.ty == rhs.ty { self.small(lhs.ty) } else { None }
    }

    /// True if values of `ty` are equal exactly when their words are (pointers).
    fn word_eq(&self, ty: Ty) -> bool {
        match ty.kind() {
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => true,
            TyKind::Optional(inner) => layout::is_nullable_pointer(*inner),
            _ => false,
        }
    }

    fn eq(&mut self, e: &'a Expr, lhs: &'a Expr, rhs: &'a Expr, neg: bool, want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        let ops = self.operands(&[lhs, rhs], span)?;
        let dst = self.dst(e, want);
        let (Arg::Data { reg: a, .. }, Arg::Data { reg: b, .. }, Arg::Data { reg, .. }) = (ops[0], ops[1], dst) else {
            return Err(unsupported!());
        };
        if let Some(k) = self.small_eq(lhs, rhs) {
            let cmp = if neg { Cmp::Ne } else { Cmp::Eq };
            self.emit(Op::CmpI { cmp, dst: reg, a, b, k }, span);
        } else if let Some(k) = float_eq(lhs, rhs) {
            let cmp = if neg { Cmp::Ne } else { Cmp::Eq };
            self.emit(Op::CmpF { cmp, dst: reg, a, b, k }, span);
        } else if lhs.ty == rhs.ty && self.word_eq(lhs.ty) {
            self.emit(Op::EqW { dst: reg, a, b, neg }, span);
        } else if lhs.ty == rhs.ty && matches!(lhs.ty.kind(), TyKind::Bool) {
            self.emit(Op::EqBool { dst: reg, a, b, neg }, span);
        } else {
            self.emit(Op::EqG { dst: reg, a, b, ty: lhs.ty, neg }, span);
        }
        Ok(dst)
    }

    fn coerce(&mut self, e: &'a Expr, kind: Coercion, inner: &'a Expr, want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        match kind {
            Coercion::PtrToPtr | Coercion::TupleRename | Coercion::FnPtrWiden
                if !second_class(e.ty) && !second_class(inner.ty) =>
            {
                let v = self.expr_in(inner, want)?;
                if let (Arg::Data { reg, .. }, None) = (v, want) {
                    return Ok(Arg::Data { reg, size: self.size(e.ty) });
                }
                Ok(v)
            }
            Coercion::FromNever => {
                self.expr(inner)?;
                self.trap_const(span, "internal error: a `never` expression produced a value");
                Ok(self.dst(e, want))
            }
            Coercion::FnToPtr => {
                let v = self.expr(inner)?;
                let dst = self.dst(e, want);
                match (v, dst) {
                    (Arg::Val(_), Arg::Val(_)) | (Arg::Data { .. }, Arg::Data { .. }) => self.mov(dst, v, span),
                    _ => {
                        let (dst, src) = (self.operand(dst), self.operand(v));
                        self.emit(Op::FnToPtr { dst, src, to: e.ty }, span);
                    }
                }
                Ok(dst)
            }
            Coercion::IntWiden => {
                let v = self.expr(inner)?;
                let dst = self.dst(e, want);
                let (Arg::Data { reg: src, .. }, Arg::Data { reg, .. }) = (v, dst) else { return Err(unsupported!()) };
                match (self.small(inner.ty), self.small(e.ty)) {
                    (Some(from), Some(to)) => {
                        self.emit(Op::Widen { dst: reg, src, from, to }, span);
                    }
                    _ => {
                        let kind = self.coercion(kind);
                        self.emit(Op::CoerceG { dst: reg, src, kind, from: inner.ty, to: e.ty }, span);
                    }
                }
                Ok(dst)
            }
            Coercion::WrapOptional => {
                let v = self.expr(inner)?;
                let dst = self.dst(e, want);
                let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                self.make_some(reg, e.ty, v, span);
                Ok(dst)
            }
            Coercion::WrapOk => {
                let v = self.expr(inner)?;
                let dst = self.dst(e, want);
                let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                self.make_res(reg, e.ty, false, v, span);
                Ok(dst)
            }
            Coercion::Unresolved => Err(unsupported!()),
            _ => {
                let v = self.expr(inner)?;
                let dst = self.dst(e, want);
                let (Arg::Data { reg: src, .. }, Arg::Data { reg, .. }) = (v, dst) else { return Err(unsupported!()) };
                let kind = self.coercion(kind);
                self.emit(Op::CoerceG { dst: reg, src, kind, from: inner.ty, to: e.ty }, span);
                Ok(dst)
            }
        }
    }

    // --- optionals, results, unions -------------------------------------------------------------------

    /// Builds the present optional of type `ty` holding `payload` in `dst`.
    pub fn make_some(&mut self, dst: Reg, ty: Ty, payload: Arg, span: Span) {
        let inner = self.m.opt_inner(ty);
        let ti = self.m.ti(ty);
        let (size, off0, off1) = (ti.size as u32, ti.offsets.first().copied(), ti.offsets.get(1).copied());
        let Arg::Data { reg: src, size: psize } = payload else { return };
        if layout::is_nullable_pointer(inner) {
            let n = psize.min(size);
            if words(n) == words(size) && n == size {
                self.mov(Arg::Data { reg: dst, size }, Arg::Data { reg: src, size: n }, span);
            } else {
                self.emit(Op::Zero { dst, words: words(size) }, span);
                self.insert(dst, 0, src, n, span);
            }
            return;
        }
        let off0 = off0.unwrap_or(0) as u32;
        let off1 = off1.unwrap_or(0) as u32;
        let n = psize.min(size - off0);
        let t = if dst == src && n > 0 { self.copy_tmp(src, n, span) } else { src };
        self.emit(Op::Zero { dst, words: words(size) }, span);
        if n > 0 {
            self.insert(dst, off0, t, n, span);
        }
        self.insert_imm(dst, off1, &[1], span);
    }

    fn copy_tmp(&mut self, src: Reg, size: u32, span: Span) -> Reg {
        let t = self.alloc(size);
        self.mov(Arg::Data { reg: t, size }, Arg::Data { reg: src, size }, span);
        t
    }

    /// Builds the result of type `ty` (an error if `is_err`) holding `payload` in `dst`.
    pub fn make_res(&mut self, dst: Reg, ty: Ty, is_err: bool, payload: Arg, span: Span) {
        let ti = self.m.ti(ty);
        let (size, off0, off1) = (ti.size as u32, ti.offsets[0] as u32, ti.offsets[1] as u32);
        let (src, psize) = match payload {
            Arg::Data { reg, size } => (reg, size),
            Arg::Val(_) => (0, 0),
        };
        let n = psize.min(size - off1);
        let t = if dst == src && n > 0 { self.copy_tmp(src, n, span) } else { src };
        self.emit(Op::Zero { dst, words: words(size) }, span);
        if is_err {
            self.insert_imm(dst, off0, &[1], span);
        }
        if n > 0 {
            self.insert(dst, off1, t, n, span);
        }
    }

    /// Builds variant `variant` of union `ty` with `payload` in `dst`.
    pub fn make_variant(&mut self, dst: Reg, ty: Ty, variant: u32, payload: Option<Arg>, span: Span) -> C {
        let (tag, variants) = self.m.union_def(ty).ok_or(unsupported!())?;
        let size = self.size(ty);
        let tag_bytes =
            self.m.int_bytes(tag, &crate::interp::num::Int::from_big(variants[variant as usize].tag.clone()));
        let p = match payload {
            Some(Arg::Data { reg, size: ps }) if ps > 0 => {
                let off = self.m.payload_offset(ty) as u32;
                let n = ps.min(size.saturating_sub(off));
                let t = if reg == dst { self.copy_tmp(reg, n, span) } else { reg };
                Some((t, off, n))
            }
            _ => None,
        };
        if size > 0 {
            self.emit(Op::Zero { dst, words: words(size) }, span);
        }
        self.insert_imm(dst, 0, &tag_bytes, span);
        if let Some((t, off, n)) = p {
            self.insert(dst, off, t, n, span);
        }
        Ok(())
    }

    /// The test of `ty`'s tag against `variant`, in the pool.
    pub fn tag_test(&mut self, ty: Ty, variant: u32) -> C<u32> {
        let (tag, variants) = self.m.union_def(ty).ok_or(unsupported!())?;
        let k = self.small(tag).ok_or(unsupported!())?;
        let to = |v: &num_bigint::BigInt| num_traits::ToPrimitive::to_i128(v).ok_or(unsupported!());
        let want = to(&variants[variant as usize].tag)?;
        let mut valid = Vec::with_capacity(variants.len());
        for v in variants {
            valid.push(to(&v.tag)?);
        }
        valid.sort_unstable();
        let off = self.m.ti(ty).offsets.first().copied().unwrap_or(0) as u32;
        self.code.tags.push(TagTest { ty, off, k, want, valid: valid.into() });
        Ok(self.code.tags.len() as u32 - 1)
    }

    /// Jumps to `t` if the optional of type `ty` in `src` is absent (`absent`) or present.
    pub fn jmp_present(&mut self, src: Reg, ty: Ty, t: u32, present: bool, span: Span) {
        let inner = self.m.opt_inner(ty);
        if layout::is_nullable_pointer(inner) {
            if present {
                self.emit(Op::JmpNZ { c: src, t }, span);
            } else {
                self.emit(Op::JmpZ { c: src, t }, span);
            }
        } else {
            let off = self.m.ti(ty).offsets[1] as u32;
            self.emit(Op::JmpByte { c: src, off, t, when: present }, span);
        }
    }

    // --- builtins ----------------------------------------------------------------------------------------

    fn builtin(&mut self, e: &'a Expr, op: Builtin, args: &'a [Expr], want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        match op {
            Builtin::Assert => {
                let ok = self.label();
                self.emit(Op::JmpFast { t: ok }, span);
                let c = self.expr(&args[0])?;
                let Arg::Data { reg, .. } = c else { return Err(unsupported!()) };
                self.emit(Op::JmpIf { c: reg, t: ok }, span);
                let kind = TrapKind::Assert;
                match args.get(1) {
                    Some(m) => {
                        let v = self.expr(m)?;
                        let Arg::Data { reg, .. } = v else { return Err(unsupported!()) };
                        self.emit(Op::TrapMsg { src: reg, kind }, span);
                    }
                    None => self.trap_const(span, "assertion failed"),
                }
                self.bind(ok);
                Ok(self.dst(e, want))
            }
            Builtin::Get | Builtin::GetPtr | Builtin::First | Builtin::Last => {
                let base = &args[0];
                let res = e.ty;
                let (esz, align) = match res.kind() {
                    TyKind::Optional(t) => match (op, t.kind()) {
                        (Builtin::GetPtr, TyKind::Ptr { pointee, .. }) => (self.size(*pointee), self.align(*t)),
                        _ => (self.size(*t), self.align(*t)),
                    },
                    _ => return Err(unsupported!()),
                };
                let later: Vec<&Expr> = args[1..].iter().collect();
                let (addr, len) = match base.ty.kind() {
                    TyKind::Slice { .. } => {
                        let sv = self.expr(base)?;
                        let sv = self.stable(sv, &later, span);
                        let Arg::Data { reg: s, .. } = sv else { return Err(unsupported!()) };
                        (self.addr_of_arg(sv, span), SliceLen::OfSlice(s))
                    }
                    TyKind::Array { len, .. } => match self.place(base)? {
                        PlaceC::Mem { addr, .. } => (addr, SliceLen::Static(*len)),
                        PlaceC::Bits { .. } => return Err(unsupported!()),
                    },
                    TyKind::Ptr { pointee, .. } => {
                        let TyKind::Array { len, .. } = pointee.kind() else { return Err(unsupported!()) };
                        let pv = self.expr(base)?;
                        let pv = self.stable(pv, &later, span);
                        (self.addr_of_arg(pv, span), SliceLen::Static(*len))
                    }
                    _ => return Err(unsupported!()),
                };
                let idx = match op {
                    Builtin::Get | Builtin::GetPtr => {
                        let v = self.expr(&args[1])?;
                        let Arg::Data { reg, .. } = v else { return Err(unsupported!()) };
                        let d = self.alloc(8);
                        self.emit(Op::ToIndex { dst: d, src: reg, ty: args[1].ty }, span);
                        Some((d, IntK::U64))
                    }
                    _ => None,
                };
                let dst = self.dst(e, want);
                let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
                self.code.gets.push(GetInfo { op, addr, len, idx, esz: esz as u64, align: align as u64, res });
                let info = self.code.gets.len() as u32 - 1;
                self.emit(Op::Get { dst: reg, info }, span);
                Ok(dst)
            }
            _ => {
                if matches!(op, Builtin::Convert | Builtin::Trunc)
                    && args.len() == 1
                    && let (Some(from), Some(to)) = (self.small(args[0].ty), self.small(e.ty))
                {
                    let v = self.expr(&args[0])?;
                    let dst = self.dst(e, want);
                    let (Arg::Data { reg: src, .. }, Arg::Data { reg, .. }) = (v, dst) else {
                        return Err(unsupported!());
                    };
                    if op == Builtin::Convert {
                        self.emit(Op::Widen { dst: reg, src, from, to }, span);
                    } else {
                        self.emit(Op::Trunc { dst: reg, src, from, to }, span);
                    }
                    return Ok(dst);
                }
                if matches!(op, Builtin::Rotl | Builtin::Rotr)
                    && args.len() == 2
                    && let (Some(k), Some(bk)) = (self.small(args[0].ty), self.small(args[1].ty))
                {
                    let ops = self.operands(&[&args[0], &args[1]], span)?;
                    let dst = self.dst(e, want);
                    let (Arg::Data { reg: a, .. }, Arg::Data { reg: b, .. }, Arg::Data { reg, .. }) =
                        (ops[0], ops[1], dst)
                    else {
                        return Err(unsupported!());
                    };
                    self.emit(Op::RotI { left: op == Builtin::Rotl, dst: reg, a, b, k, bk }, span);
                    return Ok(dst);
                }
                let refs: Vec<&'a Expr> = args.iter().collect();
                let list = self.operands(&refs, span)?;
                if list.iter().any(|a| matches!(a, Arg::Val(_))) {
                    return Err(unsupported!());
                }
                let tys: Vec<Ty> = args.iter().map(|a| a.ty).collect();
                let a = self.arg_list(list, tys);
                let dst = self.dst(e, want);
                self.code.builtins.push(BuiltinInfo { op, args: a, res: e.ty, out: dst });
                let info = self.code.builtins.len() as u32 - 1;
                self.emit(Op::Builtin { info }, span);
                Ok(dst)
            }
        }
    }

    // --- calls ------------------------------------------------------------------------------------------

    fn call(&mut self, e: &'a Expr, callee: &'a Callee, args: &'a [Expr], want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        // A variadic foreign call needs the types of its arguments, which the tree-walker has.
        if let Callee::Fn(f) = callee
            && matches!(self.m.prog.fns[f.index()].kind, FnKind::Foreign { variadic: true, .. })
        {
            return Err(unsupported!());
        }
        let refs: Vec<&'a Expr> = args.iter().collect();
        let cv = match callee {
            Callee::Indirect(c) => {
                let v = self.expr(c)?;
                Some(self.stable(v, &refs, span))
            }
            _ => None,
        };
        let list = self.operands(&refs, span)?;
        let a = self.arg_list(list, Vec::new());
        let out = self.dst(e, want);
        match callee {
            Callee::Fn(f) => {
                if self.m.prog.heap_fns.contains_key(f) {
                    self.emit(Op::CallHeap { f: *f, args: a, out }, span);
                } else {
                    self.emit(Op::Call { f: *f, args: a, out }, span);
                }
            }
            Callee::Indirect(c) => {
                let (callee, out) = (self.operand(cv.unwrap()), self.operand(out));
                self.emit(Op::CallInd { callee, cty: c.ty, args: a, out }, span);
            }
            Callee::Perform { effect, op } => {
                self.code.effects.push(effect.clone());
                self.code.effect_ctl.push(self.m.prog.ctl_ops.contains(&(effect.effect, *op)));
                let eff = self.code.effects.len() as u32 - 1;
                let out = self.operand(out);
                self.emit(Op::Perform { eff, op: *op, args: a, out }, span);
            }
        }
        Ok(out)
    }

    /// The offsets of the items of an aggregate that fills whole words one after the other,
    /// so that each can be computed where it belongs.
    fn tiled_offsets(&self, e: &Expr, items: &[Expr]) -> Option<Vec<u32>> {
        if self.m.bits_struct_ty(e.ty).is_some()
            || matches!(self.m.adt(e.ty), Some(AdtDef { kind: AdtKind::CUnion { .. }, .. }))
            || items.iter().any(|i| second_class(i.ty))
        {
            return None;
        }
        let mut parts = Vec::with_capacity(items.len());
        for (i, it) in items.iter().enumerate() {
            let (_, off) = self.m.field_layout(e.ty, i).ok()?;
            parts.push((off as u32, self.size(it.ty)));
        }
        let offs = parts.iter().map(|p| p.0).collect();
        let size = self.size(e.ty);
        (size > 0 && tiles(&mut parts, size)).then_some(offs)
    }

    /// Builds a value of `items` that are each computed straight into their place in a new value.
    fn aggregate_in_place(&mut self, items: &'a [Expr], offs: Vec<u32>, ty: Ty, span: Span) -> C<Arg> {
        let dst = self.fresh(ty);
        let Arg::Data { reg, .. } = dst else { return Err(unsupported!()) };
        for (it, off) in items.iter().zip(offs) {
            let n = self.size(it.ty);
            if n == 0 {
                self.expr(it)?;
                continue;
            }
            let slot = Arg::Data { reg: reg + off / 8, size: n };
            let v = self.expr_in(it, Some(slot))?;
            self.mov(slot, v, span);
        }
        Ok(dst)
    }

    fn aggregate(&mut self, e: &'a Expr, items: &'a [Expr], want: Option<Arg>) -> C<Arg> {
        let span = e.span;
        if want.is_none()
            && let Some(offs) = self.tiled_offsets(e, items)
        {
            return self.aggregate_in_place(items, offs, e.ty, span);
        }
        let refs: Vec<&'a Expr> = items.iter().collect();
        let vals = self.operands(&refs, span)?;
        let dst = self.dst(e, want);
        let Arg::Data { reg, size } = dst else { return Err(unsupported!()) };
        if self.m.bits_struct_ty(e.ty).is_some() {
            let a = self.arg_list(vals, Vec::new());
            self.emit(Op::BitsAggregate { dst: reg, ty: e.ty, args: a }, span);
            return Ok(dst);
        }
        // The items may be in `dst`'s registers already only if `dst` is fresh, which it is
        // when it was allocated after them; otherwise they are elsewhere.
        let is_union = matches!(self.m.adt(e.ty), Some(AdtDef { kind: AdtKind::CUnion { .. }, .. }));
        let mut parts = Vec::new();
        if !is_union {
            for (i, v) in vals.iter().enumerate() {
                let (_, off) = self.m.field_layout(e.ty, i).map_err(|_| unsupported!())?;
                if let Arg::Data { size: n, .. } = *v {
                    parts.push((off as u32, n));
                }
            }
        }
        // Items that fill whole words, one after the other, leave nothing to clear.
        if size > 0 && !(!is_union && tiles(&mut parts, size)) {
            self.emit(Op::Zero { dst: reg, words: words(size) }, span);
        }
        if is_union {
            if let Some(Arg::Data { reg: src, size: n }) = vals.first().copied()
                && n > 0
            {
                self.insert(reg, 0, src, n, span);
            }
            return Ok(dst);
        }
        for (i, v) in vals.into_iter().enumerate() {
            let (_, off) = self.m.field_layout(e.ty, i).map_err(|_| unsupported!())?;
            if let Arg::Data { reg: src, size: n } = v
                && n > 0
            {
                self.insert(reg, off as u32, src, n, span);
            }
        }
        Ok(dst)
    }
}
