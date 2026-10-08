# 2. Basics

[Contents](README.md) · [Previous: Getting started](01-getting-started.md) · [Next: Control flow](03-control-flow.md)

This chapter covers the everyday vocabulary of the language: how to name things, how literals get their types, what the numeric types are, and how operators behave. Gamma's operators look like C's, but the ones that C programmers routinely misread are compile errors here.

## Bindings

Every name is introduced by one of a small family of forms (§4.1 of the [specification][s4]):

| Form | Meaning |
| --- | --- |
| `x :: e` | immutable binding; the type comes from `e` |
| `x: T : e` | immutable binding of type `T` |
| `x := e` | mutable variable; the type comes from `e` |
| `x: T = e` | mutable variable of type `T` |
| `x: T = ---` | mutable variable of type `T`, deliberately uninitialized |
| `a, b := e` | destructure a tuple |

The pattern is regular: a `:` followed by a type is a type annotation, a second `:` makes the binding immutable, an `=` makes it mutable, and `::` and `:=` are the short forms when the type is inferred.

```gamma
demo :: fn() \ {Out} {
    greeting :: "hello"            // immutable, type string
    count := 0                     // mutable; the type is inferred from use
    ratio: f32 = 0.5               // mutable with an explicit type
    LIMIT: usize : 4096            // immutable with an explicit type
    count += 1
    print("{} {} {} {}\n", (greeting, count, ratio, LIMIT))
}
```

At the top level only immutable bindings exist, and each initializer is evaluated at compile time ([chapter 11](11-compile-time.md)). Mutable variables live inside function bodies. (The one exception, `#global`, is for C interop; see [chapter 14](14-interop.md).)

Immutability is what it says. A `::` binding cannot be assigned, its fields cannot be assigned, and `&x` gives a `^const T` rather than a `^T`. It is shallow with respect to pointers: if `p :: &v` and `v` is mutable, `p` itself cannot be rebound, but `p^ = 3` still works.

### Parameters are immutable

Function parameters are immutable bindings. If you want to modify a copy, say so:

```gamma
digits_in :: fn(n: u32) -> u32 {
    m := n               // a mutable copy
    count: u32 = 0
    while m > 0 {
        m /= 10
        count += 1
    }
    return count
}
```

### No shadowing

Within a function, a local may not reuse the name of another local or parameter that is in scope. Nested blocks do not get a fresh start:

```gamma
bad :: fn(n: u32) -> u32 {
    n := n + 1                     // error: `n` is already a parameter
    total := 0
    for i in 0..n {
        total := total + 1         // error: `total` is already in scope
    }
    return 0
}
```

This removes a whole class of "which `x` did I mean?" bugs, and it is why you will see `m := n` above. The rule applies to function literals and handler literals nested inside the function, and to names bound by patterns (`for` variables, `match` arms, binding conditions). Sibling blocks are fine, because the first name's scope has ended:

```gamma
ok :: fn(xs: []const i32, ys: []const i32) -> i32 {
    total := 0
    for x in xs { total += x }     // `x` goes out of scope with the loop
    for x in ys { total += x }     // so this `x` is a new binding
    return total
}
```

A local *may* reuse a name from outside the function: a top-level name, an imported module's name or a predeclared name. It hides that name for its scope. (The names of types such as `u8` and `bool` are reserved and cannot be hidden.)

### Uninitialized storage

`x: T = ---` declares a variable without initializing it:

```gamma
buf: [256]u8 = ---
```

This is allowed only for **plain-data** types: integers, floats, and arrays, tuples and structs made only of those. Reading uninitialized plain data gives an unspecified value, but it is never undefined behavior. Types with invariants, such as `bool`, optionals, unions, pointers and slices, cannot be left uninitialized, so you cannot produce an invalid value by forgetting to initialize. Debug builds fill `---` storage with `0xAA` bytes so mistakes stand out.

### Destructuring

A function that returns a tuple is unpacked with several names on the left:

```gamma
divmod :: fn(n, d: u32) -> (q: u32, r: u32) => (n / d, n % d)

use_it :: fn() -> u32 {
    q, r := divmod(17, 5)          // q is 3, r is 2
    _, rest := divmod(100, 7)      // `_` discards one element
    return q + r + rest
}
```

