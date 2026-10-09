# Gamma implementation — agent memory INDEX (read first)

## State (session 1, 2026-10-08/09) — PR chroma04/gamma-lang#1 open, subscribed, CI green
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
- M8 fibers done in sema, interpreter and C (roadmap/M08 status, decisions/010).
- M11 mostly done (roadmap/M11 status): all ambient effects with system + test handlers,
  std io/fs/fmt/math/thread/arena/fixed_buffer/c (docs/std.md).
- Core modules (is_prelude=true) may: use @heap_*, see prelude privates, use `is`/@hash on params.
- @stack_size done (decisions/011); core sorts are iterative (fixed 64-entry stacks).
- Structs by value over the C ABI done (mirror C types, decisions/009).
- @asm done (§19.5 specified: pairs of (constraint, ^T place) / (constraint, value)).
- #callconv done (M10 status). Elementwise array arithmetic done.
- #fast_math/#deprecated/#inline checks done (M12 file).
- Wide ints in C done; spreads (§11.4) done.
- `if … else e =>` captures done (if_expr lowers last branch to a Match).
- Generic nested fns done (declared via decls::declare_fn with a synthetic Item; Sema.nested_generic).
- Qualified consts in patterns done (pattern.rs const_pattern_value).
- Consts with temporaries done (wrapped in a `<constant>` fn); `[2]P(..)[1]` hoisting (hoist_prefix).
- @write, @prefetch done; literals keep own effects in FnDef.effects (C non-plain); effect params = types (spec).
- Not yet: native `gamma test`; std/fmt for wide ints.
- Threads done (M10 status): std/thread, interp GIL (src/interp/threads.rs), C TLS + locks.
- Build modes: `--mode=debug|checked|fast` (Session.mode → target.mode, interp, cgen).
- Trap locations point at the innermost frame (prelude line for double free); no backtrace yet.

## Next steps
1. Remaining sema gaps (list above), then M12 polish (guide review, diagnostics).
2. C-backend gaps (M07 status); M12 polish (docs/guide review, diagnostics).
3. Keep PR green; CI runs on push.

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
