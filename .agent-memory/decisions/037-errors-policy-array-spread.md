# 037 Errors policy (spec ch. 23), array spread, `+` vs `,` (s33, 2026-10-23)
User asked four nitpicks; outcomes:
- `needs A + B` vs `where Ord(T), Eq(U)`: KEPT both. Effect set = one value (nameable
  `Io :: Fs + Alloc`, passed as one arg `Sched(Out + Clock)` where `,` would split args);
  where = list of independent predicates. Recorded as a C.8 row.
- Array spread: `..a` for `[N]T`/`#simd` = unnamed tuple of N elems everywhere a tuple spread
  goes (calls, generic calls, rest args, templates, tuple/array/vector construction). Not
  `[E]T` (positions only on request, §5.6.1) nor ParamArray (unknown N; checked once).
  Destructuring arrays (`a, b := arr`) deliberately NOT added: spread is the one bridge.
  Impl: construct.rs spread_array/array_element/elements_tuple; ast::ExprKind::Elements
  (synthetic, never parsed) made by pack_rest_args for a lone rest spread; generics.rs
  generic_call_spread emits synthetic `tmp[i]` AST args. `T(arr)` for tuple T → "spread it".
- Errors policy (ch. 23): every compile error is a spec rule; lints never errors; compile
  warnings only #deprecated; hints only from program + spec; foreign notation allowlist
  (==, !=, &&, ||, ! as not, = in cond, 0-octal); no confusable tables; limits table 23.5
  (parser MAX_DEPTH raised 64→256 internal units ≈ ≥64 user levels; checked 240 parens /
  120 ifs fine on interp, C, Cranelift).
- Audit applied: removed `else =>` arm fix, `\` effect message + Backslash token; `= ` →
  `is` hint only after if/while cond; `#allow` unknown rule moved from sema to `gamma lint`
  (error there); limit messages cite §23.5; runtime-symbol message = limitation; tuple
  construction no cascade "missing element" after a failed spread.
- Left as is (judged compliant): line-final comma fix, keyword-as-name "rename it" note,
  column-1 declaration recovery for unclosed brackets, "not supported yet" errors (worded as
  limitations; real gaps: consteval.rs 152-186, ty_expr.rs 524/751, call.rs 788, expr.rs 137/1547).
