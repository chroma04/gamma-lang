# Gamma: agent memory INDEX (read first)

## State (2026-11-21)
- `main` = PRs #1–#139 merged (0.3.0 prepared in #132; tag v0.3.0 goes on its merge
  commit). `## Unreleased` in CHANGELOG.md started in s77.
  Tag `rust-reference-final` marks the last Rust implementation.
- The compiler is self-hosted: `compiler/` (Gamma) builds `gamma` from the C seed in
  `bootstrap/seed/` (= the build's C units; refresh with `tools/refresh-seed.sh` after
  cgen changes or new core intrinsics). Back ends, both translating function bodies from
  LIR (compiler/lir): interpreter (`--backend=interp`, register VM, interp/vm_lir.gm) and
  C (cgen/lirc.gm; gcc/clang, MinGW, Fil-C Safe, wasm32-wasi, freestanding). `run`
  defaults to C. Debug a lowering problem with `gamma lir FILE`.
- Tests: `tools/test.sh` → `tests/harness/` suites. CI (ci.yml): every job runs
  bootstrap/build.sh then tools/test.sh (subsets for the Safe, s390x, aarch64, WASI
  lanes; Windows via cross-built gamma.exe + harness.exe), plus fuzz smoke, difftest,
  benchmarks, editors.
- Last sessions: s77 perf round 10 (072); s78 shared template bodies (073); s79 LLVM
  re-evaluated (074): stay on C; s79–s83 lowered IR (075), plan DONE: s81 interp from
  LIR (tree evaluator deleted), s82 bounds-check elimination, s83 cgen from LIR for every
  body, TIR body translation deleted, seed refreshed; cgen's whole-program analyses
  (borrow.gm, may-exit, call depth) deliberately stay on TIR (075).
- Gotcha: `gamma build` and bootstrap default to Debug = gcc -Og; perf numbers in
  068/069/072 are of that build (Fast is ~15% fewer Ir).

## Next steps
1. roadmap/next.md, top down: tooling gaps (`test --backend=interp`, package manager
   gaps), testing, language. Performance §2 now lists only structural changes.
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
- history.md      compact project history, s1–s83

## Conventions in Gamma code (quick reminders; the guide is the reference)
- Effects: `fn() -> T needs Alloc + Out`; effect-set args `Sched(Out + Clock)`; pure
  foreign fn `needs not`.
- `print("{} {}", a, b)`: values are rest args. Ops are always qualified (`Clock.now()`,
  `Log.log(..)`, `f.read(buf)`); only `yield` is bare.
- Inferable type params by name, first: `new(T: Page, ---)`. Tests: `ok(x)`, no `try`.
- Core modules (`lib/core`) may use @heap_*, prelude privates, `is`/@hash on params.
- New examples need a case in tests/examples/*.run and `gamma fmt`.
- Learned idioms go into the guide chapter they belong to (docs/guide/).
