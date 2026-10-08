# M7 — Native code via C
TIR → C11 (+ GNU extensions where needed) → system cc. Runtime in `runtime/` (C). Evidence
passing (App. D.1), exit flags (D.2), static specialization where handler known (D.3),
checks per build mode (Debug/Checked/Fast), traps with locations, integers of any width
(native for ≤128 bits, runtime routines above), f16 via _Float16 or soft conversions.
`gamma build`, `gamma run --backend=c`. Differential testing: every run test executes on both
interpreter and C backend.

## Status (design: decisions/008)
Done: src/cgen/{mod,types,func,expr,arith,helpers}.rs + runtime/gm_rt.{h,c} (+ rt_test.c,
tests/runtime.rs, delegated). All 22 run tests pass on both backends; harness runs every run
test via `--backend=c` (GAMMA_TEST_C=0 skips; probe cached). Generated C is ASan/UBSan clean.
`gamma build [-o]`, `gamma run --backend=c` (arg0 = source path on unix).
Not yet: fibers/ctl/resume, foreign fns, #global, #bits fields, ints >128 bits, float pow,
elementwise array ops, Fast/Checked mode flags (Options{checks,debug} exist; CLI always debug),
static handler specialization (D.3), stack-overflow trap (C just segfaults), `gamma test` native.
Gotchas: Const is a place (static gm_cN) — `&CONST` must not copy; pattern types needed
finalizing (sema fix); defers run by a ladder must save/clear gm_unw.
