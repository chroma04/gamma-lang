# The Cranelift back end

The `gamma` tool has three ways to run a program: the interpreter (the default for
`gamma run` and `gamma test`), the C back end, which translates the program to C and
compiles it with the system C compiler (the default for `gamma build` and `gamma bench`),
and the **Cranelift back end**, which generates machine code directly with
[Cranelift](https://cranelift.dev). Choose it with `--backend=cranelift` on `run`, `build`,
`test` and `bench`:

```sh
gamma run --backend=cranelift main.gm
gamma build --backend=cranelift main.gm -o main
gamma test --backend=cranelift src/
```

All three give the same results: the same output, the same trap messages and locations,
the same exit status. A program that behaves differently on one of them is a bug in the
compiler. The whole conformance suite runs through each back end.

## When to use it

Cranelift is a code generator built for compile speed. Compared with the C back end:

- **Builds are much faster**, typically 4 to 10 times. Most of what remains is the
  link. The difference is largest for Checked and Fast builds, which the C back end
  optimizes fully.
- **Programs run slower**, typically 1.5 to 3 times, and more for code dominated by
  calls of small functions, because Cranelift does not inline. Numeric loops come close
  to the C back end.

So it suits the edit-compile-run loop of a program too slow to interpret, test suites that
call C, and generated or very large programs. Use the C back end for release builds,
benchmarks and debugging.

The build modes (`--mode=debug|checked|fast`) work as on the C back end: Debug and Checked
builds keep every check, and Fast builds remove them (§21). Safe builds (`--mode=safe`)
need Fil-C, which compiles C, so they use the C back end only.

## Where it runs

The back end generates code for 64-bit Linux and macOS, on x86-64 and AArch64 (Apple
silicon included). It still needs the C toolchain, for two things: it compiles the C
runtime that every Gamma program links (once, then cached in the cache directory), and the
C compiler links the executable. `CC`, `CFLAGS`, `LDFLAGS`, `--target` and `GAMMA_RUNNER`
work as for the C back end ([chapter 1](01-getting-started.md)), so a cross compiler for a
supported target and an emulator run programs for another architecture:

```sh
CC=aarch64-linux-gnu-gcc CFLAGS=-static GAMMA_RUNNER="qemu-aarch64 -L /usr/aarch64-linux-gnu" \
    gamma run --backend=cranelift main.gm
```

Windows, 32-bit targets, big-endian targets and freestanding programs need the C back
end.

## C interoperability

Everything in [chapter 14](14-interop.md) works:

- Foreign calls follow the platform's C calling convention, including structs, tuples,
  arrays and `#c_union`s passed and returned by value (the System V rules on x86-64, the
  AArch64 procedure call standard and Apple's variant of it), `#win64` functions, and
  variadic functions with any mix of integer, pointer and floating-point arguments.
- `#export` functions can be called from C with the same convention, structs by value
  included.
- The C sources and libraries of a package's `[build]` settings are compiled and linked as
  with the C back end ([chapter 15](15-packages.md#build-settings)).
- `@asm` works: each use is compiled by the C compiler into a small function of its own,
  which the generated code calls with the operands' addresses. The template, constraints
  and clobbers mean what they mean to GCC and Clang. Because the statement is not inlined
  into the surrounding function, a template must work only through its operands and
  clobbers: it cannot rely on the values other registers or the stack pointer had in
  the calling function.

## What it does not do yet

- **Debug information.** `-g` needs the C back end; with `--backend=cranelift` it is
  an error that says so.
- **A C function calling a Gamma function pointer with a struct by value.** A Gamma
  function passed to C as a callback works when its parameters and result are scalars or
  pointers (`qsort` comparators, for example). One whose parameters or result are structs
  passed by value is rejected with an error at the call that passes it to C.
- **`f16` across the C boundary**, and structs that contain one.
- **Coverage.** `gamma test --coverage` runs in the interpreter.

A construct the back end cannot compile is reported as an error that begins "not supported
by the Cranelift back end yet" and points at the source; the C back end compiles the same
program.

## How it works

The [compiler architecture](../architecture.md#the-cranelift-back-end) describes the
design: how values are represented, the calling conventions, effects and fibers, and how
the generated code shares the C back end's runtime.
