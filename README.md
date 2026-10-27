# Gamma

Gamma is a systems programming language intended to replace C. It is **C in which
everything a function depends on appears in its signature**: its inputs in the parameter
list, its failures in `E!T`, its absences in `?T`, and its *context* (the allocator,
input and output, the clock, randomness, its scheduler, the outside world) in its effect
set after `needs`. **Algebraic effects and handlers** supply that context, so it is explicit
without being passed by hand, and tests can replace it.

```gamma
fs    :: import "std/fs"
arena :: import "std/arena"

// Counts the lines of a file. The signature says everything it touches.
count_lines :: fn(path: string) -> fs.ReadError!usize needs Fs + Alloc {
    text := try fs.read_all(path)          // memory from whatever allocator is current
    defer free(text)
    n: usize = 0
    for b in bytes(text) { if b is '\n' { n += 1 } }
    return n
}

main :: fn() -> fs.ReadError!() needs Fs + Alloc + Out {
    scratch := arena.new()                 // you choose where memory comes from
    defer scratch.deinit()
    with scratch.allocator() {
        print("{} lines\n", try count_lines("README.md"))
    }
}

test "counts lines" {                      // tests get an in-memory file system
    ok(fs.write_all("notes.txt", "one\ntwo\n"))
    expect_eq(ok(count_lines("notes.txt")), 2)
}
```

From C it keeps what matters: data laid out as in C, values that copy as bytes, explicit
memory management with `defer`, no garbage collector and no hidden allocation. From
modern languages it takes tagged unions with exhaustive `match`, optionals instead of
null, results instead of `errno`, generics, modules and a package manager. And effects
do the jobs C leaves to globals, callbacks and `setjmp`: allocation, I/O, iteration,
early exit, coroutines, testing.

