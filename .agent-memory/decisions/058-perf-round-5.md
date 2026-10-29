# 058 — Performance round 5 (s56, branch claude/perf-round-5)

User ask: "another session of performance enhancements" (open-ended). Picked by measuring
the test suite (tools/test.sh -v per-test times; baseline 275 s wall, 1097 s summed).

## Where the suite's time goes (baseline)
- selfhost fixed point 128 s (critical path: 4 C units of ~18 MB, 50-60 s each at -O2;
  the build waits on the slowest unit; unit 0 is ~10% slower, it has 0.8 MB more data).
- conformance/run 256 s summed: C lane 178 s vs interpreter lane 53 s (measured per test
  with a script running both lanes). A median Debug build was ~200 ms: front end ~35 ms,
  probes (`cc --version`) ~4 ms, compile ~120 ms, link ~25 ms (bfd; lld slower here).
- fmt corpus 61 s: ~4,000 `gamma` spawns from the harness; sys time 18.5 of 25 s = fork().

## Done
- posix_spawn (glibc >= 2.29, not Fil-C; `GM_POSIX_SPAWN` in gm_rt.h): file actions
  dup2/addchdir_np, SETSIGMASK empty, SETSIGDEF {SIGPIPE}, SETPGROUP 0 for groups. glibc
  reports exec/chdir errno and reaps the failed child. Fork path kept elsewhere.
- `GM_RT_PROGRAM` (defined with GM_RT_SEPARATE in driver/native.gm PROGRAM_PRELUDE, also in
  the split --emit-c header): gm_rt.h includes only stdint/stddef/stdbool/stdarg/string/
  errno (not on _WIN32). System headers were ~33 ms of every cc call. Generated code must
  not call libc beyond string.h/errno (stdio was used by the test main → gm_test_index).
- Trap messages outlined: gm_trap_index/slice/shift/lanes/unwrap (runtime). gcc time of a
  compiler unit -3.5%, compiler binary 9.40 → 8.80 MB.
- New runtime names go in compiler/sema/runtime_names.gm (sorted, count in the type).

## Measured, not taken
- lld link 48 ms vs bfd 27 ms for a small program. `-lpthread` vs `-pthread` ~2 ms.
- GCC -ftime-report on a compiler unit: no dominant pass (expansion 36 of 53 s spread
  over RA, scheduling, PTA, CSE…): only less code helps. Per-unit ~4 MB is shared
  declarations (prototypes, types, string externs) of 17.5 MB.

## Interpreter: bit builtins in the VM
- `@rotl/@rotr/@clz/@ctz/@popcount/@bswap/@bitreverse` on word ints (int_w: 8/16/32/64)
  went through ev_builtin → apply_builtin (u128 get/put, int_info per call): ~900 interp
  instrs per op in SHA-256. Now Op.IntRot/IntBits (code.gm) → Ins.IntBit (vm.gm) →
  `bit_word` (run.gm, #noinline). Rotation amounts are always u32 (spec). std_hash interp
  3.55 → 0.75 s. Test: conformance/run/bit_intrinsics_words.gm.
- blob_over (cgen expr.gm): in DEBUG builds only, values ≤ 64 bytes with ≥ 2 gap stores
  are memset whole (never on Fil-C: memset is a runtime call there, the reason gaps were
  stored one by one, s21). Debug C of a test binary -3%. In optimized builds it cost +5%
  Ir on map_insert (Fast) and +4% on interp calls (GCC does not trim the memset).
- Measuring cgen changes on the compiler itself: the binary's code is from the cgen of the
  compiler that BUILT it. Compare generation N+2 (built by a compiler with the change).
- Interp benches after the round (Ir, orig → now): call 259 → 256M, perform 372 → 363M,
  with 597 → 582M, iterate 437 → 427M; Fast map_insert 9.40 → 8.28M.
- get_uint/put_uint: 8/16-byte fast paths (le64/put_le64; rd_word/wr_word can't be used:
  #inline functions may not be mutually recursive, wr_word falls back to put_uint).
- utf8 test profile: vm_slow entry ~35 instrs, cost is in tree work it hands off
  (Discard ~1,370/iteration for `for r in runes(e)`, EvalSlot, Stmt). u128 arithmetic
  (parse_duration) runs in the tree: get/put_uint + int_binop, no single hotspot.
- Profiling: `gamma build -g compiler -o $SP/gcg/gcg` + callgrind + names.py (maps gm_fN
  through #line to `name@file`; callgrind_annotate already shows file:line names for -g).

## Generated C size (biggest lever for the self-build and test binaries)
- Measure by source: `gamma build -g compiler` → C with #line; scratch agg.py sums function
  bytes per (file, line) of the first #line = definition; core functions have no #line
  (group them by signature shape). Before: 51.2 MB function text (-g), str.format (781)
  1,540 instances; core List.write instances 5.5 MB; their sink closures 2 MB; List.regrow
  298 instances × 8.7 KB.
- @write with a `.Fn` sink (cgen expr.gm `write`): closure + ccx once, then per piece
  `fn_sink_stub` (calls closure: tagged plain / hidden, returns unwinding) or
  `fn_format_stub` (gm_format or gm_format_spec, skip empty, free buf) — both in
  Gen.stubs/helpers (keys "fnsink"/"fnformat"). Skipped if string is by_ref.
- str.format writes through @write itself (no List.write instance per template).
- core list.gm: regrow → regrow_raw(^RawList, …, size, align) (RawList = List layout with
  items: []u8). Run time unchanged.
- Result (Checked self-build, gen built by a compiler with all changes): C 72.7 → 60 MB,
  binary 9.40 → 7.90 MB, cc CPU 215 → 192 s, wall 66 → 62 s (units balanced serially
  within 10%; parallel contention makes unit 0 look slower). Suite 275 → ~205 s wall,
  1097 → 804 s summed (conformance/run 256 → 115 s).
- Not done: map growth per (K,V) (55 × 9 KB, rehashes: needs hash fn pointer), sort per T
  (79 × 4.9 KB), harness kit.check/fail forward templates to str.format (harness only),
  seed refresh (56 MB diff; bootstrap works with the current seed — verified).
- Pain point logged: comptime `template :: string` → instance per template; wrappers
  multiply (spec-notes/pain-points.md). Language idea: checked template values.
