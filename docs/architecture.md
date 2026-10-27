# Compiler architecture

This document describes how `gamma`, the Gamma compiler, is organized. It is written in
Gamma (`compiler/`, package `gammac`; the executable is `gamma`) and builds itself from a
checked-in C seed ([`self-hosting.md`](self-hosting.md)). The document covers the stages a
program passes through, where each stage lives, and the invariants that connect them.

## Pipeline

```
source text
  │  syntax/                 tokens with significant newlines (spec §2)
  ▼
tokens
  │  parse/                  abstract syntax tree (spec Appendix A; ast/)
  ▼
AST
  │  load/                   modules, packages and their top-level names
  │  sema/                   declarations: types, signatures, constants
  │  check/                  function bodies: names, types, effects, literal inference,
  │                          exhaustiveness
  ▼
TIR                          typed intermediate representation (tir/)
  ├─▶ eval/                  compile-time evaluation (spec §17)
  ├─▶ interp/                interpreter: gamma run --backend=interp, gamma repl
  └─▶ cgen/                  C code generation → system C compiler → executable
```

`main.gm` parses the command line and dispatches; `driver/` holds what the commands share
beyond the stages (the options, the C toolchain and the target, the cache of compiled
runtimes, the library paths, native builds).

### Why an interpreter *and* a code generator

Gamma requires compile-time evaluation of ordinary functions (spec §17): array lengths,
top-level constants and arguments for compile-time parameters are computed by running Gamma
code inside the compiler, with every checked operation checked and undefined behavior
detected. That needs an evaluator that does not depend on a C compiler. It works on the
same typed IR as the code generator (`eval/` for compile-time values, `interp/` for whole
programs), which gives programs a development loop with no C compiler and a reference
semantics that the native back end is tested against: every `run` conformance test runs on
both and the results must agree.

### Why C as the native target

Generating C keeps the implementation portable and lets the system C compiler do register
allocation and optimization. The features that C cannot express directly are provided by a
small runtime library (`runtime/gm_rt.c`, `runtime/gm_rt.h`): stack switching for fibers
(spec §14), value formatting, and trap reporting. The effect system compiles to *evidence
passing* and *exit flags* (spec Appendix D), which are plain C. C is also what makes the
bootstrap seed possible: the compiler's own generated C, compiled by any C compiler, is the
compiler.

### Demand-driven semantic analysis

Type checking and compile-time evaluation depend on each other: checking `buf: [BLOCK * 2]u8`
needs the value of `BLOCK`, which may be computed by a function that must itself be checked
first. Semantic analysis is therefore demand-driven: each top-level declaration is resolved
when it is first needed (`resolve_decl` in `compiler/sema/sema.gm`), and a declaration that
is requested while it is being resolved is a dependency cycle, reported with every
declaration in it (spec §3.4). Function bodies are checked after the declarations, and the
bodies that compile-time evaluation reaches are checked on demand first.

## Source layout

Every directory of `compiler/` is a module (spec §3.1); `compiler/gamma.toml` is the
package manifest and `compiler/main.gm` the root module.

| Path | Contents |
| --- | --- |
| `compiler/main.gm` | the `gamma` command line: options and dispatch |
| `compiler/source/` | source files, byte spans, line and column lookup |
| `compiler/diag/` | diagnostics, their fixes, and their `human`, `short` and `json` renderings |
| `compiler/syntax/` | tokens, the lexer, the exact values of literals, the token dump of `gamma lex` |
| `compiler/ast/` | the syntax tree and its stable text dump (`gamma parse --dump`) |
| `compiler/parse/` | the parser: `parser.gm` (state and error recovery), `expr.gm`, `stmt.gm` (blocks, statements, control flow), `items.gm` (declarations), `types.gm`, `pattern.gm`, `lits.gm` (type, handler and foreign literals) |
| `compiler/bigint/`, `compiler/bignum/`, `compiler/ratio/` | arbitrary-precision integers, natural numbers and exact rationals, for the values of integer and float constants |
| `compiler/types/` | semantic types (hash-consed `Ty`), their display in messages, vector types |
| `compiler/load/` | loading the root module and every module its imports name; packages from `gamma.toml` (`manifest.gm`, `toml.gm`, `semver.gm`); top-level `when` (`when.gm`); the core library (`core.gm`); the compilation target (`target.gm`); the path of the running executable (`exe.gm`); file paths (`paths.gm`) |
| `compiler/sema/` | semantic analysis of declarations: names and scopes, structs and unions, type expressions, function signatures and effect sets, constraints, untyped constants, constants, layout sizes, `foreign` blocks, `#soa` types; the `gamma decls` dump |
| `compiler/check/` | checking function bodies and lowering them to TIR: expressions, calls, methods, patterns, `match`, exhaustiveness, literal inference, effects, handlers, iterators, generics, escape and dangling-pointer checks, continuations, `@stack_size`, format templates, vectors; the `gamma tir` dump |
| `compiler/tir/` | the typed intermediate representation, compile-time values, deep copies for generic instances |
| `compiler/eval/` | compile-time evaluation (spec §17) over TIR |
| `compiler/interp/` | the interpreter |
| `compiler/cgen/` | the C back end |
| `compiler/driver/` | the C toolchain and target selection, native builds, the cache of runtimes and programs, library paths, the interpreter entry, the options |
| `compiler/pkg/` | the package manager: git and archive dependencies, resolution, `gamma.lock`, build settings, the cache, `gamma new`, and the C sources and libraries of native builds |
| `compiler/testing/` | `gamma test` and `gamma bench`: running, reporting, coverage |
| `compiler/fmt/` | the source formatter behind `gamma fmt` |
| `compiler/doc/` | the API documentation generator behind `gamma doc` |
| `compiler/lint/` | the lint rules and their levels |
| `compiler/linting/` | the `gamma lint` command line and its `--fix` rounds |
| `compiler/fix/` | choosing and applying the fixes that diagnostics carry (`gamma fix`) |
| `compiler/repl/` | the REPL behind `gamma repl` |
| `compiler/ide/` | the editor index: references, hover text, scopes and outlines of one module |
| `compiler/lsp/` | the language server behind `gamma lsp` |

Outside `compiler/`:

| Path | Contents |
| --- | --- |
| `runtime/` | the C runtime of compiled programs, with the gdb and lldb scripts of `-g` builds and its own C tests (`rt_test.c`) |
| `lib/core/` | the core library: the prelude, `List`, hash maps, slices and complex numbers, written in Gamma |
| `lib/std/` | the rest of the standard library (see [`std.md`](std.md); its reference, [`std/`](std/index.md), is generated by `gamma doc`) |
| `bootstrap/` | the C seed and the scripts that build and verify `gamma` with only a C compiler (see [`self-hosting.md`](self-hosting.md)) |
| `tests/harness/` | the test harness, written in Gamma, one module per suite (see [`contributing.md`](contributing.md)) |
| `tests/conformance/`, `tests/examples/` | the conformance tests, grouped by area, and the cases that run `examples/` |
| `fuzz/` | the libFuzzer targets of the compiler |
| `tools/` | `test.sh`, the release packaging and install scripts, the seed refresh, and generators for checked-in files (the Unicode tables of `std/text`, the float printing table) |
| `editors/` | editor support: TextMate and tree-sitter grammars, extensions for VS Code, Zed and Emacs (see [`editors.md`](editors.md)) |
| `web/` | the website generator and the playground |

