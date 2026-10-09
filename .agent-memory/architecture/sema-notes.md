# Sema internals (operational notes; contributor-facing design is in docs/architecture.md)
- FnCx (func.rs) = one *outermost* function body; nested literals/clauses use enter_fn/exit_fn
  which swap per-frame state (locals, ret, declared, performed, labels, clause, handled) into
  `outer: Vec<SavedFrame>`. Scopes are shared; `scope_depths` gives owning frame depth;
  lookup_local_at → (binding, depth); cross-frame refs → ExprKind::Upvar{levels}.
- Nested FnDefs are collected in `nested` and finalized with the same InferTable at finish().
- Literal inference: Coercion::Unresolved placeholders + `pending` list; obligations checked in
  finalize. `failed_coercions` dedups errors.
- Effects recorded in `performed` (EffectRef, span); checked against `declared` at finish.
  Literal clauses/closures push their non-own effects to the enclosing frame on exit.
- Patterns: pattern.rs (PatCtx Refutable/Irrefutable/IsTest), matching.rs (match, binding conds,
  `is` pattern detection), exhaust.rs (usefulness + witnesses, int range splitting).
- Struct construction binds temps only when named args are out of field order.
- Prelude: lib/core/prelude.gm (Out, Err). Builtin fns print/eprint/trap/assert special-cased
  in call.rs; universe types in builtins.rs.
- TIR additions vs original brief: UntypedFloat, Coercion::Unresolved (sema-only), PatKind::Or,
  Builtin::First/Last/RangeLen, TyKind::Handler(u32), ParamId names.
- Interp quirks: `#c_union` Aggregate writes offset 0; InOptional inferred from types; Else
  infers rhs mode by type; Range `in` lowered in sema (seq.rs) so interp's ADT-name path unused.
- Spreads (§11.4): `bind_args` returns (vals, stmts); with any spread every arg goes through a
  temp in written order; callers wrap via `finish_call(pre, call, receiver)` which also hoists an
  Indirect callee / method receiver. `spread_parts` (construct.rs) expands tuples/structs.
  Generic calls: `generic_call_spread` evaluates spreads (and non-trivial earlier args) into
  named locals `<argN>` and rewrites the AST args to `<argN>.k` / `name: <argN>.name`.
- `(T1, T2)(…)` tuple-type construction: classify_callee checks Tuple + looks_like_type.
