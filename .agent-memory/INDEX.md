# Gamma implementation — agent memory INDEX (read first)

## State (2026-11-13) — PRs #1–#121 merged, v0.1.0 + rust-reference-final tagged
- s68 (branch claude/magical-dirac-tgheox): INCREMENTAL NATIVE BUILDS (decisions/065):
  stable C names (cgen/names.gm: gm_f_<name>_<hash>, content/key-named data and types),
  split by source file with per-unit pruned headers (units.gm split_by_file), unit
  objects cached by text (driver/rtcache.gm unit_objects; pooled compile_objects).
  Compiler debug build: cold 41 s, one-file body edit 4 s. Follow-ups in 065.
- s67 (branch claude/optimistic-curie-kxu2yf): INTERPRETER REDESIGN (decisions/064): 16-byte
  specialized `Bc` per instruction (bc.gm), every compiled call made inside the vm_run loop
  with a CallRec per body (no host recursion, no Frame for frameless bodies), page-table
  memory (O(1) address lookup), much wider vm_compile coverage, cgen drops stack probes of
  #inline helpers. 2–4x on real programs; 064 lists measured next candidates.
- s51–s66: perf rounds (decisions/055–063; profiling recipe in docs/contributing.md),
  refactor rounds (roadmap/refactor-backlog.md: done), lint stale-element-pointer (060),
  tuple spreads (061), 0.2.0 prep. LESSON: at most 2–3 parallel building agents on this
  16 GB box. CHANGELOG `## Unreleased` open. Pain-points "Open": seed-refresh watch,
  fn-as-format-arg, template size.
- s50 (branch claude/eager-curie-ocf0wz): PAIN-POINT SWEEP (decisions/054): whole list
  triaged (pain-points.md "Settled"), 16 items implemented (language: else/array joins,
  const lifting into ?/!, contextual needs/handler/foreign, `{0}`/`{:q}`, local types; lint
  stale-element-pointer; std: text trim, Channel drained, Fs/Proc ops, pub handles, Env on
  threads). Seed refreshed (prelude uses new intrinsics).
- s49 (branch claude/serene-cerf-gnjh9y): PHASE 4 of retiring Rust DONE (decisions/052,
  053): no Rust left (except editors/zed). Tests = `tests/harness/` (Gamma program, ~1315
  tests, `tools/test.sh [-j N] [-v] [--list] [--filter T] [PREFIX...]`), difftest in Gamma,
  fuzz/ flattened (s47–48: C seed bootstrap/, tools/refresh-seed.sh). Std: SpawnOptions env changes inherited env, clear_env, group (§21.7).
