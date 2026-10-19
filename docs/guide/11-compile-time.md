# 11. Compile time

[Contents](README.md) · [Previous: Generics](10-generics.md) · [Next: Fibers](12-fibers.md)

C has the preprocessor, and C++ and Rust add macro and `constexpr` sublanguages. Gamma has none of these. There is **no macro language and no separate compile-time sublanguage**: ordinary functions run at compile time whenever their effect set proves they cannot touch the outside world. The same function that builds a lookup table at compile time can be called at run time, and the compiler's evaluator is held to the same rules as the real machine.

## What runs at compile time

A call is evaluated at compile time if the callee is a Gamma function with an **empty effect set** and every argument is a constant expression. An empty effect set means no `Out`, no `Alloc`, no `Sys` (so no foreign calls and no `#global` access). It may still use effects it handles internally, such as a `for` loop over an iterator, because those never leave the evaluation.

```gamma
crc_table :: fn(poly: u32) -> [256]u32 {
    table: [256]u32 = ---
    n: u32 = 0
    while n < 256 {
        c := n
        k: u32 = 0
        while k < 8 {
            if c & 1 is 1 {
                c = (c >> 1) ~ poly
            } else {
                c = c >> 1
            }
            k += 1
        }
        table[n] = c
        n += 1
    }
    return table
}

CRC_TABLE :: crc_table(0xEDB88320)

crc32 :: fn(data: []const u8) -> u32 {
    crc: u32 = 0xFFFFFFFF
    for b in data {
        idx := (crc ~ u32(b)) & 0xFF
        crc = (crc >> 8) ~ CRC_TABLE[idx]
    }
    return crc ~ 0xFFFFFFFF
}
```

`CRC_TABLE` is computed by the compiler and placed in read-only static memory. `crc_table` is an ordinary function with an ordinary loop; nothing marks it as "constexpr". If you add a `print` to it, its effect set becomes `{Out}` and the line `CRC_TABLE :: crc_table(...)` stops compiling, which is exactly the right outcome.

The evaluator implements the language exactly as the target would: the target's integer sizes, layout and float behavior, with **every checked operation checked** in all build modes and every kind of undefined behavior detected. A failing check, a `trap`, detected undefined behavior or running out of step budget is a **compile error** that reports the compile-time call stack. A compile-time bug is therefore never silent miscompilation.

```gamma
TOO_BIG :: 200u8 + 100u8        // compile error: u8 overflow, caught during evaluation
```

## Where compile-time evaluation happens

It is *required* (and failure is a compile error) for:

- top-level `::` initializers, and typed constants `x: T : e`;
- array lengths;
- `when` conditions;
- arguments for compile-time parameters (type parameters, `::` parameters, and all parameters of generic types);
- default values of parameters and fields;
- `@static_assert`, and `#global` initializers.

It is *opportunistic* for an immutable local `x :: e` whose initializer is a constant expression: it is evaluated at compile time if that succeeds within the step budget, and otherwise at run time without error. A failing check is still an error, because the same call would fail at run time.

```gamma
LINE_MAX :: 256                          // an untyped constant, used as an array length

lookup_demo :: fn() -> u32 {
    buf: [LINE_MAX]u8 = ---              // array length: required at compile time
    table :: crc_table(0x82F63B78)       // opportunistic: computed now if it fits the budget
    buf[0] = 1
    return table[1] +% u32(buf[0])
}
```

Such a local constant is not second-rate: wherever a compile-time value is needed, a local constant (an untyped one like `K :: 4`, a typed one like `Z: usize : 3`, or one computed by a call) can be used, in array lengths, local type aliases, `@static_assert`, `when` conditions and arguments for compile-time parameters:

```gamma
square :: fn(x: usize) -> usize => x * x

local_demo :: fn() -> usize {
    K :: 4
    S :: square(3)
    Row :: [K]u16                        // a local type alias built from a local constant
    @static_assert(K is 4, "K is four")
    grid := [S]bool(fill: true)          // array length from a computed constant
    row: Row = Row(fill: 2)
    total := grid.len + row.len          // 9 + 4
    when K is 4 { total += 1 }
    return total                         // 14
}
```

A local that is *not* a constant expression (for example `n := square(x)` of a run-time `x`) cannot be used this way, and the compiler reports the name as unknown in a compile-time position.

### The step budget

Each required evaluation has a budget of evaluation steps (implementation-defined, and at least ten million per top-level declaration). A runaway loop is a compile error rather than a hang. A declaration that needs more can ask for it with an attribute:

```gamma
#fuel(50_000_000)
BIG_TABLE :: crc_table(0x04C11DB7)
```

### What a constant may contain

A compile-time value that becomes part of the program (a top-level constant, a `#global` initializer, a local constant that was evaluated at compile time) lives in static memory, so it is restricted:

- It may contain pointers only of read-only kinds (`^const T`, `[^]const T`, `[]const T`, `cstring`) and function pointers. Mutable pointers, `rawptr`, continuations and allocators are errors.
- Every pointer must point into static data: a string literal or another top-level constant. A pointer into memory that existed only during the evaluation is a compile error.

