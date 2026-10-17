# Changelog

All notable changes to the Gamma language, its standard library and the `gamma` tool.
Versions and what each kind of change means are described in
[`docs/versioning.md`](docs/versioning.md).

## Unreleased

### Added

- `gamma fix` applies the fixes that the compiler's diagnostics suggest, where a
  diagnostic has exactly one: `&&` and `||` become `and` and `or`, an unused value gets
  `_ = `, `s.len()` on a slice loses its `()`, `else` as a match pattern becomes `_`, a
  trailing comma after a match arm goes, a payload variant pattern `.V` becomes `.V(_)`,
  `x := v` at top level becomes `x :: v`, and an uppercase radix prefix is lowercased.
  It changes only the files of the input's package, and repeats until nothing is left
  to apply. The language server offers the same fixes as quick fixes (code actions),
  including both readings of the ambiguous `try x else d`, and `--diag-format=json`
  prints diagnostics with their locations and fixes as one JSON object per line.
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

- Faster `f32` and `f64` code on the interpreter: arithmetic, comparisons and branches
  on floats run as native bytecode instructions (a Mandelbrot loop runs about 8x faster),
  and so do `@rotl` and `@rotr` on integers of up to 64 bits (SHA-256 about 1.9x faster).
- Functions that call variadic C functions (`printf` and its relatives) run on the
  interpreter's bytecode VM instead of its slower tree-walker.
- The C back end passes a large argument read from an element of an array, slice or
  many-pointer by address instead of copying it first, as it already did for fields.
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
- Optimized C builds declare small functions `inline`, so gcc inlines them as Clang
  does: a loop adding 32-byte structs through a small function ran three times slower
  with gcc than with Clang and now runs as fast.
- A large argument read from a field of a variable or of a pointer's target (`f(m.a)`)
  is passed in place in C, as one read from the variable itself already was, instead of
  being copied to a temporary first.
- Generated C clears only the padding of a small struct, optional, result or union
  value it builds, instead of the whole value with `memset`, which Fil-C turns into a
  call: a Safe build's `new` + `destroy` loop runs about 13% faster.
- `tools/difftest` generates generic functions and structs, strings, effects with
  `fn`/`ctl`/`esc` handlers, and fiber tasks with channels; a CI job runs 300 seeds on
  every push.
- The interpreter performs an operation with less work (about 6% fewer instructions in
  a loop of handled performs).

### Fixed

- C back end: a call to a function that performs no effects itself but is passed a
  second-class function was not treated as a possible non-local exit. When the passed
  function performed an operation whose handler left its `with` (`break` after
  `cancel`), the caller went on running with an undefined result, and could print
  output the interpreter does not, before the exit took effect. Calls through function
  pointers with such parameters had the same problem.
- On macOS, a program run with `gamma run --backend=c` could be killed when another
  `gamma` process started the same cached executable at the same time: marking a
  cached file as used no longer opens it for writing outside Windows.
- In Debug builds, a trap in a task started by `@fiber` (such as a `std/fiber` task)
  named a different caller for the task on each back end; both now name the
  `with … on` that drives it.

- `bench/benches.gm` used an old form of `new` and `destroy` and no longer built, which
  the benchmark job of CI reported only in its summary; that job now fails when the
  benchmarks do not build or run.

- A conversion `T(x)` no longer pushes `T` into `x`'s branches and operands:
  `f32(if c { a } else { b })` with `a, b: i16` was rejected.
- `gamma check` no longer crashes on a constant whose initializer has a type error
  (`X :: "ab" < 5usize`).
- Generated C names each source file once rather than at every trap site, which makes
  very large functions faster to compile (a 100,000-term sum: 31 s to 27 s with gcc).
- Very long functions, such as a chain of thousands of checked operations, took hours
  to compile with Clang 19 and later (Apple clang 17 and later, the compiler of current
  macOS): a 50,000-link chain now takes about 35 seconds with Clang 20.
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
