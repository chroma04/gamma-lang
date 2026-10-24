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
| checker | the diagnostics of `gamma check` |
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
2. **Parser** — the syntax tree (the counterpart of `src/syntax/ast.rs`), the parser, and
   the dump format of `src/syntax/ast_dump.rs`. Checked against the `.ast` goldens of
   `tests/conformance/parse/` and against `gamma parse --dump` on the whole repository.
3. **Modules and packages** — loading a module directory, imports, `gamma.toml` and the
   lock file, as `src/driver.rs` and `src/pkg/` do.
4. **Checker** — name resolution, types, inference, effects, generics, compile-time
   evaluation, producing a typed representation like `src/tir.rs`. Checked against the
   diagnostics of `gamma check` on the conformance tests of `check/`.
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

## Building and running

```sh
gamma build compiler -o gammac          # or: gamma run compiler lex FILE
./gammac lex examples/hello.gm
./gammac lex --diag-format=json tests/conformance/lex/errors_numbers.gm
gamma test compiler                     # gammac's own unit tests
cargo test --test selfhost              # the parity tests
```

`gammac lex FILE...` prints each file's tokens to standard output and its diagnostics to
standard error, sorted by position, in the `short` format by default. With several files,
each file's output on both streams is preceded by a line `==> FILE <==`, so that one run
can cover the whole repository. The exit status is 1 if a file has errors and 2 for a
usage error or a file that cannot be read.

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
- Sources are formatted with `gamma fmt compiler`; a test checks it.