- s46 (#91): compiler/interp tree-walking interpreter + repl (051); slow.
- s38–s45d: gammac written in compiler/ (043–050); roadmap/self-hosting.md, gammac-cli.md.
  Idioms handbook removed s63 (user: directionless); patterns live in guide chapters
  (01 naming/modules, 03 match, 05 trap-vs-return + Abort parser, 09 arena/list pitfalls).
  Add learned patterns to the chapter they belong to. doc_examples checks for-c/for-rust only.
- s33–s37: errors policy + spread (037), HashMap (038), complex/#vec (039), #soa (040/042).
- s32 (#63): whole-codebase review: roadmap/review-2026-10-20.md (written against Rust src;
  re-check items against compiler/ before acting).
- Back ends: interpreter (`--backend=interp`, tree-walking) and C (gcc/clang, MinGW, Fil-C
  Safe, wasm32-wasi, freestanding). `run` defaults to C. Cranelift is gone.
- Build modes: `--mode=debug|checked|safe|fast`. Core modules (is_prelude) may use @heap_*,
  prelude privates, `is`/@hash on params.
- CI (ci.yml): every job runs bootstrap/build.sh then tools/test.sh (subsets for safe,
  s390x, aarch64, wasi lanes, windows via cross-built gamma.exe + harness.exe), fuzz smoke,
  difftest, benchmarks, editors. Verify locally: tools/test.sh (or prefixes), gamma fmt
  --check compiler tests/harness, gamma lint on touched code.
- examples/: cases in tests/examples/*.run (format: tests/harness/examples/examples.gm);
  new examples need a case and `gamma fmt`. Conformance format: tests/harness/conformance/.
- Older history: sessions/ and decisions/.

## Next steps
0. roadmap/review-2026-10-20.md "Open" (re-check against compiler/). Refactor backlog is done.
1. Keep CHANGELOG Unreleased current (start one above 0.2.0). User: enable private vulnerability reporting.
   Diagnostics must follow spec ch. 23 (policy for new errors/hints; limits in §23.5).
1b. Rust is gone (052/053). Follow-ups: kit helpers (gamma_in, sorted dir listing, jsonl),
   harness Watch thread → Proc.try_wait, pain-points.md "Open", Windows CI of the harness.
2. roadmap/todo.md: the prioritized list agreed with the user (P0 → nice). P1 done (s18);
   next P2: fuzz sema/differential, perf, Windows, Emacs.
2b. Incremental builds follow-ups (decisions/065 "Not done"): data of unit 0 by owner, -g.
3. VM follow-ups: decisions/064 "Not done" (push cost, dispatch, builtins, stack-local
   allocations, lazy frames); `with … on`, value loops, u128 ops still tree; bugs-open.md.
   C size follow-ups: decisions/058 "Not done" (map growth, sort per T, seed refresh).
4. Keep docs current with every change: CHANGELOG Unreleased, docs/std regen
   (`gamma doc lib/std --all -o docs/std`), lib/ `gamma fmt`-formatted (tests/fmt.rs).

## Map
- roadmap/        todo.md (prioritized to-do), milestones.md (M0–M12 + gaps), bugs-open.md,
                  self-hosting.md (gammac stages), gammac-cli.md (porting the gamma commands),
                  c-backend-plan.md, review-2026-10-20.md (s32 review findings + status),
                  refactor-backlog.md (s64–s66 refactors: done; small leftovers)
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001–059, one topic each (filenames say it): 008 C back end, 017 Safe/Fil-C,
                  020 bytecode VM, 023 tooling, 025/030 design reviews, 033 lint, 034 wasm,
                  035 Cranelift (removed), 036 website, 037 errors policy + array spread, 043 self-hosting, 044 s39 pain-point changes, 045 dangling error + shadowing stays, 046 gammac evaluator, 047 `?x` pattern, 048 gammac C back end, 049 temp arrays as slices + flush before blocking, 050 native coverage, 051 gammac interpreter, 052 retire Rust + C seed, 053 Gamma test harness + SpawnOptions env/group, 054 s50 pain-point sweep, 055 compiler performance (s51), 056 interpreter VM (s52), 057 perf round 4 (s55), 058 perf round 5 (s56), 059 playground root stack (s57), 060 stale-element growth summaries (s58), 061 tuple-expression spread (s60), 062 perf round 6 (s61; profiling recipe), 063 perf round 7 (s62; unit-cache idea, what is left), 064 interpreter redesign (s67: Bc forms, in-loop calls, page memory; stats recipe). `ls decisions/` for the rest.
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved) ·
                  pain-points.md (repeated usability pain: flag to user, user rule)
- sessions/       s01–s30 logs (compacted; s28 = decisions/030, s29/s31/s32 have none)
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main.
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- Effects: `fn() -> T needs Alloc + Out`; set args `Sched(Out + Clock)`; pure C fn `needs not`.
- `print("{} {}", a, b)`: values are rest args. Ops always qualified: `Clock.now()`, `Log.log(..)`, `f.read(buf)`; only `yield` bare. Inferable type
  params by name, first: `new(T: Page, ---)`. Tests: `ok(x)`, no `try`.
