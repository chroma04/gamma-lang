# The self-hosted compiler

`compiler/` holds `gammac`, a Gamma compiler written in Gamma. It is being built in
stages next to the reference implementation in `src/`, which stays the compiler that
users run until `gammac` can do everything it does. The work has two purposes:

- **Dogfooding.** A compiler is a large program with demanding data structures, string
  handling, error reporting and performance needs. Writing one in Gamma exercises the
  language, the standard library and all three back ends far more than examples and
  conformance tests do, and every awkward spot it hits is a candidate for a fix in the
  language or the library.
- **Self-hosting.** `gammac` compiles itself, so that Gamma's own toolchain can be
  written in Gamma.

## The parity contract

Each stage of `gammac` must produce output **byte-identical** to the reference
implementation's for the same input. Every stage has a stable textual form that makes
this checkable:

| Stage | Compared output |
| --- | --- |
| lexer | the token dump of `gamma lex`, and its diagnostics |
| parser | the syntax tree dump of `gamma parse --dump`, and its diagnostics |
| modules and packages | the module graph of `gamma modules`, and its diagnostics |
| declarations | what `gamma decls` prints for every top-level declaration, and its diagnostics |
| bodies | the typed program of `gamma tir`, and its diagnostics |
| checking | the diagnostics of `gamma check`, in the human format |
| back end | the output and exit status of the compiled programs |
| test runner | the output and exit status of `gamma test --backend=c` and `gamma bench`, timings masked |
| targets and caches | `check`, `build`, `run`, `test` and `bench` for other targets, freestanding and `--mode=safe`: the output, exit status and files of both, and the objects they cache |
| formatter | the output, messages and exit status of `gamma fmt` |
| `doc` | the standard output, standard error, exit status and files of `gamma doc` |
| lint and fix | the diagnostics, messages, edited files and exit status of `gamma lint` and `gamma fix` |
| package manager | the output, messages, files (`gamma.toml`, `gamma.lock`, the package store) and exit status of `gamma new`, `init`, `fetch`, `update`, `add`, `remove` and `cache`, and of the commands that use the build settings of `gamma.toml` |
| bootstrap | the C that `gammac` generates for itself, built by itself |

Diagnostics are compared in the JSON format (`--diag-format=json`), which carries every
part of a diagnostic: the message, the primary span, labels, notes and fixes.

The comparison runs over **every `.gm` file in the repository**: the conformance tests
(including the ones written to provoke errors), the standard library, the examples, the
benchmarks and `gammac`'s own sources. `tests/selfhost.rs` does this on every `cargo test`,
so a change to either implementation that makes them disagree fails the build. When the
reference implementation changes on purpose (a new token, a reworded message), `gammac`
changes with it in the same pull request.

Parity is strict on purpose. It turns the whole test corpus of the reference compiler
into a test suite for `gammac` without writing a single new expectation, and it makes the
reference implementation the specification of everything the language specification
leaves to implementations: message texts, spans and recovery after errors.

## Stages

1. **Lexer** — done. `gammac lex` matches `gamma lex` on the whole repository, with and
   without the layout recovery that tools use after errors.
2. **Parser** — done. The syntax tree, the parser with its error recovery, and the dump
   format: `gammac parse --dump` matches `gamma parse --dump` on the whole repository,
   syntax errors included.
3. **Modules and packages** — done. Loading the root module and every module its imports
   name, packages and their path dependencies from `gamma.toml`, collecting each
   module's top-level names (including the taken branches of top-level `when`s), and
   import cycles: `gammac modules` matches `gamma modules` on the root of every program in
   the repository. Git and archive dependencies come from the package manager (see
   below), and a malformed `gamma.toml` is reported with the reference's messages. Not
   yet: `when` conditions beyond `target`, `@size_of`, literals and constants.
