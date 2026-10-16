# Gamma implementation — agent memory INDEX (read first)

## State (2026-10-16) — PRs #1–#39 merged; s17 PR open (v0.1.0 prep: bugs #50–53, CHANGELOG)
- s14 (branch claude/jolly-dijkstra-kenzir): documentation sweep of guide, std docs, spec,
  architecture/contributing, CLI help, source comments and this memory
  (sessions/s14-2026-10-15-docsweep.md). Audit leftovers: roadmap/bugs-open.md #50–53.
- The whole spec is implemented in both back ends (interpreter = bytecode VM + tree-walker
  fallback; C back end, gcc and clang). Every run test runs on both and must match.
  Milestones: roadmap/milestones.md (all done; per-area gaps). Decisions: decisions/.
- Tooling: packages (git/path/archive deps, lock, profiles), `gamma lsp`, editors/, release
  pipeline + install.sh/.ps1, gamma doc/repl/fmt/test/bench/coverage, Windows (MinGW) port.
  Left for the user: tag v0.1.0, publish the VS Code extension (Marketplace/Open VSX tokens).
- Build modes: `--mode=debug|checked|safe|fast` (Safe needs Fil-C in /opt/fil). Core modules
  (is_prelude) may use @heap_*, prelude privates, `is`/@hash on params.
- CI = fmt + clippy -D warnings + test (ubuntu, macos, windows) + Safe + s390x cross +
  editors + benchmarks. Verify locally (fmt, clippy -D warnings, cargo test, CC=clang
  conformance for cgen changes) before pushing.
- Recent history: s13 bug sweep (PR #36; locals not named like types, decisions/024);
  s12 to-do P2 (PR #35); s11 tooling (PR #34). Older: sessions/.

## Next steps
1. Drive the s17 PR (branch claude/wonderful-cerf-sfn0qf) to merge; then the user tags
   v0.1.0 (release workflow re-run by hand on the s17 branch incl. Windows — see s17 log).
2. roadmap/todo.md: the prioritized list agreed with the user (P0 → nice). Next up: P1
   dogfood pass + `examples/`, then P2 (fuzzing, tree-sitter newline, Cargo metadata, …).
3. VM follow-ups in decisions/020; bugs-open.md.
4. Keep docs current with every change: CHANGELOG Unreleased, docs/std regen
   (`gamma doc lib/std --all -o docs/std`), lib/ `gamma fmt`-formatted (tests/fmt.rs).

## Map
- roadmap/        todo.md (prioritized to-do), milestones.md (M0–M12 + gaps), bugs-open.md,
                  c-backend-plan.md
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
                  · 007 value params + sema-time const eval · 008 C back end · 009 interop
                  · 010 fibers · 011 stack size · 012 handler specialization + local exits
                  · 013 debug info (-g) · 014 native build speed (cached runtime obj)
                  · 015 toolchain env (CC/CFLAGS/LDFLAGS) + target from the compiler
                  · 016 C perf s6 (exit flag measured, nested spec, gm_out, borrow.rs)
                  · 017 Safe mode via Fil-C (capability-clean C, ucontext fibers, Safe arena)
                  · 018 stdlib policy §22 + s7 language changes (fmt specs, op=, .to)
                  · 019 perf round s9 (where time goes, what was measured/kept/dropped)
                  · 020 bytecode VM (shape, fallbacks, measurements, follow-ups)
                  · 021 language papercuts (tuple payloads, ret inference, generic fn ptrs, cast)
                  · 022 effect-set parameters (`E: effects`, `needs E`, Sched(E))
                  · 023 tooling: packages, LSP, editor grammars, release
                  · 024 locals not named like types (+ local consts at comptime, scope isolation)
                  · 025 design review (thesis, principles; implemented: cancel, Test effect, rest
                    params, named args/inferable params, universe w/o ops, named effect sets)
                  · 026 effect syntax `needs A + B`, `needs not` (foreign only), no `..E`
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-s08-2026-10-08..12.md (compacted + lessons) · s11-2026-10-14-p1-tooling.md
                  · s12-2026-10-14-todo-p2.md · s13-2026-10-15-bughunt.md
                  · s14-2026-10-15-docsweep.md · s15-2026-10-15-vision.md · s16-2026-10-16-needs.md
                  · s17-2026-10-16-release-prep.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu,
  macos, windows) + Safe + cross + editors + benchmarks. Use source::canonicalize, never
  Path::canonicalize (Windows \\?\ paths).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- Effects: `fn() -> T needs Alloc + Out`; set args `Sched(Out + Clock)`; pure C fn `needs not`.
- `print("{} {}", a, b)`: values are rest args. Ops qualified: `Clock.now()`. Inferable type
  params by name, first: `new(T: Page, ---)`. Tests: `ok(x)`, no `try`.
