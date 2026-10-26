# Changelog

All notable changes to the Gamma language, its standard library and the `gamma` tool.
Versions and what each kind of change means are described in
[`docs/versioning.md`](docs/versioning.md).

## Unreleased

### Self-hosting

- **`gammac` compiles itself.** `compiler/cgen/` is a C back end for `gammac`, ported from
  the reference implementation's, and `gammac build` compiles a program to an executable
  with it (`--mode=debug|checked|fast`, `-o`, `--emit-c`). Every conformance `run` test
  compiled by `gammac` behaves as expected, and the bootstrap reaches a fixed point: the
  `gammac` that `gammac` builds generates byte-identical C for itself
  (`docs/self-hosting.md`, `tests/selfhost.rs`).
- `gammac build` in the Debug mode compiles the runtime on its own and splits the program
  into several C units compiled in parallel, as `gamma build` does; building `gammac` with
  itself takes about a third of the time on four cores.

### Language

- **`?x`** in a pattern binds an optional or a result as it is, absent or failed value
  included, where a bare name would match only a present or success value (§9.3):
  `.Break(?label, ?value)` matches every `.Break` and binds both optionals. It is
  irrefutable; against any other type it is an error. The "not exhaustive" error and the
  `present-only-binding` lint now suggest it.
- Storing the address of a local where it certainly outlives the local is a compile error,
  as returning it already was (§15.1): assigning `&x` (or a slice of a local array), by
  itself or inside another value, to a local of an enclosing scope, a captured local or a
  `#global`, and returning it inside a value. This catches keeping `&x` from
  `for x in xs` or from a pattern binding, which are copies. The cases that only may
  dangle (through a pointer, or passed to a call that may keep it) stay the
  `dangling-address` lint.
- `[1, 2]` gets a note saying how an array value is written (`[_]i32(1, 2)`), and that a
  tuple is `(a, b)`.
- **`pub(package)`** makes a declaration or a struct field visible to every module of its
  package, and to nothing outside it (§3.3): for what a package's modules share with each
  other but not with its users. A declaration may not mention a type less visible than
  itself. `gamma doc` shows only `pub` names.
- A private function of a type's module is no longer callable as a method of the type from
  other modules, as §7.10 always said (`x.helper()` where `helper` is private was accepted).
- A member of an inline tuple of `in` may be a pattern form, as on the right of `is`:
  `k in (.Fn(_), .Handler(_), .Type)` tests whether `k` matches any of them (§7.7).
- **Complex numbers**: `Complex(T)` for a float type `T`, a core struct `{ re, im }` with the
  layout of C's `_Complex`, and the operators `+ - * /`, unary `-`, and scaling by a scalar;
  methods `conj()` and `abs()`. Division is Smith's algorithm (§5.3.1, §7.4.7).
  Imaginary literals write complex constants: `3 + 4j` is one untyped complex constant,
  computed exactly and typed by inference (falling back to `Complex(f64)`); `2.5j32` fixes
  the part type. A real constant is a complex number where one is expected and beside one
  (`z: C64 = 1`, `z + 1`, `1 / z`, `z is 0`) (§2.5.2, §6.1, §6.2).
- **Math vectors**: `#vec [N]T`, with `N` from 2 to 4 and an integer or float `T`, an array
  layout that is C-compatible, element-wise operators, scalar `*` and `/`, and the methods
  `dot`, `cross`, `norm`, `min`, `max` and `abs` (§5.6.3, §7.4.7).
- **Structure-of-arrays**: `#soa [N]S` and `#soa []S` store a struct type `S` by field, one
  array per field. `a[i]` gathers or stores a whole element, `a[i].f` is a field of one
  element (a place), `a.f` a whole column; slicing, `&a` to a slice, `for`, `is` and `@hash`
  work as for arrays and slices; `T.make(n)` and `s.free()` allocate and release a `#soa`
  slice in one block (§5.6.4). `a.copy_from(src)` copies a `#soa` column by column (the
  lengths must be equal; the two may overlap).
- `#soa []T` and `#soa [N]T` of a type parameter in generic code: whole elements (`a[i]`,
  `a[i] = x`), `len`, slicing, `for`, `copy_from`, `make` and `free` work there, and each instantiation
  checks that its argument is a struct `#soa` can store (§5.6.4). A local alias may name an
  attributed type: `Cols :: #soa []T`, then `Cols.make(n)`.
- `Float(T)` now implies `Neg(T)`, so unary minus works in generic code over floats.
- Examples `nbody.gm` (math vectors) and `julia.gm` (complex numbers).
- An array `[N]T` or a vector spreads into its `N` elements like an unnamed tuple:
  `f(..xs)`, `(u8, u8, u8)(..xs)`, `print("{} {}", ..pair)` (§11.4). An enum-indexed array,
  or one whose length is a compile-time parameter, cannot be spread.