4. **Checker** — done, in two parts. `gammac decls` and `gammac tir` match `gamma decls`
   and `gamma tir` on every program in the repository, error messages included.
   - **Declarations.** Resolving every top-level declaration without checking function
     bodies: types and type expressions, structs and unions (generic ones and their
     instances), vector, complex and `#soa` types, effects, function signatures,
     constraints, untyped constants with exact arithmetic, typed constants, array
     lengths and compile-time arguments (computed by calling functions where needed),
     `foreign` blocks and `#global`s.
   - **Bodies.** Expressions, statements, patterns, inference, effects, the instantiation
     of generic functions and compile-time evaluation, producing the typed intermediate
     representation of `src/tir.rs` (`compiler/tir/`). `gamma tir` prints that
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
     the trap messages and the compile-time call stack of the reference.

     Generic functions are checked once with their type parameters opaque, as the
     reference does; calls of them infer the type arguments, check constraints and name
     the instance. Each instance is then produced as the reference produces it: its body
     is rewritten for the type arguments, so that what it calls is instantiated in turn
     (with the nesting limit of §16.7), what depends on the type arguments (literals,
     layout queries, `.bits`, format specs, templates that are compile-time parameters,
     computed array lengths, static assertions, type tests, `#soa` operations, the
     handler declarations a `with` chooses) is checked for it, and the function
     literals and handler clauses written in a generic function are copied for each
     instance.

5. **Back end** — done. `gammac build` compiles a program to an executable through C, as
   `gamma build` does: it checks the program, generates C, puts the runtime
   (`runtime/gm_rt.h` and `runtime/gm_rt.c`) in front of it and runs the C compiler. Every
   conformance `run` test compiled this way gives the expected output and exit status.
   The C back end (`compiler/cgen/`) is a port of `src/cgen/` with the same representation
   of values and the same use of the runtime, so values have the same bytes, checks trap
   with the same messages, and effects, handlers, fibers and continuations work the same. It leaves out
   what only makes the generated code faster or quicker to compile — the specialization
   of functions for the handler that is their context, reading parameters in place,
   splitting long functions, constant arguments in static storage — and what it has no use
   for yet: the cache of compiled package C sources. The Safe mode, freestanding targets,
   other targets and the caches of the compiled runtime and of built programs are in
   the section "Targets, modes and caches" below.

   Before generating code, `gammac` checks every function body the program can reach,
   including the core library's (which `gammac tir`, like `gamma tir`, leaves out), and
   produces the instances of generic functions that those bodies call. A program that
   performs `Alloc` or one of the input and output effects runs inside the core library's
   default handlers, as with `gamma`.
6. **Bootstrap** — done. `gammac` compiles itself: the `gammac` built by `gamma build`
   (stage 1) builds a second `gammac` (stage 2), and the C that stage 2 generates for
   `gammac`'s sources is byte-identical to the C stage 1 generated, so the compiler built
   by itself is a fixed point. Stage 2 also checks `gammac`'s sources exactly as stage 1
   does.

```sh
gamma build compiler -o gammac1          # stage 1, built by the reference implementation
GAMMA_STD=lib/std ./gammac1 build compiler -o gammac2
GAMMA_STD=lib/std ./gammac2 build compiler --emit-c -o gammac3
cmp gammac2.c gammac3.c                  # identical
```

`gammac build [--mode=debug|checked|fast] [-g] [-o EXE] [--emit-c] ROOT` writes the C next to
the executable (`EXE.c`, the runtime and the program as one translation unit) and compiles
it with `$CC` (default `cc`); `--emit-c` stops after writing it. A Checked or Fast build
compiles `EXE.c` itself at `-O2`, so the optimizer sees the whole program. A Debug build
favors turnaround, as `gamma build` does: it compiles the runtime on its own (`EXE.rt.c`)
and splits the program into units (`EXE.u0.c`, `EXE.u1.c`, …) that share a header of
types and declarations and divide the function bodies between them. One unit is made per
96 KiB of function text, up to the number of processors and at most 8 (`GAMMA_C_UNITS`
forces a number, which tests use to split small programs); all are compiled at once and
then linked. On four cores this brings building `gammac` itself from about 110 to about 40
seconds. The runtime's sources are read from `$GAMMA_RUNTIME`, or else from the
`runtime` directory two levels above the standard library's. It runs on Unix-like systems;
on Windows, use `gamma build`.

