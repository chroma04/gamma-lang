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
