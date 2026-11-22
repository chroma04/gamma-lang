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
- Windows: Safe (Fil-C is Linux-only), `gamma debug`, gdb printers are Linux-only. ARM64
  Windows: builds, CI job added s84 (gotchas/platforms.md). (Quoting is
  done: runtime gm_win_cmdline + rt_test.c; cache root made 0700 on POSIX only, Windows
  relies on %LOCALAPPDATA%'s ACL.) Windows-only code paths (spawn, install.ps1,
  gm_fs_open_dir) are exercised only by Windows CI.

## 2. Performance (measure first; callgrind recipe in docs/contributing.md)
Empty: s85 (decisions/077) did or dropped every follow-up. Leftovers noted there (payload
copy for `return Some(local)`, aggregate straight into a wrapping optional) are small; do
them only with a profile that shows them.

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
