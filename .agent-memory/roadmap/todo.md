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
  Leftover papercuts from it: see bugs-open.md "Dogfood papercuts".

## P2 — worth doing early
- Fuzzing: cargo-fuzz on the parser; random-program differential (interp vs gcc vs clang);
  tree-sitter as a second parser.
- Tree-sitter scanner: a block comment containing a line break must count as a newline
  (§2.8); scanner.c only handles newlines it sees directly (not verified by running).
- bugs-open #48–49 (gcc on huge functions / deep expressions).
- Perf: Safe new/destroy ~4x Checked; interpreter `perform` cost.
- Cargo.toml metadata: license (MIT), description, repository, rust-version.
- GitHub issue templates and SECURITY.md (.github has only workflows).
- Windows follow-ups: Safe, `gamma debug`, gdb printers (Linux-only); ARM64 Windows.
- Editors: Zed extension (none shipped), Emacs untested.
- Library tests runnable from a dependency root (bugs-open #30).

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
