# Changelog

All notable changes to the Gamma language, its standard library and the `gamma` tool.
Versions and what each kind of change means are described in
[`docs/versioning.md`](docs/versioning.md).

## Unreleased

## 0.1.0 - 2026-10-16

The first release: the complete language of `gamma-spec.md`, implemented on two
back ends that give the same results.

- **Language**: algebraic effects and handlers for allocation, I/O, iteration, early
  exit and fibers, with effect sets written `needs A + B` (named sets and effect-set
  parameters included); generics with type, compile-time value and effect-set
  parameters, inferred or named; rest parameters; compile-time evaluation, `when` and
  local constants; optionals and error unions; modules and packages; `test` and `bench`
  declarations, with failing a test as the `Test` effect; C interoperability (`foreign`,
  `#export`, `#callconv`, inline assembly, atomics).
- **Interpreter** (`gamma run`, `gamma test`): functions run as register bytecode, with
  a tree-walking fallback; foreign C functions are called through libffi.
- **C back end** (`gamma build`, `--backend=c`): build modes Debug, Checked, Fast and
  Safe (memory-safe through Fil-C, Linux only); debug information with gdb views and
  pretty printers (`-g`); cross-compiling with `--target=`.
- **Platforms**: Linux and macOS (x86_64 and arm64), and x86_64 Windows with a
  MinGW-w64 C compiler (gcc or clang).
- **Standard library**: the core library and the `std/` modules listed in
  [`docs/std.md`](docs/std.md), including files, networking, threads, fibers, text,
  JSON, hashes and collections. Additions follow the admission policy of §22.
- **Tooling**: `gamma fmt`, `gamma doc` (API documentation in Markdown or HTML),
  `gamma repl`; `gamma test` and `gamma bench` with filters, parallel jobs, timing and
  line coverage; packages with path, git and archive dependencies, version
  requirements, `gamma.lock` and build profiles with C sources and libraries
  (`gamma new`, `init`, `add`, `remove`, `fetch`, `update`, `cache`); the language
  server `gamma lsp`; a VS Code extension and TextMate and tree-sitter grammars;
  prebuilt binaries with install scripts for every platform above.