So a table built at compile time is returned **by value** (`[256]u32`) rather than as a pointer to a buffer the function allocated. Other constants can then refer to it through `const` slices.

Values of type `type`, effect types, modules and untyped constants exist only at compile time. You can bind them with `::`, pass them to compile-time parameters, compare types with `is` (`T is f64`, also inside a generic function, where each instantiation decides it), and use them in `when`, but you cannot put them in a run-time variable.

> **Common mistake: effects in a constant.** A constant initializer must perform no effects:
>
> ```gamma
> noisy :: fn() -> u32 needs Out {
>     print("computing\n")
>     return 7
> }
>
> SEVEN :: noisy()          // error: `noisy` performs Out, so it cannot run at compile time
> ```
>
> Remove the effect, or compute the value at run time with `seven := noisy()` inside a function.

## Conditional compilation: `when`

`when` replaces `#ifdef`. The condition is a compile-time `bool`, and only the taken branch is type-checked and compiled. The untaken branches are *parsed*, but names in them need not resolve, so a branch can mention functions that exist only on another platform.

At the top level, `when` does not introduce a scope. The declarations in the taken branch are declared in the surrounding module as if you had written them there:

```gamma
when target.os is .Windows {
    PATH_SEP :: '\\'
} else {
    PATH_SEP :: '/'
}

when target.ptr_bits is 64 {
    Word :: u64
} else when target.ptr_bits is 32 {
    Word :: u32
} else {
    @compile_error("unsupported pointer width")
}
```

`when` may also appear among the fields of a `struct`, the variants of a `union`, and as a statement inside a function (where each branch is a block with its own scope):

```gamma
Stats :: struct {
    hits: u32 = 0
    when target.mode is .Debug {
        trace: u32 = 0               // this field exists only in Debug builds
    }
}

mode_demo :: fn() needs Out {
    when target.mode is .Fast {
        print("checks are off\n")
    } else {
        print("checks are on\n")
    }
}
```

The predeclared module `target` (no import needed) provides:

| Name | Value |
| --- | --- |
| `target.os` | a union: `.Linux`, `.MacOS`, `.Windows`, `.FreeBSD`, `.Wasi`, `.Freestanding` |
| `target.arch` | `.X86_64`, `.Aarch64`, `.Riscv64`, `.S390x`, `.Wasm32`, ... |
| `target.endian` | `.Little` or `.Big` |
| `target.ptr_bits` | an untyped integer constant |
| `target.mode` | `.Debug`, `.Checked`, `.Safe` or `.Fast` |

A `when` condition may not depend on a type parameter or compile-time value parameter of an enclosing generic, because generic bodies are checked once ([chapter 10](10-generics.md)). If different types need different code, write separate functions.

## Static assertions and compile errors

`@static_assert(cond, "message")` fails the build if a compile-time condition is false. Use it to pin down layouts, sizes and assumptions that would otherwise be silent bugs:

```gamma
Header :: struct {
    magic: u32
    version: u16
    flags: u16
    length: u64
}

@static_assert(@size_of(Header) is 16, "Header must match the on-disk format")
@static_assert(@offset_of(Header, length) is 8, "length must be 8-byte aligned")
```

It can appear as a top-level declaration or as a statement. `@compile_error("message")` is an intrinsic of type `never` that is an error if it is compiled, which makes it the `#error` of Gamma when it sits in a `when` branch (as above).

Other compile-time intrinsics are `@size_of(T)`, `@align_of(T)`, `@offset_of(S, field)`, `@type_of(e)` (without evaluating `e`), `@type_name(T)`, `@location()`, `@hash(x, seed)` (structural, for any type that supports `is`), and the bit-twiddling helpers `@clz`, `@ctz`, `@popcount`, `@bswap`, `@bitreverse`, `@rotl` and `@rotr`. §20.3 of the [specification][s20] has the full table, including the intrinsics that are not compile-time.

## Why this design

- **One language.** A table, a configuration constant and a run-time helper are the same kind of code. There is nothing to learn twice, and the unit tests for a compile-time function are ordinary tests.
- **Soundness from effects.** Only functions that provably touch nothing outside can run at compile time, so the compiler never has to guess what a call might do.
- **The evaluator is checked.** It treats the target faithfully and detects undefined behavior, so a compile-time bug is a compile error, not a compiler crash or a wrong constant.
- **No macros.** Compile-time evaluation covers tables and constants, and `when` covers configuration. Anything else is a function.

## Summary

- A call runs at compile time if its effect set is empty and its arguments are constants.
- Top-level initializers, array lengths, `when` conditions and compile-time parameters require it; constant locals get it opportunistically.
- Constants hold only read-only pointers into static data, so build tables by value.
- `when` is the conditional-compilation tool, `target` describes the platform, and `@static_assert` pins your assumptions.

Next, [chapter 12](12-fibers.md) covers fibers and continuations.

For the full rules, see [§17 Compile-time evaluation][s17] and §3.6 (`when` and `target`) of the specification.

[s17]: ../../gamma-spec.md#17-compile-time-evaluation
[s20]: ../../gamma-spec.md#20-attributes-and-intrinsics