`gammac build -g` (and `run -g`) adds debug information, as `gamma build -g` does
(`compiler/cgen/debug.gm` and `views.gm` port `src/cgen/debug.rs` and `views.rs`). Each
statement of the generated C is preceded by a `#line` directive naming its `.gm` file and
line, so a debugger stops at Gamma lines (`break file.gm:17`), and the C between functions
keeps its own location. Frame fields also have the names of their Gamma locals
(`F.count`, `F.p.x`) with a debugger view of their type (a C type laid out like the Gamma
value, with members named after fields, variants and the like), and struct mirrors name
their fields after the Gamma fields; a Gamma name that is a macro of a system header is
suspended around those declarations. The build is optimized no further than `-O0` (Debug) or
`-O2` and compiled with `-g`. On Linux and FreeBSD the gdb pretty printers
(`runtime/gdb_printers.py`) are embedded in the executable; `gammac build -g` also writes the
lldb formatters (`runtime/lldb_formatters.py`) to `gamma_lldb.py` next to it. Both Python
files are read from the runtime directory at build time. The `#line` directives name the
absolute path of the source file, found from the working directory without resolving
symbolic links.

`gammac run [--mode=…] ROOT [ARG…]` builds the program the same way into a new temporary
directory, runs it with the arguments after the file and ends with its exit status (128
plus the signal number if a signal ended it, 134 for a trap), as `gamma run
--backend=c` does. `gammac check ROOT` checks a program as `gamma check` does and prints
only the diagnostics; `tests/selfhost.rs` compares them, in the human format, with the
reference's on every program. Diagnostics are printed in the human format by default, and
`--diag-format=short|json` selects the others, as with `gamma`.

`gammac decls` and `gammac tir` read the core library from `$GAMMA_CORE`, or else from
the `core` directory next to the standard library's. A root module without `main` is
checked as a library, as `gamma tir` does: the way `gamma test` would check it, with
its tests and benchmarks and those of the modules of its package that it imports.

### The test runner

`gammac test [options] ROOT` and `gammac bench [options] ROOT` (`compiler/testing/`, a port
of `src/testing.rs` for the C back end) check the program as a library with its tests (the
modules of the root's package that it imports; all of them with `--all`), compile the
chosen tests into one executable whose `main` runs the test given by its first argument,
and run it once per test with captured output. They print what `gamma test --backend=c`
and `gamma bench` print, with the same options: `--filter=`, `--all`, `--show-output`,
`--jobs=N` / `-j N` (0: one per processor; the output stays in order), `--timeout=SECONDS`,
`--time`, `--mode=` and `--diag-format=`. A benchmark builds in fast mode unless `--mode`
is given. `--package=NAME` tests a dependency of the input's package (or one of its
dependencies) instead. `--backend=interp|cranelift`, `--coverage` and `--target` are
usage errors. A test that exceeds `--timeout` is killed by a watching thread, which
cannot reach processes the test started or a test that closed its output and then ran on.
`tests/selfhost.rs` runs the `test` conformance tests through `gammac test` and compares
a standard library module, options and failures with the reference.

### The formatter

