# Gamma implementation — agent memory INDEX (read first)

## State (session 2, 2026-10-09) — PR #1 merged; session-2 PR open on claude/gifted-hamilton-smqfae
- The whole spec is implemented in both back ends (interpreter + C), except "Not yet". Every run
  test runs on both and must match (gcc and clang). Milestone files roadmap/M00..M12; decisions/.
- Session 2: docs audit + dogfooding subagents (≈35 example_* conformance tests). Fixed their bugs:
  ptr arithmetic, escape checks (sema/escape.rs, also packed-field pointers), Signed⇒Num, #bits
  bitwise, T(c) in generics, effect ambiguity under `with`, handler types in generic instances,
  qualified effects `m.E`, local type aliases, Range values (prelude Range/RangeIncl), `else {}`
  fallback, ==/!= hints, test Env empty, free_aligned, signed zero consts.
- Debug backtraces on traps (interp: Trap.trace; C: gm_bt chain; //! stderr directive),
  stack overflow → trap even for huge frames (SIGSEGV altstack handler), //! mode: directive.
- Open bugs/usability list: roadmap/bugs-open.md. Not yet: static handler specialization (D.3).
- Build modes: `--mode=debug|checked|fast`. Core modules (is_prelude) may use @heap_*, prelude
  privates, `is`/@hash on params.

## Next steps
1. `gamma fmt` merged (src/fmt.rs, tests/fmt.rs; lib/ must stay formatted).
2. bugs-open.md leftovers (big-value copies in C). Then: static handler specialization (D.3)?
3. Keep PR #2 green (subscribed); CI runs on push. Update the PR description at the end of each chunk.

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria); bugs-open.md
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
                  · 007 value params + sema-time const eval · 008 C back end · 009 interop
                  · 010 fibers · 011 stack size
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md · s02-2026-10-09.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- `print` takes one args value: several arguments go in a tuple `print("{} {}", (a, b))`.