## Front end

### Lexer

The lexer (`compiler/syntax/lexer.gm`) turns UTF-8 source into tokens and decides which
newlines are significant (spec §2.8), so the parser sees a `Newline` token exactly where a
statement or declaration may end. It keeps a stack of open brackets: inside `( )` and `[ ]`
newlines are dropped; inside `{ }` and at top level they are kept unless the previous token
is a binary operator (or another token that cannot end an expression) or the next line
begins with `else`, `and`, `or` or `.name`. A `{ }` that is the body of a `match` is tracked
separately, because there every line starts a new arm.

The lexer also forms the two-word operators `is not` and `not in`, records `///`
documentation comments, and computes the exact values of literals (`literal.gm`): integers
as unbounded naturals and floats as exact rationals, since literals are typed and rounded
later (spec §6.1).

### Parser

The parser is a hand-written recursive-descent parser with precedence climbing for
expressions. Gamma treats types as compile-time values, so the syntax tree has a single
expression type that also covers type syntax (`?T`, `[]T`, `fn(…) -> R`, `struct { … }`, …).

A few constructs cannot be classified by syntax alone; the parser builds one node and leaves
the decision to name resolution (spec §A.7):

- `E!T` (error-union type) versus `x!` (unwrap) followed by more postfix operators: the
  parser attempts to parse a type after `!` and, if that succeeds, produces a `Bang` node.
- `?T(x)` and `[3]u8(1, 2, 3)`: a prefix type operator applies to the following postfix
  expression, including a call; whether that call is a generic type application
  (`?List(u8)`) or a construction is known only after resolution.
- The right operand of `is` is parsed as an expression and turned into a pattern when it
  contains `_` or a variant without its payload.

Other ambiguities are resolved syntactically: the first `{` after the head of an `if`,
`while`, `for`, `match`, `with` or `when` always begins its body, and a label in `break L`
is recognized because the parser tracks the labels of the enclosing loops.

Every node carries a `NodeId`, unique within a compilation, so later stages can attach types
and other facts in side tables instead of mutating the tree.

## Diagnostics

All stages report problems to a shared `Diagnostics` collector (`compiler/diag/`). Messages
are lowercase and specific. The `--diag-format=short` flag prints one
`path:line:col: severity: message` line per diagnostic, followed by a `note` line for each
secondary location and each attached explanation; the conformance tests match against it.
`--diag-format=json` prints one JSON object per diagnostic, with byte offsets, lines and
columns, labels, notes and fixes.

A diagnostic may carry *fixes*: source edits that resolve it, each a replacement of a span's
text. Human-oriented output does not show them, since the message says what to write; the
language server sends a diagnostic's fixes along with it in the `data` field and returns them
as quick fixes when the client asks for code actions on that diagnostic; and `gamma fix`
(`compiler/fix/`) checks the program, applies every fix that is the only one of its
diagnostic to the files of the input's package (never the standard library or other
packages), skipping fixes that overlap, and checks again until nothing is left to apply,
since a fix can let checking get further (a syntax error stops analysis). Fixes are attached
where the right edit is unambiguous: the lexer (uppercase radix prefixes), the parser
(`&&`/`||`, `else` as a pattern, a trailing comma after a match arm, `try x else d` with both
readings) and semantic analysis (an unused value, `s.len()` on a slice, a payload variant
pattern without `(_)`, `x := v` at top level). To tell a comma at the end of a line from one
in the middle, the lexer records the line breaks it drops after a token that continues the
line.

Cascades are kept down in three ways. `Diagnostics` drops a diagnostic that repeats an
earlier one with the same severity, location and message, and an error whose message
mentions the error type `{error}`, which only restates an earlier error. The parser reports a
keyword used as a name once and reads later uses of the word as that name. Semantic analysis
gives an expression that is already wrong the error type, which every later check accepts
silently; a statement whose expression reported an error does not also get the "value is
unused" error.

An "unreachable code" error carries a note at the construct that ends the control flow.

Problems in the input are diagnostics and never traps; only a failure of the environment (an
unreadable file) is an error value. A trap inside the compiler ends the process, since traps
cannot be caught.

## Modules and packages

`compiler/load/` loads the root module (a file or a directory) and, transitively, every
module its imports name, and collects each module's top-level names, including those of the
taken branches of top-level `when`s (`when.gm` evaluates the conditions, which decide what a
module declares and therefore what it imports). Package roots come from `gamma.toml`
(`manifest.gm`, on the TOML reader `toml.gm`); version requirements are in `semver.gm`.
`gamma modules` prints the module graph. The loader accepts *overlays* (the text of open
editor buffers stands in for files on disk) and can `keep_going` past syntax errors; the
language server uses both.

The core library (`lib/core/`) is loaded before the program (`core.gm`); the prelude's public
names are in every module's scope.

`compiler/pkg/` is everything about dependencies. It does not depend on semantic analysis.

| File | Contents |
| --- | --- |
| `git.gm` | the `git` command-line tool, the on-disk store (`<cache>/git/db`, `<cache>/git/checkouts`, `<cache>/git/url`), the SHA-256 checksum of a tree (symbolic links included), making checkouts read-only and removing them, the `.used` stamps |
| `archive.gm` | `url = …` dependencies: download with `curl`, unpack `.tar.gz` safely, strip a single top-level directory |
| `resolve.gm` | the resolver: version selection, lock reuse, checkouts and archives, cycle and path-escape checks |
| `lock.gm` | `gamma.lock`: deterministic rendering and parsing (git entries and archive entries) |
| `build.gm` | build settings: the root package's settings for a profile, and the collection of C sources, include and library directories from the whole dependency graph |
| `link.gm` | what a native build links besides the generated program: the C compiler's command and flags, the packages' C sources compiled to objects, and their libraries |
| `cache.gm` | `gamma cache clean`: garbage collection by last use |
| `edit.gm` | format-preserving manifest edits for `gamma add` and `gamma remove` (it edits the lines of the text, which keeps comments and layout) |
| `scaffold.gm` | the files of `gamma new` and `gamma init` |
| `cli.gm` | argument handling of the package commands, which `main.gm` dispatches before it parses compiler options |
| `sync.gm` | resolve, fetch and write the lock; what the compiler calls before it loads a program |
| `support.gm` | shared helpers, the cache directory |

The manifest parser and version requirements are in `compiler/load/`, because the loader
reads manifests too.

Before `check`, `build`, `run`, `test`, `bench`, `lint` and `fix` analyze a program, `main.gm`
finds the root package (the nearest `gamma.toml`), walks the dependency graph (path
dependencies too, since they may have git or archive dependencies), and hands the loader a
map from package directory to dependency name to dependency directory.

The resolver identifies a git package by its normalized URL and `subdir`, collects every
requirement on it, and chooses one revision: the locked one if it still satisfies all
requirements, else the highest matching tag, the branch head, or the commit. A choice that a
later requirement contradicts restarts the walk with the larger requirement set; requirements
only grow, so it terminates. Manifests are read from checkouts, which are made (into a
temporary directory, then renamed) as soon as a revision is chosen. Checkouts are named by
revision, so choosing again is free.

Design points for contributors:

- The lockfile fixes the commit; the checksum is a second line of defense and is checked only
  when a checkout is created. Hashing every use would read the whole tree on every build.
