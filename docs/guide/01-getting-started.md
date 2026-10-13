# 1. Getting started

[Contents](README.md) · [Next: Basics](02-basics.md)

This chapter shows the smallest Gamma program, explains what you can do with the `gamma` tool today, and sketches how source files and modules fit together.

## Hello, world

```gamma
main :: fn() \ {Out} {
    print("hello, world\n")
}
```

Four things are happening here, and each one comes back in later chapters.

- `main :: ...` **binds a name**. In Gamma, `::` introduces an immutable binding, and everything at the top level of a file is one: functions, types, constants, imports. There is no separate "function declaration" syntax; a function is a value written as `fn(...) { ... }` and given a name with `::`.
- `fn()` is a function with no parameters and no `-> T`, so it returns `()` (the unit value).
- `\ {Out}` is the function's **effect set**. It says "this function may perform output, and the caller must provide something to handle it". `print` performs the `Out` effect, so any function that calls `print` must either list `Out` or handle it. For `main`, the runtime installs a default handler that writes to standard output, so `\ {Out}` is all it takes. Leave it off and the program is rejected: Gamma does not allow I/O to happen without the signature saying so. [Chapter 7](07-effects.md) explains effects fully.
- `print("hello, world\n")` writes the string. There is no implicit newline, and adjacent string literals are never concatenated.

`print` takes a template with `{}` placeholders. With one placeholder the argument follows directly; with two or more, the arguments go in a tuple:

```gamma
square :: fn(x: i32) -> i32 => x * x

main :: fn() \ {Out} {
    n :: 12
    print("{}\n", square(n))
    print("{} squared is {}\n", (n, square(n)))
}
```

The placeholder count is checked at compile time against the arguments. Every type has a fixed, structural display format (integers in decimal, structs as `Name(field: value, ...)`, enums as `.Name`, and so on); see §21.6.1 of the [specification][s21].

A placeholder can carry a format spec after a colon, for tables and numbers: a width and alignment (`{:>8}`, `{:<12}`, `{:^5}`, with a fill character as in `{:*>8}`), a sign or zero padding (`{:+}`, `{:05}`), an integer base (`{:x}`, `{:#X}`, `{:b}`, `{:o}`), and for floats a precision or scientific notation (`{:.2}`, `{:e}`, `{:.3e}`):

```gamma
print("{:<8}|{:>6}|{:8.2}\n", ("apples", 12, 0.8333))     // apples  |    12|    0.83
print("{:#06x} {:+} {:b}\n", (255, 5, 5))                  // 0x00ff +5 101
```

A spec that does not fit the argument (`{:x}` on a float) is a compile error.

### Returning a status

`main` has three permitted shapes (§3.7 of the [specification][s3]). The effect set may be anything drawn from the ambient effects (`Alloc`, `Out`, `Err`, `In`, `Clock`, `Rand`, `Fs`, `Net`, `Env`, `Sys`):

```gamma
// 1. no result: exit status 0
main :: fn() \ {Out} { ... }

// 2. an exit status
main :: fn() -> u8 \ {Out} {
    print("something went wrong\n")
    return 2
}

// 3. a result: success exits with 0, an error prints its type and variant
//    to standard error and exits with 1
main :: fn() -> E!() \ {Out} { ... }
```

(Only one `main` can exist in a module, of course; the three are alternatives.) Chapter 5 covers the `E!()` form.

## The `gamma` tool

> **Status.** The reference implementation of Gamma is under construction. Today the `gamma` tool lexes, parses and type-checks programs and runs them with an interpreter. The core of the language works: functions, literal inference, integers and floats, structs, unions, tuples, arrays and slices, optionals and results, patterns and `match`, function literals, formatted output with `print`, effects and handlers, fibers, iterators, generics, allocation with lists and maps, modules, tests and calls to C. `gamma build` compiles a program to a native executable through the system C compiler; `gamma run --backend=c` compiles and runs it in one step. The standard library beyond the core library is a set of modules under `std/` ([the standard library reference](../std.md)).

