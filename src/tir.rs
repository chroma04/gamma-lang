//! The typed intermediate representation (TIR).
//!
//! Semantic analysis lowers the syntax tree to TIR. TIR is consumed by the
//! interpreter (compile-time evaluation and `gamma run`) and by the C back end.
//!
//! Invariants established by semantic analysis:
//!
//! - Every expression has its final type: no inference variables remain.
//!   (Generic functions are lowered once with [`TyKind::Param`] types and
//!   instantiated by substitution before execution.)
//! - Names are resolved to locals, functions, constants or globals.
//! - Implicit coercions (§6.4) are explicit [`ExprKind::Coerce`] nodes;
//!   auto-dereference of field access and method receivers is an explicit
//!   [`ExprKind::Deref`] / [`ExprKind::AddrOf`].
//! - Operators are resolved to typed operations ([`BinOp`], [`UnOp`]).
//! - Struct construction lists every field in declaration order (defaults filled in).
//! - `for` over ranges, arrays and slices is lowered to [`ExprKind::Loop`];
//!   `for` over an iterator call is lowered to a `with` installing a handler.
//! - Method calls are ordinary calls; untyped constants are inlined as constants.
//!
//! [`TyKind::Param`]: crate::types::TyKind::Param

use crate::source::Span;
use crate::symbol::Symbol;
use crate::types::{AdtId, EffectId, EffectRef, EffectSet, Ty};
use num_bigint::BigInt;
use std::collections::HashMap;
use std::rc::Rc;

