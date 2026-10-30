# 023 Tooling round (s11): packages, LSP, editors, release
## Packages (src/pkg/, guide ch.15)
- git CLI via std::process, no libgit2; crates toml_edit (parse + format-preserving add/remove),
  semver (Cargo VersionReq; bare `1.2` = ^1.2), sha2.
- Store: <cache_dir()>/git/db/<name>-<16hex> bare mirrors, checkouts/<name>-<16hex>/<rev12>/
  (cache disabled → <root>/.gamma/deps). Lock = gamma.lock at root package, deterministic.
- Identity (normalized URL, subdir); ONE rev per repo for the graph; conflicts are errors.
  Checksum (sha256 over sorted regular files, path NUL len contents) verified on checkout creation.
- Compiler commands call pkg::prepare → Session.packages (Resolution: canonical root → name →
  dir); sema read_manifest prefers it, else path deps; git dep without it → "run gamma fetch".
- A dep's root module is named after the dependency (import() passes the name).
- LSP uses prepare with offline+locked (never network, never writes the lock).
## LSP (src/lsp/, src/ide.rs, src/sema/ide/)
- No hooks in checking: analyze_for_ide = analyze_tests pipeline (+ Sema.keep_going past syntax
  errors) then reads tables + TIR (facts.rs) and re-walks the AST with its own scopes (walk.rs,
  items.rs). Normal compile path untouched.
- Unit = module directory; Session.overlays for unsaved buffers; catch_unwind → retry strict →
  "internal compiler error" diagnostic. serde_json only (no LSP crates).
- types::mark/release_since (unsafe) frees types allocated by one analysis; only run_stdio uses
  it (single-threaded; EffectId::splice pre-created). If a new process-global cache holding Ty/
  AdtId/EffectId/ParamId is ever added, it must be created before the mark or the LSP breaks.
## Editors (editors/)
- tree-sitter external scanner mirrors §2.8; generated src/ committed (CLI 0.25.10 pinned);
  CI regenerates + diffs. tree-sitter test highlight assertions inside trailing comments make
  0.25.10 allocate 4 GiB (killed a runner) — don't add such assertions.
## Release
- Tag vX.Y.Z (must equal compiler/gamma.toml version; main.gm + lsp.gm VERSION must match,
  tested by cli/version_is_the_compiler_package_version; bump editors/* too) → release.yml; archives are install prefixes;
  std found at <exe>/../lib/gamma/std. Runner labels ubuntu-22.04-arm, macos-15-intel unverified.
