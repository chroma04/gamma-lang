# M9 — Modules, packages, test runner
directory modules, `import` (relative, std, packages via gamma.toml), visibility (§3.3),
cycles, `test` declarations and `gamma test` with test handlers (§12.9: leak-checking alloc,
captured output, fake clock, fixed-seed rand, in-memory fs, loopback net), expect.

## Status
Done: dir modules (load_dir_module, module_by_dir keyed by canonical dir; Module.dir keeps the
path as written for messages), imports (relative within package root, std via $GAMMA_STD or
lib/std, deps from gamma.toml `[dependencies] x = { path = "…" }`), cycle check (DFS over
import_edges, reported on the closing edge), `test` items (sema/tests_decl.rs: E from first
`try` via FnCx.infer_test_error, fallback re-check as fn() with diags truncated), prelude
run_test/run_test_unit with tracking allocator (leak check), `gamma test` (captured output,
interp::run_entry), harness `stage: test` + `*.test` dirs.
Not yet: fake Clock/Rand/Fs/Net/In test handlers (those effects don't exist yet), test filter
flag, tests of imported modules (only root module's tests run), lib/std doesn't exist.
