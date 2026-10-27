# The compiler written in Gamma

`compiler/` holds `gamma`, the Gamma compiler, written in Gamma. It was built in stages
next to a reference implementation in Rust (`src/`), and since the last stage it is the
only compiler: it builds itself, and it is what users run. The Rust code is frozen: it is
no longer built for users, released or tested on Gamma code, and stays in the repository
as the retired reference until it is deleted. Its last state is the git tag
`rust-reference-final`. This page describes how the compiler in `compiler/` is organized
and how it was grown; where it names the reference, it means that Rust code.

The work had two purposes:

- **Dogfooding.** A compiler is a large program with demanding data structures, string
  handling, error reporting and performance needs. Writing one in Gamma exercises the
  language, the standard library and the back ends far more than examples and
  conformance tests do, and every awkward spot it hit was a candidate for a fix in the
  language or the library.
- **Self-hosting.** `gamma` compiles itself, so that Gamma's own toolchain is written in
  Gamma.

## How it was built: the parity contract

While the reference existed, each stage of the compiler had to produce output
**byte-identical** to the reference's for the same input. Every stage has a stable
textual form that made this checkable:

| Stage | Compared output |
| --- | --- |
| lexer | the token dump of `lex`, and its diagnostics |
| parser | the syntax tree dump of `parse --dump`, and its diagnostics |
| modules and packages | the module graph of `modules`, and its diagnostics |
| declarations | what `decls` prints for every top-level declaration, and its diagnostics |
| bodies | the typed program of `tir`, and its diagnostics |
| checking | the diagnostics of `check`, in the human format |
| back end | the output and exit status of the compiled programs |
| interpreter, REPL, test runner, formatter, `doc`, lint and fix, language server, package manager, targets and caches | the output, exit status and files of the same command in both |
| bootstrap | the C that the compiler generates for itself, built by itself |

Diagnostics were compared in the JSON format (`--diag-format=json`), which carries every
part of a diagnostic: the message, the primary span, labels, notes and fixes. The
comparison ran over every `.gm` file in the repository: the conformance tests (including
the ones written to provoke errors), the standard library, the examples, the benchmarks
and the compiler's own sources. `tests/selfhost.rs` did this on every `cargo test`, so a
change to either implementation that made them disagree failed the build.

Parity was strict on purpose. It turned the whole test corpus of the reference into a test
suite for the new compiler without writing a single new expectation, and it made the
reference the specification of everything the language specification leaves to
implementations: message texts, spans and recovery after errors. Those messages, spans and
recoveries are now defined by the compiler in `compiler/` and its tests.

