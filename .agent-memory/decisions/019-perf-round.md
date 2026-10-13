# 019 Performance round (s9, thread "optimization/performance refactoring", branch claude/perf-refactor-tzdpsa)

## Where time went (measured 2026-10-13, 4 cores)
- `gamma run` (default = interpreter) ~100x slower than C: interp is the user-facing lever.
- cargo test ~3 min: compile of tests, lib unit tests 16 s (ONE test: tests_fast
  small_binop_matches_general), conformance ~44 s = interp ~40% + gcc ~60% of CPU.
- Front end (parse+check incl. std) ~16 ms release / 30 ms dev per invocation: not a lever.
- gcc time ∝ generated C volume; -Og 20% faster compile but sha256 debug build 4x slower: kept -O1.
- Safe lane (GAMMA_TEST_SAFE=1): 19 min wall / 63 CPU-min; filcc compiling the reachable
  runtime was ~half of each small program's build (hello 2.5 s).

## Done (bench: scratchpad ibench.sh = 8 slow conformance tests + sha256/wordfreq)
- Ty::kind lock-free (types.rs KINDS: chunked AtomicPtr table, chunk c = 1024<<c slots).
- interp TyInfo<'a> caches `adt: Option<&AdtDef>` (Machine::adt was SipHash per field access).
- interp address cache 256 → 4096 entries.
- interp locals ≤ 16 B (REG_MAX) live in `Slot::Reg([u8;16])` until their address is needed;
  `local_addr` spills (allocates, copies) on demand, so correctness needs no escape analysis.
  `set_local` writes slot or memory; Assign to a Local place and Let bypass eval_place.
  kill_frame empties Reg slots. Total interp bench 46.4 s → ~30 s.
- Rotl/Rotr of ≤64-bit ints without BigInt (sha256 interp −24%).
- cgen: Local read as its own type = `tN = F.lN` (not GM_LD of its address); Assign to a
  Local of the same type = `lv = v`; misaligned-cast check calls gm_trap_misaligned (runtime).
  ~10% less gcc time for Debug builds.
- Safe builds link the cached runtime object (rtcache) like Debug: hello 2.5 s → 1.3 s
  build; programs ≤10% slower.
- tests: small_binop test skips Debug mode (= Checked) and 1-byte types on targets 2–4
  (16 s → 4 s); conformance runner records times in CARGO_TARGET_TMPDIR/conformance-times.txt
  and starts the slowest first.
## Measured and dropped
- gm_bt call sites as one store of a static gm_site pointer: no measurable gcc gain.
- File path literals as static arrays: no gain.
- Interp: reading fields of a Reg local without spilling it (reg_part): no gain.
- gcc PCH of gm_rt.h for Debug builds (cache gm_rt-<hash>.h.gch; must be the TU's first
  line, GM_RT_SEPARATE defined inside the header): hello build 202 → 186 ms; not worth a
  gcc-only cache path.
- Safe at -O1 instead of -O2: filcc 15–25% faster, programs ±8%; kept -O2 (Safe trap
  behavior was validated at -O2, see gotchas).
- Safe lane after the cached runtime: 19 min / 63 CPU-min → 13 min / 49 CPU-min. The rest
  is filcc on the program; slice_algorithms alone takes 33 s (big main).
- dev profile opt-level 2: interp +15%, incremental rebuild same; not changed (noise-level
  for the suite).
## Ideas left
- Interp: dispatch overhead now dominates (eval_small/exec_stmt/eval_place); next big step
  is a pre-compiled form (closures or bytecode), not more fast paths.
- C compile: overflow-trap and backtrace-site code ~8% each of Debug gcc time; multi-TU parallel builds for big programs (latency only).
- Profiling: `apt-get install linux-tools-6.8.0-31-generic`, perf at
  /usr/lib/linux-tools/6.8.0-31-generic/perf, `echo -1 > /proc/sys/kernel/perf_event_paranoid`;
  no HW counters (task-clock only); build with RUSTFLAGS="-C force-frame-pointers=yes" in
  a separate CARGO_TARGET_DIR for call graphs.
