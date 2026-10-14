# 022 Effect-set parameters

Problem: std/fiber tasks could perform only `Sched`, because `spawn`'s entry is a fn pointer
whose effect set is fixed. Options weighed: ambient effects (unsound with fn pointers), generic
operations (unsound / heavy), effect variables in value types (too big). Chosen: a new kind of
compile-time parameter, `E: effects`, monomorphized like a type parameter.

## Shape
- Declared on fns, effects, structs/unions, handler decls. Spliced into effect sets as `..E`
  (`EffectRef` whose id is the pseudo-effect `EffectId::splice()` named "..", arg = Param).
- Values: `TyKind::Effects(EffectSet)`; literal `\ {A, B}` (parser: Backslash in primary →
  `ExprKind::EffectSet`). A single effect is also accepted as an argument.
- Substitution: map args then `EffectSet::flatten` (replaces `..E` once E is `Effects(s)`).
- Omitted / unbound effect params become empty sets (`Sched` == `Sched(\ {})`), so old code works.
- Inference (`match_effects` in sema/effects.rs): first match effects named once on both sides,
  then bind one `..E` to whatever actual effects the pattern does not name.
- Positional explicit args: `denotes_effects` decides whether an argument targets an effect param
  (EffectSet literal, call, effect param ident, effect decl name). Otherwise it is skipped so
  `fiber.spawn(Job, worker, …)` binds `worker` to `f`, not to E.
- Soundness rule (§16.6): performing `..E` inside a `with` that handles any effect is an error
  (E might contain it), same spirit as the Yield(T) vs Yield(u32) rule.
- Errors: E used as a type; bare `E` in a set (must be `..E`).

## std/fiber
`Sched(E)`, `Scheduler(E)`, `scheduler(E)`, `handler(E) Sched(E) for ^Scheduler(E) \ {..E}`,
`entry(E)` used as fn pointer. Tasks: `\ {Sched(E), ..E}` or fewer. Test run/fiber_task_effects.gm,
check/effect_set_params.gm. Docs: std.md fiber, guide 10 + 12, spec §12.1, §16.1, §16.6, C.10.