`gammac fmt [--check] <file or directory>...` is `gamma fmt` (`compiler/fmt/`, a port of
`src/fmt.rs` and the command in `src/main.rs`): it formats `.gm` files in place, walks
directories in name order (skipping hidden directories and a package's `target`), with
`--check` lists the files that would change instead, and with `-` filters standard input
to standard output. It works on the tokens of `gammac`'s own lexer, and like the
reference it lexes its result again and refuses to write anything that would change the
tokens. For the same text it gives the same output, the same lexical error messages and
the same exit statuses (1 if a file fails or, with `--check`, would change; 2 for a usage
error). Only the texts that name the tool differ: the messages for an unreadable file or
a bad command line start `gammac:` instead of `gamma:`, and the usage text is `gammac`'s.
`tests/selfhost.rs` compares the output on every `.gm` file in the repository and on
variants of each that the formatter must change (no indentation, collapsed or added
spaces, CR LF line ends, cut off in the middle), and compares whole directories, in place
and with `--check`.

### API documentation

`gammac doc` is a port of `gamma doc` (`src/doc/`): it writes the API documentation of a
module, or with `--all` of every module below a directory, as Markdown or HTML, from the
public declarations and doc comments of the syntax tree that `gammac`'s parser builds. It
takes the options of `gamma doc` (`--format=markdown|html`, `-o PATH`, `--all`,
`--missing`; see `gammac doc --help`) and writes the same files byte for byte, so
`gammac doc lib/std --all -o docs/std` regenerates the standard library reference. The
code is in `compiler/doc/`, one file for each part of `src/doc/` (`collect.gm` reads a
module, `markdown.gm`, `html.gm` and `md_html.gm` render it, `doc.gm` is the command line).
A module with syntax errors is reported with the diagnostics of `gammac check` in the
human format, and the command fails. It differs from the reference in two small ways: its
messages start `gammac:` instead of `gamma:`, and the name of a module is found without
resolving symbolic links, so a link to `lib/std` is documented under the link's name. Like
the reference, it treats the Unicode white space characters as white space when it renders
doc comments.

`tests/selfhost.rs` runs both commands on `lib/std` in both formats, on single modules and
files, on the fixtures of `tests/doc/` and on the error cases, and compares what they
print and every file they write.

### Lint and fix

`gammac lint [options] <file or directory>` is `gamma lint` and `gammac fix <file or
directory>` is `gamma fix`. The lint rules are split as in the reference: those that
walk the syntax tree are in `compiler/lint/syntax.gm`, those that read the checked
program (unused locals and declarations, variables that never change, comparisons with a
known result, dangling addresses, and the mutated-copy and present-only-binding rules) in
`checked.gm`, `copy.gm` and `present.gm`; `lint.gm` holds the rules' table, their levels
(defaults, the `[lint]` table of `gamma.toml`, the command line, `#allow`) and the
diagnostics. `compiler/linting/` is the command line, with `--fix` and its rounds;
`compiler/fix/` plans and applies the fixes that diagnostics carry, in rounds, for
`gammac fix` and `gammac lint --fix`. Both only edit the package's own files, never the
standard library.

To report what the rules need, the checker records which declarations and locals are used
(`Sema.decl_uses`, `outside_uses`, `local_uses`, `local_use_counts`, `nested_owner`);
`gammac tir` and `gammac decls` do not look at them. Linting checks the program with its
tests (as `gamma test` would), and, in a package, with the other modules of the package.
Warnings carry their rule: `warning[unused-variable]: …` in the human and short formats,
and a `code` field in JSON. Messages that name the tool (`gamma: applied 2 fixes in 1
file`, `gamma lint: 3 warnings`) keep the reference's text, so that output compares
equal. Differences: `--target` is not an option of `gammac`, and messages for bad command
lines start `gammac:`.
`tests/selfhost.rs` compares `gammac lint` with `gamma lint` in the JSON format on every
program root at the default levels and with `--warn=all`, and `gammac fix` and `gammac
lint --fix` on scratch copies of the single-file conformance programs, comparing the
output and the edited files.

### Packages and build settings

`gammac new`, `init`, `fetch`, `update`, `add`, `remove` and `cache` are the package
commands of `gamma`, with the same options, output and files. The code is in
`compiler/pkg/`: `git.gm` and `archive.gm` run `git`, `curl` and `tar` (as the reference
does, with the same flags), `resolve.gm` picks the revisions, `lock.gm` reads and writes
`gamma.lock`, `cache.gm` is `cache clean`, `scaffold.gm` is `new` and `init`, `edit.gm`
edits `gamma.toml` for `add` and `remove`, and `cli.gm` parses the command lines. The
manifest itself (`manifest.gm`) and version requirements (`semver.gm`, with the messages
of the `semver` crate) are in `compiler/load/`, because the loader reads manifests too.
Syntax errors in `gamma.toml` and `gamma.lock` read as the TOML library of the reference
words them, as far as a manifest can have them. `edit.gm` edits the lines of the text
instead of a document tree, which keeps comments and layout the same for the manifests
the commands handle (line ends are written as `\n`, as the reference does).

`gammac check`, `build`, `run`, `test`, `bench`, `lint` and `fix` fetch what the lock
file does not cover before they load the program, take `--offline`, `--locked` and `--profile=NAME`, and apply the
`[build]` table and the profile of the root package's `gamma.toml`: `mode` (under
`--mode`), `cflags` and `ldflags` (before `CFLAGS` and `LDFLAGS`), and, in a native build,
the `c-sources`, `include-dirs`, `libs` and `lib-dirs` of the package and its
dependencies. The C sources are compiled again by every build (there is no object cache),
each with the include directories of its own package. `CC` may hold arguments.

