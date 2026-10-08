# Gamma implementation — agent memory INDEX (read first)

## State (end of session 1, 2026-10-08) — PR chroma04/gamma-lang#1 open, subscribed
- Pipeline works end to end: lexer → parser → sema → TIR → interpreter. `gamma run file.gm`.
- Done: M0–M3 (core data types, patterns, methods, closures), M4 (effects/handlers/with/for over
  iterators/handler decls/Env), M5 partly: generic fns + generic structs/unions (monomorphized,
  decisions/006), std iterators, Alloc effect + Allocator vtable + heap allocator (entry wrapper
  installs it when main performs Alloc), new/make/make_fill/free/destroy, List(T) — all in
  `lib/core/prelude.gm` (whole prelude checked every compile; `@heap_alloc/@heap_free` prelude-only).
- Interpreter covers whole TIR except fibers/foreign calls.
- Not yet: comptime value params `N :: usize` (§16.5), §16.6 distinctness, HashMap/@hash/map,
  sort builtins, ctl/fibers (M8), imports/modules/tests (M9), when/target (M6), C backend (M7),
  typed consts in array lengths, multiple assignment/destructuring stmt, `using` ptr coercion.
- Trap locations point at the innermost frame (prelude line for double free); no backtrace yet.

## Next steps
1. Finish M5: value params (needs symbolic array lengths), HashMap + @hash, sort builtins.
2. M3 leftovers (roadmap/M03) and M6 `when`/target/@static_assert.
3. M7 C backend is the big one after that; then M9 modules (imports needed for std).
4. Keep PR green; CI runs on push.

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria)
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- `print` takes one args value: several arguments go in a tuple `print("{} {}", (a, b))`.
