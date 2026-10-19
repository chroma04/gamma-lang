//! The bytecode engine: function bodies compiled to register code, which is how the
//! interpreter runs every function.
//!
//! [`compile`] translates a function the first time it is called, [`exec`] runs the
//! code. Closed expressions (constants, globals, compile-time evaluation) and the
//! bodies of `with … on` are compiled the same way, to code without parameters
//! ([`compile_expr`], [`compile::compile_sub`]).
//!
//! # Registers
//!
//! Each activation has a vector of 64-bit words. A value of `n` bytes occupies
//! `ceil(n / 8)` consecutive words that hold its bytes in target layout (the
//! interpreter's memory format), with the unused bytes of the last word zero; every
//! instruction that writes a value keeps that true. Locals live in registers unless
//! something needs them elsewhere (see below); temporaries are allocated above the
//! locals like a stack. Second-class values (closures and handler values) cannot be
//! bytes and have registers of their own, of [`Value`]s.
//!
//! Addresses computed for places are plain numbers in one word. On a little-endian
//! target a pointer's bytes, read as a word, are that number already.
//!
//! # Locals
//!
//! A local stays in its frame's slots when its address may be needed, a nested
//! function captures it, it is a second-class value, it is larger than 64 bytes, or
//! the body of a `with … on` mentions it. Closures therefore see the locals they
//! capture wherever the function that owns them runs.
//!
//! # `with … on`
//!
//! The body of a `with … on` runs on a fiber, which has a thread of its own (see
//! `fiber.rs`), so it is code of its own: it runs in the frame of the activation that
//! contains it, which is where the locals it shares with the rest of the function
//! live. A `return` in it, and a `break` or `continue` to a label outside it, are
//! flows to that frame, which take the fiber down and land in the function's code.
//!
//! # What cannot be compiled
//!
//! A function the compiler cannot translate (a bug in the compiler, or a temporary
//! larger than any frame) gets code that only traps, with an internal error naming
//! the line of the compiler that gave up.
//!
//! # Control flow
//!
//! `break`, `continue` and `return` within a function become jumps, preceded by
//! copies of the deferred statements they leave. Flows that arrive from a call
//! (traps, exits, and jumps out of closures and handler clauses into this frame) are
//! handled with a table of the function's scopes: deferred statements, loops and
//! `with` expressions, innermost first.

mod compile;
mod compile_flow;
mod exec;
mod heap;

use super::bytes::Bytes;
use super::machine::*;
use super::value::*;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use std::cell::Cell;
use std::rc::Rc;

pub use compile::{compile, compile_expr};
pub use exec::St;
pub use heap::{HeapCx, HeapSlot};

/// Prints the counts of instructions run (`--features vm-profile`, `GAMMA_VM_PROFILE=1`).
#[cfg(feature = "vm-profile")]
pub fn profile_report() {
    exec::profile::report()
}

/// A register: an index into an activation's words, or (for second-class values) into
/// its values.
pub type Reg = u32;

/// A small integer type: at most 64 bits in at most 8 bytes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct IntK {
    pub signed: bool,
    pub bits: u8,
    pub size: u8,
}

impl IntK {
    pub fn of(si: SmallInt) -> IntK {
        IntK { signed: si.signed, bits: si.bits as u8, size: si.size as u8 }
    }

    #[inline(always)]
    pub fn si(self) -> SmallInt {
        SmallInt { signed: self.signed, bits: self.bits as u32, size: self.size as usize }
    }

    /// The unsigned 64-bit integer type, for indexes already converted to a word.
    pub const U64: IntK = IntK { signed: false, bits: 64, size: 8 };
}

/// Where an operand is: `size` bytes of data at a register, or a second-class value.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Arg {
    Data { reg: Reg, size: u32 },
    Val(Reg),
}

impl Arg {
    pub const UNIT: Arg = Arg::Data { reg: 0, size: 0 };
}

/// The length of an indexing operation that is not bounds checked.
pub const NO_LEN: u32 = u32::MAX;

