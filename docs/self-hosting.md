# The self-hosted compiler

`compiler/` holds `gammac`, a Gamma compiler written in Gamma. It is being built in
stages next to the reference implementation in `src/`, which stays the compiler that
users run until `gammac` can do everything it does. The work has two purposes:

- **Dogfooding.** A compiler is a large program with demanding data structures, string
  handling, error reporting and performance needs. Writing one in Gamma exercises the
  language, the standard library and all three back ends far more than examples and
  conformance tests do, and every awkward spot it hits is a candidate for a fix in the
  language or the library.
- **Self-hosting.** Eventually `gammac` compiles itself, so that Gamma's own toolchain
  is written in Gamma.

## The parity contract

Each stage of `gammac` must produce output **byte-identical** to the reference
implementation's for the same input. Every stage has a stable textual form that makes
this checkable:

| Stage | Compared output |
| --- | --- |
| lexer | the token dump of `gamma lex`, and its diagnostics |
| parser | the syntax tree dump of `gamma parse --dump`, and its diagnostics |
| modules and packages | the module graph of `gamma modules`, and its diagnostics |
| declarations | what `gamma decls` prints for every top-level declaration, and its diagnostics |
| bodies | the typed program of `gamma tir`, and its diagnostics |
| back end | the output and exit status of the compiled programs |

Diagnostics are compared in the JSON format (`--diag-format=json`), which carries every
part of a diagnostic: the message, the primary span, labels, notes and fixes.

The comparison runs over **every `.gm` file in the repository**: the conformance tests
(including the ones written to provoke errors), the standard library, the examples, the
benchmarks and `gammac`'s own sources. `tests/selfhost.rs` does this on every `cargo test`,
so a change to either implementation that makes them disagree fails the build. When the
reference implementation changes on purpose (a new token, a reworded message), `gammac`
changes with it in the same pull request.

Parity is strict on purpose. It turns the whole test corpus of the reference compiler
into a test suite for `gammac` without writing a single new expectation, and it makes the
reference implementation the specification of everything the language specification
leaves to implementations: message texts, spans and recovery after errors.

## Stages

1. **Lexer** — done. `gammac lex` matches `gamma lex` on the whole repository, with and
   without the layout recovery that tools use after errors.
2. **Parser** — done. The syntax tree, the parser with its error recovery, and the dump
   format: `gammac parse --dump` matches `gamma parse --dump` on the whole repository,
   syntax errors included.
3. **Modules and packages** — done. Loading the root module and every module its imports
   name, packages and their path dependencies from `gamma.toml`, collecting each
   module's top-level names (including the taken branches of top-level `when`s), and
   import cycles: `gammac modules` matches `gamma modules` on the root of every program in
   the repository. Not yet: git and archive dependencies (which need the lock file and the
   package manager), the exact messages for a malformed `gamma.toml`, and `when`
   conditions beyond `target`, `@size_of`, literals and constants.
4. **Checker** — in progress, in two parts:
   - **Declarations.** Resolving every top-level declaration without checking function
     bodies: types and type expressions, structs and unions (generic ones and their
     instances), effects, function signatures, constraints, untyped constants with exact
     arithmetic, and the typed constants made of literals and constructors. `gammac decls`
     matches `gamma decls` on every program it does not report as using something it
     cannot check yet (see below); that is most of the repository. Not yet: foreign blocks,
     `#global`s, `#soa` and vector types, and constants that call functions, which need
     the second part.
   - **Bodies.** Expressions, statements, patterns, inference, effects, the instantiation
     of generic functions and compile-time evaluation, producing the typed intermediate
     representation of `src/tir.rs` (`compiler/tir/`). `gamma tir` prints that
     representation in a stable text form: every function written outside the core
     library, in source order, one line per expression with its type. `gammac tir` matches
     it on the programs it supports, which so far are those made of functions with
     locals, assignments, arithmetic and comparisons, `if` and `while` with binding
     conditions, `match` and patterns with exhaustiveness, `is` and `in` with patterns,
     `for` over ranges, arrays, slices and tuples, `return`, `break` and `continue`, calls,
     method calls (builtin methods, functions of the receiver's module and effect
     operations), `E.op(…)`, struct, variant and array construction, indexing, slicing,
     `.len` and `.ptr`, optionals and results (`else`, `try`, `!e`, `x?`, `x!`, error
     lifting), `print` with plain `{}` placeholders, `assert` and `trap`, literal inference
     with its fallbacks, and the checks of effects, of `main`, of `#inline` and of
     escaping pointers. Generic functions are checked once with their type parameters
     opaque, as the reference does; calls of them infer the type arguments (from the
     arguments, a method receiver, literal fallbacks and `()` defaults), check
     constraints, and name the instance, as do generic functions used as function
     pointers. Compile-time value parameters, `@caller_location()` defaults, generic
     types written in bodies (constructions and struct patterns), methods of generic
     types such as `List`, and the slice methods of the core library are supported. The
     bodies of instances are not produced, since the dump shows templates, but they are
     followed as producing them would be: what they call is instantiated in turn, with
     the nesting limit of §16.7, and literals and `.bits` that depend on the type
     arguments are checked for each instance. Next: `defer`, `with` and handlers,
     intrinsics, function literals and calls of function pointers, generic effects,
     rest and spread arguments.
5. **Back end** — C generation, linking the existing C runtime (`runtime/`). Checked by
   running the `run` conformance tests through `gammac` and comparing with the reference.
6. **Bootstrap** — `gammac` built by `gamma` (stage 1) compiles `gammac` (stage 2), which
   compiles it again (stage 3). The generated C of stages 2 and 3 must be identical.