- A request for the default branch yields to any other requirement on the same repository.
- Symbolic links are part of the checksum, with a record that no regular file can produce
  (its length field is `u64::MAX`); trees without links hash as before, so old lockfiles stay
  valid. A link that is absolute or climbs out of the tree is an error when the checksum is
  computed, i.e. before the checkout is used (a bad checkout is deleted).
- Checkouts and unpacked archives are made read-only after verification; everything that
  deletes one restores permissions first. Last use is the mtime of a `<dir>.used` file beside
  the checkout, since the checkout cannot hold a stamp.
- Archives are keyed by the SHA-256 of the file, so a URL whose content changes gets a new
  directory. The lock stores both that hash and the tree checksum; `sha256` in the manifest
  wins over the lock when they differ (the author changed it on purpose). `update` does
  nothing to an archive whose URL is unchanged.
- The `pkg` suite of the test harness builds local repositories and archives (`file://` URLs,
  through `curl`) and runs the real executable with a private `GAMMA_CACHE_DIR`; it needs
  `git` and `curl` but no network.

### Build settings and C sources

`main.gm` reads the root package's build settings (`build.gm`) right after parsing the
command line, applies `mode` and `debug-info` under any explicit option, and puts the
manifest's `cflags` and `ldflags` in front of the environment's in the toolchain. After the
dependencies are resolved, the graph is walked once more for the C files with their include
directories, the libraries and the library directories. A native build compiles each C file
again every time (there is no object cache), each with the include directories of its own
package; a program with such sources is not kept in the cache of built programs either. The
interpreter cannot load foreign code, so a program with C sources or `foreign` functions runs
through the C back end.

## Semantic analysis

Semantic analysis resolves names, checks types and effects, and lowers each function body to
TIR. It has two parts. `compiler/sema/` resolves declarations without looking at function
bodies; `compiler/check/` checks bodies, each inside the context of its function, and
produces the typed program. `gamma decls` and `gamma tir` print the result of each part in a
stable text form, which tests of the compiler use.

