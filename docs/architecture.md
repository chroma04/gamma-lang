# Compiler architecture

This document describes how the reference implementation of Gamma is organized:
the stages a program passes through, where each stage lives, and the invariants
that connect them. Parts marked *(planned)* are designed but not yet implemented.

## Pipeline

```
source text
  │  syntax::lexer          tokens with significant newlines (spec §2)
  ▼
tokens
  │  syntax::parser         abstract syntax tree (spec Appendix A)
  ▼
AST  (syntax::ast)
  │  sema                   name resolution, type checking, effect checking,
  │                         literal inference, exhaustiveness
  ▼
TIR                         typed intermediate representation
  ├─▶ interp                interpreter: compile-time evaluation and `gamma run`
  └─▶ cgen        (planned) C code generation → system C compiler → executable
```

### Why an interpreter *and* a code generator

Gamma requires compile-time evaluation of ordinary functions (spec §17): array
lengths, top-level constants and arguments for compile-time parameters are
computed by running Gamma code inside the compiler, with every checked operation
checked and undefined behavior detected. The interpreter that does this works on
the same typed IR as the code generator, so it is also used to run programs
directly (`gamma run`), which gives a fast development loop and a reference
semantics that the native back end is tested against.

### Why C as the native target

Generating C keeps the implementation portable and lets the system C compiler do
register allocation and optimization. The features that C cannot express
directly are provided by a small runtime library: stack switching for fibers
(spec §14), arithmetic on integers wider than 128 bits, and trap reporting. The
effect system compiles to *evidence passing* and *exit flags* (spec Appendix D),
which are plain C.

### Demand-driven semantic analysis

Type checking and compile-time evaluation depend on each other: checking
`buf: [BLOCK * 2]u8` needs the value of `BLOCK`, which may be computed by a
function that must itself be checked first. Semantic analysis is therefore
demand-driven: each top-level declaration is checked when it is first needed,
and a declaration that is requested while it is being checked is a dependency
cycle, reported with every declaration in it (spec §3.4).

## Source layout

| Path | Contents |
| --- | --- |
| `src/main.rs` | the `gamma` command-line tool |
| `src/fmt.rs` | the source formatter behind `gamma fmt` (token based, see `docs/contributing.md`) |
| `src/driver.rs` | `Session`: runs the stages for the CLI and tests |
| `src/source.rs` | source files, byte spans, line/column lookup |
| `src/diag.rs` | diagnostics and their rendering |
| `src/symbol.rs` | interned identifiers |
| `src/syntax/token.rs` | token definitions |
| `src/syntax/lexer.rs` | the lexer |
| `src/syntax/parser/` | the parser |
| `src/syntax/ast.rs` | the syntax tree |
| `src/syntax/ast_dump.rs` | stable textual rendering of the syntax tree |
| `src/types.rs` | semantic types (hash-consed `Ty`) |
| `src/layout.rs` | sizes, alignments and field offsets for a target |
| `src/float.rs` | correctly rounded conversion of exact literals to IEEE formats |
| `src/tir.rs` | the typed intermediate representation |
| `src/sema/` | semantic analysis (see below) |
| `src/interp/` | the interpreter (see below) |
| `src/cgen/` | the C back end (see below) |
| `runtime/` | the C runtime of compiled programs, embedded in the compiler |
| `lib/core/` | the parts of the core library written in Gamma, embedded in the compiler |
| `tests/conformance.rs` | the conformance test runner |
| `tests/conformance/` | conformance tests, grouped by area |

## Front end

### Lexer

The lexer turns UTF-8 source into tokens and decides which newlines are
significant (spec §2.8), so the parser sees a `Newline` token exactly where a
statement or declaration may end. It keeps a stack of open brackets: inside
`( )` and `[ ]` newlines are dropped; inside `{ }` and at top level they are kept
unless the previous token is a binary operator (or another token that cannot end
an expression) or the next line begins with `else`, `and`, `or` or `.name`. A
`{ }` that is the body of a `match` is tracked separately, because there every
line starts a new arm.

The lexer also forms the two-word operators `is not` and `not in`, records `///`
documentation comments, and computes the exact values of literals: integers as
unbounded naturals and floats as exact rationals, since literals are typed and
rounded later (spec §6.1).

### Parser