/// The words a value of `size` bytes occupies.
#[inline(always)]
pub fn words(size: u32) -> u32 {
    size.div_ceil(8)
}

/// How a parameter receives its argument.
#[derive(Clone, Copy, Debug)]
pub enum ParamLoc {
    Reg { reg: Reg, size: u32 },
    Frame { local: LocalId, ty: Ty },
    Val { local: LocalId },
}

/// The bit field of a `#bits` struct that a place names.
#[derive(Clone, Copy, Debug)]
pub struct BitsPlace {
    pub whole: Ty,
    pub off: u32,
    pub fty: Ty,
}

/// Operands of a generic binary operator.
#[derive(Clone, Copy, Debug)]
pub struct BinInfo {
    pub op: BinOp,
    pub lty: Ty,
    pub rty: Ty,
    pub res: Ty,
    pub lsize: u32,
    pub rsize: u32,
    /// The kind of the right operand of a small-integer operator.
    pub rk: IntK,
}

/// A slicing operation `base[lo..hi]` once the base is evaluated.
#[derive(Clone, Copy, Debug)]
pub struct SliceInfo {
    /// The register with the address of the first element.
    pub ptr: Reg,
    pub len: SliceLen,
    pub lo: Option<(Reg, IntK)>,
    pub hi: Option<(Reg, IntK)>,
    pub inclusive: bool,
    pub esz: u64,
}

#[derive(Clone, Copy, Debug)]
pub enum SliceLen {
    Static(u64),
    /// The length is in the second half of the slice value in this register.
    OfSlice(Reg),
    /// A many-pointer has no length.
    Unknown,
}

/// The test of a union value's tag against variant `want` (at byte `off`, of integer kind `k`).
#[derive(Clone, Debug)]
pub struct TagTest {
    pub ty: Ty,
    pub off: u32,
    pub k: IntK,
    pub want: i128,
    /// Every valid tag, sorted: a value matching none of them traps.
    pub valid: Box<[i128]>,
}

/// A `get`, `get_ptr`, `first` or `last` on a sequence whose elements start at `addr`.
#[derive(Clone, Copy, Debug)]
pub struct GetInfo {
    pub op: Builtin,
    pub addr: Reg,
    pub len: SliceLen,
    pub idx: Option<(Reg, IntK)>,
    pub esz: u64,
    pub align: u64,
    pub res: Ty,
}

/// A call of a builtin function.
#[derive(Clone, Copy, Debug)]
pub struct BuiltinInfo {
    pub op: Builtin,
    pub args: u32,
    pub res: Ty,
    pub out: Arg,
}

/// A string literal and, once materialized, its address.
#[derive(Debug)]
pub struct StrLit {
    pub bytes: Rc<[u8]>,
    pub nul: bool,
    pub addr: Cell<u64>,
}

/// The kinds of messages a trap with a run-time message has.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TrapKind {
    /// `trap(msg)`.
    Plain,
    /// A failed `assert(c, msg)`.
    Assert,
}

/// A floating-point type with native instructions (`f16` goes through the generic ones).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FloatK {
    F32,
    F64,
}

/// An arithmetic operator of [`Op::BinF`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FArith {
    Add,
    Sub,
    Mul,
    Div,
}

/// A comparison of two small integers or two floats.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Cmp {
    Lt,
    Le,
    Gt,
    Ge,
    Eq,
    Ne,
}

impl Cmp {
    pub fn of(op: BinOp) -> Option<Cmp> {
        Some(match op {
            BinOp::Lt => Cmp::Lt,
            BinOp::Le => Cmp::Le,
            BinOp::Gt => Cmp::Gt,
            BinOp::Ge => Cmp::Ge,
            _ => return None,
        })
    }
}

