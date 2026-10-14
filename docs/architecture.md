# Compiler architecture

This document describes how the reference implementation of Gamma is organized:
the stages a program passes through, where each stage lives, and the invariants
that connect them.

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
  └─▶ cgen                  C code generation → system C compiler → executable
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
| `src/lsp/` | the language server behind `gamma lsp` (see [Language server](#language-server)) |
| `src/ide.rs` | the editor index: references, hover text, scopes and outlines of one module |
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
| `lib/std/` | the rest of the standard library (see `docs/std.md`) |
| `tools/` | generators for checked-in files (the Unicode tables of `std/text`) |
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
`path:line:col: severity: message` line per diagnostic, followed by a
`note` line for each secondary location and each attached explanation; this is
what the test suite matches against.

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
| `effects.rs`, `handler_decl.rs` | performing operations, handler literals and declarations (generic ones are templates instantiated by `handlers_for`), `with`, `for` over iterators |
| `cont_check.rs` | the static check that a `ctl` clause consumes its continuation exactly once (§14.2), a small dataflow pass over the finished TIR |
| `escape.rs` | rejecting pointers to stack memory that outlive it |
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

The interpreter (`src/interp/`) runs programs for `gamma run` and evaluates
constants at compile time, so it implements the language exactly as the target
would: target integer sizes, layouts and float semantics, with every checked
operation checked. It has two engines over the same machine (memory, frames,
handler contexts): a tree-walker that evaluates TIR directly, and a bytecode VM
(`src/interp/vm/`) that runs most functions. Both are described below.

**Memory.** The interpreter models a flat address space. Every allocation (a
local variable, a temporary whose address is taken, a string literal, a
constant, a heap block) gets a fresh, never-reused range of addresses, aligned
to at least 16 bytes and followed by an unused gap. Values are byte strings in
target layout and pointers are plain addresses, so pointer arithmetic, casts and
`bits` reinterpretation behave as in compiled code. Because addresses are never
reused and freed or dead allocations are remembered, accesses through dangling,
out-of-bounds, misaligned or null pointers are detected and reported as traps
(at compile time, as the undefined behavior §17.1 requires to be diagnosed).
A local of up to 16 bytes (a number, a pointer, a slice) starts out in its
frame's slot instead, since most locals never have their address taken; the
first time something needs its address, it moves to an allocation of its own
with its current value. Reads and assignments of such a local touch only the
slot, which saves an allocation per local and call and an address lookup per
access.

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
programs never take it. `@futex_wait` and `@futex_wake` are emulated with one global
wake-up counter and condition variable: a wake-up wakes every waiter, which is allowed
because a futex wait may return early, and the waiter reads the counter before it gives
up the lock, so no wake-up is lost. Compiled programs use the kernel's futex.

**Foreign calls** (`src/interp/ffi.rs`). A call to a foreign function looks its symbol
up with `dlsym` (in the process, which has the C library, and in the libraries the
program names, opened as `lib<name>.so`/`.dylib`, also in the `-L` directories of
`LDFLAGS`) and calls it through libffi, whose call frame is built from the
declaration: integers of 8 to 64 bits, floats, pointers, `bool`, structs by value
(a `#c_union` by value is described to libffi as the struct the C ABI treats
alike) and variadic calls (`ffi_prep_cif_var`, with the argument types read from
the call site; the VM leaves such calls to the tree-walker). The `libffi` crate
builds its own copy of libffi, so no system library is needed.

Interpreter pointers are addresses in the interpreter's address space, not host
addresses, so memory crosses the boundary by copying. A pointer argument makes
a *mirror* of the allocation it points into: a host buffer with the allocation's
bytes, in which the pointers (found from the static type of the pointee) are
translated the same way. C is given the mirror's address. After the call, the
bytes C changed (compared with a snapshot taken before) are copied back, and
pointers C stored or returned that point into mirrors are translated back, so
`strchr` results and out-pointers (`strtol`'s `end`) are interpreter pointers
again. A mirror lives as long as its allocation, so the address C saw stays valid
between calls (`strtok`), and is refreshed at each call that passes a pointer to
it. Addresses that are in no interpreter allocation (`getenv`'s result, a
`FILE *`, memory from `malloc`, a mirror seen from a callback) are host memory:
once a foreign function has been called, `Memory` lets accesses to addresses above
everything it handed out go to the host directly. Foreign `#global`s are such
addresses (`dlsym`, and `__errno_location` for `errno`).

A function pointer passed to C becomes a libffi closure (cached per function) that
calls `call_function`. Pointers it receives are host addresses, usable directly
by the interpreted callback. Unwinding a trap through C frames is not possible,
so the trap is kept, later callbacks return at once, and the foreign call
returns it (with the call site standing for the callback's caller in the
backtrace) when C returns. `exit` called by C and `atexit` handlers work because
`gamma run` ends the process with `exit` while the machine is still alive when
callbacks were registered. A foreign declaration whose symbol is an `#export`ed
Gamma function calls that function directly.

`ffi::check_program` decides before the run whether the interpreter can do all
of this for the program: inline assembly, a signature libffi cannot describe
(`i128`, `#packed` or field-aligned structs by value, `f16`), a calling convention
other than C, a pointer to a union that holds pointers, a symbol `dlsym` does not
find, or a target other than the host's send the program to the C back end
instead. Compile-time evaluation traps on a foreign call.

**The bytecode VM.** Walking the tree costs a Rust call, a match and a boxed
value per node, so a function is compiled to register bytecode the first time it
is called (`code_of` in `src/interp/vm/mod.rs`) and run by a dispatch loop
(`src/interp/vm/exec.rs`). Calls from compiled code to compiled code pass
registers directly; calls to and from tree-walked code go through `Value`s.

- *Registers* are 64-bit words holding values in target layout: an `n`-byte value
  takes `⌈n/8⌉` words, its last word zero-padded, and pointers are addresses. A
  local of up to 64 bytes lives in registers unless something needs its address
  (a closure capturing it, `&x`, an assignment through a place the VM cannot
  resolve), in which case it lives in the frame as in the tree-walker. Small
  literal constants get registers that are filled when the activation starts.
- *Control flow* that stays in a function (`break`, `continue`, `return`, `try`,
  a clause's `break` to a `with` in the same function) compiles to jumps, with the
  `defer` and `errdefer` statements it crosses copied inline. Flows from
  elsewhere (an effect clause unwinding several calls, a trap) arrive as Rust
  values and are matched against a table of scopes, innermost first: a deferred
  statement's code, a loop or `with` landing, or a guard that turns a flow
  escaping a deferred statement into a trap.
- *Fallbacks.* A few constructs run on the tree-walker from inside compiled
  code: `with … on`, `resume`, `discard`, `@fiber`, system calls, and a
  statement whose value is too large for registers (over 64 KiB). Locals they
  mention live in the frame, where both engines see them. A function the
  compiler cannot handle at all runs on the tree-walker as a whole.

The dispatch loop is instantiated per byte order and per whether a step budget
(compile-time evaluation) is being counted, so the common case pays for neither.
An `Op` is at most 24 bytes (checked at compile time): operands that do not fit
live in side tables of the `Code` (`operands`, `builtins`, `coercions`, `bins`).
Integer instructions work on 64-bit patterns, with a cold path for overflow that
applies the wrap, saturate or checked semantics, and an instruction that
compares and branches also counts the step. The loop runs on a raw instruction
pointer and register base and reads hot registers unchecked; that is sound
because `Code::valid` checks, when a function is compiled, that those registers
and every jump target are in range and that the code ends in a trap. An
activation creates its heap `Frame` only when something needs one, and calls
between compiled functions, effect clauses and function pointers copy arguments
register to register. `--features vm-profile` counts opcodes and adjacent pairs
(`GAMMA_VM_PROFILE=1`, `GAMMA_VM_PAIRS=1`), which is how fused instructions are
chosen.
`GAMMA_INTERP=tree` turns the VM off; see `docs/contributing.md` for the
debugging switches.

**Constants.** Evaluating a constant produces its bytes plus *relocations* for
any pointers into static data (string literals, other constants, functions), so
the value can later be placed in a compiled program's read-only data.

## The C back end

`gamma build` and `gamma run --backend=c` translate the checked program to C
(`src/cgen/`) and compile it, with the runtime (`runtime/gm_rt.h`,
`runtime/gm_rt.c`), using the system C compiler (`$CC`, default `cc`, plus `$CFLAGS`
and `$LDFLAGS`; see `src/toolchain.rs`) in GNU C11 mode, with `-fwrapv` and
`-fno-strict-aliasing` (Gamma memory is untyped bytes). The compilation target is
decided before semantic analysis, because layouts and the `target` module depend
on it: the default compiler means the host, and any other toolchain is asked for
its predefined macros (`$CC $CFLAGS -dM -E`). `--target=<triple>` chooses the
compiler for a triple (`Toolchain::for_target`: `<triple>-gcc`, else
`clang --target=<triple>`, or `$CC` with the option added if it is clang) and then
checks the probed target against the triple (`check_triple`: architecture, byte
order, operating system), so a mismatched compiler is an error. `GAMMA_RUNNER`
prefixes the command that runs a built executable (`gamma run --backend=c`,
`gamma test`), for cross builds run under an emulator. The checked program
records its target (`Program::target`), and the interpreter and the C back end
both use it.
The generated code follows the interpreter exactly, and the conformance
runner checks this: every `run` test also runs natively and must print the same
output and exit with the same status.

How the runtime is linked depends on the build mode:

- Checked and Fast builds are one translation unit, the runtime followed by the
  generated code, compiled at `-O2`. Everything in the runtime is `static`, so
  the optimizer sees the whole program and drops what it does not use.
- Debug builds favor turnaround. Compiling the runtime is most of the work of
  compiling a small program, so it is compiled once on its own (with
  `GM_RT_SEPARATE` defined, which gives the declarations marked `GM_API` external
  linkage), cached, and linked with each program; the generated code includes
  only `gm_rt.h` and is compiled at `-O1` (`-O0` with `-g`). Compared with `-O2`
  this halves the C compiler's time, while the programs run about as fast; `-O0`
  would compile faster again but run several times slower. The cache
  (`src/cgen/rtcache.rs`) is the directory `$GAMMA_CACHE_DIR` (an empty value
  turns it off), else `$XDG_CACHE_HOME/gamma`, else `~/.cache/gamma`
  (`~/Library/Caches/gamma` on macOS). An object's name is a hash of the runtime
  source, the compiler's command (with its arguments and `$CFLAGS`) and `--version`
  output, and the flags, so a
  changed runtime or compiler gets a new object. Without a usable cache, a Debug
  build is one translation unit too. The test runners point `GAMMA_CACHE_DIR`
  into Cargo's target directory.
- Safe builds are compiled at `-O2` like Checked builds but link the cached
  runtime object as Debug builds do (compiled with Fil-C, at `-O2`). Fil-C
  compiles slowly, and the runtime code a program reaches was half the time of
  building a small program (2.5 s for hello world, 1.3 s with the cached
  object), while the programs ran at most 10% slower without the runtime
  inlined into them.

The generated `main` runs the program's entry function on a thread with a 64 MiB
stack (`gm_run_main`), so recursion depth does not depend on the host's default
stack size, and roughly matches the interpreter's.

`gamma test` uses the C back end for modules that need it (foreign functions,
`@asm`; their output is then captured with the test's) or with `--backend=c`. `cgen::generate_tests` emits one program whose
`main` runs the test entry selected by its first argument, exactly as the
ordinary `main` runs a program's entry; the driver builds it once, from the
tests `--filter` selects, and runs it per test, passing a test iff the exit status is 0. A trap's message and
location reach the report from standard error.

**Values.** Scalars (integers up to 128 bits, floats, `bool`, pointers) are
native C values. Every other type is a struct whose fields are read and written
with `memcpy` at the layout's offsets (`src/cgen/types.rs`). Values therefore have
the same bytes in both back ends. Integers narrower than their C storage are
kept sign- or zero-extended, and arithmetic checks the Gamma type's range.

A struct, tuple, array, optional, result or tagged union is a *typed struct*
(`Types::typed`): members of the field types (a union's payloads in a C `union`),
at the Gamma offsets, with every byte of padding an explicit `uint8_t` member so
that a copy moves exactly the bytes the layout has and a zero-initialized value
has zero padding. A `_Static_assert` on each checks size, alignment and member
offsets against the Gamma layout. Typed structs let the C compiler see which
parts of a value are scalars (gcc's scalar replacement of aggregates gives up on
byte arrays), and types with the same shape share one definition. Every other
type (`#packed` and `#bits` structs, zero-size types, integers over 128 bits)
is a *blob*, a struct holding its bytes with the layout's size and alignment.
Typed structs are not used under Fil-C, where a value is a blob so that copying
it keeps the capabilities of the pointers inside, nor on 32-bit targets, where
C and Gamma align 64-bit members differently.

**Wide integers.** An integer wider than 128 bits is a blob holding its
two's-complement value in `⌈bits/64⌉` 64-bit words, least significant first, each
word in the target's byte order (`src/cgen/wide.rs`).
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

**Byte order.** The C back end and the runtime work on little- and big-endian
targets. An integer of up to 128 bits is one number in the target's byte order
(C's own layout, read with `GM_LD`/`GM_ST`); a wider one is the word sequence above, so
the `gm_w*` routines see native 64-bit words whatever the order. Code that must
address the bytes of an integer goes through `gm_ibp` (the i-th least significant
byte; `gm_wbp` for the wide layout) instead of indexing, and `gm_load_u128` /
`gm_store_u128` read and write a whole integer of any storage size. Bytes of
compile-time values are laid out for the target (`int_bytes_swap_order` in
`src/layout.rs`; a `ConstKey` keeps an integer's bytes little-endian, whatever the
target), and the interpreter's memory is in target order, so constants emitted into
C are exactly what the generated code would store. Hashes mix the little-endian
bytes of every value, so they agree across targets, and printed output is the same
everywhere except where a program looks at its own memory (`bits`, `#c_union`,
`target.endian`). Fibers switch stacks in assembly for x86-64, AArch64 and s390x;
other architectures trap on the first fiber switch. `f16` is C's `_Float16`; where
the C compiler has none (`Target::f16`), a program that uses `f16` is rejected
with an error naming the limitation.

**Functions and frames.** Each function becomes
`RET gm_fN(void *env, gm_node *cx, gm_node *inst, params…)`. The locals that
function literals and handler clauses reach live in a frame struct whose
address is the `env` of those literals and clauses, so captured variables are
shared, as §11.6 requires. The other locals of up to 32 bytes are plain C
locals (larger ones gain nothing from registers, and the stack check, which must
assume they lie below the frame, would overestimate what the function needs;
Safe builds have no such check, so there every uncaptured local is a C local, and
one whose address escapes goes to the heap alone instead of with the frame):
once the frame's
address escapes (into a handler node or a closure), the C compiler must keep
every field of the frame in memory, but it can still keep the locals outside it
in registers, and under Fil-C an escaping frame is allocated on the
garbage-collected heap, so a smaller one costs less. Every local stays in the
frame in a function that contains a `with … on`, whose body is moved into a
function of its own and reaches the locals through a pointer to the frame, and
in Debug builds and with `-g`, so that the dead frame filled with `0xDD`
(§18.3) covers every local and debuggers show them all as its fields.

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
on demand, one per function and specialization.

A specialization passes on through the clone. A direct call that the clone makes
in its own context to a function performing the same effect (a recursive
iterator calling itself) calls that function's clone for the same handler. And a
`for` loop in the clone over another iterator (an adapter such as
`for v in inner() { yield(f(v)) }`) installs its loop body with a table of clause
clones specialized for the clone's own handler: the loop body runs with the
clone's `cx` as its context, so its `yield` calls the outer loop body by name too.
The iterator it calls is then specialized for that loop body together with the
outer handler (`SpecKey::outer`), and the chain of loop bodies can inline into
the innermost iterator. Specializations nest at most three deep (`nested_spec`),
since a recursive iterator would otherwise nest them without end; deeper levels
use the single-level clones.

A call of a clause by name is followed by the test for a non-local exit only if
the clause may return with one (`Gen::clause_may_exit`): a loop body that does
not `break`, `return` or call anything that could needs no test, which is what
lets a chain of adapters compile to a plain loop.

**Non-local exits.** An exit whose target is in the same C function runs the
deferred statements it crosses and jumps there with `goto`; since nothing then
takes the frame's address, the C compiler can keep the frame of simple
functions in registers. `break`, `continue`, `return` and `try` aimed at another
frame, clause exits to a `with`, and `Env.exit` set a global exit record and
return. After each call that may exit this way, the caller tests the record and
runs its *ladder*: the pending deferred statements from the innermost scope
outwards, with a test at each loop and `with` for whether it is the exit's
target. A deferred statement run by a ladder sets the exit aside while it runs.
The ladder exists once per function, not once per exit: each scope ends with
its rung, reached only by `goto`, which runs that scope's part and jumps to the
rung of the enclosing scope (a block's rung has an entry per deferred
statement, so an exit runs only those registered before it). An exit is then a
single `goto`, and every deferred statement is emitted at most twice (its
normal run and its rung), which keeps the C functions of code with many calls
and `defer`s small; C compilers, Fil-C's especially, are slow on huge
functions. An exit within the function that crosses a deferred statement takes
the ladder too.
The test after a call reads the thread-local exit record. Returning the exit as
a flag next to the result instead (Appendix D.2) was measured on a recursive
effectful function: about 15% faster with gcc and 25% slower with clang, which
handles the two-register result less well. The record stays; instead the test is
left out wherever the callee cannot exit (see above).
Traps print their message and stop the process (status 134) without unwinding.
In Debug builds each function also links a small record (`gm_bt`) into a
per-thread chain on entry, unlinks it on exit, and stores the location of each
call it makes in it; a trap prints the chain as a backtrace. Each fiber keeps its
own chain, which continues into the chain of the `with` running it.

**Debug information.** With `-g`, the generated C maps back to the Gamma source
(`src/cgen/debug.rs`). Every C line of a function body is preceded by a `#line`
directive naming the absolute path and line of the Gamma statement it comes from,
so the C compiler's DWARF line table, and with it debuggers, profilers and
sanitizer reports, refer to `.gm` lines; each generated function ends with a
directive that returns to the C file's own numbering. A directive per C line is
needed because one numbers the lines after it consecutively, while a statement
usually takes several C lines. Each frame field is an anonymous union of its `lN`
name and the Gamma name of the local, typed with the type's *debugger view*
(`src/cgen/views.rs`), so a debugger prints `F.p.x`. A view is a C type with the
exact layout of the Gamma value but with named, typed members: struct fields by
name, tuple elements as `_0`, `_1`…, a tagged union as a `tag` of a C enum
(`Shape_Circle`) plus an anonymous union with a member per payload, an optional as
`value` and `present`, a result as `is_err`, `ok` and `err`, a slice as a typed
`ptr` and `len`, pointers as pointers to views, `bool` as `_Bool`. Views are packed
structs with members at the Gamma offsets and unnamed bit-fields as padding (which
debuggers skip), so their size is the value's and their alignment 1: they never
change the frame's layout, and a `_Static_assert` checks each size. Views reached
only through a pointer are declared after the view that points at them, so
recursive types work. On ELF targets the executable also carries gdb pretty
printers (`src/cgen/gdb_printers.py`) in its `.debug_gdb_scripts` section; they
recognize views by the suffix of their struct tag (`gm_dv3_union`, `_slice`,
`_string`, `_optional`, `_result`) and show the active variant only, slice
elements, string text, `null` and `!error`. The generated code never refers to these names, but
they may be macros of system headers (`linux`, `st_mtime`), so the declarations
that contain them suspend such macros with `#pragma push_macro`. Locals that
share a name, and names that are C keywords or reserved, keep only their `lN`
name. A Debug build with `-g` is compiled with `-O0` so that every local is
visible (the cached runtime object is compiled with `-g -O2`); Checked and Fast
builds keep `-O2`.

**Fibers.** The runtime switches stacks with a few lines of assembly for x86-64,
AArch64 and s390x that save the callee-saved registers. The body of a `with … on` is
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
with the runtime's C headers. `gamma run` uses this back end for programs the
interpreter cannot run (inline assembly, and foreign code it cannot call, see
"Foreign calls" in the interpreter section).

Blobs do not cross the C ABI as C structs do: a struct of two `double`s goes in
floating-point registers, a 16-byte blob in integer registers. So a struct,
`#c_union` or `#bits` struct whose fields are all C-compatible also gets a
*mirror* C type with the same fields (`Types::mirror`), with static assertions
that the C compiler lays it out exactly as Gamma does. Plain-convention
signatures (foreign functions, exports, functions with at most `{Sys}`, and
calls through their pointers) take and return mirrors, and values are copied
between blob and mirror with `GM_CONV` at those calls; the C compiler removes
the copies.

**Large values.** Between Gamma functions, a value larger than two words
(`Types::by_ref`: a blob over 16 bytes) is passed by address and the callee
copies it into its frame before anything else runs, unless it can read it in place
(below). The C ABI would pass such a
value through memory anyway, but by value the C compiler made several copies
per call (a temporary, the conversion union, the argument area, the frame), and
fiber stacks sized by `@stack_size` must hold them. Because the copy happens on
entry, the address may be that of any place holding the value: an argument read
from a local or through a pointer is passed as that place when no later argument
can write memory (`no_writes`), and `return x` copies `x` straight into the result
slot. An argument built from literals only, such as the `SourceLocation` that
`@caller_location()` fills in for `new`, lives in static storage initialized at
startup like a top-level constant (`src/cgen/constarg.rs`), and the call passes
its address instead of building a temporary at every call; under Fil-C that
temporary's escaping address cost a heap allocation per call. (Passing such
values by value instead is slower still under Fil-C: its calling convention
copies an aggregate argument through memory it allocates.) A plain function whose own signature differs from the C ABI as a result
(it has a large parameter, or returns a large mirror type) gets a separate entry
point `gm_fN_c` with the C signature (`Gen::needs_wrapper`, `c_abi_wrapper`); it
is what function pointers, constants and `#export` refer to (`Gen::addr_name`),
while direct calls use the internal one.

A large result is returned through a first parameter `gm_out`, the address of
the caller's destination, and the function returns `void`. The callee writes it
only when it returns (or, below, as the last thing it does), so the caller passes
the variable or place being assigned (`x = f(…)`, `x := f(…)`, `a[i] = f(…)`,
`return f(…)`) instead of a temporary: the assignment still happens after the
whole call, and an exit out of the call leaves the destination as it was
(`FnGen::expr_into`). Struct and array constructions are likewise built in their
destination once every element is evaluated, except that an element which is itself
a struct or array (or, into a new variable, a call) is built directly in its place
in the destination, and an element read from a place is copied straight from it.
That writes the destination before the later elements are evaluated, so it is done
only when nothing evaluated meanwhile can read the destination (`Observed`): a new
variable or result slot, or the caller's destination when every element only reads
constants and the function's own locals and parameters. An assignment to an existing
place builds the value in a temporary first. A caller does not pass a destination
that overlaps a parameter the callee reads in place while building its result (it
copies that argument first; `Gen::read_during_build`). A function body with no pending
deferred statement builds its result, or copies a returned local, straight into
`gm_out`; otherwise the result goes to the frame's result slot first, and the
deferred statements run before it is copied out.

A large parameter is read in place, without the copy, when no difference can be
observed (Appendix D.5; `src/cgen/borrow.rs`): the function never stores into
it, takes its address or slices it, and is *pure*: it has no effects, stores only
into its own locals, and calls only pure functions, so nothing that could change
the caller's value runs during the call.

Generated code is compiled with `-fno-strict-aliasing`. Values are byte blobs
written and read piecewise with `memcpy`, so type-based alias analysis gains
nothing, and gcc 13 at `-O2` was seen to drop such writes under it.

## Language server

`gamma lsp` serves the Language Server Protocol over standard input and output.
Users are pointed to `docs/lsp.md`; this section is about how it works.

### Structure

| File | Responsibility |
| --- | --- |
| `src/lsp/transport.rs` | JSON-RPC messages with `Content-Length` framing |
| `src/lsp/position.rs` | byte offsets to and from LSP positions (UTF-16 units, or bytes when the client offers UTF-8) |
| `src/lsp/analysis.rs` | one run of the compiler over a module, and its diagnostics as LSP diagnostics |
| `src/lsp/features.rs` | hover, definition, references, completion, signature help and symbols, as JSON |
| `src/lsp/server.rs` | open documents, when to analyze, and request dispatch |
| `src/ide.rs` | the index that the requests are answered from |
| `src/sema/ide/` | building the index from the checker's state |

The server is single-threaded. A second thread only reads messages and queues
them. The main loop handles a message, and checks the open modules when the queue
is empty, so a burst of edits costs one run. A request that needs a current
answer checks first. The loop runs on the compiler thread, which has a large
stack, because the passes recurse over the syntax tree.

### Analysis

A module is a directory (spec §3.1), so each run checks the whole directory that
contains an open document. `Session::overlays` maps paths to the text of open
documents; `Session::load` reads those instead of the disk. Importers see unsaved
buffers of the modules they import. An edit marks stale every analysis that read
a file of the edited directory; the next idle moment re-checks them.

`sema::analyze_for_ide` runs the same pipeline as `gamma test` (so `main` is not
required), with `Sema::keep_going` set so that syntax errors do not stop it.
The diagnostics stay in the session. `Analysis::run` wraps the call in
`catch_unwind`. If the checker panics, the run is repeated as `gamma test` would
do it, which stops at syntax errors, and those errors are reported. If that
fails too, the user sees one "internal compiler error" diagnostic on the first
open file of the directory.

Diagnostics are published for every open document of the directory and for every
other file that has diagnostics. Files that had diagnostics before and have none
now get an empty list. A diagnostic without a location goes to line 0 of the
first open document. Labels become `relatedInformation`; notes are appended to
the message.

The type interner never frees memory, and a run allocates fresh ids for every
struct, union and effect, so a long session would grow without bound. The index
therefore holds no `Ty`, only text, and the server calls `types::mark` before a
run and `types::release_since` after it. This is safe only because nothing else
allocates types, so the library tests never call it.

### The index

`sema/ide/` reads the state of a finished run. It adds no code to the checking
passes, so ordinary compilation is unaffected.

- `facts.rs` walks the typed IR of every function of the module (not the
  instances of generics, and not the core library). It records the type of each
  expression by span, the function each call resolves to, the declared locals,
  and a reference from each use of a local to its declaration. Where several
  expressions share a span, the innermost one is the first.
- `walk.rs` and `items.rs` re-parse the files of the module and walk the syntax
  tree with their own lexical scopes. They resolve a name to a local, or to a
  declaration of the module, its imports or the universe scope. A field, method,
  variant, operation or named argument is resolved through the type of the
  expression before the `.` or of the call, which comes from the facts. The walk
  records references, the hover text of each declared name, the locals visible at
  each offset (name, type and the byte range where it is visible), and the
  outline of each file.
- `info.rs` describes declarations: a function signature printed from the checked
  signature (with effects), the source text of a type, the members that `.` offers
  for a type (fields, then the functions of the type's module that take it as
  their first parameter) and for a module (its `pub` names).

Doc comments come from `ast::Item::doc`. References into the core library have no
file to open, so definition returns nothing for them, while hover still works.

### Completion

Names come from the locals visible at the cursor, the declarations of the module
and the universe scope. Members after `.` come from the index of the current
text when it has the receiver. Typing `x.` usually leaves text that does not
check. The server then takes out the dot and what was typed after it, checks the
module again with that text, and looks up the receiver by its byte range, which
the edit did not move.

### Limitations

- References are found within the analyzed module only.
- A name in a part of the file that is not taken by a top-level `when` is not
  resolved.
- Tuple element names and the members of builtin types are not references; the
  builtin methods of numbers and slices are listed by name only.
- Completion after `.` re-checks the module when the current text has no receiver
  for the dot, which is as slow as an edit.
- Files that are not saved and do not exist on disk are not part of their
  module's directory listing.
- Modules with a syntax error in the middle of a function may be checked with
  poor recovery; then only the syntax errors are reported until they are fixed.

## Safe builds (Fil-C)

`--mode=safe` builds through the C back end with [Fil-C](https://fil-c.org), a
memory-safe implementation of C in which every pointer carries an invisible
capability for its allocation. `Toolchain::for_safe_build` (`src/toolchain.rs`)
looks for `filcc` on `PATH`, then in `/opt/fil/bin`, honoring `$CC`; a compiler
that is not Fil-C is an error. `Target::filc` (`src/layout.rs`) is set when the
compiler predefines `__FILC__`, and the generated code and the runtime branch on
it. The interpreter treats Safe as Checked. The semantics are
§18.7 of the specification; the points below are what the C back end does to
make Fil-C accept and preserve the program's pointers.

- **Capability-clean C.** A pointer must never pass through an integer, or it
  loses its capability. Pointer arithmetic goes through `GM_PTR_ADD` (Fil-C's
  `zmkptr`, plain arithmetic elsewhere), and the tagging of function pointers
  through `GM_PTR_TAG` and `GM_PTR_UNTAG` (`zorptr`, `zandptr`).
- **Blobs.** Values are blobs here, not typed structs. A by-value byte blob that may hold pointers is emitted as a union
  with `void*` lanes, so copying it keeps the capabilities of the pointers
  inside. This is also why a pointer rebuilt from bytes written as integers
  grants nothing.
- **Typed accesses.** `GM_LD` and `GM_ST` load and store through a packed
  struct instead of `memcpy`. Under Fil-C a `memcpy` is a call into its runtime,
  and one on a local's address makes the local (in practice the whole frame
  `F`) escape to the garbage-collected heap, which cost a heap allocation per
  call. An address turned into a pointer (`to_ptr`, `GM_INT_TO_PTR`) passes
  through an empty `asm`, so the optimizer cannot fold it back into the pointer
  it came from and its capability.
- **`_Float16`.** Fil-C 0.686 miscompiles the conversion libcalls, so
  conversions use a software routine and the build adds `-mf16c`.
- **Debug information.** The gdb script section (top-level `asm`) is skipped.
- **Runtime.** Heap blocks come from `zgc_aligned_alloc`. Thread handles are
  indices into a table, not pointers cast to integers. Fibers switch with
  `swapcontext`, since Fil-C compiles no stack-switching assembly, on stacks
  Fil-C allocates (the `stack` of `with … on` gives only the size). Stack probes
  are off (`GM_NO_FRAME_PROBES`), because Fil-C checks stack overflow itself.
  Standard output is flushed on every write, since a memory error ends the
  process without running any runtime code that could flush it.

Fil-C reports memory errors itself ("filc safety error: …") and exits with
status 134, as a Gamma trap does.
