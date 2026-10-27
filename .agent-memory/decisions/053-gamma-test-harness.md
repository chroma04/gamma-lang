# 053 Test harness in Gamma; SpawnOptions env changes + process groups (s49, 2026-10-27)

Phase 4 of 052: tests/*.rs (cargo) → `tests/harness/` (Gamma program, package `harness`,
path dep on compiler/ as `gammac` for driver.from_env/target/strip_wasm_trap_report and
load.Target).

## Harness design
- `main.gm`: `suites()` table (name, `^fn(cfg, out: ^List(kit.Test)) needs Setup`); args
  `[-j N] [-v] [--list] [--filter TEXT] [PREFIX...]` (prefix of `suite/test`; filter =
  substring, also GAMMA_TEST_FILTER). Worker threads (`with heap_allocator(), system_io(),
  system_output()` — thread.defaults can't be used: closure can't need Sys), @atomic_add
  job index, arena per test, report printed under a mutex on failure (-v: all). Times in
  `<tmp>/harness-times`, longest first, unknown first. Exit 1 on failure, 2 on usage.
- `kit/`: Config (root=cwd must hold gamma-spec.md, compiler, tmp=target/test-tmp or
  GAMMA_TEST_TMPDIR, cache, timeout_s, bless, c, safe, safe_lane, target, arch, os_kind,
  vars). Env on threads: TestEnv handler over a snapshot of `kit.VARS` (add names there if a
  test or the compiler's helpers read a new variable!). T: fail/check/expect_text/
  expect_contains/skip/log/diff/quote; Command (argv, env changes, dir, input) run/run_ok/
  try_run (spawn failure → null); runs(prog) (`--version`); Watchdog (watchdog/disarm) used by
  run and the interactive lsp/pkg clients; scratch/write/read/read_if_exists/repo/join.
- tools/test.sh builds gamma (as before) and the harness with CC/CFLAGS/LDFLAGS/
  GAMMA_RUNNER/GAMMA_C_UNITS unset (they may name a cross compiler); GAMMA_TEST_HARNESS = a
  prebuilt one (Windows CI cross-builds harness.exe with gamma.exe).
- Suites: one module dir each; ~1315 tests. conformance 841 (one per file/.test dir),
  examples per .run case, doc_examples per block, lint per std module, lanes per run file
  (only with GAMMA_TEST_LANE). long_expressions ~13 min (native_long_sum 11 min).
- Deviations accepted: doc golden index title is the directory name ("shapes modules");
  native_build C-per-site test counts gamma's helper layout (GAMMA_C_UNITS=1); pkg
  canonicalize via `sh -c 'cd && pwd -P'`, chmod/ln/touch by shelling out (no std API).

## SpawnOptions change (spec §21.7, CHANGELOG)
- `env: []const string` now CHANGES the inherited env (`NAME=value` sets, `NAME` removes, in
  order); `clear_env: bool` starts empty (old replace behavior); `group: bool` = own process
  group (setpgid in child + parent; kill(-pid)); Windows: job object (CREATE_SUSPENDED,
  AssignProcessToJobObject, TerminateJobObject) — untested locally (no mingw here), CI only.
- Runtime flags: 64 = clear env, 128 = group (GM_PROC_CLEAR_ENV/GM_PROC_GROUP); env entries
  always passed; merge in C (gm_child_env; Windows merges GetEnvironmentStringsW,
  case-insensitive names). interp/sys.gm passes them through.
- Why: the harness had no way to set one variable for a child (no env listing) nor to kill
  `gamma run --backend=c` with its child on timeout.

## Other phase-4 removals
src/, Cargo.*, rustfmt.toml, web/playground, web/sitegen, fuzz cargo crate (fuzz/gammac/* →
fuzz/, new inputs fuzz/found/), runtime/gm_clif.c (RUNTIME_NAMES regenerated from
gm_rt.h+gm_rt.c, minus "gm_rt"), tools/difftest → Gamma (same programs per seed, verified
0..300 against the Rust build). CI: no Rust except the Zed extension (editors/zed).

## CI lessons (PR #97)
- macOS: calling setpgid in BOTH parent and child raced → child's setpgid failed EPERM
  (1–2 spawns in ~1300 reported PermissionDenied). Now child only (spawn waits for exec).
- Building the harness for Windows: code after `when target.os is .Windows { return … }` is
  "unreachable code" there → always write `when … { } else { }` for early returns.
- editors/zed needs its own rustfmt.toml (was the root one).
- All 17 CI jobs green on 78f042f (first run of Windows job objects/env merge passed).