/// One instruction. Jump targets are instruction indexes; `t` fields are filled in when
/// the function's code is complete.
#[derive(Clone, Copy, Debug)]
pub enum Op {
    Nop,
    // --- data ---
    Mov {
        dst: Reg,
        src: Reg,
        words: u32,
    },
    Mov1 {
        dst: Reg,
        src: Reg,
    },
    MovVal {
        dst: Reg,
        src: Reg,
    },
    Imm {
        dst: Reg,
        w: u64,
    },
    /// Constant bytes from the pool.
    Bytes {
        dst: Reg,
        idx: u32,
    },
    Zero {
        dst: Reg,
        words: u32,
    },
    /// Copies `size` bytes at byte `off` of the value in `src` to `dst`.
    Extract {
        dst: Reg,
        src: Reg,
        off: u32,
        size: u32,
    },
    /// Copies the `size` bytes of `src` to byte `off` of the value in `dst`.
    Insert {
        dst: Reg,
        off: u32,
        src: Reg,
        size: u32,
    },
    /// `n` copies of the `esz` bytes of `src`.
    Fill {
        dst: Reg,
        src: Reg,
        esz: u32,
        n: u32,
    },
    // --- locals in the frame ---
    LdLocal {
        dst: Reg,
        local: LocalId,
        ty: Ty,
    },
    StLocal {
        local: LocalId,
        src: Reg,
        ty: Ty,
    },
    /// `let x` without a value.
    InitLocal {
        local: LocalId,
        ty: Ty,
    },
    AddrLocal {
        dst: Reg,
        local: LocalId,
        ty: Ty,
    },
    LdUpvar {
        dst: Reg,
        levels: u32,
        local: LocalId,
        ty: Ty,
    },
    AddrUpvar {
        dst: Reg,
        levels: u32,
        local: LocalId,
        ty: Ty,
    },
    LdLocalVal {
        dst: Reg,
        local: LocalId,
    },
    StLocalVal {
        local: LocalId,
        src: Reg,
    },
    LdUpvarVal {
        dst: Reg,
        levels: u32,
        local: LocalId,
    },
    // --- memory ---
    Load {
        dst: Reg,
        addr: Reg,
        size: u32,
        align: u32,
    },
    Store {
        addr: Reg,
        src: Reg,
        size: u32,
        align: u32,
    },
    LoadBits {
        dst: Reg,
        addr: Reg,
        bits: BitsPlace,
    },
    StoreBits {
        addr: Reg,
        src: Reg,
        bits: BitsPlace,
    },
    /// Converts a pointer's bytes to an address and back (big-endian targets only).
    PtrToAddr {
        dst: Reg,
        src: Reg,
    },
    AddrToPtr {
        dst: Reg,
        src: Reg,
    },
    AddImm {
        dst: Reg,
        src: Reg,
        imm: u64,
    },
    /// `base + i * esz` after checking `i < len` (unchecked and wrapping if `len` is [`NO_LEN`]).
    IndexAddr {
        dst: Reg,
        base: Reg,
        idx: Reg,
        ik: IntK,
        esz: u32,
        len: u32,
    },
    /// Like `IndexAddr` with a length of 2^32 or more, which is in register `len`.
    IndexAddrW {
        dst: Reg,
        base: Reg,
        idx: Reg,
        ik: IntK,
        esz: u32,
        len: Reg,
    },
    /// The address of element `i` of the slice in `slice`, bounds checked.
    SliceIndexAddr {
        dst: Reg,
        slice: Reg,
        idx: Reg,
        ik: IntK,
        esz: u32,
    },
    /// Element `i` of the array value in `src`, bounds checked.
    IndexVal {
        dst: Reg,
        src: Reg,
        idx: Reg,
        ik: IntK,
        esz: u32,
        len: u32,
    },
    MakeSlice {
        dst: Reg,
        info: u32,
    },
    AllocTemp {
        dst: Reg,
        src: Reg,
        size: u32,
        align: u32,
    },
    MarkTemps {
        dst: Reg,
    },
    KillTemps {
        src: Reg,
    },
    Str {
        dst: Reg,
        idx: u32,
    },
    ConstLoad {
        dst: Reg,
        id: ConstId,
        ty: Ty,
    },
    ConstAddr {
        dst: Reg,
        id: ConstId,
    },
    GlobalAddr {
        dst: Reg,
        g: GlobalId,
    },
    // --- small integers ---
    /// Division, remainder and shifts of small integers (`info` describes the operands).
    BinI {
        op: BinOp,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: IntK,
        info: u32,
    },
    /// `+`, `-` and `*` of small integers of kind `k`; the flavor decides what an overflow does.
    AddI {
        fl: Flavor,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: IntK,
    },
    SubI {
        fl: Flavor,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: IntK,
    },
    MulI {
        fl: Flavor,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: IntK,
    },
    /// Bitwise operators on values of at most 8 bytes, whatever their type.
    AndW {
        dst: Reg,
        a: Reg,
        b: Reg,
    },
    OrW {
        dst: Reg,
        a: Reg,
        b: Reg,
    },
    XorW {
        dst: Reg,
        a: Reg,
        b: Reg,
    },
    CmpI {
        cmp: Cmp,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: IntK,
    },
    BrI {
        cmp: Cmp,
        a: Reg,
        b: Reg,
        k: IntK,
        t: u32,
    },
    NegI {
        dst: Reg,
        a: Reg,
        k: IntK,
    },
    /// `@rotl(a, b)` (`left`) or `@rotr(a, b)` of a small integer of kind `k` by an
    /// amount of kind `bk`.
    RotI {
        left: bool,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: IntK,
        bk: IntK,
    },
    /// `+ - * /` of `f32` or `f64` values.
    BinF {
        op: FArith,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: FloatK,
    },
    CmpF {
        cmp: Cmp,
        dst: Reg,
        a: Reg,
        b: Reg,
        k: FloatK,
    },
    BrF {
        cmp: Cmp,
        a: Reg,
        b: Reg,
        k: FloatK,
        t: u32,
    },
    NegF {
        dst: Reg,
        a: Reg,
        k: FloatK,
    },
    NotI {
        dst: Reg,
        a: Reg,
        k: IntK,
    },
    /// The low bits of a small integer as one of another type.
    Trunc {
        dst: Reg,
        src: Reg,
        from: IntK,
        to: IntK,
    },
    Widen {
        dst: Reg,
        src: Reg,
        from: IntK,
        to: IntK,
    },
    /// Converts an integer of any type to an index word (negative: 0, too large: saturated).
    ToIndex {
        dst: Reg,
        src: Reg,
        ty: Ty,
    },
    // --- other data ---
    /// Word equality of values of at most 8 bytes (pointers).
    EqW {
        dst: Reg,
        a: Reg,
        b: Reg,
        neg: bool,
    },
    EqBool {
        dst: Reg,
        a: Reg,
        b: Reg,
        neg: bool,
    },
    BinG {
        dst: Reg,
        a: Reg,
        b: Reg,
        info: u32,
    },
    UnG {
        dst: Reg,
        a: Reg,
        op: UnOp,
        ty: Ty,
    },
    EqG {
        dst: Reg,
        a: Reg,
        b: Reg,
        ty: Ty,
        neg: bool,
    },
    InG {
        dst: Reg,
        elem: Reg,
        cont: Reg,
        ety: Ty,
        cty: Ty,
        neg: bool,
    },
    CoerceG {
        dst: Reg,
        src: Reg,
        kind: u32,
        from: Ty,
        to: Ty,
    },
    FnToPtr {
        dst: u32,
        src: u32,
        to: Ty,
    },
    /// Field `idx` of the `#bits` struct value in `src`.
    BitsField {
        dst: Reg,
        src: Reg,
        ty: Ty,
        idx: u32,
    },
    /// Builds a `#bits` struct from its fields.
    BitsAggregate {
        dst: Reg,
        ty: Ty,
        args: u32,
    },
    UnwrapOpt {
        dst: Reg,
        src: Reg,
        ty: Ty,
    },
    UnwrapRes {
        dst: Reg,
        src: Reg,
        ty: Ty,
    },
    PatRange {
        dst: Reg,
        src: Reg,
        ty: Ty,
        info: u32,
    },
    Get {
        dst: Reg,
        info: u32,
    },
    // --- control ---
    Jmp {
        t: u32,
    },
    JmpIf {
        c: Reg,
        t: u32,
    },
    JmpIfNot {
        c: Reg,
        t: u32,
    },
    /// Jumps if the word in `c` is (not) zero.
    JmpNZ {
        c: Reg,
        t: u32,
    },
    JmpZ {
        c: Reg,
        t: u32,
    },
    /// Jumps if the byte at `off` of the value in `c` is (not) zero.
    JmpByte {
        c: Reg,
        off: u32,
        t: u32,
        when: bool,
    },
    /// Jumps if the tag of the union value in `src` is (not) the one of the test; traps
    /// on a tag of no variant.
    JmpTag {
        src: Reg,
        test: u32,
        t: u32,
        when: bool,
    },
    /// Jumps back to the head of a loop, letting other threads run.
    JmpTick {
        t: u32,
    },
    /// A `JmpTick` to a `BrI`, fused: lets other threads run, then jumps to `exit` if the
    /// comparison holds and to `body` otherwise.
    TickBr {
        cmp: Cmp,
        a: Reg,
        b: Reg,
        k: IntK,
        exit: u32,
        body: u32,
    },
    /// Jumps in Fast builds (over the checks of `assert`).
    JmpFast {
        t: u32,
    },
    TrapConst {
        msg: u32,
    },
    /// Traps with the message in the string slice in `src`.
    TrapMsg {
        src: Reg,
        kind: TrapKind,
    },
    /// The end of the code of a deferred statement run when a flow passes through it.
    EndSeg,
    // --- calls ---
    Call {
        f: FnId,
        args: u32,
        out: Arg,
    },
    /// A call of the core library's `new` or `destroy`, which the machine does itself when
    /// the default heap allocator is current ([`heap`]) and otherwise makes as `Call` does.
    CallHeap {
        f: FnId,
        args: u32,
        out: Arg,
    },
    CallInd {
        callee: u32,
        cty: Ty,
        args: u32,
        out: u32,
    },
    Perform {
        eff: u32,
        op: u32,
        args: u32,
        out: u32,
    },
    Builtin {
        info: u32,
    },
    Write {
        info: u32,
        sink: Arg,
        args: u32,
    },
    /// `with … on` once its handlers are installed (`withs[info]`): runs the body on a
    /// fiber and drives it.
    WithOn {
        info: u32,
    },
    /// `resume k(v)`: the continuation and the value are `args[args]`.
    Resume {
        args: u32,
    },
    /// `cancel k`.
    Cancel {
        k: Arg,
    },
    /// `@fiber(f, arg, stack)`, with the operands `args[args]`.
    Fiber {
        args: u32,
        out: Arg,
    },
    Ret {
        src: Arg,
    },
    /// `break` to a loop or `with` of an enclosing function.
    ThrowBreak {
        levels: u32,
        label: LabelId,
        v: Arg,
    },
    /// `break` to the `with` that installed the running clause's handler.
    ThrowBreakInst {
        v: Arg,
    },
    ThrowContinue {
        levels: u32,
        label: LabelId,
    },
    /// `return` from the function `levels` up (a function literal or handler clause
    /// returning from the function that contains it).
    ThrowReturn {
        levels: u32,
        v: Arg,
    },
    // --- handlers ---
    PushCtx,
    PopCtx,
    RestoreCtx {
        depth: u32,
    },
    InstallLit {
        v: Reg,
        label: LabelId,
    },
    InstallDecl {
        info: u32,
        src: Reg,
        size: u32,
        label: LabelId,
    },
    MkClosure {
        dst: Reg,
        f: FnId,
        env: bool,
    },
    MkHandler {
        dst: Reg,
        h: HandlerId,
    },
}

