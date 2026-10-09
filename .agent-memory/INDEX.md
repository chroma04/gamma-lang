# Gamma implementation — agent memory INDEX (read first)

## State (session 1, 2026-10-08/09) — PR chroma04/gamma-lang#1 open, subscribed, CI green
- The whole spec is implemented in both back ends (interpreter + C), except what is listed
  under "Not yet". Every run test runs on both and must match (gcc and clang).
- Milestone status files (roadmap/M00..M12) say what is done per area; decisions/ say how.
- Highlights by area: generics + value params (006/007); C back end incl. wide ints, mirror
  structs at the C ABI, stack checks (008, M07); fibers + @stack_size (010/011); interop:
  foreign, #export, #callconv, @asm, threads (009, M10); ambient effects + std (M11).
- Late language completions (this session): spreads §11.4, `if … else e =>`, generic nested
  fns, qualified consts in patterns, consts with temporaries, @write/@prefetch,
  elementwise arrays, #deprecated/#fast_math/#inline (see architecture/sema-notes.md).
- Spec amendments are indexed in spec-notes/amendments.md.
- Not yet: native `gamma test` (subagent working in a worktree; merge its branch when it
  reports), std/fmt hex/bin for ints >128 bits, static handler specialization (D.3, optional).
- Build modes: `--mode=debug|checked|fast`. Core modules (is_prelude) may use @heap_*, prelude
  privates, `is`/@hash on params. Trap locations: innermost frame; no backtrace yet.

## Next steps
1. Merge native-test work. Then M12 polish: guide review against the current language,
   diagnostics pass, README/docs accuracy; maybe std/fmt wide ints.
2. Keep PR green; CI runs on push. Update the PR description at the end of each chunk.

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria)
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
                  · 007 value params + sema-time const eval · 008 C back end · 009 interop
                  · 010 fibers · 011 stack size
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- `print` takes one args value: several arguments go in a tuple `print("{} {}", (a, b))`.
