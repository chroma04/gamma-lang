# 039 Complex numbers and math vectors (s35, 2026-10-23)

User asked which Odin features fit Gamma; verdicts agreed with the user:
complex YES, small math vectors YES (narrow), SOA maybe later (fixed arrays/slices only,
only `xs[i].f` a place), matrices NO (library on #vec), quaternions NO (library).
Rejections recorded in spec C.8. User: "work on complex numbers and vectors first".

## Design (spec §5.3.1, §5.6.3, §7.4.7, §21.3, C.2, C.8, §19.1 table)
- `Complex(T)` = core struct `{ pub re, im: T } where Float(T)` in lib/core/complex.gm,
  re-exported (CORE_MODULES). No imaginary literal (`2i` vs `2i8`), no `complex()` helper
  (alias `C64 :: Complex(f64)` + construction by name). Methods conj, abs (scaled hypot).
  Transcendentals in std/complex (f64 only, like std/math).
- `#vec [N]T`: ArrayKind::Math, N in 2..=4, T any integer/float, concrete. Layout [N]T,
  C-compatible as a field (cabi pieces treat Math like Plain). Not Ord. No array methods;
  dot/cross/norm/min/max/abs. Conversions V(a), [N]T(v) (Bits), W(v) lossless per element.
- Scalars: `*` either side, `/` on the right, for both. No `v + 1` (C.8). #simd keeps its
  no-broadcast rule (a splat is an instruction; not revisited).
- Complex `/` = Smith's algorithm; `*` plain formula (no Annex G recovery: GCC's
  `__muldc3` call would break P2). abs() not necessarily correctly rounded.
- Float(T) now implies Neg(T) (code: generics.rs implies(); spec §16.3 table) — needed by
  core/complex, and true anyway.

## Implementation (src/sema/mathvec.rs)
- #vec operators expand in sema to Block{let temps; Array([scalar op per element])} using
  arith_op per element → back ends see only scalars; trap spans are the operator's.
- Complex operators lower to calls of private `op_*` fns of core/complex via
  `complex_call` (binds checked args to declared `<argN>` locals, AST Ident args,
  generic_call — same trick as generic_call_spread). Floats never trap, so call spans
  don't matter. Cranelift doesn't inline → a call per op there (accepted).
- Hooks: expr.rs binary_op (lhs compound → rhs checked with no expectation; arith arm:
  rhs compound → scalar-left), neg_expr/bit_not_expr, methods.rs collection_method,
  construct.rs conversions, vector.rs vector_type accepts `#vec`.

## Delivered
std/complex (lib/std/complex: arg, from_polar, exp, log, sqrt, pow, sin, cos; Annex G
deviations in its //! doc), conformance tests, guide 04-types, examples nbody/julia.

## Follow-ups (not done)
- tree-sitter corpus case for `#vec` (CLI not installed in s35).
- SOA (`#soa`), lane names `.x .y .z .w` / swizzles (rejected for now: two ways to index).
- Inline complex ops in Cranelift (perf), formatting `Complex` as `1+2i` (no: structural).
