# 022 Effect-set parameters

Problem: std/fiber tasks could perform only `Sched`, because `spawn`'s entry is a fn pointer
whose effect set is fixed. Options weighed: ambient effects (unsound with fn pointers), generic
operations (unsound / heavy), effect variables in value types (too big). Chosen: a new kind of
compile-time parameter, `E: effects`, monomorphized like a type parameter.

## Shape
- Declared on fns, effects, structs/unions, handler decls. A plain term of an effect set
  (`needs E + Out`, s16 syntax, decisions/026) is an `EffectRef` whose id is the pseudo-effect `EffectId::splice()` named "..", arg = Param).
- Values: `TyKind::Effects(EffectSet)`; an argument is a `+` expression `A + B` (sema
  `resolve_effect_arg` + `effect_terms`), a single effect, a named set or another param.
- Substitution: map args then `EffectSet::flatten` (replaces a term `E` once E is `Effects(s)`).
- Omitted / unbound effect params become empty sets (`Sched` == Sched of the empty set), so old code works.
- Inference (`match_effects` in sema/effects.rs): first match effects named once on both sides,
  then bind one term `E` to whatever actual effects the pattern does not name.
- Inferable effect params (named in another param's type) are given by name only (s15 rule).
- Soundness rule (§16.6): performing `E` inside a `with` that handles any effect is an error
  (E might contain it), same spirit as the Yield(T) vs Yield(u32) rule.
- Errors: E used as a type, a value, or a single effect (`handler E {…}`).

## std/fiber
`Sched(E)`, `Scheduler(E)`, `scheduler(E)`, `handler(E) Sched(E) for ^Scheduler(E) needs E`,
`entry(E)` used as fn pointer. Tasks: `needs Sched(E) + E` or fewer. Test run/fiber_task_effects.gm,
check/effect_set_params.gm. Docs: std.md fiber, guide 10 + 12, spec §12.1, §16.1, §16.6, C.10.
