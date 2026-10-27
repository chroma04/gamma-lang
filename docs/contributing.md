# Contributing

Bugs and proposals go in GitHub issues (the issue forms ask for what helps); security
problems are reported privately, as [`SECURITY.md`](../SECURITY.md) describes.

## Building

The compiler is a single Rust crate. With a stable Rust toolchain (1.95 or newer, the
`rust-version` in `Cargo.toml`):

```sh
cargo build            # debug build (lightly optimized)
cargo build --release
```

The binary is `target/<profile>/gamma`. `cargo run -- <args>` builds and runs it.

`gammac`, the compiler written in Gamma (`compiler/`), is built by `gamma build compiler`,
or without Rust from the C seed in `bootstrap/` with `bootstrap/build.sh`, which only needs
a C compiler (see [The bootstrap seed](#the-bootstrap-seed)).

## Checks

Continuous integration runs, on Linux and macOS (and the tests on Windows with MinGW-w64 gcc):

```sh
cargo fmt --all -- --check
cargo clippy --all-targets      # warnings are errors in CI
cargo test --all-targets
```

Run them before sending a change. Formatting uses the settings in
`rustfmt.toml`. CI also has jobs for Safe builds with Fil-C, for a big-endian
cross build (s390x under qemu), for the editor grammars (see below and
[`editors.md`](editors.md)) and, on pull requests, for benchmarks (see below).

## Formatting Gamma code

`gamma fmt <file or directory>...` rewrites `.gm` files in the canonical style;
`gamma fmt --check` only lists the files that would change (exit status 1 if
there are any), and `gamma fmt -` filters standard input to standard output. The
formatter (`src/fmt.rs`) works on tokens and never changes meaning: it keeps the
line breaks of the source, collapses runs of blank lines, indents by bracket
nesting (four spaces), normalizes the spacing inside a line and copies comments
and literals verbatim. Trailing comments and the columns of aligned `::`/`=>`
keep their spacing. Files with lexical errors are not formatted. `tests/fmt.rs`
checks that formatting keeps the tokens and is idempotent for every `.gm` file in
`lib/` and `tests/conformance/`. The library itself is kept formatted (the same test fails
otherwise): run `gamma fmt lib` after editing it. Conformance tests are not
reformatted, since some pin columns in their expected output.

## Lint rules

`gamma lint` ([`lint.md`](lint.md)) is kept clean on `lib/std` and `examples/`:
`tests/lint.rs` fails if either has a warning. To add a rule, give it an entry in
`RULES` (`src/lint/mod.rs`) with its default level and a one-line summary, then
report it from the AST walk (`src/lint/syntax.rs`) if the syntax tree is enough, or
as a `Finding` from `src/sema/lint.rs` if it needs types or name resolution. Attach a
fix only when it cannot change what the program does; `gamma lint --fix` undoes a
fix that stops the program compiling, but not one that changes its behavior. Add a
conformance test for it under `tests/conformance/lint/`, with the cases it must not
report, and a row to the tables of `docs/lint.md`.

## API documentation

`gamma doc` (`src/doc/`) writes Markdown or HTML documentation for the `pub`
declarations of a module from the syntax tree, their `///` comments and the `//!`
comments at the top of the module's files. It copies signatures from the source text,
so a module needs to parse but not to type-check. Document the standard library in
those comments.

The reference in `docs/std/` is generated, not written by hand. After changing a doc
comment or a public declaration in `lib/std`, regenerate it:

```
cargo run -- doc lib/std --all -o docs/std
```

`cargo test --test doc` fails with this command in its message while the checked-in files
differ from the output. `gamma doc lib/std --all --missing -o /tmp/std` also lists the
public declarations that have no doc comment. The generator's own tests compare against
the files in `tests/doc/golden/`, which are rewritten by
`GAMMA_BLESS=1 cargo test --test doc` after an intended change to the output.

## Tests

There are two kinds of tests.

**Unit tests** live next to the code they test, in `#[cfg(test)]` modules.

**Integration tests** (`tests/*.rs`) run the real `gamma` binary: the formatter
(`fmt.rs`), the package manager (`pkg.rs`, needs `git`), the language server
(`lsp.rs`), the documentation generator (`doc.rs`), `gamma repl` (`repl.rs`),
`gamma test` and `gamma bench` (`testing_cli.rs`), build settings in `gamma.toml`
(`build_profiles.rs`), the internal compiler error report (`ice.rs`), the C runtime
(`runtime.rs`), how native builds link the runtime (`native_build.rs`), debug
information (`debug_info.rs`), the Cranelift back end (`cranelift.rs`), the C toolchain variables and cross compilation
(`cross.rs`), foreign calls from the interpreter (`ffi_interp.rs`), the layout of an
installed release (`install_layout.rs`), very long expressions
(`long_expressions.rs`), functions with thousands of statements (`long_functions.rs`), the self-hosted compiler in `compiler/`, whose output must match the reference implementation's on every `.gm` file of the repository (`selfhost.rs`, see [`self-hosting.md`](self-hosting.md)), and the programs in `examples/` (`examples.rs`, below). The
header comment of each file says what it needs and when it skips itself.

**Conformance tests** are Gamma source files under `tests/conformance/`,
grouped into directories by area (`lex/`, `parse/`, …). The runner,
`tests/conformance.rs`, runs each file through the `gamma` binary and checks the
outcome against expectations written in the file:

```gamma
//! stage: parse
//! dump

main :: fn() {
    x := 1 + 2 * 3
    y := a & b | c          //~ ERROR cannot mix
}
```

| Directive | Meaning |
| --- | --- |
| `//! stage: lex\|parse\|check\|run\|test` | how far to run the compiler (default `run`); `test` runs the file's `test` declarations |
| `//! dump` | compare the stage's dump with a golden file: `<name>.tokens` (lex) or `<name>.ast` (parse) |
| `//! arch: x86_64` | run the test only on hosts of that architecture (`aarch64`, `s390x`, …) |
| `//! os: unix` | run the test only when compiling for that kind of operating system (`unix`: all but Windows; or `windows`); for tests of what the other kind lacks (POSIX-only C functions, the System V calling convention). Keep these few: a test that is not about the platform should work on both, with `when target.os` where it must differ |
| `//! endian: little` | run the test only on targets of that byte order (`big`), for tests that show the layout of values in memory |
| `//! needs: f16` | leave the test out of the C lanes where the target's C compiler has no `_Float16` |
| `//! mode: fast` | build a `run` or `test` test in that mode (`debug`, `checked`, `safe`, `fast`; default `debug`); `safe` tests run only through the C back end and need Fil-C, and are skipped without it |
| `//! native: <reason>` | run a `run` or `test` test only through the C back end, for what the interpreter cannot do |
| `//! filter: text` | run a `test` test with `gamma test --filter=text` |
| `//! cranelift: skip <reason>` | leave the test out of the Cranelift lane (below) |
| `//! safe: skip <reason>` | leave the test out of the Safe lane (below), because its behavior differs in Safe builds by design |
| `//! stderr` | also compare standard error with the sibling `<name>.stderr` (for trap reports) |
| `//! exit: N` | expected exit status of a `run` or `test` test (default 0) |
| `//~ ERROR text` | an error whose message contains `text` is reported on this line |
| `//~ WARNING text` | likewise for a warning |
| `//~ NOTE text` | a note of some diagnostic on this line: a secondary location, such as a call in a compile-time call stack, or an explanation attached to a diagnostic reported here (unannotated notes are allowed) |
| `//~? ERROR text` | an error without a source location (e.g. a missing `main`) |

One line may carry several annotations, such as `//~ ERROR a //~ NOTE b`.
Every error and warning the compiler reports must be expected by an annotation,
and every annotation must be matched. The expected standard output of a `run` or
`test` test is the sibling file `<name>.stdout` (empty if absent), and the sibling
file `<name>.stdin`, if present, is the program's standard input.

A test of several modules is a directory whose name ends in `.test`, such as
`tests/conformance/run/imports.test/`. The program's root module is its `main/`
subdirectory if there is one, and the directory itself otherwise; the other
subdirectories hold the modules it imports (and may carry `gamma.toml` files).
Directives and annotations may be written in any of its files, and its expected
output is the sibling `<name>.stdout`.

Useful environment variables (`GAMMA_STD` points the compiler at another standard
library directory; by default it uses the one next to the installed binary, else
`lib/std` of the source tree):

```sh
GAMMA_TEST_FILTER=parse/match cargo test --test conformance   # run a subset
BLESS=1 cargo test --test conformance                         # rewrite golden files
```

Only bless after checking that the new output is correct; `git diff` shows what
changed.

Every `run` test that expects no errors also runs through the C back end and
must produce the same output and exit status as with the interpreter. This
needs a C compiler (`$CC` or `cc`); without one the C lanes are skipped, unless
`GAMMA_TEST_C=1` is set (as in CI), which makes that a failure. Set `GAMMA_TEST_C=0` to skip it, and
`GAMMA_TEST_DEBUG_INFO=1` to build every such program with `-g` (worth doing after
changing `src/cgen/debug.rs` or `src/cgen/views.rs`). The C
runtime has its own tests (`cargo test --test runtime`, in `runtime/rt_test.c`).

**Cranelift lane.** The same tests also run through `--backend=cranelift` when the C
toolchain targets 64-bit Linux or macOS on x86-64 or AArch64 and its programs can run
(built for the host, or through `GAMMA_RUNNER`), with the same expectations; set
`GAMMA_TEST_CRANELIFT=0` to skip the lane. CI runs it on Linux and macOS, and on AArch64
Linux under QEMU, which is also how to run it locally for the other architecture:

```sh
CC=aarch64-linux-gnu-gcc CFLAGS=-static CC_x86_64_unknown_linux_gnu=gcc \
  GAMMA_RUNNER="qemu-aarch64 -L /usr/aarch64-linux-gnu" \
  cargo test --test conformance --test cranelift
```

(`CC_x86_64_unknown_linux_gnu` keeps the build scripts of dependencies on the host's
compiler.) `tests/cranelift.rs` checks the back end's C calling conventions against code
the C compiler compiles. `GAMMA_DUMP_CLIF=1` makes the compiler print the Cranelift IR of
every function it generates, the first thing to look at when the lane fails.

To debug the interpreter's bytecode VM (see `docs/architecture.md`):

- `GAMMA_VM_DUMP=name` (or `*`) prints the bytecode of each function of that name
  as it is compiled, or the line of the compiler that could not translate it.
- `cargo build --release --features vm-profile` builds a `gamma` that, with
  `GAMMA_VM_PROFILE=1`, prints how often each opcode ran when `gamma run` exits;
  with `GAMMA_VM_PAIRS=1` as well it lists the most frequent pairs of consecutive
  opcodes, which is where to look for instructions worth fusing.

Large Debug builds are split into several C files compiled in parallel (see
`docs/architecture.md`). Test programs are too small to be split, so CI runs the
conformance tests with `GAMMA_C_UNITS=3` as well, which splits every Debug build
into three units (and fails a build that cannot be split):

```sh
GAMMA_C_UNITS=3 cargo test --test conformance --test examples
```

**Safe lane.** Tests marked `//! mode: safe` (in `tests/conformance/safe/`) build
with Fil-C and run when it is found. To install it, download a release from
<https://github.com/pizlonator/fil-c/releases> (the `optfil` x86_64 tarball),
extract it and run `sudo ./setup.sh`, which installs to `/opt/fil`; the build
looks for `filcc` on `PATH`, then in `/opt/fil/bin`. Then

```sh
GAMMA_TEST_SAFE=1 cargo test --test conformance
```

requires Fil-C and adds the lane: every `run` and `test` test in the default or
Checked mode also runs as a Safe build and must match its expected output and
exit status (standard error is not compared). Mark a test that differs by design
(for example one that depends on a Debug-only trap or on the size of a stack)
with `//! safe: skip <reason>`. `GAMMA_TEST_SAFE=0` turns the Safe tests off. CI has
a job that installs Fil-C and runs the lane.

The suite also runs with a cross compiler, when the machine can run its output
(for example through qemu-user and binfmt):

```sh
CC=aarch64-linux-gnu-gcc CFLAGS=-static cargo test --test conformance --test runtime
```

`CC` and `CFLAGS` also reach the build scripts of C dependencies (libffi), which
must compile for the host. If the build fails to link, point them at the host
compiler with the target-specific variables, as CI does:
`CC_x86_64_unknown_linux_gnu=gcc CFLAGS_x86_64_unknown_linux_gnu=`.

Without binfmt, `GAMMA_RUNNER` names the emulator that starts the executables, and
a big-endian target works the same way (CI runs it on s390x):

```sh
sudo apt-get install gcc-s390x-linux-gnu qemu-user
GAMMA_RUNNER="qemu-s390x -L /usr/s390x-linux-gnu" CC=s390x-linux-gnu-gcc CFLAGS=-static \
    cargo test --release --test conformance --test runtime
```

Write tests so that they print the same on both byte orders: use `target.endian`
in a `when` where a program inspects its own memory, or mark the test `//! endian:`.

`tests/cross.rs` checks the toolchain variables and target detection; most of it
needs `clang`.

**Examples.** The programs in `examples/` are run by `tests/examples.rs` on both back
ends. Each run of an example is a file `tests/examples/<case>.run`, naming the example
and giving its arguments, standard input and expected exit status, next to the
expected output `<case>.stdout`; the format is described in the header of
`tests/examples.rs`. A new example needs at least one case (a test checks this) and
must be formatted with `gamma fmt`. Programs run with `examples/` as their working
directory, so a case's paths are the ones the example's doc comment tells readers to
type. `GAMMA_TEST_FILTER` and `BLESS=1` work as for the conformance tests:

```sh
GAMMA_TEST_FILTER=wordfreq cargo test --test examples
```

**Documentation examples.** The code blocks of the guides for C and Rust programmers
(`docs/guide/for-c-programmers.md`, `docs/guide/for-rust-programmers.md`) are checked
by `tests/doc_examples.rs`. A block tagged `gamma` is a whole program that must be
formatted and must run on both back ends, printing what the `output` block after it
shows; `gamma test` blocks must pass `gamma test`; `gamma error` blocks must be rejected
with errors exactly on the lines marked `// error:`; `gamma fragment` blocks are not
checked. The format is described in the header of `tests/doc_examples.rs`, and
`GAMMA_TEST_FILTER` selects blocks by `<document>:<line>`:

```sh
GAMMA_TEST_FILTER=for-rust cargo test --test doc_examples
```

When adding a feature, add conformance tests that follow the specification
section by section: valid programs that exercise each rule, and invalid programs
that check each compile error the section requires.

## Benchmarks

`bench/benches.gm` holds `bench` declarations, run with `gamma bench bench/` (see
`bench/README.md`); `bench/programs/` holds small programs for counting interpreter
instructions. On pull requests CI runs `scripts/bench-compare.sh` to compare the
benchmarks of the branch with those of its base; the job only reports, it never fails.

## Generated files

`lib/std/text/tables.gm` and the conformance test `tests/conformance/run/std_text_ucd.gm`
are generated from the Unicode Character Database by `tools/gen_text_tables.py` (Python 3,
standard library only). To move to a new Unicode version, download the files the script
lists from `https://www.unicode.org/Public/<version>/ucd/` and run it from the
repository root with the download directory as its argument. It checks the assumptions
`std/text` makes about the properties and fails if a new version breaks them.

The table of powers of ten in `gm_shortest` (`runtime/gm_rt.c`, the shortest-digits float
printer) is generated by `tools/gen_float_table.py` (Python 3, standard library only), which
also checks the integer approximations of the logarithms the C and Rust code use. The
interpreter (`src/interp/num.rs`) computes the same table at start-up. The algorithm is
Schubfach; both implementations are tested against a slow search over the exact decimal
expansion, which defines the result (spec §21.6.1).

## The bootstrap seed

`bootstrap/seed/` is the C that `gammac` generates for itself, from which
`bootstrap/build.sh` builds `gammac` with only a C compiler (`bootstrap/README.md`). It is
generated, never edited: `bootstrap/seed/SOURCE` names the git trees of `compiler/`,
`lib/` and `runtime/` it comes from, and the CI job "Bootstrap from the seed" fails if the
seed is not exactly what the compiler of those trees generates for them
(`bootstrap/verify.sh`), if it no longer builds the current `gammac`, or if that build is
not a fixed point.

Refresh the seed when `gammac`'s sources stop building with it: commit the change to
`compiler/`, `lib/` or `runtime/`, then run `tools/refresh-seed.sh` and commit
`bootstrap/seed/`. The script builds `gammac` from the current seed (or with the `gammac`
given as its argument, such as one built by `gamma build compiler`), has it build itself,
and writes the C that compiler generates for itself. Because a refreshed seed has to be
built by the previous one, a change that has `gammac`'s own sources use a new language
feature comes in two steps: first teach the compiler the feature and refresh the seed,
then use the feature in `compiler/`. Changes that do not need a refresh leave the seed
alone, which keeps the repository from growing with every change.

## Releases

A release is a tag `vX.Y.Z` on `main` whose version matches `version` in `Cargo.toml`.
Pushing the tag runs `.github/workflows/release.yml`, which:

1. builds `gamma` in release mode on Linux (x86_64 and arm64, on Ubuntu 22.04 so that the
   binaries run with older glibc versions), macOS (x86_64 and arm64) and Windows (x86_64,
   with the C runtime linked statically);
2. packages each with `tools/package-release.sh` as `gamma-<os>-<arch>.tar.gz` (a `.zip`
   on Windows) plus a `.sha256` file. The archive is an installation prefix: `bin/gamma`, the standard library
   in `lib/gamma/std` (an installed compiler finds it relative to its own path), `LICENSE`
   and `README.md`;
3. unpacks each archive, hides the source tree's `lib/std`, and checks that the packaged
   compiler runs a program that imports `std/...`, on the interpreter and through C;
4. checks that the CI workflow passed on the tagged commit (a tag on a commit whose CI
   failed or never ran publishes nothing), records a build provenance attestation for each
   archive (`gh attestation verify <archive> --repo chroma04/gamma-lang` checks one), and
   creates the GitHub release with the archives, `SHA256SUMS`, `tools/install.sh` and
   `tools/install.ps1`.

To cut a release, follow [`versioning.md`](versioning.md): in short, update
`CHANGELOG.md`, bump `version` in `Cargo.toml` (and `Cargo.lock` with `cargo build`),
merge that, then `git tag vX.Y.Z && git push origin vX.Y.Z`. Running the workflow by hand
(workflow dispatch) builds and checks the archives without publishing anything, which is a
good test after changing the workflow or the packaging script.

`tools/install.sh` downloads the archive for the machine it runs on, checks its checksum and
installs it under `$GAMMA_INSTALL` (default `~/.gamma`). `GAMMA_VERSION` picks a release and
`GAMMA_DOWNLOAD` a different download directory; `GAMMA_DOWNLOAD=file:///path/to/dist` tests
the script against archives built locally with `tools/package-release.sh`.
`tools/install.ps1` does the same on Windows.

## The editor grammars

`editors/vscode` (TextMate grammar and its scope tests) and `editors/tree-sitter-gamma`
need Node.js; CI runs `npm ci && npm test` in both, checks that the generated tree-sitter
parser is up to date (`npx tree-sitter generate`) and that it parses the standard library and
every conformance test the compiler accepts. Change the grammars along with the language's syntax; see
`editors/README.md`.

## Fuzzing

`fuzz/` is a [cargo-fuzz](https://github.com/rust-fuzz/cargo-fuzz) crate (outside the
main workspace, so `cargo build` and `cargo test` never compile it). Its targets feed
arbitrary text to the front end: `parse` lexes and parses it, `fmt` formats
text the parser accepts and checks that the result formats to itself, and `check` runs
the whole front end (name resolution, type checking, lowering) on it as a single-file
program, with the real standard library from `lib/std` of the source tree. Two targets
cover what comes from other people's repositories: `manifest` parses text as a
`gamma.toml` and as a `gamma.lock` (and checks that an accepted lock names no path outside
the package store), and `archive` unpacks bytes as a package archive into a scratch
directory and checks that nothing is written outside it. Diagnostics for
invalid input are expected; panics, stack overflows, hangs and quadratic blowups are bugs.
CI checks that the targets compile (`RUSTC_BOOTSTRAP=1 cargo check --manifest-path
fuzz/Cargo.toml`).

```sh
rustup toolchain install nightly --profile minimal
cargo install cargo-fuzz
fuzz/seed.sh                      # corpus from lib/, examples/ and tests/conformance/
cargo +nightly fuzz run parse -- -max_total_time=600 -timeout=5 -rss_limit_mb=4096
cargo +nightly fuzz run fmt -- -max_total_time=600 -timeout=5 -rss_limit_mb=4096
cargo +nightly fuzz run manifest -- -max_total_time=600 -timeout=5
cargo +nightly fuzz run archive -- -max_total_time=600 -timeout=5 -max_len=65536
cargo +nightly fuzz run check -s none -- -max_total_time=600 -timeout=20 -rss_limit_mb=4096 \
    -max_len=8192 -jobs=4 -workers=4
```

Every `check` run loads and checks the standard library, so it is much slower than the
other targets; `-s none` (no sanitizer) and a small `-max_len` keep it at a useful rate,
and `-jobs`/`-workers` use several cores.

Crashing inputs are saved in `fuzz/artifacts/<target>/`; `fuzz/corpus/` and
`fuzz/artifacts/` are not checked in. To turn a crash into a regression test, shrink it
with `cargo +nightly fuzz tmin <target> <artifact>`, fix the bug, and add the small input
as a conformance test (for example `tests/conformance/parse/<name>.gm` with
`//! stage: parse` and `//~ ERROR` annotations, or `tests/conformance/check/<name>.gm`
with `//! stage: check`) or a unit test in the module concerned.
Fuzzing is not part of CI.

## Differential testing

`tools/difftest/` is a small Rust crate (its own workspace, like `fuzz/`, with no
dependencies) that generates random programs and checks that every back end gives the
same result. A seed determines a program: well-typed, terminating (loops have constant
bounds, calls only go to earlier functions, recursion is bounded by a depth argument),
and covering integers of all widths from 8 to 64 bits with checked, wrapping, saturating
and optional-result operators and conversions, comparisons, boolean logic, `if`, `while`
and `for`, local variables and assignment, functions and bounded recursion, structs,
arrays, unions and `match`, optionals and results with `try`, and `print`. Checked
operations may trap; the back ends must then trap in the same way.

Four optional areas are switched on per program by the seed (so some programs stay small and
focused), and the generator keeps them well typed and terminating:

- **Generics**: generic functions with inferred or written type arguments instantiated at
  many integer types (`g_pick(T: type, a: T, b: T, c: bool)`, constrained `Ord` and `Int`
  functions, a generic function that takes a closure capturing variables) and the generic
  struct `Pair(T)` with functions taking pointers to it.
- **Strings**: literals (empty, UTF-8, escapes), `.len`, `first`, `get`, `index_of`,
  in-bounds slicing, comparison and `is`, `starts_with`, `std/str` (`find`, `count`, `trim`,
  `parse_int`, ...), and strings built in a growable buffer with `write` templates (padding,
  alignment), also in loops, which needs the `Alloc` effect. A string that points into such a
  buffer is never reassigned or returned, since its scope frees the buffer.
- **Effects and handlers**: a user-defined effect `Ev` with a `fn` operation, an `esc`
  operation (the handler prints and breaks out of the `with` with a value) and a counter
  captured by the handler, and an effect `Co` with `ctl` operations whose handler resumes
  with values or cancels the suspended code and leaves the `with`, running on a fiber stack
  (`with h on stack { ... }`). Functions declare `needs Ev` / `needs Co`, and are only called
  where the effect is available or after `main` installs handlers; `with` blocks nest, so
  exits cross fibers and handlers perform `Ev` handled further out.
- **Fibers**: producer and consumer tasks that talk through `std/fiber/channel` under the
  cooperative scheduler of `std/fiber` (`fiber.spawn`, `fiber.Sched.yield_now`, `fiber.Sched.join`).
  Scheduling is round-robin and uses no clock, so the output is deterministic.

Each program runs with the interpreter (`gamma run`) and with the C back end
(`gamma run --backend=c`) compiled by every C compiler listed in `--cc` (default: gcc and
clang; compilers that are not installed are skipped). Standard output, standard error
(with the program's path replaced by `FILE`) and the exit status must be equal.

```sh
cargo build --release                     # the gamma executable it tests
cargo build --release --manifest-path tools/difftest/Cargo.toml --target-dir target
target/release/difftest --start 0 --count 1000      # seeds 0..1000, in parallel
target/release/difftest --print 17                  # show the program of seed 17
target/release/difftest --save 17 /tmp/p.gm         # write it to a file
target/release/difftest --reduce /tmp/p.gm          # shrink a program that disagrees
```

Programs on which the back ends disagree are kept in the work directory (`--work`, by
default `difftest/` in the system temporary directory) and reported with all three
outputs; `--reduce` deletes lines while the same back ends keep disagreeing and writes
`<file>.min.gm`. A program that the compiler rejects is reported too, since the
generator should never produce one. `--mode=checked` tests Checked builds, `--gamma`
(or `$GAMMA`) selects the executable. To turn a disagreement into a regression test, decide
from the specification which side is wrong, fix it, and add the reduced program as
`tests/conformance/run/<name>.gm`, which runs on both back ends. Like fuzzing,
differential testing is not part of the main test job, but CI runs it in its own job
("Differential tests (gcc, clang)", Ubuntu only): it builds `gamma` in release mode and
`difftest`, runs the fixed seeds 0..100 (reproducible anywhere), and then 200 seeds starting
at a point that moves with the run number, so successive runs cover new programs. Both need
gcc and clang. A failure prints the seed and the three outputs; `difftest --print SEED`
shows the program and `--reduce` shrinks it. The job takes a few minutes.

## The website

`web/` holds the website: a generator that renders the guide, the specification, the
library reference and these documents into pages, and a playground that runs the
interpreter, compiled to WebAssembly, in the browser. The *Website* workflow builds it on
every pull request (failing on a broken internal link, or on a playground example that
does not run) and publishes it from `main`. [`web/README.md`](../web/README.md) explains
how to build and preview it locally.

The compiler must keep building for `wasm32-wasip1`: code that needs threads, the C
library or Cranelift goes behind `cfg(not(target_family = "wasm"))`, with a stand-in for
WebAssembly hosts (see `src/interp/ffi_wasm.rs`).

## The specification

`gamma-spec.md` is the definition of the language. If an implementation question
reveals a contradiction, gap or impractical rule in the specification, fix the
specification in the same change (keeping it self-consistent and updating its
examples), rather than letting the implementation silently diverge.

## Code style

- Follow the existing structure described in [`architecture.md`](architecture.md).
- Public items get doc comments; other comments explain *why*, not *what*.
- Diagnostics follow chapter 23 of the specification: every compile error is a rule
  of the specification (an implementation limit says so), and hints come from the
  program and the specification only, never from guesses about other languages or
  look-alike characters beyond those §23.4 lists. They are lowercase, name the
  construct involved, and suggest a fix when the rule leaves one. When the fix is a mechanical edit that is right whenever
  the diagnostic is, attach it too (`Diagnostic::with_fix`), so that `gamma fix` and
  the language server can apply it; when there are several reasonable edits, attach
  each (`gamma fix` then leaves the choice to the programmer). Test it in
  `tests/fix.rs`.
- No `unwrap()` on user-controlled input: report a diagnostic instead.
