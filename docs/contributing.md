# Contributing

## Building

The compiler is a single Rust crate. With a stable Rust toolchain:

```sh
cargo build            # debug build (lightly optimized)
cargo build --release
```

The binary is `target/<profile>/gamma`. `cargo run -- <args>` builds and runs it.

## Checks

Continuous integration runs, on Linux and macOS:

```sh
cargo fmt --all -- --check
cargo clippy --all-targets      # warnings are errors in CI
cargo test --all-targets
```

Run them before sending a change. Formatting uses the settings in
`rustfmt.toml`.

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

## Tests

There are two kinds of tests.

**Unit tests** live next to the code they test, in `#[cfg(test)]` modules.

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
| `//! endian: little` | run the test only on targets of that byte order (`big`), for tests that show the layout of values in memory |
| `//! needs: f16` | leave the test out of the C lanes where the target's C compiler has no `_Float16` |
| `//! mode: fast` | build a `run` or `test` test in that mode (`debug`, `checked`, `safe`, `fast`; default `debug`; `safe` tests need Fil-C and are skipped without it) |
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

Useful environment variables:

```sh
GAMMA_TEST_FILTER=parse/match cargo test --test conformance   # run a subset
BLESS=1 cargo test --test conformance                         # rewrite golden files
```

Only bless after checking that the new output is correct; `git diff` shows what
changed.

Every `run` test that expects no errors also runs through the C back end and
must produce the same output and exit status as with the interpreter. This
needs a C compiler (`$CC` or `cc`); set `GAMMA_TEST_C=0` to skip it, and
`GAMMA_TEST_DEBUG_INFO=1` to build every such program with `-g` (worth doing after
changing `src/cgen/debug.rs` or `src/cgen/views.rs`). The C
runtime has its own tests (`cargo test --test runtime`, in `runtime/rt_test.c`).

The interpreter has two engines (see `docs/architecture.md`), and the tests use
the bytecode VM. CI also runs the conformance tests on the tree-walker; to do that
locally, set `GAMMA_INTERP=tree`:

```sh
GAMMA_INTERP=tree GAMMA_TEST_C=0 cargo test --test conformance
```

When the two disagree, these help:

- `GAMMA_VM_DUMP=name` (or `*`) prints the bytecode of each function of that name
  as it is compiled, or why it runs on the tree-walker instead.
- `cargo build --release --features vm-profile` builds a `gamma` that, with
  `GAMMA_VM_PROFILE=1`, prints how often each opcode ran when `gamma run` exits;
  with `GAMMA_VM_PAIRS=1` as well it lists the most frequent pairs of consecutive
  opcodes, which is where to look for instructions worth fusing.

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

When adding a feature, add conformance tests that follow the specification
section by section: valid programs that exercise each rule, and invalid programs
that check each compile error the section requires.

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

## Releases

A release is a tag `vX.Y.Z` on `main` whose version matches `version` in `Cargo.toml`.
Pushing the tag runs `.github/workflows/release.yml`, which:

1. builds `gamma` in release mode on Linux (x86_64 and arm64, on Ubuntu 22.04 so that the
   binaries run with older glibc versions) and macOS (x86_64 and arm64);
2. packages each with `tools/package-release.sh` as `gamma-<os>-<arch>.tar.gz` plus a
   `.sha256` file. The archive is an installation prefix: `bin/gamma`, the standard library
   in `lib/gamma/std` (an installed compiler finds it relative to its own path), `LICENSE`
   and `README.md`;
3. unpacks each archive, hides the source tree's `lib/std`, and checks that the packaged
   compiler runs a program that imports `std/...`, on the interpreter and through C;
4. creates the GitHub release with the archives, `SHA256SUMS` and `tools/install.sh`.

To cut a release: bump `version` in `Cargo.toml` (and `Cargo.lock` with `cargo build`),
merge that, then `git tag vX.Y.Z && git push origin vX.Y.Z`. Running the workflow by hand
(workflow dispatch) builds and checks the archives without publishing anything, which is a
good test after changing the workflow or the packaging script.

`tools/install.sh` downloads the archive for the machine it runs on, checks its checksum and
installs it under `$GAMMA_INSTALL` (default `~/.gamma`). `GAMMA_VERSION` picks a release and
`GAMMA_DOWNLOAD` a different download directory; `GAMMA_DOWNLOAD=file:///path/to/dist` tests
the script against archives built locally with `tools/package-release.sh`.

## The specification

`gamma-spec.md` is the definition of the language. If an implementation question
reveals a contradiction, gap or impractical rule in the specification, fix the
specification in the same change (keeping it self-consistent and updating its
examples), rather than letting the implementation silently diverge.

## Code style

- Follow the existing structure described in [`architecture.md`](architecture.md).
- Public items get doc comments; other comments explain *why*, not *what*.
- Diagnostics are lowercase, name the construct involved, and suggest a fix when
  there is an obvious one.
- No `unwrap()` on user-controlled input: report a diagnostic instead.
