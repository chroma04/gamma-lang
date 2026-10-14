# Gamma

Gamma is a systems programming language intended to replace C: simple, fast and
sensible, with **algebraic effects and handlers** for allocation, I/O, iteration,
early exit and coroutines.

```gamma
main :: fn() \ {Out} {
    for i in 1..=3 {
        print("{} squared is {}\n", (i, i * i))
    }
}
```

- [`gamma-spec.md`](gamma-spec.md) — the complete language specification.
- [`docs/guide/`](docs/guide/README.md) — a tutorial introduction to the language.
- [`docs/std.md`](docs/std.md) — the standard library.
- [`docs/architecture.md`](docs/architecture.md) — how the compiler is organized.
- [`docs/contributing.md`](docs/contributing.md) — building, testing and contributing.
- [`CHANGELOG.md`](CHANGELOG.md) and [`docs/versioning.md`](docs/versioning.md) — what
  changed in each release, and what stays stable between releases.

## Status

This repository contains the reference implementation, written in Rust. The whole
specification is implemented, on two back ends that must agree on every test: an
interpreter (the default for `gamma run` and `gamma test`) and a C code generator
(`gamma build`, `--backend=c`).

| Component | Status |
| --- | --- |
| Lexer and parser (spec §2, Appendix A) | implemented; `gamma fmt` formats code |
| Semantic analysis | the whole language: effects, handlers and fibers, generics with compile-time and effect-set parameters, compile-time evaluation and `when`, modules, packages, tests, and C interop |
| Interpreter / compile-time evaluator | implemented: functions run as register bytecode, and foreign C functions are called through `dlsym` and libffi |
| Native code generation (via C) | the whole language; build modes Debug, Checked, Fast and Safe (memory-safe through [Fil-C](https://fil-c.org), Linux only); debug information with gdb views (`-g`); cross-compiling (`--target=`, tested on aarch64 and big-endian s390x) |
| Standard library | the core library (output, environment, iterators, allocation, `List`, `HashMap`, files, network) and 24 modules: `std/io`, `std/fs`, `std/fmt`, `std/str`, `std/text`, `std/ascii`, `std/bytes`, `std/encoding/base64`, `std/encoding/hex`, `std/math`, `std/random`, `std/time`, `std/path`, `std/cli`, `std/log`, `std/net`, `std/collections` (deque, heap, bitset), `std/arena`, `std/fixed_buffer`, `std/fiber`, `std/thread`, `std/c` |
| Packages | `gamma.toml` manifests with path, git and archive dependencies, version requirements, a `gamma.lock` lockfile, build profiles with C sources and libraries, and a package cache (`gamma new`, `add`, `fetch`, `update`, `cache`) |
| Editors | a language server (`gamma lsp`), a VS Code extension, and TextMate and tree-sitter grammars in [`editors/`](editors/) |

### Which back end?

`gamma run` and `gamma test` interpret the program: there is no C compiler involved, it
starts at once, and it is the quickest way to iterate. Use `--backend=c` (or `gamma build`)
when the program's speed matters (compute-heavy code often runs 10 to 30 times faster), when
it uses inline assembly, when you want to debug it with gdb or lldb (`-g`), or to check it
under `--mode=safe`. Both back ends give the same output; a program that differs between
them is a bug.

## Installing

Prebuilt binaries for Linux and macOS (x86_64 and arm64) are attached to each
[release](https://github.com/chroma04/gamma-lang/releases):

```sh
curl -fsSL https://github.com/chroma04/gamma-lang/releases/latest/download/install.sh | sh
```

installs `gamma` and its standard library in `~/.gamma` (`GAMMA_INSTALL` changes that).
Native builds also need a C compiler (`cc`, gcc or clang).

## Building from source

You need a recent stable Rust toolchain.

```sh
cargo build --release
./target/release/gamma help
```

## Using it

```sh
gamma new hello              # create a package: gamma.toml, main.gm with a test, .gitignore
gamma run hello              # type-check and run a program (a file or a module directory)
gamma test hello             # run the `test` declarations of a module and its imports
gamma run --backend=c file.gm  # compile through C and run
gamma build file.gm -o prog  # compile to a native executable via the system C compiler
gamma build -g file.gm       # the same, with debug information for gdb/lldb at .gm lines
gamma check file.gm          # type-check only
gamma test --filter=parse file.gm  # only the tests whose name contains `parse`
gamma add json --git https://github.com/someone/json-gm  # add a dependency
gamma fetch                  # download the dependencies, writing gamma.lock
gamma update                 # move the dependencies to their newest allowed versions
gamma fmt file.gm            # format source files in place (files or directories)
gamma fmt --check dir        # list the files that are not formatted; exit 1 if any
gamma doc dir                # API documentation of a module, as Markdown (--format=html, -o, --all)
gamma repl                   # an interactive read-eval-print loop (see the getting-started chapter)
gamma lsp                    # the language server, for editors
gamma lex file.gm            # print the token stream
gamma parse --dump file.gm   # print the syntax tree
```

`run`, `build` and `test` also take `--mode=debug|checked|safe|fast` (default `debug`). Safe builds check every memory access and need [Fil-C](https://fil-c.org) on Linux (see the [safety chapter](docs/guide/13-safety.md)).

## Testing

```sh
cargo test
```

runs the unit tests and the conformance suite in `tests/conformance/`. See
[`docs/contributing.md`](docs/contributing.md) for the test file format.

## License

MIT — see [`LICENSE`](LICENSE).