Differences: `debug-info` of the manifest follows `-g`; `gammac modules`, `decls` and
`tir` do not look at the lock file, as the reference's do not; the C sources of packages
have no object cache (so a program with such sources is not kept in the cache of built
programs either); and messages start `gammac:`.
`tests/selfhost.rs` runs both programs over the same steps in two scratch directories
(local `file://` repositories and archives) and compares what they print and write.

### Targets, modes and caches

`gammac check`, `build`, `run`, `test` and `bench` take `--target=TRIPLE` and `--mode=safe`
and read `CC`, `CFLAGS`, `LDFLAGS`, `GAMMA_RUNNER`, `WASI_SDK_PATH` and `WASM_OPT` as
`gamma` does. The target is a run-time value (`load.Target`, in `compiler/load/target.gm`)
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

`tests/selfhost.rs` compares `gammac` with `gamma` on `check --target=...` over every
program, on build, run, test and bench for other targets (when the cross compiler and the
emulator are installed), on the freestanding firmware example, and on the error cases (an
unknown triple, a missing compiler, Safe mode without `filcc`). The Safe mode itself is
compared only where `/opt/fil/bin/filcc` exists. Not ported: `lint` and `fix` with
`--target`, the quoting of Windows command lines, and the private (0700) mode of the cache
directories, which the standard library cannot set.

### Growing a stage

A stage that covers a large part of the language grows over several steps. Until it is
complete, `gammac` reports each construct it cannot handle yet with an error that starts
`gammac cannot`, and `tests/selfhost.rs` compares only the programs without such an
error. Everything else must match exactly. The number of programs compared may only
grow: the test fails if it drops below `DECLS_FLOOR` (declarations) or `TIR_FLOOR`
(bodies), which are raised as `gammac` learns more. Both checker stages are complete, so
both floors are the number of programs in the repository: a program that `gammac` stops
checking fails the test.

`tests/selfhost.rs` builds `gammac` through the C back end in the Debug mode, so that its
overflow and bounds checks are on, when a C compiler is available, and otherwise runs it
in the interpreter (slower, but the same results). With a C compiler, on Unix, it also
compiles every conformance `run` test with `gammac build` and runs it against the test's
expectations, and runs the bootstrap above.

## Conventions

- **Memory.** A compilation allocates from an arena that is released at the end, so the
  compiler's data structures do not free their parts one by one. An allocation failure
  traps (`x!` on the result): a compiler that runs out of memory has nothing better to
  do.
- **Errors are diagnostics.** Problems in the input are reported to a `diag.Diagnostics`
  and the stage goes on, as the reference implementation does; only a failure of the
  environment (an unreadable file) is an error value.
- **No dependencies** beyond the standard library.
- **Mirror the reference.** Each Gamma module corresponds to a part of `src/` and keeps its
  structure and comments, so that a change to one is easy to carry to the other. Where
  Gamma has a better way to say the same thing (`try` for `?`, `defer` to restore parser
  state), the Gamma code uses it; [Idiomatic Gamma](guide/idioms.md) collects these.
- Sources are formatted with `gamma fmt compiler`; a test checks it.