/// A scope that flows from calls must pass through or may land in.
#[derive(Clone, Debug)]
pub struct Scope {
    /// The instructions it covers: `lo..hi`.
    pub lo: u32,
    pub hi: u32,
    pub kind: ScopeKind,
}

#[derive(Clone, Debug)]
pub enum ScopeKind {
    /// A deferred statement: its code runs from `seg` up to an `EndSeg`, in the handler
    /// context at depth `depth`.
    Defer { is_err: bool, seg: u32, depth: u32 },
    /// A loop (`cont` is where `continue` goes) or a `with` (`cont` unused).
    Label { label: LabelId, brk: u32, cont: u32, depth: u32, result: Arg, temps: Option<Reg> },
    /// The code of a deferred statement: a flow other than a trap may not leave it.
    Guard { span: Span },
}

/// A function's compiled code.
pub struct Code<'a> {
    pub ops: Vec<Op>,
    pub spans: Vec<Span>,
    pub nregs: u32,
    /// The frame slots an activation needs (for the locals not in registers).
    pub nslots: u32,
    /// The first registers of an activation: locals (zero) and constants.
    pub init: Vec<u64>,
    pub nvals: u32,
    pub params: Vec<ParamLoc>,
    pub scopes: Vec<Scope>,
    pub bytes: Vec<Bytes>,
    pub strs: Vec<StrLit>,
    pub msgs: Vec<String>,
    pub args: Vec<Box<[Arg]>>,
    /// Single operands of the instructions that have too many to hold them.
    pub operands: Vec<Arg>,
    /// The types that go with each argument list: those of a builtin's arguments, and those
    /// of the variadic arguments of a call of a variadic foreign function.
    pub arg_tys: Vec<Box<[Ty]>>,
    pub bins: Vec<BinInfo>,
    pub builtins: Vec<BuiltinInfo>,
    pub coercions: Vec<Coercion>,
    pub slices: Vec<SliceInfo>,
    pub tags: Vec<TagTest>,
    pub gets: Vec<GetInfo>,
    pub ranges: Vec<(num_bigint::BigInt, num_bigint::BigInt, bool)>,
    pub effects: Vec<EffectRef>,
    /// For each of `effects`, whether the operation is a `ctl` one.
    pub effect_ctl: Vec<bool>,
    pub decls: Vec<Box<[HandlerDeclId]>>,
    pub writes: Vec<&'a Expr>,
    pub withs: Vec<WithOn<'a>>,
    /// The body of a `with … on` (see [`compile::compile_sub`]): it runs in its function's
    /// frame, so a `return` to that frame is not this code's end.
    pub sub: bool,
}