The parser is a hand-written recursive-descent parser with precedence climbing
for expressions. Gamma treats types as compile-time values, so the syntax tree
has a single expression type that also covers type syntax (`?T`, `[]T`,
`fn(…) -> R`, `struct { … }`, …).

A few constructs cannot be classified by syntax alone; the parser builds one node
and leaves the decision to name resolution (spec §A.7):

- `E!T` (error-union type) versus `x!` (unwrap) followed by more postfix
  operators: the parser attempts to parse a type after `!` and, if that
  succeeds, produces a `Bang` node.
- `?T(x)` and `[3]u8(1, 2, 3)`: a prefix type operator applies to the following
  postfix expression, including a call; whether that call is a generic type
  application (`?List(u8)`) or a construction is known only after resolution.
- The right operand of `is` is parsed as an expression and turned into a pattern
  when it contains `_` or a variant without its payload.

Other ambiguities are resolved syntactically: the first `{` after the head of an
`if`, `while`, `for`, `match`, `with` or `when` always begins its body, and a
label in `break L` is recognized because the parser tracks the labels of the
enclosing loops.

Every node carries a `NodeId`, unique within a compilation, so later stages can
attach types and other facts in side tables instead of mutating the tree.

## Diagnostics

All stages report problems to a shared `Diagnostics` collector. Messages are
lowercase and specific. The `--diag-format=short` flag prints one
`path:line:col: severity: message` line per diagnostic, which is what the test
suite matches against.

## Semantic analysis

Semantic analysis (`src/sema/`) resolves names, checks types and effects, and
lowers each function body to TIR.

| File | Responsibility |
| --- | --- |
| `mod.rs` | the `Sema` context, the entry point `analyze`, finding `main` |
| `modules.rs` | loading the root module and the core prelude; collecting top-level names |
| `decls.rs` | resolving top-level declarations on demand, with cycle detection |
| `adt.rs` | struct and union declarations |
| `ty_expr.rs` | evaluating type expressions |
| `untyped.rs` | exact arithmetic for untyped constants |
| `func.rs` | the per-function context, scopes, statements, loops |
| `expr.rs` | expressions, operators, coercions, optionals and results |
| `call.rs`, `methods.rs`, `construct.rs` | calls, method calls and builtin methods, construction |
| `pattern.rs`, `matching.rs`, `exhaust.rs` | patterns, `match`, binding conditions, exhaustiveness |
| `seq.rs` | slicing, `in`, and `for` over arrays and slices |
| `infer.rs`, `finalize.rs` | literal inference and the final pass over each body |
| `fnlit.rs` | function literals, captures and second-class rules |
| `effects.rs`, `handler_decl.rs` | performing operations, handler literals and declarations, `with`, `for` over iterators |
| `generics.rs` | constraints, inference of type arguments, monomorphization |
| `consteval.rs` | compile-time evaluation (checking the bodies it reaches first), `@static_assert` |
| `stack_size.rs` | `@stack_size`: the deepest chain of frames below a function, following statically known handlers and function arguments |
| `builtins.rs` | predeclared names built into the compiler (primitive type names) |

### The core prelude and the entry point

The core library is ordinary Gamma source in `lib/core/`, compiled into the
tool. The prelude (`prelude.gm`) is visible in every module: it declares the
ambient effects (`Out`, `Err`, `Env`, `Alloc`, `In`, `Clock`, `Rand`), the
standard iterators and allocation (`Allocator`, `new`, `make`, `free`, …). The containers live in
their own core modules, `list.gm` and `map.gm`; only their types and
constructors (`List`, `list`, `HashMap`, `map`, `map_with`) are re-exported to
the universe scope, and their other functions are reached as methods, which
resolve in the receiver type's module (§21.5).

Core modules see the prelude's private declarations and may use a few
host-facing intrinsics: `@heap_alloc` and `@heap_free`, and the system
services behind the default handlers (`@sys_read`, `@clock_now`,
`@clock_wall`, `@clock_sleep`, `@rand_fill`; `tir::SysOp`). Services that can
fail return a count or a negative error code that the prelude turns into an
`IoError`, so both back ends report errors identically. They may also use `is`
and `@hash` on unconstrained type parameters: the map's public constructors
require `Eq` exactly when the default hashing is used. The whole core library
is checked on every compilation, so mistakes in it surface immediately.