| Area | Where |
| --- | --- |
| the `Sema` context, resolving declarations on demand with cycle detection | `sema/sema.gm` |
| scopes and names: a module's declarations, the universe scope, members of other modules | `sema/names.gm` |
| structs and unions, generic instances | `sema/adt.gm` |
| type expressions | `sema/ty_expr.gm` |
| function signatures, effect declarations and effect sets | `sema/fnsig.gm` |
| constraints on type parameters | `sema/generics.gm` |
| exact arithmetic for untyped constants | `sema/untyped.gm` |
| typed constants and field defaults | `sema/consts.gm` |
| `foreign` blocks, `#global`s, C compatibility | `sema/foreign.gm` |
| `#soa` types | `sema/soa.gm` |
| sizes and alignments | `sema/layout.gm` (the back end's layouts with field offsets are in `cgen/layout.gm`) |
| the per-function context, scopes, statements, loops | `check/func.gm` |
| expressions, operators, optionals and results | `check/expr.gm`, `check/errors.gm` |
| coercions | `check/coerce.gm` |
| calls and builtin functions, methods, rest parameters and spreads | `check/call.gm`, `check/methods.gm`, `check/spread.gm` |
| patterns, `match`, exhaustiveness | `check/pattern.gm`, `check/matching.gm`, `check/exhaust.gm` |
| `in` and `for` over ranges, arrays and slices | `check/seq.gm` |
| literal inference | `check/infer.gm`, `check/finalize.gm` |
| function literals, captures and second-class rules | `check/fnlit.gm` |
| `with`, handler literals and declarations, `resume`, `cancel` | `check/handlers.gm`, `check/handler_decls.gm` |
| iterators | `check/iterators.gm` |
| generic function calls and instances | `check/generic.gm` |
| compile-time evaluation requests (`@static_assert`, constants, array lengths) | `check/consts.gm`, `check/eval_prepare.gm` |
| continuations used exactly once (§14.2) | `check/cont_check.gm` |
| pointers to stack memory that outlive it | `check/escape.gm`, `check/dangling.gm` |
| `@stack_size` | `check/stack_size.gm` |
| format templates | `check/template.gm` |
| vectors, math vectors, complex numbers, `#soa` in bodies | `check/vector.gm`, `check/mathvec.gm`, `check/soa.gm` |
| tests and benchmarks as functions | `check/tests.gm` |
| preparing a program for code generation | `check/codegen.gm`, `check/program.gm` |

Generic functions are checked once with their type parameters opaque; calls of them infer the
type arguments, check constraints and name the instance. Each instance is then produced: its
body is rewritten for the type arguments, so that what it calls is instantiated in turn (with
the nesting limit of §16.7), what depends on the type arguments (literals, layout queries,
`.bits`, format specs, templates that are compile-time parameters, computed array lengths,
static assertions, type tests, `#soa` operations, the handler declarations a `with` chooses)
is checked for it, and the function literals and handler clauses written in a generic function
are copied for each instance (`tir/clone.gm`).

Before generating code, the compiler checks every function body the program can reach,
including the core library's, and produces the instances of generic functions that those
bodies call (`check/codegen.gm`). `gamma tir` leaves the core library out.

To report what the lint rules need, the checker records which declarations and locals are
used (`Sema.decl_uses`, `outside_uses`, `local_uses`, `local_use_counts`, `nested_owner`);
`gamma tir` and `gamma decls` do not look at them.

### The core prelude and the entry point

The core library is ordinary Gamma source in `lib/core/`. The prelude (`prelude.gm`) is
visible in every module: it declares the ambient effects (`Out`, `Err`, `Env`, `Alloc`, `In`,
`Clock`, `Rand`, `Fs`, `Net`, `Proc`, and `Sys`), the standard iterators and allocation
(`Allocator`, `new`, `make`, `free`, …). The list lives in its own core module, `list.gm`, and
the builtin methods of arrays and slices that are written in Gamma (the sorts, …) in
`slice.gm`; only `List` and `list` are re-exported to the universe scope, and the other
functions are reached as methods, which resolve in the receiver type's module (§21.5).
`complex.gm` declares `Complex`, re-exported too, with its methods and the private functions
that its operators call (§5.3.1). `map.gm` is the hash map: the prelude binds it as the
hidden module name `map` (the test runner's allocation tracker uses it), and
`std/collections/hash_map` re-exports its declarations as `core.map.HashMap` and so on, the
way `std/fs` re-exports `core.File`.

Core modules see the prelude's private declarations and may use a few host-facing intrinsics:
`@heap_alloc` and `@heap_free`, and the system services behind the default handlers
(`@sys_read`, `@clock_now`, `@clock_wall`, `@clock_sleep`, `@rand_fill`, the file system and
network calls). Services that can fail return a count or a negative error code that the
prelude turns into an `IoError`, so both back ends report errors identically. They may also
use `is` and `@hash` on unconstrained type parameters: the map's public constructors require
`Eq` exactly when the default hashing is used. The whole core library is checked on every
compilation, so mistakes in it surface immediately.

The program's entry is a synthesized function that calls `main` inside the prelude's default
handlers for the ambient effects it performs: `with heap_allocator(), system_io() { main() }`
(only the providers for the effects `main` performs), where `system_io()` handles `In`,
`Clock`, `Rand`, `Fs`, `Net` and `Proc` through the intrinsics above. The ambient effects with
host-provided handlers (`Out`, `Err`, `Env`) are installed by the runtime itself. Tests run
under the prelude's test handlers instead (`run_test`): a leak-checking allocator, empty
input, a fake clock that `sleep` advances, and a random generator with a fixed seed.

### Types

`Ty` values are interned (`compiler/types/`): structurally equal types have equal handles, so
type identity is `is`. Nominal types (`struct`, `union`, `effect` literals) get a fresh id
each time a literal is evaluated. A declaration naming a struct is marked resolved *before*
its fields are, so a type can refer to itself through a pointer; containing itself by value
is reported as an infinitely sized type.

### Literal inference

Untyped literals get type variables, which unify with each other and with concrete types
wherever the specification requires equal types (the operands of most binary operators,
assignment, comparison). Variables carry a kind (integer or float literal), an "unsigned
only" flag for indices and shift amounts, and a fallback (`i32`, `u32`, `usize` or `f64`).

To keep the result independent of statement order (§6.1), a coercion whose source is still an
unresolved variable is not decided immediately. The checker records it and emits a
placeholder. After the whole body has been checked, `finalize.gm`:

1. resolves each pending coercion: an unresolved variable takes the target type, or the type
   the target wraps for `?T` and `E!T`;
2. applies fallbacks to the remaining variables;
3. checks deferred obligations that need final types (lossless conversions, numeric and
   ordering requirements, unused values);
4. rewrites the body with final types, checks every literal against its type and rounds float
   literals exactly, and replaces each placeholder with the real coercion.

### Patterns and exhaustiveness

Patterns are checked against the scrutinee type and lowered to TIR patterns, with the
implicit unwrapping of optionals and results (§9.3) made explicit as `Some`, `Ok` and `Err`
patterns. Exhaustiveness and unreachable arms are decided by the usefulness algorithm over
constructor-deconstructed patterns (`exhaust.gm`). Integer patterns are handled by splitting
the type's range at the boundaries of the ranges in a column, so `0..=127, 128..=255` is
exhaustive for `u8`. When a match is not exhaustive, the algorithm produces a witness, which
is shown in the error message.

### Compile-time evaluation

Compile-time evaluation (§17) of typed constants, defaults of fields and parameters, array
lengths, compile-time arguments, `@static_assert`, `@compile_error`, `when` conditions in
bodies and immutable locals with constant initializers is done by a tree-walking evaluator
over the typed representation (`compiler/eval/`). It runs the core library functions and the
handler declarations a computation uses, checking their bodies on demand, with the step
budget, `#fuel`, the trap messages and the compile-time call stack. Its values
(`tir/value.gm`) keep their structure, with pointers as places in cells; the C back end turns
them into static storage and the interpreter lays them out in its memory.

## The interpreter

`gamma run --backend=interp` and `gamma repl` run the program in the interpreter
(`compiler/interp/`) instead of compiling it, and so need no C compiler. It implements the
language exactly as the target would: target integer sizes, layouts and float semantics, with
every checked operation checked. It walks the typed program:

- **Values** are bytes in the target's layout, and each expression is evaluated into a
  destination buffer of the size of its type. Buffers for intermediate values come from
  scratch memory used as a stack, which each statement and call releases, so evaluating
  allocates nothing. A local whose address is never taken lives in its frame's part of that
  memory; the others get an allocation of their own.
- **Memory** (`memory.gm`) is a flat address space in which every allocation gets fresh
  addresses and freed ones are remembered, so out-of-bounds, use-after-free, null, misaligned
  and read-only accesses trap with a message that says which.
- **Control flow** leaves expressions as a `Flow` error; a `break` or `return` first writes
  its value where the loop, `with` or call it leaves puts its result. Blocks run their
  `defer` and `errdefer` statements on the way.
- **Effects.** The handler context is a persistent list of installed handlers, captured by
  closures and handler literals.
- **Second-class values** are bytes too: a function value holds its code address and handles
  to the frame and handler context it captures.
- **Fibers and threads** (`fiber.gm`, `threads.gm`) run as fibers of the compiler itself,
  under a scheduler that is a handler of a `ctl` effect: a fiber of the program, or a thread,
  hands control to another (its driver, the fiber it resumes, the next thread). Threads take
  turns, every 2000 calls and loop iterations and whenever one blocks; a blocking file read
  runs on a host thread meanwhile. The program runs on a large host stack, which `calloc`
  provides so that it costs only what is used.
- **Foreign code**: a program that calls foreign functions, reads foreign variables or uses
  `@asm` runs only through the C back end (`gamma run` switches to it by itself), and
  `native:` conformance tests mark such tests.

The interpreter is slower than compiled code by a factor that depends on the program (a
hundred times on the slowest tests when the compiler is a Debug build). It traps where the
C back end does, with the same messages.

## The C back end

`gamma build` and `gamma run` translate the checked program to C (`compiler/cgen/`) and
compile it, with the runtime (`runtime/gm_rt.h`, `runtime/gm_rt.c`), using the system C
compiler (`$CC`, default `cc`, plus `$CFLAGS` and `$LDFLAGS`; `driver/toolchain.gm`) in GNU
C11 mode, with `-fwrapv` and `-fno-strict-aliasing` (Gamma memory is untyped bytes). The
compilation target is decided before semantic analysis, because layouts and the `target`
module depend on it: the default compiler means the host, and any other toolchain is asked
for its predefined macros (`$CC $CFLAGS -dM -E`). `--target=<triple>` chooses the compiler for
a triple (`<triple>-gcc`, else `clang --target=<triple>`, or `$CC` with the option added if it
is clang or `zig cc`) and then checks the probed target against the triple (architecture,
byte order, operating system), so a mismatched compiler is an error. `GAMMA_RUNNER` prefixes
the command that runs a built executable (`gamma run`, `gamma test`), for cross builds run
under an emulator. The target is a run-time value (`load.Target`, `load/target.gm`) that the
loader, the checker, the evaluator and the back end all read: pointer size, byte order, the
operating system, the architecture, whether it is a Fil-C target and whether `f16` exists.

| File | Responsibility |
| --- | --- |
| `gen.gm`, `program.gm` | `Gen`: the whole program, prototypes, constants, entry points; the checked program as the back end sees it |
| `types.gm`, `layout.gm` | C representations of types (typed structs, blobs, mirrors), type descriptors for `@write`; layouts with field offsets |
| `func.gm`, `fngen.gm` | functions: frames, blocks, statements, the exit ladders; the state of generating one function |
| `expr.gm`, `calls.gm` | expressions, places and coercions; calls, effects, `with`, `try`, conditions, patterns |
| `consts.gm` | compile-time values in static storage |
| `spec.gm` | handler specialization |
| `chain.gm` | long chains of operations (`a + b + …`, `x.abs().abs()…`) through an accumulator |
| `outline.gm` | long runs of statements moved into helper functions that share the frame |
| `arith.gm` | checked scalar operators |
| `vector.gm` | vector operations as GCC/Clang vector extensions, lane loops where a check applies (§5.6.2) |
| `helpers.gm` | per-type equality, ordering and hashing |
| `fiber.gm` | `with … on`, `ctl` performs, `resume`, `cancel`, `@fiber` |
| `borrow.gm`, `constarg.gm` | parameters read in place; static storage for constant arguments |
| `units.gm` | splitting a Debug build into translation units compiled in parallel |
| `freestanding.gm`, `wasi.gm` | freestanding targets; WebAssembly for WASI |
| `debug.gm`, `views.gm` (and `runtime/gdb_printers.py`, `runtime/lldb_formatters.py`) | debug information for `-g` |

`gamma build [--mode=debug|checked|fast] [-g] [-o EXE] [--emit-c] ROOT` writes the C next to
the executable (`EXE.c`) and compiles it; `--emit-c` stops after writing it. `gamma run`
builds the program the same way into a new temporary directory, runs it with the arguments
after the file and ends with its exit status (128 plus the signal number if a signal ended
it, 134 for a trap). The runtime's sources are read from `$GAMMA_RUNTIME`, or else from the
`runtime` directory next to the standard library's (an installation) or two levels above it
(the repository). The program's `main` is called inside the core library's default handlers
for the ambient effects it performs.

The generated code follows the interpreter exactly, and the conformance suite checks this:
every `run` test also runs natively and must print the same output and exit with the same
status.

Operands are evaluated into C temporaries in order, so expressions are never nested in
the C text. A function with tens of thousands of operations is still expensive to
compile: Clang tracks the lifetime of every variable of a scope at each branch out of
it, and gcc's points-to analysis and jump threading are superlinear in the size of one
function. A chain of more than 256 operations that each take the previous one's value
as their first operand (binary operators, `and`/`or`, unary operators, built-in and
function-call links with a scalar value) is therefore emitted as an accumulator
variable updated by one block per link (`chain.gm`), which keeps the live temporaries of
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
split instead (`outline.gm`). When a body has more than 1,500 expression nodes, every
block of more than 400 has its statements dealt out in runs of up to 400 nodes, each
moved into a helper `gm_cN_K` that the function calls where the run was. A helper takes
the frame's address and defines `F` as `(*gm_Fp)`, so a statement's code is the same as
inline; such a function keeps all its locals in the frame, and the helper also gets the
other C variables statements read (`env`, `cx`, `inst`, `gm_out`, the backtrace record
`B`, the node of an enclosing `with`, the parameters read in place). A jump out of a run
(to a rung of the ladder, a loop's exit, the function's return) becomes `return k` with
a number per target, and the caller's `switch` jumps there; `jump` (`cgen/outline.gm`) emits every
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
  (`driver/rtcache.gm`) is the directory `$GAMMA_CACHE_DIR` (an empty value
  turns it off), else `$XDG_CACHE_HOME/gamma`, else `~/.cache/gamma`
  (`~/Library/Caches/gamma` on macOS, `%LOCALAPPDATA%\gamma` on Windows). An object's name is a hash of the runtime
  source, the compiler's command (with its arguments and `$CFLAGS`) and `--version`
  output, and the flags, so a
  changed runtime or compiler gets a new object. Without a usable cache, a Debug
  build is one translation unit too. The test harness gives the compiler under
  test a cache directory of its own. Cached objects and executables are found by a
  hash and used without any check, so the cache directory must be private to the
  user: a cache that other users can write to lets them plant programs that
  `gamma run` then executes. The key of a
  cached executable also covers the size and modification time of the library
  files it links (the package's, those of `foreign` blocks, and those named by
  `-L` / `-l` in `$LDFLAGS`) and, for WASI, the version of `wasm-opt`. Leftover
  temporary files of builds that died are removed after an hour.
- A large Debug build is split into several translation units, compiled in
  parallel and then linked (`cgen/units.gm`): one per 96 KiB of function
  text, at most one per processor and 8. The generator writes everything with
  internal linkage, as one unit needs, and keeps it by role (`Parts` in `cgen/units.gm`):
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
  programs), which then fails. On four cores this brings building the
  compiler itself from about 110 to about 40 seconds.
- Safe builds are compiled at `-O2` like Checked builds but link the cached
  runtime object as Debug builds do (compiled with Fil-C, at `-O2`). Fil-C
  compiles slowly, and the runtime code a program reaches was half the time of
  building a small program (2.5 s for hello world, 1.3 s with the cached
  object), while the programs ran at most 10% slower without the runtime
  inlined into them.

The generated `main` runs the program's entry function on a thread with a 64 MiB
stack (`gm_run_main`), so recursion depth does not depend on the host's default
stack size, and roughly matches the interpreter's.


**Values.** Scalars (integers up to 128 bits, floats, `bool`, pointers) are
native C values. Every other type is a struct whose fields are read and written
with `memcpy` at the layout's offsets (`cgen/types.gm`). Values therefore have
the same bytes in both back ends. Integers narrower than their C storage are
kept sign- or zero-extended, and arithmetic checks the Gamma type's range.

A struct, tuple, array, optional, result or tagged union is a *typed struct*
(`typed` in `cgen/types.gm`): members of the field types (a union's payloads in a C `union`),
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
`cgen/layout.gm`; a `ConstKey` keeps an integer's bytes little-endian, whatever the
target), and the interpreter's memory is in target order, so constants emitted into
C are exactly what the generated code would store. Hashes mix the little-endian
bytes of every value, so they agree across targets, and printed output is the same
everywhere except where a program looks at its own memory (`bits`, `#c_union`,
`target.endian`). Fibers switch stacks in assembly for x86-64, AArch64 and s390x;
other architectures trap on the first fiber switch. `f16` is C's `_Float16`; where
the C compiler has none (`load.Target`), a program that uses `f16` is rejected
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
signature (`gm_pfN`, kept in `Gen.stubs`) that finds the handler and calls it, and each
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
(Appendix D.3 of the specification; `cgen/spec.gm`). In the clone,
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
the clause may return with one (`clause_may_exit`): a loop body that does
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
(`cgen/debug.gm`). Every C line of a function body is preceded by a `#line`
directive naming the absolute path and line of the Gamma statement it comes from,
so the C compiler's DWARF line table, and with it debuggers, profilers and
sanitizer reports, refer to `.gm` lines; each generated function ends with a
directive that returns to the C file's own numbering. A directive per C line is
needed because one numbers the lines after it consecutively, while a statement
usually takes several C lines. Each frame field is an anonymous union of its `lN`
name and the Gamma name of the local, typed with the type's *debugger view*
(`cgen/views.gm`), so a debugger prints `F.p.x`. A view is a C type with the
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
printers (`runtime/gdb_printers.py`) in its `.debug_gdb_scripts` section; they
recognize views by the suffix of their struct tag (`gm_dv3_union`, `_slice`,
`_string`, `_optional`, `_result`) and show the active variant only, slice
elements, string text, `null` and `!error`. For lldb, `gamma build -g` writes the
same conventions as a Python module (`runtime/lldb_formatters.py`, read from the
runtime directory at build time) to `gamma_lldb.py` next to the executable; it registers summaries and
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
(`cgen/helpers.gm`) that agree with the interpreter, including the hash
values, so hash maps iterate in the same order.

**Calling conventions and foreign code.** A function whose effects are at most
`Sys` takes no hidden parameters: it has the plain C calling convention, so it
can be passed to C as a callback, and function pointer types with such effect
sets are C function pointers (§19.1). Where a plain function is used as code
of the hidden convention (a closure, or a pointer widened to a type with
effects), the low bit of its address is set, and those calls test it. A plain
function may have a convention other than C (`#callconv(.Win64)`), kept in
the function's `callconv` and in the function type of pointer types, where it takes part in type
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
*mirror* C type with the same fields (`mirror` in `cgen/types.gm`), with static assertions
that the C compiler lays it out exactly as Gamma does. Plain-convention
signatures (foreign functions, exports, functions with at most `Sys`, and
calls through their pointers) take and return mirrors, and values are copied
between blob and mirror with `GM_CONV` at those calls; the C compiler removes
the copies.

**Large values.** Between Gamma functions, a value larger than two words
(`by_ref` in `cgen/types.gm`: a blob over 16 bytes) is passed by address and the callee
copies it into its frame before anything else runs, unless it can read it in place
(below). The C ABI would pass such a
value through memory anyway, but by value the C compiler made several copies
per call (a temporary, the conversion union, the argument area, the frame), and
fiber stacks sized by `@stack_size` must hold them. Because the copy happens on
entry, the address may be that of any place holding the value: an argument read
from a local or through a pointer, or from a field (of a field…) of one, is passed as
that place (`in_place_root`) when no later argument
can write memory (`no_writes`), and `return x` copies `x` straight into the result
slot. An argument built from literals only, such as the `SourceLocation` that
`@caller_location()` fills in for `new`, lives in static storage initialized at
startup like a top-level constant (`cgen/constarg.gm`), and the call passes
its address instead of building a temporary at every call; under Fil-C that
temporary's escaping address cost a heap allocation per call. (Passing such
values by value instead is slower still under Fil-C: its calling convention
copies an aggregate argument through memory it allocates.) A plain function whose own signature differs from the C ABI as a result
(it has a large parameter, or returns a large mirror type) gets a separate entry
point `gm_fN_c` with the C signature (`needs_wrapper`, `c_abi_wrapper`); it
is what function pointers, constants and `#export` refer to (`addr_name`),
while direct calls use the internal one.