The parity tests were retired with the reference. What replaced them is described in
[Retiring the reference](#retiring-the-reference).

## Stages

1. **Lexer** — done. `gamma lex` printed the reference's token stream for the whole
   repository, with and without the layout recovery that tools use after errors.
2. **Parser** — done. The syntax tree, the parser with its error recovery, and the dump
   format (`gamma parse --dump`), matching the reference on the whole repository, syntax
   errors included.
3. **Modules and packages** — done. Loading the root module and every module its imports
   name, packages and their path dependencies from `gamma.toml`, collecting each
   module's top-level names (including the taken branches of top-level `when`s), and
   import cycles (`gamma modules`). Git and archive dependencies come from the package
   manager (see below).
4. **Checker** — done, in two parts. `gamma decls` and `gamma tir` print the declarations and the
   typed program of every program in the repository, error messages included.
   - **Declarations.** Resolving every top-level declaration without checking function
     bodies: types and type expressions, structs and unions (generic ones and their
     instances), vector, complex and `#soa` types, effects, function signatures,
     constraints, untyped constants with exact arithmetic, typed constants, array
     lengths and compile-time arguments (computed by calling functions where needed),
     `foreign` blocks and `#global`s.
   - **Bodies.** Expressions, statements, patterns, inference, effects, the instantiation
     of generic functions and compile-time evaluation, producing the typed intermediate
     representation (`compiler/tir/`). `gamma tir` prints that
     representation in a stable text form: every function written outside the core
     library, in source order, one line per expression with its type. That covers the
     whole language: patterns with exhaustiveness, optionals and results, `defer`,
     function literals, nested functions (generic ones included) and function pointers,
     effects and their checks, `with`, handler literals and declarations (for generic
     types and effects too), effect-set parameters, `ctl` operations with `resume`,
     `cancel` and `with … on`, iterators, `for` and arguments that are iterators,
     `@fiber` and the `@stack_size` analysis, tests, rest parameters and spreads,
     formatting templates, vectors, complex numbers, `f16` and `#soa`, struct attributes
     (`#bits`, `#c_union`, `using`, `#export`, `#callconv`, `#trap_hook`), enum-indexed
     arrays, local type aliases, `@type_of` and `is` on types, range values, pointer
     arithmetic, intrinsics, and the checks of `main`, `#inline`, escaping pointers,
     dangling addresses and continuations.

     Compile-time evaluation (§17) of typed constants, defaults of fields and
     parameters, array lengths, compile-time arguments, `@static_assert`,
     `@compile_error`, `when` conditions in bodies and immutable locals with constant
     initializers is done by a tree-walking evaluator over the typed representation
     (`compiler/eval/`). It runs the core library functions and the handler declarations
     a computation uses, checking their bodies on demand, with the step budget, `#fuel`,
     the trap messages and the compile-time call stack.

     Generic functions are checked once with their type parameters opaque; calls of them infer the type arguments, check constraints and name
     the instance. Each instance is then produced: its body
     is rewritten for the type arguments, so that what it calls is instantiated in turn
     (with the nesting limit of §16.7), what depends on the type arguments (literals,
     layout queries, `.bits`, format specs, templates that are compile-time parameters,
     computed array lengths, static assertions, type tests, `#soa` operations, the
     handler declarations a `with` chooses) is checked for it, and the function
     literals and handler clauses written in a generic function are copied for each
     instance.

5. **Back end** — done. `gamma build` compiles a program to an executable through C: it
   checks the program, generates C, puts the runtime (`runtime/gm_rt.h` and
   `runtime/gm_rt.c`) in front of it and runs the C compiler. The C back end
   (`compiler/cgen/`) was a port of the reference's, with the same representation of
   values and the same use of the runtime, so values have the same bytes, checks trap
   with the same messages, and effects, handlers, fibers and continuations work the same.
   It also makes the same optimizations: functions are specialized for the handler that is
   their context (`spec.gm`), parameters a function only reads are passed in place
   (`borrow.gm`), constant arguments live in static storage (`constarg.gm`), long chains
   of operations go through an accumulator variable (`chain.gm`), and long functions are
   split into helpers (`outline.gm`). The Safe mode, freestanding targets, other targets
   and the caches of the compiled runtime and of built programs are in the section
   "Targets, modes and caches" below; the only thing left out is a cache of compiled
   package C sources.

   The generated code initializes only the constants a program uses. Before generating
   code, the compiler checks every function body the program can reach, including the core
   library's (which `gamma tir` leaves out), and produces the instances of generic
   functions that those bodies call. A program that performs `Alloc` or one of the input
   and output effects runs inside the core library's default handlers.
6. **Bootstrap** — done. The compiler compiles itself: a compiler built by another one
   (stage 1) builds a second (stage 2), and the C that stage 2 generates for the
   compiler's sources is byte-identical to the C stage 1 generated, so the compiler built
   by itself is a fixed point. Stage 2 also checks the sources exactly as stage 1 does.

```sh
GAMMA_STD=lib/std ./gamma1 build compiler -o gamma2
GAMMA_STD=lib/std ./gamma2 build compiler --emit-c -o gamma3
cmp gamma2.c gamma3.c                    # identical
```

`bootstrap/seed/` holds the C that the compiler generates for itself, and
`bootstrap/build.sh` builds `gamma` from it with nothing but a C compiler (see
`bootstrap/README.md`): the seed's compiler (`gamma0`) builds the current sources
(`gamma1`), and the result builds itself (`gamma`). The seed is generated from committed
sources, which `bootstrap/seed/SOURCE` names, and CI checks that it is exactly what the
compiler of those sources generates for them, and that the build from it reaches the fixed
point on Linux (x86-64 and AArch64) and macOS. One seed serves all of them because the
compiler asks the system for the operating system and architecture it runs on (`uname`)
instead of fixing them when it is compiled, so its generated C is the same on every
64-bit little-endian Unix-like host. `tests/selfhost.rs` checks the fixed point, that
the compiler's unit tests pass and its sources are formatted, and that the split C units
build.

`gamma build [--mode=debug|checked|fast] [-g] [-o EXE] [--emit-c] ROOT` writes the C next to
the executable (`EXE.c`, the runtime and the program as one translation unit) and compiles
it with `$CC` (default `cc`); `--emit-c` stops after writing it. A Checked or Fast build
compiles `EXE.c` itself at `-O2`, so the optimizer sees the whole program. A Debug build
favors turnaround: it compiles the runtime on its own (`EXE.rt.c`)
and splits the program into units (`EXE.c`, `EXE.1.c`, `EXE.2.c`, …) that share a header
of types and declarations and divide the function bodies between them. One unit is made per
96 KiB of function text, up to the number of processors and at most 8 (`GAMMA_C_UNITS`
forces a number, which tests use to split small programs); all are compiled at once and
then linked. On four cores this brings building the compiler itself from about 110 to about 40
seconds. `--emit-c` with `GAMMA_C_UNITS` above 1 writes that split form instead of one
file, whatever the mode: the shared text in `EXE.h`, the runtime in `EXE.rt.c` and the
units, each of which includes `EXE.h`; compiled one by one and linked, they make the
program. The bootstrap seed has this form. The runtime's sources are read from `$GAMMA_RUNTIME`, or else from the
`runtime` directory next to the standard library's (an installation) or two levels above
it (the repository). `gamma` runs on Unix-like systems and, cross-compiled, on Windows
(see "Installing gamma, and Windows" below).

`gamma build -g` (and `run -g`) adds debug information (`compiler/cgen/debug.gm` and
`views.gm`). Each
statement of the generated C is preceded by a `#line` directive naming its `.gm` file and
line, so a debugger stops at Gamma lines (`break file.gm:17`), and the C between functions
keeps its own location. Frame fields also have the names of their Gamma locals
(`F.count`, `F.p.x`) with a debugger view of their type (a C type laid out like the Gamma
value, with members named after fields, variants and the like), and struct mirrors name
their fields after the Gamma fields; a Gamma name that is a macro of a system header is
suspended around those declarations. The build is optimized no further than `-O0` (Debug) or
`-O2` and compiled with `-g`. On Linux and FreeBSD the gdb pretty printers
(`runtime/gdb_printers.py`) are embedded in the executable; `gamma build -g` also writes the
lldb formatters (`runtime/lldb_formatters.py`) to `gamma_lldb.py` next to it. Both Python
files are read from the runtime directory at build time. The `#line` directives name the
absolute path of the source file, found from the working directory without resolving
symbolic links.

`gamma run [--mode=…] ROOT [ARG…]` builds the program the same way into a new temporary
directory, runs it with the arguments after the file and ends with its exit status (128
plus the signal number if a signal ended it, 134 for a trap). `gamma check ROOT` checks a
program and prints only the diagnostics, in the human format by default;
`--diag-format=short|json` selects the others.

`gamma decls` and `gamma tir` read the core library from `$GAMMA_CORE`, or else from
the `core` directory next to the standard library's. A root module without `main` is
checked as a library: the way `gamma test` would check it, with its tests and benchmarks
and those of the modules of its package that it imports.

### The interpreter

`gamma run --backend=interp [--mode=…] ROOT [ARG…]` runs the program in the compiler's
interpreter (`compiler/interp/`) instead of compiling it, and so needs no C compiler.
The interpreter walks the typed program (the reference's compiled each function to
bytecode, which made it faster):

- **Values** are bytes in the target's layout, and each expression is evaluated into a
  destination buffer of the size of its type. Buffers for intermediate values come from
  scratch memory used as a stack, which each statement and call releases, so evaluating
  allocates nothing. A local whose address is never taken lives in its frame's part of
  that memory; the others get an allocation of their own.
- **Memory** (`memory.gm`) is a flat address space in which every allocation gets fresh
  addresses and freed ones are remembered, so out-of-bounds, use-after-free, null,
  misaligned and read-only accesses trap with a message that says which.
- **Control flow** leaves expressions as a `Flow` error; a `break` or `return` first writes
  its value where the loop, `with` or call it leaves puts its result.
- **Second-class values** are bytes too: a function value holds its code address and
  handles to the frame and handler context it captures.
- **Fibers and threads** (`fiber.gm`, `threads.gm`) run as fibers of the compiler itself,
  under a scheduler that is a handler of a `ctl` effect: a fiber of the program, or a
  thread, hands control to another (its driver, the fiber it resumes, the next thread).
  Threads take turns, every 2000 calls and loop iterations and whenever one blocks; a
  blocking file read runs on a host thread meanwhile. The program runs on a large host
  stack, which `calloc` provides so that it costs only what is used.
- **Foreign code**: a program that calls foreign functions, reads foreign variables or
  uses `@asm` runs only through the C back end, and `native:` conformance tests say so.

The interpreter is slower than compiled code by a factor that depends on the program (a
hundred times on the slowest tests when the compiler is a Debug build). The conformance
suite runs every `run` test on it first and then through C, and the two must agree.

### The REPL

`gamma repl [--quiet] [--mode=…] [--diag-format=…] [FILE]` (`compiler/repl/`) is an
interactive loop on this interpreter. Every input is checked as part of a program
synthesized from the session (declarations kept as text, statements replayed with their
output hidden up to a marker), each in an arena of its own that is released afterwards.
`tests/repl.rs` feeds it sessions of inputs.

### The test runner

`gamma test [options] ROOT` and `gamma bench [options] ROOT` (`compiler/testing/`) check
the program as a library with its tests (the modules of the root's package that it
imports; all of them with `--all`), compile the chosen tests into one executable whose
`main` runs the test given by its first argument, and run it once per test with captured
output. Both always compile through C. Options: `--filter=`, `--all`, `--show-output`,
`--jobs=N` / `-j N` (0: one per processor; the output stays in order), `--timeout=SECONDS`,
`--time`, `--coverage[=FILE]`, `--mode=` and `--diag-format=`. A benchmark builds in fast mode unless `--mode`
is given. `--package=NAME` tests a dependency of the input's package (or one of its
dependencies) instead, and `--target=TRIPLE` builds the tests for another target (see
"Targets, modes and caches"). There is no other back end: `--backend=cranelift` is rejected. Coverage
(`compiler/testing/coverage.gm`) instruments the bodies after every function the
executable needs has one, so the instances of generic functions are counted too, and the
executable writes its counters to the file named by its second argument. Paths are not
resolved through symbolic links, so a package reached through one is reported under the
link's name. A test that exceeds `--timeout` is killed by a watching thread, which
cannot reach processes the test started or a test that closed its output and then ran on.

