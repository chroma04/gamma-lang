# Open work, in priority order (delete items when done; finished work goes to git log)
Outside the repo and the user's to do: package registry, hosted website/playground,
Homebrew, macOS signing, VS Code Marketplace/Open VSX and Zed extension publishing,
Linguist/nvim-treesitter submissions. (Private vulnerability reporting: enabled.)

## Always
- Keep `CHANGELOG.md` `## Unreleased` current (0.3.0 is the last release section; start
  `## Unreleased` above it with the first change after it).
- New diagnostics follow spec ch. 23 (kinds, what is an error, limits in §23.5).
- Regenerate `docs/std` after std changes: `gamma doc lib/std --all -o docs/std`.

## 1. Tooling gaps
- Windows: Safe, `gamma debug`, gdb printers are Linux-only; ARM64 Windows. (Quoting is
  done: runtime gm_win_cmdline + rt_test.c; cache root made 0700 on POSIX only, Windows
  relies on %LOCALAPPDATA%'s ACL.) Windows-only code paths (spawn, install.ps1,
  gm_fs_open_dir) are exercised only by Windows CI.

## 2. Performance (measure first; callgrind recipe in docs/contributing.md)
s77 (decisions/072) measured every item of the old list: kept the cheap wins, closed the
rest with numbers. What is left needs a structural change; do it only if asked:
- LIR follow-ups (decisions/075 "State after P4", plan done s83): C blocks per statement
  run in short lists (stack reuse); nested helpers in split long functions. (Range-check
  elision done s84: 346 of 1,090 slicing checks in the compiler removed.)
- cgen: build results in place (068: `Abort!ast.Expr` copied 2–3x per parser level).
- Template sharing follow-ups (decisions/073): forward a template to another template
  parameter (kit.check → str.format, cgen linef → str.format); thunks without the
  generic's unused locals.

## 3. Testing and robustness
- Fuzzing: more differential coverage (tools/difftest: new constructs in its generator).
  (Targets: parse, fmt, check = front end, lower = + prepare_codegen + cgen (s75; the 961
  examples/conformance programs pass through it), manifest, archive.) Tree-sitter as a
  second parser: done s84 (CI editors job parses 300 difftest programs per run; 2,200 seeds
  checked locally, all clean).
- Editors (need a desktop, the user's): Zed extension untried in real Zed; Emacs mode
  tested in batch only (CI).

## 4. Language and library (only with evidence; check spec §22 for std additions)
- Platforms: FreeBSD (no FreeBSD machine or CI here). Embedded (32-bit MCU) profile:
  BLOCKED on 32-bit targets: `gamma build --target=thumbv7em-none-eabi` /
  `riscv32-unknown-none-elf` with clang 18 stops at the driver's `__int128` check
  (native.gm). Needs a software i128/u128 in cgen (arith, bits, helpers, consts, expr: ~50
  uses) and gm_rt (~37 uses), e.g. a two-word struct with helper functions when the C
  compiler lacks `__int128`; then a freestanding harness test on thumbv7em/riscv32 (qemu
  not installed). x86_64/aarch64 freestanding already works (tests/harness/freestanding).

## Small cleanups (only when touching the code)
- check: generic_call_packed's per-argument loop; write_expr's big match.
