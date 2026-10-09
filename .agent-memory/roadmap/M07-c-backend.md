# M7 — Native code via C
TIR → C11 (+ GNU extensions where needed) → system cc. Runtime in `runtime/` (C). Evidence
passing (App. D.1), exit flags (D.2), static specialization where handler known (D.3),
checks per build mode (Debug/Checked/Fast), traps with locations, integers of any width
(native for ≤128 bits, runtime routines above), f16 via _Float16 or soft conversions.
`gamma build`, `gamma run --backend=c`. Differential testing: every run test executes on both
interpreter and C backend.

## Status (design: decisions/008)
Done: src/cgen/{mod,types,func,expr,arith,helpers,fiber}.rs + runtime/gm_rt.{h,c} (+ rt_test.c,
tests/runtime.rs). Every run test runs on both backends (GAMMA_TEST_C=0 skips); ASan/UBSan clean;
gcc and clang (macOS CI = clang). Fibers, foreign fns, #global, #bits all done.
`gamma build [-o]`, `gamma run --backend=c`, `--mode=debug|checked|fast` (Options{checks,debug}).
Elementwise array ops done (sema finalize Numeric peels arrays; cgen/elementwise.rs loops).
Wide ints (>128 bits) done: runtime gm_w* routines (LE two's complement, ext. on write), cgen/wide.rs;
std/fmt hex/bin of wide ints still traps (library). Stack check disabled under ASan (GM_ASAN).
Not yet: static handler specialization (D.3),
`gamma test` native. Main/thread stack overflow traps in checked builds (gm_stack_limit TLS,
gm_thread_stack(), 256 KiB reserve; fast mode segfaults = UB).
Gotchas: Const is a place (static gm_cN) — `&CONST` must not copy; pattern types needed
finalizing (sema fix); defers run by a ladder must save/clear gm_unw; `(T){}` not `(T){0}`.
