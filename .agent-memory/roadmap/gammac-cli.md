# gammac: porting the rest of the `gamma` tool (s45, user: "expand the gammac suite to
cover the rest of gamma functionality")

Reading agreed: the commands/flags/modes of `gamma` that gammac lacks; each port is
checked against the reference byte for byte (stdout, stderr, status) in tests/selfhost.rs.

## Inventory (s45 start) and status
| Area | Rust | Status |
| --- | --- | --- |
| check, run (via C), help, version, human diag format (default) | main.rs, diag.rs | DONE s45 (me): check 804/804 human |
| fmt | fmt.rs + fmt_command | DONE s45 (W-fmt): 1088 files + 16k mutated inputs match |
| test / bench (native path only) | testing.rs, analyze_package_tests | W-test |
| lint + fix (+ diag codes) | lint/, sema/lint*.rs, linting.rs, fix.rs | W-lint |
| doc | doc/ | W-doc |
| gamma.toml build settings: [build]/[profile.*] mode, debug-info, cflags, ldflags, c-sources, include-dirs, libs, lib-dirs; --profile | pkg/build.rs, manifest.rs | W-pkg (with packages) |
| package manager: new/init/fetch/update/add/remove/cache, git/archive deps, lock, --offline/--locked, --package | pkg/ (3.9k lines) | W-pkg |
| -g (#line, frame names, views, gdb/lldb printers) | cgen/debug.rs, views.rs | todo (subagent) |
| --target cross, wasm32-wasi, freestanding objects, --mode=safe (Fil-C) | toolchain.rs, cgen/wasi.rs | todo |
| CFLAGS/LDFLAGS env, runtime object cache, run exe cache | toolchain.rs, rtcache.rs | todo (small) |
| C optimizations (spec, borrow, outline, chain, constarg) | cgen/ | todo; speed only |
| lsp | lsp/ (2.5k) | todo, late |
| repl, --coverage, --backend=interp/cranelift | interp/, clif/ | OUT of scope (need the Rust interpreter / Cranelift); tell user |

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