A large result is returned through a first parameter `gm_out`, the address of
the caller's destination, and the function returns `void`. The callee writes it
only when it returns (or, below, as the last thing it does), so the caller passes
the variable or place being assigned (`x = f(…)`, `x := f(…)`, `a[i] = f(…)`,
`return f(…)`) instead of a temporary: the assignment still happens after the
whole call, and an exit out of the call leaves the destination as it was
(`expr_into`). Struct and array constructions are likewise built in their
destination once every element is evaluated, except that an element which is itself
a struct or array (or, into a new variable, a call) is built directly in its place
in the destination, and an element read from a place is copied straight from it.
That writes the destination before the later elements are evaluated, so it is done
only when nothing evaluated meanwhile can read the destination (`Observed`): a new
variable or result slot, or the caller's destination when every element only reads
constants and the function's own locals and parameters. An assignment to an existing
place builds the value in a temporary first. A caller does not pass a destination
that overlaps a parameter the callee reads in place while building its result (it
copies that argument first; `read_during_build`). A function body with no pending
deferred statement builds its result, or copies a returned local, straight into
`gm_out`; otherwise the result goes to the frame's result slot first, and the
deferred statements run before it is copied out.

A large parameter is read in place, without the copy, when no difference can be
observed (Appendix D.5; `cgen/borrow.gm`): the function never stores into
it, takes its address or slices it, and is *pure*: it has no effects, stores only
into its own locals, and calls only pure functions, so nothing that could change
the caller's value runs during the call. It is also read in place when every
call passes a value that nothing writes while the callee runs: a constant from
static storage (`constarg.gm`), a temporary, or a local of the caller that is
never assigned, addressed or captured after its initialization. That needs every
call to be in sight, so the function must be a named one whose address is never
taken, nor exported; and its parameter must not be captured by a function
literal. The typical case is the `SourceLocation` that `@caller_location()` gives
`new` and `destroy`, and that they pass on.

