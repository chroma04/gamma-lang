# Checker internals (compiler/check, compiler/sema): operational notes
Contributor-facing design is in docs/architecture.md; these are the non-obvious mechanics.

- FnCx (check/func.gm) = one OUTERMOST function body. Nested literals/clauses use
  enter_fn/exit_fn, which swap per-frame state (locals, ret, declared, performed, labels,
  clause, handled) into `outer`. Scopes are shared; `scope_depths` gives the owning frame
  depth; lookup_local_at → (binding, depth); cross-frame references → Upvar(levels).
- Nested FnDefs are collected in `nested` and finalized with the same infer table at finish.
- Literal inference: unresolved coercion placeholders + a pending list; obligations are
  checked in finalize.gm; `failed_coercions` dedups errors. FnCx.settle resolves a literal
  variable early where the spec (§6.1) lists sites that need it (@type_of, atomics, print
  args, `with &x`, expect_eq, literal-only generic args).
- Effects: recorded in `performed`, checked against `declared` at finish; literal clauses/
  closures push their non-own effects to the enclosing frame on exit.
- Patterns: pattern.gm (PatCtx refutable/irrefutable/is-test; const_pattern_value),
  matching.gm (match, binding conditions, `is`), exhaust.gm (usefulness + witnesses,
  integer range splitting).
- Struct construction binds temps only when named args are out of field order.
- Spreads (§11.4): bind_args (ArgBinder) puts every arg through a temp in written order
  once any spread is present; finish_call hoists an indirect callee / method receiver.
  spread.gm: spread_parts expands tuples/structs; spread_generic_args evaluates spreads of
  generic calls into `<argN>` locals and rewrites the args to `<argN>.k`.
- `if v := r {…} else e => d` becomes Match{Ok(p) => then, Err(e) => d}.
- `[2]P(…)[1].y`: hoist_prefix rewrites the AST so the prefix applies to the construction.
- Generic nested fns: fnlit.gm builds a synthetic item and declares it; Sema.nested_generic
  maps FnId → name so recursion works.
- Generic handler declarations: HandlerTemplate (check/handler_decls.gm); clauses are generic
  clause fns; handlers_of_type matches templates, instantiates, caches in
  Sema.handler_instances; a concrete decl for the same effect wins.
- Consts needing temps are wrapped in a `<constant>` FnDef + call; comptime evaluation goes
  through Sema.eval_hook (check/consts.gm, eval_prepare.gm ensure_body checks reachable
  bodies on demand and reports cycles).
- Monomorphization rewrites a clone of each generic body per instance (program.gm
  mono_expr/mono_fn), depth limit 256.
- Gotcha hit 7x while writing the checker: a bare name at an optional position below the
  top of a pattern matches only present values; bind with `?x` (decisions/047).
