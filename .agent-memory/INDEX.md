# Gamma implementation — agent memory INDEX (read first)

## State (session 1, 2026-10-08) — PR chroma04/gamma-lang#1 open, subscribed, CI green
- Pipeline works end to end: lexer → parser → sema → TIR → interpreter. `gamma run file.gm`.
- Done: M0–M4; M5 mostly: generic fns/structs/unions (decisions/006), compile-time value params
  on fns and types incl. fn-pointer/string/plain-data args (decisions/007), sema-time const eval
  (sema/consteval.rs: typed array lengths, value args), Alloc/Allocator/heap allocator,
  new/make/free, core modules core/list + core/map (only List/list/HashMap/map/map_with
  re-exported; methods resolve in receiver's module), HashMap in Gamma, @hash, field privacy.
- M6 mostly done (roadmap/M06 status): when, target module, module members `m.x`, consts
  evaluated at compile time, #fuel, static_assert/compile_error/location/type_of intrinsics.
- M9 mostly done (roadmap/M09 status): directory modules, imports + gamma.toml deps, cycles,
  `test` decls + `gamma test` (leak-checking, captured output), harness `*.test` dirs.
- M7 C backend works for all run tests (roadmap/M07 status, decisions/008): src/cgen + runtime/.
- M10 interop mostly done (roadmap/M10 status, decisions/009): foreign, #global, #export, std/c,
  atomics, volatile; #bits/#c_union in both backends.
- Core modules (is_prelude=true) may: use @heap_*, see prelude privates, use `is`/@hash on params.
- Not yet: §16.6 distinctness, sort builtins, make_aligned, runes, ctl/fibers (M8),
  std (M11), C-backend gaps (M07 status), threads/@asm (M10), multiple
  assignment/destructuring stmt, `using` ptr coercion, struct patterns with generic type names.
- Trap locations point at the innermost frame (prelude line for double free); no backtrace yet.

## Next steps
1. M8 fibers (interp needs separate stacks; C runtime context switch) — last big language piece.
2. M11 std (io/fs/fmt/… via foreign + Sys handlers); M3/M5 leftovers; C-backend gaps.
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