You build the tool from the repository with Cargo, then use it like this:

```text
gamma run hello.gm         type-check the program and run it
gamma build hello.gm       compile it to a native executable `hello` (-o picks the name)
gamma check hello.gm       type-check without running
gamma lex file.gm          print the tokens of a source file
gamma parse file.gm        check the syntax of a source file
gamma parse --dump file.gm check the syntax and print the syntax tree
gamma fmt file.gm          format a source file in place (a directory: every .gm file in it)
gamma fmt --check file.gm  list the files that formatting would change, without writing
gamma help                 print usage
gamma version              print the compiler version
```

A program is a single `.gm` file or a directory of `.gm` files (one module). Arguments after the file name of `gamma run` are passed to the program. `--diag-format=human|short` chooses how diagnostics are printed; they go to standard error, and the tool exits with status 1 if there were errors. When a running program traps (for example on integer overflow), the tool prints the reason and the source location and exits with status 134.

`gamma run` interprets the program, which starts at once and gives the most precise diagnostics, but runs it roughly 10 to 100 times slower than compiled code. For programs that do real work (megabytes of input, long loops, benchmarks), use `gamma run --backend=c`: the first run pays for a C compilation (around a second), and later runs of the same program reuse the executable, which is kept in the cache directory described below. The two back ends give the same output.

### Build modes

The specification defines three build modes: **Debug** (all checks, plus extra diagnostics such as poisoning freed memory), **Checked** (all checks, optimized) and **Fast** (checks removed, optimized), and an optional fourth, **Safe** (all checks, and every memory access checked; the reference tool builds it with Fil-C). They are covered in [chapter 13](13-safety.md). How you select a mode is up to the implementation; the reference tool takes `--mode=debug|checked|safe|fast` (debug is the default) on `run`, `build` and `test`. The mode selects which checks are compiled in and whether frees are checked. The C back end optimizes Checked and Fast builds fully; Debug builds are optimized lightly so that they compile quickly (and not at all with debug information, below). Debug builds also reuse a precompiled copy of the runtime, and `gamma run --backend=c` reuses the executables of programs it has built, both kept in `~/.cache/gamma` (set `GAMMA_CACHE_DIR` to use another directory, or to an empty value to turn the cache off).

### Debugging native code

`gamma build -g` adds debug information that maps the executable to the Gamma source, so a debugger such as `gdb` or `lldb` stops at `.gm` lines and shows them, and backtraces, profilers and sanitizers name them too. The generated C is kept next to the executable (`hello.c`). In a debugger, the locals of the current function are fields of the frame `F`, under their Gamma names: `print F.count`, `print F.p.x`. Values show with their Gamma structure: tuple elements as `_0`, `_1`… (or their names), a tagged union as its `tag` and a member per variant (`F.shape.Rect.w`), a slice as `ptr` and `len` (`print *F.xs.ptr@F.xs.len`), an optional as `value` and `present`, a result as `is_err`, `ok` and `err`. On Linux the executable also carries pretty printers that gdb loads once it trusts the file (`add-auto-load-safe-path <dir>` in `~/.gdbinit`, or `gdb -iex 'add-auto-load-safe-path .'`): they print `Shape_Rect = {w = 2, h = 3}`, slices as their elements, strings as text, optionals as their value or `null` and results as their value or `!error`. A Debug build with `-g` is compiled without optimization, so every local is there; Checked and Fast builds stay optimized. `-g` also works with `gamma run --backend=c` and `gamma test --backend=c`.

```text
gamma build -g hello.gm
gdb ./hello
(gdb) break hello.gm:12
(gdb) run
(gdb) print F.total
(gdb) print F.shape
$2 = Shape_Rect = {w = 2, h = 3}
```

### The C compiler and cross-compiling

