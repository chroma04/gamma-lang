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
  ├─▶ cgen                  C code generation → system C compiler → executable
  └─▶ clif                  Cranelift code generation → object file → linked executable
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
(spec §14), value formatting, and trap reporting. The
effect system compiles to *evidence passing* and *exit flags* (spec Appendix D),
which are plain C.

The second native back end, Cranelift, trades the C compiler's optimization for compile
speed: it emits machine code directly, several times faster than a C compiler compiles the
generated C, and shares the runtime with the C back end (see
[The Cranelift back end](#the-cranelift-back-end)).

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
| `src/main.rs` | the `gamma` command-line tool: options and dispatch |
| `src/testing.rs` | `gamma test` and `gamma bench`: selecting, running (interpreter or native, optionally in parallel), reporting, the coverage table |
| `src/coverage.rs` | statement coverage: inserts `Builtin::Cover` counters into a checked program (the interpreter counts them; `interp/coverage.rs`) |
| `src/doc/` | the API documentation generator behind `gamma doc` (syntax tree only; Markdown and HTML renderers) |
| `src/fmt.rs` | the source formatter behind `gamma fmt` (token based, see `docs/contributing.md`) |
| `src/repl/` | the REPL behind `gamma repl` (see [REPL](#repl)) |
| `src/lsp/` | the language server behind `gamma lsp` (see [Language server](#language-server)) |
| `src/ide.rs` | the editor index: references, hover text, scopes and outlines of one module |
| `src/toolchain.rs` | the C toolchain from `CC`, `CFLAGS`, `LDFLAGS`, `GAMMA_RUNNER` and `--target`, and the target it probes |
| `src/driver.rs` | `Session`: runs the stages for the CLI and tests |
| `src/source.rs` | source files, byte spans, line/column lookup |
| `src/diag.rs` | diagnostics and their rendering |
| `src/fix.rs` | `gamma fix`: choosing and applying the fixes diagnostics carry |
| `src/lint/` | `gamma lint`: the rules, their levels and fixes; `syntax.rs` holds the rules that need only the syntax tree |
| `src/sema/lint.rs` | the lint rules that need the checker: unused names and declarations, comparisons and assignments of a value with itself |
| `src/linting.rs` | the `gamma lint` command line and its `--fix` loop (in the binary) |
| `src/ice.rs` | the report printed for an internal compiler error |
| `src/symbol.rs` | interned identifiers |
| `src/syntax/token.rs` | token definitions |
| `src/syntax/lexer/` | the lexer (`mod.rs`), and the exact values of literals (`literal.rs`) |
| `src/syntax/parser/` | the parser: `mod.rs` (the parser state), `expr.rs`, `stmt.rs` (blocks, statements, control flow), `items.rs` (declarations), `types.rs`, `pattern.rs`, `lits.rs` (type, handler and foreign literals) |
| `src/syntax/ast.rs` | the syntax tree |
| `src/syntax/ast_dump.rs` | stable textual rendering of the syntax tree |
| `src/types.rs` | semantic types (hash-consed `Ty`) |
| `src/layout.rs` | sizes, alignments and field offsets for a target |
| `src/float.rs` | correctly rounded conversion of exact literals to IEEE formats |
| `src/tir.rs` | the typed intermediate representation |
| `src/pkg/` | the package manager: manifest, build settings, lockfile, git and archive dependencies, the cache, `gamma new` (see below) |
| `src/sema/` | semantic analysis (see below) |
| `src/interp/` | the interpreter, with the bytecode VM in `src/interp/vm/` (see below) |
| `src/cgen/` | the C back end (see below) |
| `src/clif/` | the Cranelift back end (see below) |
| `runtime/` | the C runtime of compiled programs, embedded in the compiler |
| `lib/core/` | the parts of the core library written in Gamma, embedded in the compiler |
| `lib/std/` | the rest of the standard library (see `docs/std.md`; its reference, `docs/std/`, is generated by `gamma doc`) |
| `tools/` | generators for checked-in files (the Unicode tables of `std/text`, the float printing table), and the release packaging and install scripts |
| `editors/` | editor support: TextMate and tree-sitter grammars (see `docs/editors.md`) |
| `tests/conformance.rs` | the conformance test runner |
| `tests/conformance/` | conformance tests, grouped by area |
| `tests/*.rs` | the other integration tests: formatter, package manager, language server, runtime, native builds, cross compilation, debug information |

## Packages

`src/pkg/` is everything about manifests and dependencies. It does not depend on semantic analysis; the compiler depends on it in two places.

| File | Contents |
| --- | --- |
| `manifest.rs` | the one `gamma.toml` parser (`toml_edit`, so errors have lines); validation of dependency tables and of `[build]` / `[profile.*]` (`BuildSettings`, `Manifest::build_settings`) |
| `lock.rs` | `gamma.lock`: deterministic rendering and parsing (git entries and archive entries) |
| `git.rs` | the `git` command-line tool, the on-disk store (`<cache>/git/db`, `<cache>/git/checkouts`, `<cache>/git/url`), the SHA-256 checksum of a tree (symbolic links included), making checkouts read-only and removing them, the `.used` stamps |
| `archive.rs` | `url = …` dependencies: download with `curl`, unpack `.tar.gz` safely (`flate2` + `tar`), strip a single top-level directory |
| `resolve.rs` | the resolver: version selection, lock reuse, checkouts and archives, cycle and path-escape checks |
| `build.rs` | build settings: the root package's settings for a profile (read before a session starts) and the collection of C sources, include and library directories from the whole dependency graph |
| `cache.rs` | `gamma cache clean`: garbage collection by last use |
| `edit.rs` | format-preserving manifest edits for `gamma add` and `gamma remove` |
| `scaffold.rs` | the files of `gamma new` and `gamma init` |
| `cli.rs` | argument handling of the package commands, which `main.rs` dispatches before it parses compiler options |
| `mod.rs` | `Resolution`, `sync` (resolve, fetch, write the lock) and `prepare` (what the compiler calls) |

Before `check`, `run`, `build`, `test` and `bench` analyze a program, `main.rs` calls `pkg::prepare` with the input path. It finds the root package (the nearest `gamma.toml`), walks the dependency graph (path dependencies too, since they may have git or archive dependencies), and returns a `Resolution`: package directory to dependency name to dependency directory. The result goes into `Session::packages`. `sema/imports.rs` consults it in `package_of`; a package without an entry falls back to reading its manifest for path dependencies, and reports a git or archive dependency it has no resolution for.

The resolver identifies a git package by its normalized URL and `subdir`, collects every requirement on it, and chooses one revision: the locked one if it still satisfies all requirements, else the highest matching tag, the branch head, or the commit. A choice that a later requirement contradicts restarts the walk with the larger requirement set; requirements only grow, so it terminates. Manifests are read from checkouts, which are made (into a temporary directory, then renamed) as soon as a revision is chosen. Checkouts are named by revision, so choosing again is free.

Design points for contributors:

- The lockfile fixes the commit; the checksum is a second line of defense and is checked only when a checkout is created. Hashing every use would read the whole tree on every build.
- A request for the default branch yields to any other requirement on the same repository.
- Symbolic links are part of the checksum, with a record that no regular file can produce (its length field is `u64::MAX`); trees without links hash as before, so old lockfiles stay valid. A link that is absolute or climbs out of the tree is an error when the checksum is computed, i.e. before the checkout is used (a bad checkout is deleted).
- Checkouts and unpacked archives are made read-only after verification (`git::make_read_only`); everything that deletes one goes through `git::remove_tree`, which restores permissions first. Last use is the mtime of a `<dir>.used` file beside the checkout, since the checkout cannot hold a stamp.
- Archives are keyed by the SHA-256 of the file, so a URL whose content changes gets a new directory. The lock stores both that hash and the tree checksum; `sha256` in the manifest wins over the lock when they differ (the author changed it on purpose). `update` does nothing to an archive whose URL is unchanged.
- Tests of the package manager (`tests/pkg.rs`) build local repositories and archives (`file://` URLs, through `curl`) and run the real binary with a private `GAMMA_CACHE_DIR`; they need `git` and `curl` but no network.

### Build settings and C sources

`main.rs` reads the root package's build settings (`pkg::build::root_settings`) right after parsing the command line, applies `mode` and `debug-info` under any explicit option, and puts the manifest's `cflags` and `ldflags` in front of the environment's in the `Toolchain`. After the dependencies are resolved, `pkg::build::collect` walks the graph (using the `Resolution` for git and archive packages) and returns `cgen::Extras`: the C files with their include directories, the libraries and the library directories.

`cgen/csrc.rs` turns `Extras` into something a build uses. `link_inputs` compiles each C file to an object, named by the hash of the *preprocessed* file (`cc -E`, so headers count), the compiler, its version and the flags, in `<cache>/native`; the resulting `Link` is passed to `cgen::compile` (object files and `-L` / `-rpath` / `-l` arguments) and mixed into the key of `cached_program`. `load_for_interpreter` links the same objects into a shared library, which `interp::load_library` opens (`dlopen`, `RTLD_GLOBAL`); `Ffi::lookup` searches those handles after the libraries a `foreign` block names, and `interp::add_library_dirs` adds `lib-dirs` to the places `open_lib` looks. `main.rs` takes the native path instead for Safe builds and `--target`, which cannot load code into the interpreter.

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

A diagnostic may carry *fixes* (`Diagnostic::with_fix`): source edits that resolve
it, each a replacement of a span's text. Human-oriented output does not show them,
since the message says what to write; `--diag-format=json` prints one JSON object per
diagnostic, with byte offsets, lines and columns, notes and fixes; the language
server sends a diagnostic's fixes along with it in the `data` field and returns them
as quick fixes when the client asks for code actions on that diagnostic; and
`gamma fix` (`src/fix.rs`) checks the program, applies every fix that is the only
one of its diagnostic to the files of the input's package (never the standard library
or other packages), skipping fixes that overlap, and checks again until nothing is
left to apply, since a fix can let checking get further (a syntax error stops
analysis). Fixes are attached where the right edit is unambiguous: the lexer
(uppercase radix prefixes), the parser (`&&`/`||`, `else` as a pattern, a trailing
comma after a match arm, `try x else d` with both readings) and semantic analysis
(an unused value, `s.len()` on a slice, a payload variant pattern without `(_)`,
`x := v` at top level). To tell a comma at the end of a line from one in the middle,
the lexer records the line breaks it drops after a token that continues the line
(`LexOutput::joined_lines`).

`gamma lint` (`src/lint/`) checks the program like `gamma check` and then runs two
kinds of rules over the files of the input's package. Rules about names and uses work
from the checker's results (`src/sema/lint.rs`): name resolution records which
declarations (`Sema::decl_uses`) and which local bindings (`Sema::local_uses`) each
name refers to, so uses that leave no trace in TIR (`a.len` of an array, `@type_of(x)`)
still count, and TIR says which locals are assigned or borrowed mutably. The other
rules re-parse each file and walk its AST (`src/lint/syntax.rs`). Each finding is a
warning whose `code` is the rule's name, with a fix where one is safe; levels come from
the defaults, the manifest's `[lint]` table, the command line and `#allow` attributes.
`gamma lint --fix` reuses `gamma fix`'s planning and, when a round of fixes breaks the
program, undoes it and retries the fixes one at a time, leaving out the ones that break it.

Cascades are kept down in three ways. `Diagnostics::push` drops a diagnostic that
repeats an earlier one with the same severity, location and message, and an error
whose message mentions the error type `{error}`, which only restates an earlier
error. The parser reports a keyword used as a name once and reads later uses of the
word as that name. Semantic analysis gives an expression that is already wrong the
error type, which every later check accepts silently; a statement whose expression
reported an error does not also get the "value is unused" error.

An "unreachable code" error carries a note at the construct that ends the control
flow (`never_cause` in `src/sema/func.rs`).

`src/ice.rs` handles panics in the compiler: a hook installed by `main` records the
message and location, and `main` prints them as an "internal compiler error" with
a request to report it, exiting with status 101. `RUST_BACKTRACE` also shows the
backtrace. The language server does not install the hook, since it catches panics
itself. `GAMMA_DEBUG_PANIC=1` makes any command panic at start-up, to test the report.

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
| `call.rs`, `methods.rs`, `construct.rs` | calls (`E.op(…)` is recognized in `FnCx::call`, since an operation is not a name of its module), method calls (`module_method` looks at the module's function and at `Module::effect_ops` for operations that take the receiver first; `Sema::check_method_candidates` rejects a type with two candidates of one name) and builtin methods, construction |
| `pattern.rs`, `matching.rs`, `exhaust.rs` | patterns, `match`, binding conditions, exhaustiveness |
| `seq.rs` | slicing, `in`, and `for` over arrays and slices |
| `vector.rs` | vector types `#simd [N]T`: their lane-wise operators, conversions and methods (§5.6.2) |
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
ambient effects (`Out`, `Err`, `Env`, `Alloc`, `In`, `Clock`, `Rand`, `Fs`, `Net`, `Proc`, and
`Sys`), the standard iterators and allocation (`Allocator`, `new`, `make`, `free`, …). The
containers live in their own core modules, `list.gm` and `map.gm`, and the builtin methods
of arrays and slices that are written in Gamma (the sorts, …) in `slice.gm`; only their types and
constructors (`List`, `list`, `HashMap`, `map`, `map_with`) are re-exported to
the universe scope, and their other functions are reached as methods, which
resolve in the receiver type's module (§21.5).

Core modules see the prelude's private declarations and may use a few
host-facing intrinsics: `@heap_alloc` and `@heap_free`, and the system
services behind the default handlers (`@sys_read`, `@clock_now`,
`@clock_wall`, `@clock_sleep`, `@rand_fill`, the file system and network calls; `tir::SysOp`). Services that can
fail return a count or a negative error code that the prelude turns into an
`IoError`, so both back ends report errors identically. They may also use `is`
and `@hash` on unconstrained type parameters: the map's public constructors
require `Eq` exactly when the default hashing is used. The whole core library
is checked on every compilation, so mistakes in it surface immediately.

The program's entry is a synthesized function that calls `main` inside the
prelude's default handlers for the ambient effects it performs:
`with heap_allocator(), system_io() { main() }` (only the providers for the effects
`main` performs), where `system_io()` handles `In`, `Clock`, `Rand`, `Fs`, `Net` and `Proc`
through the intrinsics above. The ambient effects with
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
operation checked. A machine (memory, frames, handler contexts) runs register
bytecode compiled from TIR by the VM in `src/interp/vm/`, described below; every
function, constant initializer and compile-time evaluation runs that way.

Its files: `machine.rs` holds the interpreter state and the typed helpers all evaluation
code shares; `memory.rs` the flat address space below; `bytes.rs` the small-buffer byte
string values are made of, and `fast.rs` integers of at most 64 bits as exact `i128`
values; `calls.rs` calls, handler contexts, `with`, `perform` and the native ambient
handlers; `consts.rs` the memoized evaluation of constants and their place in static
memory; `display.rs` the display rules of `@write` (§21.6.1); `sys.rs` and `proc.rs` the
system services and child processes behind the default handlers (§12.9); `testkit.rs`
helpers for building TIR by hand in the unit tests. In `vm/`, `compile.rs` translates a
function body to bytecode (where locals live, expressions) and `compile_flow.rs` its
statements, patterns and control flow.

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

**Fibers.** The interpreter nests Rust calls (a clause run by a driver, a call from a
builtin or from C), so a fiber suspended in the middle of its code needs a Rust stack
of its own: each fiber runs on an OS thread
(`src/interp/fiber.rs`). The threads share the machine and pass control to each
other over channels, so exactly one runs at a time; the per-thread parts of the
machine (temporaries, call depth, stack limits) are saved and restored at each
switch. A `with … on` runs its body on a new fiber and then acts as the driver:
it waits for a fiber to perform a `ctl` operation, runs the clause, and switches
to the fiber that `resume` names. `cancel` sends the fiber a cancellation that
unwinds it like an exit. A continuation is a fiber number and a suspension
count, so consuming one twice, or from the wrong `with`, is detected. Each call
is charged against the fiber's stack by `tir::frame_cost`, the same cost model
`@stack_size` uses, so running out of a fiber's stack traps as in compiled code,
and a fiber given `@stack_size(f)` bytes never traps while running `f`.

**Threads.** Each Gamma thread runs on an OS thread of its own, sharing the
machine under a fair global lock (`src/interp/threads.rs`): a thread holds it while
it evaluates and hands it on every few thousand calls or loop iterations and around
calls that may block. A trap on any thread is recorded and stops every thread at its
next turn (threads asleep in a futex are woken for it). File opens, reads and writes,
name lookups and socket shutdowns run with the lock released, so a thread blocked on a
pipe or a slow lookup does not stop the others; foreign calls keep it. A child process
records under its own mutex whether it was reaped: `Proc.wait` waits without reaping
(`waitid` with `WNOWAIT`), then reaps and sets the flag under the mutex, and `Proc.kill`
refuses once it is set, so it never signals a process id the system may have reused.
Children a program never waited for are reaped when its run ends. The lock is created by the first `@thread_spawn`, so single-threaded
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
the call site; the VM keeps them with the call's argument list). The `libffi` crate
builds its own copy of libffi, so no system library is needed. On Windows the symbols
come from `LoadLibrary`/`GetProcAddress`: the C library is looked up in both
`ucrtbase.dll` and `msvcrt.dll` (UCRT does not export `printf` and its relatives, and a
MinGW program may use either), and the libraries a program names are opened as
`<name>.dll` or `lib<name>.dll`. Since the two C libraries keep their own output buffers,
the interpreter flushes both around foreign calls, and registers `atexit` handlers with
the C library that ends the process.

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
addresses (`dlsym`, and `__errno_location` for `errno`, asked again at each access
since every thread, and so every fiber, has its own). A mirror is a copy: C code that
keeps a pointer after the call and writes through it later changes the mirror, which
the interpreter reads back only at the next call that passes that allocation. A pointer
to freed memory, or a function whose type cannot become a C callback, is a trap rather
than an address C would misuse.

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

**The bytecode VM.** A function is compiled to register bytecode the first time it
is called (`code_of` in `src/interp/vm/mod.rs`) and run by a dispatch loop
(`src/interp/vm/exec.rs`). Calls from compiled code to compiled code pass
registers directly; calls from the runtime (a builtin, a clause run by a fiber's
driver, a thread's start, a callback from C) go through `Value`s. A closed
expression, such as a constant's initializer, compiles to code of its own without
parameters (`compile_expr`), run in a fresh frame.

- *Registers* are 64-bit words holding values in target layout: an `n`-byte value
  takes `⌈n/8⌉` words, its last word zero-padded, and pointers are addresses. A
  local of up to 64 bytes lives in registers unless something needs its address
  (a closure capturing it, `&x`, an assignment through a place the VM cannot
  resolve, or the body of a `with … on` mentioning it), in which case it lives in
  its frame's slots. A local larger than 64 bytes always does. Small
  literal constants get registers that are filled when the activation starts.
- *Instructions* have fast forms for the common types: small integers (up to 64
  bits; `AddI`, `CmpI`, `BrI` compare-and-branch, `RotI`…), `f32`/`f64` (`BinF`, `CmpF`,
  `BrF`, `NegF`) and pointer words. Anything else (big integers, `f16`, arrays,
  `#bits` structs) goes through generic instructions that decode the operands'
  bytes with the general arithmetic of `src/interp/ops.rs` and `num.rs`. A branch on a negated float comparison jumps over
  an unconditional jump instead of inverting the comparison, which a NaN would break.
- *Control flow* that stays in a function (`break`, `continue`, `return`, `try`,
  a clause's `break` to a `with` in the same function) compiles to jumps, with the
  `defer` and `errdefer` statements it crosses copied inline. Flows from
  elsewhere (an effect clause unwinding several calls, a trap) arrive as Rust
  values and are matched against a table of scopes, innermost first: a deferred
  statement's code, a loop or `with` landing, or a guard that turns a flow
  escaping a deferred statement into a trap.
- *`with … on`.* Its body runs on a fiber, on another thread, so it is compiled to
  code of its own (`compile_sub`) that runs in the frame of the activation that
  contains it: the locals the body mentions live in that frame. A `return` in the
  body, and a `break` or `continue` to a loop outside it, are flows to that frame,
  which unwind the fiber and land in the function's own scopes. `resume`,
  `cancel` and `@fiber` are instructions of their own; system calls and coverage
  counters go through the generic builtin instruction.
- *Large values* live in registers too (an array value of a megabyte takes
  128 Ki words); only a value larger than any frame (1 GiB) cannot be compiled.
  Indexing an array of 2^32 or more elements checks the index against a length
  in a register (`IndexAddrW`). A function the compiler cannot translate gets
  code that only traps with an internal error naming the line of the compiler
  that gave up (`trap_code`); its parameters are bound as usual, so the checks of
  the call (such as the size of its frame) come first.

Calls between compiled functions do not nest Rust calls: the dispatch loop makes the
callee the running activation, with the caller, the place for the result and where to go
on kept in the callee's `St` and the callee on `Machine::vm_stack` (saved and restored
with the other per-fiber parts of the machine). A call, a `perform` of a compiled clause
and a call through a function pointer or closure all start this way, and a `return`
copies the result straight into the caller's registers. Activations are boxed, pooled and
reused with their register buffers; compiled code is referred to by pointer, since it is
never dropped or replaced. Flows that unwind through several calls are handled by the
same loop, innermost first, with the same traces as before.

*Allocation.* `new` and `destroy` of the core library perform `Alloc.current`, call the
allocator through its vtable and (Debug builds) record or check the owner and site of the
block. When the innermost `Alloc` handler is the declaration for `Allocator` installed with
the value `heap_allocator()` returns, the VM (`vm/heap.rs`, `Op::CallHeap`) does all of it
in one step with the same effects on memory and the same tags. It does so only if nothing
in the library code could trap or be observed to differ: no step budget, no fiber stack
accounting, no threads, depth and Rust stack far from their limits, the block valid for
`destroy`; otherwise the instruction makes the ordinary call, so every trap report is
unchanged. The tables the machine keeps by address or id use a plain multiplicative hash
(`fxmap.rs`), and reads and writes of up to 64 bytes go from memory to registers directly.

A run may also have a deadline (`gamma test --timeout`), checked with the step budget
every 1024 calls or loop iterations; a run blocked in a system call does not reach it.
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
See `docs/contributing.md` for the debugging switches.

**Constants.** Evaluating a constant produces its bytes plus *relocations* for
any pointers into static data (string literals, other constants, functions), so
the value can later be placed in a compiled program's read-only data.

**On WebAssembly.** The crate also builds for `wasm32-wasip1`, for the web playground
(`web/playground/`). There are no threads there, so evaluation (`run_entry`, `eval_const`)
runs on the caller's stack instead of a thread of its own, with `STACK_SIZE` matching the
stack the playground links with; fibers and `@thread_spawn` report that they cannot start.
Calls into C are replaced by `ffi_wasm.rs`, which rejects programs with foreign
declarations, and the Cranelift back end (`src/clif/`) is not compiled.

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
both use it. The files:

| File | Responsibility |
| --- | --- |
| `mod.rs` | `Gen`: the whole program, prototypes, constants, entry points, the C compiler invocation |
| `types.rs` | C representations of types (`Types`: typed structs, blobs, mirrors) and the type descriptors for `@write` |
| `func.rs` | functions: frames, blocks, statements, the exit ladders, handler specialization |
| `expr.rs` | expressions, places, coercions, calls, effects, pattern matching |
| `chain.rs` | long chains of operations (`a + b + …`, `x.abs().abs()…`) through an accumulator |
| `outline.rs` | long runs of statements moved into helper functions that share the frame |
| `arith.rs` | checked scalar operators |
| `vector.rs` | vector operations as GCC/Clang vector extensions, lane loops where a check applies (§5.6.2) |
| `helpers.rs` | per-type equality, ordering and hashing |
| `fiber.rs` | `with … on`, `ctl` performs, `resume`, `cancel`, `@fiber` |
| `borrow.rs`, `constarg.rs` | parameters read in place; static storage for constant arguments |
| `rtcache.rs` | the cached runtime object of Debug and Safe builds |
| `units.rs` | splitting a Debug build into translation units compiled in parallel |
| `csrc.rs` | the C sources, libraries and directories of `[build]` settings (see [Build settings and C sources](#build-settings-and-c-sources)) |
| `debug.rs`, `views.rs`, `gdb_printers.py`, `lldb_formatters.py` | debug information for `-g` |

### Freestanding targets

A target whose `os` is `Freestanding` (`Target::os`, from a compiler that defines no
operating system macro) makes a program with no `main`: sema rejects `main`, the default
handler providers (`heap_allocator`, `system_io`, `system_output`, checked where names are
used, `check_freestanding_use`) and `#trap_hook` anywhere else, and records the hook in
`Program::trap_hook`. `cgen::generate_object` queues the exports and the hook instead of an
entry, and `Gen::gen_start` (constants and the build mode, once, from every export wrapper),
`Gen::gen_trap_hook` (`gm_trap_hook_call`, which builds the `string` and `SourceLocation`
arguments and calls the hook) and the absence of `main` are what differ in the generated C.
`cgen::compile` then calls `compile_object`: one translation unit of the runtime and the generated
code, compiled with `-c -ffreestanding` into the object `gamma build` writes. There is no cached
runtime object, because the hook is code of the program and without the operating system parts
the runtime is small; there is no link step, so the packages' C sources and the `foreign` libraries
are the linker's business.

The runtime selects its freestanding variant with `GM_FREESTANDING`, defined by `compile_object`
before `gm_rt.h` (and `GM_TRAP_HOOK` when the program has a hook). Then `gm_rt.h` includes only
`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` and `<stdarg.h>` and declares `memcpy`, `memmove`,
`memset` and `memcmp`; `strlen`, `strcmp` and `snprintf` become the runtime's own
(`gm_snprintf` understands the conversions the runtime and the generated code use, including an
exact `%.*f` and `%.*e`); `GM_TLS`, which is `_Thread_local` elsewhere, is empty; and `gm_rt.c`
leaves out the ambient effects, the system services, the system heap and its Debug records, and
threads, and keeps the formatter, the fiber machinery (the assembly context switch has no operating
system dependency, but it is `.local` there, and fiber records come from a static slab) and the
traps. A trap calls `gm_trap_hook_call` once (a trap in the hook executes the trap instruction
directly), or executes `__builtin_trap()`. Formatting buffers (`gm_buf`) take their memory from a
static arena (`gm_scratch`) that is emptied whenever no buffer is live. The stubs for `gm_write_out`,
`gm_args` and the like exist only so that the generated code for the ambient handlers, which
never runs without a handler installed, compiles. `main.rs` refuses `run`, `test` and `bench` for
such a target (`refuse_freestanding`) and `build` calls `build_object`.

The generated code follows the interpreter exactly, and the conformance
runner checks this: every `run` test also runs natively and must print the same
output and exit with the same status.

Operands are evaluated into C temporaries in order, so expressions are never nested in
the C text. A function with tens of thousands of operations is still expensive to
compile: Clang tracks the lifetime of every variable of a scope at each branch out of
it, and gcc's points-to analysis and jump threading are superlinear in the size of one
function. A chain of more than 256 operations that each take the previous one's value
as their first operand (binary operators, `and`/`or`, unary operators, built-in and
function-call links with a scalar value) is therefore emitted as an accumulator
variable updated by one block per link (`chain.rs`), which keeps the live temporaries of
any scope bounded; the first operand is still evaluated first, so order, traps and
early exits are unchanged. Chains of large values (structs, arrays) and chains through
other operations keep one scope. A generated function of more than 20,000 temporaries
is marked `GM_HUGE_FN`, which turns off those two gcc passes for it, and `-w` is
accompanied by `-Wno-free-nonheap-object` because gcc otherwise still runs its
access-warning pass over such functions. Clang from LLVM 19 on (Apple clang 17 on)
has a pass of its own that is superlinear in such a body: EarlyCSE asks for the known
bits of values through the selects and checks of every link, and a chain of 10,000
checked operations takes minutes, 50,000 hours. A generated function of more than
2,000 temporaries, several times more than the largest functions of ordinary programs,
is therefore marked `GM_LONG_FN`, which compiles it without optimization (`optnone`)
on those versions of Clang and means nothing elsewhere.

A function that is long because it has many statements, rather than one long chain, is
split instead (`outline.rs`). When a body has more than 1,500 expression nodes, every
block of more than 400 has its statements dealt out in runs of up to 400 nodes, each
moved into a helper `gm_cN_K` that the function calls where the run was. A helper takes
the frame's address and defines `F` as `(*gm_Fp)`, so a statement's code is the same as
inline; such a function keeps all its locals in the frame, and the helper also gets the
other C variables statements read (`env`, `cx`, `inst`, `gm_out`, the backtrace record
`B`, the node of an enclosing `with`, the parameters read in place). A jump out of a run
(to a rung of the ladder, a loop's exit, the function's return) becomes `return k` with
a number per target, and the caller's `switch` jumps there; `FnGen::jump` emits every
jump that can leave a statement, so it alone decides. A statement bigger than a run
stays in the function and its own blocks are split. Each C function is then small,
and the C compiler's time grows linearly with the Gamma function's length: a `main` of
10,000 assorted statements took 87 s to compile with gcc at `-O1` and now takes 4 s.
Builds with `-g` keep functions whole, so that a debugger shows the frame as `F`.

At the other end, optimized builds (every mode but Debug) declare a function of at most
64 temporaries and labels `inline` unless it carries an `#inline` hint. gcc at `-O2`
inlines a function not declared `inline` only when its size estimate is tiny, and the
frame set-up of a generated function already exceeds it, so a small function on large
values (adding two 4-element vectors) stayed a call, with its arguments copied to
temporaries, at about three times the cost of Clang's code. Declared `inline`, gcc
weighs it against its larger limit for such functions, which still keeps big ones out.

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
  (`~/Library/Caches/gamma` on macOS, `%LOCALAPPDATA%\gamma` on Windows). An object's name is a hash of the runtime
  source, the compiler's command (with its arguments and `$CFLAGS`) and `--version`
  output, and the flags, so a
  changed runtime or compiler gets a new object. Without a usable cache, a Debug
  build is one translation unit too. The test runners point `GAMMA_CACHE_DIR`
  into Cargo's target directory. Cached objects and executables are found by a
  hash and used without any check, so the cache directory must be private to the
  user: gamma creates it with mode 0700 (and temporary directories and files
  exclusively, never reusing an existing name), but a cache that other users can
  write to lets them plant programs that `gamma run` then executes. The key of a
  cached executable also covers the size and modification time of the library
  files it links (the package's, those of `foreign` blocks, and those named by
  `-L` / `-l` in `$LDFLAGS`) and, for WASI, the version of `wasm-opt`. Leftover
  temporary files of builds that died are removed after an hour.
- A large Debug build is split into several translation units, compiled in
  parallel and then linked (`src/cgen/units.rs`): one per 96 KiB of function
  text, at most one per processor and 8. The generator writes everything with
  internal linkage, as one unit needs, and keeps it by role (`units::Parts`):
  support code, types and frames; function declarations; static data, one item
  per line; function definitions; the entry code. A split shares a header of the
  first two plus an `extern` declaration of each data item and each function,
  gives the functions and data external linkage (their names all start with
  `gm_`, so they cannot clash with C code), defines the data and the entry code
  in unit 0 and deals the function definitions out by size. Unit `i` > 0 is
  written next to unit 0's `prog.c` as `prog.i.c`. Units are compiled with
  `-Werror=implicit-function-declaration`, so a function left undeclared fails
  the build instead of being called with C's implicit `int` signature. Code of
  a shape the splitter does not recognize is compiled as one unit, unless
  `GAMMA_C_UNITS=<n>` forces a split into `n` units (tests use it on small
  programs), which then fails. calc (16k lines of C) builds in 0.6–0.9 s
  instead of 1.4 s on 4 cores.
- Safe builds are compiled at `-O2` like Checked builds but link the cached
  runtime object as Debug builds do (compiled with Fil-C, at `-O2`). Fil-C
  compiles slowly, and the runtime code a program reaches was half the time of
  building a small program (2.5 s for hello world, 1.3 s with the cached
  object), while the programs ran at most 10% slower without the runtime
  inlined into them.

The generated `main` runs the program's entry function on a thread with a 64 MiB
stack (`gm_run_main`), so recursion depth does not depend on the host's default
stack size, and roughly matches the interpreter's.

**Windows.** The runtime has a second implementation of its system layer for x86-64
Windows, selected by `_WIN32` in `runtime/gm_rt.h` and `runtime/gm_rt.c`, built with
MinGW-w64 gcc or clang. It uses the Win32 API directly rather than a POSIX emulation
such as winpthreads, so that programs need no DLL besides the system's (they are also
linked `-static`):

- Threads are `_beginthreadex` threads (the stack size is a reservation, so the
  64 MiB main stack costs address space only); the runtime's lock is an `SRWLOCK`; the
  futex is `WaitOnAddress` / `WakeByAddress*` (`-lsynchronization`); time is
  `QueryPerformanceCounter` and `GetSystemTimePreciseAsFileTime`, and sleeping a
  high-resolution waitable timer; random bytes come from `BCryptGenRandom`.
- Names cross into Win32 as UTF-16: paths, the environment and the arguments (from
  `GetCommandLineW`) are converted from and to UTF-8 at the boundary. Files are C
  runtime descriptors opened with `CreateFileW` (so all sharing modes are allowed,
  deleting an open file included, as with Rust's `File`) and `_open_osfhandle` in
  binary mode; the standard streams are switched to binary mode by `gm_run_main`, which
  also sets the console's output code page to UTF-8 when standard output is a console.
  Directories are `FindFirstFileW` searches. Win32 errors map to the same `IoError`
  kinds the interpreter derives from Rust's `ErrorKind`, and any other error is
  `Other(<Win32 code>)`.
- Sockets are Winsock 2 (`WSAStartup` on first use); `SO_REUSEADDR` is not set on
  listeners, since on Windows it allows taking over a port in use.
- A trap exits with status 134 as elsewhere. `roundeven`, which MinGW's C library
  lacks, is `rint` (`GM_ROUNDEVEN`).
- The C back end passes no `-pthread` and adds `-static -lws2_32 -lsynchronization
  -lshell32 -lbcrypt` (`cgen::windows_libs`); executables get the extension `.exe`.
- The Rust side compiles for `x86_64-pc-windows-msvc` or `-gnu`. Source file names are
  stored with `/` (`SourceMap::add`) so that diagnostics read the same on every system, and
  a crash status (an NT status such as `0xC0000005`) is reported as 134.

**WASI.** For `wasm32-wasi` (`__wasi__`, `target.os` `.Wasi`) the runtime keeps its POSIX
layer, as wasi-libc provides it, and replaces what WASI lacks (`GM_WASI` in
`runtime/gm_rt.h`):

- There are no threads: the runtime's lock does nothing, `gm_thread_spawn` fails with
  `ENOTSUP` (which `gm_io_error`, like `ENOSYS`, maps to `IoError.Unsupported`), a futex
  wait sleeps out its timeout, and thread-local storage is a plain global. The entry
  function runs on the module's own stack, 64 MiB, placed first in memory
  (`--stack-first`), whose bounds the linker's `__stack_low` / `__stack_high` give the
  stack checks. `Net` and `Proc` operations fail with `ENOTSUP`. Random bytes come from
  `getentropy`.
- WebAssembly cannot switch stacks, so fiber switches use Binaryen's Asyncify. Every
  context (the entry function's and each fiber's) is resumed by one driver,
  `gm_wasi_drive`, at the bottom of the module's stack: `gm_rt_switch` records the
  target, starts an unwind (Asyncify saves each frame's locals into a buffer at the low
  end of the suspending context's own stack, below its frames) and returns to the driver,
  which sets the stack pointer to the target's stack and calls its entry function with a
  rewind started, which rebuilds its frames and returns from its own `gm_rt_switch`. The
  cost of a switch is therefore proportional to the depth of both stacks. A buffer that
  would reach the frames is reported as a stack overflow.
- The C back end links without optimizing (Clang would otherwise run `wasm-opt` before
  the transformation, and its inlining would move code into the functions Asyncify must
  leave alone), then `cgen/wasi.rs` runs `wasm-opt --asyncify` and the build's
  optimization level over the module. Only a module that imports from `asyncify` is
  transformed; the generated `main` calls `gm_run_main_fibers`, the only function that
  reaches the driver, only when the program uses `with ... on` (`gm_with_go`), so a
  program without fibers keeps its size and speed.
- Function "addresses" are table indices, so the tag that marks a plain function in a
  function value is the top bit (`GM_PTR_TAG_BIT`) rather than bit 0.
- WASI's `proc_exit` refuses statuses of 126 and above, so a trap reports and then
  executes `unreachable` (`__builtin_trap`); Wasmtime then exits with 134 as a native
  program would, and `gamma` drops the report Wasmtime adds (`toolchain::strip_wasm_trap_report`).
  Modules are run with Wasmtime (`toolchain::WASI_RUNNER`), the current directory granted,
  the environment inherited and the native stack for WebAssembly frames raised to 256 MiB
  so that the program's own stack overflows first.

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
type (`#packed` and `#bits` structs, zero-size types)
is a *blob*, a struct holding its bytes with the layout's size and alignment.
Typed structs are not used on 32-bit targets, where C and Gamma align 64-bit
members differently, nor under Fil-C, where a blob of pointer-sized words is
made of members the copy moves one by one (`types::BlobKind`): words when the
value holds no pointer; words typed as pointers or numbers when its pointers are
always at the same offsets (a struct of pointers and numbers), so that the copy
keeps their capabilities; and otherwise, as for a union whose variants hold a
pointer and a number in the same word, a C union of the bytes with
pointer-sized lanes. A struct of pointers would not do there: where the same
word is also read as a number, the optimizer turns the pointer into a number and
back, which drops its capability. Copying a byte array would be a call into the
Fil-C runtime.

**Byte order.** The C back end and the runtime work on little- and big-endian
targets. An integer (at most 128 bits wide)
is one number in the target's byte order (C's own layout, read with `GM_LD`/`GM_ST`).
Code that must address the bytes of an integer goes through `gm_ibp` (the i-th least
significant byte) instead of indexing, and `gm_load_u128` /
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

**Size of the generated C.** The C compiler's time grows faster than the size of a
function, so code that every site would repeat is shared. A write to the ambient
`Out` or `Err` (`print`, `@write`) is a call of one small function per program and
signature (`gm_pfN`, in `Gen::stubs`) that finds the handler and calls it, and each
literal or formatted piece of a template is one call of a second function that builds
the string and returns whether an unwind began. Generated programs with thousands of
prints in one `main` took a minute to compile before; the stubs and the register
`@black_box` cut the compile time of a 9,400-line `main` of such statements
from over 60 s to about 20 s, and the generated C from about 30 lines per statement to
about 14. What remains grows faster than linearly with the size of one function
(splitting the same statements over eight functions halves it again).

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
elements, string text, `null` and `!error`. For lldb, `gamma build -g` writes the
same conventions as a Python module (`src/cgen/lldb_formatters.py`, embedded in the
compiler) to `gamma_lldb.py` next to the executable; it registers summaries and
synthetic children providers for the view tags in a `gamma` category, looking through
the anonymous union of tagged unions, and is loaded with `command script import`. The generated code never refers to these names, but
they may be macros of system headers (`linux`, `st_mtime`), so the declarations
that contain them suspend such macros with `#pragma push_macro`. Locals that
share a name, and names that are C keywords or reserved, keep only their `lN`
name. A Debug build with `-g` is compiled with `-O0` so that every local is
visible (the cached runtime object is compiled with `-g -O2`); Checked and Fast
builds keep `-O2`.

**Fibers.** The runtime switches stacks with a few lines of assembly for x86-64,
AArch64 and s390x that save the callee-saved registers. (On Windows the x86-64 version
follows the Win64 convention: it also saves `rsi`, `rdi` and `xmm6`-`xmm15`, and swaps the
stack bounds in the thread information block, `StackBase`, `StackLimit` and
`DeallocationStack`, which the system reads when it dispatches an exception, so that
code on a fiber's stack, which is ordinary heap memory, is on a valid stack as far as
Windows can tell.) The body of a `with … on` is
moved into a C function of its own, which reaches the enclosing function's frame
through a pointer, and runs on a fiber whose stack is the given memory. The
`with` itself becomes the driver loop. A `ctl` perform stores its arguments in a
request on the fiber's stack and switches to the driver, which calls the clause
through a small per-site thunk. `resume` and the cancellation by `cancel` are
two more kinds of non-local exit: `resume` ends the clause and tells the driver
which fiber to switch to; a cancelled fiber unwinds to its base and switches
back. Fiber records come from a pool and are never freed, and each suspension
increments the fiber's epoch, so a continuation (fiber, epoch) that was already
consumed is always detected. In Debug and Checked builds every function entry
compares its frame's address with the lowest address the running stack may use:
the fiber's limit, or, on a thread's own stack, the bottom of that stack (found
with `pthread_getattr_np` or `pthread_get_stackaddr_np` at the start of `main`
and of each thread) plus a reserve for reporting the overflow. The check also counts the bytes of the C temporaries the function declares, since they may lie below its frame. A frame too large for what is left can still fault in the C compiler's prologue (which probes each page before the check runs), so each thread also has a `SIGSEGV` handler on an alternate signal stack that reports a fault just below the stack's limit as the same stack overflow trap. (On Windows the stack's bounds come from `GetCurrentThreadStackLimits`, each thread keeps 64 KiB of guaranteed stack with `SetThreadStackGuarantee`, and a vectored exception handler reports `EXCEPTION_STACK_OVERFLOW` and the same faults.) Large values read from a place are copied with `memmove` rather than through a C temporary.

**Formatting and comparison.** `@write` formats through the runtime's
`gm_format`, driven by type descriptors generated for each formatted type.
Structural `is`, ordering and `@hash` use C functions generated per type
(`src/cgen/helpers.rs`) that agree with the interpreter, including the hash
values, so hash maps iterate in the same order.

**Calling conventions and foreign code.** A function whose effects are at most
`Sys` takes no hidden parameters: it has the plain C calling convention, so it
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
signatures (foreign functions, exports, functions with at most `Sys`, and
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
from a local or through a pointer, or from a field (of a field…) of one, is passed as
that place (`FnGen::in_place_root`) when no later argument
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
the caller's value runs during the call. It is also read in place when every
call passes a value that nothing writes while the callee runs: a constant from
static storage (`constarg.rs`), a temporary, or a local of the caller that is
never assigned, addressed or captured after its initialization. That needs every
call to be in sight, so the function must be a named one whose address is never
taken, nor exported; and its parameter must not be captured by a function
literal. The typical case is the `SourceLocation` that `@caller_location()` gives
`new` and `destroy`, and that they pass on.

Generated code is compiled with `-fno-strict-aliasing`. Values are byte blobs
written and read piecewise with `memcpy`, so type-based alias analysis gains
nothing, and gcc 13 at `-O2` was seen to drop such writes under it.

## The Cranelift back end

`--backend=cranelift` (`src/clif/`) translates the checked program from TIR to Cranelift
IR and emits one object file with `cranelift-object`; the C compiler of the toolchain
links it with the runtime. The user-facing description is
[`docs/guide/native-backend.md`](guide/native-backend.md). The back end targets 64-bit
Linux and macOS on x86-64 and AArch64 (`clif::check_target`), and translates TIR
directly rather than the interpreter's bytecode, whose registers and values are tied to
the interpreter's memory model.

**One runtime, two back ends.** The C runtime (`runtime/gm_rt.c`, compiled with
`GM_RT_SEPARATE` as for split Debug builds) and its glue for this back end
(`runtime/gm_clif.c`) are compiled together into one object, cached like the C back end's
runtime object (`cgen::runtime_object_from`). The glue holds what the C back end writes
into each program: the process's `main` (which reads the entry table `gm_clif_prog`),
equality, ordering and hashing driven by type descriptors instead of a function per type,
the checks whose trap messages are formatted, arithmetic on `i128`/`u128` and `f16`, and
`@write` to an output effect. Generated code reaches the runtime's records (`gm_node`,
`gm_fiber`, `gm_unwind`, …) at fixed offsets, which static assertions in the glue pin
down. Values have the bytes `layout.rs` gives them, as on the other back ends, and traps
go through the same runtime functions, so messages, locations and exit statuses agree.

**Values.** `Gen::rep` maps each type to a representation: integers (normalized: sign- or
zero-extended from their width), `bool` as an `i8`, `f16` as its bits in an `i16`, `f32`,
`f64`, pointers (and optional pointers, null being 0), values without bytes, and
everything else in memory, handled by address. Scalar locals that are neither captured
nor addressed are SSA variables; the others live in the frame, one stack slot laid out
like the C back end's frame struct, with its `{up, clause_lit}` header first, so the
runtime and nested functions reach it the same way.

**Calling conventions.** Gamma functions call each other with the back end's own
convention: a function that is not plain takes `([result address], env, cx, inst,
params…)`, a plain one (§19.1) `([result address], params…)`; scalars travel in
registers and values in memory as the address of a copy, which the callee copies into its
frame. Plain function pointers are tagged with their low bit where effectful code is
expected, as in the C back end, so every function is aligned to 16 bytes. Foreign calls
and `#export` use the platform's C convention (`clif/cabi.rs`): aggregates are classified
by the System V x86-64 rules (eightbytes of class INTEGER or SSE, larger ones in memory),
by AAPCS64 (homogeneous floating-point aggregates, one or two integer registers, larger
ones by reference) or as Windows x64 passes them, and an export whose C signature differs
from its plain one gets a thunk that converts. Calls through a plain `^fn` whose C signature
differs from the plain one (it passes an aggregate by value) use the C convention too, since
the pointer may come from C (§19.2): a Gamma function stored in one is the address of such a
thunk, and a second-class value made from one at run time calls it through a shim that takes
the hidden convention and calls the pointer, its environment, as C does. Two details Cranelift leaves to the
front end: a variadic call with floats on x86-64 goes through a ten-byte trampoline that
sets `al` (System V's count of vector registers) before jumping to the callee, and on
Apple's AArch64 the unused argument registers are filled so that the variadic arguments
land on the stack, where Apple's convention puts them.

**Control and effects.** Effects compile to evidence passing as in the C back end
(Appendix D): handler nodes (`gm_node`) linked in a context, clauses called through the
node, and non-local exits that set the thread's `gm_unw` record and jump down the
*ladder* of the current scope, a block per scope that runs the pending `defer`s and tests
whether the exit targets an enclosing loop, `with` or function. The body of a
`with … on` (§14) is a function of its own that runs on a fiber of the runtime, `ctl`
operations are performed through clause thunks, and `@fiber` gets a start function;
the runtime switches stacks, as for the C back end.

**Inline assembly.** Cranelift has none, so each `@asm` becomes a C function holding the
`__asm__` statement (operands are the places the arguments name, reached through an array
of addresses), compiled with the C compiler and linked with the program.

| File | Responsibility |
| --- | --- |
| `mod.rs` | `Gen`: the module, representations and signatures, exports, entry table, the link |
| `func.rs` | frames, locals, the ladder, statements, and the generated functions (`with … on` bodies, clause thunks, fiber starts, entry wrappers, export thunks) |
| `expr.rs` | expressions, places, coercions, calls (including foreign calls), effects, fibers, patterns |
| `ops.rs` | operators, conversions, checks, builtins, vectors, `@asm` |
| `cabi.rs` | the C calling conventions for aggregates |
| `data.rs` | constants, statics, strings and type descriptors |

`GAMMA_DUMP_CLIF=1` prints the Cranelift IR of each function as it is defined; in
builds with debug assertions the Cranelift verifier also checks every function.

## Tests and benchmarks

`test "name" { … }` and `bench "name" { … }` are one syntax-tree item (`ItemKind::Test`, with a `bench` flag) and are collected only when `tests_enabled` is set (`gamma test`, `gamma bench`, the language server). `sema/tests_decl.rs` turns each body into a function whose error type is inferred from its first `try`, and wraps it in an entry function that calls a runner of the core prelude: `run_test` / `run_test_unit` (tracking allocator, test handlers, leak check) for a test, `run_bench` / `run_bench_unit` for a benchmark. The entries go to `Program::tests` and `Program::benches`. A benchmark's entry is wrapped like `main` (`entry_point`), so it runs under the system handlers. `run_bench` finds the number of runs per sample by growing batches, takes 20 timed samples with the `Clock` effect, and prints one line, `gamma-bench: <runs> <ns> <ns> …`, that `src/testing.rs` turns into the report (the benchmark's own output precedes it). `@black_box` is `Builtin::BlackBox`: the identity in the interpreter, and in C an empty `asm` statement that may change the value (integers and booleans; Fil-C and other types: a round trip through a `volatile` pointer).

`src/testing.rs` selects the cases (`--filter`), runs them and prints the results in order. `pool` runs the jobs on `--jobs` threads and hands the results over in job order. The interpreter's state is not shared between threads, so each thread analyzes the program again (the analysis is deterministic, so test number `i` is the same everywhere); native runs only start processes. `--all` makes `analyze_package_tests` load every directory of the package (`Sema::load_package_modules`).

Coverage (`--coverage`) is a rewrite of the checked program (`src/coverage.rs`): before each statement, and before the value of each block, of the functions of the package that are not tests, it inserts `Builtin::Cover(n)`, where `n` numbers a source line. The interpreter counts executions in process-wide atomic counters (`interp/coverage.rs`). Nothing is inserted, and nothing slows down, without the option; the C back end does not support the builtin, so coverage needs the interpreter.

## API documentation

`gamma doc` (`src/doc/`) works on the syntax tree only, so a module needs to parse but
not to type-check. `collect.rs` reads the `pub` declarations of a module (a directory or a
file), copies each signature from the source text between the spans the parser recorded, and
attaches its `///` comments and the `//!` comments at the top of the module's files.
`markdown.rs` and `html.rs` render a module and the index of a set of modules, and
`md_html.rs` is the small Markdown renderer for doc comments (everything is escaped). The
output depends only on the sources, never on the machine or the time, so the standard library
reference in `docs/std/` is checked in and `tests/doc.rs` fails when it is stale.

## REPL

`gamma repl` (`src/repl/`) has no incremental compilation or persistent interpreter state to build on: every `Machine` belongs to one `tir::Program`, and locals die when `main` returns. Each input is therefore compiled as a complete program from the session's state, with `sema::analyze` over an in-memory file (`Session::overlays`):

| Part of the file | Content |
| --- | --- |
| helpers | `__repl_type`, which names the type an expression gets by default (`1 + 2`) |
| declarations | the source text of each earlier top-level item, and for a declaration input the new text |
| `main` | the kept statements, the replay marker, then the new input |

- `report.rs` turns the output into what the user typed: diagnostics relative to the input, trap reports, and the filter that hides the output of replayed statements.
- `input.rs` classifies text by parsing it twice, as top-level items and as the body of a function. `x := e` parses as an item but is not a declaration, so it is read as a statement. Items that are plain `name :: value` bindings are tried as constants first and as local variables if that fails.
- `main` returns `AllocError!()` so `try` works on allocation.
- An expression statement is tried as written (it must have type `()`), then as `__repl_v := e` followed by `print("{}\n", __repl_v)`.
- Variables are replayed before the new input, with the output of `Out` and `Err` hidden until a marker string (`report::Filter`). A statement is kept if it checks with the effect set `Alloc` and the kept statements before it, which separates repeatable statements from those with visible effects. A variable defined from an effectful expression is kept anyway, and flagged.
- Redefining a variable renames the new one (`x__2`) and rewrites later inputs at the token level (identifiers after `.` or before `:` are left alone).
- `report::render_region` maps diagnostics in the input to positions relative to it; errors elsewhere in the synthesized file are shown against the whole text.

## Language server

`gamma lsp` serves the Language Server Protocol over standard input and output.
Users are pointed to `docs/lsp.md`; this section is about how it works.

### Structure

| File | Responsibility |
| --- | --- |
| `src/lsp/transport.rs` | JSON-RPC messages with `Content-Length` framing |
| `src/lsp/position.rs` | byte offsets to and from LSP positions (UTF-16 units, or bytes when the client offers UTF-8) |
| `src/lsp/analysis.rs` | one run of the compiler over a module, and its diagnostics as LSP diagnostics |
| `src/lsp/features.rs` | hover, definition, completion, signature help and symbols, as JSON |
| `src/lsp/refs.rs` | find references and rename across the modules of a package |
| `src/lsp/workspace.rs` | the modules of a package, and a cache of what was learned from checking each |
| `src/lsp/corelib.rs` | files for the embedded core library, so that definitions in it can be opened |
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
The diagnostics stay in the `Session`. `Analysis::run` wraps the call in
`catch_unwind`. If the checker panics, the run is repeated as `gamma test` would
do it, which stops at syntax errors, and those errors are reported. If that
fails too, the user sees one "internal compiler error" diagnostic on the first
open file of the directory.

Diagnostics are published for every open document of the directory and for every
other file that has diagnostics. Files that had diagnostics before and have none
now get an empty list. A diagnostic without a location goes to line 0 of the
first open document. Labels become `relatedInformation`; the explanations that a
diagnostic carries are appended to its message.

The type interner never frees anything, and a run allocates fresh ids for every
struct, union and effect, so a server that runs for days would grow without bound. The index
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

Doc comments come from `ast::Item::doc`. The embedded core library is compiled
from text under the file names `<core>/name.gm`; `lsp/corelib.rs` maps such a name
to `lib/core/name.gm` next to `lib/std` when that file has the embedded text, and
otherwise to a read-only copy in the cache directory named after a hash of the
text, so definition and hover work for it. `print` and the other builtin
functions are not declared in any source, so they have no location.

A declared name is found at a position through the references that point to it,
and also through the outline, the local scopes and the hover table, so that a
declaration that is not used in its own module (the usual case for the `pub`
names of a library) can still be asked about.

### Syntax errors

The parser recovers at statements, match arms, fields and declarations, so a
statement that does not parse becomes an error expression and the rest of the
function is checked. What it cannot do is notice that a block is unclosed: a
missing `}` or `)` makes every later declaration part of the open construct. In
the editor path (`Session::recover_layout`, used by `analyze_for_ide` and by the
index's own parse) a file whose lexing had errors is lexed again with
`lexer::lex_recovering`, which closes all open brackets, reporting each as
unclosed, wherever a line starts in the first column with `name ::`, `pub`,
`handler`, `test "…"` or an attribute. Gamma code is formatted with declarations
in the first column and nothing else is, and the second pass runs only if the
text already has errors, so well-formed text, and ordinary compilation, are
unaffected.

### References and rename

References across modules need the analyses of modules other than the open one.
`lsp/workspace.rs` finds the module directories under the package root and the
roots of its path dependencies, and `Workspace::facts` analyzes one, keeping a
`ModuleFacts`: its references as (file path, offsets) pairs for uses in its own
files, the text of its files, its local scopes, and for each declared name the
names declared next to it (the top level of the module, or the fields or variants
of one type). Nothing in it refers to the compiler's types, so it survives the
release of an analysis's types. An entry is valid while a hash of the files that
the analysis read (the list of `.gm` files of each directory and their contents,
buffers instead of disk) is unchanged. Only directories whose files contain the
name are analyzed. Locals and parameters are searched in the open module alone.

Rename finds the declaration and its uses this way, then refuses if the target is in
the core or standard library or outside the package root, if the new name is not
an identifier, is a keyword or names a builtin type, if a sibling declaration or an
overlapping local scope has the name, or if a local named so is visible at one of
the uses. The edits are the exact name ranges, narrowed from the recorded span
when it covers more than the name (as `a.name` may).

### Completion

Names come from the locals visible at the cursor, the declarations of the module
and the universe scope. Members after `.` come from the index of the current
text when it has the receiver. Typing `x.` usually leaves text that does not
check. The server then takes out the dot and what was typed after it, checks the
module again with that text, and looks up the receiver by its byte range, which
the edit did not move.

### Limitations

- A name in a part of the file that is not taken by a top-level `when` is not
  resolved.
- Tuple element names and the members of builtin types are not references; the
  builtin methods of numbers and slices are listed by name only.
- Completion after `.` re-checks the module when the current text has no receiver
  for the dot, which is as slow as an edit.
- The layout heuristic of syntax-error recovery assumes declarations start in the
  first column and nothing else does; an unclosed bracket in a file that is not so
  formatted still swallows the rest of its construct.
- Rename does not check for a clash between the new name and a name in an
  importing module's own scope beyond the locals at the uses.

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