The stages mirror the reference implementation's module structure so that a reviewer can
read the two side by side.

## Layout

| Path | Contents |
| --- | --- |
| `compiler/gamma.toml` | the package manifest (`gammac`) |
| `compiler/main.gm` | the command line |
| `compiler/source/` | source files, byte spans, line and column lookup (`src/source.rs`) |
| `compiler/diag/` | diagnostics and their `short` and `json` rendering (`src/diag.rs`) |
| `compiler/bignum/` | natural numbers and rationals of any size, for the exact values of literals |
| `compiler/syntax/` | tokens (`token.gm`), the lexer (`lexer.gm`, `literal.gm`) and the token dump (`dump.gm`) (`src/syntax/`) |
| `compiler/ast/` | the syntax tree (`ast.gm`) and its dump (`dump.gm`) (`src/syntax/ast.rs`, `ast_dump.rs`) |
| `compiler/load/` | loading modules and packages (`loader.gm`), the core library (`core.gm`), file paths (`paths.gm`), the `gamma.toml` reader (`toml.gm`) and the conditions of top-level `when`s (`when.gm`) (`src/sema/modules.rs`, `imports.rs`) |
| `compiler/types/` | types, hash-consed (`types.gm`), and their text in messages (`display.gm`) (`src/types.rs`) |
| `compiler/bigint/`, `compiler/ratio/` | integers and rationals of any size, for the exact arithmetic of untyped constants |
| `compiler/tir/` | the typed intermediate representation that checking produces (`src/tir.rs`) |
| `compiler/check/` | checking function bodies (`src/sema/func.rs`, `expr.rs`, `finalize.rs` and the files named alike): the per-function context, statements and control flow (`func.gm`), expressions (`expr.gm`), calls and builtins (`call.gm`), calls of generic functions (`generic.gm`), method calls (`methods.gm`), implicit coercions (`coerce.gm`), optionals and results (`errors.gm`), patterns (`pattern.gm`), `match` and pattern tests (`matching.gm`), exhaustiveness (`exhaust.gm`), sequences, `in` and `for` (`seq.gm`), literal inference (`infer.gm`), the final pass over a body (`finalize.gm`), the checks of a whole program (`program.gm`, `escape.gm`) and the `tir` dump (`dump.gm`) |
| `compiler/sema/` | checking: the state and the resolution of declarations (`sema.gm`), names (`names.gm`), type expressions (`ty_expr.gm`), structs and unions (`adt.gm`), signatures and effects (`fnsig.gm`), constraints (`generics.gm`), untyped constants (`untyped.gm`), typed constants (`consts.gm`), layouts (`layout.gm`) and the `decls` dump (`dump.gm`) (`src/sema/`) |
| `compiler/parse/` | the parser: its state, errors and recovery (`parser.gm`), expressions, statements, declarations, types, patterns and the brace-delimited literals (`src/syntax/parser/`) |

## Building and running

```sh
gamma build compiler -o gammac          # or: gamma run compiler lex FILE
./gammac lex examples/hello.gm
./gammac lex --diag-format=json tests/conformance/lex/errors_numbers.gm
./gammac parse --dump examples/hello.gm
GAMMA_STD=lib/std ./gammac modules examples/grep.gm
GAMMA_STD=lib/std ./gammac decls examples/grep.gm
GAMMA_STD=lib/std ./gammac tir examples/hello.gm
gamma test compiler                     # gammac's own unit tests
cargo test --test selfhost              # the parity tests
```

`gammac lex FILE...` prints each file's tokens to standard output, and `gammac parse --dump
FILE...` its syntax tree; both print the diagnostics to standard error, sorted by
position, in the `short` format by default. With several files,
each file's output on both streams is preceded by a line `==> FILE <==`, so that one run
can cover the whole repository. The exit status is 1 if a file has errors and 2 for a
usage error or a file that cannot be read.

`gammac decls` and `gammac tir` read the core library from `$GAMMA_CORE`, or else from
the `core` directory next to the standard library's. A root module without `main` is
checked as a library, as `gamma tir` does: the way `gamma test` would check it.

### Growing a stage

A stage that covers a large part of the language grows over several steps. Until it is
complete, `gammac` reports each construct it cannot handle yet with an error that starts
`gammac cannot`, and `tests/selfhost.rs` compares only the programs without such an
error. Everything else must match exactly. The number of programs compared may only
grow: the test fails if it drops below `DECLS_FLOOR` (declarations) or `TIR_FLOOR`
(bodies), which are raised as `gammac` learns more.

`tests/selfhost.rs` builds `gammac` through the C back end in the Debug mode, so that its
overflow and bounds checks are on, when a C compiler is available, and otherwise runs it
in the interpreter (slower, but the same results).

## Conventions

- **Memory.** A compilation allocates from an arena that is released at the end, so the
  compiler's data structures do not free their parts one by one. An allocation failure
  traps (`x!` on the result): a compiler that runs out of memory has nothing better to
  do.
- **Errors are diagnostics.** Problems in the input are reported to a `diag.Diagnostics`
  and the stage goes on, as the reference implementation does; only a failure of the
  environment (an unreadable file) is an error value.
- **No dependencies** beyond the standard library.
- **Mirror the reference.** Each Gamma module corresponds to a part of `src/` and keeps its
  structure and comments, so that a change to one is easy to carry to the other. Where
  Gamma has a better way to say the same thing (`try` for `?`, `defer` to restore parser
  state), the Gamma code uses it; [Idiomatic Gamma](guide/idioms.md) collects these.
- Sources are formatted with `gamma fmt compiler`; a test checks it.