/// A `with … on`: the code of its body, its label, and where its stack and its result are.
pub struct WithOn<'a> {
    pub code: Box<Code<'a>>,
    pub label: LabelId,
    pub stack: Arg,
    pub out: Arg,
}

/// The compiled form of each function, filled in on first call.
#[derive(Default)]
pub enum CodeSlot<'a> {
    #[default]
    Unknown,
    Ready(Rc<Code<'a>>),
    /// A foreign function: it has no body.
    Foreign,
}

impl<'a> Machine<'a> {
    /// The compiled code of `f`, compiling it on first use; `None` for a foreign function.
    #[inline]
    pub fn code_of(&mut self, f: FnId) -> Option<Rc<Code<'a>>> {
        match &self.vm_code[f.index()] {
            CodeSlot::Ready(c) => Some(c.clone()),
            CodeSlot::Foreign => None,
            CodeSlot::Unknown => self.compile_now(f),
        }
    }

    /// Like [`Self::code_of`] without taking a share of the code: compiled code is never
    /// dropped or replaced while the machine lives.
    #[inline(always)]
    pub fn code_ptr(&mut self, f: FnId) -> Option<*const Code<'a>> {
        match &self.vm_code[f.index()] {
            CodeSlot::Ready(c) => Some(Rc::as_ptr(c)),
            CodeSlot::Foreign => None,
            CodeSlot::Unknown => self.compile_now(f).map(|c| Rc::as_ptr(&c)),
        }
    }

    #[inline(never)]
    fn compile_now(&mut self, f: FnId) -> Option<Rc<Code<'a>>> {
        let def = &self.prog.fns[f.index()];
        if def.body.is_none() {
            self.vm_code[f.index()] = CodeSlot::Foreign;
            return None;
        }
        let c = compile(self, f);
        let name = def.name.as_str();
        if let Ok(d) = std::env::var("GAMMA_VM_DUMP")
            && (d == "*" || d == name)
        {
            match &c {
                Ok(c) => eprintln!("{}", c.dump(name)),
                Err(u) => eprintln!("== {} cannot be compiled ({}:{})", name, u.0, u.1),
            }
        }
        let c = Rc::new(c.unwrap_or_else(|u| compile::trap_code(self, Some(def), &u)));
        self.vm_code[f.index()] = CodeSlot::Ready(c.clone());
        Some(c)
    }
}

