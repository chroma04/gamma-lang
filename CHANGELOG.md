# Changelog

All notable changes to the Gamma language, its standard library and the `gamma` tool.
Versions and what each kind of change means are described in
[`docs/versioning.md`](docs/versioning.md).

## Unreleased

### Added

- Windows (x86_64): the interpreter and the C back end (`gamma build`, `--backend=c`,
  `gamma test`) run on Windows with a MinGW-w64 C compiler (gcc or clang; MSVC's `cl` is
  not supported). The runtime has Win32 implementations of fibers, threads, the futex,
  files, sockets and time; releases include a Windows zip and `tools/install.ps1`.
  Conformance tests that are inherently POSIX are marked `//! os: unix`.
- `docs/versioning.md`: the versioning and stability policy, and how a release is cut.
- `gamma doc`: API documentation, as Markdown or HTML, from `pub` declarations, `///` doc
  comments and `//!` module comments (§2.2). The standard library reference in
  `docs/std/` is generated with it.
- `gamma repl`: an interactive read-eval-print loop on the interpreter.
- Build settings in `gamma.toml`: a `[build]` table and named `[profile.<name>]` tables
  (`--profile=`) with the build mode, `-g`, C flags, and C sources, include directories
  and libraries to link, which dependencies may ship too.
- Packages: archive dependencies (`{ url = "…tar.gz", sha256 = "…" }`, `gamma add --url`);
  `gamma cache dir` and `gamma cache clean`; checkouts are read-only; symbolic links are
  covered by the checksum (trees without them keep their checksum).
- Language server: references and rename across the modules of a package, checking past
  an unclosed bracket, definitions in the core library, unsaved new files.
- Standard library: `std/hash` (CRC-32, SHA-256), `std/json` (RFC 8259),
  `std/collections/ordered_map` (a B-tree), `thread.detach`; a guide chapter touring the
  library (`docs/guide/16-standard-library.md`).

### Changed

- **Breaking:** `HashMap.get_or_put` is removed; use `entry` (§21.5).
- **Breaking:** `WaitGroup.wait_all` is renamed `wait`.
- **Breaking:** `std/log`'s `ErrLog` no longer takes a `time` field and needs only `Err`;
  the timestamped logger is `StampedLog`, which also needs `Clock`.
- §22.4 says what "cryptography" excludes: fixed, unkeyed digests and checksums are
  admitted to the standard library.
- Diagnostics: a keyword used as a name is one error with a hint; a `struct` declared in
  a function body no longer causes follow-up errors; repeated identical diagnostics are
  reported once; "unreachable code" says which construct ends the control flow; a failed
  call no longer also reports its unused result.
- A panic in the compiler prints a short "internal compiler error" report and exits with
  status 101; the backtrace is printed only with `RUST_BACKTRACE` set.
- The interpreter runs `new` and `destroy` with the default allocator about 5 times
  faster, and calls and effect operations about 20–30% faster; `bench/` holds the
  micro-benchmarks.
- Safe builds run hash map inserts about 2.7 times and list pushes about 2 times faster:
  slices are read in place instead of copied, which under Fil-C costs a runtime call.

### Fixed

- A local type alias can be passed as the type argument of a generic function.

## 0.1.0 (not yet tagged)

The first release: the complete language of `gamma-spec.md`, implemented on two
back ends that give the same results.

- **Language**: algebraic effects and handlers (allocation, I/O, iteration, early exit,
  fibers), generics with compile-time and effect-set parameters, compile-time evaluation
  and `when`, optionals and error unions, modules, packages, `test` declarations, and C
  interoperability (`foreign`, `#export`, inline assembly).
- **Interpreter** (`gamma run`, `gamma test`): register bytecode with a tree-walking
  fallback; foreign C functions called through libffi.
- **C back end** (`gamma build`, `--backend=c`): build modes Debug, Checked, Fast and Safe
  (memory-safe through Fil-C, Linux only); debug information with gdb views (`-g`);
  cross-compiling with `--target=`.
- **Standard library**: the core library and the `std/` modules listed in
  [`docs/std.md`](docs/std.md).
- **Tooling**: `gamma fmt`; packages with path and git dependencies, version requirements
  and `gamma.lock` (`gamma new`, `init`, `add`, `remove`, `fetch`, `update`); the language
  server `gamma lsp`; a VS Code extension and TextMate and tree-sitter grammars; prebuilt
  binaries for Linux and macOS (x86_64 and arm64) with an install script.
