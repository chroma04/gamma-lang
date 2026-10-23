//! The abstract syntax tree produced by the parser.
//!
//! The tree is a faithful, unresolved representation of the source. Types are
//! expressions (spec §5), so type syntax appears as [`ExprKind`] variants; where
//! the grammar is ambiguous between a type and a value, the parser produces one
//! node and name resolution decides (spec §A.7).
//!
//! Every expression, pattern, statement and item carries a [`NodeId`] that is
//! unique within one parsed file set, so later passes can attach information
//! in side tables keyed by id.

use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::token::{FloatLit, IntLit, StrLit};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct NodeId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Ident {
    pub name: Symbol,
    pub span: Span,
}

/// One parsed source file.
#[derive(Clone, Debug)]
pub struct File {
    pub items: Vec<Item>,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Top-level declarations (§3.4, §A.1)
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Item {
    pub id: NodeId,
    pub span: Span,
    /// Text of the doc comments (`///`) directly preceding the item, joined by newlines.
    pub doc: Option<String>,
    pub attrs: Vec<Attribute>,
    pub is_pub: bool,
    pub kind: ItemKind,
}

#[derive(Clone, Debug)]
pub enum ItemKind {
    /// `name :: e`, `name: T : e`, and `#global name: T = e`.
    Binding(Binding),
    /// Top-level `when` (§3.6). Each branch holds items declared as if at top level.
    When(WhenItems),
    /// `handler E for T needs A { … }` (§12.6).
    Handler(HandlerDecl),
    /// `test "name" { … }` (§3.8), or `bench "name" { … }` (§3.9) if `bench`.
    Test { name: StrLit, name_span: Span, body: Block, bench: bool },
    /// A top-level intrinsic call, i.e. `@static_assert(…)` (§17.7).
    Intrinsic(Expr),
}

#[derive(Clone, Debug)]
pub struct WhenItems {
    pub cond: Expr,
    pub then: Vec<Item>,
    /// `else when …` is represented as an `else` holding a single `When` item.
    pub else_: Option<Vec<Item>>,
    pub span: Span,
}

/// `#name` or `#name(args)` (§20.1).
#[derive(Clone, Debug)]
pub struct Attribute {
    pub name: Ident,
    /// `None` when written without parentheses.
    pub args: Option<Vec<Expr>>,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Bindings (§4.1)
// ---------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BindingKind {
    /// `x :: e` or `x: T : e` — immutable.
    Const,
    /// `x := e` or `x: T = e` — mutable.
    Var,
}

#[derive(Clone, Debug)]
pub struct Binding {
    pub id: NodeId,
    /// One name, or several for destructuring (`a, b := e`). `_` appears as an
    /// `Ident` named `_`.
    pub names: Vec<Ident>,
    /// The annotated type, if any.
    pub ty: Option<Box<Expr>>,
    pub kind: BindingKind,
    /// The initializer; `---` is [`ExprKind::Uninit`].
    pub init: Box<Expr>,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Statements (§8, §A.4)
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Block {
    pub id: NodeId,
    pub stmts: Vec<Stmt>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct Stmt {
    pub id: NodeId,
    pub kind: StmtKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum StmtKind {
    /// Local binding, including nested named functions (`f :: fn(…) {…}`).
    /// Attributes (e.g. `#inline` on a nested function) are kept on the binding statement.
    Binding {
        attrs: Vec<Attribute>,
        binding: Binding,
    },
    /// `p = e`, `p op= e`, `a, b = e`, `_ = e`.
    Assign {
        places: Vec<Expr>,
        op: AssignOp,
        value: Box<Expr>,
    },
    /// An expression statement (includes `return`, `break`, `for`, `if`, labeled `with`, …).
    Expr(Expr),
    /// `defer S` / `errdefer S`; the body is a block, expression or assignment statement.
    Defer {
        is_err: bool,
        body: Box<Stmt>,
    },
    When(WhenStmt),
    While {
        label: Option<Ident>,
        cond: Cond,
        body: Block,
    },
    /// `cancel k` (§14.5).
    Cancel(Expr),
    /// `pattern := value else fallback` with a refutable pattern (§4.5): binds the
    /// pattern's names for the rest of the block, or runs `fallback`, which must leave it.
    LetElse {
        pat: Pattern,
        value: Expr,
        fallback: Expr,
    },
    /// A nested block `{ … }` used as a statement.
    Block(Block),
}

#[derive(Clone, Debug)]
pub struct WhenStmt {
    pub cond: Expr,
    pub then: Block,
    pub else_: Option<WhenElse>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum WhenElse {
    Block(Block),
    When(Box<WhenStmt>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AssignOp {
    /// `=`
    Assign,
    /// `op=` for the given binary operator (only arithmetic, bitwise and shift ops).
    Compound(BinOp),
}

/// A condition of `if` or `while` (§8.2): parts joined by `and`.
#[derive(Clone, Debug)]
pub struct Cond {
    pub parts: Vec<CondPart>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum CondPart {
    /// `pattern := e`
    Bind {
        pat: Pattern,
        value: Expr,
    },
    Expr(Expr),
}

// ---------------------------------------------------------------------------
// Expressions (§7, §A.5) — including type syntax (§A.2)
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Expr {
    pub id: NodeId,
    pub kind: ExprKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum ExprKind {
    // --- literals and names ---
    Int(Box<IntLit>),
    Float(Box<FloatLit>),
    Str(Box<StrLit>),
    Bool(bool),
    Null,
    /// `---`, valid as an initializer, constructor field or argument (§4.4).
    Uninit,
    Ident(Ident),
    /// `_`. Only meaningful in assignment targets, `[_]T`, and patterns written
    /// on the right of `is` (§7.6); elsewhere it is rejected after parsing.
    Underscore,
    /// `.Name` — a union variant whose type comes from context (§5.11).
    DotIdent(Ident),

    /// `(e)`.
    Paren(Box<Expr>),
    /// `()`, `(a,)`, `(a, b)`, `(x: a, y: b)`. Also tuple *types* such as `(i32, u8)`.
    Tuple(Vec<TupleElem>),
    /// The elements of a tuple or array as an unnamed tuple. Never parsed: a spread `..e`
    /// passed as the whole of a call's rest arguments (§11.2) becomes this.
    Elements(Box<Expr>),

    // --- operators ---
    Unary {
        op: UnOp,
        operand: Box<Expr>,
    },
    Binary {
        op: BinOp,
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    /// `e else d` or `e else x => d` (§10.4).
    Else {
        lhs: Box<Expr>,
        capture: Option<Ident>,
        rhs: Box<Expr>,
    },

    // --- postfix ---
    /// `e?` (§10.3).
    OptUnwrap(Box<Expr>),
    /// `e!` with nothing type-like after it (§10.3).
    ResUnwrap(Box<Expr>),
    /// `lhs!rhs` in an expression position where `rhs` parsed as a type: either
    /// the error-union type `E!T` or an unwrap followed by postfix operators (§A.7 rule 3).
    Bang {
        lhs: Box<Expr>,
        rhs: Box<Expr>,
    },
    /// `e^` (§5.5).
    Deref(Box<Expr>),
    /// `f(args)`: call, construction, conversion or generic type application (§7.11).
    Call {
        callee: Box<Expr>,
        args: Vec<Arg>,
        trailing_comma: bool,
    },
    /// `a[i]`.
    Index {
        base: Box<Expr>,
        index: Box<Expr>,
    },
    /// `a[lo..hi]`, `a[lo..=hi]`, `a[lo..]`, `a[..hi]`, `a[..]`.
    Slice {
        base: Box<Expr>,
        lo: Option<Box<Expr>>,
        hi: Option<Box<Expr>>,
        inclusive: bool,
    },
    /// `e.name` — field, builtin method, method call callee, or module member.
    Field {
        base: Box<Expr>,
        name: Ident,
    },
    /// `e.0` — tuple element by position.
    TupleField {
        base: Box<Expr>,
        index: u32,
        index_span: Span,
    },

    // --- type syntax (§A.2) ---
    /// `?T`
    OptionalType(Box<Expr>),
    /// `^T` / `^const T`. `^fn(…)` (a function pointer type) is `PtrType` over `FnType`.
    PtrType {
        is_const: bool,
        inner: Box<Expr>,
    },
    /// `[]T` / `[]const T`
    SliceType {
        is_const: bool,
        inner: Box<Expr>,
    },
    /// `[^]T` / `[^]const T`
    ManyPtrType {
        is_const: bool,
        inner: Box<Expr>,
    },
    /// `[N]T`, or `[_]T` when `len` is `None` (only in array constructors).
    ArrayType {
        len: Option<Box<Expr>>,
        elem: Box<Expr>,
    },
    /// `E!T` in a type position (§5.13).
    ErrorUnionType {
        err: Box<Expr>,
        ok: Box<Expr>,
    },
    /// `fn(params) -> R needs E` as a type.
    FnType(Box<FnSig>),
    /// `cont(T)` (§14.2). Parsed as an ordinary call `cont(T)` is also acceptable
    /// since `cont` is a predeclared name; the parser produces this node.
    ContType(Box<Expr>),
    Struct(Box<StructLit>),
    Union(Box<UnionLit>),
    Effect(Box<EffectLit>),
    /// Attributes written before a literal or type: `#packed struct {…}`,
    /// `#bits(u16) struct {…}`, `#c_union union {…}`, `#callconv(.C) ^fn(…)`.
    Attributed {
        attrs: Vec<Attribute>,
        inner: Box<Expr>,
    },

    // --- functions, handlers, modules ---
    Fn(Box<FnLit>),
    Handler(Box<HandlerLit>),
    /// `import "path"` (§3.2).
    Import(Box<StrLit>),
    /// `foreign "lib" { … }` (§19.2).
    Foreign(Box<ForeignBlock>),

    // --- control flow (all are expressions) ---
    If(Box<IfExpr>),
    Match(Box<MatchExpr>),
    With(Box<WithExpr>),
    For(Box<ForExpr>),
    /// A block used as an expression: match arm bodies, clause bodies, `else e => {…}`.
    Block(Box<Block>),
    Return(Option<Box<Expr>>),
    Break {
        label: Option<Ident>,
        value: Option<Box<Expr>>,
    },
    Continue {
        label: Option<Ident>,
    },
    /// `resume k(v)` / `resume k()` (§14.3).
    Resume {
        cont: Box<Expr>,
        value: Option<Box<Expr>>,
    },

    /// `@name` or `@name(args)` (§20.3).
    Intrinsic {
        name: Ident,
        args: Option<Vec<Arg>>,
    },

    /// Placeholder produced after a syntax error, so parsing can continue.
    Error,
}

#[derive(Clone, Debug)]
pub struct TupleElem {
    pub name: Option<Ident>,
    pub value: Expr,
}

#[derive(Clone, Debug)]
pub struct Arg {
    pub kind: ArgKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum ArgKind {
    /// `e` (including `---`).
    Positional(Expr),
    /// `name: e`.
    Named(Ident, Expr),
    /// `..e` (§11.4).
    Spread(Expr),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UnOp {
    /// `-e`
    Neg,
    /// `~e`
    BitNot,
    /// `&e`
    AddrOf,
    /// `!e` — make an error value (§10.2).
    MakeErr,
    /// `try e`
    Try,
    /// `not e`
    Not,
}

/// Overflow behavior of `+ - * <<` (§7.4.2).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Flavor {
    /// Plain: checked.
    Checked,
    /// `%`: wrapping.
    Wrap,
    /// `|`: saturating.
    Sat,
    /// `?`: optional result.
    Opt,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
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
    /// `..`
    Range,
    /// `..=`
    RangeIncl,
    Is,
    IsNot,
    In,
    NotIn,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
}

impl BinOp {
    pub fn as_str(self) -> &'static str {
        use BinOp::*;
        use Flavor::*;
        match self {
            Add(Checked) => "+",
            Add(Wrap) => "+%",
            Add(Sat) => "+|",
            Add(Opt) => "+?",
            Sub(Checked) => "-",
            Sub(Wrap) => "-%",
            Sub(Sat) => "-|",
            Sub(Opt) => "-?",
            Mul(Checked) => "*",
            Mul(Wrap) => "*%",
            Mul(Sat) => "*|",
            Mul(Opt) => "*?",
            Shl(Checked) => "<<",
            Shl(Wrap) => "<<%",
            Shl(Sat) => "<<|",
            Shl(Opt) => "<<?",
            Div => "/",
            Rem => "%",
            Shr => ">>",
            BitAnd => "&",
            BitOr => "|",
            BitXor => "~",
            Range => "..",
            RangeIncl => "..=",
            Is => "is",
            IsNot => "is not",
            In => "in",
            NotIn => "not in",
            Lt => "<",
            Le => "<=",
            Gt => ">",
            Ge => ">=",
            And => "and",
            Or => "or",
        }
    }
}

// ---------------------------------------------------------------------------
// Control-flow expressions
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct IfExpr {
    /// `if c1 {…} else if c2 {…} …` — at least one branch.
    pub branches: Vec<(Cond, Block)>,
    pub else_: Option<IfElse>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum IfElse {
    Block(Block),
    /// `else e => body` — error capture (§8.2); `body` is a block or expression.
    Capture {
        name: Ident,
        body: Box<Expr>,
    },
}

#[derive(Clone, Debug)]
pub struct MatchExpr {
    pub scrutinee: Expr,
    pub arms: Vec<Arm>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct Arm {
    /// Alternatives `p1, p2, …` (at least one).
    pub pats: Vec<Pattern>,
    pub guard: Option<Expr>,
    /// An expression or an [`ExprKind::Block`].
    pub body: Expr,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct WithExpr {
    pub label: Option<Ident>,
    pub handlers: Vec<Expr>,
    /// `on stack` (§14.1).
    pub on: Option<Expr>,
    pub body: Block,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct ForExpr {
    pub label: Option<Ident>,
    /// The comma-separated patterns of the head; more than one means a tuple pattern (§8.4).
    pub pats: Vec<Pattern>,
    pub iter: Expr,
    pub body: Block,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Functions (§11, §A.3)
// ---------------------------------------------------------------------------

/// A function signature, shared by function literals and function types.
#[derive(Clone, Debug)]
pub struct FnSig {
    pub params: Vec<Param>,
    /// True if the parameter list ends with `...` (foreign functions only, §19.2).
    pub variadic: bool,
    pub ret: Option<Expr>,
    /// `needs E1 + E2`; `None` when omitted, `Some(vec![])` for `needs not` (§19.2).
    pub effects: Option<Vec<Expr>>,
    pub span: Span,
}

/// One parameter group as written: `a, b: i32`, `T: type`, `N :: usize`,
/// `x: u8 = 0`, or (in literals with a known expected type) a bare `x`.
/// In function types the names are optional: `fn(i32, u8)` has groups with no names.
#[derive(Clone, Debug)]
pub struct Param {
    pub names: Vec<Ident>,
    /// Declared with `::` — a compile-time value parameter (§16.5).
    pub comptime: bool,
    /// Declared `name: ..T` — a rest parameter, which takes the remaining positional
    /// arguments as a tuple (§11.3).
    pub rest: bool,
    pub ty: Option<Expr>,
    pub default: Option<Expr>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct Constraint {
    /// `Eq`, `Ord`, `Num`, `Int`, `Neg`, `Float`, `Plain` (validated later).
    pub name: Ident,
    pub ty: Expr,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct FnLit {
    pub sig: FnSig,
    pub where_: Vec<Constraint>,
    pub body: FnBody,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum FnBody {
    Block(Block),
    /// `=> expr`
    Expr(Expr),
}

// ---------------------------------------------------------------------------
// Type literals (§5.10, §5.11, §12.1)
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct StructLit {
    /// `struct(P: T, …)` — generic parameters (§16.4).
    pub params: Option<Vec<Param>>,
    pub where_: Vec<Constraint>,
    pub members: Vec<StructMember>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum StructMember {
    Field(FieldDecl),
    When { cond: Expr, then: Vec<StructMember>, else_: Option<Vec<StructMember>>, span: Span },
}

#[derive(Clone, Debug)]
pub struct FieldDecl {
    pub attrs: Vec<Attribute>,
    pub is_pub: bool,
    pub is_using: bool,
    /// `a, b: T` declares several fields; `_` (padding in `#bits` structs) is an `Ident` named `_`.
    pub names: Vec<Ident>,
    pub ty: Expr,
    pub default: Option<Expr>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct UnionLit {
    /// `union(T)` — explicit tag type.
    pub tag_type: Option<Expr>,
    /// `union(P: type, …)` — generic parameters.
    pub params: Option<Vec<Param>>,
    pub where_: Vec<Constraint>,
    pub members: Vec<UnionMember>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum UnionMember {
    Variant(Variant),
    When { cond: Expr, then: Vec<UnionMember>, else_: Option<Vec<UnionMember>>, span: Span },
}

#[derive(Clone, Debug)]
pub struct Variant {
    pub name: Ident,
    pub payload: Option<Expr>,
    /// `Name = c` — explicit tag value.
    pub value: Option<Expr>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct EffectLit {
    pub params: Option<Vec<Param>>,
    pub ops: Vec<OpDecl>,
    pub span: Span,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OpKind {
    Fn,
    Esc,
    Ctl,
}

#[derive(Clone, Debug)]
pub struct OpDecl {
    pub name: Ident,
    pub kind: OpKind,
    pub params: Vec<Param>,
    pub ret: Option<Expr>,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Handlers (§12.5, §12.6)
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct HandlerLit {
    pub effect: Expr,
    pub clauses: Vec<Clause>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct HandlerDecl {
    /// `handler(T: type, …)`: parameters of a handler for a generic type (§12.6).
    pub params: Option<Vec<Param>>,
    pub effect: Expr,
    pub for_ty: Expr,
    pub effects: Option<Vec<Expr>>,
    pub clauses: Vec<Clause>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct Clause {
    pub name: Ident,
    pub params: Vec<Ident>,
    /// An expression or an [`ExprKind::Block`].
    pub body: Expr,
    pub span: Span,
}

// ---------------------------------------------------------------------------
// Foreign blocks (§19.2)
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct ForeignBlock {
    pub lib: StrLit,
    pub members: Vec<ForeignMember>,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub struct ForeignMember {
    pub attrs: Vec<Attribute>,
    pub kind: ForeignMemberKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum ForeignMemberKind {
    /// `name :: fn(…) -> R needs E`
    Fn { name: Ident, sig: FnSig },
    /// `#global name: T` (the `#global` attribute is consumed, not kept in `attrs`).
    Global { name: Ident, ty: Expr },
}

// ---------------------------------------------------------------------------
// Patterns (§9, §A.6)
// ---------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Pattern {
    pub id: NodeId,
    pub kind: PatternKind,
    pub span: Span,
}

#[derive(Clone, Debug)]
pub enum PatternKind {
    /// `_`
    Wildcard,
    /// A bare identifier: always a binding (§9.2).
    Bind(Ident),
    /// An integer, character, string or bool literal, optionally negated:
    /// the expression is `Int`, `Str`, `Bool`, or `Unary { Neg, Int|Float }`.
    /// (Float literal patterns are rejected after parsing.)
    Lit(Box<Expr>),
    /// `null`
    Null,
    /// `lo..hi` / `lo..=hi`. Each bound is a literal (possibly negated) or a
    /// qualified constant path.
    Range { lo: Box<Expr>, hi: Box<Expr>, inclusive: bool },
    /// `!p` — the error of a result.
    Err(Box<Pattern>),
    /// A path with optional arguments:
    /// - `.V`, `.V(p…)` — `path` is [`ExprKind::DotIdent`];
    /// - `T.V`, `T.V(p…)`, `m.CONST` — `path` is an `Ident`/`Field` chain;
    /// - `S(f: p, …)` — struct pattern, `path` is an `Ident` or `Field` chain.
    ///
    /// Resolution decides whether it denotes a variant, a constant, or a struct.
    /// A bare identifier without arguments is never a `Path` (it is `Bind`).
    Path { path: Box<Expr>, args: Option<Vec<PatArg>> },
    /// `(p1, p2, …)` or `(a: p, b: q)`. `(p)` without a comma is just `p`.
    Tuple(Vec<PatArg>),
}

/// A sub-pattern in a variant, struct or tuple pattern: positional `p` or
/// named `name: p`. A bare identifier argument is parsed as a positional
/// `Bind`; in a struct pattern, resolution treats it as the shorthand `x: x`.
#[derive(Clone, Debug)]
pub struct PatArg {
    pub name: Option<Ident>,
    pub pat: Pattern,
    pub span: Span,
}

/// Allocates [`NodeId`]s. One generator is shared by all files of a compilation
/// so that ids are unique across files.
#[derive(Debug, Default)]
pub struct NodeIdGen {
    next: u32,
}

impl NodeIdGen {
    pub fn new() -> NodeIdGen {
        NodeIdGen::default()
    }

    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> NodeId {
        let id = NodeId(self.next);
        self.next += 1;
        id
    }
}
