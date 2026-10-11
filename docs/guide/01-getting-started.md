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

### Build modes

The specification defines three build modes: **Debug** (all checks, plus extra diagnostics such as poisoning freed memory), **Checked** (all checks, optimized) and **Fast** (checks removed, optimized). They are covered in [chapter 13](13-safety.md). How you select a mode is up to the implementation; the reference tool takes `--mode=debug|checked|fast` (debug is the default) on `run`, `build` and `test`. The mode selects which checks are compiled in and whether frees are checked; the C back end always optimizes.

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
}
```

A test passes if it finishes, and fails if `expect` fails, a trap occurs, or the test allocator detects a leak. Returning an error also fails the test, so `try` works inside tests. More on tests in [chapter 13](13-safety.md).

`gamma test file.gm` (or a module directory) runs the tests of the root module, which then needs no `main`. Each test runs on its own; its output is captured and shown only if it fails:

```text
$ gamma test squares.gm
test "squares" ... ok
test "cubes" ... FAILED
    expectation failed
      at squares.gm:12:5
1 passed, 1 failed
```

The command exits with status 1 if any test failed.

Tests run in the interpreter by default. A module that calls C functions or contains `@asm` cannot run there, so its tests are compiled with the C back end instead, and `--backend=c` does the same for any module. The compiler builds one executable and runs it once per test; the report is the same as above.

## Where to go next

[Chapter 2](02-basics.md) covers the everyday vocabulary: bindings, literals, numeric types and operators.

For more detail on the topics in this chapter, see [§3 Programs, modules and top-level declarations][s3] and [§2 Lexical structure][s2] of the specification.

[s2]: ../../gamma-spec.md#2-lexical-structure
[s3]: ../../gamma-spec.md#3-programs-modules-and-top-level-declarations
[s21]: ../../gamma-spec.md#21-the-core-library
