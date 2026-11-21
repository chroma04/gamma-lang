# Open work, in priority order (delete items when done; finished work goes to git log)
Outside the repo and the user's to do: package registry, hosted website/playground,
Homebrew, macOS signing, VS Code Marketplace/Open VSX and Zed extension publishing,
Linguist/nvim-treesitter submissions, enabling GitHub private vulnerability reporting.

## Always
- Keep `CHANGELOG.md` `## Unreleased` current (0.3.0 is the last release section; start
  `## Unreleased` above it with the first change after it).
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
s77 (decisions/072) measured every item of the old list: kept the cheap wins, closed the
rest with numbers. What is left needs a structural change; do it only if asked:
- Lowered IR (decisions/075; user approved the detour, P5/LLVM dropped). P0–P2 done
  (s79–s81: the interp runs everything from LIR, no tree evaluator). NEXT: P3 passes
  (bounds-check elimination), P4 cgen on LIR (075 "State after P1 + P2", follow-ups).
- Interp: push without copying constants (consts as immediates in Bc) and without the
  frame memset (needs proof registers are written before read); `with` re-resolves its
  perform site per ctx; remove the dead Machine.temps/kill_temps.
- cgen: build results in place (068: `Abort!ast.Expr` copied 2–3x per parser level).
- Template sharing follow-ups (decisions/073): forward a template to another template
  parameter (kit.check → str.format, cgen linef → str.format); thunks without the
  generic's unused locals.

## 3. Testing and robustness
- Fuzzing: more differential coverage (tools/difftest); tree-sitter as a second parser.
  (Targets: parse, fmt, check = front end, lower = + prepare_codegen + cgen (s75; the 961
  examples/conformance programs pass through it), manifest, archive.)
- Editors: Zed extension untried in real Zed; Emacs mode tested in batch only.

## 4. Language and library (only with evidence; check spec §22 for std additions)
- Vector follow-ups if asked: lane sqrt, two-input shuffles, lane trunc/round to ints.
- Spec: §18.4.2 "compatible type" is undefined (Safe = reinterpret bytes; interp = bytes;
  decisions/074 TBAA note). Define it, likely as layout/bits-based, with the tests.
- Platforms: FreeBSD, a freestanding/embedded profile. Docs: Rosetta-style examples.

## Small cleanups (only when touching the code)
- check: generic_call_packed's per-argument loop; write_expr's big match.
