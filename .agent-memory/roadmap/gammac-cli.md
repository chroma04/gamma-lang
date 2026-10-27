# gammac: porting the rest of the `gamma` tool (s45, user: "expand the gammac suite to
cover the rest of gamma functionality")

Reading agreed: the commands/flags/modes of `gamma` that gammac lacks; each port is
checked against the reference byte for byte (stdout, stderr, status) in tests/selfhost.rs.

## Inventory (s45 start) and status
| Area | Rust | Status |
| --- | --- | --- |
| check, run (via C), help, version, human diag format (default) | main.rs, diag.rs | DONE s45 (me): check 804/804 human |
| fmt | fmt.rs + fmt_command | DONE s45 (W-fmt): 1088 files + 16k mutated inputs match |
| test / bench (native path only) | testing.rs, analyze_package_tests | DONE s45 (W-test): 37 conformance + std + compiler match |
| lint + fix (+ diag codes) | lint/, sema/lint*.rs, linting.rs, fix.rs | DONE s45 (W-lint): 804 roots json+human, 188 fix copies |
| doc | doc/ | DONE s45 (W-doc): ~1000 runs match |
| gamma.toml build settings: [build]/[profile.*] mode, debug-info, cflags, ldflags, c-sources, include-dirs, libs, lib-dirs; --profile | pkg/build.rs, manifest.rs | DONE s45 (W-pkg) |
| package manager: new/init/fetch/update/add/remove/cache, git/archive deps, lock, --offline/--locked, --package | pkg/ (3.9k lines) | DONE s45 (W-pkg): compiler/pkg/, load/manifest+semver; 5 selfhost tests |
| -g (#line, frame names, views, gdb/lldb printers) | cgen/debug.rs, views.rs | DONE s45 (W-debug); printers moved to runtime/ |
| --target cross, wasm32-wasi, freestanding objects, --mode=safe (Fil-C) | toolchain.rs, cgen/wasi.rs | DONE s45b (W-cross): load.Target run-time value; driver/toolchain.gm, target.gm; Fil-C blob kinds added by me after CI Safe failure |
| CFLAGS/LDFLAGS/CC env | toolchain.rs | DONE s45 (W-pkg) |
| runtime object cache, run exe cache | rtcache.rs | DONE s45b (W-cross): driver/rtcache.gm; no package C-source obj cache |
| C optimizations (spec, borrow, outline, chain, constarg) | cgen/ | DONE s45b (W-opt): same C functions modulo numbering; gammac emits only used consts |
| lsp | lsp/ (2.5k) | DONE s45b (W-lsp): compiler/lsp + compiler/ide; Loader overlays/keep_going; stdout via /dev/stdout |
| --coverage | coverage.rs, testing.rs | DONE s45d (C back end in both, decisions/050) |
| run --backend=interp | interp/ | DONE s46 (decisions/051): 476/476 run tests; FFI/asm programs run natively |
| repl | repl/ | DONE s46 (W-repl): tests/repl.rs sessions match (selfhost repl_matches_the_reference) |
| test/bench --backend=interp | testing.rs | NOT YET (usage error); next candidate |
| --backend=cranelift | clif/ | OUT of scope for gammac (user: Rust-specific; native codegen far future) |

## Notes
- `gammac run` passes the temp exe path as argv[0]; gamma passes the source path. Needs a
  spawn option for argv[0] (SpawnOptions has none: runtime ×3 + interp + clif). Logged in
  spec-notes/pain-points.md; no run test depends on it.
- check_program(s, root, tests: bool): tests only for `tir` of a root without main (and
  `test`/`bench`); check/build/run report "the root module of a program must declare `main`".
- Driver code: compiler/driver/ (options.gm Options/Command/Mode, paths.gm std/core/runtime
  dirs, native.gm build/run/compile_units/run_cc/exit_code); main.gm = arg parsing +
  load_program dispatch; diagnostics of checking are rendered BEFORE the back end runs.
- Survey tools in $SP/tools (roots.py → roots.txt = program_roots; cmp_check.sh).
- Worktree subagents may start on a STALE base (1c948ee): tell them to `git reset --hard <branch>` first.
- W-pkg gaps: `fix` does not call prepare_packages (gamma's does); `test`/`bench` do not
  resolve package deps (gamma's testing.rs gets the Resolution); lint/fix ignore
  --offline/--locked; manifest `mode = "safe"` errors in build/run; edit.gm edits text lines
  (matches toml_edit on ~20 layouts). `--package=` for test is a usage error.
- s45b (user: "finish the rest save for repl and coverage"): PR #88. Selfhost lanes
  GAMMAC_TEST_LANE=s390x|wasi|safe (test programs_compiled_by_gammac_in_a_lane...) run in
  the s390x/WASI/Safe CI jobs. Local tools: s390x gcc + qemu + wasi-libc + binaryen via
  apt (sudo ok), wasmtime in ~/tools; Fil-C install needs root setup.sh (classifier
  refused) → Safe only verified in CI.
- Known gaps: lint/fix --target; Windows cmdline quoting; 0700 cache dirs; package C
  object cache; lsp traps end the server (no ICE diagnostic); UB programs under forced
  checked/fast modes exit differently (double_free 139 vs 134) — UB, ignored.
