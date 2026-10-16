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
(s19 did: tree-sitter comment newline, Cargo metadata + MSRV job, issue forms + SECURITY.md,
fuzz targets, `gamma test --package=<dep>`, Zed extension, long-chain C compile time.
s20: fiber channel, trap-path dedup, check fuzz target + differential tester.)
- Fuzzing: fuzz/ targets parse, fmt, check (s20: ~345k execs, 1 const-eval panic fixed;
  ~25 exec/s/worker, loading std dominates). tools/difftest (s20): random programs, interp
  vs gcc vs clang, ~10k programs, 0 disagreements; found `T(x)` pushing T into branches.
  Left: tree-sitter as a second parser; difftest coverage of effects/handlers/fibers/
  generics/strings (generator in tools/difftest/src/program.rs); CI job running a few
  hundred seeds.
- bugs-open #48 (C compilers on huge functions; chains done in s19, statement runs left).
- Perf: Safe new/destroy ~4x Checked; interpreter `perform` cost.
- Windows follow-ups: Safe, `gamma debug`, gdb printers (Linux-only); ARM64 Windows.
- Editors: Zed extension (editors/zed, s19) untried in real Zed — publish to zed-industries/extensions
  is the user's; Emacs untested.

## Nice — after 0.1.0
- Language: `@fields(T)` reflection (§22 deferred), SIMD types (C.10), enum-indexed arrays
  `[Enum]T` and bit sets, a `Send`-like rule for migrating fibers.
- Compiler: retire the tree-walker (VM covers everything), smaller Debug C (overflow-trap and
  backtrace-site code ~8% each of gcc time), several C translation units in parallel, a native
  back end (LLVM/Cranelift).
- Platforms: wasm32-wasi (no fibers/threads), FreeBSD, freestanding/embedded profile.
- Tools: machine-applicable fix-its (`--diag-format=json`, LSP, `gamma fix`), `gamma lint`,
  LLDB formatters, profiler docs (perf/Instruments on `-g` builds).
- Packages: several versions of one repository in a dependency graph.
- Library (each through §22): time zones, `std/process`, TLS via `std/c`, compression.
- Docs/project: Rosetta-style examples, "Gamma for C/Zig/Rust programmers", self-hosting.
