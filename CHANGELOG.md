# Changelog

All notable changes to the Gamma language, its standard library and the `gamma` tool.
Versions and what each kind of change means are described in
[`docs/versioning.md`](docs/versioning.md).

## Unreleased

### Added

- `editors/zed/`: a Zed extension for Gamma (highlighting, outline, indentation and the
  language server).
- `examples/`: runnable programs, from command-line tools (`wordfreq`, `grep`,
  `sha256sum`, `json_query`) through effects, allocators, fibers, threads, TCP and C
  interop to a multi-file calculator and a package. The test suite runs each of them
  on both back ends.
- `gamma test --package=<name>` (and `gamma bench`) runs the tests of a dependency, with
  the revisions the package's lockfile holds for it and its own dependencies.
- `SECURITY.md` and GitHub issue forms for bug reports and language proposals.
- `Cargo.toml` metadata: description, license, repository and the minimum Rust
  version (1.95), which CI now checks.
- `fuzz/`: cargo-fuzz targets for the lexer, the parser, the formatter and the type
  checker (see `docs/contributing.md`).
- `tools/difftest/`: a differential tester that generates random, well-typed programs
  and checks that the interpreter and the C back end (with gcc and Clang) agree.
- `std/fiber/channel`: a bounded channel between fiber tasks. A task that sends to a
  full channel or receives from an empty one is parked until another task changes it.
  Underneath, `std/fiber` gains `Waiters`, the `park` operation of `Sched` and `wake`,
  for other ways for tasks to wait for each other.
- An effect-set parameter that occurs in an effect of the function's own set
  (`fn(E: effects) needs Sched(E)`) is inferable: a call takes it from the instantiation
  of that effect available there, as an operation call does (§16.6). Helpers that tasks
  call no longer need their scheduler's effects spelled out. Like other inferable
  parameters it is given by name (`f(E: Out)`), no longer positionally.

### Changed

- Diagnostics: a literal that does not fit the type a conversion `T(x)` gave its
  variable points at that conversion; a missing method or field `.name` at the start of
  a line explains that the line continues the previous one; `else` as a `match` pattern
  suggests `_`; `---` on an array that is not plain data suggests `fill:`.
  `.Ok`/`.Err`/`.Some`/`.None` patterns explain how results and optionals are matched,
  without follow-on exhaustiveness errors; `str.starts_with` (and the other built-in
  slice methods called through a module) suggests `s.starts_with(…)`; an iterator
  passed where a value is expected (`enumerate(bytes(s))`) is one error instead of two.
- Performing one instantiation of a generic effect where only another is declared or
  handled (a task declared `needs Sched` spawned under `fiber.scheduler(Out)`) says
  that the two are different effects, instead of suggesting to declare the effect.
- A local may not take the name of a type alias that reaches into another module
  (`Handle :: fs.File`), as for any other type name (§4.6).
- `io.ReadError` is now the same type as `fs.ReadError`, so one function can propagate
  both with `try`.

### Fixed

- A conversion `T(x)` no longer pushes `T` into `x`'s branches and operands:
  `f32(if c { a } else { b })` with `a, b: i16` was rejected.
- `gamma check` no longer crashes on a constant whose initializer has a type error
  (`X :: "ab" < 5usize`).
- Generated C names each source file once rather than at every trap site, which makes
  very large functions faster to compile (a 100,000-term sum: 31 s to 27 s with gcc).
- Very long expression chains compile much faster with gcc and Clang. A 100,000-term
  sum took gcc over 3 minutes and Clang many minutes; both now take under 30 seconds.
  A 100,000-link method chain took gcc 8 s and Clang 13 s, and now takes 4 s and 9 s.
- `gamma fmt` glued a line-initial `. name` after a `{` into `.name`, which changed the
  indentation when the output was formatted again; the space is now kept.
- A path dependency such as `../json` was not found when the input was a bare file name
  (`gamma run main.gm` in the package directory): the leading `..` was dropped.
- The `std/net` documentation called `connect` and `close_socket` without `Net.`.
- `r is !.V` with a variant `V` that has a payload was rejected; it now tests the
  variant, as `u is .V` does (§7.6).
- The tree-sitter grammar treats a block comment that contains a line break as a line
  break (§2.2).

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