- New chapter 23 of the specification: what is a compile error, a trap or an error value,
  what a diagnostic reports and what it does not, and the limits every implementation
  supports (64 levels of nesting, …).
- The specification states rules the compiler already enforced: a struct may not contain
  itself by value, a directory without `.gm` files is not a module, and the characters of
  foreign library and symbol names.
- The hash map moved from the core library to the standard module
  `std/collections/hash_map`: write `hm :: import "std/collections/hash_map"`, then
  `hm.hash_map(K, V)` and `hm.HashMap(K, V)`. `List` is now the only container in the
  core library, the one the language definition refers to (§21.5). The universe names
  `HashMap`, `hash_map` and `hash_map_with` stay for one release as `#deprecated` names of
  the same declarations, so existing code compiles with a warning.
- The specification's rationale (Appendix C.8) records why constraints stay predicates on
  one type and why `in` covers only built-in sequences: library containers answer
  membership with `contains`.

- An optional or result on the left of `in` with an inline tuple compares as `is` does: the
  members convert to its type, so `s.get(i) in (' ', '\t')` works for `s.get(i): ?u8`
  (§7.7).

### Standard library

- `std/collections/soa_list`: `SoaList(S)`, a growable list stored by field, with `items` a
  `#soa []S` (`push`, `pop`, `insert`, `remove`, `swap_remove`, `extend`, `reserve`,
  `truncate`, `clear`, `deinit`). Like `List`, it keeps the allocator it was created with.
- Slices and arrays have new builtin methods (§21.3): `reverse()`, `rotate_left(k)`,
  `rotate_right(k)`, `dedup()`, `partition(pred)`, `select_nth(n)` and `select_nth_by(n,
  less)`, `is_sorted()` and `is_sorted_by(less)`, and `upper_bound(x)`, the counterpart of
  `binary_search`'s lower bound. Like the sorts, they work in place and allocate nothing.
- `std/uuid`: RFC 9562 identifiers, with `v4()` (needs `Rand`), `v7()` (needs `Clock +
  Rand`), and `parse` and `format` of the canonical text.
- `std/net`: IP address values `Ipv4`, `Ipv6` and `IpAddr`, parsed from the text forms of
  RFC 4291 (`parse_ipv4`, `parse_ipv6`, `parse_ip`) and written in the canonical form of
  RFC 5952 (`format_ipv4`, `format_ipv6`, `format_ip`), plus `is_loopback` and
  `is_unspecified`.
- `std/fs`: `remove_all(path)` removes a directory tree without following symbolic links;
  `temp_dir()`, `create_temp(dir, prefix, buf)` and `make_temp_dir(dir, prefix, buf)`
  create temporary files and directories under fresh random names.
- `std/math`: `asinh`, `acosh`, `atanh`, `erf`, `erfc`, `lgamma`, `lgamma_sign`, `tgamma`,
  `frexp`, `ldexp` and `nextafter`; and, for every integer type, `gcd`, `lcm`, `isqrt`,
  `ilog2`, `ilog10`, `div_floor` and `mod_floor`.
- `std/complex`: `tan`, `sinh`, `cosh`, `tanh`, `asin`, `acos`, `atan`, `asinh`, `acosh` and
  `atanh`, with the branch cuts and special values of C99 Annex G. `sin` and `cos` are now
  computed through `sinh` and `cosh`, so they also follow Annex G for infinite and NaN parts,
  and a finite part is no longer lost to an overflow (`sin(-0.5 + 711i)` was `-inf + inf*i`).
- `std/time`: `parse_duration`, the inverse of `format_duration`; ISO 8601 week dates and
  ordinal dates (`iso_week`, `from_iso_week`, `day_of_year`, `from_ordinal`, and
  `format_`/`parse_iso_week` and `format_`/`parse_ordinal` for their text).
- `std/text`: UTF-16 with `utf16_len`, `encode_utf16`, `append_utf16` and the iterator
  `decode_utf16`.

### Diagnostics

- `gamma lint` has a new rule, `mutated-copy`: a local holding a copy of a `List`, a
  `HashMap` or another value with a resizable buffer (`tys := cx.tys`) that is then
  changed as if it were the original (`tys.intern(k)`), which loses the change; a pointer
  (`tys := &cx.tys`) was meant. It was found when such copies broke the self-hosted
  compiler.
- `gamma lint` has three new rules: `dangling-address`, for the address of a local (a loop
  variable, a pattern binding) stored, passed to be stored or returned where it outlives the
  local; `needless-pub`, for `pub(package)` names no other module uses and `pub` ones of
  a program package; and `present-only-binding`, for a `match` arm such as `.Return(v)`
  whose name matches only a present optional, when no arm names `.Return(null)` and a
  catch-all arm with a body takes it instead. Lint now names the files of a package's other modules relative to the
  working directory, as `gamma check` does.