- [The website](https://chroma04.github.io/gamma-lang/) — the guide, the specification and
  the library reference as web pages, and a [playground](https://chroma04.github.io/gamma-lang/playground.html)
  that runs programs in your browser.
- [`gamma-spec.md`](gamma-spec.md) — the complete language specification.
- [`docs/guide/`](docs/guide/README.md) — a tutorial introduction to the language.
  Coming from another language? Read [Gamma for C programmers](docs/guide/for-c-programmers.md)
  or [Gamma for Rust programmers](docs/guide/for-rust-programmers.md).
- [`docs/std.md`](docs/std.md) — the standard library.
- [`docs/architecture.md`](docs/architecture.md) — how the retired Rust implementation is organized.
- [`docs/contributing.md`](docs/contributing.md) — building, testing and contributing.
- [`docs/self-hosting.md`](docs/self-hosting.md) — the compiler is written in Gamma: how
  it was built, and how it builds itself.
- [`CHANGELOG.md`](CHANGELOG.md) and [`docs/versioning.md`](docs/versioning.md) — what
  changed in each release, and what stays stable between releases.

## Status

The compiler, `gamma`, is written in Gamma (`compiler/`) and builds itself; the
whole specification is implemented. A program runs on one of two back ends that must
agree on every test: a C code generator (the default for `gamma build`, `gamma run`,
`gamma test` and `gamma bench`) and an interpreter (`gamma run --backend=interp`, and
`gamma repl`). The repository still holds the Rust implementation the compiler was
first written against (`src/`), frozen and no longer built or tested; it is kept until
it is deleted. Its last state is the tag `rust-reference-final`.

| Component | Status |
| --- | --- |
| Lexer and parser (spec §2, Appendix A) | implemented; `gamma fmt` formats code |
| Semantic analysis | the whole language: effects, handlers and fibers, generics with compile-time and effect-set parameters, compile-time evaluation and `when`, modules, packages, tests, and C interop |
| Interpreter / compile-time evaluator | implemented: a tree-walking interpreter written in Gamma; it needs no C compiler, but a program that calls foreign C code or uses inline assembly runs only through C |
| Native code generation (through C) | the whole language; build modes Debug, Checked, Fast and Safe (memory-safe through [Fil-C](https://fil-c.org), Linux only); debug information with gdb views (`-g`); cross-compiling (`--target=`, tested on aarch64 and big-endian s390x); WebAssembly for WASI (`--target=wasm32-wasi`, run with Wasmtime) |
| Platforms | Linux and macOS (x86_64 and arm64), and x86_64 Windows with a MinGW-w64 C compiler (gcc or clang; MSVC's `cl` is not supported) |
| Standard library | the core library (output, environment, iterators, allocation, `List`, files, network) and 39 modules (see [`docs/std/`](docs/std/index.md)): `std/io`, `std/fs`, `std/fmt`, `std/str`, `std/text`, `std/ascii`, `std/bytes`, `std/encoding/base64`, `std/encoding/hex`, `std/hash` (with `crc32` and `sha256`), `std/json`, `std/math`, `std/complex`, `std/random`, `std/uuid`, `std/time`, `std/path`, `std/cli`, `std/process`, `std/log`, `std/net`, `std/collections` (hash_map, deque, heap, bitset, ordered_map, soa_list), `std/arena`, `std/fixed_buffer`, `std/fiber`, `std/thread` (with `wait_group`), `std/c` |
| Packages | `gamma.toml` manifests with path, git and archive dependencies, version requirements, a `gamma.lock` lockfile, build profiles with C sources and libraries, and a package cache (`gamma new`, `add`, `fetch`, `update`, `cache`) |
| Editors | a language server (`gamma lsp`), a VS Code extension, a Zed extension, an Emacs mode, and TextMate and tree-sitter grammars in [`editors/`](editors/) |

### Which back end?

`gamma run` compiles the program through C and runs it, as do `gamma build`, `gamma test`
and `gamma bench`, so a C compiler is needed. `gamma run --backend=interp` interprets the
program instead: there is no C compiler involved and it starts at once, which suits a
quick check, but it is slower (compute-heavy code runs far faster through C), and a
program that calls foreign C code or uses inline assembly cannot run on it. Use C, with
`-g`, to debug a program with gdb or lldb, and `--mode=safe` to check it under Fil-C.
Both back ends give the same output; a program that differs between them is a bug.

## Installing

Prebuilt binaries for Linux and macOS (x86_64 and arm64) and Windows (x86_64) are
attached to each [release](https://github.com/chroma04/gamma-lang/releases):

```sh
curl -fsSL https://github.com/chroma04/gamma-lang/releases/latest/download/install.sh | sh
```

installs `gamma` and its standard library in `~/.gamma` (`GAMMA_INSTALL` changes that). On
Windows, run this in PowerShell:

```powershell
irm https://github.com/chroma04/gamma-lang/releases/latest/download/install.ps1 | iex
```

which installs in `%USERPROFILE%\.gamma` and adds its `bin` directory to your `PATH`.
`build`, `run` (by default), `test` and `bench` also need a C compiler (`cc`, gcc or
clang); on Windows it must be a MinGW-w64 compiler on `PATH` (for example gcc from
[MSYS2](https://www.msys2.org)'s UCRT64 environment, or an
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) clang). `gamma run --backend=interp`
needs none.

## Building from source

You need only a C compiler (`cc`, gcc or clang) and nothing else: the repository holds the
compiler as C, generated by an earlier version of itself (`bootstrap/seed/`).

```sh
bootstrap/build.sh
target/bootstrap/gamma help
```

compiles the seed and builds the current compiler with it, twice, so that the result is
the compiler as generated by itself (a few minutes). After changing the compiler, rebuild
it with the one you have:

```sh
GAMMA_STD=$PWD/lib/std GAMMA_RUNTIME=$PWD/runtime target/bootstrap/gamma build compiler -o <out>
```

See [`bootstrap/README.md`](bootstrap/README.md).

## Using it

```sh
gamma new hello              # create a package: gamma.toml, main.gm with a test, .gitignore (and a git repository)
gamma run hello              # compile through C and run a program (a file or a module directory)
gamma test hello             # run the `test` declarations of a module and its imports
gamma run --backend=interp file.gm  # interpret instead of compiling (no C compiler needed)
gamma build file.gm -o prog  # compile to a native executable via the system C compiler
gamma build -g file.gm       # the same, with debug information for gdb/lldb at .gm lines
gamma check file.gm          # type-check only
gamma fix file.gm            # apply the fixes the compiler suggests (`&&` → `and`, `_ = f()`, …)
gamma lint hello             # warn about unused code, suspicious comparisons and simpler forms (--fix applies fixes)
gamma test --filter=parse file.gm  # only the tests whose name contains `parse`
gamma test --all --jobs=4 --coverage hello  # every module's tests, 4 at a time, with a line coverage table
gamma bench hello            # run the `bench` declarations and report the time per run
gamma add json --git https://github.com/someone/json-gm  # add a dependency
gamma fetch                  # download the dependencies, writing gamma.lock
gamma update                 # move the dependencies to their newest allowed versions
gamma remove json             # remove a dependency
gamma cache clean --older-than=30  # drop unused fetched packages and native builds
gamma fmt file.gm            # format source files in place (files or directories)
gamma fmt --check dir        # list the files that are not formatted; exit 1 if any
gamma doc dir                # API documentation of a module, as Markdown (--format=html, -o, --all)
gamma repl                   # an interactive read-eval-print loop (see the getting-started chapter)
gamma lsp                    # the language server, for editors
gamma lex file.gm            # print the token stream
gamma parse --dump file.gm   # print the syntax tree
```

`run`, `build`, `test` and `bench` also take `--mode=debug|checked|safe|fast` (default `debug`; `bench` defaults to `fast`) and `--profile=<name>` (a `[profile.<name>]` of `gamma.toml`). `gamma help` lists every option; `gamma help packages` and `gamma help doc` list those of the package commands and of `doc`. Safe builds check every memory access and need [Fil-C](https://fil-c.org) on Linux (see the [safety chapter](docs/guide/13-safety.md)).

## Testing

```sh
tools/test.sh
```

builds `target/bootstrap/gamma` (from the seed the first time) and runs the test suites
with it: the conformance suite in `tests/conformance/`, the examples, the compiler's own
tests and more. `tools/test.sh --test conformance` runs one suite. The suites are a cargo
test harness, so Rust is needed to run them. See [`docs/contributing.md`](docs/contributing.md)
for the test file format.

## License

MIT — see [`LICENSE`](LICENSE).