### The formatter

`gamma fmt [--check] <file or directory>...` (`compiler/fmt/`) formats `.gm` files in
place, walks directories in name order (skipping hidden directories and a package's
`target`), with `--check` lists the files that would change instead, and with `-` filters
standard input to standard output. It works on the tokens of the compiler's own lexer, and
it lexes its result again and refuses to write anything that would change the tokens.
Exit status: 1 if a file fails or, with `--check`, would change; 2 for a usage error.
`tests/fmt.rs` checks that formatting keeps the tokens and is idempotent on every `.gm`
file in the repository and on variants of each that the formatter must change (no
indentation, collapsed or added spaces, CR LF line ends, cut off in the middle).

### API documentation

`gamma doc` writes the API documentation of a module, or with `--all` of every module
below a directory, as Markdown or HTML, from the public declarations and doc comments of
the syntax tree that the compiler's parser builds. Options: `--format=markdown|html`,
`-o PATH`, `--all`, `--missing`; see `gamma doc --help`. `gamma doc lib/std --all -o
docs/std` regenerates the standard library reference. The code is in `compiler/doc/`
(`collect.gm` reads a module, `markdown.gm`, `html.gm` and `md_html.gm` render it,
`doc.gm` is the command line). A module with syntax errors is reported with the
diagnostics of `gamma check` in the human format, and the command fails. The name of a
module is found without resolving symbolic links, so a link to `lib/std` is documented
under the link's name. It treats the Unicode white space characters as white space when it
renders doc comments. `tests/doc.rs` runs it on `lib/std` in both formats, on single
modules and files, on the fixtures of `tests/doc/` and on the error cases.