- A `match` left incomplete by a bare name in the position of an optional inside a
  pattern (which matches only a present value, §9.3) now points at that name and says how
  to bind the optional as it is: bind the value that holds it, such as `.Break(b)` for the
  whole payload.
- An `else` arm of a `match` gets the plain "expected a pattern" error, without a fix.
- The hint to write `is` for `=` is given only after an `if` or `while` condition.
- `#allow` with a name that is not a lint rule no longer stops compilation; `gamma lint`
  reports it.
- Nesting deeper than the parser's limit (now 256) and other implementation limits say
  which limit was reached.
- A spread that fails no longer also reports every element it did not supply.
- The old effect syntax `\` is no longer recognized: `\` is an unexpected character.

### Documentation

- [Idiomatic Gamma](docs/guide/idioms.md), a growing handbook of the patterns Gamma code uses,
  checked by the test suite.

### Added

- `gamma tir`: checks a program and prints the typed form the checker produces for each
  of its functions (resolved names, typed operators, explicit conversions), see chapter
  15 of the guide. `gammac tir` checks the function bodies of a growing subset of
  programs the same way, generic functions and their calls included, and evaluates
  constants, defaults, array lengths, static assertions and immutable locals at compile
  time. `gammac decls` and `gammac tir` now match `gamma decls` and `gamma tir` on every
  program in the repository.
- `&e` where a pointer `^T` is expected checks `e` with `T` expected, so a variant or tuple
  takes its type from it: `area(&.Circle(2.0))` (§7.2).
- `gamma decls`: resolves the top-level declarations of a program without checking
  function bodies, and prints what each denotes (types with their fields or variants,
  effects, signatures, constants), with the diagnostics of declarations.
- `gamma modules`: loads a program the way `gamma check` does, without checking its
  declarations, and prints its modules in load order with their directories, packages
  and imports (see chapter 15 of the guide).
- `compiler/`: `gammac`, a Gamma compiler written in Gamma, the start of a self-hosted
  toolchain. Its lexer and parser produce the same tokens, syntax trees and diagnostics as
  `gamma lex` and `gamma parse --dump` on every `.gm` file of the repository, and loads
  every program's modules as `gamma modules` does; it resolves the declarations of most
  programs as `gamma decls` does. `tests/selfhost.rs` checks all of it (see
  `docs/self-hosting.md`).

### Fixed

- The fallback of an `else` whose left side has an error is now checked: its own errors
  are reported, and a `break` in it counts for its loop, which was otherwise reported as
  never ending, with the code after it as unreachable.
- A Debug build split into C units (`GAMMA_C_UNITS`, or a large program) of a program
  that uses `#global errno` failed with "the generated C cannot be split into units".
- `gamma tir` on a library (a root module without `main`) left out its tests and
  benchmarks: they were not checked, and their functions were missing from the dump.
- A pattern binding with `else` in a block whose value is coerced to an expected type
  (`x: i64 = if c { .A(v) := u else return 0; v } else { 0 }`) was rejected as not
  leaving the block.
- `gamma fmt` indented a line starting with a variant (`.A`) as a continuation when it
  was the first line of a block.

- Messages that show an effect set with an effect-set parameter (`needs E + Out`) list its
  terms in the same order in every run; the order used to depend on what the same process
  had compiled before (the language server, the test runner).
- C back end: an assignment through a slice, a pointer or an index stores to the place as
  it was before the right side ran, as §4.7 says and the interpreter and the Cranelift
  back end did: `s.xs[0] = f(&s)` no longer stores into the slice that `f` put in `s.xs`.
- Interpreter: `x!` on a result whose success type is empty (`E!()`) no longer crashes
  the interpreter.

## 0.1.0 - 2026-10-20

The first release: the complete language of `gamma-spec.md`, implemented on three
back ends that give the same results.

- **Language**: algebraic effects and handlers for allocation, I/O, processes, iteration,
  early exit and fibers, with effect sets written `needs A + B` (named sets and
  effect-set parameters included); generics with type, compile-time value and effect-set
  parameters, inferred or named; rest parameters; compile-time evaluation, `when` and
  local constants; optionals and error unions; SIMD vectors (`#simd [N]T`) and arrays
  indexed by an enum (`[E]T`); modules and packages; `test` and `bench` declarations,
  with failing a test as the `Test` effect; C interoperability (`foreign`, `#export`,
  `#callconv`, inline assembly, atomics).
- **Interpreter** (`gamma run`, `gamma test`): functions run as register bytecode, with
  a tree-walking fallback; foreign C functions are called through libffi.
