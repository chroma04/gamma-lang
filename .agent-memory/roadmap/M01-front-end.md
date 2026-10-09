# M1 — Front end: lexer, parser, AST dump
Status: see INDEX.md.
Scope: full §2 lexer (incl. newline rules, match bodies), full Appendix A parser
producing `syntax::ast`, `gamma parse --dump` S-expression dump, error recovery.
Exit criteria:
- Every code example in the spec parses (ported into tests/conformance/parse/).
- Golden dumps for focused constructs; error tests for parse-time precedence errors.
- No panics on truncated inputs (fuzz-style unit test).
