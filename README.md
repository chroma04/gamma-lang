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

## Status

This repository contains the reference implementation, written in Rust. It is
under active construction:

| Component | Status |
| --- | --- |
| Lexer (spec §2) | implemented |
| Parser (spec Appendix A) | implemented |
| Semantic analysis | the language: effects, handlers and fibers, generics with compile-time parameters, compile-time evaluation and `when`, modules, imports, tests, and C interop |
| Interpreter / compile-time evaluator | implemented, except calls to foreign code (`gamma run` compiles such programs instead) |
| Native code generation (via C) | the language, including fibers and foreign functions (`gamma build`, `gamma run --backend=c`) |
| Standard library | core library (output, environment, iterators, allocation, `List`, `HashMap`, files, network); `std/io`, `std/fs`, `std/fmt`, `std/str`, `std/math`, `std/arena`, `std/fixed_buffer`, `std/thread`, `std/c` |

## Building

You need a recent stable Rust toolchain.

```sh
cargo build --release
./target/release/gamma help
```

Available commands today:

```sh
gamma run file.gm            # type-check and run a program (a file or a module directory)
gamma run --backend=c file.gm  # compile through C and run (default backend: interp)
gamma build file.gm -o prog  # compile to a native executable via the system C compiler
gamma build -g file.gm       # the same, with debug information for gdb/lldb at .gm lines
gamma check file.gm          # type-check only
gamma test file.gm           # run the `test` declarations of a module
gamma lex file.gm            # print the token stream
gamma parse file.gm          # check syntax
gamma parse --dump file.gm   # print the syntax tree
gamma fmt file.gm            # format source files in place (files or directories)
gamma fmt --check dir        # list the files that are not formatted; exit 1 if any
```

`run`, `build` and `test` also take `--mode=debug|checked|fast` (default `debug`).

## Testing

```sh
cargo test
```

runs the unit tests and the conformance suite in `tests/conformance/`. See
[`docs/contributing.md`](docs/contributing.md) for the test file format.

## License

MIT — see [`LICENSE`](LICENSE).