- **C back end** (`gamma build`, `--backend=c`): build modes Debug, Checked, Fast and
  Safe (memory-safe through Fil-C, Linux only); debug information with gdb and lldb views
  and pretty printers (`-g`); cross-compiling with `--target=`, including WebAssembly
  for WASI (`--target=wasm32-wasi`).
- **Cranelift back end** (`--backend=cranelift`, 64-bit Linux and macOS): generates
  machine code directly, several times faster to build than through C.
- **Platforms**: Linux and macOS (x86_64 and arm64), x86_64 Windows with a MinGW-w64 C
  compiler (gcc or clang), and WASI.
- **Standard library**: the core library and the `std/` modules listed in
  [`docs/std.md`](docs/std.md), including files, networking, processes, threads, fibers
  (with sleeping and channels), text, time zones, JSON, hashes and collections.
  Additions follow the admission policy of §22.
- **Tooling**: `gamma fmt`, `gamma lint`, `gamma fix` (applies the compiler's suggested
  fixes), `gamma doc` (API documentation in Markdown or HTML), `gamma repl`; `gamma test`
  and `gamma bench` with filters, parallel jobs, timeouts, timing and line coverage;
  packages with path, git and archive dependencies, version requirements, `gamma.lock`
  and build profiles with C sources and libraries (`gamma new`, `init`, `add`, `remove`,
  `fetch`, `update`, `cache`); the language server `gamma lsp`; VS Code and Zed
  extensions and TextMate and tree-sitter grammars; prebuilt binaries with install
  scripts for every platform above.
- **Documentation**: the guide (with chapters for C and Rust programmers), runnable
  examples, and a website with a playground that runs programs in the browser.

The feature set above was first frozen on 2026-10-16. The lists below record what was
added, changed and fixed between then and this release, for anyone who built Gamma from
source in that time.

### Added

- `gamma test --timeout=<seconds>` (and `gamma bench`): a test that runs longer fails
  with `timed out after N s`, on the interpreter and natively. The interpreter also stops
  a test that is sleeping when its time is up.

- A website, published with GitHub Pages from `web/`: a landing page, the guide, the
  specification and the standard library reference as pages, and a playground that runs
  programs in the browser with the interpreter compiled to WebAssembly (everything except
  files, processes, the network, threads, fibers and C). A sleep lasts as long as asked;
  Stop ends the program. The compiler crate now also builds for `wasm32-wasip1`.
- The Cranelift back end: `--backend=cranelift` on `gamma run`, `build`, `test` and
  `bench` generates machine code directly with Cranelift instead of going through C. It
  compiles the whole language, passes the whole conformance suite, and builds several
  times faster than the C back end, for code that runs somewhat slower. Foreign calls and
  `#export` follow the platform's C calling convention, including structs passed and
  returned by value and variadic functions; `@asm` is compiled by the C compiler. It
  supports 64-bit Linux and macOS on x86-64 and AArch64, still uses the C compiler to build
  the runtime and to link, and does not emit debug information (`-g`) yet. See
  `docs/guide/native-backend.md`.
- `gamma lint`: warnings, each naming its rule, about code that compiles but is probably
  a mistake or has a simpler form: unused variables, parameters, imports, private
  declarations and labels; comparisons of a value with itself; self-assignments;
  constant `if` and `while` conditions; `defer` and `errdefer` that run at once or never;
  identical and empty branches; comparisons with `true` and `false`; `if c { true } else
  { false }`; `not not x`; `else { if … }`; unnecessary parentheses; and names not in the
  usual case. `prefer-immutable`, off by default, finds `:=` variables that are never
  assigned. `gamma lint --fix` applies the fixes the warnings carry, and undoes any after
  which the program no longer compiles. Levels come from the `[lint]` table of
  `gamma.toml`, from `--allow=`, `--warn=`, `--deny=` and `-D`, and from `#allow("rule")`
  before a declaration (a new attribute). See `docs/lint.md`.

- Guides for C and Rust programmers (`docs/guide/for-c-programmers.md`,
  `docs/guide/for-rust-programmers.md`): how what you know maps onto Gamma, what changes
  and why, with examples that the test suite compiles and runs on both back ends.
