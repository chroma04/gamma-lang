# Changelog

All notable changes to the Gamma language, its standard library and the `gamma` tool.
Versions and what each kind of change means are described in
[`docs/versioning.md`](docs/versioning.md).

## Unreleased

### Added

- Named effect sets: `Io :: \ {Fs, Alloc, Err}` names a list of effects, spliced into
  an effect set as `\ {..Io, Clock}`, and usable as the argument of an effect-set
  parameter (§12.3).
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
- Local constants (`K :: 4`, `Z: usize : 3`, `S :: f()` evaluated at compile time) can be
  used where a compile-time value is needed: array lengths, local type aliases,
  `@static_assert`, `when` and compile-time arguments.
- `Bytes :: Yield(u8)` names an instantiation of a generic effect; it can be used in
  effect sets and handler literals, and `Bytes.yield(x)` performs that instantiation.

### Changed

- **Breaking:** the universe scope holds the predeclared effects but not their
  operations, except `yield`: write `Clock.now()`, `Alloc.current()`, `Env.args()`,
  `Fs.open(path, opts)` (methods on files and sockets are unchanged). The types of `Fs`
  and `Net` (`File`, `OpenOptions`, `Socket`, …) are exported by `std/fs` and `std/net`
  instead, and the methods of `Instant` and `Duration` are reached only as methods. An
  unknown name that is one of these says how to reach it (§21.1).
- **Breaking:** `map` and `map_with` are renamed `hash_map` and `hash_map_with`.
- **Breaking:** rest parameters. The last parameter of a function may be declared
  `args: ..A`; it takes the remaining positional arguments as one typed tuple, and a spread
  passes one on (`print(template, ..args)`). `print`, `eprint`, `@write`, `List.write`,
  `std/str`'s `write` and `format`, and `std/log` take their values this way:
  `print("{} of {}", n, total)` instead of `print("{} of {}", (n, total))`. A tuple given
  as one value is printed as a tuple (§11.2, §21.6.1).
- **Breaking:** an inferable compile-time parameter (one that another parameter's type
  mentions) is never bound positionally: it is inferred, or written by name
  (`new(T: Page, ---)`, `max(T: i64, xs)`, `double(T: u8)` as a function pointer). This
  replaces the rule that counted positional arguments to decide whether type arguments
  were given (§16.2). Non-inferable ones, such as `list(string)`, stay positional.
- Named arguments may be written anywhere in an argument list; positional arguments bind,
  in order, the parameters that no named argument names (§11.3).
- Generic inference matches function-literal arguments after the other arguments, so a
  literal's parameter types can come from a later argument.
- **Breaking:** failing a test is an effect. The predeclared `Test` effect
  (`note`, and `fail: esc(…) -> never`) is given to test and benchmark bodies; `expect`,
  `expect_eq` and the new `ok(x)` perform it, so helper functions that declare `\ {Test}`
  can check things on a test's behalf, and a failure runs the test's `defer`s. A test body
  returns nothing: `try` no longer works in one (write `ok(x)`), and the rule that the
  first `try` decides a test's error type is gone (§3.8, §21.6).
- **Breaking:** the keyword `discard` is now `cancel` (`cancel k` cancels a suspended
  fiber, §14.5), so that "discard" keeps one meaning (`_ = e`).
- **Breaking:** handler declarations may not overlap (§12.6): a declaration for a
  specific type that a generic declaration of the same effect also matches is an error,
  where the specific one used to take precedence.
- An unconstrained untyped literal used as a shift amount now falls back to `usize`, like
  other literals that count or position (§6.1), instead of `u32`.
- **Breaking:** a local variable, constant, parameter, pattern or loop binding, or nested
  function may not have the name of a type visible where it is declared — a predeclared
  type (including every sized integer name such as `u2` or `i24`), a top-level type or
  alias, a type parameter or a local alias (§4.6, §21.1).
- A top-level function never infers its return type: without `-> R` it returns `()`
  (§11.1).
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
- C back end: writes to the ambient output and `@black_box` are calls of small shared
  functions instead of being expanded at every site, so functions with thousands of
  prints compile many times faster.
- Diagnostics on a very long source line show a window of the line around the location.

### Fixed

- A local constant computed by a call (`S :: square(3)`) declared after a local type alias
  is evaluated at compile time, so it can be an array length.
- A top-level declaration resolved while a function body is being checked no longer sees
  that body's type parameters or local constants.
- Long chains of field accesses and method calls are checked in linear time; a chain of
  more than 100 000 links is reported as nested too deeply instead of overflowing the
  stack.
- An allocation larger than the address space can hold is an `AllocError` in Safe builds
  too, instead of stopping the program.
- The interpreter on Windows opens a file read-only when `create`, `truncate` or
  `exclusive` is given without `write`, as on other systems.
- `gamma repl <file>` exits with status 1 when the file cannot be loaded.
- `--mode=`, `--backend=` and `--diag-format=` with a bad value report the value and what is
  accepted, instead of "unknown option".
- `gamma fmt <directory>` skips hidden directories and a package's `target` directory.
- `gamma lsp` with an unknown option reports it (status 2).
- A hand-edited `version` or `checksum` in `gamma.lock` is noticed: a plain run repairs the
  version, `--locked` fails, and the checksum of a cached checkout is verified against the
  lock. An empty `gamma.lock` counts as missing.
- A local type alias can be passed as the type argument of a generic function.
- `math.atan2` no longer returns NaN when an operand is of magnitude 2^1023 or more.
- The interpreter opens a file with neither `write` nor `append` read-only, like the C back
  end, instead of failing with `InvalidInput`.
- An allocation too large for the host is an `AllocError` in the interpreter, as in the C
  back end, instead of a trap; `std/arena` no longer overflows on huge requests.
- `json.Writer` supports the nesting depth `json.parse` accepts by default (512), and a
  `write_string` or `key` that fails on invalid UTF-8 leaves the writer unchanged.
- `fmt.zero_pad` keeps the sign and prefix when the text is a slice of the output buffer.
- `time.to_unix` no longer traps for the earliest representable instant.
- `bytes.read_int` and `bytes.write_int` work for odd widths such as `u24`.
- `str.Builder.write` accepts a template without arguments.
- The `std/io` example for `StringIn` compiles.
- The REPL reports a stray closing bracket or an unfinished block comment on a line of its
  own at once, instead of swallowing the following lines.
- `gamma lsp` answers requests before `initialize` with error -32002, survives a huge
  `Content-Length`, and does not format a document that has syntax errors.
- Package and dependency names that can never be imported (keywords, `_`, builtin type
  names, `std`, `core`) are rejected; `gamma new` notes when it changes the directory name,
  and `gamma init` no longer says it created a package over an existing `gamma.toml`.
- `gamma fmt` formats files that do not end in a line break, and formatting is stable when
  aligned lines are re-indented.
- `gamma doc` renders code spans with several backticks and `***bold italic***`, and
  demotes headings of doc comments in Markdown output as in HTML.
- C back end: `@bswap` of wide integers whose width is not a multiple of 64, and loads of
  integers of odd widths (`u7`, `i24`, …) whose padding bits are set.
- Compile-time evaluation may install declared handlers (including an allocator's), no
  longer crashes on a local constant that calls a nested function or on initializers with
  type errors.
- Rejected at compile time: handler literals in tuples or behind `&`, function literals
  that perform effects outside their type used as data, `&` of a `#bits` field.

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
