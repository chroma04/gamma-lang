# Back ends: C and the interpreter

The `gamma` tool has two ways to run a program: the **C back end**, which translates the
program to C and compiles it with the system C compiler, and the **interpreter**, which
walks the checked program and needs no C compiler. `gamma build`, `gamma test` and
`gamma bench` always use C; `gamma run` uses C by default and the interpreter with
`--backend=interp`; `gamma repl` uses the interpreter.

```sh
gamma run main.gm                      # compile through C and run
gamma run --backend=interp main.gm     # interpret
gamma build main.gm -o main            # a native executable
gamma test src/                        # tests are compiled through C
```

Both give the same results: the same output, the same trap messages and locations, the
same exit status. A program that behaves differently on one of them is a bug in the
compiler. The whole conformance suite runs each `run` test through both.

## When to use which

- **The C back end** is the one for programs. Builds take as long as the C compiler takes
  (Checked and Fast builds are optimized fully, Debug builds favor turnaround, and large
  ones are compiled in parallel), and the code is as fast as C. Use it for tests,
  benchmarks, release builds, debugging with `-g`, and Safe builds. Later runs of the same
  program reuse the executable kept in the cache directory.
- **The interpreter** starts at once and needs no C compiler, which suits a quick check of
  a small program and machines without a toolchain. It is much slower than compiled code
  (compute-heavy programs run tens to hundreds of times slower), and it cannot run a
  program that calls foreign C code or uses inline assembly: those run only through C
  (`gamma run` without `--backend=interp` does that; with it, the program is refused).

The build modes (`--mode=debug|checked|fast`) work on both: Debug and Checked builds keep
every check, and Fast builds remove them (§21). Safe builds (`--mode=safe`) need Fil-C,
which compiles C, so they use the C back end only.

## Where the C back end runs

Linux and macOS on x86-64 and AArch64, and x86-64 Windows with MinGW-w64, as well as
cross targets (`--target`), freestanding programs and WebAssembly (`wasm32-wasi`). A C
compiler is needed on the machine that builds: it compiles the C runtime that every Gamma
program links (once, then cached in the cache directory) and the generated code. `CC`,
`CFLAGS`, `LDFLAGS`, `--target` and `GAMMA_RUNNER` are described in
[chapter 1](01-getting-started.md), so a cross compiler for a supported target and an
emulator run programs for another architecture:

```sh
CC=aarch64-linux-gnu-gcc CFLAGS=-static GAMMA_RUNNER="qemu-aarch64 -L /usr/aarch64-linux-gnu" \
    gamma run main.gm
```

## C interoperability

Everything in [chapter 14](14-interop.md) works through C:

- Foreign calls follow the platform's C calling convention, including structs, tuples,
  arrays and `#c_union`s passed and returned by value, `#win64` functions, and variadic
  functions with any mix of integer, pointer and floating-point arguments.
- `#export` functions can be called from C with the same convention, structs by value
  included.
- The C sources and libraries of a package's `[build]` settings are compiled and linked
  ([chapter 15](15-packages.md#build-settings)).
- `@asm` is passed to the C compiler, and its template, constraints and clobbers mean what
  they mean to GCC and Clang.

## Debugging

`gamma build -g` (and `gamma run -g`) adds debug information that maps the executable to
the Gamma source, so a debugger such as gdb or lldb stops at `.gm` lines and shows Gamma
locals ([chapter 1](01-getting-started.md)). The interpreter has no debug information; its
Debug mode reports the calls that led to a trap instead.

## How it works

The generated C is ordinary C: the same representation of values as the specification
describes, and the runtime in `runtime/gm_rt.c` for allocation, traps, fibers and
effects. `gamma build --emit-c` writes it instead of compiling it. See
[the architecture of the C back end](../architecture.md#the-c-back-end) for the design.
