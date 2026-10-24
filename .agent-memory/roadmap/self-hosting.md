# Self-hosting roadmap (decisions/043; public doc docs/self-hosting.md)
State (s38b): stage 1 lexer + stage 2 parser done; parity (dump + JSON diags) on all ~980
.gm files, plain and --recover. gammac modules: source, diag, bignum, syntax (tokens,
lexer, token dump), ast (tree + dump), parse (parser), main.gm (lex|parse CLI).
Port took ~1 session by hand (no subagents): 1:1 file mapping to src/syntax/parser/*.rs
made it mechanical; matched on first full-corpus run.

## Stage 3 done (s38c): `gammac modules` == `gamma modules` on ~785 program roots.
Gaps: git/url deps, malformed-manifest messages, general `when` conds (decisions/043).

## Next: stage 4 checker (biggest)
- Split into sub-stages with their own parity dumps; candidates: (a) name resolution of
  top-level decls + type expressions (dump resolved decl kinds/types?), (b) const eval,
  (c) function bodies → TIR. Needs a reference dump for each (like `gamma modules`):
  design a `gamma check --dump-decls` (types of every top-level decl) first.
- Keep: interpreted lane (found a C backend bug in s38c).