The program's entry is a synthesized function that calls `main` inside the
prelude's default handlers for the ambient effects it performs:
`with heap_allocator(), system_io() { main() }`, where `system_io()` handles
`In`, `Clock` and `Rand` through the intrinsics above. The ambient effects with
host-provided handlers (`Out`, `Err`, `Env`) are installed by the runtime
itself. Tests run under the prelude's test handlers instead (`run_test`): a
leak-checking allocator, empty input, a fake clock that `sleep` advances, and a
random generator with a fixed seed.

### Types

`Ty` values are interned: structurally equal types have equal handles, so type
identity is `==`. Nominal types (`struct`, `union`, `effect` literals) get a
fresh id each time a literal is evaluated. A declaration naming a struct is
marked resolved *before* its fields are, so a type can refer to itself through a
pointer; containing itself by value is reported as an infinitely sized type.

### Literal inference

Untyped literals get type variables (`TyKind::Infer`), which unify with each
other and with concrete types wherever the specification requires equal types
(the operands of most binary operators, assignment, comparison). Variables carry
a kind (integer or float literal), an "unsigned only" flag for indices and shift
amounts, and a fallback (`i32`, `u32`, `usize` or `f64`).

To keep the result independent of statement order (§6.1), a coercion whose
source is still an unresolved variable is not decided immediately. The checker
records it and emits a placeholder `Coercion::Unresolved`. After the whole body
has been checked, `finalize.rs`:

1. resolves each pending coercion: an unresolved variable takes the target type,
   or the type the target wraps for `?T` and `E!T`;
2. applies fallbacks to the remaining variables;
3. checks deferred obligations that need final types (lossless conversions,
   numeric and ordering requirements, unused values);
4. rewrites the body with final types, checks every literal against its type and
   rounds float literals exactly, and replaces each placeholder with the real
   coercion.

### Patterns and exhaustiveness

Patterns are checked against the scrutinee type and lowered to `tir::Pat`, with
the implicit unwrapping of optionals and results (§9.3) made explicit as `Some`,
`Ok` and `Err` patterns. Exhaustiveness and unreachable arms are decided by the
usefulness algorithm over constructor-deconstructed patterns (`exhaust.rs`).
Integer patterns are handled by splitting the type's range at the boundaries of
the ranges in a column, so `0..=127, 128..=255` is exhaustive for `u8`. When a
match is not exhaustive, the algorithm produces a witness, which is shown in the
error message.

## The interpreter

The interpreter (`src/interp/`) walks TIR directly. It runs programs for
`gamma run` and evaluates constants at compile time, so it implements the
language exactly as the target would: target integer sizes, layouts and float
semantics, with every checked operation checked.

**Memory.** The interpreter models a flat address space. Every allocation (a
local variable, a temporary whose address is taken, a string literal, a
constant, a heap block) gets a fresh, never-reused range of addresses, aligned
to at least 16 bytes and followed by an unused gap. Values are byte strings in
target layout and pointers are plain addresses, so pointer arithmetic, casts and
`bits` reinterpretation behave as in compiled code. Because addresses are never
reused and freed or dead allocations are remembered, accesses through dangling,
out-of-bounds, misaligned or null pointers are detected and reported as traps
(at compile time, as the undefined behavior §17.1 requires to be diagnosed).

**Control flow.** Evaluation returns either a value or a *flow*: `break`,
`continue`, `return`, an exit to a `with`, or a trap. Loops, `with` expressions
and function calls consume the flows aimed at them; everything else propagates
them, running deferred statements on the way. `errdefer` statements run when a
function returns an error value, or when its frame is unwound by a flow whose
target lies in a caller. In Debug builds a trap collects each call it leaves on the
way out (the function and its call site) for the backtrace that `gamma run` prints.

**Effects.** The handler context is a persistent linked list from effects to
installed handlers, passed down the evaluation. `with` extends it; performing an
operation looks up the innermost handler and runs the clause as a function
whose environment is the frame where the handler literal was written. Second-class
function values capture the context where they were created, as §11.6 requires.