impl Code<'_> {
    /// Checks what the dispatch loop relies on without checking again: every jump lands on
    /// an instruction, and the registers that the instructions with unchecked register
    /// access name exist.
    pub fn valid(&self) -> bool {
        let n = self.ops.len() as u32;
        let nregs = self.nregs;
        // The last instruction stops the activation, so there is no running off the end.
        if !matches!(self.ops.last(), Some(Op::TrapConst { .. })) {
            return false;
        }
        for op in &self.ops {
            let ok = match *op {
                Op::Jmp { t }
                | Op::JmpIf { t, .. }
                | Op::JmpIfNot { t, .. }
                | Op::JmpNZ { t, .. }
                | Op::JmpZ { t, .. }
                | Op::JmpByte { t, .. }
                | Op::JmpTag { t, .. }
                | Op::JmpFast { t }
                | Op::JmpTick { t } => t < n,
                Op::BrI { a, b, t, .. } | Op::BrF { a, b, t, .. } => t < n && a < nregs && b < nregs,
                Op::TickBr { a, b, exit, body, .. } => exit < n && body < n && a < nregs && b < nregs,
                Op::Mov1 { dst, src } | Op::AddImm { dst, src, .. } | Op::NegF { dst, a: src, .. } => {
                    dst < nregs && src < nregs
                }
                Op::Imm { dst, .. } => dst < nregs,
                Op::AddI { dst, a, b, .. }
                | Op::SubI { dst, a, b, .. }
                | Op::MulI { dst, a, b, .. }
                | Op::CmpI { dst, a, b, .. }
                | Op::BinF { dst, a, b, .. }
                | Op::RotI { dst, a, b, .. }
                | Op::CmpF { dst, a, b, .. }
                | Op::AndW { dst, a, b }
                | Op::OrW { dst, a, b }
                | Op::XorW { dst, a, b } => dst < nregs && a < nregs && b < nregs,
                _ => true,
            };
            if !ok {
                return false;
            }
        }
        self.scopes.iter().all(|s| {
            s.lo <= s.hi
                && s.hi <= n
                && match s.kind {
                    ScopeKind::Label { brk, cont, .. } => brk < n && cont < n,
                    ScopeKind::Defer { seg, .. } => seg < n,
                    ScopeKind::Guard { .. } => true,
                }
        })
    }

    /// A listing of the code, for debugging the compiler (`GAMMA_VM_DUMP=name`).
    pub fn dump(&self, name: &str) -> String {
        let mut s = format!("== {} ({} regs, {} vals, params {:?})\n", name, self.nregs, self.nvals, self.params);
        for (i, op) in self.ops.iter().enumerate() {
            s.push_str(&format!("{:5}  {:?}\n", i, op));
        }
        for sc in &self.scopes {
            s.push_str(&format!("scope {}..{} {:?}\n", sc.lo, sc.hi, sc.kind));
        }
        s
    }
}
/// Instructions are copied and fetched all the time; keep large operands in side tables.
const _: () = assert!(std::mem::size_of::<Op>() <= 24);
