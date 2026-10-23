# The LLVM back-end experiment

Gamma has three ways to make native code: the C back end (any C compiler; the default for
`gamma build`), the Cranelift back end (`--backend=cranelift`, fast to compile) and the
experimental LLVM back end (`--backend=llvm`) described here. The LLVM back end exists to
answer one question: **is LLVM's optimizer worth the baggage of an LLVM back end?** It is a
measuring stick, not a supported back end.

## What the LLVM back end is

`src/llvm/` (about 1,400 lines with its C glue) lowers TIR to *textual* LLVM IR. `clang -x ir -O<n>` compiles
the IR to an object, which is linked with the C runtime (`runtime/gm_rt.c` plus the glue in
`runtime/gm_llvm.c`, compiled once and cached like the Cranelift back end's runtime). Writing
text rather than calling the LLVM C API means the compiler links no LLVM library and gains no
Rust dependency: the only requirement is a `clang` at build time.

It translates a subset of the language and reports anything else as "not supported by the
LLVM back end":

- integers of 8 to 64 bits, `bool`, `f32`, `f64`, fixed-size arrays of them (nested too) and
  pointers to those;
- top-level functions and direct calls; `if`, `match` on scalars (constants, ranges,
  alternatives, guards), `while`, `for` over ranges, `break`, `continue`, `return`, `trap`;
- all integer operators with the plain, wrapping (`+%`) and saturating (`+|`, `-|`) flavors,
  conversions between the supported scalars, `min`, `max`, `abs`, `sqrt`, `@black_box`;
- `print` of scalars and string literals to the ambient `Out`.

No structs, slices, strings as values, optionals, results, effects, handlers or allocation.
Checks follow the spec as in the other back ends: overflow, bounds and shift checks trap
with the same message and location in Debug, Checked and Safe builds and are removed in Fast
builds; division by zero and signed division overflow always trap; running out of stack
traps. A Debug build's trap report has no call trace.

```sh
gamma run --backend=llvm prog.gm               # optimizes at -O2
GAMMA_LLVM_OPT=0 gamma build --backend=llvm prog.gm
GAMMA_DUMP_LLVM=1 gamma build --backend=llvm prog.gm 2> prog.ll
GAMMA_LLVM_CLANG=clang-18 gamma run --backend=llvm prog.gm
```

`tests/llvm.rs` checks that a program using every supported construct prints what the
interpreter prints in every build mode at `-O0` and `-O2`, and that failed checks report
exactly what the interpreter reports.

## Method

`bench/backends/` holds six programs written within the subset, so every back end compiles
the same source:

| Program | What it stresses |
|---|---|
| `collatz` | checked integer arithmetic, division, unpredictable branches |
| `fib` | recursive calls (naive `fib(38)`) |
| `hash` | a latency-bound chain of 64-bit multiplies, shifts and xors |
| `mandel` | float arithmetic in a tight loop |
| `matmul` | nested loops over arrays behind bounds checks, a call per product |
| `sieve` | array stores and loads behind bounds checks |

`scripts/bench-backends.py` builds each program with each back end, times the build (with the
runtime object already cached, as in everyday use) and the fastest of five runs, and checks
that every back end prints the same checksum. They all did.

```sh
cargo build --release
python3 scripts/bench-backends.py --runs=5 --all-levels
```

The numbers below are from a 4-core x86-64 Linux container (gcc 13.3, clang 18.1) on a shared
host: differences under about 10% are noise.

## Results

Run time in seconds, Checked builds (the checks on, as most programs ship):

| program | c (gcc) | c (clang) | cranelift | llvm -O0 | llvm -O1 | llvm -O2 | llvm -O3 |
|---|---:|---:|---:|---:|---:|---:|---:|
| collatz | 0.807 | 0.870 | 1.235 | 2.043 | 0.885 | 0.884 | 0.878 |
| fib | 0.112 | 0.191 | 0.439 | 0.283 | 0.187 | 0.190 | 0.186 |
| hash | 0.441 | 0.436 | 0.465 | 0.452 | 0.434 | 0.429 | 0.421 |
| mandel | 0.453 | 0.457 | 0.524 | 1.084 | 0.480 | 0.454 | 0.461 |
| matmul | 0.169 | 0.163 | 0.786 | 1.444 | 0.165 | 0.170 | 0.159 |
| sieve | 0.280 | 0.263 | 0.415 | 0.734 | 0.279 | 0.253 | 0.257 |

Fast builds (the removable checks removed):

| program | c (gcc) | c (clang) | cranelift | llvm -O0 | llvm -O1 | llvm -O2 | llvm -O3 |
|---|---:|---:|---:|---:|---:|---:|---:|
| collatz | 0.664 | 0.519 | 1.183 | 1.703 | 0.527 | 0.532 | 0.525 |
| fib | 0.002 | 0.098 | 0.342 | 0.205 | 0.109 | 0.109 | 0.109 |
| hash | 0.446 | 0.427 | 0.468 | 0.450 | 0.439 | 0.428 | 0.430 |
| mandel | 0.460 | 0.456 | 0.533 | 1.061 | 0.471 | 0.459 | 0.462 |
| matmul | 1.310 | 0.170 | 0.661 | 0.804 | 0.170 | 0.183 | 0.159 |
| sieve | 0.287 | 0.173 | 0.414 | 0.658 | 0.171 | 0.147 | 0.146 |

Build time of these small programs: C back end 0.3–0.8 s, Cranelift about 0.05 s, LLVM
0.07–0.11 s at any level. The LLVM figure flatters it: it compiles only the program's own
functions (the subset uses nothing from the core library), and most of it is starting
`clang`; Cranelift compiles more and is still faster.

## What the numbers say

**LLVM's optimizer pays off against Cranelift, a lot in places.** LLVM `-O2` over Cranelift:

| | collatz | fib | hash | mandel | matmul | sieve |
|---|---:|---:|---:|---:|---:|---:|
| Checked | 1.4× | 2.3× | 1.1× | 1.2× | 4.6× | 1.6× |
| Fast | 2.2× | 3.1× | 1.1× | 1.2× | 3.6× | 2.8× |

Code that is one long dependency chain (`hash`) or mostly float arithmetic (`mandel`) gains
little: there is nothing to remove, and both back ends schedule it about equally well. Loops
over arrays and small calls gain the most.

**The gains come from a few classic optimizations, and `-O1` already gets all of them.**
`-O3` is never better than `-O2`, and `-O1` is within noise of both. In `matmul` the optimized
module has `mul` inlined into `main`, and all eight bounds checks and all ten overflow checks
are gone, proved redundant by the loop ranges. That is the check hoisting the spec asks
implementations for ("`for i in 0..a.len { a[i] }` is check-free in Checked builds"). Cranelift
does not do it, which is why Cranelift's Checked `matmul` is 19% slower than its Fast one.
With the checks off, LLVM is still 3.6× faster. That remaining gap is ordinary scalar
optimization: inlining, keeping locals in registers, loop-invariant code motion and strength
reduction of `i * N + k`. Unoptimized LLVM (`-O0`, every local in memory) is slower than
Cranelift everywhere.

**Gamma already has LLVM's optimizer: the C back end compiled by clang.** LLVM `-O2` and
`c (clang)` are within noise on every program, in both modes. Run-time performance would not
improve by adding an LLVM back end. The C back end's quality does depend on the C compiler,
though: gcc is 8× slower than clang on Fast `matmul`, yet finishes Fast `fib` in 2 ms (reasons
not investigated here).

## The baggage

What a production LLVM back end would cost, beyond this experiment:

- **Linking LLVM** (through `llvm-sys` or `inkwell`): `libLLVM` is a 123 MB shared library
  here (the whole `gamma` binary, Cranelift included, is 18 MB). Building `gamma` would need
  the LLVM development packages, and each binding crate version is pinned to one LLVM major
  version. Distribution on Windows and macOS gets harder.
- **Textual IR, as here:** no build-time cost, but `clang` becomes a run-time requirement (the
  C back end needs a C compiler anyway). The IR syntax changes between LLVM versions (opaque
  pointers since LLVM 15, renamed intrinsics), so the back end would have to track it.
- **Either way:** a fourth implementation of the whole language. The C back end needed the
  runtime protocol, effects and handlers, fibers, the C ABI, debug information and so on; this
  subset covers none of them. The Cranelift back end took about 9,300 lines to reach the full
  conformance suite.

## Conclusion

A full LLVM back end is not worth the baggage. Its run-time advantage over Cranelift is real
(1.1–4.6× here), but the C back end with clang already delivers the same code. Its likely
advantage over the C back end is compile time, which Cranelift already covers better.

What the experiment does show is where Cranelift falls short and how cheaply that could be
fixed. The checks in range loops and the calls to small functions cost the most, and both can
be addressed above any code generator: by eliding checks that a loop's range proves, and by
inlining small plain functions in TIR. That would help Cranelift and the bytecode VM alike.
The LLVM back end stays as a yardstick for such work: `GAMMA_LLVM_OPT=1` shows what a
mid-end could reach on a given program.