**Fibers.** The evaluator is recursive, so a fiber suspended in the middle of an
expression needs a Rust stack of its own: each fiber runs on an OS thread
(`src/interp/fiber.rs`). The threads share the machine and pass control to each
other over channels, so exactly one runs at a time; the per-thread parts of the
machine (temporaries, call depth, stack limits) are saved and restored at each
switch. A `with … on` runs its body on a new fiber and then acts as the driver:
it waits for a fiber to perform a `ctl` operation, runs the clause, and switches
to the fiber that `resume` names. `discard` sends the fiber a cancellation that
unwinds it like an exit. A continuation is a fiber number and a suspension
count, so consuming one twice, or from the wrong `with`, is detected. Each call
is charged against the fiber's stack by `tir::frame_cost`, the same cost model
`@stack_size` uses, so running out of a fiber's stack traps as in compiled code,
and a fiber given `@stack_size(f)` bytes never traps while running `f`.

**Threads.** Each Gamma thread runs on an OS thread of its own, sharing the
machine under a fair global lock (`src/interp/threads.rs`): a thread holds it while
it evaluates and hands it on every few thousand calls or loop iterations and around
calls that may block. A trap on any thread is recorded and stops every thread at its
next turn. The lock is created by the first `@thread_spawn`, so single-threaded
programs never take it.

**Constants.** Evaluating a constant produces its bytes plus *relocations* for
any pointers into static data (string literals, other constants, functions), so
the value can later be placed in a compiled program's read-only data.

## The C back end

`gamma build` and `gamma run --backend=c` translate the checked program to one
C translation unit (`src/cgen/`), prefixed by the runtime (`runtime/gm_rt.h`,
`runtime/gm_rt.c`), and compile it with the system C compiler (`$CC`, default
`cc`) in GNU C11 mode. The generated code follows the interpreter exactly, and
the conformance runner checks this: every `run` test also runs natively and
must print the same output and exit with the same status.

`gamma test` uses the C back end for modules that need it (foreign functions,
`@asm`) or with `--backend=c`. `cgen::generate_tests` emits one program whose
`main` runs the test entry selected by its first argument, exactly as the
ordinary `main` runs a program's entry; the driver builds it once and runs it
per test, passing a test iff the exit status is 0. A trap's message and
location reach the report from standard error.

**Values.** Scalars (integers up to 128 bits, floats, `bool`, pointers) are
native C values. Every other type is a *blob*, a struct holding its bytes with
the Gamma layout's size and alignment, whose fields are read and written with
`memcpy` at the layout's offsets (`src/cgen/types.rs`). Values therefore have
the same bytes in both back ends. Integers narrower than their C storage are
kept sign- or zero-extended, and arithmetic checks the Gamma type's range.

**Wide integers.** An integer wider than 128 bits is a blob holding its
little-endian two's-complement value in `⌈bits/64⌉` words (`src/cgen/wide.rs`).
Every operation (arithmetic and its overflow flavors, shifts, comparison,
conversion to and from other integers and floats, `min`, `abs`, `pow`, bit
counting, hashing and decimal display) calls a `gm_w*` routine of the runtime
with the operands' addresses, the width and the signedness. The routines
return the overflow flag that the operator's flavor needs, so the checks and
trap messages are those of narrow integers. They read the padding bits above
the width as the sign (or zero) extension and always write them that way, so a
value is the same whichever way it was made, and they use schoolbook
multiplication and shift-and-subtract division. `runtime/rt_test.c` checks them
against `__int128` and against exact big-integer vectors.

**Functions and frames.** Each function becomes
`RET gm_fN(void *env, gm_node *cx, gm_node *inst, params…)`. Its locals live in
a frame struct whose address is the `env` of the function literals and handler
clauses it creates, so captured variables are shared, as §11.6 requires.

**Effects.** The handler context `cx` is a linked list of `gm_node`s, built on
the C stack by `with`. Performing an operation finds the innermost node for the
effect instance and calls the clause from its table. Handler declarations go
through small thunks that load the handler value. The ambient `Out`, `Err` and
`Env` have native nodes served by the runtime.

**Handler specialization.** A `with` that installs a single handler literal
around a direct call to a function performing its effect — every `for` loop over
an iterator — calls a clone of that function specialized for the handler
(Appendix D.3 of the specification; `Spec` in `src/cgen/func.rs`). In the clone,
the handler's node is known to be `cx`, so each perform of the effect outside
any `with` of the clone calls the clause by name instead of searching the
context and calling through the table, and the C compiler can inline the loop
body into the iterator. `ctl` operations and the bodies of `with … on` keep the
general path. Clones share the frame struct of their function and are generated
on demand, at most one per function and handler literal.

