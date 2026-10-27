# Contributing

Bugs and proposals go in GitHub issues (the issue forms ask for what helps); security
problems are reported privately, as [`SECURITY.md`](../SECURITY.md) describes.

## Building

`gamma` is written in Gamma (`compiler/`) and builds itself. From a checkout you need only
a C compiler (`cc`, gcc or clang):

```sh
bootstrap/build.sh               # writes target/bootstrap/gamma (a few minutes)
```

The script compiles the C seed in `bootstrap/seed/` and builds the compiler of the
current sources with it (see [The bootstrap seed](#the-bootstrap-seed)). After changing
`compiler/`, `lib/` or `runtime/`, rebuild with the compiler you have:

```sh
GAMMA_STD=$PWD/lib/std GAMMA_RUNTIME=$PWD/runtime target/bootstrap/gamma build compiler -o <out>
```

## Checks

Everything builds and tests with a C compiler alone. Continuous integration builds `gamma`
from the seed in every job and runs, on Linux and macOS (and the conformance tests and
examples on Windows with MinGW-w64 gcc):

```sh
tools/test.sh                   # all the suites, with target/bootstrap/gamma
gamma fmt --check compiler lib examples tests/harness
gamma test compiler             # the compiler's own unit tests
```

Run them before sending a change. CI also has jobs for the Safe, big-endian, AArch64 and WASI
lanes (see Tests), differential testing and fuzzing (below), the editor grammars (see below
and [`editors.md`](editors.md)) and, on pull requests, benchmarks. Building and testing
the compiler needs no Rust toolchain; the Zed editor extension (`editors/zed`) is Rust, because
Zed requires it, and CI builds it with the Rust toolchain.

## Formatting Gamma code

`gamma fmt <file or directory>...` rewrites `.gm` files in the canonical style;
`gamma fmt --check` only lists the files that would change (exit status 1 if
there are any), and `gamma fmt -` filters standard input to standard output. The
formatter (`compiler/fmt/`) works on tokens and never changes meaning: it keeps the
line breaks of the source, collapses runs of blank lines, indents by bracket
nesting (four spaces), normalizes the spacing inside a line and copies comments
and literals verbatim. Trailing comments and the columns of aligned `::`/`=>`
keep their spacing. Files with lexical errors are not formatted. The `fmt` suite
checks that formatting keeps the tokens and is idempotent for every `.gm` file in the
repository and on variants of each that the formatter must change (no indentation, collapsed
or added spaces, CR LF line ends, cut off in the middle). The library itself is kept formatted (the same test fails
otherwise): run `gamma fmt lib` after editing it. Conformance tests are not
reformatted, since some pin columns in their expected output.

## Lint rules

`gamma lint` ([`lint.md`](lint.md)) is kept clean on `lib/std` and `examples/`:
the `lint` suite fails if either has a warning. To add a rule, give it an entry in
`RULES` (`compiler/lint/lint.gm`) with its default level and a one-line summary, then
report it from the AST walk (`compiler/lint/syntax.gm`) if the syntax tree is enough, or
as a `Finding` from `compiler/lint/checked.gm` if it needs types or name resolution. Attach a
fix only when it cannot change what the program does; `gamma lint --fix` undoes a
fix that stops the program compiling, but not one that changes its behavior. Add a
conformance test for it under `tests/conformance/lint/`, with the cases it must not
report, and a row to the tables of `docs/lint.md`.

## API documentation

`gamma doc` (`compiler/doc/`) writes Markdown or HTML documentation for the `pub`
declarations of a module from the syntax tree, their `///` comments and the `//!`
comments at the top of the module's files. It copies signatures from the source text,
so a module needs to parse but not to type-check. Document the standard library in
those comments.

The reference in `docs/std/` is generated, not written by hand. After changing a doc
comment or a public declaration in `lib/std`, regenerate it:

```
gamma doc lib/std --all -o docs/std
```

`tools/test.sh doc/` fails with this command in its message while the checked-in files
differ from the output. `gamma doc lib/std --all --missing -o /tmp/std` also lists the
public declarations that have no doc comment. The generator's own tests compare against
the files in `tests/doc/golden/`, which are rewritten by
`BLESS=1 tools/test.sh doc/` after an intended change to the output.

## Tests

There are two kinds of tests.

**Unit tests** are `test` declarations next to the code they test; `gamma test compiler`
runs those of the compiler.

**Harness tests** run an executable of the compiler, and are written in Gamma: the test
harness in `tests/harness/` (package `harness`, which depends on the compiler's modules for
what it shares with it). `tools/test.sh` builds both and runs them:

```sh
tools/test.sh [-j N] [-v] [--list] [--filter TEXT] [PREFIX...]
```

It builds `target/bootstrap/gamma` from the current sources (from the seed the first time,
then incrementally with the previous one, and only when `compiler/`, `lib/` or `runtime/`
changed), builds the harness with it, and runs the harness. Each test has a name
`suite/test`, such as `conformance/run/std_process.gm` or `cli/every_command_takes_help`. A
`PREFIX` selects the tests whose name starts with it, so `tools/test.sh conformance/` runs one
suite and `tools/test.sh conformance/run/std_` a part of it; with no prefix every test runs.
`--filter TEXT` (or `GAMMA_TEST_FILTER=TEXT`) keeps the tests whose name contains `TEXT`,
`--list` prints the names instead of running, `-v` lists every test as it passes or is skipped, and
`-j N` sets the number of tests that run at once (default: one per processor). Tests run in
parallel on threads, those that took longest in the previous run first. The exit status is 0 if
no test failed.

```sh
tools/test.sh                              # every suite
tools/test.sh conformance/ examples/       # two suites
tools/test.sh --filter parse/match         # a subset by name
GAMMA_TEST_COMPILER=$PWD/target/bootstrap/gamma target/bootstrap/harness cli/   # a compiler and a harness you built yourself
```

### Suites

`suites()` in `tests/harness/main.gm` lists the suites; each is a module directory of
`tests/harness/` with a function that adds its tests. The header comment of each module says
what it checks, what it needs and when it skips itself.

| Suite | Tests |
| --- | --- |
| `conformance` | every file of `tests/conformance/` (below) |
| `examples`, `doc_examples` | the programs in `examples/`; the code blocks of the guides (below) |
| `cli`, `testing_cli`, `repl`, `lsp` | the command line, `gamma test` and `gamma bench`, `gamma repl`, the language server |
| `fmt`, `lint`, `fix`, `doc` | the formatter, `gamma lint`, `gamma fix` and the documentation generator (the checked-in `docs/std/` must be current) |
| `pkg`, `build_profiles` | the package manager (needs `git`), build settings in `gamma.toml` |
| `native_build`, `debug_info`, `runtime` | how native builds link the runtime, debug information, the C runtime (`runtime/rt_test.c`) |
| `cross`, `freestanding`, `wasi`, `lanes` | the C toolchain variables and cross compilation, freestanding targets, WebAssembly for WASI, the lanes of other platforms (below) |
| `long_expressions`, `long_functions` | very long expressions, functions with thousands of statements |
| `selfhost` | the compiler reaches a fixed point compiling itself, its unit tests pass, its sources are formatted, the split C units build, and a packaged release runs from its installed layout (see [`self-hosting.md`](self-hosting.md)) |
| `harness` | the harness itself |

### Environment

| Variable | Meaning |
| --- | --- |
| `GAMMA_TEST_COMPILER` | the `gamma` executable under test; `tools/test.sh` sets it to the one it builds, and respects a value you set |
| `GAMMA_TEST_HARNESS` | a prebuilt harness executable, instead of building one (for example one cross-built for Windows) |
| `GAMMA_TEST_C` | `0` skips the lanes that need a C compiler; `1` makes a missing C compiler a failure (CI sets it) |
| `GAMMA_TEST_SAFE` | `1` adds the Safe lane (Fil-C) to the conformance tests; `0` turns the Safe tests off |
| `GAMMA_TEST_LANE` | `safe`, `s390x`, `aarch64` or `wasi`: the lane of the `lanes` suite (below) |
| `GAMMA_TEST_TIMEOUT` | seconds a program may run (default 600); the program and everything it started are killed |
| `GAMMA_TEST_TMPDIR` | where tests keep their files (default `target/test-tmp`) |
| `GAMMA_TEST_FILTER` | the same as `--filter` |
| `GAMMA_TEST_DEBUG_INFO` | `1` builds every compiled test program with `-g` |
| `BLESS` | `1` rewrites golden files from the output instead of comparing; check `git diff` afterwards |
| `CC`, `CFLAGS`, `LDFLAGS`, `GAMMA_RUNNER`, `WASI_SDK_PATH`, `WASM_OPT` | the C toolchain the suites use, and an emulator for executables built for another platform |

`GAMMA_STD` points the compiler at another standard library directory; by default it uses
the one next to the installed binary, else `lib/std` of the source tree.

### Writing a test

A test is a function `fn(t: ^kit.T) -> kit.Fail!() needs kit.Run`, registered in the `tests`
function of its suite. A check that fails records its message in `t` and returns `Fail`, so a
test reads as a list of `try`s. Each test runs on a thread of its own with an arena that is
freed when it ends, so tests allocate freely. The module `tests/harness/kit` has the pieces:

- checks: `t.check(cond, template, args…)`, `t.expect_text(what, expected, actual)` (a diff on
  failure), `t.expect_contains(what, text, needle)`, `t.fail(…)` and `t.skip(…)`;
- commands: `t.gamma(argv)` is a command that runs the compiler under test; `kit.command(prog)`
  runs another program; `c.set(name, value)`, `c.stdin(bytes)` and `c.dir` adjust it;
  `t.run(c)` runs it with the time limit and returns its `status`, `out` and `err`,
  `t.run_ok(c)` also fails the test if it did not succeed, and `t.try_run(c)` returns `null`
  when the program is not installed;
- files: `t.scratch(name)` is a fresh directory for the test, `t.write(path, text)`,
  `t.read(path)`, `t.repo(rel)` a path in the repository.

This is a suite with a test that every command takes `--help` (adapted from
`tests/harness/cli/cli.gm`):

```gamma
kit :: import "../kit"
str :: import "std/str"

pub tests :: fn(cfg: ^const kit.Config, out: ^List(kit.Test)) needs Alloc {
    out.push(kit.Test(name: "every_command_takes_help", run: every_command_takes_help))!
}

every_command_takes_help :: fn(t: ^kit.T) -> kit.Fail!() needs kit.Run {
    for cmd in ([_]string("check", "run", "fmt")) {
        out := try t.run(t.gamma([_]string(cmd, "--help")))
        try t.check(out.success(), "`gamma {} --help` failed:\n{}", cmd, out.err)
        try t.expect_contains("the help", out.out, "usage: gamma")
    }
    return ()
}
```

A new suite is a directory of `tests/harness/` with a module like this one, imported and added
to `suites()` in `main.gm`.

### Conformance tests

Conformance tests are Gamma source files under `tests/conformance/`, grouped into directories
by area (`lex/`, `parse/`, `run/`, …). The suite `tests/harness/conformance` runs each file
through the compiler under test and checks the outcome against expectations written in the
file itself, such as `//! stage: parse` for how far to run the compiler and `//~ ERROR text`
at the end of a line for a diagnostic expected there. The directives (stage, golden dumps,
build mode, architecture, byte order, operating system, requirements, expected exit status and
output, annotations), the layout of a test of several modules (`name.test/`), and the rules
for which tests run where are documented in the header comment of
[`tests/harness/conformance/conformance.gm`](../tests/harness/conformance/conformance.gm).

```gamma
//! stage: parse
//! dump

main :: fn() {
    x := 1 + 2 * 3
    y := a & b | c          //~ ERROR cannot mix
}
```

Every error and warning the compiler reports must be expected by an annotation, and every
annotation must be matched. Only bless (`BLESS=1 tools/test.sh conformance/`) after checking
that the new output is correct.

A `run` test that expects no errors runs twice and must give the same output and exit status
both times: first in the interpreter (`--backend=interp`), then compiled through C. A `test`
test runs through C (`gamma test` always does). The C lane needs a C compiler (`$CC` or `cc`);
without one it is skipped, unless `GAMMA_TEST_C=1` is set (as in CI), which makes that a
failure. The C runtime has its own tests (`tools/test.sh runtime/`, in `runtime/rt_test.c`).

Large Debug builds are split into several C files compiled in parallel (see
[`architecture.md`](architecture.md#the-c-back-end)). Test programs are too small to be split,
so CI runs the conformance tests and the examples with `GAMMA_C_UNITS=3` as well, which splits
every Debug build into three units (and fails a build that cannot be split):

```sh
GAMMA_C_UNITS=3 tools/test.sh conformance/ examples/
```

When adding a feature, add conformance tests that follow the specification section by
section: valid programs that exercise each rule, and invalid programs that check each compile
error the section requires.

### Other lanes

The `lanes` suite builds every conformance `run` test for a platform the host is not, and
checks the output and exit status. `GAMMA_TEST_LANE` names the lane, and without it the suite
does nothing:

| `GAMMA_TEST_LANE` | What it does | Needs |
| --- | --- | --- |
| `safe` | builds each test as a Safe build and runs it (standard error is not compared) | [Fil-C](https://fil-c.org) |
| `s390x` | cross-compiles for big-endian s390x, runs under qemu-user | `s390x-linux-gnu-gcc`, `qemu-s390x` |
| `aarch64` | cross-compiles for AArch64 Linux, runs under qemu-user | `aarch64-linux-gnu-gcc`, `qemu-aarch64` |
| `wasi` | builds for `wasm32-wasi`, runs under Wasmtime | clang with the WASI C library, `wasmtime` |

```sh
GAMMA_TEST_LANE=aarch64 tools/test.sh lanes/
```

To install Fil-C, download a release from <https://github.com/pizlonator/fil-c/releases>
(the `optfil` x86_64 tarball), extract it and run `sudo ./setup.sh`, which installs to
`/opt/fil`; the build looks for `filcc` on `PATH`, then in `/opt/fil/bin`.
`GAMMA_TEST_SAFE=1 tools/test.sh conformance/` also adds the Safe lane to the conformance
suite itself: every `run` and `test` test in the default or Checked mode also runs as a Safe
build and must match. Mark a test that differs by design (for example one that depends on a
Debug-only trap or on the size of a stack) with `//! safe: skip <reason>`. Write tests so that
they print the same on both byte orders: use `target.endian` in a `when` where a program
inspects its own memory, or mark the test `//! endian:`. CI has a job for each lane (the s390x
one also runs the C runtime tests, the WASI one the `wasi` suite).

The `cross` suite checks the toolchain variables and target detection; most of it needs
`clang`. `CC` and `CFLAGS` select the C compiler the suites use, and `GAMMA_RUNNER` names an
emulator that starts executables built for another platform.

### Examples

The programs in `examples/` are run by the `examples` suite on both back ends (the
interpreter and C). Each run of an example is a file `tests/examples/<case>.run`, naming the
example and giving its arguments, standard input and expected exit status, next to the
expected output `<case>.stdout`; the format is described in the header of
[`tests/harness/examples/examples.gm`](../tests/harness/examples/examples.gm). A new example
needs at least one case (a test checks this) and must be formatted with `gamma fmt`. Programs
run with `examples/` as their working directory, so a case's paths are the ones the example's
doc comment tells readers to type. `BLESS=1` rewrites the `.stdout` files:

```sh
tools/test.sh --filter wordfreq examples/
```

### Documentation examples

The code blocks of the guides for C and Rust programmers
(`docs/guide/for-c-programmers.md`, `docs/guide/for-rust-programmers.md`) are checked by the
`doc_examples` suite. A block tagged `gamma` is a whole program that must be formatted
(`gamma fmt --check`) and must run with `gamma run --backend=interp` and with
`gamma run --backend=c`, printing what the `output` block after it shows; `gamma test` blocks
must pass `gamma test` (through C); `gamma error` blocks must be rejected with errors exactly
on the lines marked `// error:`; `gamma fragment` blocks are not checked. The format is
described in the header of
[`tests/harness/doc_examples/doc_examples.gm`](../tests/harness/doc_examples/doc_examples.gm),
and tests are named `<document>:<line>`:

```sh
tools/test.sh --filter for-rust doc_examples/
```

## Benchmarks

`bench/benches.gm` holds `bench` declarations, run with `gamma bench bench/` (see
`bench/README.md`); `bench/programs/` holds small programs that exercise the call, effect and allocation paths. On pull requests CI runs `scripts/bench-compare.sh` to compare the
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
also checks the integer approximations of the logarithms the C code uses. The
interpreter (`compiler/interp/`) computes the same table at start-up. The algorithm is
Schubfach; both implementations are tested against a slow search over the exact decimal
expansion, which defines the result (spec §21.6.1).

## The bootstrap seed

`bootstrap/seed/` is the C that `gamma` generates for itself, from which
`bootstrap/build.sh` builds `gamma` with only a C compiler (`bootstrap/README.md`). It is
generated, never edited: `bootstrap/seed/SOURCE` names the git trees of `compiler/`,
`lib/` and `runtime/` it comes from, and the CI job "Bootstrap from the seed" fails if the
seed is not exactly what the compiler of those trees generates for them
(`bootstrap/verify.sh [GAMMA0]`), if it no longer builds the current compiler, or if that
build is not a fixed point.

Refresh the seed when the compiler's sources stop building with it: commit the change to
`compiler/`, `lib/` or `runtime/`, then run `tools/refresh-seed.sh [GAMMA]` and commit
`bootstrap/seed/`. The script builds the compiler from the current seed (or with the
`gamma` given as its argument, such as one built by `gamma build compiler`), has it build
itself, and writes the C that compiler generates for itself. Because a refreshed seed has
to be built by the previous one, a change that has the compiler's own sources use a new
language feature comes in two steps: first teach the compiler the feature and refresh the
seed, then use the feature in `compiler/`. Changes that do not need a refresh leave the
seed alone, which keeps the repository from growing with every change. Each seed is
exactly what the previous seed's compiler generates, which ties the chain to the last
compiler built by the Rust reference implementation (see
[self-hosting.md](self-hosting.md#retiring-the-reference)).

## Releases

A release is a tag `vX.Y.Z` on `main` whose version matches `version` in
`compiler/gamma.toml`. Pushing the tag runs `.github/workflows/release.yml`, which:

1. builds `gamma` for linux-x86_64, linux-aarch64, macos-x86_64 and macos-aarch64. On each
   runner `bootstrap/build.sh` builds it from the seed, and that compiler then builds the
   release binary in Fast mode (`gamma build --mode=fast compiler`). The Linux binaries
   are built on Ubuntu 22.04 so that they run with older glibc versions. The Windows
   binary (windows-x86_64) is cross-compiled on Linux by the Linux release `gamma` with
   MinGW-w64 (`gamma build compiler --target=x86_64-w64-mingw32`);
2. packages each with `tools/package-release.sh <asset> <gamma binary> <dir>` as
   `gamma-<asset>.tar.gz` (a `.zip` on Windows) plus a `.sha256` file. The archive is an
   installation prefix: `bin/gamma`, `lib/gamma/std`, `lib/gamma/core` and `lib/gamma/runtime`
   (`gm_rt.c`, `gm_rt.h` and the debugger scripts; an installed compiler finds them
   relative to its own path), `LICENSE` and `README.md`;
3. smoke-tests each archive with `tools/smoke-release.sh`: it starts the unpacked `gamma`
   through `PATH` from another directory, with the source tree's libraries and runtime
   hidden, and runs a program on the interpreter, through C and as a built executable;
   the Windows archive is checked on a Windows runner with MinGW-w64 gcc. See
   [architecture.md](architecture.md#installation-layout);
4. checks that the CI workflow passed on the tagged commit (a tag on a commit whose CI
   failed or never ran publishes nothing), records a build provenance attestation for each
   archive (`gh attestation verify <archive> --repo chroma04/gamma-lang` checks one), and
   creates the GitHub release with the archives, `SHA256SUMS`, `tools/install.sh` and
   `tools/install.ps1`.

To cut a release, follow [`versioning.md`](versioning.md): in short, update
`CHANGELOG.md`, bump `version` in `compiler/gamma.toml`, merge that, then
`git tag vX.Y.Z && git push origin vX.Y.Z`. Running the workflow by hand
(workflow dispatch) builds and checks the archives without publishing anything, which is a
good test after changing the workflow or the packaging script.

`tools/install.sh` downloads the archive for the machine it runs on, checks its checksum and
installs it under `$GAMMA_INSTALL` (default `~/.gamma`). `GAMMA_VERSION` picks a release and
`GAMMA_DOWNLOAD` a different download directory; `GAMMA_DOWNLOAD=file:///path/to/dist` tests
the script against archives built locally. To try it: `bootstrap/build.sh`, then
`tools/package-release.sh linux-x86_64 target/bootstrap/gamma dist` and
`GAMMA_DOWNLOAD=file://$PWD/dist GAMMA_INSTALL=/tmp/prefix sh tools/install.sh`.
`tools/install.ps1` does the same on Windows.

## The editor grammars

`editors/vscode` (TextMate grammar and its scope tests) and `editors/tree-sitter-gamma`
need Node.js; CI runs `npm ci && npm test` in both, checks that the generated tree-sitter
parser is up to date (`npx tree-sitter generate`) and that it parses the standard library and
every conformance test the compiler accepts. Change the grammars along with the language's syntax; see
`editors/README.md`.

## Fuzzing

`fuzz/` holds the libFuzzer targets of `gamma`. Fuzzing needs a built `gamma` and clang with
libFuzzer (on Debian and Ubuntu, `apt install clang libclang-rt-dev`). Each target is a Gamma
program that imports the modules of `compiler/` through a path dependency (`fuzz/gamma.toml`)
and feeds arbitrary input to one part of the compiler:

| Target | Input and checks |
| --- | --- |
| `parse` | text, lexed and parsed |
| `fmt` | text the parser accepts is formatted, and the result must format to itself |
| `check` | text as a single-file program through the whole front end (name resolution, type checking, lowering), with the real standard library from `lib/std` of the source tree |
| `manifest` | text as a `gamma.toml` and as a `gamma.lock` (an accepted lock must name no path outside the package store) |
| `archive` | bytes unpacked as a package archive into a scratch directory; nothing may be written outside it |

Diagnostics for invalid input are expected; traps, stack overflows, hangs and quadratic
blowups are bugs.

How a Gamma program becomes a libFuzzer target: `gamma build --emit-c` writes the C of the
program, and `build.sh` compiles it with `-fsanitize=fuzzer-no-link` (coverage
instrumentation, plus `address` for every target but `check`) and links it with libFuzzer's
driver library without its `main`. The generated C defines `main`, which `build.sh` renames
(`-Dmain=gm_program_main`) so that `shim.c` can own the real one. It starts the Gamma program,
whose `main` sets up the runtime and the ambient handlers and then calls
`LLVMFuzzerRunDriver`; libFuzzer calls an `#export`ed Gamma function (§19.4) for every input.
`harness/harness.gm` runs each input in a fresh arena under the default handlers and fixes the
environment variables the compiler reads (`GAMMA_STD`, `GAMMA_CORE`, `GAMMA_RUNTIME`). A trap
ends the process with status 134 through `exit`, which libFuzzer reports as "fuzz target
exited" and records as a crash input like any other; a stack overflow is a signal and a hang
is libFuzzer's timeout.

```sh
GAMMA=target/bootstrap/gamma fuzz/build.sh        # every target; or name some: build.sh parse fmt
fuzz/seed.sh                                      # corpus: the repository's sources, manifests and sample archives
fuzz/run.sh parse -max_total_time=600             # options of the kind of target, then yours
fuzz/run.sh check -max_total_time=600 -jobs=4 -workers=4
fuzz/run.sh parse fuzz/artifacts/parse/crash-0123 # reproduce an input
fuzz/smoke.sh 30                                  # build all, run each for 30 s; fails on a crash
```

`build.sh` reads `GAMMA` (default `gamma` on `PATH`), `CLANG`, `MODE` (default `checked`),
`SAN` and `OPT`; `run.sh` reads `GAMMA_STD` and `GAMMA_RUNTIME` (default: those of the source
tree) and passes everything after the target to libFuzzer. `seed.sh` runs `seed-sources.sh`,
which copies the Gamma sources (the library, the examples and the conformance tests) and the
manifests and lockfiles of the repository into `fuzz/corpus/TARGET`, and adds a few package
archives for the `archive` target (a valid one and the kinds the unpacker must refuse). The
`check` target loads and checks the standard library for every input, so it is slow (tens of
inputs per second); `-jobs` and `-workers` use several cores, and `build.sh` builds it without
a sanitizer by default.

Seeds are in `fuzz/corpus/TARGET`, new inputs go to `fuzz/found/TARGET` and crashing inputs to
`fuzz/artifacts/TARGET`; none of them is checked in. `build.sh selftest` builds a target that
traps, overflows the stack and hangs on purpose, to check that each is recorded (see
`selftest.gm`). To turn a crash into a regression test, shrink it with `-minimize_crash=1`,
fix the bug, and add the small input as a conformance test (for example
`tests/conformance/parse/<name>.gm` with `//! stage: parse` and `//~ ERROR` annotations, or
`tests/conformance/check/<name>.gm` with `//! stage: check`) or a unit test in the module
concerned. CI builds every target and runs each for 30 seconds on the seed corpus
(`fuzz/smoke.sh 30`).

## Differential testing

`tools/difftest/` is a small Gamma program (a package of its own) that generates random programs and checks that every back end gives the
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

Each program runs with the interpreter (`gamma run --backend=interp`) and with the C back
end (`gamma run --backend=c`) compiled by every C compiler listed in `--cc` (default: gcc and
clang; compilers that are not installed are skipped). Standard output, standard error
(with the program's path replaced by `FILE`) and the exit status must be equal.

```sh
bootstrap/build.sh                        # the gamma executable it tests
target/bootstrap/gamma build tools/difftest -o target/difftest
target/difftest --start 0 --count 1000      # seeds 0..1000, in parallel
target/difftest --print 17                  # show the program of seed 17
target/difftest --save 17 /tmp/p.gm         # write it to a file
target/difftest --reduce /tmp/p.gm          # shrink a program that disagrees
```

Programs on which the back ends disagree are kept in the work directory (`--work`, by
default `difftest/` in the system temporary directory) and reported with all three
outputs; `--reduce` deletes lines while the same back ends keep disagreeing and writes
`<file>.min.gm`. A program that the compiler rejects is reported too, since the
generator should never produce one. `--mode=checked` tests Checked builds, `--gamma`
(or `$GAMMA`, else `bootstrap/gamma` next to the `difftest` executable, else
`target/bootstrap/gamma`) selects the executable. To turn a disagreement into a regression test, decide
from the specification which side is wrong, fix it, and add the reduced program as
`tests/conformance/run/<name>.gm`, which runs on both back ends. Like fuzzing,
differential testing is not part of the main test job, but CI runs it in its own job
("Differential tests (gcc, clang)", Ubuntu only): it builds `gamma` from the seed and
`difftest` with it, runs the fixed seeds 0..100 (reproducible anywhere), and then 200 seeds starting
at a point that moves with the run number, so successive runs cover new programs. Both need
gcc and clang. A failure prints the seed and the three outputs; `difftest --print SEED`
shows the program and `--reduce` shrinks it. The job takes a few minutes.

## The website

`web/` holds the website: a generator that renders the guide, the specification, the
library reference and these documents into pages, and a playground that runs the
interpreter, compiled to WebAssembly, in the browser. Both are built with `gamma`
(`web/gen`, `web/play`; see below). The *Website* workflow builds `gamma` from the seed, then the site, on
every pull request (failing on a broken internal link, or on a playground example that
does not run) and publishes it from `main`. [`web/README.md`](../web/README.md) explains
how to build and preview it locally.

The interpreter in `compiler/` must keep building for `wasm32-wasi`, since the playground
is built from it: code that needs threads or the C compiler must not be reached from the
playground's `main` (`web/play/main.gm`). `web/check.mjs` runs every example of the
playground's menu in the built module.

## The specification

`gamma-spec.md` is the definition of the language. If an implementation question
reveals a contradiction, gap or impractical rule in the specification, fix the
specification in the same change (keeping it self-consistent and updating its
examples), rather than letting the implementation silently diverge.

## Code style

- Follow the existing structure of `compiler/` (see [`architecture.md`](architecture.md)).
- Public items get doc comments; other comments explain *why*, not *what*.
- Diagnostics follow chapter 23 of the specification: every compile error is a rule
  of the specification (an implementation limit says so), and hints come from the
  program and the specification only, never from guesses about other languages or
  look-alike characters beyond those §23.4 lists. They are lowercase, name the
  construct involved, and suggest a fix when the rule leaves one. When the fix is a mechanical edit that is right whenever
  the diagnostic is, attach it too (`add_fix` in `compiler/diag/`), so that `gamma fix` and
  the language server can apply it; when there are several reasonable edits, attach
  each (`gamma fix` then leaves the choice to the programmer). Test it in
  the `fix` suite.
- No trap on user-controlled input: report a diagnostic instead.
