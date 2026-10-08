# Gamma implementation — agent memory INDEX (read first)

## State (session 1, 2026-10-08) — PR chroma04/gamma-lang#1 open, subscribed, CI green
- Pipeline works end to end: lexer → parser → sema → TIR → interpreter. `gamma run file.gm`.
- Done: M0–M4; M5 mostly: generic fns/structs/unions (decisions/006), compile-time value params
  on fns and types incl. fn-pointer/string/plain-data args (decisions/007), sema-time const eval
  (sema/consteval.rs: typed array lengths, value args), Alloc/Allocator/heap allocator,
  new/make/free, core modules core/list + core/map (only List/list/HashMap/map/map_with
  re-exported; methods resolve in receiver's module), HashMap in Gamma, @hash, field privacy.
- Core modules (is_prelude=true) may: use @heap_*, see prelude privates, use `is`/@hash on params.
- Not yet: §16.6 distinctness, sort builtins, make_aligned, runes, ctl/fibers (M8),
  imports/modules/tests (M9), when/target/@static_assert (M6), C backend (M7), multiple
  assignment/destructuring stmt, `using` ptr coercion, struct patterns with generic type names.
- Trap locations point at the innermost frame (prelude line for double free); no backtrace yet.

## Next steps
1. M6: `when`, target constants, @static_assert/@compile_error/@location (use eval_tir_const).
2. M3/M5 leftovers above; then M7 C backend (big) and M9 modules (imports needed for std).
3. Keep PR green; CI runs on push.

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria)
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
                  · 007 value params + sema-time const eval
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- `print` takes one args value: several arguments go in a tuple `print("{} {}", (a, b))`.