The number of names must equal the number of elements. Individual names cannot be annotated; annotate the whole binding instead: `q, r: (u32, u32) = divmod(17, 5)`.

Assignment can also be multiple, which makes a swap trivial: `a, b = (b, a)`. All the right-hand values are computed before any store.

## Literals

### Integers

```gamma
decimal :: 1_000_000
hex     :: 0xFF
octal   :: 0o755
binary  :: 0b1010_0101
typed   :: 200u8           // suffixes: u8, i16, u128, usize, isize, ...
```

There is no octal-by-accident: a decimal literal may not have a leading `0` (`017` is an error). Underscores may separate digits. An integer literal has an exact, unbounded mathematical value, and it only gets a type when it is used.

A character literal such as `'a'` is also an *integer literal* whose value is the Unicode code point. Gamma has no character type; `'a'` is a number that you can use as a `u8` or a `rune` (an alias for `u32`) as long as it fits.

### Floats

Both sides of the `.` are required, which keeps `1..10` a range and `t.0` a field access:

```gamma
pi    :: 3.14159
small :: 1.5e-3
big   :: 2.5E10
f32v  :: 1.5f32
```

`1.` and `.5` are not float literals.

### Strings

```gamma
plain   :: "tab\there, quote \" and newline\n"
unicode :: "caf\u{e9}"
raw     :: `C:\dir\file`          // no escapes
multi   :: `first line
second line`
cstr    :: c"for C functions"     // a cstring, NUL-terminated
```

A `string` is a `[]const u8` slice of UTF-8 bytes in read-only static memory. A regular string literal is *not* NUL-terminated; a `c"..."` literal is. Adjacent literals are never concatenated, so a missing comma in a list of strings is an error rather than a silent bug.

### Literal inference

Untyped literals get their types by *inference over the whole function*, not from a default that is applied immediately. Each untyped literal gets a type variable that is unified with whatever the literal meets:

```gamma
sum_to :: fn(xs: []const u32) -> u32 {
    total := 0                // becomes u32: it is returned as u32 and added to u32s
    for x in xs { total += x }
    return total
}

index_demo :: fn(items: []const u8) -> usize {
    count := 0                // usize: compared with items.len
    while count < items.len { count += 1 }
    other := 0                // nothing constrains it: falls back to i32
    scale := 1.5              // falls back to f64
    return count + 0
}
```

If nothing constrains a literal, it falls back to `i32` (integers), `f64` (floats), `usize` (range bounds, array lengths, indexes and slice bounds) or `u32` (shift amounts). The order of statements does not matter. What never happens is a silent conversion: the variable gets exactly one type, and a literal that does not fit it is an error.

```gamma
big: u8 = 256         // error: 256 does not fit u8
x: i32 = 3.0          // error: a float literal never has an integer type
y: f32 = 3            // fine: 3 converts exactly
```

### Untyped constants

An immutable binding with no type annotation whose initializer is built only from literals and other untyped constants is an *untyped constant*. It is computed exactly and re-typed at each use, as if you had pasted its value there:

```gamma
PAGE :: 4096
MASK :: PAGE - 1              // 4095
PI   :: 3.14159265358979323846
TAU  :: 2 * PI

mask_demo :: fn(addr: u64) -> u64 => addr & MASK    // MASK is u64 here
```

`x: u8 = PAGE` is a compile error, because 4096 does not fit. Write a typed constant when you want a fixed type: `MAX: usize : 4096`.

## Numeric types

