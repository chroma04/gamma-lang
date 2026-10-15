# 024 Locals may not have the name of a type (2026-10-15, user's call)

- Rule (§4.6, §21.1): no local binding (var, local const, param, pattern/loop binding, nested
  fn name) may share the name of a type visible there: predeclared types incl. every sized
  int name (u1..u65535, i1..i65535 — so `i1`, `u2`, `i24` are unavailable as variable names),
  `string`/`cstring`/`rune`, top-level types/aliases/generic types, type params, local aliases.
  Other top-level/predeclared names (functions, constants, effect ops like `now`) may still
  be hidden. User suggested forbidding instead of implementing hiding-in-type-position.
- Implementation: FnCx::declare → Sema::names_type (ty_expr.rs): type_params, then the
  module decl judged syntactically (decl_is_type / syntax_is_type, Done state if resolved),
  never resolving — so no evaluation cycles from a name check. Field-access aliases
  (`T :: m.U`) are not recognized as types (minor gap).
- Same session: Sema.local_consts (Untyped/Const bindings in scope, truncated with scopes)
  make local constants visible to compile-time expressions; enter_module_scope /
  leave_module_scope (decls.rs) hide body scopes (type_params, value_scope, local_fns,
  local_consts) during resolve_decl, check_fn_body and on-demand handler clauses — this
  also fixed a real leak (generic body's `T` replacing a top-level `T` in a lazily resolved
  decl).
- DeclValue::EffectInst(EffectRef) for `Bytes :: Yield(u8)`; `E(T).op` / alias `.op` go
  through FnCx::perform_op_as (explicit instantiation, no inference).