### Language server

`gamma lsp` is the Language Server Protocol over standard input and output, with
diagnostics, hover, go to definition, references, rename, completion, document symbols,
signature help, formatting and quick fixes. The code is in `compiler/lsp/` (the
transport, JSON writer, analysis, requests and workspace search) and `compiler/ide/` (the
index of facts that hover, definition and completion read, built from a checked program).

The server reads messages on a thread of its own and analyzes a directory only when no
message is waiting. Each analysis allocates from an arena that is freed when the analysis
is replaced. To check a module while its files are open in an editor, the loader takes
overlays (the text of open documents stands in for the files on disk) and can keep a
module whose files have syntax errors (`keep_going`).

A failure inside the checker ends the server, since traps cannot be caught; symbolic links
are not resolved when paths are compared; and the response to each request is not freed,
so the process grows slowly. On Windows, where output cannot be written unbuffered
through `/dev/stdout`, responses can be held back until the buffer fills. `tests/lsp.rs`
runs sessions of requests, with an edit that breaks the text halfway.

### Lint and fix

`gamma lint [options] <file or directory>` and `gamma fix <file or directory>`. The lint
rules are split: those that walk the syntax tree are in `compiler/lint/syntax.gm`, those
that read the checked program (unused locals and declarations, variables that never change,
comparisons with a known result, dangling addresses, and the mutated-copy and
present-only-binding rules) in `checked.gm`, `copy.gm` and `present.gm`; `lint.gm` holds
the rules' table, their levels (defaults, the `[lint]` table of `gamma.toml`, the command
line, `#allow`) and the diagnostics. `compiler/linting/` is the command line, with `--fix`
and its rounds; `compiler/fix/` plans and applies the fixes that diagnostics carry, in
rounds, for `gamma fix` and `gamma lint --fix`. Both only edit the package's own files,
never the standard library.

