# Gamma implementation — agent memory INDEX (read first)

## State (2026-10-13) — PRs #1–#18 merged; s6–s9 = parallel Claude Projects threads
- Thread "bytecode VM" (branch claude/project-thread-rh6kp0): `gamma run` interpreter compiles
  functions to register bytecode (src/interp/vm/, decisions/020); bench 34.1 s → 10.0 s; tree-walker
  kept for fallbacks and `GAMMA_INTERP=tree` (CI lane on ubuntu).
- s9 thread "performance round" (branch claude/perf-refactor-tzdpsa): interpreter ~35% faster
  (locals in frame slots, lock-free Ty::kind), smaller Debug C, Safe builds link the cached
  runtime, faster test suite; measurements + leftover ideas in decisions/019. Profiling with
  perf: see decisions/019 end.
- s8 thread "bug-hunting round" (PR #18, merged): ~40 compiler,
  runtime and stdlib fixes found by hunter subagents (sessions/s08-2026-10-12-bughunt.md);
  leftovers bugs-open.md #39–40.
- s7 thread "Dogfooding" (branch claude/dogfooding-wnnaie): spec §22 stdlib policy (apply it
  to every stdlib addition!), format specs, L2–L6, many new std modules (decisions/018,
  sessions/s07). Leftovers in bugs-open.md #31–38.
- s6 thread "Fil-C memory safety" (PR #14, merged): `--mode=safe` via Fil-C, spec §18.7, Safe
  lane (GAMMA_TEST_SAFE=1), CI job (decisions/017). Thread "Faster Fil-C codegen" (PR #15,
  merged): ladder once per scope, typed GM_LD/GM_ST under Fil-C, to_ptr fix; Safe now ~2–16×
  Checked (decisions/017 end).
- s6 thread "C perf" (branch claude/project-thread-8a201g): items 4–6 — nested handler
  specialization, exit-test omission, gm_out results, in-place construction, borrowed params,
  -fno-strict-aliasing (decisions/016, sessions/s06). Other s6 threads: build speed,
  cross-compiling, debugger views (expect merges touching src/cgen).
- s6 thread "Speed up native builds" (branch claude/project-thread-1hib7q): Debug builds link a
  cached runtime object, -O1 (decisions/014). C codegen thread too.
- s6 thread "Support cross-compiling" (branch claude/project-thread-y3y0bn, PR #12): CC with
  args, CFLAGS/LDFLAGS, target probed from the compiler (decisions/015); suite passes
  cross-compiled for aarch64 under qemu.
- s5 (Claude Projects thread "Strengthen the C back end"): `gamma build -g` debug info
  (decisions/013); plan for the rest in roadmap/c-backend-plan.md. Thread "Better debugger
  views" (branch claude/project-thread-scimha): plan item 7, typed views + gdb printers.
- Parallel s6 threads (2026-10-11): std/text graphemes (branch claude/project-thread-rk2qb5,
  see roadmap/M11), plus others on native build speed, cross-compiling, C perf, debugger.
- The whole spec is implemented in both back ends (interpreter + C), except "Not yet". Every run
  test runs on both and must match (gcc and clang). Milestone files roadmap/M00..M12; decisions/.
- Sessions 1–3: see sessions/. Session 4: C big values by address, std/collections, qualified
  generic names + declaration aliases + re-exported modules, dogfood round 4 (example4_*) and
  its fixes (sessions/s04). Session 5: `gamma test` runs imported same-package modules'
  tests, `--filter=` (sessions/s05).
- GitHub Actions has runners again (2026-10-12), incl. the Safe (Fil-C) job; still verify locally
  (fmt, clippy -D warnings, cargo test, CC=clang conformance) before pushing.
- Open bugs/usability list: roadmap/bugs-open.md; open design items: spec-notes/open-issues.md.
- Build modes: `--mode=debug|checked|safe|fast` (Safe needs Fil-C in /opt/fil). Core modules (is_prelude) may use @heap_*, prelude
  privates, `is`/@hash on params.

## Next steps
1. Drive the s6 PRs green; C back end plan items 1–7 done (uncaptured locals as C locals:
   thread "Uncaptured locals", branch claude/project-thread-oemqrj).
2. bugs-open.md leftovers (#23–#40); computed array lengths done (decisions/007); VM
   follow-ups in decisions/020 (superinstructions, smaller Op, cheaper calls).
3. lib/ must stay `gamma fmt`-formatted (tests/fmt.rs checks).
4. Safe mode follow-up (decisions/017 end): constant large args static (done, PR #17);
   left: `&key` escapes in std maps, `new`'s GC alloc (allocation-heavy code ~13× Checked).

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria); bugs-open.md;
                  c-backend-plan.md (prioritized C back end weaknesses)
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
                  · 007 value params + sema-time const eval · 008 C back end · 009 interop
                  · 010 fibers · 011 stack size · 012 handler specialization + local exits
                  · 013 debug info (-g: #line per C line, named frame fields)
                  · 014 native build speed (cached runtime object, GM_API linkage)
                  · 015 toolchain env (CC/CFLAGS/LDFLAGS) + target from the compiler
                  · 016 C perf s6 (exit flag measured, nested spec, gm_out, borrow.rs)
                  · 017 Safe mode via Fil-C (capability-clean C, ucontext fibers, Safe arena)
                  · 018 stdlib policy §22 + s7 language changes (fmt specs, op=, .to)
                  · 019 perf round s9 (where time goes, what was measured/kept/dropped)
                  · 020 bytecode VM (shape, fallbacks, measurements, follow-ups)
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md · s02-2026-10-09.md · s03-2026-10-10.md · s04-2026-10-10.md
                  · s05-2026-10-11.md · s06-2026-10-11.md (cross) · s06-2026-10-12.md (C perf)
                  · s06-2026-10-12-safe.md (Safe mode) · s07-2026-10-12.md (dogfooding)
                  · s08-2026-10-12-bughunt.md (bug hunt)
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- `print` takes one args value: several arguments go in a tuple `print("{} {}", (a, b))`.
