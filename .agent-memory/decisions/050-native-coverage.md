# 050 — Coverage on the C back end (s45d)

User (s45d): "Go ahead for coverage. Cranelift was a Rust-specific fancy and native code
generation is a far, far future goal. We should definitely keep an interpreter in mind."
So: coverage on C (both compilers); Cranelift stays out of scope for gammac; a gammac
interpreter (for repl) is a future multi-session item — keep designs interpreter-friendly
(e.g. coverage counts live behind `Builtin.Cover`, any back end can implement it).

- `Builtin::Cover(id)` → C `GM_COVER(id)` (relaxed atomic add on `gm_cover_counts`).
  Test `main` calls `gm_cover_init(n, argc > 2 ? argv[2] : NULL)` when instrumented
  (n = number of counters: Rust walks the program, gammac keeps `Sema.cover_counters`).
  atexit dumps one decimal per line (trap exits via exit(134) → still dumped; a killed
  timeout test writes nothing). Path via argv not env so GAMMA_RUNNER (qemu/wasm) is fine.
- Runner: file `coverage-J` per test in the temp dir; sums with wrapping add.
- Reference fix: counted fns were "name not starting with `<`" → dropped `<fn>` literals
  and `<for body>` clauses. Now: generated = `<…>` except those two; tests/benches and any
  fn whose span lies inside a test body are excluded. Generic TEMPLATES are counted (Rust
  prog has their bodies), so unused generics show 0 — gammac does the same.
- gammac instruments after `check.prepare_codegen(entries + export_fns)` (all bodies,
  incl. instances produced lazily); second prepare inside generate_program is a no-op.
- Parity verified: whole `compiler` package (29,953 lines) identical report + LCOV;
  selfhost `testing_matches_the_reference` has coverage jobs + LCOV compare.
- gammac canonicalize doesn't resolve symlinks (documented in docs/self-hosting.md).