To report what the rules need, the checker records which declarations and locals are used
(`Sema.decl_uses`, `outside_uses`, `local_uses`, `local_use_counts`, `nested_owner`);
`gamma tir` and `gamma decls` do not look at them. Linting checks the program with its
tests (as `gamma test` would), and, in a package, with the other modules of the package.
Warnings carry their rule: `warning[unused-variable]: …` in the human and short formats,
and a `code` field in JSON. `--target` is not an option of `gamma lint`. `tests/lint.rs`
and `tests/fix.rs` run the rules over the repository and over scratch copies of the
conformance programs.

### Packages and build settings

`gamma new`, `init`, `fetch`, `update`, `add`, `remove` and `cache` are the package
commands. The code is in `compiler/pkg/`: `git.gm` and `archive.gm` run `git`, `curl` and
`tar`, `resolve.gm` picks the revisions, `lock.gm` reads and writes `gamma.lock`,
`cache.gm` is `cache clean`, `scaffold.gm` is `new` and `init`, `edit.gm` edits
`gamma.toml` for `add` and `remove`, and `cli.gm` parses the command lines. The manifest
itself (`manifest.gm`) and version requirements (`semver.gm`) are in `compiler/load/`,
because the loader reads manifests too. `edit.gm` edits the lines of the text instead of a
document tree, which keeps comments and layout the same for the manifests the commands
handle (line ends are written as `\n`).

`gamma check`, `build`, `run`, `test`, `bench`, `lint` and `fix` fetch what the lock
file does not cover before they load the program, take `--offline`, `--locked` and `--profile=NAME`, and apply the
`[build]` table and the profile of the root package's `gamma.toml`: `mode` (under
`--mode`), `cflags` and `ldflags` (before `CFLAGS` and `LDFLAGS`), and, in a native build,
the `c-sources`, `include-dirs`, `libs` and `lib-dirs` of the package and its
dependencies. The C sources are compiled again by every build (there is no object cache),
each with the include directories of its own package; so a program with such sources is
not kept in the cache of built programs either. `CC` may hold arguments. `gamma modules`,
`decls` and `tir` do not look at the lock file. `tests/pkg.rs` runs the commands over
steps in scratch directories (local `file://` repositories and archives).

### Targets, modes and caches

`gamma check`, `build`, `run`, `test` and `bench` take `--target=TRIPLE` and `--mode=safe`
and read `CC`, `CFLAGS`, `LDFLAGS`, `GAMMA_RUNNER`, `WASI_SDK_PATH` and `WASM_OPT`. The target is a run-time value (`load.Target`, in `compiler/load/target.gm`)
that the loader, the checker, the evaluator and the C back end read: pointer size, byte
order, the operating system, the architecture, whether it is a Fil-C target and whether
`f16` exists. Without `--target` it is read from the predefined macros of the C compiler
(`cc -dM -E`); with it, `compiler/driver/toolchain.gm` picks `<triple>-gcc`, else
`clang --target=<triple>`, or adds the target to a `CC` that names clang or `zig cc`, and
refuses a compiler that makes code for another machine.