Generated code is compiled with `-fno-strict-aliasing`. Values are byte blobs
written and read piecewise with `memcpy`, so type-based alias analysis gains
nothing, and gcc 13 at `-O2` was seen to drop such writes under it.

### Freestanding targets

A target whose `os` is `Freestanding` (from a compiler that defines no
operating system macro) makes a program with no `main`: sema rejects `main`, the default
handler providers (`heap_allocator`, `system_io`, `system_output`, checked where names are
used, `check_freestanding_use`) and `#trap_hook` anywhere else, and records the hook in
the program. `generate_object` queues the exports and the hook instead of an
entry, and `gen_start` (constants and the build mode, once, from every export wrapper),
`gen_trap_hook` (`gm_trap_hook_call`, which builds the `string` and `SourceLocation`
arguments and calls the hook) and the absence of `main` are what differ in the generated C.
`driver/native.gm` then calls `compile_object`: one translation unit of the runtime and the generated
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
never runs without a handler installed, compiles. `gamma` refuses `run`, `test` and `bench` for
such a target (`refuse_freestanding`) and `build` calls `build_object`.

### Windows

The runtime has a second implementation of its system layer for x86-64
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
  deleting an open file included) and `_open_osfhandle` in
  binary mode; the standard streams are switched to binary mode by `gm_run_main`, which
  also sets the console's output code page to UTF-8 when standard output is a console.
  Directories are `FindFirstFileW` searches. Win32 errors map to the `IoError`
  kinds that POSIX errors map to, and any other error is `Other(<Win32 code>)`.
- Sockets are Winsock 2 (`WSAStartup` on first use); `SO_REUSEADDR` is not set on
  listeners, since on Windows it allows taking over a port in use.
- A trap exits with status 134 as elsewhere. `roundeven`, which MinGW's C library
  lacks, is `rint` (`GM_ROUNDEVEN`).
- The C back end passes no `-pthread` and adds `-static -lws2_32 -lsynchronization
  -lshell32 -lbcrypt` (`windows_libs` in `driver/native.gm`); executables get the extension `.exe`.
- Source file names are stored with `/` (`compiler/source/`) so that diagnostics read the
  same on every system. `gamma` itself is cross-compiled on Linux with MinGW-w64
  (`gamma build compiler --target=x86_64-w64-mingw32 -o gamma.exe`, by any `gamma`) and linked
  statically; the platform-dependent parts of the compiler are the
  ones the standard library already has per platform (paths, the temporary and cache
  directories, process creation), the path of the executable, and the C compiler lookup
  (without `CC`, the first of `cc`, `gcc` and `clang` on `PATH`, split at `;`). `CC`,
  `CFLAGS`, `LDFLAGS` and `GAMMA_RUNNER` are split with the rules of the Windows command line,
  where a backslash is literal unless it precedes a double quote (`pkg.split_words`).
  Native builds need a MinGW-w64 `gcc` or `clang`; MSVC's `cl.exe` is not supported.

### WASI

For `wasm32-wasi` (`__wasi__`, `target.os` `.Wasi`) the runtime keeps its POSIX
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
  leave alone), then `cgen/wasi.gm` runs `wasm-opt --asyncify` and the build's
  optimization level over the module. Only a module that imports from `asyncify` is
  transformed; the generated `main` calls `gm_run_main_fibers`, the only function that
  reaches the driver, only when the program uses `with ... on` (`gm_with_go`), so a
  program without fibers keeps its size and speed.
- Function "addresses" are table indices, so the tag that marks a plain function in a
  function value is the top bit (`GM_PTR_TAG_BIT`) rather than bit 0.
- WASI's `proc_exit` refuses statuses of 126 and above, so a trap reports and then
  executes `unreachable` (`__builtin_trap`); Wasmtime then exits with 134 as a native
  program would, and `gamma` drops the report Wasmtime adds (`strip_wasm_trap_report` in `driver/toolchain.gm`).
  Modules are run with Wasmtime (`driver/toolchain.gm`), the current directory granted,
  the environment inherited and the native stack for WebAssembly frames raised to 256 MiB
  so that the program's own stack overflows first.

