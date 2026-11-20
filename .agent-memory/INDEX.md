# Gamma: agent memory INDEX (read first)

## State (2026-11-20)
- `main` = PRs #1–#130 merged; PR #131 (s75) open. Released: 0.1.0, 0.2.0 (CHANGELOG `## Unreleased` open).
  Tag `rust-reference-final` marks the last Rust implementation.
- The compiler is self-hosted: `compiler/` (Gamma) builds `gamma` from the C seed in
  `bootstrap/seed/` (= the build's C units; refresh with `tools/refresh-seed.sh` after
  cgen changes or new core intrinsics). Back ends: interpreter (`--backend=interp`,
  register VM + tree fallback) and C (gcc/clang, MinGW, Fil-C Safe, wasm32-wasi,
  freestanding). `run` defaults to C.
- Tests: `tools/test.sh` → `tests/harness/` suites. CI (ci.yml): every job runs
  bootstrap/build.sh then tools/test.sh (subsets for the Safe, s390x, aarch64, WASI
  lanes; Windows via cross-built gamma.exe + harness.exe), plus fuzz smoke, difftest,
  benchmarks, editors.
- Last sessions: s74 tooling (070); s75 roadmap sweep (history.md s75; decision 071 struct
  errors rejected). Windows CI test step ~23 min vs ~70 s on Linux: cause found and fixed
  in s75 (Windows headers in every Debug unit); confirm on the next Windows CI run
  (gotchas/platforms.md "Windows CI time").

## Next steps
1. roadmap/next.md, top down: tooling gaps (`test --backend=interp`, package manager
   gaps), then performance (interp and compiler candidate lists), testing, language.
2. roadmap/bugs.md: small open bugs, good for a short session.
3. spec-notes/pain-points.md "Open": flag to the user before acting on the big one.

## Map
- roadmap/        next.md (all open work, prioritized) · bugs.md (open bugs, allowed
                  behaviour differences)
- architecture/   overview.md (agent pointers on top of docs/architecture.md) ·
                  checker.md (check/ and sema/ mechanics)
- decisions/      README.md = index by topic and status; one numbered file per decision
- spec-notes/     amendments.md (index of spec edits by session) · pain-points.md
                  (recurring usability pain: open, settled, by design)
- gotchas/        build-and-test.md (memory limits, rebuilds, diagnosing failures, git) ·
                  platforms.md (clang, aarch64, s390x, Windows, WASI, Fil-C) ·
                  codegen.md (generated C, runtime, host stack) ·
                  parallel-agents.md (worktree workers: stale bases, limits)
- history.md      compact project history, s1–s73

## Conventions in Gamma code (quick reminders; the guide is the reference)
- Effects: `fn() -> T needs Alloc + Out`; effect-set args `Sched(Out + Clock)`; pure
  foreign fn `needs not`.
- `print("{} {}", a, b)`: values are rest args. Ops are always qualified (`Clock.now()`,
  `Log.log(..)`, `f.read(buf)`); only `yield` is bare.
- Inferable type params by name, first: `new(T: Page, ---)`. Tests: `ok(x)`, no `try`.
- Core modules (`lib/core`) may use @heap_*, prelude privates, `is`/@hash on params.
- New examples need a case in tests/examples/*.run and `gamma fmt`.
- Learned idioms go into the guide chapter they belong to (docs/guide/).