- **Freestanding targets** (`*-none-*`): there is no `main`; the functions marked
  `#export` are the entry points, each of which starts the program (`gm_start`) first, and
  `#trap_hook` receives the traps. `build` writes `<name>.o`; `run`, `test` and `bench`
  are refused. The code is in `compiler/cgen/freestanding.gm`.
- **`wasm32-wasi`**: the link flags, the `.wasm` name, `wasm-opt --asyncify` for programs
  that switch fibers (`compiler/cgen/wasi.gm`; `WASM_OPT` names the program) and
  `wasmtime` as the runner unless `GAMMA_RUNNER` is set.
- **`--mode=safe`**: builds with Fil-C (`filcc` on `PATH` or in `/opt/fil/bin`) and links
  the cached runtime. It cannot be combined with `--target`.
- **Caches** (`compiler/driver/rtcache.gm`): Debug and Safe builds link a runtime object
  compiled once (`gm_rt-<hash>.o` in the cache directory), and `run` keeps the programs
  it built (`run/prog-<hash>`, the 32 most recent), keyed by the C, the compiler and its
  flags. `cache clean` counts both.

`tests/cross.rs`, `tests/freestanding.rs` and `tests/wasi.rs` cover other targets, the
freestanding firmware example and the error cases (an unknown triple, a missing compiler,
Safe mode without `filcc`). `tests/lanes.rs` builds every conformance `run` test for the
s390x (under qemu), AArch64 (under qemu), WASI (under Wasmtime) and Safe lanes, selected by
`GAMMA_TEST_LANE` (see `docs/contributing.md`); CI runs one job per lane. Not ported: the
quoting of Windows command lines, and the private (0700) mode of the cache directories,
which the standard library cannot set.

### Installing gamma, and Windows

A release archive (`gamma-<asset>.tar.gz`, a `.zip` on Windows) is an installation
prefix:

```
bin/gamma             (bin/gamma.exe on Windows)
lib/gamma/std/        the standard library
lib/gamma/core/       the core library
lib/gamma/runtime/    gm_rt.c and gm_rt.h, which every native build compiles, and the gdb
                      and lldb helper scripts that `-g` builds use
```

`gamma` needs no environment variables to find them. `compiler/load/exe.gm` asks the
system for the path of the running executable (`/proc/self/exe` on Linux,
`_NSGetExecutablePath` and `realpath` on macOS, `GetModuleFileNameW` on Windows) instead of
trusting `argv[0]`, which is only a name when the program is started through `PATH`. The
standard library is then `../lib/gamma/std` from the executable's directory, the core
library beside it, and the runtime `runtime` beside that (in a repository checkout, where
there is none, two levels above `std`). `$GAMMA_STD`, `$GAMMA_CORE` and `$GAMMA_RUNTIME`
override each. A native build also needs a C compiler on the machine, as everywhere.

`tools/package-release.sh <asset> <gamma binary> <dir>` makes the archive from a built
`gamma` and the repository's libraries; its version is the one in `compiler/gamma.toml`.
`tools/smoke-release.sh` checks an unpacked archive: it starts `gamma` through `PATH` from
another directory and runs a program on the interpreter, through C and as a built
executable. `tools/install.sh` (or `install.ps1`) installs it. The release workflow
(`.github/workflows/release.yml`) builds `gamma` on each Unix platform from the seed with
`bootstrap/build.sh`, then builds the release binary in Fast mode with it, so that no step
needs cargo.

**Windows.** `gamma` is built for `x86_64-w64-mingw32` on Linux with MinGW-w64
(`apt install gcc-mingw-w64-x86-64`), by any `gamma`:

```sh
gamma build compiler --target=x86_64-w64-mingw32 -o gamma.exe
```

