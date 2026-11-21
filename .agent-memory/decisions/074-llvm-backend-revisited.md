# 074 — LLVM back end and an LLVM JIT, revisited (s79): stay on C; CURRENT

User ask: "revisit LLVM as a backend … better IR without going through C semantics …
might help the interpreter (a major effort sink), some kind of JIT". Investigation only;
no back-end code changed. Supersedes the "Why C not LLVM" line of 002 with numbers.

## Verdict
- No direct LLVM IR back end now. Clang already turns our C into the IR we would emit;
  what direct IR adds is ~0–5% run time (unmeasured upper bound) and 0–45% of clang's
  compile time, for clang users only, at the price of a second native code generator,
  since the C back end must stay (seed bootstrap, gcc, MinGW, Fil-C Safe, s390x).
- No LLVM JIT for the interpreter: it would add work, not remove it (see below).
- Cheap wins inside the C path: done in s79, see "Follow-up (s79)" at the end.
- If the interpreter's cost is the real problem, the structural fix is a shared lowered
  IR under TIR (below), and an LLVM emitter becomes cheap only after that.

## Measurements (x86-64, 4 cores, clang 18.1.3, gcc 13.3; min of 5–7 runs)
Scratch kernel `k.gm` (slice sum, struct dot product, small-struct add, deref):
- Fast mode, clang -O2: sum is vectorized (2×<2 x i64>, unrolled), bounds checks gone,
  IPSCCP specialized the length. Nothing left for direct IR to win.
- Checked mode: overflow → `llvm.sadd/smul.with.overflow` + branch to a noreturn
  `gm_trap` per site; the bounds check of `for i in 0..xs.len { xs[i] }` is removed by
  clang. No vectorization because traps are precise (§18.1/§18.5): a semantic limit,
  the same in any back end.
- Blob loads via memcpy come out `align 1` (harmless on x86/aarch64).
Clang front-end share (largest seed units, 1.18 MB and 0.82 MB of C):
- -fsyntax-only 0.23 / 0.17 s; C → .ll at -O0 0.57 / 0.25 s.
- -O2: full 1.77 / 1.19 s vs .ll → .o 1.82 / 0.80 s (≈ 0–33% saved).
- -O0: full 0.93 / 0.40 s vs .ll → .o 0.76 / 0.22 s (18–45% saved).
- gcc -O2: 3.04 / 2.06 s, i.e. 1.7× clang -O2.
gcc vs clang building the compiler (Checked, -O2, cold cache): build 46 s vs 32 s;
binary 9.3 vs 7.6 MB; `check compiler` 708 vs 547 ms (−23%); interp loop 6.70 vs 6.48 s.
bench/programs + nbody/mandelbrot (Checked): mixed, ±15% each way (clang ahead on
map_insert, new_destroy, with, nbody; gcc ahead on perform, loop.gm collatz+fib).
ThinLTO (seed units, clang -O2 -flto=thin, lld): compile 18 s + link 21 s vs 32 s;
`check compiler/fmt` callgrind 309.1 → 283.3 M Ir (−8%); wall −4%, interp loop ±noise.
This bounds what one whole-program LLVM module would give over split C units.
Exit flag (D.2) as a register: clang `swiftcall` + `swift_context`/`swift_error_result`
(r12) vs the TLS record, recursive fib(40) with a test after each call: 552 vs 534 ms.
No win, as with the two-register return in architecture.md. Noisy machine.
Interpreter vs native: collatz 3M + fib(32): Checked native 0.85 s, interp 7.3 s (8.6×).
Small program `gamma run`, warm runtime cache: native 60–80 ms, interp 14 ms.
Compiler profile (`check compiler/fmt`) is flat: regrow_raw 5%, memcpy 3.7%; no C-shaped
overhead (exit tests, stack checks) stands out.

## What direct IR could say that our C does not (and whether C can)
Sound for Gamma and expressible in GNU C/clang C, but not emitted by cgen today:
- `^T` never null → `nonnull`/`returns_nonnull`, `__builtin_assume(p)`.
- (WRONG, see follow-up) `gm_out` → `restrict`.
- in-place large params (borrow.gm proves no writer) → `const T *restrict`.
- borrow.gm "pure" functions → `__attribute__((pure))`.
- union tag switches → `default: __builtin_unreachable()`; odd-width ints → assume range.
- trap calls → `preserve_most`/`cold` (only `#cold` hints emit `cold` today).
LLVM-only, but not worth it:
- `dereferenceable`: UNSOUND, a `^T` may dangle while unused (§18.4 is UB on access only).
- TBAA: §18.4.2 makes wrong-type access UB, but "compatible type" is undefined in the
  spec, Safe builds define punning as reinterpretation, and the interpreter treats memory
  as bytes: TBAA would make native and interp diverge on programs the tests accept.
  Open spec ambiguity: define "compatible type" in §18.4.2 (not done; needs design).
- tailcc/guaranteed tail calls: only matter if Gamma gets `become`; clang `musttail`
  and `preserve_none` (19+) already reach C.
