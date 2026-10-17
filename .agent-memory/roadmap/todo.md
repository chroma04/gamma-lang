# To-do list toward and after v0.1.0 (agreed with the user in s17, 2026-10-16)
Replaces the user's older list (assessed at PR #34). Delete items when done; keep priority order.
Beyond the repo (not ours): package registry, website, hosted playground, Homebrew, macOS
signing/notarization, VS Code Marketplace/Open VSX publishing (user's tokens), Linguist and
nvim-treesitter submissions.

## P0 — blocks tagging v0.1.0
- Merge the s17 PR (#40). Done in it: main CI checked green, Release workflow run by hand on
  the branch (all 5 builds incl. first Windows run, smoke tests green), CHANGELOG folded into
  `## 0.1.0 - 2026-10-16` (adjust the date if tagged later), release notes from CHANGELOG.
- User: tag the merge commit `v0.1.0`, push the tag; check install.sh / install.ps1 on clean
  Linux, macOS and Windows machines.

## P1 — should land before the tag
- Done in s18 (PR #41): `examples/` (19 programs, tests/examples.rs) = the dogfood pass.
  Its small papercuts were fixed in s19; the rest: bugs-open.md "Dogfood papercuts".

## P2 — worth doing early
(s21: gcc inline of small fns, field args in place, Emacs mode, difftest coverage + CI.
 s19 did: tree-sitter comment newline, Cargo metadata + MSRV job, issue forms + SECURITY.md,
fuzz targets, `gamma test --package=<dep>`, Zed extension, long-chain C compile time.
s20: fiber channel, trap-path dedup, check fuzz target + differential tester.)
- Fuzzing: fuzz/ targets parse, fmt, check (s20: ~345k execs, 1 const-eval panic fixed;
  ~25 exec/s/worker, loading std dominates; needs nightly, not installed by default).
  tools/difftest covers generics/strings/effects/fibers since s21, CI runs 300 seeds/run.
  s24: 1500 debug seeds (300000..301500) + checked-mode batch (400000..), 0 disagreements.
  Left: tree-sitter as a second parser.
- bugs-open #48 (C compilers on huge functions; chains done in s19, statement runs left).
- Perf: Safe new/destroy ~3.6x Checked (s21: memset → gap stores; rest is Fil-C cost of
  by-ref param copies and blob loads with pointers, see sessions/s21); interpreter `perform`
  (s21 trimmed it ~6%; vm_perform still ~200 instr self).
  s22: VM float ops + RotI; examples show no other generic-op hotspot.
- Windows follow-ups: Safe, `gamma debug`, gdb printers (Linux-only); ARM64 Windows.
- Editors: Zed extension (editors/zed, s19) untried in real Zed — publish to zed-industries/extensions
  is the user's. Emacs: editors/emacs/gamma-ts-mode.el (s21, ERT tests in batch Emacs 29.3;
  committed parser is ABI 15, Ubuntu's Emacs needs `generate --abi 14` — CI step regenerates).

## Nice — after 0.1.0
- Language: `@fields(T)` reflection (§22 deferred), SIMD types (C.10), enum-indexed arrays
  `[Enum]T` and bit sets, a `Send`-like rule for migrating fibers.
- Compiler: retire the tree-walker (s24: no run test falls back for a whole function except
  huge_local's 1e12-element array; left: statements with values > REG_VALUE_MAX run as
  TreeStmt, coverage builtin, const eval), smaller Debug C (overflow-trap and
  backtrace-site code ~8% each of gcc time; s25 split Debug C into parallel units, units.rs), a native
  back end (LLVM/Cranelift).
- Platforms: wasm32-wasi (no fibers/threads), FreeBSD, freestanding/embedded profile.
- Tools: more fix-its (s24 machinery + 8, s25 +7, s26 needs; rejected list in decisions/028), `gamma lint`,
  profiler docs (perf/Instruments on `-g` builds). LLDB formatters done (s26).
- Packages: several versions of one repository in a dependency graph.
- Library (each through §22): time zones, `std/process`, TLS via `std/c`, compression.
- Docs/project: Rosetta-style examples, "Gamma for C/Zig/Rust programmers", self-hosting.
