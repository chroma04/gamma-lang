# 026 — Effect sets are `needs A + B` (s16, user request)

User disliked `\ {A, B}`: braces already mean "code/members". Options presented: bare `\ A + B`,
keyword clause, `;` context params in the parameter list, `fn[A](…)`. User picked the keyword
clause with `needs`, plus `needs not` for pure foreign functions.

Rules (spec §11.1, §12.3, §16.6, §19.2, A.7):
- `needs` is a reserved keyword; the clause is `needs T1 + T2`; terms are effect refs, named
  sets or effect-set params (all resolved by kind in sema; `..E` splicing is gone).
- `needs not` = empty set, parse error unless on a foreign member's own signature
  (`Parser::parse_foreign_fn_sig`) and must end the clause (`needs not Alloc` → error).
  AST: `FnSig.effects == Some(vec![])` ⇔ `needs not`.
- Effect-set arguments / named sets are ordinary `+` expressions (`ExprKind::Binary Add`):
  `Sched(Out + Clock)`, `Io :: Fs + Alloc + Err`. `sema::decls::effect_terms` flattens;
  `is_effect_sum` decides a top-level `X :: A + B` is a set (first term resolves to an
  effect/inst/set). `Log :: Out` stays an effect alias. Empty set has no written form outside
  foreign `needs not`; display shows `()` (types.rs `fmt_effects`), and trailing empty
  effect-set generic args are omitted when printing (`shown_args`).
- `\` still lexes; `parse_needs_opt` gives a migration error for `\ {…}`.
- Rejected: `uses` (too close to `using`), `with` (reads like installing a handler).
Migration was a regex script over ~600 files (Rust `format!` strings with `{{` escaping were
missed at first: tests/pkg.rs, long_expressions.rs).
