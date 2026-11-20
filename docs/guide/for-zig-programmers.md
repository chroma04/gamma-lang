# Gamma for Zig programmers

[Contents](README.md) · [Gamma for C programmers](for-c-programmers.md) · [Gamma for Rust programmers](for-rust-programmers.md)

This page is for programmers who know Zig and want to know where Gamma agrees with it and where it went another way. Of the languages Gamma is usually compared with, Zig is the closest: both are C replacements without a garbage collector or hidden control flow, both make allocation explicit, both have `defer` and `errdefer`, error unions with `try`, optionals, compile-time type parameters and a C-compatible memory layout. The tutorial chapters, which this page links to, go into depth; the [language specification](../../gamma-spec.md) is normative.

The differences come from one decision. Zig makes dependencies explicit by **passing them**: an `std.mem.Allocator` argument, a writer argument, an `io` parameter. Gamma makes them explicit by **declaring them**: a function lists the *effects* it performs (`needs Alloc + Out`), and the code that calls it installs *handlers* for them. The allocator is still chosen by the caller and still visible in the signature, but it is not threaded through every call by hand, and the same mechanism carries I/O, time, randomness, logging, test fakes, generators and fibers.

The other large difference is compile time. Zig's `comptime` is a second programming language over types: functions that take and return `type`, `@typeInfo`, `inline for` over fields. Gamma's compile-time evaluation runs ordinary functions on values, and types are parameters, never results of computation. That is less powerful by design; what it gives up and what replaces it is described [below](#compile-time-type-parameters-not-type-functions).

## What will feel familiar

A small program in each language: parse a decimal number into a `u8`, with a typed error. The Zig version:

```zig
const ParseError = error{ Empty, BadDigit, TooBig };

fn parseU8(s: []const u8) ParseError!u8 {
    if (s.len == 0) return error.Empty;
    var n: u8 = 0;
    for (s) |c| {
        if (c < '0' or c > '9') return error.BadDigit;
        n = std.math.mul(u8, n, 10) catch return error.TooBig;
        n = std.math.add(u8, n, c - '0') catch return error.TooBig;
    }
    return n;
}
```

And in Gamma:

```gamma
ParseError :: union { Empty, BadDigit: usize, TooBig }

parse_u8 :: fn(s: string) -> ParseError!u8 {
    if s.len is 0 { return !.Empty }
    n: u8 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
        n = n *? 10 +? (c - '0') else return !.TooBig
    }
    return n
}

main :: fn() needs Out {
    for s in [_]string("42", "", "4x2", "300") {
        if n := parse_u8(s) {
            print("{} -> {}\n", s, n)
        } else e => {
            print("{:q} -> {}\n", s, e)
        }
    }
}
```

```output
42 -> 42
"" -> .Empty
"4x2" -> .BadDigit(1)
"300" -> .TooBig
```

The shape is the same: an error union `E!T`, `return !e` for `return error.E`, early exit on failure. The differences in this one program are small and typical:

- An error type is **any type**, usually a union, so an error can carry data (`.BadDigit(i)`), which a Zig error set cannot.
- `*?` and `+?` are arithmetic that gives `null` on overflow, so `a *? 10 +? d else …` replaces `std.math.mul` and `std.math.add` with `catch`.
- `if n := r { … } else e => { … }` is `if (r) |n| … else |e| …`.
- `print` writes formatted output with no writer argument and no `try`: the `needs Out` in `main`'s signature is the dependency.

## A translation table

| Zig | Gamma | Notes |
| --- | --- | --- |
| `const x = 5;` / `var x: u32 = 5;` | `x := 5` / `x: u32 = 5` | every local is mutable; constants are `X :: 5` |
| `undefined` | `---` | only for plain data; reading it gives an unspecified value, never undefined behavior |
| `@import("std")` | `str :: import "std/str"` | one module per file or directory |
| `pub fn f() void` | `pub f :: fn()` | functions are constants bound with `::` |
| `?T`, `orelse`, `.?` | `?T`, `else`, `x?` | `x?` traps on `null` in checked builds |
| `E!T`, `try`, `catch` | `E!T`, `try`, `else e =>` | `x!` is `x catch unreachable` |
| `error.Name` | `!.Name` | a variant of the function's error union |
| inferred error sets (`!T`) | none | the error type is written; [lifting](05-optionals-and-errors.md) composes them |
| `defer`, `errdefer` | `defer`, `errdefer` | the same |
| `switch` | `match` | exhaustive, with patterns and bindings |
| `union(enum)` | `union` | always tagged; `#c_union` for C's untagged union |
| `enum` | `union` without payloads | |
| `packed struct(u16)` | `#bits(u16) struct` | |
| `extern struct` | `struct` | structs have the C layout; `#packed` removes the padding |
| `[]const u8` | `string` (= `[]const u8`) | strings are byte slices |
| `[*]T`, `[*:0]u8` | `[^]T`, `cstring` | |
| `comptime T: type` | `T: type` | compile-time parameters are ordinary parameters |
| `comptime if` / `if (builtin.os…)` | `when target.os is .Linux { … }` | |
| `inline for` | none | see [below](#compile-time-type-parameters-not-type-functions) |
| `std.mem.Allocator` parameter | `needs Alloc` | the caller installs the allocator with `with` |
| `anytype` writer | `needs Out`, or a `fn(bytes: []const u8)` parameter | |
| `@intCast`, `@truncate` | `T(x)` (lossless only), `x.checked(T)`, `x.trunc(T)` | |
| `+%`, `+|` | `+%`, `+|` | and `+?`, which gives `null` on overflow |
| `unreachable` | `@unreachable()` or `trap()` | |
| `test "name" { … }` | `test "name" { … }` | run with `gamma test` |
| `build.zig` | `gamma.toml` | a manifest, not a program |

## Allocation: declared, not passed

In Zig a function that allocates takes an allocator, and every caller passes one down:

```zig
fn joinWords(alloc: std.mem.Allocator, words: []const []const u8) ![]u8 {
    var out = std.ArrayList(u8).init(alloc);
    errdefer out.deinit();
    for (words) |w| {
        try out.appendSlice(w);
        try out.append(' ');
    }
    return out.toOwnedSlice();
}
```

In Gamma the same function says `needs Alloc`, and the allocator is whichever one the caller installed:

```gamma
arena :: import "std/arena"

join_words :: fn(words: []const string) -> AllocError![]u8 needs Alloc {
    out := list(u8)
    errdefer out.deinit()
    for w in words {
        try out.extend(w)
        try out.push(' ')
    }
    return out.into_slice()             // toOwnedSlice
}

main :: fn() -> AllocError!() needs Alloc + Out {
    // The default allocator: the program's heap.
    a := try join_words([_]string("on", "the", "heap"))
    defer free(a)
    print("{}\n", string(a))
    // An arena for a region of code: everything inside allocates from it.
    ar := arena.new()
    defer ar.deinit()
    with ar.allocator() {
        b := try join_words([_]string("in", "an", "arena"))
        print("{}\n", string(b))
    }
    return ()
}
```

```output
on the heap 
in an arena 
```

What carries over from Zig:

- **The caller chooses**, per region of code. `with ar.allocator() { … }` is the Gamma spelling of "pass the arena's allocator to everything in here".
- **Allocation is visible.** A function without `Alloc` in its signature does not allocate through the current allocator, so `needs Alloc` is as reliable a marker as an `Allocator` parameter.
- **Failure is a value.** Allocation returns `AllocError!T`; nothing aborts behind your back.
- **A container remembers its allocator.** Like `ArrayList`, a `List` keeps the allocator it was created with and uses it for every later operation, so it cannot be freed through another one (Debug builds also tag each block with its allocator and trap on a mismatch).

What does not: there is no unmanaged/managed split, because a container always carries its allocator, and there is no way to call an allocating function without some allocator installed (the program's `main` starts with the system heap). A function that wants an allocator as data, to keep in a struct, takes an `Allocator` value, which is what `Alloc.current()` returns. [Chapter 9](09-memory.md) covers allocators and arenas.

## Errors: types, not sets

A Zig error set is a set of names, global to the program, and errors carry no payload; data travels in an out-parameter or a diagnostics struct. A Gamma error is a value of any type. Most error types are unions, and a function's error type is written in its signature: there is no inferred error set. Composition is by **lifting**: if the function's error type is a union with exactly one variant whose payload is the callee's error type, `try` wraps the callee's error in that variant.

```gamma
ParseError :: union { Empty, BadDigit: usize }
IoError :: union { NotFound, Denied }
LoadError :: union { Parse: ParseError, Io: IoError, Missing }

read :: fn(name: string) -> IoError!string {
    if name is "secret" { return !.Denied }
    return "12x"
}

parse :: fn(s: string) -> ParseError!u32 {
    if s.len is 0 { return !.Empty }
    n: u32 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
        n = n * 10 + u32(c - '0')
    }
    return n
}

load :: fn(name: string) -> LoadError!u32 {
    text := try read(name)          // IoError lifts into .Io
    return try parse(text)          // ParseError lifts into .Parse
}

main :: fn() needs Out {
    print("{}\n", load("config") is !.Parse(.BadDigit(2)))
    print("{}\n", load("secret") is !.Io(.Denied))
}
```

```output
true
true
```

Where Zig merges error sets with `||`, Gamma nests them, and the nesting is information: `.Parse(.BadDigit(2))` says which step failed. An error that is a struct (a position and a message, say) is fine as an error type, but it is not lifted into: its other fields would have to be made up. A small function that builds it from what the call site knows does the job.

Error return traces have no equivalent. A trap (`x!` on an error, a failed bounds check) prints a backtrace in Debug builds; a propagated error is just a value.

## Undefined behavior and build modes

Zig has illegal behavior that is safety-checked in Debug and ReleaseSafe and undefined in ReleaseFast. Gamma has the same split with one rule (§18.1): **every check traps in Debug, Checked and Safe builds and is removed in Fast builds.**

| Zig | Gamma | |
| --- | --- | --- |
| Debug | `--mode=debug` (the default) | plus poisoned frames, a tagged heap, backtraces |
| ReleaseSafe | `--mode=checked` | every check, optimized |
| ReleaseFast | `--mode=fast` | checks removed |
| ReleaseSmall | none | |
| none | `--mode=safe` | every memory access checked (Fil-C) |

Two differences matter in practice. In Gamma most failed checks in Fast builds give an **unspecified value**, not undefined behavior: an overflowing `+`, a `null` unwrapped with `?`, a bad float-to-integer conversion produce some value of the type, and the program goes on. The list of operations that are undefined behavior is short and closed (§18.4): mainly memory errors and data races. And integer overflow is never undefined: plain `+` traps when checked, `+%` wraps, `+?` gives `null`.

## Compile time: type parameters, not type functions

Generic code takes types as parameters, written in the same list as the others and inferred from the arguments where possible:

```gamma
largest :: fn(T: type, xs: []const T) -> ?T where Ord(T) {
    best := xs.first() else return null
    for x in xs {
        if x > best { best = x }
    }
    return best
}

Pair :: struct(A, B: type) {
    first: A
    second: B
}

main :: fn() needs Out {
    print("{}\n", largest([_]i32(3, 9, 4)) else 0)
    p := Pair(i32, string)(first: 1, second: "one")
    print("{} {}\n", p.first, p.second)
}
```

```output
9
1 one
```

So far this is Zig's `comptime T: type`. The differences:

- **Constraints are declared.** `where Ord(T)` states what the body needs; the checker checks the body against it, once, and a call with a type that is not ordered is an error at the call. Zig checks each instantiation instead. The predicates are a closed, built-in set (`Eq`, `Ord`, `Num`, `Int`, `Neg`, `Float`, `Plain`, §16.3); there are no user-defined traits.
- **Types are never computed.** There is no function that returns a `type`, no `@typeInfo`, no `inline for` over fields. A generic *struct* (`Pair(A, B)`) is how a type is parameterized. Equality, hashing and printing are derived from a type's structure by the language, so the most common uses of reflection do not need it; serialization is code a program writes for its types (C.8 has the reasoning).
- **Compile-time evaluation runs ordinary functions.** Any function without run-time effects can be called where a constant is needed (`TABLE :: build_table()`), and `@static_assert` checks conditions. A compile-time value parameter is written `n :: usize`.
- **Conditional compilation is `when`**, which takes a compile-time condition (`target.os`, `target.arch`, `target.mode` or any constant) and checks only the branch taken.

## Interfaces: function pointers, or effects

The Zig pattern for runtime polymorphism, a struct holding a context pointer and a table of function pointers (as `std.mem.Allocator` is), works the same way in Gamma: a struct of `^fn` fields. Often an effect is the better tool, because it replaces the interface *and* the parameter that would carry it:

```gamma
Log :: effect {
    log: fn(msg: string)
}

work :: fn(n: u32) -> u32 needs Log {
    Log.log("working")
    return n * 2
}

main :: fn() needs Out {
    count: u32 = 0
    logger :: handler Log {
        log(msg) => {
            count += 1
            print("log: {}\n", msg)
        }
    }
    r := with logger { work(21) }
    print("{} after {} message\n", r, count)
}
```

```output
log: working
42 after 1 message
```

`work` neither takes nor stores a logger; its signature says it logs, and the handler installed around the call decides what logging means. A test installs a handler that records the messages. Effects are covered in [chapter 7](07-effects.md).

## Concurrency

Zig removed its async functions and is redesigning concurrency around an I/O interface passed as a parameter. Gamma has OS threads (`std/thread`) and **fibers**: a `with … on` statement runs code on a stack of its own, and operations of `ctl` effects can suspend it, which is how generators, schedulers and non-blocking I/O are built as libraries. Like the allocator, the scheduler is an effect, not a parameter. See [chapter 12](12-fibers.md).

## Talking to C

C interop is as direct as in Zig, without `@cImport`: foreign functions are declared in a `foreign` block, with C-compatible types, and the C compiler that builds the program links them.

```gamma
c :: import "std/c"

libc :: foreign "c" {
    abs :: fn(x: c.c_int) -> c.c_int needs not
}

main :: fn() needs Out {
    print("{}\n", libc.abs(-7))
}
```

```output
7
```

`needs not` is the programmer's promise that the C function performs no effects (it is not verified); without it a foreign function needs `Sys`. Structs have the C layout, so a struct of C-compatible fields is passed to C as is. `#export` makes a Gamma function callable from C. A package lists C sources and libraries in its `gamma.toml`. [Chapter 14](14-interop.md) has the details.

## Zig habits to unlearn

- **Passing an allocator everywhere.** Write `needs Alloc` and let the caller choose with `with`. Take an `Allocator` value only when a structure must keep one.
- **Error sets as global names.** Write a union per module or operation, give variants payloads, and let lifting compose them.
- **`comptime` metaprogramming.** Reach for a generic struct, a function taking a type, or plain code that the compile-time evaluator runs; there is no type-level programming to fall back on.
- **Shadowing.** Neither language allows it.
- **`anytype`.** A parameter's type is always written or a type parameter; a callback is a `fn(…)` parameter, and an output sink is usually the `Out` effect.
- **`build.zig`.** The build is described by a manifest, and `gamma build`, `run`, `test` and `fmt` need nothing else.

## Where to go next

The [tutorial](README.md) starts from the beginning. [Optionals and errors](05-optionals-and-errors.md), [effects](07-effects.md), [memory](09-memory.md) and [generics](10-generics.md) are the chapters where Gamma differs most from Zig.
