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
- `[N]T` with N a value param → `TyKind::ParamArray { len, elem }`; subst turns it into `Array`.
  `len` is `Param(pid)` or a computed `TyKind::LenExpr{op: LenOp, lhs, rhs}` (operands Param/
  Value/LenExpr; usize only; plain + - * / % << >> & | ^). `Ty::len_expr` folds Value⊕Value
  (trap → Error) and sorts commutative operands by Ty index: equality = structural, so
  `[A+B]`≡`[B+A]` but `[N+N]`≠`[2*N]`. Built by `Sema::array_len` (ty_expr.rs, `len_ty` converts
  check_const_expr TIR). `.len` → `FnCx::len_value` rebuilds TIR (folded by mono).
  `Sema.len_exprs` (Ty, span) are checked per instantiation (`check_len_exprs`, in
  produce_instance + instantiate_adt) → "overflows or divides by zero with `N` = 0".
  A param used only inside a LenExpr is not inferable (generic_call_with `inferable`);
  an inferred binding may itself be a LenExpr (normalize_value accepts it for usize params).
- Supported value types: plain data without pointers (bytes key) and fn pointers/null.
  Strings etc. → "not supported yet" (print's template is a builtin special case).
