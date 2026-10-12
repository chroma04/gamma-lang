# 018 Standard library policy (spec §22) and the s7 language changes

## Stdlib admission (§22) — requested by the user: "robust demarcation to prevent bloat"
- Admit only what is (a) needed by many unrelated programs, (b) has one obviously right
  design, (c) wraps the OS/target or (d) is a vocabulary type other packages exchange.
  Domain formats/protocols (JSON, HTTP, regex, compression, crypto beyond hashing) stay
  outside unless they become vocabulary; ordered maps / reflection / JSON are "later" and
  must clear §22 first.
- Conventions: caller buffer or append to ^List; Yield iterators; services as effects with a
  default handler; no second way of doing the same thing (that is why str.dupe became
  `dupe` in prelude and str.format is the only allocating formatter).

## Language changes (approved as L1–L6 in the plan)
- L1 format specs `{:[fill]align][+][#][0][width][.prec][x|X|o|b|e]}` checked at sema
  (also after mono); runtime gm_format_spec in C, display::format_spec in interp. Width
  counts scalar values.
- L2 `try` in a test body accepts any error type (prints `error: …`, traps).
- L3 `op=` widens the right side when lossless to the (numeric) place type.
- L4 arrays pass to `[]const T` params (AddrOfTemp for rvalues).
- L5 `.to(F)` float target only (int narrowing keeps trunc/checked/sat); `unsigned_abs`.
- L6 an identical import repeated in another file of the module is allowed.
- bool is in `Ord` (false < true).
- `expect_eq` builtin (tests only); leak report lists allocation sites (TestTracker).
- `gamma run --backend=c` caches executables (rtcache::cached_program, 32 kept).
- After merging main (Safe lane, PR #15): `.cast` now enforced pointer→pointer (data↔data,
  fn↔fn; spec §6.6 table); std/fiber stores the task fn as a cast fn pointer (usize
  round-trip lost the Fil-C capability). `exit_status_code` maps SIGTRAP/SIGILL (Fil-C
  panics) to 134. gm_segv also reports an overflow when the faulting sp < gm_stack_limit
  (clang inlines huge frames before the check). Debug-only "in test" locations → tests
  marked `safe: skip`. Fil-C 0.686 installs locally from the GitHub release (setup.sh -u).
