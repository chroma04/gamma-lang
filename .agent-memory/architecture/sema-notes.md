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
- `if v := r {…} else e => d`: if_expr turns the last branch into Match{Ok(p) => then, Err(e) => d}.
- Generic nested fns: fnlit::nested_generic_fn builds a synthetic Item and calls
  decls::declare_fn; Sema.nested_generic maps FnId → name so check_fn_body declares it (recursion).
- Qualified consts in patterns: pattern.rs const_pattern_value evaluates to a literal.
- Consts needing temps: check_const_expr wraps them in a `<constant>` FnDef + Call.
- `[2]P(…)[1].y`: expr::hoist_prefix rewrites the AST (prefix applies to the construction).
- Non-capturing literal with own effects: FnDef.effects gets them (so cgen isn't plain).

## Generic handler declarations (s3)
- `handler(T: type, N: usize) E for X` → HandlerTemplate (handler_decl.rs); clauses are generic
  Clause fns (FnSig.generics; instantiate keeps FnKind::Clause). handlers_for(&mut, t) matches
  templates with match_params, instantiates clauses, caches in Sema.handler_instances; a
  concrete decl for the same effect wins. Generic bodies get placeholder decls with Param
  args; mono_expr re-runs handlers_for on the substituted type.
