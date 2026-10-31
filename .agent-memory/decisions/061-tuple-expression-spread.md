# 061 Spreads in tuple expressions (s60, 2026-10-31)
User asked: is generalizing spread to `(a, b, ..xs)` and `[_]T(a, b, ..xs)` worth it?
- Arrays: ALREADY worked (array constructors are calls, so §11.3/§11.4 apply: mixed
  elements + spreads of tuples or `[N]T`; slices/ParamArray/`[E]T` rejected). No change.
- Typed tuple construction `(i64, i64, i64)(1, ..t)` already worked; only the bare tuple
  literal rejected `..` ("expected an expression"). DECIDED: add it. Rationale: makes
  "the argument list is a tuple" (§11.3) literally true (tuple expr = arg list without a
  callee), a generalization not an addition; gives `(..a)` array→tuple and named-tuple
  extend/update without spelling the type (matters in generic code). Evidence of demand
  was weak (0 uses of typed-tuple spread outside tests), so the cheapness decided it.
- Semantics (§5.9 paragraph, §11.4): expand spreads in order; result must be fully named
  or unnamed; expected type → each element coercion site (spread parts coerced at the end
  when arity/names fit); a later NAMED element replaces a spread-supplied one, keeping
  its position AND its type (`(..p, x: 5)` has p's type) — differs from typed
  construction only in that the literal has no type of its own. Explicit-then-spread
  of the same name, or two spreads → "element `x` is supplied twice" (one per spread).
  Tuple TYPES reject spreads (parser in type mode; ty_expr resolve_type).
- Impl: ast.TupleElem.spread flag + ast.has_spread; parse_paren; check/expr.gm
  spread_tuple + coerce_element (shared with tuple()); consumers that read tuple syntax
  skip spread tuples (looks_like_type, local_is_type, expr_to_pattern, inline `in`
  members → value path, @asm entries). Tree-sitter grammar `_tuple_element`.
- Not done (deliberately): rest patterns `(a, ..rest)` in destructuring (037 keeps spread
  as the one bridge).
