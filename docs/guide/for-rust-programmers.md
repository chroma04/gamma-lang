# Gamma for Rust programmers

[Contents](README.md) · [Gamma for C programmers](for-c-programmers.md)

This page is for programmers who know Rust well and want to know how Gamma differs. It maps familiar Rust ideas onto Gamma, shows the short programs that replace the ones you would write in Rust, and says, for each place where Gamma chose differently, what the trade-off is. It is a companion to the tutorial chapters, which it links to for depth, not a replacement for them. When you need an exact rule, the [language specification](../../gamma-spec.md) is normative.

Gamma and Rust share more than their syntax suggests. Both are compiled systems languages without a garbage collector, both reject null, both have tagged unions with exhaustive `match`, both monomorphize generics, both have a package manager and built-in tests. They disagree about two large questions, and most of this page follows from the answers:

1. **How is memory safety achieved?** Rust proves it at compile time with ownership, borrowing and lifetimes. Gamma does not try to prove it. Values are copied bitwise and own nothing, memory is managed explicitly (as in C), and the language narrows the dangerous part through checks that are always on in development, a Debug build that poisons and tags memory, a leak-checking test heap, and an optional Safe build that traps on memory errors. The price is that use-after-free and double free are still your responsibility; the benefit is a language with no borrow checker, no lifetime annotations and no `Pin`.
2. **How is code made generic and configurable?** Rust uses traits. Gamma has no user-defined traits: generic code is constrained by a closed set of built-in predicates, behavior is passed as a parameter, dynamic dispatch is a struct of function pointers, and everything that a function takes from its surroundings (allocation, I/O, time, logging, early exit, coroutines) is an **effect** listed in its signature and supplied by a **handler** at the call site.

Gamma's own summary is that *everything a function depends on appears in its signature*. In Rust you get part of that from the type system (inputs, errors, `Option`) and the rest from conventions and globals; Gamma writes down the rest, too.

> **A note on tone.** Rust's choices are good ones for what Rust is for. Gamma is aimed at the programmer who would otherwise write C, and it makes the trade-offs of that audience: explicit memory, predictable cost, C layout, and a short language that fits in one's head. If you want machine-checked memory safety, Rust gives it to you and Gamma does not.

## What will feel familiar

Here is a small program in each language. It parses a decimal number into a `u8` and reports the outcome. The Rust version:

```rust
#[derive(Debug)]
enum ParseError {
    Empty,
    BadDigit(usize),
    TooBig,
}

fn parse_u8(s: &str) -> Result<u8, ParseError> {
    if s.is_empty() {
        return Err(ParseError::Empty);
    }
    let mut n: u8 = 0;
    for (i, c) in s.bytes().enumerate() {
        if !c.is_ascii_digit() {
            return Err(ParseError::BadDigit(i));
        }
        n = n
            .checked_mul(10)
            .and_then(|n| n.checked_add(c - b'0'))
            .ok_or(ParseError::TooBig)?;
    }
    Ok(n)
}

fn main() {
    for text in ["42", "", "4x", "300"] {
        match parse_u8(text) {
            Err(ParseError::Empty) => println!("empty"),
            Err(ParseError::BadDigit(at)) => println!("bad digit at {at}"),
            Err(ParseError::TooBig) => println!("too big"),
            Ok(n) => println!("{n}"),
        }
    }
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
    for text in ("42", "", "4x", "300") {
        match parse_u8(text) {
            !.Empty => print("empty\n")
            !.BadDigit(at) => print("bad digit at {}\n", at)
            !.TooBig => print("too big\n")
            n => print("{}\n", n)
        }
    }
}
```

```output
42
empty
bad digit at 1
too big
```

Almost every line has a counterpart:

- `Result<u8, ParseError>` is written `ParseError!u8`, with the error type first. `Err(e)` is `!e`, and `Ok(n)` is just `n`, because a success wraps implicitly.
- `enum` with payloads is `union`. A union without payloads is an enum. Both are tagged, and `match` over them must be exhaustive.
- `Option<T>` is `?T`, with `null` for `None` ([below](#absence-and-failure-t-et-and-try)).
- Rust's `?` operator is the prefix word `try`. (A postfix `?` exists in Gamma too, but it means something else; see [the habits section](#rust-habits-to-unlearn).)
- `checked_mul` and `checked_add` are the operators `*?` and `+?`, which give `null` on overflow and can be chained, so the whole computation reads as one expression with an `else` fallback.
- `println!` is `print` with `{}` placeholders and no implicit newline. It is an ordinary function (the placeholders are checked against the arguments at compile time), not a macro.
- There is no `&str` versus `String` split. A `string` is a read-only slice of bytes, `[]const u8`.
- The one new thing in `main`'s signature is `needs Out`: the function says that it writes output. Leaving it off is a compile error. That is the effect system, and it gets its [own section](#effects-the-new-idea).

Other things that carry over without surprises:

- **No null.** Every pointer type is non-null, and a nullable one is spelled `?^T`, with the same size as `^T`, just as `Option<&T>` and `Option<Box<T>>` are pointer-sized in Rust.
- **No implicit numeric conversions.** `u8 + u16` is an error in both languages. Widening happens only where a value is stored into a place of known type.
- **No inheritance and no overloading** of functions. Gamma has no operator overloading either.
- **Modules, visibility and a package manager.** A module is a directory, names are private unless marked `pub`, and a package has a manifest and a lock file ([below](#packages-and-cargo)).
- **Built-in test declarations**, run by the tool.
- **Exhaustive `match`** with guards, bindings, ranges and nested patterns.
- **Monomorphized generics** with no run-time cost.

## A translation table

The table lists the Rust construct you would reach for and what Gamma uses. The sections that follow explain the entries that are not obvious.

| Rust | Gamma | Notes |
| --- | --- | --- |
| `let x = e;` | `x :: e` | immutable binding |
| `let mut x = e;` | `x := e` | mutable variable |
| `let x: T = e;` / `let mut x: T = e;` | `x: T : e` / `x: T = e` | annotated forms |
| shadowing, `let x = x + 1;` | not allowed in a function | pick a new name ([below](#bindings-and-mutability)) |
| `Option<T>`, `None`, `Some(x)` | `?T`, `null`, `x` | `Some` wraps implicitly |
| `Result<T, E>`, `Err(e)`, `Ok(x)` | `E!T`, `!e`, `x` | error type first |
| `e?` | `try e` | error types *lift* instead of using `From`; pending `defer`s run |
| `x.unwrap()` | `x?` (optional), `x!` (result) | traps; checked operation |
| `x.unwrap_or(d)` / `unwrap_or_else` | `x else d` | `d` is evaluated lazily; `else e => d` sees the error |
| `if let Some(v) = x { }` | `if v := x { }` | binding condition |
| `let Some(v) = x else { return None };` | `v := x else return null` | the fallback has type `never` |
| `match x { ... }` | `match x { ... }` | patterns are similar; `_`, ranges, guards |
| `enum E { A, B(u32) }` | `E :: union { A, B: u32 }` | tagged union; no payloads makes it an enum |
| `struct S { a: u32 }` | `S :: struct { a: u32 }` | constructed by name: `S(a: 1)` |
| `impl S { fn f(&self) }` | `f :: fn(s: ^const S)` in the same module | called as `s.f()` |
| `impl Trait for T` | none | see [generics](#generics-without-traits) |
| `fn f<T: Ord>(x: T)` | `f :: fn(T: type, x: T) where Ord(T)` | closed set of constraints |
| `dyn Trait`, `Box<dyn Trait>` | a struct holding a data pointer and `^fn` pointers | as `Allocator` does |
| `Box<T>` | `^T` from `new`, released with `destroy` | no ownership |
| `Vec<T>` | `List(T)` | takes an allocator at construction |
| `HashMap<K, V>` | `HashMap(K, V)` | hash and equality are structural |
| `String` | a `List(u8)` or a slice from `dupe` | no builder type in the core library |
| `&str`, `&[T]` | `string`, `[]const T` | a pointer and a length |
| `&mut [T]` | `[]T` | |
| `&T`, `&mut T` | `^const T`, `^T` | raw: no lifetimes, no aliasing rules |
| `*const T`, `*mut T` | `^const T`, `^T`; `[^]T` for arithmetic | |
| `[T; N]` | `[N]T` | arrays are values |
| `(A, B)` | `(A, B)`, also named: `(x: A, y: B)` | |
| `Drop`, RAII | `defer`, `errdefer` | nothing runs implicitly |
| `std::mem::swap` | `a, b = b, a` | multiple assignment |
| closures `\|x\| x + n` | `fn(x) => x + n` | second-class; capture by reference |
| `fn(i32) -> i32` pointers | `^fn(x: i32) -> i32` | captures nothing |
| `impl Fn(i32) -> i32` argument | `fn(i32) -> i32` parameter | no allocation, can't escape |
| `Box<dyn Fn>` | no equivalent | a `^fn` plus an explicit data pointer |
| `Iterator`, `for` | a function with `needs Yield(T)`, `for` | no adapters; see [iteration](#iteration) |
| `async fn`, `.await` | fibers: `ctl` effects, `with ... on` | |
| `panic!`, `assert!` | `trap(msg)`, `assert(cond, msg)` | a trap cannot be caught |
| `break 'label` out of nested calls, `catch_unwind` | `esc` effect operations | early exit that runs `defer` |
| `println!`, `format!`, `write!` | `print`, `str.format`, `@write` | not macros |
| `macro_rules!`, proc macros | none | compile-time functions and `when` |
| `const fn`, `const X: T` | any function with an empty effect set; `X :: e` | |
| `#[cfg(...)]` | `when target.os is .Linux { ... }` | |
| `#[derive(Debug, Clone, Eq, Hash)]` | nothing to write | printing, `is` and `@hash` are structural |
| `#[derive(PartialOrd)]` on structs | order a tuple of keys | structs are not ordered |
| `unsafe { }` | no block; see [the safety section](#safety-and-what-plays-the-role-of-unsafe) | `Sys`, many-pointers, `cast` |
| `extern "C" { }` | `foreign "c" { }` | |
| `#[no_mangle] extern "C" fn` | `#export("sym")` | |
| `#[repr(C)]` | the default layout | `#packed`, `#align(n)` adjust it |
| `#[test]` | `test "name" { }` | `expect`, `expect_eq`, `ok` |
| `cargo new/run/test/add` | `gamma new/run/test/add` | `gamma.toml`, `gamma.lock` |
| `cargo fmt` | `gamma fmt` | |
| cross-compiling with `--target` | `CC=... gamma build`, `--target=<triple>` | native code goes through C |

## Bindings and mutability

Rust binds with `let` and marks mutability on the binding. Gamma's forms are `::` for an immutable binding and `:=` for a mutable one, with the type optional in both:

```gamma
main :: fn() needs Out {
    greeting :: "hello"       // let greeting = "hello";
    count := 0                // let mut count = ...;
    ratio: f32 = 0.5          // let mut ratio: f32 = 0.5;
    LIMIT: usize : 4096       // let limit: usize = 4096;
    count += 1
    print("{} {} {} {}\n", greeting, count, ratio, LIMIT)
}
```

```output
hello 1 0.5 4096
```

The same `::` that binds a local also introduces functions, types, imports and constants at the top level. There is no `fn`, `struct` or `const` declaration form: a function is a value written `fn(...) { ... }` and given a name, which is why `main :: fn() ...` looks the way it does. At the top level every binding is immutable and evaluated at compile time; there are no `static mut`s (the one exception is `#global`, which exists for C interop and requires `Sys`).

**There is no shadowing.** This is the opposite of idiomatic Rust, where `let x = x.trim();` and `let x = x?;` rebind names freely. In Gamma, a local may not reuse the name of another local or parameter that is in scope, including in nested blocks and in the patterns of `for`, `match` and `if`:

```gamma error
parse :: fn(text: string) -> usize {
    text := text.len          // error: `text` is already a parameter
    return text
}
```

The reason is the same as for most of Gamma's rules: it removes a class of "which `x` did I mean?" mistakes, and it makes every use of a name refer to one thing within a function. The cost is that you invent a second name (`trimmed`, `n2`, or, for a mutable copy of a parameter, `m := n`). Sibling scopes are fine, so two loops can each use `x`. A local may hide a name from *outside* the function (a top-level function or an import), but never the name of a type, so `u8`, `string` or `Point` cannot be variable names.

A mutable variable can be left uninitialized with `x: T = ---`, but only for plain-data types (integers, floats and arrays and structs of them). Reading it gives an unspecified value, never undefined behavior, which is a different bargain from Rust's `MaybeUninit`: a half-initialized integer is still a valid integer, so there is no invalid value to be careful about. Types with invariants (`bool`, unions, pointers, optionals) cannot be left uninitialized.

Assignment is a statement, not an expression, so `a = b = c` and `if (x = f())` do not exist. Multiple assignment, `a, b = b, a + b`, replaces `mem::swap` for locals and evaluates all right-hand values before storing.

Gamma reads like a language that uses words for logic and sigils for shapes. Equality is the word `is` (and `is not`), `and`, `or` and `not` replace `&&`, `||` and `!`, and `==` does not exist. Conditions must be `bool`, braces are mandatory, and a line break ends a statement. Mixing operators whose precedence people misremember (`a & b | c`, `1 << n - 1`, `a < b < c`, `p and q or r`) is a compile error that asks for parentheses. See [chapter 2](02-basics.md).

## Numbers: literals, conversions and overflow

### Types and literals

The integer types are `u8` to `u128` and `i8` to `i128`, plus **any width from 1 to 128 bits** (`u3`, `i24`), and `usize` and `isize`. `usize` is a distinct type from `u64`, deliberately, so that code correct on a 64-bit target does not silently depend on it. There are `f16`, `f32` and `f64`. `bool` is not an integer, and there is no `char`: `'a'` is an integer literal with the code point as its value, and `rune` is an alias for `u32`.

Integer literals have no type of their own until they are used. This is *inference over the whole function*, like Rust's: in `total := 0; for x in xs { total += x }` with `xs: []const u32`, `total` becomes a `u32`, and `0` is never an `i32` that must be converted.

A literal that nothing constrains falls back to `i32` (integers), `f64` (floats) or `usize` (anything that counts or positions: array lengths, indexes, ranges, shift amounts). A literal that does not fit is an error, not a wrap: `big: u8 = 256` does not compile.

### Conversions: `T(x)` and named methods

Rust's `as` does several jobs at once: widening, truncation, float-to-int saturation, pointer casts. Gamma splits them. `T(x)` is a conversion that is **lossless for every value of the source type**, and the compiler rejects it otherwise (legality depends on the types only, never on what the optimizer can prove about the value). Anything lossy is a method whose name says what happens:

| Rust | Gamma |
| --- | --- |
| `x as u64` where `x: u32` | `u64(x)` |
| `x as u8` where `x: u32` (keeps low bits) | `x.trunc(u8)` |
| `u8::try_from(x).ok()` | `x.checked(u8)`, a `?u8` |
| `x.min(255) as u8` | `x.sat(u8)` |
| `f as i32` (saturates, NaN is 0) | `f.sat(i32)` |
| `f as i32` when you want an error for out of range | `f.trunc(i32)` (traps if out of range), `f.round(i32)`, `f.floor(i32)`, `f.ceil(i32)` |
| `i as f64` | `i.to(f64)` or, if exact, `f64(i)` |
| `f32::to_bits(x)` / `transmute` | `x.bits(u32)` |

```gamma
main :: fn() needs Out {
    big: u32 = 300
    print("{} {} {} {}\n", u64(big), big.trunc(u8), big.checked(u8), big.sat(u8))
    f: f64 = 3.7
    print("{} {} {}\n", f.trunc(i32), f.round(i32), f.to(f32))
}
```

```output
300 44 null 255
3 4 3.7
```

There is no `as`. Writing one is a syntax error, which sends you to the right method. The cost is a few extra characters; the benefit is that a reader never has to ask whether a cast could lose information, because the spelling tells them.

### Overflow

In Rust, integer overflow panics in debug builds and wraps in release builds (unless you turn overflow checks on), so the same source has two behaviors, and you choose a third by calling `wrapping_add`, `saturating_add` or `checked_add`. Gamma makes the choice part of the operator and keeps one rule for the plain operator:

| Gamma | Rust | On overflow |
| --- | --- | --- |
| `a + b` | `a + b` | **traps** in Debug, Checked and Safe builds; an unspecified (but valid) value in Fast builds |
| `a +% b` | `a.wrapping_add(b)` | wraps modulo 2^N |
| `a +\| b` | `a.saturating_add(b)` | clamps at the type's limits |
| `a +? b` | `a.checked_add(b)` | `null`, so the result is `?T` |

The same four flavors exist for `-`, `*` and `<<`, and as compound assignments (`+%=`, `-|=`, ...). Overflow is never undefined behavior in Gamma: an unchecked build gives *some* number, so an optimizer can never use overflow to delete one of your own checks. Division by zero, and `MIN / -1`, trap in every build mode.

```gamma
main :: fn() needs Out {
    x: u8 = 250
    print("{} {} {}\n", x +% 10, x +| 10, x +? 10)
    print("{}\n", x +? 10 else 255)
    print("{}\n", @max_value(u8))
}
```

```output
4 255 null
255
255
```

The default build mode of the `gamma` tool is Debug. See [chapter 2](02-basics.md) for the operators and [chapter 13](13-safety.md) for build modes.

## Absence and failure: `?T`, `E!T` and `try`

`Option` and `Result` are built into the syntax, and they mirror each other: `?T` is a `T` that may be `null`, and `E!T` is a `T` or an error `E`. Both wrap implicitly, so success paths read like plain code. There are no combinators (`map`, `and_then`, `ok_or`); you use the operators and statements below instead, which is part of why Gamma code is short and flat.

| Task | `Option` / `Result` in Rust | Gamma |
| --- | --- | --- |
| test and bind | `if let Some(v) = x { }` | `if v := x { }` |
| bind, or leave | `let Some(v) = x else { return None };` | `v := x else return null` |
| fallback value | `x.unwrap_or(d)` | `x else d` |
| fallback from the error | `r.unwrap_or_else(\|e\| f(e))` | `r else e => f(e)` |
| propagate | `x?` | `try x` |
| assert | `x.unwrap()` | `x?` for an optional, `x!` for a result |
| turn a result into an option | `r.ok()` | `r else null` |
| turn an option into a result | `o.ok_or(e)` | `o else return !e` |
| match on all cases | `match r { Ok(v) => .., Err(e) => .. }` | `match r { !e => .., v => .. }` |

```gamma
first_even :: fn(xs: []const i32) -> ?i32 {
    for x in xs {
        if x % 2 is 0 { return x }
    }
    return null
}

sum_of_first_evens :: fn(a, b: []const i32) -> ?i32 {
    x := try first_even(a)
    y := try first_even(b)
    return x + y
}

main :: fn() needs Out {
    odds :: [_]i32(1, 3, 5)
    evens :: [_]i32(2, 4)
    if total := sum_of_first_evens(evens[..], [_]i32(7, 6)[..]) {
        print("total {}\n", total)
    }
    n := first_even(odds[..]) else -1
    print("{} {}\n", n, sum_of_first_evens(odds[..], evens[..]))
}
```

```output
total 8
-1 null
```

A few differences are worth knowing before you write real code:

- **`try` does not convert between optional and result.** In Rust, `?` can turn `None` into an error only through `ok_or`. Here `try` on an optional needs a function that returns an optional, and `try` on a result needs a function that returns a result; the conversions are the `else` forms in the table.
- **Errors lift instead of `From`.** Rust calls `From::from` on the error in `?`, which depends on which impls exist. Gamma has no traits to look up, so it has one built-in rule: if the function's error type is a union with *exactly one* variant that carries the source error's type, `try` wraps the error in that variant. If two variants match, it is ambiguous and a compile error, and you write the wrapping yourself.
- **Errors are values of any type**, with no `Error` trait. The convention is a union with one variant per failure, which makes `match` exhaustive.
- **Ignoring a result is an error.** `f()` as a statement, where `f` returns anything but `()`, does not compile; write `_ = f()`. This is `#[must_use]` for every function, and it applies to optionals too.
- **`main` can return `E!()`.** An error prints its type and variant to stderr and exits with status 1, like returning `Err` from Rust's `main`.

```gamma
ParseError :: union { Empty, BadDigit: usize }
ConfigError :: union { Parse: ParseError, Missing }

parse_port :: fn(s: string) -> ParseError!u16 {
    if s.len is 0 { return !.Empty }
    n: u16 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
        n = n *% 10 +% u16(c - '0')
    }
    return n
}

port_from :: fn(text: ?string) -> ConfigError!u16 {
    s := text else return !.Missing
    return try parse_port(s)          // ParseError is lifted into ConfigError.Parse
}

main :: fn() -> ConfigError!() needs Out {
    port := try port_from("8080")
    print("port {}\n", port)
    print("{}\n", port_from(null))
    print("{}\n", port_from("80x"))
    return ()
}
```

```output
port 8080
!.Missing
!.Parse(.BadDigit(2))
```

`try` also differs in an important way from `?`: it is a prefix operator, and when it propagates, the function's pending `defer` and `errdefer` statements run on the way out. See [chapter 5](05-optionals-and-errors.md).

### Traps are not panics

`panic!` unwinds and can be caught with `catch_unwind`. A Gamma **trap** terminates the process at once: it cannot be caught, it does not unwind and it does not run `defer` statements. Debug builds print the call chain. Traps are for bugs (a failed check, an `assert`, `x?` on `null`), and everything recoverable is a value. For the cases where Rust programmers reach for panics to exit early across several frames, Gamma has `esc` operations, which unwind visibly and run cleanup ([effects](#effects-the-new-idea)).

## Unions and `match`

A Gamma `union` is a tagged union: Rust's `enum`. Variants are constructed with `.Name(payload)` when the type is known from context, or `Type.Name(payload)` otherwise.

```gamma
Shape :: union {
    Circle: f32
    Rect: (w: f32, h: f32)
    Empty
}

area :: fn(s: Shape) -> f32 => match s {
    .Circle(r) => 3.14159 * r * r
    .Rect(w, h) if w is h => w * w
    .Rect(w, h) => w * h
    .Empty => 0
}

main :: fn() needs Out {
    shapes := [_]Shape(.Circle(1.0), .Rect(w: 2, h: 3), .Rect(w: 2, h: 2), .Empty)
    for s in shapes {
        print("{} has area {}\n", s, area(s))
    }
}
```

```output
.Circle(1) has area 3.14159
.Rect(w: 2, h: 3) has area 6
.Rect(w: 2, h: 2) has area 4
.Empty has area 0
```

Points where it differs from Rust:

- **Tuple payloads are flattened in patterns.** The payload `(w: f32, h: f32)` is matched as `.Rect(w, h)`, not `.Rect((w, h))`.
- **A guard makes an arm not count for exhaustiveness**, so a guarded arm is followed by an unguarded one, as above.
- **`is` tests a variant without binding**: `if s is .Empty { ... }`. To bind, use `if .Circle(r) := s { ... }`, or `.Circle(r) := s else return null` to leave the function otherwise.
- **A bare name in a pattern always binds**, even if a constant of that name exists. To avoid the mistake of expecting a comparison, the compiler rejects a bare name that is a visible constant; use a guard instead.
- **Integers become enums only through `from_tag`**, which returns an optional. There is no `as` from an integer to an enum, and no `transmute`. In the other direction `u8(d)` and `d.tag()` work.
- **`@variants(E)` is a constant array of the variants** of an enum, and an array indexed by an enum, `[E]T`, is built by naming every variant. This replaces the `strum`-style crates and the `match`-to-array dance.

```gamma
Dir :: union { N, E, S, W }

main :: fn() needs Out {
    visits := [Dir]u32(fill: 0)
    visits[.E] += 2
    for d in @variants(Dir) {
        print("{} {}\n", @variant_name(d), visits[d])
    }
    print("{}\n", Dir.from_tag(7))
}
```

```output
N 0
E 2
S 0
W 0
null
```

`match` also unwraps optionals and results directly: `null` matches the absent case, `!p` matches an error, and any other pattern is matched against the present or success value, as the first program in this page shows. See [chapter 3](03-control-flow.md) and [chapter 4](04-types.md).

## Structs and methods

A `struct` is a nominal type with fields, constructed **by name only**: `Counter(step: 5)`. Fields can have constant defaults, `..w` copies fields from another value (Rust's `..w` struct update), and there is no positional constructor, so reordering fields can never silently change what a constructor means.

There are no `impl` blocks. A **method** is a top-level function in the module that declares the type, whose first parameter is the type or a pointer to it, and `e.name(args)` calls it:

```rust
struct Counter {
    count: u32,
    step: u32,
}

impl Counter {
    fn bump(&mut self) {
        self.count += self.step;
    }
    fn total(&self) -> u32 {
        self.count
    }
}
```

```gamma
Counter :: struct {
    count: u32 = 0
    step: u32 = 1
}

bump :: fn(c: ^Counter) { c.count += c.step }
total :: fn(c: ^const Counter) -> u32 => c.count

main :: fn() needs Out {
    c := Counter(step: 5)
    c.bump()                  // means bump(&c)
    c.bump()
    print("{}\n", c.total())
    d := Counter(..c, step: 100)
    print("{} {}\n", d.count, d.step)
}
```

```output
10
10 100
```

`^T` and `^const T` take the place of `&mut self` and `&self`, but they are plain pointers. The receiver rules are small: if the first parameter is `^T` and the receiver is a mutable place, the compiler passes `&c`; if the parameter is `T` and the receiver is a pointer, it passes the dereferenced value; and a temporary can be passed as `^const T`. Calling a mutating method on an immutable binding is an error, which is Rust's `cannot borrow as mutable` for the one case Gamma tracks:

```gamma error
Counter :: struct { count: u32 = 0 }

bump :: fn(c: ^Counter) { c.count += 1 }

main :: fn() {
    frozen :: Counter()
    frozen.bump()             // error: `frozen` is immutable
}
```

Other differences:

- **Visibility is per module.** Fields are private unless `pub`, and a struct with a private field cannot be constructed or destructured outside its module. Methods can be added only in the module that declares the type; for a foreign type you write a free function and call it normally.
- **No inheritance, but `using` embeds a struct** as the first field, promoting its fields and letting a `^Outer` stand in for a `^Inner`. Rust has no direct equivalent; it is C's "struct-as-base-class" idiom made explicit.
- **Layout is the C layout** (declaration order, natural alignment), not Rust's unspecified default. `#packed` and `#align(n)` adjust it, `#bits(u16)` structs pack named fields exactly, and `@size_of`, `@align_of` and `@offset_of` plus `@static_assert` pin it down.
- **Structs are not ordered.** Rust's `#[derive(PartialOrd)]` orders by field order, which couples layout to meaning. In Gamma you order a tuple of keys: `people.sort_by_key(fn(p) => (p.last, p.first))`. Structs do compare with `is`, field by field.
- **No `#[derive]` at all.** Printing (`print("{}", x)`), equality (`is`), hashing (`@hash`) and copying are all derived structurally for every type, and nothing can override them. Custom text is a function you write that returns a string or writes into a sink.

## Memory without ownership

This is the biggest change, and it is easier to understand as "Gamma manages memory like C, with the sharp edges filed down" than as "Rust without the borrow checker".

### Values are bitwise copies and own nothing

Every assignment, argument and return copies the value's bytes. Nothing is "moved out of", nothing has a destructor, and nothing needs `Clone` for a plain copy:

```gamma
Point :: struct { x, y: i32 }

Handle :: struct { hits: ^u32 }

main :: fn() needs Out {
    a := Point(x: 1, y: 2)
    b := a
    b.x = 10
    print("{} {}\n", a.x, b.x)

    n: u32 = 0
    h := Handle(hits: &n)
    h2 := h                   // copies the pointer, not the counter
    h2.hits^ += 1
    h.hits^ += 1
    print("{}\n", n)
}
```

```output
1 10
2
```

In Rust, `let b = a;` moves `a` unless `Point` is `Copy`, and a type that holds a unique resource is never `Copy`. In Gamma, `b := a` always copies, so copying a struct that *refers to* memory (a `List`, a pointer, a slice) copies the reference. The second half of the example is the point: `h` and `h2` alias the same counter, and nothing stops you from keeping both. Freeing through both would be a double free. That is the trade: the borrow checker's rules disappear, and so do the guarantees they gave.

### Allocation is an effect

There is no global allocator. Functions that allocate perform the `Alloc` effect, listed in their signature, and the **caller** decides where memory comes from by installing a handler. `new`, `make` and friends use whatever allocator is current, and they return a result, because allocation can fail:

```gamma
Point :: struct { x, y: i32 }

main :: fn() -> AllocError!() needs Alloc + Out {
    p := try new(Point(x: 1, y: 2))       // Box::new
    defer destroy(p)
    p.x += 10

    names := list(string)                 // Vec::new
    defer names.deinit()
    try names.push("ada")
    try names.push("grace")

    ages := hash_map(string, u32)         // HashMap::new
    defer ages.deinit()
    try ages.put("ada", 36)

    for name in names.items {
        age := ages.get(name) else 0
        print("{} is {}, p.x = {}\n", name, age, p.x)
    }
    return ()
}
```

```output
ada is 36, p.x = 11
grace is 0, p.x = 11
```

Compare with Rust:

| | Rust | Gamma |
| --- | --- | --- |
| who picks the allocator | one global (`#[global_allocator]`), or an `Allocator` type parameter on nightly | the caller, per region of code, with `with` |
| can a function allocate? | invisible in the signature | `needs Alloc`, or it was handed an `Allocator` value |
| allocation failure | aborts (or `try_reserve`) | `AllocError!T`, handled like any result |
| freeing | `Drop`, automatic | `defer destroy(p)` / `names.deinit()`, written by you |
| a container's allocator | fixed when the type is chosen | captured when the container is created, used for its whole life |

Because `Alloc` is an ordinary effect, arenas and fixed buffers are not special:

```gamma
arena :: import "std/arena"

main :: fn() -> AllocError!() needs Alloc + Out {
    a := arena.new()
    defer a.deinit()                      // frees everything at once
    total: usize = 0
    with a.allocator() {
        for i in 0..4 {
            buf := try make(u8, 64 * (i + 1))   // from the arena; no individual free
            buf.fill(0)
            total += buf.len
        }
    }
    print("{}\n", total)
    return ()
}
```

```output
640
```

Two details keep this safe enough in practice. A container (`List`, `HashMap`) captures the allocator that was current when it was created and uses that same handle for every later operation, so you cannot free memory through a different allocator than the one that produced it (Debug builds also tag every allocation with its allocator and trap on a mismatch). And a function without `Alloc` in its effect set does not allocate through the current allocator, so `needs Alloc` is a reliable "this function may allocate" marker. No language construct allocates: closures, `for` loops and string literals never do.

### `defer` replaces `Drop`

`defer S` runs a statement when the enclosing block exits by any path (end of block, `return`, `break`, `continue`, a propagating `try`), in reverse order. `errdefer S` runs only if the block is left by an error or an early exit, for undoing partial work. You write the cleanup on the line after the acquisition.

```gamma
main :: fn() needs Out {
    defer print("third\n")
    defer print("second\n")
    print("first\n")
}
```

```output
first
second
third
```

Two consequences for a Rust programmer. First, **forgetting the cleanup compiles**; nothing in the type system says that a `List` must be `deinit`ed. Gamma's answer is dynamic: `gamma test` runs tests on a leak-checking heap, so a missing `defer names.deinit()` fails the test with the line that allocated the leaked block. Second, there is no way for a library to put cleanup *inside a type*. A function that returns a resource to its caller uses `errdefer` for the steps that follow the acquisition, and the caller owns the cleanup by convention.

### What is and is not caught

Everything below is by comparison with what a Rust program gets for free from the compiler:

| Mistake | Rust | Gamma |
| --- | --- | --- |
| use after free | compile error | undefined behavior; Debug builds fill freed memory with `0xDD` to make stale reads stand out; Safe builds trap |
| double free | impossible (ownership) | undefined behavior; Debug builds detect it in many cases; Safe builds trap |
| dangling reference to a local | compile error | the direct cases are compile errors (returning `&x` or a slice of a local); other cases are undefined behavior, trapped in Safe builds |
| leaks | allowed (`mem::forget`, cycles) | tests fail on leaks |
| out-of-bounds index | panic | trap; undefined behavior only in Fast builds |
| data race | compile error (`Send`/`Sync`) | undefined behavior (C11 memory model); Safe builds make it unspecified, not memory-corrupting |
| integer overflow | panic or wrap | defined; never undefined |
| uninitialized read | impossible in safe code | allowed for plain data; unspecified value |

The full list of operations with undefined behavior is short and closed (§18.4). **Safe builds** (`--mode=safe`, optional, built with the Fil-C memory-safe C compiler on supported platforms) check every memory access and trap on most of the memory errors above, at a cost of two to five times in speed, so they are a testing tool and a way to run untrusted input, not a default. [Chapter 13](13-safety.md) has the details, and [chapter 9](09-memory.md) covers allocators.

If this is a deal-breaker for the program you are writing, that is a reasonable conclusion: Rust's guarantee here is real and Gamma's checks are not a substitute. The case for Gamma's side of the trade is that the language has no lifetimes to annotate, no `Rc<RefCell<T>>` for graphs and doubly linked lists, no `Pin` for self-referential state, and a plain C memory model that existing C tooling (and existing C programmers) already understand.

## Generics without traits

Generic functions take a **type as an ordinary parameter**. The parameter is a compile-time value, written in the same list as the others, and it is usually inferred from the arguments:

```gamma
largest :: fn(T: type, xs: []const T) -> ?T where Ord(T) {
    best := xs.first() else return null
    for x in xs {
        if x > best { best = x }
    }
    return best
}

main :: fn() needs Out {
    scores :: [_]i32(3, 9, 4)
    names :: [_]string("ada", "grace", "edsger")
    print("{} {}\n", largest(scores[..]), largest(names[..]))
    print("{}\n", largest(T: i32, scores[0..0]))
}
```

```output
9 "grace"
null
```

Like Rust, a generic body is checked once where it is written, treating `T` as opaque, and then monomorphized. Unlike Rust, the checks come from a **closed set of built-in constraints** rather than traits you define:

| Constraint | Grants |
| --- | --- |
| `Eq(T)` | `is`, `is not`, `in`, literal patterns, `@hash` |
| `Ord(T)` | `Eq`, plus `<`, `<=`, `>`, `>=` |
| `Num(T)` | `Ord`, plus `+ - * /` and literals at type `T` |
| `Int(T)` | `Num`, plus `%`, overflow flavors, shifts, bitwise operators |
| `Neg(T)` | `Num`, plus unary `-` |
| `Float(T)` | `Num`, plus the float methods |
| `Plain(T)` | `---` and `.bits` for plain-data types |

A few notes on inference. A type parameter that another parameter's type mentions (`T` in `xs: []const T`) is *inferable*, and is never bound positionally: either the arguments determine it, or you write it by name (`largest(T: f64, ...)`). A parameter that nothing mentions, like the element type of `list(string)`, is an ordinary positional parameter. All the arguments that determine a type parameter must agree exactly, with no widening, and an untyped literal never decides on its own. That is stricter than Rust's inference, and the reason is local reasoning: which argument binds which parameter never depends on counting.

### The five jobs of a trait

Gamma's design argument is that a trait bundles five jobs, and four of them have a direct mechanism here. What Gamma leaves out is the fifth, *implicit, compositional resolution* of implementations (coherence, orphan rules, blanket impls, associated types), which is the most complex part of a trait system and the part that makes it impossible to tell from `m.put(k, v)` which code runs. Appendix C.6 of the specification has the full argument. In practice:

| In Rust you would write | In Gamma |
| --- | --- |
| a bound like `T: Ord + Copy` | `where Ord(T)` |
| a bound on behavior you define, e.g. `T: Shape` | pass the behavior as a function parameter |
| `impl Ord for Person` or `#[derive(Ord)]` | `xs.sort_by_key(fn(p) => (p.last, p.first))` |
| `Hash` and `Eq` for map keys | nothing: both are structural; custom ones are parameters of `HashMap` |
| `impl Display` | `print("{}", x)` formats every type structurally; custom text is a function |
| `Box<dyn Trait>` | a struct with a data pointer and `^fn` pointers |
| a trait object chosen at run time from a closed set | a `union` and `match` |
| `impl Trait for T` supplied by the application, not the library | an effect and a handler |
| `impl Add for Matrix` | a function `add(a, b)`; operators never call user code |
| `trait Monoid { ... }` for your own number types | a constant struct of operations, passed as an argument |

Passing behavior is first-class. The standard `sort_by` takes a comparison, and a `HashMap` takes its hash and equality as compile-time parameters:

```gamma
lower :: fn(c: u8) -> u8 => if c in 'A'..='Z' { c + 32 } else { c }

fold_hash :: fn(key: ^const string, seed: u64) -> u64 {
    h := seed
    for b in bytes(key^) { h = (h ~ u64(lower(b))) *% 1099511628211 }
    return h
}

fold_eq :: fn(a, b: ^const string) -> bool {
    if a^.len is not b^.len { return false }
    for i in 0..a^.len {
        if lower(a^[i]) is not lower(b^[i]) { return false }
    }
    return true
}

main :: fn() -> AllocError!() needs Alloc + Out {
    names := hash_map_with(K: string, u32, hash: fold_hash, eq: fold_eq)
    defer names.deinit()
    try names.put("Ada", 1)
    print("{} {}\n", names.get("ADA"), names.get("grace"))
    return ()
}
```

```output
1 null
```

This is `HashMap<K, V, S: BuildHasher>` with the hasher chosen by a compile-time function parameter instead of a trait impl: one explicit argument, and the call site tells you which hash runs.

### Dynamic dispatch by hand

For an *open* set of implementations (plugins, drivers), you write the vtable yourself, as the standard `Allocator` does with its `AllocVtable`. A data pointer plus a table of `^fn`s is exactly what `&dyn Trait` is, minus the compiler writing it for you:

```gamma
Animal :: struct {
    self: rawptr
    name: ^fn(self: rawptr) -> string
    noise: ^fn(self: rawptr) needs Out
}

Dog :: struct { barks: u32 }

dog_name :: fn(self: rawptr) -> string => "dog"

dog_noise :: fn(self: rawptr) needs Out {
    d := self.cast(^const Dog)
    for _ in 0..d.barks { print("woof\n") }
}

dog_animal :: fn(d: ^Dog) -> Animal => Animal(self: d, name: dog_name, noise: dog_noise)

introduce :: fn(a: Animal) needs Out {
    print("a {} says:\n", a.name(a.self))
    a.noise(a.self)
}

main :: fn() needs Out {
    d := Dog(barks: 2)
    introduce(dog_animal(&d))
}
```

```output
a dog says:
woof
woof
```

The `rawptr` casts are the unchecked part (`cast` checks alignment but not the type), and nothing prevents you from handing the wrong data pointer to `name`. For a **closed** set, use a `union` and `match` instead; the compiler checks every case and no pointers are involved.

Note how the effect set is part of the function pointer's type: `noise` is declared `needs Out`, so calling it requires `Out` in `introduce`. A stored function pointer has a fixed effect set; to be polymorphic over effects you give the containing generic type an effect-set parameter. See [chapter 10](10-generics.md).

## Closures

Rust closures come in three flavors (`Fn`, `FnMut`, `FnOnce`), can be boxed, returned and stored, and capture by reference or by move. Gamma has two function-value types instead:

| | `fn(...)` | `^fn(...)` |
| --- | --- | --- |
| like in Rust | `impl Fn` / `&dyn Fn` argument | `fn` pointer |
| captures locals | **by reference**, with no allocation | never |
| can be stored in a struct, returned, put in a table | **no** | yes |
| can be passed to C | no | yes, if its types are C-compatible |

A `fn` value is **second-class**: it can be passed down the call stack and called, and it can capture the enclosing function's locals by reference, but it can never outlive the frame that made it. That makes dangling captures impossible at the cost of one rule: you cannot return a closure, store one in a field, or keep one in a variable you reassign.

```gamma
apply_twice :: fn(f: fn(i32) -> i32, x: i32) -> i32 => f(f(x))

count_if :: fn(T: type, xs: []const T, pred: fn(T) -> bool) -> usize {
    n := 0
    for x in xs {
        if pred(x) { n += 1 }
    }
    return n
}

main :: fn() needs Out {
    offset := 10
    print("{}\n", apply_twice(fn(v) => v + offset, 1))

    scores :: [_]u32(55, 80, 91, 62)
    cutoff: u32 = 60
    print("{}\n", count_if(scores[..], fn(s) => s >= cutoff))
}
```

```output
21
3
```

```gamma error
adder :: fn(n: i32) -> fn(i32) -> i32 {      // error: a second-class function cannot be returned
    return fn(x) => x + n
}
```

The Rust idioms that need a returned or stored closure change shape:

- **A returned closure** (`fn adder(n: i32) -> impl Fn(i32) -> i32`) becomes a function that takes `n` explicitly, or a pair of a function pointer and a data argument.
- **A stored callback** (`struct Button { on_click: Box<dyn Fn()> }`) becomes a `^fn` field plus a data pointer, as in the vtable example above (the same shape as C's callback and `void *` pair).
- **Mutable state in a closure** (`FnMut`) is just a captured variable: `fn(x) => { total += x }` mutates `total` through the reference, since there is no borrow checker to object.

What you gain is cost: calling `f` is an ordinary call, there is no environment to allocate, and (because the callee never keeps the closure) **effects flow through callbacks without extra notation**. A callback's effects are handled where it was written, so a plain higher-order function like `repeat(3, fn() => print("hi\n"))` needs no effect annotation of its own. `async` closures, the `Send` bound on callbacks and lifetime parameters on `Fn` traits all have no counterpart. See [chapter 6](06-functions.md).

A stored function pointer has a fixed effect set; for the compile-time way to make one polymorphic over effects, see [effect-set parameters](10-generics.md#effect-set-parameters).

## Iteration

Rust's `Iterator` is a trait with a `next` method, and adapters (`map`, `filter`, `zip`) build state machines. Gamma has no iterator protocol. **An iterator is a function that performs the `Yield(T)` effect, and `for` is the handler.** Writing an iterator is writing the loop you want and calling `yield`:

```gamma
words :: fn(s: string) needs Yield(string) {
    start := 0
    for i in 0..s.len {
        if s[i] is ' ' {
            if i > start { yield(s[start..i]) }
            start = i + 1
        }
    }
    if s.len > start { yield(s[start..]) }
}

index_of_word :: fn(text, target: string) -> ?usize {
    i := 0
    for w in words(text) {
        if w is target { return i }
        i += 1
    }
    return null
}

main :: fn() needs Out {
    for w in words("the quick  brown fox") {
        print("[{}]\n", w)
    }
    print("{} {}\n", index_of_word("a b c", "c"), index_of_word("a b c", "z"))
}
```

```output
[the]
[quick]
[brown]
[fox]
2 null
```

Compare with a Rust iterator for the same job, which needs a struct to hold the state between calls to `next`. Here the state is the iterator function's own locals and stack frame:

```rust
struct Words<'a> {
    rest: &'a str,
}

impl<'a> Iterator for Words<'a> {
    type Item = &'a str;
    fn next(&mut self) -> Option<&'a str> {
        let s = self.rest.trim_start_matches(' ');
        if s.is_empty() {
            return None;
        }
        let end = s.find(' ').unwrap_or(s.len());
        let (word, rest) = s.split_at(end);
        self.rest = rest;
        Some(word)
    }
}
```

What `for` does is precise. It runs the loop body once per yielded value. `break` leaves the loop and *unwinds the iterator's frames*, running their `defer`s, because `yield` is an `esc` operation; `return`, labeled `break` and `try` inside the body work the same way. Iterators therefore clean up after themselves when a loop stops early, without any `Drop`.

Things you might look for, and where they went:

| Rust | Gamma |
| --- | --- |
| `for i in 0..n`, `0..=n` | `for i in 0..n`, `0..=n` (ranges are values: `Range(T)`) |
| `for x in &xs`, `for x in xs.iter()` | `for x in xs` |
| `for x in xs.iter_mut()` | `for p in each_ptr(xs)`, then `p^ = ...` |
| `xs.iter().enumerate()` | `for i, x in enumerate(xs)` |
| `xs.iter().rev()` | `for x in reversed(xs)` |
| `a.iter().zip(b.iter())` | `for x, y in zip(a, b)` |
| `s.bytes()`, `s.chars()` | `bytes(s)`, `runes(s)`; grapheme clusters from `std/text` |
| `iter.collect::<Vec<_>>()` | `try collect(words(s))`, or `l.collect(...)` into an existing list |
| `iter.count()`, `iter.nth(n)` | `count(words(s))`, `nth(words(s), n)` |
| `map`, `filter`, `take`, `fold` | write the loop; or an adapter function that loops over one iterator and yields |
| `peekable`, `chain`, two iterators advanced alternately | cursors (`xs.cursor()` with `while x := c.next()`), or a pull-style generator on a fiber |
| `impl IntoIterator for MyType` | an iterator function that takes the value: `for x in items(my_value)` |

Because there are no adapter types, the usual pipeline is a short loop:

```gamma
evens :: fn(xs: []const i32) needs Yield(i32) {
    for x in xs {
        if x % 2 is 0 { yield(x) }
    }
}

squares :: fn(src: fn() needs Yield(i32)) needs Yield(i32) {
    for x in src() { yield(x * x) }
}

main :: fn() needs Out {
    data :: [_]i32(1, 2, 3, 4, 5, 6)
    total := 0
    for x in squares(evens(data[..])) {
        total += x
    }
    print("{}\n", total)
}
```

```output
56
```

An adapter takes an iterator call as an argument (a parameter of type `fn() needs Yield(T)`; the call written at the call site is passed as a function) and yields again. A `for` loop is an expression whose value is the iterator's return type, which is how fallible iterators work: an iterator that returns a result must be consumed with `try for` or have its value handled. Since the compiler sees the iterator's body when it is inlined, `for x in xs` and `for i in 0..n` compile to the same counted loop you would write by hand. See [chapter 8](08-iteration.md).

## Effects: the new idea

Nothing in Rust corresponds directly to effects, so this section starts from the problems they solve. In Rust, a function that logs, allocates, reads the clock, or reports progress gets those capabilities from somewhere: a global (`println!`, the global allocator, `SystemTime::now()`), a context object threaded through every call, a generic parameter with a trait bound (`F: FnMut`, `W: Write`, `A: Allocator`), or a thread-local. Each choice shows up in signatures differently or not at all, and testing the code means a mock, a trait object or a feature flag.

In Gamma, an **effect** is an interface whose implementation is supplied by the caller's context instead of by an argument. A function lists the effects it uses after `needs`, joined with `+`. A **handler** supplies the implementation, and `with` installs it around a block. Effect operations are called through their effect (`Log.log(...)`), so every use of the context is visible where it is written.

```gamma
Level :: union { Debug, Info, Warn, Error }

Log :: effect {
    log: fn(level: Level, msg: string)
}

connect :: fn(host: string) -> bool needs Log {
    Log.log(.Info, "connecting")
    if host.len is 0 {
        Log.log(.Error, "empty host name")
        return false
    }
    return true
}

main :: fn() needs Out {
    printer :: handler Log {
        log(level, msg) => print("[{}] {}\n", level, msg)
    }
    ok := with printer { connect("example.org") }
    print("ok = {}\n", ok)
    with printer { _ = connect("") }
}
```

```output
[.Info] connecting
ok = true
[.Info] connecting
[.Error] empty host name
```

`print` itself is just `Out.write` underneath, so `Out` shows up in `main`'s signature in exactly the same way. The ambient effects (`Alloc`, `Out`, `Err`, `In`, `Clock`, `Rand`, `Fs`, `Net`, `Proc`, `Env`) have default handlers installed by the runtime before `main`, which is why `main` can simply list them. Everything else must be listed or handled, and the compiler checks it: there is no run-time "unhandled effect".

### Testing instead of mocking

Because the file system, the clock, randomness, the environment and the allocator are all effects, code is testable without a trait for each of them. `gamma test` installs fakes for the ambient effects (captured output, an in-memory file system, a fixed clock, a leak-checking heap), and your own effects are replaced by writing a handler:

```gamma test
Store :: effect {
    load: fn(key: string) -> ?u32
    save: fn(key: string, v: u32)
}

bump :: fn(key: string) -> u32 needs Store {
    n := (Store.load(key) else 0) + 1
    Store.save(key, n)
    return n
}

test "bump counts up" {
    stored: ?u32 = null
    fake :: handler Store {
        load(key) => stored
        save(key, v) => { stored = v }
    }
    first := with fake { bump("hits") }
    second := with fake { bump("hits") }
    expect_eq(first, 1)
    expect_eq(second, 2)
}
```

The Rust version defines `trait Store`, makes `bump` generic over `S: Store` (or takes `&mut dyn Store`), writes a `FakeStore` struct, and passes it in; production code passes a real one. Here `bump` has no extra parameter, the production handler is installed at `main`, and the fake lives in the test, capturing a local by reference.

### Three kinds of operation

Each operation has a kind, and the kind says what the handler is allowed to do, so reading an effect tells you the worst case:

| Kind | A handler clause may | The caller of the operation |
| --- | --- | --- |
| `fn` | return a value | always continues, like an ordinary call |
| `esc` | return a value or **exit** the `with` | continues, or is unwound and never continues |
| `ctl` | capture the continuation and switch stacks | is suspended and may continue later |

`esc` is where Rust programmers will find a replacement for several patterns: early exit through many frames (`?` chains, `panic` with `catch_unwind`, `break 'outer` out of a closure), cancellation, and aborting a request. A handler clause exits with `break`, every frame between the call and the `with` is unwound, and each frame's `defer`s run. It is plain code running, with no unwinding tables.

```gamma
Fail :: effect {
    fail: esc(msg: string) -> never
}

digit :: fn(c: u8) -> u32 needs Fail {
    if c not in '0'..='9' { Fail.fail("not a digit") }
    return u32(c - '0')
}

sum_digits :: fn(s: string) -> ?u32 {
    total := 0u32
    ok := true
    stop :: handler Fail {
        fail(msg) => {
            ok = false
            break
        }
    }
    with stop {
        for c in bytes(s) { total += digit(c) }
    }
    if ok { return total }
    return null
}

main :: fn() needs Out {
    print("{} {}\n", sum_digits("1234"), sum_digits("12x4"))
}
```

```output
10 null
```

**When to use which.** Gamma's rule of thumb is: if the *nearest* caller is the right one to decide what to do about a failure, return a result (a parse error, a missing file); if whoever *set up the context* is, perform an `esc` operation (abandon the request, exit the process, fail the test, stop an iteration). Failing the test is how `expect` works: it performs `Test.fail`, an `esc` operation, so the test unwinds and runs its `defer`s.

### Effects compared with what you know

- **Versus generic parameters (`W: Write`).** A bound is part of the type and must be repeated through every generic function in the chain. An effect set is also repeated through the chain (`needs Out`), but the handler is selected once, at a `with`, instead of being threaded through as an argument or a type parameter.
- **Versus globals.** A global allocator or `println!` is the same idea without the signature and without the ability to replace it for a region. `Sys` marks what still escapes the model ([below](#safety-and-what-plays-the-role-of-unsafe)).
- **Versus `async`.** `ctl` operations are the coroutine part; see [fibers](#fibers-instead-of-async).
- **Callbacks.** A callback parameter's declared effects are handled by the callee; anything else it does is handled where it was written. Ordinary higher-order functions need no effect variables.
- **Costs are visible.** `fn` operations are calls, `esc` operations add a check of an exit flag after the call, and `ctl` operations are stack switches on stacks you provide. A function whose effects are all `fn` always returns to its caller.
- **A clause does not handle its own performs.** Inside a handler clause, effects are handled by the context *outside* the handler. That is what keeps nested handlers for one effect predictable.

You can also give a *type* a handler once with `handler Log for ^CountingLog { ... }` and then use values directly in `with &stats { ... }`. This is how the allocator types work. A `with` can install several handlers (`with a, b { ... }`), and a named effect set (`Io :: Fs + Alloc + Err`) is a plain abbreviation for a signature that would otherwise be long. See [chapter 7](07-effects.md).

## Fibers instead of `async`

Rust's `async fn` returns a state machine (a `Future`) that something polls. The language needs `Pin`, an executor ecosystem, and a visible split between async and non-async functions (the "function colouring" problem). Gamma's `ctl` operations are one-shot continuations: a handler can **suspend the code that performed the operation**, and resume or cancel it later. Suspended code lives on a stack that you provide, with `with ... on stack`; suspension is a stack switch, and nothing is allocated implicitly.

```gamma
Ask :: effect {
    ask: ctl(question: string) -> u32
}

quiz :: fn() -> u32 needs Ask {
    a := Ask.ask("first number?")
    b := Ask.ask("second number?")
    return a + b
}

STACK_SIZE :: 16384
@static_assert(STACK_SIZE >= @min_stack, "fiber stack too small")

main :: fn() needs Out {
    stack: [STACK_SIZE]u8 = ---
    n := 0u32
    answerer :: handler Ask {
        ask(question, k) => {
            n += 1
            resume k(n * 10)
        }
    }
    total := with answerer on stack[..] {
        quiz()
    }
    print("{}\n", total)
}
```

```output
30
```

The handler gets a continuation `k` (a `cont(u32)`, because the operation returns a `u32`) and ends with `resume k(v)`, which makes the suspended `ask` return `v`. A continuation can be stored in a queue, which is all a scheduler needs; the specification's example is a short cooperative round-robin scheduler, and the standard library module `std/fiber` provides one, together with channels and sleeping (`std/fiber/channel`, `fiber.sleep`) and pull-style generators (`std/fiber/generator`).

How this compares with `async`/`await`:

| | Rust `async` | Gamma fibers |
| --- | --- | --- |
| suspension point | `.await` on a `Future` | performing a `ctl` operation |
| function colour | `async fn` vs `fn` | none, apart from the effect set: a function that can suspend lists the effect, like any other effect |
| what holds the state | a compiler-generated state machine, sized by the compiler | a stack you provide, so memory use is explicit |
| scheduler | a runtime crate (Tokio, async-std, ...) | a handler; `std/fiber` has a cooperative one |
| allocation | boxing futures, `Pin<Box<...>>` | none unless you allocate the stacks yourself |
| cancellation | dropping the future | `cancel k` unwinds the fiber, running its `defer`s |
| concurrency | usually multi-threaded executors, `Send` bounds | cooperative on one thread; fibers do not migrate between threads |
| parallelism | threads or the runtime | `std/thread` (requires `Sys`) with the C11 memory model |

The "colour" is much milder than in Rust, because the effect set is already how a function says what it needs, and ordinary higher-order functions work with callbacks that suspend. The rules that make continuations safe are one-shot use, being consumed before the `with` that made them ends, and staying on their thread; Debug and Checked builds check them dynamically where the compiler cannot. There is no `Send`/`Sync` analysis: threads exchange data explicitly, and a data race is undefined behavior, as in C. See [chapter 12](12-fibers.md).

## Compile-time evaluation instead of macros

Rust has `const fn` (a restricted subset of the language), `macro_rules!` and procedural macros, `build.rs`, and `#[cfg]`. Gamma has **no macro language and no separate compile-time sublanguage**. A call is evaluated at compile time if the callee is a Gamma function with an *empty effect set* and the arguments are constants. Because a function with no effects (and no `Sys`) has no hidden state, the compiler can run it, and the evaluator is held to the same rules as the real machine: every check is on, undefined behavior is detected, and a failure is a compile error with the compile-time call stack.

```gamma
squares :: fn() -> [8]u32 {
    table: [8]u32 = ---
    for i in 0..8 {
        table[i] = u32(i * i)
    }
    return table
}

SQUARES :: squares()

when target.os is .Windows {
    SEP :: '\\'
} else {
    SEP :: '/'
}

main :: fn() needs Out {
    print("{} {}\n", SQUARES[7], SEP is '/' or SEP is '\\')
}
```

```output
49 true
```

`squares` is an ordinary function with an ordinary loop; nothing marks it `const`. If you add a `print` to it, its effect set becomes `{Out}` and the line `SQUARES :: squares()` stops compiling, which is exactly right. The compile-time budget is large (at least ten million steps per declaration; `#fuel(n)` asks for more), and a runaway loop is a compile error rather than a hang.

| Rust | Gamma |
| --- | --- |
| `const fn` | any function with an empty effect set |
| `const X: T = ...;` | `X :: ...` at the top level (always evaluated at compile time) |
| `#[cfg(target_os = "linux")]` | `when target.os is .Linux { ... }`, also for fields, variants and statements |
| `cfg!(debug_assertions)` | `when target.mode is .Debug { ... }` |
| `compile_error!` | `@compile_error("...")` |
| `static_assertions` | `@static_assert(cond, "message")` |
| `std::mem::size_of::<T>()` | `@size_of(T)` |
| `println!`-style macros | rest parameters: `values: ..A` collects arguments into a typed tuple |
| `macro_rules!` that generates code | write a function; generic code is monomorphized |
| proc-macro derive for serialization | write the function for your type; there is no reflection over fields |

The only branches of a `when` that are type-checked are the taken ones, so a branch may mention functions that exist only on another platform. A `when` condition cannot depend on a type parameter of an enclosing generic function, because generic bodies are checked once. If different types need different code, write separate functions.

Where Rust programmers use macros for variadic APIs, Gamma uses **rest parameters**: a parameter `values: ..A` collects the remaining arguments into a typed, counted tuple, and `print(template, ..values)` passes them on. That is how `print` and your own logging wrappers are written, with no macro involved ([chapter 6](06-functions.md#rest-parameters)).

What you give up is code generation: there is no way to generate declarations or syntax, derive an implementation from a type's fields, or write a domain-specific language. That was a deliberate choice (one feature per job, with no second language at compile time), and it is the part of Gamma most likely to hurt a Rust programmer who leans on `serde`. See [chapter 11](11-compile-time.md).

## Safety, and what plays the role of `unsafe`

**Gamma has no `unsafe` keyword and no unsafe blocks.** The language has one model for what is checked, and operations that can be undefined behavior are ordinary operations. Three mechanisms play roles that `unsafe` plays in Rust:

1. **Operations outside the checked model are marked by their names, not by a block.** Pointer arithmetic and unchecked indexing on a many-pointer `[^]T` (`p[i]`, `p + n`), `cast` between pointer types (`p.cast(^const Dog)`), `downcast_unchecked`, and `to_ptr` from an integer are available in any function, and the word in the code is the warning. Prefer slices, which carry their length and are bounds-checked.
2. **`Sys` marks the edge of the model.** `Sys` is an effect with no operations that *cannot be handled*. Calling a foreign C function, reading or writing a `#global` variable, volatile memory access, inline assembly and creating threads all perform `Sys`, so a function whose signature does not mention `Sys` cannot observe or modify anything outside its arguments, the memory they point to, and its handlers. `main` lists it when it needs it. This is the closest thing to "unsafe in the signature", but note that it marks *interaction with the outside*, not *possible undefined behavior*.
3. **Checks and build modes.** The common errors that `unsafe` is for (bounds, overflow, unwrapping `null`) are checked operations that trap rather than being undefined behavior, in every mode except Fast. Safe builds extend the checks to every memory access.

A consequence: Gamma gives you no way to say "this module contains no memory unsafety", the way `#![forbid(unsafe_code)]` does in Rust. What you can say is "this function has no `Sys`", and that is a guarantee about state, not about pointer misuse.

The build modes (selected with `--mode=debug|checked|safe|fast`, default Debug in the tool) are:

| Mode | Optimized | Checks | Extras |
| --- | --- | --- | --- |
| Debug | no | all | poisoned memory, allocation tags, fiber guard pages, leak-checking tests |
| Checked | yes | all | none |
| Safe (optional) | yes | all, plus every memory access | memory errors trap |
| Fast | yes | none, except integer division | none |

There is **one rule for checks**: every check traps in Debug, Checked and Safe builds and is removed in Fast builds, and nothing in the source opts out of one locally. Rust's per-profile `overflow-checks` and the per-call `get_unchecked` have no equivalent: you choose Fast for the whole build, or you index through a many-pointer, which is never checked.

`assert(cond, msg)` follows the same rule. It is closest to `debug_assert!`, except that it is also on in Checked builds, and in Fast builds it is *removed entirely*, argument and all, so side effects inside it vanish. Never put work you need in an assert. `trap(msg)` always traps, in every mode.

## Talking to C

`extern "C"` becomes `foreign "c"`, a block whose members are Gamma-typed declarations:

```gamma
c :: import "std/c"

libc :: foreign "c" {
    abs :: fn(x: c.c_int) -> c.c_int needs not
    strlen :: fn(s: cstring) -> usize needs not
}

main :: fn() needs Out {
    print("{} {}\n", libc.abs(-7), libc.strlen(c"gamma"))
}
```

```output
7 5
```

A foreign function performs `Sys` unless you annotate it. `needs not` is a trusted assertion that the C function touches nothing but its arguments, and the compiler does not verify it (Rust's equivalent is a safe wrapper around an `unsafe` call). Because both functions are annotated here, `main` does not need `Sys`; without the annotations it would have to list it.

Other points:

- **C-compatible types are an explicit subset.** Integers, floats, `bool`, pointers (`?^T` is a nullable `T *`), function pointers, fixed arrays inside structs, and structs of those. Tagged unions, optionals of non-pointers, results, slices and tuples cannot appear in a foreign signature, because their layouts are Gamma's business. There is no `#[repr(C)]` to remember, because C layout is the default for structs.
- **Exports** are `#export("sym")` on a top-level function, using the C ABI. Its effect set must be empty or `Sys`, since a C caller cannot supply handlers.
- **Callbacks** are `^fn` values with C-compatible signatures and an empty (or `Sys`) effect set.
- **Strings.** A `string` is not NUL-terminated and never converts implicitly to a `cstring`; `c"..."` literals are NUL-terminated.
- **The C back end is the native path.** `gamma build` generates C and compiles it with the compiler named by `CC`, and cross-compiling means pointing `CC` (or `--target`) at a cross compiler. `gamma run` interprets by default and makes foreign calls itself; `gamma run --backend=c` compiles first.

See [chapter 14](14-interop.md), which also covers `#global`, atomics (C11 memory model), volatile access, `#bits`, `#packed` and freestanding programs.

## Packages and Cargo

The `gamma` tool plays the role of `cargo` and `rustfmt` together:

| Cargo | Gamma |
| --- | --- |
| `cargo new app` | `gamma new app` (also `gamma init`) |
| `cargo run` | `gamma run app`, `gamma run --backend=c app` |
| `cargo build --release` | `gamma build --mode=checked app` (or `--mode=fast`) |
| `cargo check` | `gamma check app` |
| `cargo test` | `gamma test app` (`--filter`, `--all`, `--coverage`) |
| `cargo bench` | `gamma bench app` |
| `cargo fmt` | `gamma fmt` |
| `cargo doc` | `gamma doc` (from `///` and `//!` comments) |
| `cargo add dep` | `gamma add dep --git URL`, `--path DIR` or `--url ARCHIVE` |
| `cargo update` | `gamma update` |
| `Cargo.toml`, `Cargo.lock` | `gamma.toml`, `gamma.lock` |
| `cargo fix` | `gamma fix` (applies unambiguous compiler suggestions) |
| REPL (`evcxr`) | `gamma repl` |

A manifest looks familiar:

```text
[package]
name = "app"
version = "0.1.0"

[dependencies]
json = { path = "../json" }
http = { git = "https://github.com/someone/http-gm", version = "1.2" }

[build]
mode = "checked"
c-sources = ["c/shim.c"]

[profile.release]
mode = "fast"
```

Differences you will notice:

- **There is no registry.** A dependency is a path, a git repository or a `.tar.gz` archive. Version requirements use Cargo's syntax and match tags in a git repository.
- **One revision per repository.** Gamma does not link two versions of one package side by side, so two packages that need incompatible versions of a third cannot be used together until one is updated. There is no `semver`-compatible duplicate in the dependency graph.
- **A module is a directory**, and all `.gm` files in it share one namespace; subdirectories are separate modules. Files and declarations are unordered, and `import` is an expression bound with `::`: `geom :: import "./geom"`. The standard library is `import "std/..."`. There is no `use`.
- **Profiles** hold `mode`, `debug-info`, `cflags`, `ldflags`, `c-sources` and the like. C sources in a package are compiled and linked automatically, which is the role of `build.rs` plus `cc` for most crates.
- **Layering.** The language defines the core library (`string`, `List`, `HashMap`, `print`, ...); the rest of `std/` (collections, files, JSON, hashes, time, fibers, threads) is ordinary library code outside the specification.

See [chapter 1](01-getting-started.md) and [chapter 15](15-packages.md).

## Testing

`#[test]` functions become `test "name" { ... }` declarations. A test passes if it finishes; it fails if `expect`, `expect_eq` or `ok` fails, a trap occurs, or the leak-checking heap finds a leak. There are no `assert_eq!`-style macros: `expect(cond)`, `expect_eq(actual, expected)` (shows both values on failure) and `ok(result)` (the success value of a result, or a test failure with the error; a test cannot use `try`, since it returns nothing) are ordinary functions that perform the `Test` effect, so a helper that checks things on a test's behalf says `needs Test` in its signature.

```gamma test
str :: import "std/str"

check_sorted :: fn(xs: []const i32) needs Test {
    for i in 1..xs.len {
        expect(xs[i - 1] <= xs[i], "not sorted")
    }
}

test "sorting" {
    xs := [_]i32(3, 1, 2)
    xs[..].sort()
    check_sorted(xs[..])
    n := ok(str.parse_int(i32, "42"))
    expect_eq(n, 42)
}

test "no leaks" {
    names := list(string)
    defer names.deinit()
    ok(names.push("ada"))
    expect_eq(names.items.len, 1)
}
```

Tests run in the interpreter by default (or compiled, with `--backend=c`) and in parallel with `-j`. Because the ambient effects are replaced during tests (captured output, in-memory file system, fixed clock and seed), tests are deterministic with no mocking code of your own. There is no `#[should_panic]`: a trap ends the process, so a test cannot expect one. Test a condition that would trap by checking it first, for example with `x.checked(u8)` instead of `x.trunc(u8)`.

## Rust habits to unlearn

Each item is something that compiles, or is idiomatic, in Rust and does not carry over. The ones with an error block are compile errors in Gamma.

**Shadowing.** `let x = x + 1;` is rejected; see [bindings](#bindings-and-mutability).

**`==`.** Equality is `is`, with `is not` for inequality. `is` compares slices and strings by contents and structs field by field.

```gamma error
check :: fn(x: i32) -> bool {
    return x == 0             // error: `==` is not an operator; use `is`
}
```

**`as` casts.** There are none. Use `T(x)` when the conversion is lossless and a named method (`trunc`, `checked`, `sat`) otherwise. `T(x)` also replaces `T::from(x)` and `.into()` for numbers, and it rejects lossy conversions at compile time:

```gamma error
narrow :: fn(x: u32) -> u8 {
    a := x as u8              // error: there is no `as`
    return a
}
```

```gamma error
narrow :: fn(x: u32) -> u8 {
    return u8(x)              // error: not every u32 fits a u8
}
```

**`x?` means something else.** Postfix `x?` *asserts* that an optional is present and traps if it is `null`; it is the analog of `unwrap()`. Propagation is the prefix `try x`. Postfix `x!` likewise unwraps a result, and `!e` is how an error value is written. Typing `x?` out of habit in a fallible function compiles and traps at run time, so check which one you typed.

**Relying on `Drop`.** Nothing runs when a value goes out of scope. A `List` that is not `deinit`ed leaks, a file that is not closed stays open, and the compiler does not tell you. Put `defer` on the line after the acquisition, and run your tests so the leak checker can tell you.

**Forgetting the effect.** A function that calls `print`, `new`, `Clock.now()` or anything with effects must list them or handle them. In Rust, `println!` works anywhere.

```gamma error
log :: fn(msg: string) {
    print("{}\n", msg)        // error: performs Out, which `log` neither lists nor handles
}
```

**Discarding a result.** An unused non-`()` value is an error, not a lint. Write `_ = f()` to discard on purpose.

```gamma error
check :: fn(s: string) -> ?u32 {
    return null
}

run :: fn() {
    check("x")                // error: the value is unused; write `_ = check("x")`
}
```

**Positional struct construction.** Fields are always named, and `Point(x: 1, y: 2)` is the only form; `..Default::default()` becomes field defaults and `..other`.

```gamma error
Point :: struct { x, y: i32 }

make :: fn() -> Point {
    return Point(1, 2)        // error: no positional arguments for a struct
}
```

**Smaller habits.**

- A returned or stored closure, `Box<dyn Fn>`, `impl Trait for T` and `trait` have no counterpart; see [closures](#closures) and [generics](#generics-without-traits).
- `push` and other allocating calls are fallible: `names.push("ada")` needs a `try` (or an `else`) because it returns `AllocError!()`.
- `e.name(args)` finds a field, a built-in method, or a function of the module that declares `e`'s type. You cannot add methods to a type from another module, so you call a free function: `str.trim(s)` (from `std/str`), not `s.trim()`. And `len` is a field: `s.len`, not `s.len()`.
- Structs are not ordered, so there is no `#[derive(Ord)]`; sort by a tuple of keys.
- `&mut` does not exist. A pointer to a local is `&x`, only the call site shows that the callee can modify it, and nothing prevents two pointers to one variable.
- Threads are a library facility that requires `Sys`, and allocation goes through `Alloc`; there is no global allocator and no `std::thread::spawn` available everywhere.
- Debug output is the `Out` or `Err` effect. For quick tracing from a function that has no effects, add `needs Err` and call `eprint`, or return the data and print it at the edge.

## Where to go next

- The [tutorial chapters](README.md) are the full introduction; [chapter 7](07-effects.md) (effects), [chapter 9](09-memory.md) (memory) and [chapter 13](13-safety.md) (safety) are the ones that differ most from what a Rust programmer expects.
- [§1 of the specification](../../gamma-spec.md#1-introduction) states the five principles, and [Appendix C](../../gamma-spec.md#appendix-c-design-rationale) derives each rule from them, including why Gamma has no traits (C.6), what was rejected (C.8), and the design of results versus effects (C.1).
- `examples/` in the repository has complete programs: `generators.gm` (iterators and fibers), `allocators.gm` (custom allocators), `fibers.gm`, `wordfreq.gm` and the multi-module `bank/` package.
- If you also know C, [Gamma for C programmers](for-c-programmers.md) covers the same ground from C's side.
