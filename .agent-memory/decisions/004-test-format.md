# 004 Conformance test format
Documented in tests/conformance.rs header. `.gm` files under tests/conformance/<area>/;
`//! stage: lex|parse|check|run`, `//! dump` (golden .tokens/.ast), `//! exit: N`,
`<name>.stdout` golden for run, `//~ ERROR substr` / `//~ WARNING substr` inline annotations
(same line as reported span start). Harness runs the binary as a subprocess with
`--diag-format=short`. BLESS=1 rewrites goldens; GAMMA_TEST_FILTER=substr filters.
Every unannotated error/warning fails the test.