- WebAssembly for WASI: `--target=wasm32-wasi` builds a `.wasm` module with clang and a
  WASI sysroot (wasi-sdk through `WASI_SDK_PATH`, or the system's clang with wasi-libc),
  which `gamma run` and `gamma test` run with Wasmtime (`GAMMA_RUNNER` replaces it).
  Files, directories, clocks, random numbers, arguments and the environment work;
  fibers work through Binaryen's Asyncify (`wasm-opt`, or `WASM_OPT`), applied only to
  programs that use them. `target.os` is `.Wasi`. CI runs the conformance suite compiled
  to WebAssembly.
- `IoError.Unsupported`: the system does not offer the operation. On WASI, which has no
  threads, processes or sockets, `thread.spawn` and every `Proc` and `Net` operation fail
  with it (spec §3.7.2).
- Vectors: `#simd [N]T` is an array of `N` lanes (fixed-width integers, `f32`, `f64` or
  `bool`) whose operators work lane by lane and compile to vector instructions. `is`
  compares whole vectors and gives `bool`; the methods `lanes_eq`, `lanes_ne`, `lanes_lt`,
  `lanes_le`, `lanes_gt` and `lanes_ge` compare lane by lane and give masks
  (`#simd [N]bool`) with `all`, `any`, `count` and `select`; vectors have `reduce_sum`, `reduce_min`, `reduce_max`, `min`, `max`, `abs`, `to`,
  `shuffle`, `load` from and `store` to slices, and convert to and from the array of
  their lanes. Integer lanes keep the checks of the scalar operators.
- The `Proc` ambient effect and `std/process`: start programs (`Proc.spawn`) with each
  standard stream inherited, discarded or piped, a working directory and an environment;
  write to a child's input, read its output and error streams together without deadlock
  (`read_out`), wait for its exit status or kill it. `process.output` collects a
  command's output, `process.status` runs one and `process.success` tests the result.
  Tests get a handler that refuses to start processes.
- `std/time/zone`: time zones from TZif data (RFC 8536) and POSIX `TZ` rules. Load a zone
  from the system's zoneinfo (`zone.load("Europe/Paris")`, `zone.local()`) or from bytes,
  find the offset in force at an instant, convert UTC to local time, and resolve a local
  time, reporting times that are ambiguous or skipped at a transition.
- `std/fiber`: `fiber.sleep(d)` and `fiber.sleep_until(t)` suspend a task while the others
  run; the scheduler itself does not need `Clock`. `std/fiber/generator` turns any
  iterator into a generator pulled with `next()`, so iterators can be advanced in
  lockstep.
- Arrays indexed by an enum: `[E]T` has one element per variant of the enum `E` and is
  indexed by `E` values (`names[.E]`), with no bounds check. It is built by naming every
  variant (`[Dir]string(N: "north", E: "east", S: "south", W: "west")`) or with `fill:`,
  so a table can neither miss an entry nor list one against the wrong variant. `a[..]`
  gives the elements, in declaration order, as a slice.
- `gamma build -g` writes `gamma_lldb.py` next to the executable: lldb data formatters
  that show slices as their elements, strings as text, optionals as their value or
  `null`, results as their value or `!error` and tagged unions as their variant, as the
  gdb pretty printers do. Load it with `command script import gamma_lldb.py`.
- `gamma fix` applies the fixes that the compiler's diagnostics suggest, where a
  diagnostic has exactly one: `&&` and `||` become `and` and `or`, an unused value gets
  `_ = `, `s.len()` on a slice loses its `()`, `else` as a match pattern becomes `_`, a
  trailing comma after a match arm goes, a payload variant pattern `.V` becomes `.V(_)`,
  `x := v` at top level becomes `x :: v`, and an uppercase radix prefix is lowercased.
  It changes only the files of the input's package, and repeats until nothing is left
  to apply. The language server offers the same fixes as quick fixes (code actions),
  including both readings of the ambiguous `try x else d`, and `--diag-format=json`
  prints diagnostics with their locations and fixes as one JSON object per line.
- More fixes for `gamma fix`, `--diag-format=json` and the language server: `==` and `!=`
  become `is` and `is not`, `if x = v {` becomes `if x is v {`, `a < b < c` becomes
  `a < b and b < c` (when `b` is a name), a lone `}` in a format string becomes `}}`, a
  standard-library type used without its import (`File`, `Socket`, …) gets the import
  and its module prefix, a core effect operation used bare (`make_dir(…)`) gets its
  effect (`Fs.make_dir(…)`), and `try x` in a test becomes `ok(x)`.
- A function that performs an effect it does not declare gets the fix of adding it to its
  signature (` needs Out`, or ` + Out` after its other effects); `gamma fix` repeats it
  for the callers in the next round. `main` is given only the ambient effects, since it
  must handle the others itself.
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

- `gamma fetch` takes its directory as `--dir <dir>`, like `update`, `add` and `remove`
  (the positional form still works).
- `Os` has a new case, `Wasi`, and `IoError` a new case, `Unsupported`: an exhaustive
  `match` over either needs an arm for it.
- Faster Safe builds: values that hold no pointer, or hold their pointers at fixed
  offsets, are copied word by word instead of through the Fil-C runtime; large
  parameters are read in place when every call passes a value nothing changes during the
  call (such as the `SourceLocation` of `new` and `destroy`), in every build mode; and
  the innermost handler of an effect is found without a call. A loop of `new` and
  `destroy` runs about 45% fewer instructions in a Safe build.
- Faster `gamma run` for iterators and closures: a handler clause or function literal
  reads and writes the variables it captures without making a frame of its own, which
  takes about a quarter off a `for` over an iterator.
- Programs for a freestanding target (no operating system: firmware, kernels) build to
  an object file whose entry points are the program's `#export`ed functions. There is
  no `main` and no default handler: the program installs its own, such as an allocator
  over a static buffer or an `Out` that writes to a serial port. A function marked
  `#trap_hook` receives every trap's message and location.
- A literal's type that a construct had to fix early (`@type_of`, a format spec such as
  `{:x}`, a `with` handler value) is an error, not a silent choice, when a later use
  would have given it another type (`n := 0; print("{:x}", n); for i in 0..n {}`).
