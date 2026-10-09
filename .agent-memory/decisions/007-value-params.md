# 007 Compile-time value params + sema-time const eval (M5)
- Sema-time evaluation: `Sema::eval_tir_const(expr)` (sema/consteval.rs) runs interp::eval_const
  on the in-progress Program after making sure every fn reachable from the expr has a body
  (checks pending bodies on demand, produces generic instances on demand; a fn currently being
  checked → error "depends on itself"). Used for typed array lengths, value args; later `when`,
  @static_assert, typed const folding.
- Value params reuse the type-param machinery: each gets a ParamId; `Sema.value_params:
  HashMap<ParamId, Ty>` (pid → value's type) tells them apart. Argument = a "type-level value"
  `TyKind::Value(ConstKey)` so instance keys / subst maps / Adt args stay `Ty`-based.
  `TyKind::Value` is never the type of an expression; Display prints the value.
- In bodies: name → `tir::ExprKind::ValueParam(pid)` (type = declared type); mono replaces it with
  a literal from the ConstKey (Int/Bool/FnRef/Null/ConstBytes).
- `[N]T` with N a value param → `TyKind::ParamArray { len: Param(pid) ty, elem }`; subst turns
  it into `Array`. Only a bare param name is allowed as symbolic length (no `N + 1`).
- Supported value types: plain data without pointers (bytes key) and fn pointers/null.
  Strings etc. → "not supported yet" (print's template is a builtin special case).
