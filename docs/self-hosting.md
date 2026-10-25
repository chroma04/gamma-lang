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
4. **Checker** — done, in two parts. `gammac decls` and `gammac tir` match `gamma decls`
   and `gamma tir` on every program in the repository, error messages included.
   - **Declarations.** Resolving every top-level declaration without checking function
     bodies: types and type expressions, structs and unions (generic ones and their
     instances), vector, complex and `#soa` types, effects, function signatures,
     constraints, untyped constants with exact arithmetic, typed constants, array
     lengths and compile-time arguments (computed by calling functions where needed),
     `foreign` blocks and `#global`s.
   - **Bodies.** Expressions, statements, patterns, inference, effects, the instantiation
     of generic functions and compile-time evaluation, producing the typed intermediate
     representation of `src/tir.rs` (`compiler/tir/`). `gamma tir` prints that
     representation in a stable text form: every function written outside the core
     library, in source order, one line per expression with its type. That covers the
     whole language: patterns with exhaustiveness, optionals and results, `defer`,
     function literals, nested functions (generic ones included) and function pointers,
     effects and their checks, `with`, handler literals and declarations (for generic
     types and effects too), effect-set parameters, `ctl` operations with `resume`,
     `cancel` and `with … on`, iterators, `for` and arguments that are iterators,
     `@fiber` and the `@stack_size` analysis, tests, rest parameters and spreads,
     formatting templates, vectors, complex numbers, `f16` and `#soa`, struct attributes
     (`#bits`, `#c_union`, `using`, `#export`, `#callconv`, `#trap_hook`), enum-indexed
     arrays, local type aliases, `@type_of` and `is` on types, range values, pointer
     arithmetic, intrinsics, and the checks of `main`, `#inline`, escaping pointers,
     dangling addresses and continuations.

     Compile-time evaluation (§17) of typed constants, defaults of fields and
     parameters, array lengths, compile-time arguments, `@static_assert`,
     `@compile_error`, `when` conditions in bodies and immutable locals with constant
     initializers is done by a tree-walking evaluator over the typed representation
     (`compiler/eval/`). It runs the core library functions and the handler declarations
     a computation uses, checking their bodies on demand, with the step budget, `#fuel`,
     the trap messages and the compile-time call stack of the reference.

     Generic functions are checked once with their type parameters opaque, as the
     reference does; calls of them infer the type arguments, check constraints and name
     the instance. Each instance is then produced as the reference produces it: its body
     is rewritten for the type arguments, so that what it calls is instantiated in turn
     (with the nesting limit of §16.7), what depends on the type arguments (literals,
     layout queries, `.bits`, format specs, templates that are compile-time parameters,
     computed array lengths, static assertions, type tests, `#soa` operations, the
     handler declarations a `with` chooses) is checked for it, and the function
     literals and handler clauses written in a generic function are copied for each
     instance.

`gammac decls` and `gammac tir` read the core library from `$GAMMA_CORE`, or else from
the `core` directory next to the standard library's. A root module without `main` is
checked as a library, as `gamma tir` does: the way `gamma test` would check it, with
its tests and benchmarks and those of the modules of its package that it imports.

### Growing a stage

A stage that covers a large part of the language grows over several steps. Until it is
complete, `gammac` reports each construct it cannot handle yet with an error that starts
`gammac cannot`, and `tests/selfhost.rs` compares only the programs without such an
error. Everything else must match exactly. The number of programs compared may only
grow: the test fails if it drops below `DECLS_FLOOR` (declarations) or `TIR_FLOOR`
(bodies), which are raised as `gammac` learns more. Both checker stages are complete, so
both floors are the number of programs in the repository: a program that `gammac` stops
checking fails the test.

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