Native builds use the C compiler named by the environment variable `CC` (default `cc`), which may include arguments. `CFLAGS` adds compiler options after the tool's own, so they can override them, and `LDFLAGS` adds linker options. Quotes and backslashes work as in a shell, so a path with spaces can be quoted.

When `CC` or `CFLAGS` selects another target, the program is compiled for that target: the tool asks the compiler which machine it targets, and the `target` constants (`target.arch`, `target.os`, `target.endian`, `target.ptr_bits`) and the layouts of types follow it. Any C compiler that can cross-compile works:

```text
CC="zig cc -target aarch64-linux-musl" gamma build hello.gm
CC="clang --target=aarch64-linux-gnu --sysroot=/path/to/sysroot" gamma build hello.gm
CC=aarch64-linux-gnu-gcc LDFLAGS=-static gamma build hello.gm
```

`gamma check` and `gamma run` follow the same target, so `CC=… gamma check` checks the target-specific branches of `when`. The C back end needs a target with `__int128` (in practice, a 64-bit one) and POSIX threads. Little- and big-endian targets both work (`s390x` is tested); `f16` also needs a C compiler with `_Float16`, which GCC lacks for s390x.

**`--target=<triple>`** (accepted by `check`, `run`, `test` and `build`) names the machine to compile for and picks the compiler for it:

