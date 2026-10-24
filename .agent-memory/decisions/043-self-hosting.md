# 043 Self-hosted compiler `gammac` in compiler/ (s38, 2026-10-24)

User asked: "lay the groundwork for a self-hosted compiler; doubly the ultimate dogfooding test".

## Decisions
- Location `compiler/` (package `gammac`, gamma.toml), modules mirror src/: source/, diag/,
  bignum/, syntax/ (token, lexer, literal, dump in one module). main.gm = CLI.
- **Parity contract**: every stage byte-identical to the Rust reference on its stable dump
  (tokens / ast dump / check diags / program output) + diagnostics in JSON format (has all
  fields: spans, labels, notes, fixes; keys sorted alphabetically like serde_json BTreeMap).
  Corpus = every .gm in tests/conformance, lib, examples, bench, compiler (~980 files).
  Reference changes on purpose → change gammac in the same PR.
- tests/selfhost.rs: builds gammac (Debug mode, checks on; as fast as Fast here) via C backend (common::c_backend_available)
  else `gamma run compiler` (17 s); reference computed in-process (driver::Session,
  sources.add(rel path) so paths match); multi-file mode `==> path <==` headers on both
  streams; reports first differing line. Also `gamma test compiler` + formatted check.
  Both plain and `--recover` (lex_recovering) lanes.
- Conventions: arena per compilation (main.gm `lex_file`); OOM traps via `x!`; input errors
  are diagnostics; std only. Messages built with str.format(...)!.
- Exact literal values: own bignum (Nat = List(u32) limbs; Rational reduced by cancelling
  2s/5s only, since denominators are powers of 2 or 10). mul_pow chunks by largest power
  in a limb (10^9) so 1e100000 stays fast.
- Stage plan (docs/self-hosting.md): 1 lexer DONE, 2 parser + ast dump, 3 modules/packages,
  4 checker (diags of `gamma check`), 5 C back end reusing runtime/, 6 bootstrap fixed point
  (stage2 C == stage3 C).

## Dogfooding findings (s38)
- VM crash: `x!` on `E!()` → exec.rs `extract` with size 0 indexed past regs. Fixed
  (early return), test run/unwrap_unit_result.gm. Nothing tested `r!` on a unit result before.
- Papercuts (not fixed, candidates): `?u8 in ('a','b')` is a type error while `?u8 is 'a'`
  works (wrote `(x else 0) in (...)`); no slice `.contains` (use `x in s`); no List.last();
  `while true` + returns needs no trailing return (good) but a trailing one is "unreachable".
- Perf: native gammac lexes 2.4 MB in 0.8 s (~3 MB/s), far slower than Rust; profile later.
