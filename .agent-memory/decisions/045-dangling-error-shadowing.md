# 045 Dangling addresses become errors; no-shadowing stays (s40, 2026-10-25)

User approved (s40) after the pain-point flag (≥9 hits of `&loop_var`/`&binding` kept):
- §15.1 generalized: storing an address of a local where it CERTAINLY outlives it is an
  error — assignment to a place rooted at a local of an enclosing scope / an upvar / a
  `#global`, and returning it inside a value (bare `return &x` stays the older
  check_escapes message; the new check skips non-`inside` returned addresses to avoid
  double reports). Implementation: src/sema/lint_escape.rs `walk` computes findings with
  a `certain` flag (storage(): Deref and non-array Index/Slice are uncertain);
  `Sema::check_dangling_addresses` (run after check_escapes in typed_program,
  analyze_with, ide, lint) emits certain ones as errors; the lint keeps uncertain ones
  (through pointers, `push`/`put` heuristic). gammac mirrors in check/dangling.gm.
- Only true positive in the repo: run/example3_globals_threads.gm (`ptr = &x` of main) →
  now points at a `#global nine`.
- `[1, 2]`: parse error gets a note (`[_]i32(1, 2)`, tuple `(a, b)`) in both parsers;
  test parse/array_value_syntax.gm.
- NO-SHADOWING STAYS: the user stands by it on principle even if inconvenient. Do not
  re-flag it as a pain point.