- With `CC` unset, the compiler is `<triple>-gcc` if that is on `PATH` (as installed by Debian's `gcc-s390x-linux-gnu` and similar packages), and otherwise `clang --target=<triple>` if `clang` is.
- With `CC` set, it is used as written, except that `--target=<triple>` is appended when it is clang (`-target <triple>` for `zig cc`) and names no target itself. A gcc in `CC` has to be the cross compiler for the triple already.
- `CFLAGS` and `LDFLAGS` apply as usual.

Either way the compiler is asked which machine it targets, and the build stops with an error if that is not the triple's architecture, byte order and operating system (`gamma build --target=s390x-linux-gnu` with a compiler for AArch64 is an error, not a wrong executable). `--target` cannot be combined with `--mode=safe`.

```text
LDFLAGS=-static gamma build --target=s390x-linux-gnu hello.gm
```

An executable built for another machine cannot run on this one by itself. `gamma run --backend=c` and `gamma test` start the program behind the command in `GAMMA_RUNNER`, an emulator or a remote-execution wrapper (split into words like `CC`), when that is set:

```text
GAMMA_RUNNER="qemu-s390x -L /usr/s390x-linux-gnu" LDFLAGS=-static \
    gamma run --backend=c --target=s390x-linux-gnu hello.gm
```

The program is then named by its path in `argv[0]` rather than by the source file.

## Source files

Gamma source files are UTF-8 text with the extension `.gm`. Some basics of the lexical rules, all of which will be unsurprising to a C programmer except where noted:

```gamma
// a line comment

/* a block comment
   /* block comments nest */
   still a comment */

/// a doc comment attaches to the declaration that follows it
answer :: 42
```

- Identifiers are ASCII letters, digits and `_`, case-sensitive. A lone `_` is the *discard* and cannot be read.
- Tabs and spaces are whitespace. No other Unicode whitespace is allowed outside comments and strings, and a stray carriage return is an error.
- **Newlines end statements.** You never need a `;` (you can use one to put two statements on a line). A line continues onto the next only in specific, predictable situations; [chapter 2](02-basics.md) lists them.

## Modules at a glance

A **module is a directory**. Every `.gm` file directly inside the directory belongs to the module, and all files of a module share one top-level namespace. The order of files, and of declarations within a file, means nothing, so you can use a function before the line that defines it. Subdirectories are separate modules.

A **package** is a directory tree with a `gamma.toml` manifest at its root; the manifest names the package and lists its dependencies. The **root module** is the one you name on the compiler's command line, and it must declare `main`.

Suppose a package looks like this:

```text
hello/
    gamma.toml
    main.gm
    greet/
        greet.gm
```

`greet/greet.gm` defines a public function:

```gamma
pub hello :: fn(name: string) \ {Out} {
    print("hello, {}\n", name)
}
```

and `main.gm` imports the directory as a module:

```gamma
greet :: import "./greet"

main :: fn() \ {Out} {
    greet.hello("Ada")
}
```

Points worth knowing:

- `import` is an expression that may only be the initializer of a top-level `::` binding. The name on the left (`greet`) is chosen by the importer and has nothing to do with the path.
- A path starting with `./` or `../` is relative to the importing module. Any other path starts with a package name: `std` is the standard library, and every other name must be a dependency listed in `gamma.toml`.
- Everything is **private to its module unless marked `pub`**. A private name is visible from every file of the same module, and from nowhere else. Struct fields have the same rule (§3.3).
- A module binding is not a value: you cannot store it, pass it or compare it. The import graph must be acyclic.
- A binding that just names another declaration is an alias for it: `Shape :: geom.Shape`, `area :: geom.area`, `Map :: HashMap`. Generic types and functions work through aliases and qualified names alike (`geom.Pair(i32)`). A `pub` alias re-exports the declaration, and a `pub` import is itself a member, so a module can gather others: with `pub geom :: import "./geom"` in module `lib`, `lib.geom.area(s)` works.
- The manifest is a small TOML file. This implementation reads the `[dependencies]` section, where each dependency is a directory given relative to the manifest; other keys are ignored:

  ```toml
  [package]
  name = "hello"

  [dependencies]
  json = { path = "../json" }      # import "json" or "json/sub/dir"
  ```

  A directory tree without a manifest is a package of its own, rooted at the root module's directory.
- Beyond the names that are always available (the *universe scope*: `string`, `print`, `List`, `HashMap`, `range`, and the rest listed in §21.1), this guide only relies on the module `std/c` in the interop chapter (and mentions `std/thread` there). Other `std/...` modules are outside what the specification defines; see the [standard library reference](../std.md).

## Tests

A top-level `test` declaration is compiled only when building tests. Its body can perform the ambient effects, which the test runner replaces with fakes (captured output, an in-memory file system, a leak-checking allocator):

```gamma
test "squares" {
    expect(square(7) is 49)
    expect_eq(square(-3), 9)          // on failure, shows both values
}
```

A test passes if it finishes, and fails if `expect` or `expect_eq` fails, a trap occurs, or the test allocator detects a leak (the report lists each leaked block and, for `new`, `make` and `dupe`, the line that allocated it). `try` works inside tests on any error type: an error fails the test and is reported. More on tests in [chapter 13](13-safety.md).

`gamma test file.gm` (or a module directory) runs the tests of the root module, which then needs no `main`, and of every module of the same package that it imports, directly or indirectly. The tests of the standard library and of dependency packages are not run. Each test runs on its own; its output is captured and shown only if it fails:

```text
$ gamma test squares.gm
test "squares" ... ok
test "cubes" ... FAILED
    expectation failed
      at squares.gm:12:5
1 passed, 1 failed
```

A test outside the root module is reported with its module's directory, relative to the package root: `test "parses" in util/json ... ok`. The command exits with status 1 if any test failed.

`--filter=text` runs only the tests whose name or module directory contains `text`; the summary line then also counts the tests left out (`2 passed, 0 failed, 5 filtered out`).

Tests run in the interpreter by default. A module that calls C functions or contains `@asm` cannot run there, so its tests are compiled with the C back end instead, and `--backend=c` does the same for any module. The compiler builds one executable and runs it once per test; the report is the same as above.

## Where to go next

[Chapter 2](02-basics.md) covers the everyday vocabulary: bindings, literals, numeric types and operators.

For more detail on the topics in this chapter, see [§3 Programs, modules and top-level declarations][s3] and [§2 Lexical structure][s2] of the specification.

[s2]: ../../gamma-spec.md#2-lexical-structure
[s3]: ../../gamma-spec.md#3-programs-modules-and-top-level-declarations
[s21]: ../../gamma-spec.md#21-the-core-library
