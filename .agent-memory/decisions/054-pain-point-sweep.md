# 054 Pain-point sweep (s50, 2026-10-27)

User asked: for every pain point, discard what is done / not an issue, design + implement
the rest. Triage result lives in spec-notes/pain-points.md ("Settled"). Designs:

- L1 `else` join (§10.4): fallback U that doesn't coerce to T, but T→U only adds const →
  the LEFT side is coerced to ?U / E!U (check/errors.gm else_join) so the TIR `.Else` stays
  homogeneous; no back-end change. Numeric widening is NOT lifted (`?u8 else u32` errors):
  lifting non-free coercions under `?` would hide a branch.
- L2 const-adding coercions inside `?`/`!` (§6.4 row): check/coerce.gm `adds_const`;
  emitted as `.PtrToPtr` (cgen reinterprets same-layout values, interp copies bytes, eval
  returns the value). Nullable pointers keep `.InOptional`.
- L3 arrays of one element type join as `[]const T` (§7.12): expr.gm array_elem_join;
  temporaries held by the existing §15.1 rule. `for` over a tuple now iterates at the join
  (seq.gm tuple_elem_join), elements coerced (arrays of a place-tuple slice in place).
- L4–L8, S1–S6: delegated in s50 (see pain-points.md list); outcomes recorded below.
