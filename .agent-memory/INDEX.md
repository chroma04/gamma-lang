# Gamma implementation — agent memory INDEX (read first)

## State (session 1, 2026-10-08)
- Pipeline works end to end: lexer → parser → sema → TIR → interpreter. `gamma run file.gm`.
- Done: M0, M1 (front end), M2 (hello world), most of M3 (structs/unions/tuples/arrays/slices,
  optionals/results, patterns + exhaustiveness, methods/builtins, closures + nested fns).
- Interpreter (src/interp, delegated) supports the whole TIR except fibers/foreign calls/Alloc.
- In progress: M4 effects in sema (generic effects + clause frame plumbing landed; handler
  literals, `with`, esc ops, `for` over iterators NOT yet). ClauseInfo has #[allow(dead_code)].

## Next steps
1. M4: sema/effects.rs — perform generic ops (infer instantiation; §12.1 amended), handler
   literals (TyKind::Handler(id), clause frames via enter_fn/exit_fn + ClauseInfo), `with`
   (subtract handled effects from `performed` since body start), break/continue/return in
   clauses (Target::InstallingWith / Label{levels} / Return{levels}), handler declarations,
   `for` over iterator calls (lower to With + HandlerLit; bind args to temps first), Yield in
   prelude, standard iterators (need generics → M5 or builtin special-casing).
2. M3 leftovers: typed consts via interp::eval_const (array lengths), `using` ptr coercion,
   #bits ops, top-level/statement `when` (M6), multiple assignment, destructuring.
3. M5 generics.

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria)
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
