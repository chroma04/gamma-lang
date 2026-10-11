# Gamma implementation — agent memory INDEX (read first)

## State (2026-10-11) — PRs #1–#5 merged (#4 test fix, #5 session 4); main has everything
- The whole spec is implemented in both back ends (interpreter + C), except "Not yet". Every run
  test runs on both and must match (gcc and clang). Milestone files roadmap/M00..M12; decisions/.
- Sessions 1–3: see sessions/. Session 4: C big values by address, std/collections, qualified
  generic names + declaration aliases + re-exported modules, dogfood round 4 (example4_*) and
  its fixes (sessions/s04).
- GitHub Actions currently gets no runners (account-level); verify locally (fmt, clippy -D
  warnings, cargo test, CC=clang conformance) before pushing.
- Open bugs/usability list: roadmap/bugs-open.md; open design items: spec-notes/open-issues.md.
- Build modes: `--mode=debug|checked|fast`. Core modules (is_prelude) may use @heap_*, prelude
  privates, `is`/@hash on params.

## Next steps
1. Check CI on main once GitHub Actions gets runners again.
2. bugs-open.md leftovers (#23–#30); future: interp bytecode VM, symbolic array lengths.
3. lib/ must stay `gamma fmt`-formatted (tests/fmt.rs checks).

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria); bugs-open.md
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
                  · 007 value params + sema-time const eval · 008 C back end · 009 interop
                  · 010 fibers · 011 stack size · 012 handler specialization + local exits
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md · s02-2026-10-09.md · s03-2026-10-10.md · s04-2026-10-10.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- `print` takes one args value: several arguments go in a tuple `print("{} {}", (a, b))`.
