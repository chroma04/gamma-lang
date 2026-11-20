# Code map: agent-facing pointers
The full, maintained map is docs/architecture.md (a test requires it to name every
`compiler/` module). This file adds only what an agent needs on top of it.

- `compiler/` = `gamma` (package gammac, built Checked via compiler/gamma.toml). main.gm
  dispatches; driver/ (options.gm, flags.gm CLI table, native.gm build/run + unit policy +
  caches, toolchain.gm, target.gm, rtcache.gm). Pipeline: syntax/ → parse/ → ast/ →
  load/ (modules; core.gm loads lib/core from disk) → sema/ (declarations, layouts with
  generations) → check/ (bodies → TIR; see checker.md) → tir/ → eval/ (comptime, structured
  values, decisions/046) → interp/ or cgen/.
- interp/: code.gm lowers TIR, vm_compile.gm → instructions, bc.gm 16-byte Bc forms +
  CallRec/CallSite, vm.gm loop with in-loop calls, run.gm tree fallback (`with … on`, value
  loops), memory.gm page table, fiber.gm host coroutines (decisions/051, 056, 064, 067).
- cgen/: names.gm stable C names (`gm_f_<name>_<hash>`), units.gm split by owning file
  (+ the n-way split for `--emit-c`), shared inline small fns (SHARED_SMALL_BYTES),
  outline.gm/chain.gm for huge functions (decisions/048, 065, 068, 069).
- Tools: pkg/ testing/ fmt/ doc/ lint/ linting/ fix/ repl/ ide/ lsp/.
  - `gamma lsp` = lsp/supervisor.gm, which spawns `gamma lsp --worker` (the real server,
    lsp.command) and restarts it after a trap; test hook GAMMA_LSP_DEBUG_TRAP +
    request `gamma/debugTrap`.
  - `test/bench --backend=interp`: testing/run.gm re-runs gamma with the hidden flag
    `--run-test=j` per test (driver.run_interp_entry); flags with `hidden: true` stay out
    of the usage text.
- `lib/core` (prelude, list, map, slice, complex) may use core-only intrinsics (@heap_*,
  @alloc_tag/@alloc_check, @copy_elems), prelude privates, `is`/@hash on params.
- `bootstrap/seed/` = the build's C units (decisions/066), adopted by gamma0; `build.sh`,
  `verify.sh`; `tools/refresh-seed.sh`.
- `runtime/gm_rt.{h,c}`: C runtime; Debug extras (poison, 0xAA, backtraces) = spec §18.3.
- `tests/harness/`: the test harness (package harness, depends on gammac).
- Back ends: interpreter (`--backend=interp`) and C (gcc/clang, MinGW, Fil-C Safe,
  wasm32-wasi, freestanding); `run` defaults to C. Modes `--mode=debug|checked|safe|fast`.
- History: the Rust reference implementation was retired (tag `rust-reference-final`,
  decisions/052); Cranelift went with it. Decisions before 043 name Rust files (src/*.rs);
  gammac's modules mirror them one to one.