- The constraint `Signed` is now `Neg`, named for what it grants (unary `-`).
- `ns`, `us`, `ms` and `secs` moved from the universe to `std/time`: `time.ms(5)`.
- The specification states effect-set parameter inference as two local rules.
- Effect operations live in their effect's namespace and are always called through it:
  `Log.log(.Warn, msg)` inside the module that declares `Log`, `log.Log.log(…)` outside
  it (`yield` stays a bare name). Operations are no longer top-level names of their
  module, so two effects may both have a `write` and an operation may share a name with
  a function. A bare call reports the qualified form, and `gamma fix` applies it.
- The core operations drop the suffixes that only avoided name clashes: `Out.write`,
  `Err.write`, `Fs.read`, `Fs.write`, `Net.close`, `Proc.write` and `Proc.read` (were
  `write_out`, `write_err`, `read_from`, `write_to`, `close_socket`, `write_in` and
  `read_out`). On handles they are methods: `f.read(buf)`, `sock.close()`,
  `child.write(bytes)`. An operation whose first parameter takes a type of its module is
  a method of that type, and a type may not have two methods of one name.
- Arithmetic no longer applies to arrays: `a + b` and `-a` on `[N]T` are compile errors.
  Use a `#simd [N]T` vector, whose operators are single instructions, or a loop.
- Integer types are at most 128 bits wide (`u1` … `u128`, `i1` … `i128`); wider
  arithmetic is a loop over words that an operator would hide.
- An iterator argument (`collect(words(next_chunk()))`) evaluates its call's arguments
  once, before the call that receives it, as `for` does; before, they were evaluated
  again each time the function ran the iterator.
- Computed array lengths are the same only when written the same way after constant
  folding: `[A + B]T` and `[B + A]T` are now different types. They may use `~`
  (exclusive or); the specification wrongly listed `^`.
- The specification opens with a first program, the rule "words for logic, sigils for
  shapes", the rules that best show the principles at work, and three questions every
  new feature must answer. Its grammar now writes effect sets after `needs`, and it
  explains why there are two allocation models and three ways to iterate.
- Functions with thousands of statements compile through C in time that grows linearly
  with their length: runs of statements go into helper C functions that share the frame.
  A `main` of 10,000 statements builds with gcc in 4 s instead of 87 s (Debug) and in
  11 s instead of 44 s (Fast), and with clang in 5 s instead of 87 s and 9 s instead of
  83 s.
- A large Debug build compiles its C as several files in parallel (one per 96 KiB of
  generated functions, up to one per processor): the 16k-line C of `examples/calc`
  builds in 0.6–0.9 s instead of 1.4 s on four cores. The extra files sit next to
  `prog.c` as `prog.1.c`, `prog.2.c`…
- Faster `f32` and `f64` code on the interpreter: arithmetic, comparisons and branches
  on floats run as native bytecode instructions (a Mandelbrot loop runs about 8x faster),
  and so do `@rotl` and `@rotr` on integers of up to 64 bits (SHA-256 about 1.9x faster).
- Functions that call variadic C functions (`printf` and its relatives) run on the
  interpreter's bytecode VM instead of its slower tree-walker.