The executable is linked statically and needs only system DLLs. The platform-dependent
parts of the compiler are the ones the standard library already has per platform (paths,
the temporary and cache directories, process creation, the runtime's Win32 code), plus:

- the path of the executable (above);
- the C compiler: without `CC`, the first of `cc`, `gcc` and `clang` that is on `PATH`
  (with `.exe` appended), and `PATH` is split at `;`;
- `CC`, `CFLAGS`, `LDFLAGS` and `GAMMA_RUNNER` are split with the rules of the Windows
  command line, where a backslash is literal unless it precedes a double quote, so that
  `CC=C:\msys64\ucrt64\bin\gcc.exe` works (`pkg.split_words`, `pkg.quote_word`).

Native builds need a MinGW-w64 `gcc` or `clang` on `PATH`; MSVC's `cl.exe` is not
supported. CI cross-compiles `gamma.exe` and runs the conformance suite and the examples
with it on a Windows runner. The C files and libraries of packages have not been tried on
Windows.

### Growing a stage

A stage that covers a large part of the language grew over several steps. Until it was
complete, the compiler reported each construct it could not handle yet with an error that
started `cannot`, and the parity test compared only the programs without such an error.
The number of programs compared could only grow: the test failed if it dropped below a
floor, raised as the compiler learned more. Both checker stages are complete, so nothing
is excluded any more.

`tests/selfhost.rs` builds the compiler through the C back end in the Debug mode, so that
its overflow and bounds checks are on. Besides the bootstrap above it checks that a
packaged release runs from its installed layout.

## Retiring the reference

When every stage was complete and the compiler built itself, the Rust implementation had
no remaining job, and keeping it would have meant keeping two compilers in step for
every language change. It was retired in steps: first nothing was built from it for users
(the `gamma` command became the compiler in `compiler/`), then the tests stopped
comparing with it, and the code is deleted afterwards. Its last state is the tag
`rust-reference-final` (commit `d09a1a6`). The language change after that point, spec §7.7
(the element of `in` and its members are compared at one type, joined as `if` branches
are), is the first made without the reference.

**The final cross-check.** On that commit, the compiler built by the Rust `gamma`
(`gamma build compiler`, a release build of the Rust crate) and the compiler built from
the bootstrap seed generate byte-identical C for the compiler's sources
(`gamma build compiler --emit-c` with `GAMMA_STD=lib/std` as a relative path): 61,253,433
bytes, SHA-256 `0584d6b50c606eb43f55f644abe299295c163fece27a82d609c99f0fb5b1831b`.
Together with the seed check in CI (each seed is exactly what the previous seed's
compiler generates for the sources it names), this ties every future seed to the last
compiler the Rust implementation built. Nothing in the chain of compilers depends on a
binary that cannot be rebuilt from the source before it, which is a form of diverse
double-compiling.

**What replaced each kind of check:**

| Parity check against the reference | Now |
| --- | --- |
| token, syntax tree, module, declaration and typed-program dumps; diagnostics | the expectations in `tests/conformance/` (`//~ ERROR` annotations and the golden `.tokens` and `.ast` files), which every change is tested against |
| output and exit status of compiled programs | the same expectations (`.stdout`, `//! exit`), checked for every `run` test |
| interpreter against the reference's interpreter | the interpreter against C: each `run` test runs in both and must agree, and `tools/difftest` compares them on generated programs under gcc and clang |
| formatter, `doc`, lint and fix, language server, package manager, REPL, test runner | the suites `fmt`, `doc`, `lint`, `fix`, `lsp`, `pkg`, `repl` and `testing_cli`, which test the executable named by `GAMMA_TEST_COMPILER` |
| targets, modes, caches | `cross`, `freestanding`, `wasi`, `native_build` and the lanes of `tests/lanes.rs` |
| bootstrap | the fixed-point check in `tests/selfhost.rs`, the seed check (`bootstrap/verify.sh`) and the cross-check above |
| robustness on malformed input | fuzzing the compiler itself (`fuzz/gammac`, see `docs/contributing.md`) |

The suites that tested Rust internals (the Cranelift back end, the internal compiler error
report, the Rust interpreter's child processes and deadline, its foreign calls, the layout
of an installed Rust build) were removed with them, and so was the `//! cranelift: skip`
directive.

## Conventions

- **Memory.** A compilation allocates from an arena that is released at the end, so the
  compiler's data structures do not free their parts one by one. An allocation failure
  traps (`x!` on the result): a compiler that runs out of memory has nothing better to
  do.
- **Errors are diagnostics.** Problems in the input are reported to a `diag.Diagnostics`
  and the stage goes on; only a failure of the
  environment (an unreadable file) is an error value.
- **No dependencies** beyond the standard library.
- **Structure.** Each module of the compiler keeps one concern, and the comments say why.
  Where Gamma has a better way to say a thing (`try` for `?`, `defer` to restore parser
  state), the code uses it; [Idiomatic Gamma](guide/idioms.md) collects these.
- Sources are formatted with `gamma fmt compiler`; a test checks it.
