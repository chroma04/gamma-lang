# s23 (2026-10-17, same session as s22 after PR #46 merged): difftest disagreement on main
Branch claude/difftest-shl. CI run 246 (main, #45 merge) difftest rotating seeds
1000 + (run % 500) * 200 = 50200..50400 → seed 50365 (the `<<` trap in the log was a red
herring: both engines trapped there; the diff was an extra `true` before a line).
- Cause: cgen may_unwind(Named) = !within_sys(effects) only. A fn with a second-class
  `fn` param and no effects (twice(x, f)) was a "plain" fn → caller emitted no
  `if (gm_unw.kind) goto gm_lad` → after a clause `cancel k; break v` from inside the
  closure, the caller kept running with garbage (0xAAAA…) and printed.
- Fix: cgen::takes_fn(param tys) → may_unwind true; FnPtr indirect calls check unwind
  when an argument has `fn` type (ABI/hidden convention unchanged).
  Test run/closure_arg_exit.gm (direct, generic, relay, fn ptr; before fix C printed
  "true7" and "not reached").
- Repro recipe: build tools/difftest (--target-dir target), `difftest --start S --count N
  --cc gcc,clang`, programs land in /tmp/difftest/seed-N/main.gm; minimize by hand.
