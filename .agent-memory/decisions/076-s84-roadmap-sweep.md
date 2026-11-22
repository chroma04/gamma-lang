# 076: s84 roadmap sweep: what was done, measured, dropped

User (s84): "clear out the rest of the open items in the roadmap". Done items are in git
log; this file keeps the reasons and numbers for what was decided.

## Done
- Spec §18.4: memory has no declared type (no strict aliasing); case 2 = misaligned or
  past-the-object only. Matches all back ends (gcc/clang run with -fno-strict-aliasing,
  interp is bytes, Safe reinterprets). Test: conformance/run/memory_untyped_access.gm.
- Vectors: `LaneMap: LaneOp` builtin (sqrt, trunc, round, floor, ceil, sat) = scalar builtin
  per lane (interp vector.gm, cgen vec_lane_map loop); `Shuffle: u8` = number of vector
  operands (1 or 2). check/vector.gm `vector_lane_conversion`, `shuffle_index`.
- `gamma test --backend=interp`: fork per test (runtime gm_proc_fork/gm_proc_fork_exit;
  testing/run.gm Fork protocol: workers ask the MAIN thread via Pool.fork_busy/ask/answer,
  because worker stacks are 8 MB vs main 64 MB). compiler tests 112 s -> 7 s. Linux+glibc only (macOS CI hung: a
  mutex/FILE lock records its owner's thread id, which the forked copy's thread lacks);
  macOS/Windows/WASI:
  gm_proc_fork returns Unsupported -> old `--run-test=j` spawn path.
- Interp: child-process read/write/wait on host threads when >1 green thread; scheduler
  re-polls Host waiters every 1 ms even if another thread has a later deadline (that was
  the real hang: watchdog's 10 s futex deadline hid the 1 ms poll).
- LIR: range-check elision (Check.Range) in lir/checks.gm; `plain` knows 0 <= x. Compiler:
  346 of 1,090 slicing checks removed.

## Measured, kept
- Interp `vm_perform_site`: skip `types.cmp_ref` when a Decl context has one decl
  (bench/programs/with.gm 372.6M -> 361.2M Ir, -3.1%; perform/call unchanged).

## Measured, dropped (callgrind, interp, bench/programs/call.gm, 200k calls)
- vm_push = ~174 Ir/call spread over ~20 lines. Frame memset = ~16 Ir/call (3% of the
  run), const register copy = ~5 Ir/call (1%). Removing the memset needs a proof that every
  register is written before read on every path (LIR verify gives it for temps, not for
  locals/fat slots); consts as immediates needs a new Bc operand encoding. Both dropped:
  < 4% together for a correctness obligation.
- `with` re-resolving its perform site per context: already short-circuits when the clause
  is unchanged (`s.callee is not null and s.f is f`); left after the fix above: find_ctx
  + site bookkeeping, ~200 Ir per fresh `with`. Not worth a cache keyed by ctx shape.

## Not doable in this container (stay with the user / future sessions)
- Windows-only work (Safe, gamma debug, gdb printers, ARM64 Windows), FreeBSD, real Zed /
  Emacs GUI testing, gcc 15 repro (difftest seed 91451).