- Real DWARF (DW_TAG_variant_part for unions, no `#line` per line, no views.gm/gdb/lldb
  scripts): the best LLVM-only argument; still not worth a back end alone (013).
- llvm.coro (stackless): §14/D.4 fibers are stackful with `@stack_size`; no fit.
- Per-function pass control: only `optnone` either way (GM_LONG_FN); chain.gm's clang
  motivation (scope cleanups) is front-end, would vanish, but cgen keeps it for gcc.
Cost side: textual IR drifts (opaque ptrs ≥15, `memory()` ≥16, `captures()` ≥21: probe
the version), bitcode is stable but a big writer; a third back end in difftest/conformance.

## Interpreter and JIT
- Who runs the interpreter: playground (wasm32: no JIT possible), repl, no-toolchain
  users, `test --backend=interp`. With a C compiler, `run` is already native and warm
  runs cost 60–80 ms. An ORC JIT needs libLLVM (~100 MB, version-pinned): a toolchain
  dependency for exactly the users who have no toolchain. Comptime uses eval/, not interp.
- A JIT must keep interp semantics (page memory, provenance traps, fibers-as-threads with
  preemption every 2000 calls, Flow exits): a third full code generator plus frame
  interop with the VM. Expected 2–4× over the VM, which is already 8.6× of native.
- Copy-and-patch (stencils compiled by clang at build time from the Bc arms, no LLVM at
  run time) and tail-call dispatch (`musttail`+`preserve_none`, needs a Gamma `become`)
  attack only dispatch (~19 Ir/instr, 064). Same order as superinstructions; do those first.
- The effort sink is that four executors re-implement TIR semantics: eval/ (2.1k lines),
  the tree evaluator, vm_compile+vm (4k), cgen (11.6k), each with its own analyses
  (borrow, Observed, call_depth, clause_may_exit, address-taken registers).
  Structural option, only if asked: a lowered IR under TIR (CFG of typed virtual
  registers; exits, defer ladders, `with`, evidence and patterns explicit; D.7 check
  hoisting done once, which the interpreter lacks today). vm_compile would shrink most,
  cgen would emit from it, and an LLVM text emitter (and a JIT on it) would be a few
  thousand lines rather than a back end. Order: VM first (the pain), then cgen.

## Scratch (rebuild if needed)
k.gm / loop.gm kernels; `clang $BASE_CFLAGS -O2 -S -emit-llvm -fno-inline` on emitted C;
exit/{none,tls,swift}.c micro-benchmarks; seed compiled with `-flto=thin` + `-fuse-ld=lld`.

## Follow-up (s79, same session): the cheap wins, implemented and measured
Interleaved best-of-12 wall (gcc → clang → clang+ThinLTO): `check compiler` 702 → 597 →
610 ms; `--emit-c compiler` 1582 → 1517 → 1457 ms. The earlier "−23%" was one noisy run.
- KEPT clang for Checked/Fast without CC (toolchain.gm OPTIMIZING_CC, load.optimizing_cc,
  Toolchain.host = chosen for host so no `-dM -E` probe). build.sh keeps `cc` for the
  seed: the seed's runtime predates the weak __muloti4 (below), so clang-compiling the
  seed failed to link on aarch64 CI (refresh the seed before switching build.sh).
  Runtime gm_muloti4 + weak __muloti4 (clang, not x86-64/Apple/Windows): clang lowers
  i128 mul-overflow to compiler-rt's __muloti4; Linux links libgcc. rt_test checks it. Debug keeps cc:
  clang -Og built json_query/sha256sum/life 13–50% slower (783→1061, 475→524, 259→387 ms).
- KEPT ThinLTO for split optimized builds (native.gm thin_lto/lto_link_flags; clang +
  ld.lld + host + cache; lld cache in <cache>/thinlto, 1 GiB, 1 week; `cache clean --all`
  removes it). Compiler: Ir check compiler/fmt 303.7 → 277.9 M (−8.5%); cold build 42 s
  (clang plain 32, gcc 46); no-op rebuild 3.2 s; one-file edit 3.5 s.
- KEPT `default: __builtin_unreachable()` in union tag switches (patterns.gm): interp
  map_insert Ir −1.0% (clang) / −3.6% (gcc), call −0.5% / −3.4%; check ±0.3%.
- DROPPED nonnull/returns_nonnull (^T params, by-ref params, gm_out) and restrict on
  borrowed by-ref params: no measurable change on top of the switch hint (check
  278.8 vs 278.8 M, interp equal), and nonnull would turn Fast-mode unspecified null
  pointers into C UB.
- DROPPED `restrict` on gm_out: UNSOUND. Callers pass the assigned place itself
  (`a[i] = f(&a)`); the callee may read a[i] through its parameter before writing.
- DROPPED `pure` (borrow.gm is_pure): clang makes pure functions willreturn and deletes
  unused calls; a Gamma pure function may trap or loop, which must still happen.
- DROPPED cold/preserve_most on trap calls: gm_trap* are _Noreturn, already cold paths.
