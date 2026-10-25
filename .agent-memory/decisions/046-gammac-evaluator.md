# 046 — gammac's compile-time evaluator (s41)

- Values are STRUCTURED (tir/value.gm: Int bits masked to width as u128, Float f64, Agg,
  Ptr = Place{cell, path}, Slice, variants…), not bytes like the reference's ConstValue.
  Byte views only where needed (`.bits` reinterpret, ops.gm put/get_bytes, little-endian
  only; big-endian → unsupported). Reason: far simpler in Gamma; differential output only
  needs the dumped TIR + diagnostics, never the bytes.
- Tree-walking over TIR (eval/eval.gm), control flow as `Flow` error union. Step budget
  ticks at calls and loop back-edges (same points as the VM) so "step budget" traps match.
- Evaluator is its own module (compiler/eval) because check's shared namespace clashed.
- sema → check dependency inverted by a hook: `Sema.eval_hook` (set by check.install in
  main) checks + evaluates a const expr during declaration resolution; ConstDefs live in
  Sema.consts keyed by (span, ty) (const_ids), so TIR `Const(name, span)` finds its cell.
- Lazy: fast-path consts registered Unchecked, evaluated on first use (const_value).
- prepare (eval_prepare.gm) checks reachable fn bodies on demand (pending bodies, generic
  instances via produce_instance), mirroring prepare_for_eval; cycles → reference messages.
- Immutable locals (local_const) follow opportunistic_const: diags reset on check failure;
  Blocked / step budget → runtime local; unsupported diags are kept.
