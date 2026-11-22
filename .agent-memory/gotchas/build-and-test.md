# Building and testing: pitfalls on this machine

## Resources (cloud box: ~15 GB RAM, no swap, 4 cores, ~40 GB disk allowance)
- One compiler build = gamma ~2.3 GB + up to 8 cc1 at ~800 MB. Three or more parallel
  builds / `tools/test.sh` runs get OOM-killed (exit 137, often with empty stderr): run
  at most two at once, or serialize them with `flock <scratchpad>/build.lock`.
- Remove finished git worktrees and big downloads (wine, llvm-mingw, Fil-C) promptly.
- Never time anything while another build or test runs in the background; wall time here
  is ±5–30% noisy anyway: compare callgrind Ir instead (recipe: docs/contributing.md
  "Profiling the compiler").

## Rebuilding and re-running
- `target/bootstrap/gamma` is the OLD compiler until `tools/test.sh` (or a manual
  `gamma build compiler`) rebuilds it: a compiler fix does not show in `gamma check` before.
- Re-run tests without rebuilding: `GAMMA_TEST_COMPILER=target/bootstrap/gamma
  GAMMA_TEST_HARNESS=target/bootstrap/harness tools/test.sh PREFIX` (instant, no lock).
- Do not edit `compiler/`, `lib/` or `runtime/` while `bootstrap/build.sh` or a
  `selfhost` run is going: they read sources mid-run (a fixed-point test then fails
  spuriously because stage 1 and 2 saw different trees).
- Packages that import gammac and break when a `pub` goes away: `tests/harness`,
  `fuzz/*.gm`, `web/gen`, `web/play` (check each; `gamma test web/gen`). `gamma lint
  compiler` has no auto-fix for needless-pub.
- `gamma lint lib/std` fails ("no .gm files"): lint per module dir (`gamma lint lib/std/math`).

## Diagnosing failures
- A spurious failure that names a source file ("core module X does not declare Y"): check
  whether some test WRITES into the tree (`find lib tests -newermt …`, or
  `strace -f -e trace=openat harness SUITE | grep O_WRONLY`). Tests must never do that.
- Silent exit 1 from `gamma run` = look for an unreported `.Failed`/`false` path (unit
  object cache, `gm_proc_kill` on Windows).
- Timing loops over `gamma run` of a program that reads stdin hang: redirect `</dev/null`.
- Tests run as root here; CI does not. Permission bugs (read-only checkouts) hide
  locally: run such tests as `runuser -u nobody -- env HOME=/tmp/x TMPDIR=/tmp …`.
- NEVER `pkill -f` / `pgrep -f` with a pattern that occurs in your own command line (it
  kills your shell, exit 144) or in other builds (it kills them). Use
  `ps -eo pid,comm | awk '$2 ~ /^name/'`.
- Find which gcc pass breaks generated C: `-fno-<pass>` one by one, then bisect functions
  with `__attribute__((optimize("no-…")))`.

## Run cache race (fixed s83)
- `gamma run` cache (`<cache>/run/prog-*`): prune spares programs used in the last 10 min,
  but could delete one another process had just found (stat before its `touch`). Suites
  over 10 min that rerun programs (Safe job: conformance/ then lanes/) hit it as
  "cannot run …/prog-…: No such file". driver/native.gm `run` now rebuilds privately.

## Seed refresh
- `tools/refresh-seed.sh` needs compiler/lib/runtime committed. To check the result,
  run `bootstrap/build.sh` FIRST (gamma0 must come from the new seed), then
  `bootstrap/verify.sh`; with the old gamma0 it reports every unit as different.

## Git / GitHub
- No `gh` CLI for PRs: use the GitHub MCP tools. CI job logs: `get_job_logs` with
  `tail_lines` (the download-URL form is 403 here).
- Force-push is refused by the permission classifier.
- Check CI after each push; read PR notifications before assuming green.

## Difftest rotating seeds
- CI's "Differential tests / Rotating seeds" uses seeds 1000 + (run_number % 500) * 200 ..
  +200, so a failure on main can be a latent bug a new window exposes. Reproduce:
  `gamma build tools/difftest -o X; GAMMA_TEST_C=1 X --gamma <gamma> --start S --count 1
  --cc gcc` (program kept in /tmp/difftest/seed-S/main.gm). s78: seed 10550 = local
  constant calling core code (check_bodies parked core bodies) + const string `>`.
- Measuring generated C (stack sizes etc.): do NOT set `CFLAGS` or wrap `CC` for
  `gamma build`: either changes the flags (a CC wrapper loses gcc detection, so no
  GCC_DEBUG_PASSES; Ir of `check compiler` went +10..24%). Instead `gamma build --emit-c
  compiler -o X` and compile X.c by hand with BASE_CFLAGS (native.gm) + `-pthread -Og
  -finline-functions-called-once -ftree-bit-ccp -fif-conversion -fstack-usage -c`
  (one 50 MB unit: ~3 min at -Og, more at -O2). Sum: `awk '{s+=$(NF-1)} END{print s}' X.su`.
- callgrind/the built compiler need ABSOLUTE `GAMMA_STD`/`GAMMA_RUNTIME` when run from
  another directory (relative ones fail silently: 0.75M Ir runs).
