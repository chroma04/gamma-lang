# Open work, in priority order (delete items when done; finished work goes to git log)
Outside the repo and the user's to do: package registry, hosted website/playground,
Homebrew, macOS signing, VS Code Marketplace/Open VSX and Zed extension publishing,
Linguist/nvim-treesitter submissions, enabling GitHub private vulnerability reporting.

## Always
- Keep `CHANGELOG.md` `## Unreleased` current (0.2.0 is the last release section).
- New diagnostics follow spec ch. 23 (kinds, what is an error, limits in §23.5).
- Regenerate `docs/std` after std changes: `gamma doc lib/std --all -o docs/std`.

## 1. Tooling gaps
- `test --backend=interp`: each test process re-checks the program (~1 s for the
  compiler package; a shared checked-program cache, or fork on POSIX, would remove it).
  Foreign-code tests already run natively (testing/run.gm `Fallback`).
- Windows: Safe, `gamma debug`, gdb printers are Linux-only; ARM64 Windows. (Quoting is
  done: runtime gm_win_cmdline + rt_test.c; cache root made 0700 on POSIX only, Windows
  relies on %LOCALAPPDATA%'s ACL.) Windows-only code paths (spawn, install.ps1,
  gm_fs_open_dir) are exercised only by Windows CI.

## 2. Performance (measure first; callgrind recipe in docs/contributing.md)
- Compiler: decisions/069 "Not done" (regrow_raw tail, small memcpy in extend, lexer
  ~430 Ir/token) and 068 (parser levels copy `Abort!ast.Expr` 4–5x; a cgen pass building
  results in place would help all generated code). Release binaries: single-unit
  (`GAMMA_C_UNITS=1`) `--mode=fast` build not adopted (cannot test the macOS lane here).
- Interpreter: decisions/064 "Not done" + 067 "Open candidates": push cost (~190 Ir),
  dispatch (~19 Ir/instr; ip pointer, superinstructions), generic builtins (hash, heap),
  stack arena for addressed locals, lazy frames for defer/with bodies; `with … on` and
  value loops still run on the tree evaluator.
- Generated C size: map growth per (K,V), sort per T (decisions/058); smaller Debug C
  (overflow traps and backtrace sites ~8% each of gcc time).
- Safe mode new/destroy ≈ 3.6x Checked (Fil-C malloc/free 3.7x glibc; indirect clause
  calls; decisions/032).

## 3. Testing and robustness
- Fuzzing: sema fuzz targets + more differential coverage (tools/difftest); tree-sitter
  as a second parser.
- `@stack_size` not verified against `cc -fstack-usage`.
- Editors: Zed extension untried in real Zed; Emacs mode tested in batch only.

## 4. Language and library (only with evidence; check spec §22 for std additions)
- spec-notes/pain-points.md "Open": template code size (checked template values).
- Vector follow-ups if asked: lane sqrt, two-input shuffles, lane trunc/round to ints.
- Interop gaps: const relocations of plain fns stored at effectful fn-pointer types;
  errno-style TLS foreign globals.
- Platforms: FreeBSD, a freestanding/embedded profile. Docs: Rosetta-style examples,
  "Gamma for Zig programmers".

## Small cleanups (only when touching the code)
- check: generic_call_packed's per-argument loop; write_expr's big match.