### Safe builds (Fil-C)

`--mode=safe` builds through the C back end with [Fil-C](https://fil-c.org), a
memory-safe implementation of C in which every pointer carries an invisible
capability for its allocation. `for_safe_build` (`driver/toolchain.gm`)
looks for `filcc` on `PATH`, then in `/opt/fil/bin`, honoring `$CC`; a compiler
that is not Fil-C is an error. The `filc` flag of the target (`load/target.gm`) is set when the
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

## Tests and benchmarks

`test "name" { … }` and `bench "name" { … }` are one syntax-tree item (with a `bench` flag)
and are collected only when tests are enabled (`gamma test`, `gamma bench`, `gamma lint`, the
language server). `check/tests.gm` turns each body into a function whose error type is
inferred from its first `try`, and wraps it in an entry function that calls a runner of the
core prelude: `run_test` (tracking allocator, test handlers, leak check) for a test,
`run_bench` for a benchmark. A benchmark's entry is wrapped like `main`, so it runs under the
system handlers. `run_bench` finds the number of runs per sample by growing batches, takes 20
timed samples with the `Clock` effect, and prints one line, `gamma-bench: <runs> <ns> <ns> …`,
that `compiler/testing/` turns into the report (the benchmark's own output precedes it).
`@black_box` is the identity in the interpreter, and in C an empty `asm` statement that may
change the value (integers and booleans; Fil-C and other types: a round trip through a
`volatile` pointer).

`gamma test` and `gamma bench` (`compiler/testing/`) check the program as a library with its
tests (the modules of the root's package that it imports; all of them with `--all`), compile
the chosen tests into one executable whose `main` runs the test given by its first argument,
and run it once per test with captured output. Both always compile through C. `run.gm`
selects the cases (`--filter`), runs them, on `--jobs` threads that only start processes, and
prints the results in order; a test passes if its exit status is 0, and a trap's message and
location reach the report from standard error. A test that exceeds `--timeout` is killed by a
watching thread, which cannot reach processes the test started or a test that closed its
output and then ran on. A benchmark builds in Fast mode unless `--mode` is given.

Coverage (`--coverage`, `coverage.gm`) is a rewrite of the checked program: before each
statement, and before the value of each block, of the functions of the package that are not
tests, it inserts a `Cover(n)` builtin, where `n` numbers a source line. The C back end
compiles the builtin to `GM_COVER(n)`, an atomic increment of a counter array that the test
executable's `main` allocates (`gm_cover_init`); at exit the runtime writes the counters, one
per line, to the file named by the executable's second argument (after the index of the
test), and the runner, which starts one process per test, adds up the files of all tests. The
instrumentation runs after every function the executable needs has a body, so the instances
of generic functions are counted too. Nothing is inserted, and nothing slows down, without
the option.

## Tools

### Formatter

`gamma fmt` (`compiler/fmt/`) works on the tokens of the compiler's own lexer and never
changes meaning: it keeps the line breaks of the source, collapses runs of blank lines,
indents by bracket nesting (four spaces), normalizes the spacing inside a line, and copies
comments and literals verbatim. `classify.gm` decides for every token whether it is written
tight against its neighbours and `layout.gm` does spacing, indentation and line breaks.
Trailing comments and the columns of aligned `::` and `=>` keep their spacing. It lexes its
result again and refuses to write anything that would change the tokens. Files with lexical
errors are not formatted. Directories are walked in name order, skipping hidden directories
and a package's `target`.

### API documentation

`gamma doc` (`compiler/doc/`) works on the syntax tree only, so a module needs to parse but
not to type-check. `collect.gm` reads the `pub` declarations of a module (a directory or a
file), copies each signature from the source text between the spans the parser recorded, and
attaches its `///` comments and the `//!` comments at the top of the module's files.
`markdown.gm` and `html.gm` render a module and the index of a set of modules, and
`md_html.gm` is the small Markdown renderer for doc comments (everything is escaped). The
output depends only on the sources, never on the machine or the time, so the standard library
reference in `docs/std/` is checked in and a test of the harness fails when it is stale. A
module with syntax errors is reported with the diagnostics of `gamma check`, and the command
fails.

### Lint and fix

The lint rules are split by what they need. Rules that walk the syntax tree are in
`compiler/lint/syntax.gm`. Rules that read the checked program are in `checked.gm` (unused
locals and declarations, variables that never change, comparisons with a known result),
`copy.gm` (the `mutated-copy` rule: locals holding a copy of a list or map that are changed as
if they were the original) and `present.gm` (the `present-only-binding` rule: `match` arms
whose present-only bindings leave the absent case to a catch-all arm). Addresses of locals
kept where they outlive the local are a compile error where certain (§15.1,
`check/dangling.gm`) and the `dangling-address` lint where likely. `lint.gm` holds the table
of rules, their levels (defaults, the `[lint]` table of `gamma.toml`, the command line,
`#allow`) and the diagnostics. Each finding is a warning whose `code` is the rule's name, with
a fix where one is safe. Linting checks the program with its tests (as `gamma test` would),
and, in a package, with the other modules of the package.

`compiler/linting/` is the `gamma lint` command line, with `--fix`: it reuses `gamma fix`'s
planning (`compiler/fix/`) in rounds and, when a round of fixes breaks the program, undoes it
and retries the fixes one at a time, leaving out the ones that break it. Both only edit the
package's own files, never the standard library.

### REPL

`gamma repl` (`compiler/repl/`) has no incremental compilation or persistent interpreter
state to build on: locals die when `main` returns. Each input is therefore checked as part of
a complete program synthesized from the session's state, in an arena of its own that is
released afterwards, and run in the interpreter:

| Part of the file | Content |
| --- | --- |
| helpers | `__repl_type`, which names the type an expression gets by default (`1 + 2`) |
| declarations | the source text of each earlier top-level item, and for a declaration input the new text |
| `main` | the kept statements, the replay marker, then the new input |

- `report.gm` turns the output into what the user typed: diagnostics relative to the input,
  trap reports, and the filter that hides the output of replayed statements.
- `input.gm` classifies text by parsing it twice, as top-level items and as the body of a
  function. `x := e` parses as an item but is not a declaration, so it is read as a
  statement. Items that are plain `name :: value` bindings are tried as constants first and as
  local variables if that fails. An unfinished input asks for more lines.
- `main` returns `AllocError!()` so `try` works on allocation.
- An expression statement is tried as written (it must have type `()`), then as
  `__repl_v := e` followed by `print("{}\n", __repl_v)`.
- Variables are replayed before the new input, with the output of `Out` and `Err` hidden until
  a marker string. A statement is kept if it checks with the effect set `Alloc` and the kept
  statements before it, which separates repeatable statements from those with visible
  effects. A variable defined from an effectful expression is kept anyway, and flagged.
- Redefining a variable renames the new one (`x__2`) and rewrites later inputs at the token
  level (identifiers after `.` or before `:` are left alone).

### Language server

`gamma lsp` serves the Language Server Protocol over standard input and output; users are
pointed to [`lsp.md`](lsp.md). This section is about how it works.

| File | Responsibility |
| --- | --- |
| `compiler/lsp/transport.gm` | JSON-RPC messages with `Content-Length` framing, and the thread that reads them |
| `compiler/lsp/json.gm` | building and reading JSON values |
| `compiler/lsp/position.gm` | byte offsets to and from LSP positions (UTF-16 units, or bytes when the client offers UTF-8) |
| `compiler/lsp/analysis.gm` | one run of the compiler over a module, and its diagnostics as LSP diagnostics |
| `compiler/lsp/features.gm` | hover, definition, completion, signature help and symbols, as JSON |
| `compiler/lsp/refs.gm` | find references and rename across the modules of a package |
| `compiler/lsp/workspace.gm` | the modules of a package, and a cache of what was learned from checking each |
| `compiler/lsp/server.gm` | open documents, when to analyze, and request dispatch |
| `compiler/ide/` | the index that the requests are answered from, built from a checked program |

A second thread only reads messages and queues them. The main loop handles a message, and
checks the open modules when the queue is empty, so a burst of edits costs one run. A request
that needs a current answer checks first. Each analysis allocates from an arena that is freed
when the analysis is replaced.

**Analysis.** A module is a directory (spec §3.1), so each run checks the whole directory that
contains an open document. The loader's overlays map paths to the text of open documents, so
importers see unsaved buffers of the modules they import. An edit marks stale every analysis
that read a file of the edited directory; the next idle moment re-checks them. The run is the
same as for `gamma test` (so `main` is not required), with `keep_going` set so that syntax
errors do not stop it. Diagnostics are published for every open document of the directory and
for every other file that has diagnostics. Files that had diagnostics before and have none now
get an empty list. A diagnostic without a location goes to line 0 of the first open document.
Labels become `relatedInformation`; the explanations that a diagnostic carries are appended to
its message. A failure inside the checker ends the server, since traps cannot be caught.

**The index** (`compiler/ide/`) reads the state of a finished run and adds no code to the
checking passes, so ordinary compilation is unaffected.

- `facts.gm` walks the typed IR of every function of the module (not the instances of
  generics, and not the core library). It records the type of each expression by span, the
  function each call resolves to, the declared locals, and a reference from each use of a local
  to its declaration. Where several expressions share a span, the innermost one is the first.
- `walk.gm` and `items.gm` re-parse the files of the module and walk the syntax tree with
  their own lexical scopes. They resolve a name to a local, or to a declaration of the module,
  its imports or the universe scope. A field, method, variant, operation or named argument is
  resolved through the type of the expression before the `.` or of the call, which comes from
  the facts. The walk records references, the hover text of each declared name, the locals
  visible at each offset (name, type and the byte range where it is visible), and the outline
  of each file.
- `info.gm` describes declarations: a function signature printed from the checked signature
  (with effects), the source text of a type, the members that `.` offers for a type (fields,
  then the functions of the type's module that take it as their first parameter) and for a
  module (its `pub` names).

A declared name is found at a position through the references that point to it, and also
through the outline, the local scopes and the hover table, so that a declaration that is not
used in its own module (the usual case for the `pub` names of a library) can still be asked
about. `print` and the other builtin functions are not declared in any source, so they have no
location.

**Syntax errors.** The parser recovers at statements, match arms, fields and declarations, so
a statement that does not parse becomes an error expression and the rest of the function is
checked. What it cannot do is notice that a block is unclosed: a missing `}` or `)` makes
every later declaration part of the open construct. In the editor path a file whose lexing had
errors is lexed again with `lex_recovering`, which closes all open brackets, reporting each as
unclosed, wherever a line starts in the first column with `name ::`, `pub`, `handler`,
`test "…"` or an attribute. Gamma code is formatted with declarations in the first column and
nothing else is, and the second pass runs only if the text already has errors, so well-formed
text, and ordinary compilation, are unaffected.

**References and rename.** References across modules need the analyses of modules other than
the open one. `workspace.gm` finds the module directories under the package root and the roots
of its path dependencies, and analyzes one, keeping its references as (file path, offsets)
pairs for uses in its own files, the text of its files, its local scopes, and for each
declared name the names declared next to it. An entry is valid while a hash of the files that
the analysis read (the list of `.gm` files of each directory and their contents, buffers
instead of disk) is unchanged. Only directories whose files contain the name are analyzed.
Locals and parameters are searched in the open module alone.

Rename finds the declaration and its uses this way, then refuses if the target is in the core
or standard library or outside the package root, if the new name is not an identifier, is a
keyword or names a builtin type, if a sibling declaration or an overlapping local scope has
the name, or if a local named so is visible at one of the uses. The edits are the exact name
ranges, narrowed from the recorded span when it covers more than the name (as `a.name` may).

**Completion.** Names come from the locals visible at the cursor, the declarations of the
module and the universe scope. Members after `.` come from the index of the current text when
it has the receiver. Typing `x.` usually leaves text that does not check. The server then
takes out the dot and what was typed after it, checks the module again with that text, and
looks up the receiver by its byte range, which the edit did not move.

**Limitations.**

- A name in a part of the file that is not taken by a top-level `when` is not resolved.
- Tuple element names and the members of builtin types are not references; the builtin
  methods of numbers and slices are listed by name only.
- Completion after `.` re-checks the module when the current text has no receiver for the
  dot, which is as slow as an edit.
- The layout heuristic of syntax-error recovery assumes declarations start in the first column
  and nothing else does; an unclosed bracket in a file that is not so formatted still
  swallows the rest of its construct.
- Rename does not check for a clash between the new name and a name in an importing module's
  own scope beyond the locals at the uses.
- Symbolic links are not resolved when paths are compared, and the response to each request is
  not freed, so the process grows slowly. On Windows, where output cannot be written
  unbuffered through `/dev/stdout`, responses can be held back until the buffer fills.

## Installation layout

A release archive is an installation prefix: `bin/gamma` (`bin/gamma.exe` on Windows),
`lib/gamma/std/`, `lib/gamma/core/` and `lib/gamma/runtime/` (`gm_rt.c`, `gm_rt.h` and the
debugger scripts). `gamma` needs no environment variables to find them:
`compiler/load/exe.gm` asks the system for the path of the running executable
(`/proc/self/exe` on Linux, `_NSGetExecutablePath` and `realpath` on macOS,
`GetModuleFileNameW` on Windows) instead of trusting `argv[0]`, which is only a name when the
program is started through `PATH`. The standard library is then `../lib/gamma/std` from the
executable's directory, the core library beside it, and the runtime `runtime` beside that (in
a repository checkout, where there is none, two levels above `std`). `$GAMMA_STD`,
`$GAMMA_CORE` and `$GAMMA_RUNTIME` override each (`compiler/driver/paths.gm`). A native build
also needs a C compiler on the machine, as everywhere. Packaging and installation are
described in [`contributing.md`](contributing.md#releases).

## Conventions

- **Memory.** A compilation allocates from an arena that is released at the end, so the
  compiler's data structures do not free their parts one by one. An allocation failure traps
  (`x!` on the result): a compiler that runs out of memory has nothing better to do.
- **Errors are diagnostics.** Problems in the input are reported to a `diag.Diagnostics` and
  the stage goes on; only a failure of the environment (an unreadable file) is an error value.
- **No dependencies** beyond the standard library.
- **Structure.** Each module of the compiler keeps one concern, and the comments say why.
  Where Gamma has a better way to say a thing (`try` for early return, `defer` to restore
  parser state), the code uses it; [Idiomatic Gamma](guide/idioms.md) collects these.
- Sources are formatted with `gamma fmt compiler`; a test checks it.