| Type | Meaning |
| --- | --- |
| `u1` ... `u65535`, `i1` ... `i65535` | fixed-width unsigned and signed (two's complement) integers |
| `usize`, `isize` | pointer-sized integers; **distinct** from every `uN`/`iN` |
| `f16`, `f32`, `f64` | IEEE 754 floating point |
| `bool` | `true` or `false`; not an integer |

Arbitrary widths such as `u3` or `i24` make protocol fields exact. They are stored in whole bytes in memory (1, 2, 4, 8 or 16 bytes for widths up to 128 bits), with unspecified padding bits; bit-level packing is opt-in through `#bits` structs ([chapter 14](14-interop.md)). `usize` is deliberately not an alias of `u64`, so that code which is correct on a 64-bit target does not silently break on a 32-bit one.

**There are no implicit conversions between numeric types in expressions.** Binary operators need operands of the identical type. Lossless *widening* happens only at coercion sites: initialization, assignment, argument passing, return, and the like.

```gamma
widen :: fn() -> u16 {
    a: u8 = 200
    b: u16 = a                // ok: coercion site, widening u8 -> u16
    c := a + b                // error: u8 + u16, operand types differ
    d := u16(a) + b           // ok: u16(a) is an explicit lossless conversion
    return d
}
```

`T(x)` and the named lossy conversions (`trunc`, `checked`, `sat`, ...) are in [chapter 4](04-types.md).

## Operators

### Arithmetic and overflow flavors

`+ - * / %` and unary `-` work as in C on integers; `%` does not apply to floats. The difference is overflow. Plain `+ - *` and `<<` are **checked**: on overflow they trap in Debug and Checked builds, and give an unspecified (but valid) value in Fast builds. Overflow is never undefined behavior. When you actually want a different behavior, say which, by choosing a flavor:

| Form | Result type | On overflow |
| --- | --- | --- |
| `a + b` | `T` | checked (trap) |
| `a +% b` | `T` | wraps modulo 2^N |
| `a +\| b` | `T` | saturates at the type's min or max |
| `a +? b` | `?T` | `null` |

The same four flavors exist for `-`, `*` and `<<` (`-%`, `*|`, `<<?`, ...), and as compound assignments (`+%=`, `-|=`, ...; there is no `+?=`).

```gamma
overflow_demo :: fn() \ {Out} {
    x: u8 = 250
    a := x +% 10              // 4
    b := x +| 10              // 255
    c := x +? 10              // null, of type ?u8
    d := x +? 10 else 255     // 255: the fallback type is u8
    print("{} {} {} {}\n", (a, b, c, d))
}
```

Division and remainder truncate toward zero, and dividing by zero (or computing `MIN / -1`) **always traps, in every build mode**. There are no flavors of `/` or `%`. Float division follows IEEE 754 and does not trap.

### Shifts and bitwise operators

`a << n` and `a >> n` need `n` to be an unsigned integer of any width, and the amount must be less than the width of `a`. `>>` is arithmetic on signed types, logical on unsigned. Bitwise operators are `&`, `|`, `~` (binary `~` is exclusive-or, because `^` is the pointer sigil; unary `~` is complement).

```gamma
bit_demo :: fn(flags: u32, n: u32) -> u32 {
    low := flags & 0xff
    has_bit := flags & (1 << n) is not 0
    mask := (1 << n) - 1
    if has_bit { return low ~ mask }
    return low
}
```

Here `flags & (1 << n) is not 0` reads as `(flags & (1 << n)) is not 0`: bitwise operators bind tighter than comparisons, so the C bug `x & 1 == 0` does not exist.

> **Common mistake.** Gamma refuses to guess the precedence of operators that C programmers routinely misread.
>
> ```gamma
> a & b | c          // error: mix of different bitwise operators; parenthesize
> 1 << n - 1         // error: shift mixed with arithmetic; write (1 << n) - 1
> a < b < c          // error: comparisons do not chain
> p and q or r       // error: mixing `and` and `or`; parenthesize
> ```
>
> Parenthesize the part you mean. The language does not pick a meaning for you.

### Equality and comparison

Equality is the word `is`; its negation is `is not`. There is no `==` and no `!=`. `=` is only assignment, and assignment is a statement, so `if x = 0` cannot even be written.

```gamma
check :: fn(x: i32, name: string, v: f64) -> bool {
    if x is 0 { return true }
    if name is "admin" { return true }       // compares contents, not addresses
    if v is not 1.5 { return false }
    return x < 10 and name.len <= 5
}
```

`is` is value equality, defined for each type: integers and floats by value (IEEE for floats, so `nan is nan` is false), pointers by address, aggregates field by field, **slices and strings by content**. `<`, `<=`, `>`, `>=` apply to integers, floats, enums, and to tuples, arrays and slices of those, which compare lexicographically (so strings compare bytewise). Structs are deliberately not ordered; compare a tuple of keys instead.

### Membership

`x in c` and `x not in c` test membership in a range, array, slice, or an inline tuple:

```gamma
classify :: fn(c: u8) -> u32 {
    if c in '0'..='9' { return 1 }                 // inclusive range
    if c in ('a', 'e', 'i', 'o', 'u') { return 2 } // inline tuple
    if c not in 'a'..='z' { return 3 }
    return 4
}
```

### Logical operators

`and`, `or` and `not` take and return `bool` and short-circuit. There are no truthy values: conditions must be `bool`, so `if n { ... }` on an integer is an error; write `if n is not 0`.

### Precedence

Tightest first (the full table is §7.3 and Appendix B of the specification):

| Level | Operators |
| --- | --- |
| 1 | postfix: `x?` `x!` `p^` `f(...)` `a[i]` `.name` |
| 2 | prefix: `-x` `~x` `&x` `!e` `try x` |
| 3 | `*` `/` `%` and flavors |
| 4 | `+` `-` and flavors |
| 5 | `<<` `>>` and flavors (cannot be mixed with levels 3-4 unparenthesized) |
| 6 | `&` `\|` `~` (one operator kind per chain) |
| 7 | `..` `..=` |
| 8 | `is` `is not` `in` `not in` `<` `<=` `>` `>=` (never chain) |
| 9 | `not` |
| 10 | `and`, `or` (one kind per chain) |
| 11 | `else` |

Two lexical notes: `---` is always a single token, so `a---b` is an error (write `a - -b`), and `is not` / `not in` must be written on one line.

## Newlines and statements

A newline ends a statement; `;` is an equivalent separator that is never required. A line continues onto the next only in these cases (§2.8):

1. The line **ends with** a binary operator (including `and`, `or`, `else`, `is`, `in`), an assignment operator, `,`, `::`, `:=`, `:`, `=>`, `->`, `\`, `.`, or an opening bracket.
2. The **next line starts with** `else`, `and`, `or`, or `.` followed by an identifier.
3. The newline is directly inside `( )` or `[ ]`. (Inside a `{ }` block, even one nested inside parentheses, newlines count again.)

```gamma
continuation_demo :: fn(a, b, c: i32, delta: i32) -> i32 {
    total := a +                       // trailing operator: continues
        b +
        c
    ok := a > 0
        and b > 0                      // leading `and`: continues
        and c > 0
    step := delta
        .abs()                         // leading `.name`: continues
        .min(100)
    if ok { return total + step }
    return total
}
```

> **Common mistake.** A line that *starts* with `+`, `-`, `*` or another operator does not continue the previous line. Put the operator at the end of the line instead:
>
> ```gamma
> total := first_part
>     + second_part       // error: a new statement starting with `+`
> ```

Other rules about statements:

- An expression used as a statement must have type `()` (or `never`). To throw a value away on purpose, assign it to `_`: `_ = f()`. This applies to results and optionals too, so an ignored error is always visible in the source.
- Code after something that never completes (`return`, `break`, `continue`, a call to `trap`) in the same block is a compile error ("unreachable code").
- A block `{ ... }` introduces a scope. As an expression, a block's value is its last statement if that is an expression, otherwise `()`.
- Assignment is a statement, never an expression, so `a = b = c` and `while (x = next())` do not exist. Compound assignments such as `+=` exist for all the arithmetic, bitwise and flavored operators.

## Summary

- `::` immutable, `:=` and `: T =` mutable, `---` uninitialized plain data; no shadowing inside a function.
- Literals are typed by inference, never by silent conversion; untyped constants are re-typed at each use.
- No implicit numeric conversion in expressions; overflow is an explicit choice of `+`, `+%`, `+|` or `+?`.
- `is` is equality; precedence mistakes from C are compile errors.

Next, [chapter 3](03-control-flow.md) covers control flow.

For the full rules, see [§4 Bindings][s4], [§6 Literals, constants and conversions][s6], and [§7 Expressions][s7] of the specification.

[s4]: ../../gamma-spec.md#4-bindings-scopes-and-mutability
[s6]: ../../gamma-spec.md#6-literals-constants-and-conversions
[s7]: ../../gamma-spec.md#7-expressions
