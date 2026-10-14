# 021 Language papercuts (s9)
- Whole tuple payload: `.Cat(c)` with a 2-tuple payload binds the tuple (one sub-pattern of any
  kind matches the whole payload).
- `&e` temp in out-of-order (named) args: bind_temp lifts AddrOfTemp via lift_temps so the temp
  lives for the whole call.
- `for x in (1, 2, 3)`: tuple with one element type iterates (seq.rs for_each_value).
- Block-bodied fn literals without `-> R` and without expected type infer the return type from
  `return`s (first fixes it, later coerce; none → `()`). Applies to nested `::` fns too; a nested
  fn referring to itself needs `-> R` (error). Inferred infer-var ret resolved in FnCx::finish.
- Generic fn applied only to compile-time args = instantiation → fn pointer (`double(u8)`), except
  when no required run-time param (then it's a call). Type args also inferred from an expected
  `^fn` type. Bare `g := f` of a generic stays an error. Code: generics.rs `is_instantiation`,
  `generic_fn_from_expected`.
- No `@fn_cast`: `.cast(P)` on fn pointers already existed; now checked (same callconv,
  variadic-ness, arity; each param/ret identical or both pointers incl. rawptr; effects free).