**Non-local exits.** An exit whose target is in the same C function runs the
deferred statements it crosses and jumps there with `goto`; since nothing then
takes the frame's address, the C compiler can keep the locals of simple
functions in registers. `break`, `continue`, `return` and `try` aimed at another
frame, clause exits to a `with`, and `Env.exit` set a global exit record and
return. After each call that may exit this way, the caller tests the record and
runs its *ladder*: the pending deferred statements from the innermost scope
outwards, with a test at each loop and `with` for whether it is the exit's
target. A deferred statement run by a ladder sets the exit aside while it runs.
Traps print their message and stop the process (status 134) without unwinding.
In Debug builds each function also links a small record (`gm_bt`) into a
per-thread chain on entry, unlinks it on exit, and stores the location of each
call it makes in it; a trap prints the chain as a backtrace. Each fiber keeps its
own chain, which continues into the chain of the `with` running it.

**Fibers.** The runtime switches stacks with a few lines of assembly for x86-64
and AArch64 that save the callee-saved registers. The body of a `with … on` is
moved into a C function of its own, which reaches the enclosing function's frame
through a pointer, and runs on a fiber whose stack is the given memory. The
`with` itself becomes the driver loop. A `ctl` perform stores its arguments in a
request on the fiber's stack and switches to the driver, which calls the clause
through a small per-site thunk. `resume` and the cancellation by `discard` are
two more kinds of non-local exit: `resume` ends the clause and tells the driver
which fiber to switch to; a cancelled fiber unwinds to its base and switches
back. Fiber records come from a pool and are never freed, and each suspension
increments the fiber's epoch, so a continuation (fiber, epoch) that was already
consumed is always detected. In Debug and Checked builds every function entry
compares its frame's address with the lowest address the running stack may use:
the fiber's limit, or, on a thread's own stack, the bottom of that stack (found
with `pthread_getattr_np` or `pthread_get_stackaddr_np` at the start of `main`
and of each thread) plus a reserve for reporting the overflow. The check also counts the bytes of the C temporaries the function declares, since they may lie below its frame. A frame too large for what is left can still fault in the C compiler's prologue (which probes each page before the check runs), so each thread also has a `SIGSEGV` handler on an alternate signal stack that reports a fault just below the stack's limit as the same stack overflow trap. Large values read from a place are copied with `memmove` rather than through a C temporary.

**Formatting and comparison.** `@write` formats through the runtime's
`gm_format`, driven by type descriptors generated for each formatted type.
Structural `is`, ordering and `@hash` use C functions generated per type
(`src/cgen/helpers.rs`) that agree with the interpreter, including the hash
values, so hash maps iterate in the same order.

**Calling conventions and foreign code.** A function whose effects are at most
`{Sys}` takes no hidden parameters: it has the plain C calling convention, so it
can be passed to C as a callback, and function pointer types with such effect
sets are C function pointers (§19.1). Where a plain function is used as code
of the hidden convention (a closure, or a pointer widened to a type with
effects), the low bit of its address is set, and those calls test it. A plain
function may have a convention other than C (`#callconv(.Win64)`), kept in
`FnDef::callconv` and in the `FnTy` of pointer types, where it takes part in type
identity so that sema never converts between conventions. The back end turns
`Win64` into `__attribute__((ms_abi))` (`conv_attr`) on the function's prototype
and definition, on foreign declarations and exported wrappers, and in the function
pointer type that plain indirect calls cast to. Foreign functions are declared
under private C names bound to their symbols with asm labels, so they never clash
with the runtime's C headers. The interpreter
cannot call C, so `gamma run` uses this back end for programs with foreign
functions.

Blobs do not cross the C ABI as C structs do: a struct of two `double`s goes in
floating-point registers, a 16-byte blob in integer registers. So a struct,
`#c_union` or `#bits` struct whose fields are all C-compatible also gets a
*mirror* C type with the same fields (`Types::mirror`), with static assertions
that the C compiler lays it out exactly as Gamma does. Plain-convention
signatures (foreign functions, exports, functions with at most `{Sys}`, and
calls through their pointers) take and return mirrors, and values are copied
between blob and mirror with `GM_CONV` at those calls; the C compiler removes
the copies.