- The interpreter runs everything on its bytecode VM; the tree-walking evaluator is
  gone, and with it `GAMMA_INTERP=tree`. The bodies of `with … on`, `resume`, `cancel`,
  `@fiber`, system calls, coverage counters, statements with values over 64 KiB and the
  initializers of constants and globals, which used to fall back to the tree-walker, are
  compiled too (code in a generator's body runs about twice as fast).
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

### Security

- Package manager: a git URL that starts with `-` (in `gamma.toml`, `gamma.lock` or
  `gamma add`) is rejected, and URLs are passed to git after `--`; such a URL in any
  dependency's manifest made git run a command (`--upload-pack=…`) on `gamma fetch`,
  `check`, `build` or when the language server opened the project. URLs with spaces or
  control characters are rejected too, and git's `ext::` transport is never allowed.
- Package manager: the entries of `gamma.lock` are validated (full commit ids, `sha256:`
  hashes, names, subdirectories); a damaged or hostile lock crashed `gamma fetch` or named
  a directory outside the package store. A chain of symbolic links that climbs out of a
  package (`d/up -> ..`, `esc -> d/up/..`) is rejected like a single such link. An
  archive already unpacked in the cache is hashed again against the lock, as git
  checkouts are. Downloads that stall time out, and archives over 1 GiB, or that unpack to
  more than 4 GiB or a million entries, are refused.
- `#link_name` takes only symbol characters (letters, digits, `_ $ . @`) and a foreign
  library name only letters, digits and `_ . + -`: a crafted name was pasted into the
  generated C. Foreign symbols and exports named like a symbol of the runtime are errors.

### Fixed

- Cranelift back end on x86-64 Linux and macOS: a foreign call whose 128-bit integer
  argument finds a single integer register left is refused with a message, instead of
  passing the value differently from C (the C back end and the interpreter handle it).
- A line starting with `.name` that holds a raw string or a block comment before its `:=`
  (`` .Some(`x`) := v else … ``) is read as a pattern binding, instead of continuing the line
  before it.
- `gamma build x.gm` no longer overwrites a file `x.c` that it did not generate (it
  refuses, naming the file), and intermediate objects of the C and Cranelift back ends no
  longer take the name of a file next to the output (`-o app` deleted `app.o`).
- C and Cranelift back ends: an index or slice bound of a 128-bit type (or of a type
  wider than `usize`) is checked with its whole value instead of its low bits, and an
  inclusive slice bound of `usize`'s largest value traps instead of wrapping to an empty
  slice, as on the interpreter.
- Cranelift back end: calls through a `^fn` whose signature passes a struct by value use
  the C convention (§19.2), so pointers to C functions (and Gamma functions behind such
  pointers called from C) receive the right arguments.
- `gamma cache clean` reports what it could not remove and exits with status 1 instead of
  claiming success, and no longer removes a repository directory a build is writing to.
- Mirrors of git dependencies drop branches and tags deleted upstream, so a deleted
  release no longer resolves from the cache. Git commands ignore the caller's `GIT_DIR`
  and similar variables (as set inside a git hook).
- Command-line arguments that are not valid UTF-8 are a usage error instead of a crash.
- `gamma run --backend=c` rebuilds a cached executable when a library named by a
  `foreign` block or by `-L`/`-l` in `LDFLAGS` changes, or when `wasm-opt` is upgraded.
- The cache directory and temporary build directories are created accessible to the
  user only, and temporary files never replace an existing file; failed builds no longer
  leave temporary executables in the cache, and old leftovers are removed. The cache
  directory must be private to the user (`docs/architecture.md`).
- On Windows, a backslash in `CC`, `CFLAGS`, `LDFLAGS` and `GAMMA_RUNNER` is literal
  except before a double quote (as in Windows command lines), so `C:\msys64\…` paths work.
- A failed write to the standard output (`> /dev/full`) ends the program with status 1 and
  `gamma: cannot write the standard output: …` on every back end; a closed pipe still ends
  it as before.
- Integer `pow` takes time in proportion to the bit length of the exponent on the C and
  Cranelift back ends (`x.pow(4000000000)` took seconds), and `clz`, `ctz`, `popcount`,
  `bswap` and `bitreverse` use the compiler's builtins in the C back end.
- C runtime: sockets are no longer inherited by started processes; each thread's
  alternate signal stack is freed when the thread ends; joining or detaching an unknown
  thread handle fails instead of crashing; a `connect` interrupted by a signal completes;
  the working directory may be longer than 4096 bytes.
- `#global errno` in a `foreign` block (as in the specification's example) links and reads
  the calling thread's `errno` on the C and Cranelift back ends; glibc's `errno` is not a
  plain symbol, so linking failed.
- Cranelift back end: constants are placed in read-only data, and `@cpu_count` returns a
  32-bit value as declared.
- Non-relative imports (`std/../..`, `dep//abs`) can no longer leave the directory of
  `std` or of the dependency.
- A `pub` declaration's private types are also found in the operations of a `pub` effect,
  in the effect arguments of `needs`, and in effect aliases (§3.3).
- Value arguments of generic types compare with `is`: `Box(0.0)` and `Box(-0.0)` are one
  type, NaNs are one value, and padding bytes do not count.
- Untyped constants are limited to 2^22 bits (§6.2), and very large numbers are shown by
  their size in messages.
- Clearer messages for `fill:` given with other arguments of an array construction.
- Directory, process and thread handles that were closed (or waited for) fail with an
  error instead of acting on a newer object that took over their slot, and a concurrent
  close or wait no longer frees an object another call is still using.
- Windows: starting a process searches only `PATH` for a bare program name (never the
  current directory), and refuses to pass arguments with characters `cmd.exe` interprets
  to a `.bat` or `.cmd` program; `Fs.open_dir` rejects the empty path and wildcards.
- `install.ps1`: https or file locations only, a validated `.sha256`, an atomic swap of the
  standard library, LICENSE and README installed, and `%VAR%` entries of the user `PATH`
  kept as they were.
- Package commands accept `--` before arguments that start with `-`, say when a flag is
  given a value, and `gamma new` creates no directory for a name it refuses; `gamma.toml`
  and `gamma.lock` are replaced through a fresh temporary file flushed to disk.
- Releases are published only when CI passed on the tagged commit, with a build provenance
  attestation for each archive, and the archives are reproducible.
- Field and parameter defaults are evaluated once, at compile time, where they are
  declared (§17.2): a default that traps is a compile error at the default, and a default
  that constructs its own struct is a declaration cycle. Float value arguments of generic
  types are shown by value in messages (`Box(1.5)`).
- Interpreter: file opens, reads and writes, name lookups, socket shutdowns and calls of
  foreign functions with effects no longer stop the other threads (a C call waiting for
  another Gamma thread deadlocked); `Proc.kill` never signals a process id that may have been
  reused; children a program never waited for are reaped when its run ends; foreign calls
  with large buffers copy less.
- `gamma run` passes program arguments that are not UTF-8 to the program unchanged.
- The language server registers file watchers when the client supports it, and refreshes
  diagnostics when imported files change on disk; references and rename read unchanged
  files from a cache.
- `gamma test --coverage=<file>` exits with status 1 when the tracefile cannot be
  written.
- `install.sh` accepts only `https://` and `file://` download locations, checks that the
  `.sha256` file holds a hash, and replaces the standard library of an earlier
  installation only once the new one is copied in full.
- Types larger than `isize::MAX` bytes (arrays, structs, tuples, unions, optionals,
  results, generic instances) are a compile error instead of a compiler crash or silently
  wrong sizes; on 32-bit targets this catches arrays larger than the address space.
- Types that contain each other by value (also through `using`) no longer crash the
  compiler after the error is reported, and a compile-time string pointing outside its
  data is an error instead of a crash.
- Generic effects with a generic type argument (`needs E(Box(T))`) no longer reject valid
  programs.
- Checking stays fast for long `else if` chains, for types built by repeated doubling
  (long type names in messages are abbreviated), for lines with many unmatched closing
  brackets, and for many diagnostics on long lines.
- Interpreter: a trap on one thread stops the program even when another thread is blocked
  in a mutex, condition variable, channel or join; `errno` read from other threads and
  fibers is their own; huge allocation alignments and forged slice lengths passed to
  system operations no longer crash it; `Env.var` returns values that are not UTF-8, and
  `Env.args` and `Env.var` no longer leak memory per call; passing freed memory or a
  callback it cannot translate to C traps; more than 8192 suspended fibers is a clean trap
  instead of an abort.
- `gamma lint --fix` no longer changes multi-line raw strings inside an `else { if … }`
  it collapses.
- The language server clears the diagnostics of other files of a directory when its last
  open document closes, clamps out-of-range positions, and limits header and message sizes.
- `0xFF.abs()` lexes as an integer and a method call, and a doc comment separated from a
  declaration by blank lines no longer attaches to it.
- `gamma doc` lengthens code fences around signatures that contain backticks and
  percent-encodes module links; `gamma repl` reports a line that is not UTF-8 and goes
  on; `gamma fmt` writes LF line ends inside block comments of CRLF files.
- A `::` binding whose initializer calls a nested function with an inferred return type
  (`zero :: fn() { return 0 }`, then `z :: zero()`) no longer crashes the compiler.
- Listing a directory no longer traps on targets with a 32-bit `usize`.
- C back end: `print` and `@write` no longer pass empty pieces to their sink when a
  value formats to nothing (`print("{}", "")`), so a custom `Out` handler sees the same
  calls as on the interpreter. The spec now says that pieces are never empty.
- C back end: calling a function whose frame no stack could hold (a local array of a
  terabyte, say) reports ``stack overflow: `f` needs a frame of N bytes`` at the call, as
  the interpreter does, instead of a bare `stack overflow`. Safe builds trap there too,
  where Fil-C used to allocate such a local off the stack.
- A `#bits` struct with a field that has invalid bit patterns (an enum, say) could be
  converted from an integer literal, `W(255)`, although only plain bit structs convert
  from their integer (§5.10.5); the value then held an invalid tag. This is now an
  error, as it already was for a conversion of a variable, and the message explains it.
- C back end: the overflow trap of `pow` on integers of up to 128 bits now names the
  operands (`integer overflow in `pow` (-5^31)`), as the interpreter's does.
- Interpreter: `f16` NaNs kept neither their sign nor their payload when widened or
  narrowed, so `(h / h).bits(u16)` differed from compiled code.
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