macro_rules! id_type {
    ($($(#[$m:meta])* $name:ident;)*) => {$(
        $(#[$m])*
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
        pub struct $name(pub u32);
        impl $name {
            pub fn index(self) -> usize { self.0 as usize }
        }
    )*};
}

id_type! {
    /// A function body: a named function, a function literal, or a handler clause.
    FnId;
    /// A local variable or parameter, unique within its [`FnDef`].
    LocalId;
    /// A loop or `with` that `break`/`continue` can target, unique within its [`FnDef`].
    LabelId;
    /// A top-level constant whose value is computed at compile time.
    ConstId;
    /// A `#global` variable.
    GlobalId;
    /// A handler literal, unique within the program.
    HandlerId;
    /// A handler declaration `handler E for T`.
    HandlerDeclId;
}

/// A whole program, ready to run.
#[derive(Debug, Default)]
pub struct Program {
    pub fns: Vec<FnDef>,
    pub consts: Vec<ConstDef>,
    pub globals: Vec<GlobalDef>,
    pub handlers: Vec<HandlerLit>,
    pub handler_decls: Vec<HandlerDecl>,
    /// Definitions of the (instantiated) nominal types used by the program.
    pub adts: HashMap<AdtId, AdtDef>,
    /// The entry point, for executables.
    pub main: Option<FnId>,
    /// The predeclared effects whose default handlers the runtime provides (§12.9).
    pub ambient: Ambient,
}

/// Ids of the predeclared ambient effects, when the program uses them.
#[derive(Debug, Default, Clone)]
pub struct Ambient {
    /// `Out { write_out: fn(bytes: []const u8) }`
    pub out: Option<EffectId>,
    /// `Err { write_err: fn(bytes: []const u8) }`
    pub err: Option<EffectId>,
    /// `Env { args, var, exit }` (operation indices 0, 1, 2)
    pub env: Option<EffectId>,
    /// `Alloc { current: fn() -> Allocator }`
    pub alloc: Option<EffectId>,
}

impl Program {
    pub fn func(&self, id: FnId) -> &FnDef {
        &self.fns[id.index()]
    }
    pub fn adt(&self, id: AdtId) -> &AdtDef {
        &self.adts[&id]
    }
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct FnDef {
    pub name: Symbol,
    pub span: Span,
    /// Parameters, in order. Each is also an entry in `locals`.
    pub params: Vec<LocalId>,
    pub locals: Vec<LocalDecl>,
    pub ret: Ty,
    pub effects: EffectSet,
    /// For function literals and handler clauses that capture locals: the
    /// function whose frame they capture (by reference). `None` for top-level
    /// and nested named functions, which capture nothing (§11.8).
    pub parent: Option<FnId>,
    /// What kind of body this is (affects how `return` and `break` behave).
    pub kind: FnKind,
    /// The body; its type is `ret` (or `never`). `None` for foreign functions.
    pub body: Option<Expr>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FnKind {
    /// A top-level or nested named function.
    Named,
    /// A function literal (second-class if it captures, §11.6).
    Literal,
    /// A clause of a handler literal or handler declaration. Its parameters are
    /// the operation's arguments (preceded by `self` for declarations, followed
    /// by the continuation for `ctl` operations).
    Clause,
    /// A function declared in a `foreign` block.
    Foreign { lib: Symbol, link_name: Symbol },
}

#[derive(Clone, Debug)]
pub struct LocalDecl {
    pub name: Symbol,
    pub ty: Ty,
    pub mutable: bool,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Data definitions
// ---------------------------------------------------------------------------

/// A struct or union definition, after generic instantiation.
#[derive(Clone, Debug)]
pub struct AdtDef {
    pub name: Symbol,
    pub kind: AdtKind,
}

#[derive(Clone, Debug)]
pub enum AdtKind {
    Struct {
        fields: Vec<FieldDef>,
        packed: bool,
        align: Option<u64>,
        bits: Option<Ty>,
    },
    Union {
        tag: Ty,
        variants: Vec<VariantDef>,
    },
    /// `#c_union`: every field at offset 0, no tag.
    CUnion {
        fields: Vec<FieldDef>,
    },
}

#[derive(Clone, Debug)]
pub struct FieldDef {
    pub name: Symbol,
    pub ty: Ty,
    pub is_pub: bool,
    /// `#align(n)` on the field.
    pub align: Option<u64>,
}

#[derive(Clone, Debug)]
pub struct VariantDef {
    pub name: Symbol,
    pub payload: Option<Ty>,
    /// The tag value (fits the union's tag type).
    pub tag: BigInt,
}

#[derive(Debug)]
pub struct ConstDef {
    pub name: Symbol,
    pub ty: Ty,
    pub span: Span,
    /// The initializer, evaluated once at compile time.
    pub init: Expr,
    /// The value, once evaluated.
    pub value: Option<ConstValue>,
}

/// A compile-time value in target layout. Pointers inside it point into static
/// data and are recorded as relocations, since static data has no fixed address
/// until the program is loaded or linked.
#[derive(Clone, Debug, PartialEq)]
pub struct ConstValue {
    pub ty: Ty,
    pub bytes: Vec<u8>,
    pub relocs: Vec<Reloc>,
}

/// A pointer stored at `offset` within a [`ConstValue`]: the address of
/// `target` plus `addend` bytes.
#[derive(Clone, Debug, PartialEq)]
pub struct Reloc {
    pub offset: u64,
    pub target: StaticData,
    pub addend: u64,
}

#[derive(Clone, Debug, PartialEq)]
pub enum StaticData {
    /// Read-only bytes such as a string literal's contents (a C string's
    /// stored bytes include its NUL terminator).
    Bytes(Rc<[u8]>),
    /// The storage of another constant.
    Const(ConstId),
    /// The code address of a function (for function pointers).
    Fn(FnId),
}

#[derive(Debug)]
pub struct GlobalDef {
    pub name: Symbol,
    pub ty: Ty,
    pub span: Span,
    /// `None` for foreign globals.
    pub init: Option<Expr>,
    pub thread_local: bool,
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

/// A handler literal (§12.5): one clause per operation of the effect, each a
/// [`FnDef`] of kind `Clause` whose `parent` is the function containing the literal.
#[derive(Debug)]
pub struct HandlerLit {
    pub effect: EffectRef,
    /// Indexed by operation number.
    pub clauses: Vec<FnId>,
}

/// A handler declaration `handler E for T` (§12.6). Each clause takes `self: T` first.
#[derive(Debug)]
pub struct HandlerDecl {
    pub effect: EffectRef,
    pub for_ty: Ty,
    pub clauses: Vec<FnId>,
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Expr {
    pub ty: Ty,
    pub kind: ExprKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum ExprKind {
    // --- constants ---
    /// An integer constant of integer type `ty` (fits the type).
    Int(BigInt),
    /// A float constant of float type `ty`, already rounded to that type; stored as f64
    /// (exact for f16 and f32 values).
    Float(f64),
    /// An untyped float literal's exact value. Exists only while semantic analysis
    /// is inferring literal types; never present in a finished [`Program`].
    UntypedFloat(Rc<num_rational::BigRational>),
    Bool(bool),
    /// A string literal: a `string` (slice) pointing at static bytes.
    Str(Rc<[u8]>),
    /// A C string literal: a `cstring` pointing at static, NUL-terminated bytes
    /// (the stored bytes exclude the terminator).
    CStr(Rc<[u8]>),
    /// The absent value of the optional type `ty`.
    Null,
    /// `---`: unspecified contents (plain-data `ty` only).
    Uninit,
    /// A reference to a top-level constant's value.
    Const(ConstId),

    // --- places ---
    /// A local of the current function.
    Local(LocalId),
    /// A local of an enclosing function, `levels` frames up the capture chain (≥ 1).
    Upvar {
        levels: u32,
        local: LocalId,
    },
    /// A `#global` (performs `Sys`).
    Global(GlobalId),
    /// `p^`: the place a pointer points to.
    Deref(Box<Expr>),
    /// Field `index` of a struct or tuple (place if `base` is a place).
    Field {
        base: Box<Expr>,
        index: u32,
    },
    /// The payload of a union value known to be variant `variant` (used after a
    /// successful pattern test only; not checked).
    VariantPayload {
        base: Box<Expr>,
        variant: u32,
    },
    /// `a[i]` on an array (place or value), slice or many-pointer. `checked` is true
    /// for arrays and slices (bounds checked, §18.2).
    Index {
        base: Box<Expr>,
        index: Box<Expr>,
        checked: bool,
    },
    /// `a[lo..hi]` giving a slice. Missing bounds are 0 and `len`.
    /// `base` is a place of array type, a slice, or a many-pointer.
    Slice {
        base: Box<Expr>,
        lo: Option<Box<Expr>>,
        hi: Option<Box<Expr>>,
        inclusive: bool,
    },
    /// `s.len` / `s.ptr` of a slice value.
    SliceLen(Box<Expr>),
    SlicePtr(Box<Expr>),

    // --- values ---
    /// `&place` (the place is never a temporary, §7.2).
    AddrOf(Box<Expr>),
    /// Address of a temporary holding the value of `expr`, alive until the end of the
    /// enclosing statement (used for `^const T` method receivers, §7.10).
    AddrOfTemp(Box<Expr>),
    /// A named function or non-capturing literal used as a value (function pointer or
    /// second-class function value).
    FnRef(FnId),
    /// A capturing function literal: a second-class value pairing `FnId` with the
    /// current frame (and the current handler context, §12.7).
    Closure(FnId),
    /// A handler literal value, capturing the current frame and handler context.
    HandlerRef(HandlerId),
    Unary {
        op: UnOp,
        operand: Box<Expr>,
    },
    Binary {
        op: BinOp,
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    /// Short-circuit `and` / `or`.
    Logic {
        is_and: bool,
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    Coerce {
        kind: Coercion,
        expr: Box<Expr>,
    },
    /// An explicit conversion or builtin method / intrinsic.
    Builtin {
        op: Builtin,
        args: Vec<Expr>,
    },
    Call {
        callee: Callee,
        args: Vec<Expr>,
    },
    /// Construct a struct (fields in declaration order) or tuple (elements in order).
    Aggregate(Vec<Expr>),
    /// `[N]T(e1, …)`.
    Array(Vec<Expr>),
    /// `[N]T(fill: x)`.
    ArrayFill(Box<Expr>),
    /// Union variant construction.
    Variant {
        variant: u32,
        payload: Option<Box<Expr>>,
    },
    /// `!e`: a result in the error state.
    MakeErr(Box<Expr>),
    /// `x?` / `x!`: checked unwrap (traps when absent / error, §10.3).
    Unwrap {
        expr: Box<Expr>,
        is_result: bool,
    },
    /// `x is y` (structural equality, §7.6); `negated` for `is not`.
    Eq {
        lhs: Box<Expr>,
        rhs: Box<Expr>,
        negated: bool,
    },
    /// `x in c` for ranges, arrays/slices and inline tuples (§7.7).
    In {
        elem: Box<Expr>,
        container: Box<Expr>,
        negated: bool,
    },

    // --- control flow ---
    Block(Box<Block>),
    /// `if` with parts joined by `and`; binding parts are pattern matches (§8.2).
    If {
        cond: Vec<CondPart>,
        then: Box<Expr>,
        else_: Option<Box<Expr>>,
    },
    /// `match` (§8.6); arms are tried in order.
    Match {
        scrutinee: Box<Expr>,
        arms: Vec<Arm>,
    },
    /// `x else d`; with `capture`, the error value is bound to that local in `rhs`.
    Else {
        lhs: Box<Expr>,
        capture: Option<LocalId>,
        rhs: Box<Expr>,
    },
    /// `try x`: unwraps or returns the absent/error value (lifted via `lift`) from the
    /// function `levels` frames up (0 = the current one).
    Try {
        expr: Box<Expr>,
        lift: Option<Lift>,
        levels: u32,
    },
    /// A loop: `while`, or a lowered `for`. Each iteration checks `cond` (all parts
    /// must succeed; binding parts bind for the body), then runs `body`; after the body
    /// completes normally or by `continue`, `step` (if any) runs, then the next iteration
    /// begins. `break` leaves the loop without running `step`. The loop's value is `()`.
    Loop {
        label: LabelId,
        cond: Vec<CondPart>,
        body: Box<Block>,
        step: Option<Box<Expr>>,
    },
    Break {
        target: Target,
        value: Option<Box<Expr>>,
    },
    Continue {
        target: Target,
    },
    /// `return` from the function `levels` frames up (0 = current).
    Return {
        levels: u32,
        value: Box<Expr>,
    },
    /// `with h1, h2 { body }` (§12.7); `label` is the target of `break` from clauses.
    With {
        label: LabelId,
        handlers: Vec<HandlerValue>,
        on: Option<Box<Expr>>,
        body: Box<Expr>,
    },
    /// `resume k(v)`.
    Resume {
        cont: Box<Expr>,
        value: Box<Expr>,
    },
    /// `discard k`.
    Discard(Box<Expr>),
    /// `trap(msg)` or a failed `assert`/`@unreachable()`: stops the program.
    Trap {
        message: Box<Expr>,
    },
    /// `@write(sink, template, args)`: `pieces` are the literal parts of the template,
    /// interleaved with `args` (`pieces.len() == args.len() + 1`). Each argument is
    /// formatted by its type (§21.6.1) and the output passed to the sink in pieces.
    Write {
        sink: WriteSink,
        pieces: Vec<Rc<[u8]>>,
        args: Vec<Expr>,
    },
}

/// Where `@write` sends its output.
#[derive(Clone, Debug)]
pub enum WriteSink {
    /// Call a second-class function value `fn(bytes: []const u8)`.
    Fn(Box<Expr>),
    /// Perform an operation taking `[]const u8` (e.g. `Out.write_out` for `print`).
    Perform { effect: EffectRef, op: u32 },
}

/// The target of a `break` or `continue`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Target {
    /// A loop or `with` in the function `levels` frames up the capture chain
    /// (0 = the current function body).
    Label { levels: u32, label: LabelId },
    /// The `with` that installed the handler whose clause is running (§12.7.1).
    InstallingWith,
}

#[derive(Clone, Debug)]
pub struct Block {
    pub stmts: Vec<Stmt>,
    /// The value of the block; `None` means `()` (or `never` if the last statement diverges).
    pub tail: Option<Expr>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum Stmt {
    /// Initialize a local (`init` is `None` only for `---` of plain data, which
    /// sema represents as `Some(Uninit)`; kept optional for synthesized locals).
    Let {
        local: LocalId,
        init: Option<Expr>,
    },
    /// Bind several locals from a tuple value (destructuring, §4.5). `None` entries discard.
    LetTuple {
        locals: Vec<Option<LocalId>>,
        init: Expr,
    },
    /// `place = value` (compound assignments are lowered to this with a single
    /// evaluation of the place's sub-expressions via temporaries).
    Assign {
        place: Expr,
        value: Expr,
    },
    /// `a, b = (x, y)`: all values computed before any store.
    AssignTuple {
        places: Vec<Option<Expr>>,
        value: Expr,
    },
    Expr(Expr),
    Defer {
        is_err: bool,
        body: Expr,
    },
}

#[derive(Clone, Debug)]
pub enum CondPart {
    Bool(Expr),
    /// Matches `value` against `pat`, binding its locals for later parts and `then`.
    Match {
        pat: Pat,
        value: Expr,
    },
}

#[derive(Clone, Debug)]
pub struct Arm {
    pub pat: Pat,
    pub guard: Option<Expr>,
    pub body: Expr,
}

#[derive(Clone, Debug)]
pub enum Callee {
    /// A direct call to a known function.
    Fn(FnId),
    /// A call through a function pointer or second-class function value.
    Indirect(Box<Expr>),
    /// Performing operation `op` (index into the effect's operations) of `effect`.
    Perform { effect: EffectRef, op: u32 },
}

/// How to convert an error value when `try` propagates it (§10.6).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Lift {
    /// Wrap the error in variant `variant` of the function's error union.
    Variant(u32),
}

/// A handler installed by `with`.
#[derive(Clone, Debug)]
pub enum HandlerValue {
    /// A handler literal value (evaluates `expr`, of handler type).
    Literal(Expr),
    /// A value of type `T` installing every handler declared `for T` (§12.6).
    Decl { decls: Vec<HandlerDeclId>, value: Expr },
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Pat {
    pub ty: Ty,
    pub kind: PatKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum PatKind {
    Wild,
    /// Binds the matched value to a local (a copy).
    Bind(LocalId),
    /// Matches a value `is`-equal to the constant (integers, bools, strings).
    Const(Box<Expr>),
    /// Integer range.
    Range {
        lo: BigInt,
        hi: BigInt,
        inclusive: bool,
    },
    /// The absent optional.
    Null,
    /// A present optional whose value matches.
    Some(Box<Pat>),
    /// A successful result whose value matches.
    Ok(Box<Pat>),
    /// A failed result whose error matches.
    Err(Box<Pat>),
    /// A union value of the given variant; payload pattern if any.
    Variant {
        variant: u32,
        payload: Option<Box<Pat>>,
    },
    /// Struct fields or tuple elements by index; unlisted ones are ignored.
    Fields(Vec<(u32, Pat)>),
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

pub use crate::syntax::ast::Flavor;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UnOp {
    /// Integer negation (checked) or float negation, chosen by the operand type.
    Neg,
    /// Bitwise complement (integers and `#bits` structs).
    BitNot,
    /// Logical not.
    Not,
}

/// Binary operators on numbers. The operand type (`lhs.ty`) selects integer or float
/// semantics; arrays apply the operator elementwise (§7.4.6).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BinOp {
    Add(Flavor),
    Sub(Flavor),
    Mul(Flavor),
    Shl(Flavor),
    Div,
    Rem,
    Shr,
    BitAnd,
    BitOr,
    BitXor,
    Lt,
    Le,
    Gt,
    Ge,
}

/// Implicit coercions (§6.4), made explicit.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Coercion {
    /// Integer widening (including to `usize`/`isize`).
    IntWiden,
    /// Float widening.
    FloatWiden,
    /// `T` → `?T`.
    WrapOptional,
    /// `T` → `E!T`.
    WrapOk,
    /// `E!T` → `F!T` lifting the error into variant `variant` of `F` (§10.6).
    LiftErr { variant: u32 },
    /// `never` → any type (the operand never produces a value).
    FromNever,
    /// Pointer coercions of §5.5 that do not change the representation
    /// (const-adding, kind change, to `rawptr`, embedding `^Outer` → `^S`).
    PtrToPtr,
    /// `^[N]T` → `[]T` (or the const forms).
    ArrayPtrToSlice,
    /// Tuple name erasure / naming (§5.9): same layout.
    TupleRename,
    /// A named function to a function pointer or second-class value.
    FnToPtr,
    /// A function pointer to a pointer type with a larger effect set (§12.3).
    FnPtrWiden,
    /// Applies the inner coercion inside an optional (`?^T` → `?^const T`).
    InOptional,
    /// A coercion from an unresolved literal type, decided once inference is done.
    /// Exists only during semantic analysis; never present in a finished [`Program`].
    Unresolved,
}

/// Explicit conversions, builtin methods (§21.3) and intrinsics (§20.3).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Builtin {
    /// `T(x)` lossless conversion (§6.5): integer→integer/float, float→float,
    /// bool→integer, enum→integer, `#bits` ↔ integer. Target is the expression type.
    Convert,
    /// `x.trunc(T)` (integer: keep low bits; float: toward zero, checked).
    Trunc,
    /// `x.round(T)`: float→integer (checked), integer→float, float→narrower float.
    Round,
    Floor,
    Ceil,
    /// `x.checked(T)` → `?T`.
    Checked,
    /// `x.sat(T)`.
    Sat,
    /// `x.bits(T)`: reinterpret bytes.
    Bits,
    /// `p.cast(P)` (checked alignment).
    Cast,
    /// `p.addr()`.
    Addr,
    /// `n.to_ptr(P)` → `?P`.
    ToPtr,
    /// `u.tag()`.
    Tag,
    /// `U.from_tag(x)` → `?U`.
    FromTag,
    Min,
    Max,
    Clamp,
    Abs,
    AbsDiff,
    Pow(Flavor),
    IsNan,
    IsInf,
    IsFinite,
    SignBit,
    Sqrt,
    Fma,
    Clz,
    Ctz,
    Popcount,
    Bswap,
    Bitreverse,
    /// `a.get(i)` → `?T`.
    Get,
    /// `a.get_ptr(i)` → `?^T`.
    GetPtr,
    /// `s.cstr_len()`.
    CstrLen,
    /// `assert(cond, msg, loc)`: checked (removed in Fast builds).
    Assert,
    /// `@hash(x, seed)`.
    Hash,
}

// ---------------------------------------------------------------------------
// Traversal
// ---------------------------------------------------------------------------

impl Expr {
    /// Calls `f` on each direct sub-expression, including those inside
    /// statements, conditions, match arms and patterns.
    pub fn for_each_child(&self, f: &mut dyn FnMut(&Expr)) {
        match &self.kind {
            ExprKind::Deref(a)
            | ExprKind::AddrOf(a)
            | ExprKind::AddrOfTemp(a)
            | ExprKind::SliceLen(a)
            | ExprKind::SlicePtr(a)
            | ExprKind::MakeErr(a)
            | ExprKind::Discard(a)
            | ExprKind::ArrayFill(a)
            | ExprKind::Unary { operand: a, .. }
            | ExprKind::Coerce { expr: a, .. }
            | ExprKind::Unwrap { expr: a, .. }
            | ExprKind::Try { expr: a, .. }
            | ExprKind::Field { base: a, .. }
            | ExprKind::VariantPayload { base: a, .. }
            | ExprKind::Return { value: a, .. }
            | ExprKind::Trap { message: a } => f(a),
            ExprKind::Index { base, index, .. } => {
                f(base);
                f(index);
            }
            ExprKind::Slice { base, lo, hi, .. } => {
                f(base);
                if let Some(x) = lo.as_deref() {
                    f(x);
                }
                if let Some(x) = hi.as_deref() {
                    f(x);
                }
            }
            ExprKind::Binary { lhs, rhs, .. }
            | ExprKind::Logic { lhs, rhs, .. }
            | ExprKind::Eq { lhs, rhs, .. }
            | ExprKind::Else { lhs, rhs, .. } => {
                f(lhs);
                f(rhs);
            }
            ExprKind::In { elem, container, .. } => {
                f(elem);
                f(container);
            }
            ExprKind::Builtin { args, .. } | ExprKind::Aggregate(args) | ExprKind::Array(args) => {
                args.iter().for_each(f)
            }
            ExprKind::Call { callee, args } => {
                if let Callee::Indirect(c) = callee {
                    f(c);
                }
                args.iter().for_each(f);
            }
            ExprKind::Variant { payload, .. } => {
                if let Some(x) = payload.as_deref() {
                    f(x);
                }
            }
            ExprKind::Block(b) => b.for_each_child(f),
            ExprKind::If { cond, then, else_ } => {
                cond.iter().for_each(|c| c.for_each_child(f));
                f(then);
                if let Some(x) = else_.as_deref() {
                    f(x);
                }
            }
            ExprKind::Match { scrutinee, arms } => {
                f(scrutinee);
                for a in arms.iter() {
                    a.pat.for_each_child(f);
                    if let Some(g) = a.guard.iter().next() {
                        f(g);
                    }
                    f(&a.body);
                }
            }
            ExprKind::Loop { cond, body, step, .. } => {
                cond.iter().for_each(|c| c.for_each_child(f));
                body.for_each_child(f);
                if let Some(x) = step.as_deref() {
                    f(x);
                }
            }
            ExprKind::Break { value, .. } => {
                if let Some(x) = value.as_deref() {
                    f(x);
                }
            }
            ExprKind::With { handlers, on, body, .. } => {
                for h in handlers.iter() {
                    match h {
                        HandlerValue::Literal(x) | HandlerValue::Decl { value: x, .. } => f(x),
                    }
                }
                if let Some(x) = on.as_deref() {
                    f(x);
                }
                f(body);
            }
            ExprKind::Resume { cont, value } => {
                f(cont);
                f(value);
            }
            ExprKind::Write { sink, args, .. } => {
                if let WriteSink::Fn(x) = sink {
                    f(x);
                }
                args.iter().for_each(f);
            }
            _ => {}
        }
    }
}

impl Block {
    /// Calls `f` on each expression directly inside the block's statements and tail.
    pub fn for_each_child(&self, f: &mut dyn FnMut(&Expr)) {
        for s in self.stmts.iter() {
            match s {
                Stmt::Let { init, .. } => {
                    if let Some(x) = init.iter().next() {
                        f(x);
                    }
                }
                Stmt::LetTuple { init, .. } => f(init),
                Stmt::Assign { place, value } => {
                    f(place);
                    f(value);
                }
                Stmt::AssignTuple { places, value } => {
                    places.iter().flatten().for_each(&mut *f);
                    f(value);
                }
                Stmt::Expr(e) | Stmt::Defer { body: e, .. } => f(e),
            }
        }
        if let Some(t) = self.tail.iter().next() {
            f(t);
        }
    }
}

impl CondPart {
    pub fn for_each_child(&self, f: &mut dyn FnMut(&Expr)) {
        match self {
            CondPart::Bool(e) => f(e),
            CondPart::Match { pat, value } => {
                pat.for_each_child(f);
                f(value);
            }
        }
    }
}

impl Pat {
    /// Calls `f` on the constant expressions inside the pattern.
    pub fn for_each_child(&self, f: &mut dyn FnMut(&Expr)) {
        match &self.kind {
            PatKind::Const(e) => f(e),
            PatKind::Some(p) | PatKind::Ok(p) | PatKind::Err(p) => p.for_each_child(f),
            PatKind::Variant { payload: Some(p), .. } => p.for_each_child(f),
            PatKind::Fields(fs) => fs.iter().for_each(|(_, p)| p.for_each_child(f)),
            _ => {}
        }
    }
}

impl Expr {
    /// Calls `f` on each direct sub-expression, including those inside
    /// statements, conditions, match arms and patterns.
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        match &mut self.kind {
            ExprKind::Deref(a)
            | ExprKind::AddrOf(a)
            | ExprKind::AddrOfTemp(a)
            | ExprKind::SliceLen(a)
            | ExprKind::SlicePtr(a)
            | ExprKind::MakeErr(a)
            | ExprKind::Discard(a)
            | ExprKind::ArrayFill(a)
            | ExprKind::Unary { operand: a, .. }
            | ExprKind::Coerce { expr: a, .. }
            | ExprKind::Unwrap { expr: a, .. }
            | ExprKind::Try { expr: a, .. }
            | ExprKind::Field { base: a, .. }
            | ExprKind::VariantPayload { base: a, .. }
            | ExprKind::Return { value: a, .. }
            | ExprKind::Trap { message: a } => f(a),
            ExprKind::Index { base, index, .. } => {
                f(base);
                f(index);
            }
            ExprKind::Slice { base, lo, hi, .. } => {
                f(base);
                if let Some(x) = lo.as_deref_mut() {
                    f(x);
                }
                if let Some(x) = hi.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Binary { lhs, rhs, .. }
            | ExprKind::Logic { lhs, rhs, .. }
            | ExprKind::Eq { lhs, rhs, .. }
            | ExprKind::Else { lhs, rhs, .. } => {
                f(lhs);
                f(rhs);
            }
            ExprKind::In { elem, container, .. } => {
                f(elem);
                f(container);
            }
            ExprKind::Builtin { args, .. } | ExprKind::Aggregate(args) | ExprKind::Array(args) => {
                args.iter_mut().for_each(f)
            }
            ExprKind::Call { callee, args } => {
                if let Callee::Indirect(c) = callee {
                    f(c);
                }
                args.iter_mut().for_each(f);
            }
            ExprKind::Variant { payload, .. } => {
                if let Some(x) = payload.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Block(b) => b.for_each_child_mut(f),
            ExprKind::If { cond, then, else_ } => {
                cond.iter_mut().for_each(|c| c.for_each_child_mut(f));
                f(then);
                if let Some(x) = else_.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Match { scrutinee, arms } => {
                f(scrutinee);
                for a in arms.iter_mut() {
                    a.pat.for_each_child_mut(f);
                    if let Some(g) = a.guard.iter_mut().next() {
                        f(g);
                    }
                    f(&mut a.body);
                }
            }
            ExprKind::Loop { cond, body, step, .. } => {
                cond.iter_mut().for_each(|c| c.for_each_child_mut(f));
                body.for_each_child_mut(f);
                if let Some(x) = step.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::Break { value, .. } => {
                if let Some(x) = value.as_deref_mut() {
                    f(x);
                }
            }
            ExprKind::With { handlers, on, body, .. } => {
                for h in handlers.iter_mut() {
                    match h {
                        HandlerValue::Literal(x) | HandlerValue::Decl { value: x, .. } => f(x),
                    }
                }
                if let Some(x) = on.as_deref_mut() {
                    f(x);
                }
                f(body);
            }
            ExprKind::Resume { cont, value } => {
                f(cont);
                f(value);
            }
            ExprKind::Write { sink, args, .. } => {
                if let WriteSink::Fn(x) = sink {
                    f(x);
                }
                args.iter_mut().for_each(f);
            }
            _ => {}
        }
    }
}

impl Block {
    /// Calls `f` on each expression directly inside the block's statements and tail.
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        for s in self.stmts.iter_mut() {
            match s {
                Stmt::Let { init, .. } => {
                    if let Some(x) = init.iter_mut().next() {
                        f(x);
                    }
                }
                Stmt::LetTuple { init, .. } => f(init),
                Stmt::Assign { place, value } => {
                    f(place);
                    f(value);
                }
                Stmt::AssignTuple { places, value } => {
                    places.iter_mut().flatten().for_each(&mut *f);
                    f(value);
                }
                Stmt::Expr(e) | Stmt::Defer { body: e, .. } => f(e),
            }
        }
        if let Some(t) = self.tail.iter_mut().next() {
            f(t);
        }
    }
}

impl CondPart {
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        match self {
            CondPart::Bool(e) => f(e),
            CondPart::Match { pat, value } => {
                pat.for_each_child_mut(f);
                f(value);
            }
        }
    }
}

impl Pat {
    /// Calls `f` on the constant expressions inside the pattern.
    pub fn for_each_child_mut(&mut self, f: &mut dyn FnMut(&mut Expr)) {
        match &mut self.kind {
            PatKind::Const(e) => f(e),
            PatKind::Some(p) | PatKind::Ok(p) | PatKind::Err(p) => p.for_each_child_mut(f),
            PatKind::Variant { payload: Some(p), .. } => p.for_each_child_mut(f),
            PatKind::Fields(fs) => fs.iter_mut().for_each(|(_, p)| p.for_each_child_mut(f)),
            _ => {}
        }
    }
}
