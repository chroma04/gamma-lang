# Idiomatic Gamma

[Contents](README.md)

The tutorial chapters say what the language *has*. This page says how experienced Gamma code *uses* it: the patterns that keep programs short and clear, and the habits from other languages that do not fit. It grows as the code base does. Most examples come from, or are modeled on, `gammac`, the compiler written in Gamma in [`compiler/`](../../compiler) (see [the self-hosting notes](../self-hosting.md)), which is meant to be read as an example of the style described here.

Every example on this page is a complete program checked by the test suite.

## Layout and naming

- **Format with `gamma fmt`.** Do not hand-align; let the formatter decide.
- **Names.** Functions, variables and modules are `snake_case`, types and variants are `PascalCase`, constants are `SCREAMING_CASE` (`MAX_EXPONENT: i64 : 100_000`).
- **A module is a directory.** Put one concern in each, and split a large module into several files in the same directory: they share one namespace, so no file needs to import another. `gammac`'s `syntax/` module holds `token.gm`, `lexer.gm`, `literal.gm` and `dump.gm`.
- **Imports go at the top of the file that uses them.** Each file of a module may repeat an import with the same name and path.
- **Document what is `pub`** with `///` comments, and the module with `//!` lines at the top of one of its files. Say what a function *gives* and what it *expects*, not how it works.

## Methods are functions in the type's module

There are no `impl` blocks. A function whose first parameter is the type, or a pointer to it, is called with method syntax. Take `^T` to change the value and `^const T` to read it:

```gamma
Counter :: struct {
    count: u32 = 0
    step: u32 = 1
}

bump :: fn(c: ^Counter) { c.count += c.step }

total :: fn(c: ^const Counter) -> u32 => c.count

main :: fn() needs Out {
    c := Counter(step: 5)
    c.bump()
    c.bump()
    print("{}\n", c.total())
}
```

```output
10
```

A short function is written with `=>`. A state machine such as a lexer is a struct plus functions on `^Lexer`: `lx.peek()`, `lx.emit(kind, lo, hi)`.

## Absence: `?T`, `else` and binding conditions

Reach for an optional before a sentinel value. The forms that use one read as English:

| Want | Write |
| --- | --- |
| the value or a default | `x := s.get(i) else 0` |
| the value, or leave | `b := lx.peek() else return` (or `else break`, `else continue`) |
| do something if present | `if c := s.get(i) { … }` |
| loop while present | `while c := lx.peek() { … }` |
| test without unwrapping | `if s.get(i) is 'x' { … }` |
| test for one of several | `if s.get(i) in (' ', '\t') { … }` |

`is` and inline-tuple `in` compare an optional with plain values directly: a missing value is simply not equal.

```gamma
is_space_at :: fn(s: string, i: usize) -> bool => s.get(i) in (' ', '\t')

first_word :: fn(s: string) -> string {
    i: usize = 0
    while c := s.get(i) {
        if c is ' ' { break }
        i += 1
    }
    return s[0..i]
}

main :: fn() needs Out {
    s :: "let x"
    print("{} {} {}\n", is_space_at(s, 3), is_space_at(s, 99), first_word(s))
}
```

```output
true false let
```

Slices have `get(i)`, `first()` and `last()`, which return optionals, so `items.last()` replaces `items[items.len - 1]` with its length check.

## Failure: results, `try` and when to trap

A function that can fail for reasons outside the program's control returns `E!T`, where `E` is a union with one variant per cause. Callers propagate with `try` or handle with `else e => …` or `match`. Errors carried by `try` lift into a caller's error union automatically when it has exactly one variant of that type.

When failure is a bug or cannot be handled sensibly, unwrap with `x!` (a result) or `x?` (an optional): it traps with the error in the message. A compiler, for example, treats running out of memory as fatal, so `gammac` writes `l.push(x)!` instead of threading `AllocError` through every function. Decide this per program and say so in its documentation; a library should usually return the error instead.

```gamma
ParseError :: union { Empty, BadDigit: usize }

parse_digits :: fn(s: string) -> ParseError!u32 {
    if s.len is 0 { return !.Empty }
    n: u32 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
        n = n * 10 + u32(c - '0')
    }
    return n
}

main :: fn() needs Out {
    for text in ("42", "", "4x") {
        match parse_digits(text) {
            !.Empty => print("empty\n")
            !.BadDigit(at) => print("bad digit at {}\n", at)
            n => print("{}\n", n)
        }
    }
}
```

```output
42
empty
bad digit at 1
```

A problem in the *input* of a tool (a syntax error, a bad option) is usually neither: report it and carry on, as `gammac` does with its `diag.Diagnostics`, so the user sees every problem at once.

## Matching

`match` on bytes takes character literals, ranges and alternatives, which makes scanners read like their grammar:

```gamma
Class :: union { Letter, Digit, Space, Other }

classify :: fn(c: u8) -> Class => match c {
    'a'..='z', 'A'..='Z', '_' => .Letter
    '0'..='9' => .Digit
    ' ', '\t', '\n' => .Space
    _ => .Other
}

main :: fn() needs Out {
    for c in "a1 ;" { print("{}\n", classify(c)) }
}
```

```output
.Letter
.Digit
.Space
.Other
```

- **Tables over strings.** To map an enum to text and back, write two `match` functions (`keyword_text(k)` and `keyword_from(s) -> ?Keyword`, which matches string literals). They are exhaustive, so a new variant cannot be forgotten.
- **Test a variant with `is`**, including nested payloads: `kind is .Punct(.Dot)`. Bind with `if .Punct(p) := kind { … }` or a `match` arm.
- **Guards** go after the pattern: `.Punct(p) if p in (Punct.RParen, Punct.RBracket) => …`. A guarded arm does not count toward exhaustiveness.

## Memory: one arena per phase

Most programs allocate in phases: read a file, build a structure, print a result, forget all of it. Give each phase an arena and let the arena free everything at once. Code inside the phase allocates through the `Alloc` effect without caring where the memory comes from, and nothing needs its own `deinit`:

```gamma
arena :: import "std/arena"

words :: fn(text: string) -> List(string) needs Alloc {
    out := list(string)
    start: usize = 0
    for i, c in enumerate(text) {
        if c is ' ' {
            out.push(text[start..i])!
            start = i + 1
        }
    }
    out.push(text[start..])!
    return out
}

main :: fn() needs Alloc + Out {
    a := arena.new()
    defer a.deinit()
    with a.allocator() {
        ws := words("one two three")
        print("{} {}\n", ws.items.len, ws.items.last() else "")
    }
}
```

```output
3 three
```

Outside an arena, pair each acquisition with its release on the next line: `l := list(u8)` then `defer l.deinit()`. Tests run on a heap that reports leaks, so a missing `defer` is found by `gamma test`.

## Building text

Append to a `List(u8)` with `write`, which takes a `print` template, and write the result out once. Many small `print` calls are much slower than one large write:

```gamma
main :: fn() needs Alloc + Out {
    out := list(u8)
    defer out.deinit()
    for i in 1..4 {
        if i > 1 { out.push(' ')! }
        out.write("{}:{}", i, i * i)!
    }
    out.push('\n')!
    Out.write(out.items)
}
```

```output
1:1 2:4 3:9
```

`str.format(template, args…)` returns a fresh `[]u8` (from `std/str`) for a message that is kept, such as a diagnostic. `{:04X}`, `{:02x}` and the other format specs (§21.6.1) cover padding and bases.

## Small things

- **Multiple assignment** sets several variables at once, from a tuple or a function returning one: `p, len = .Arrow, 2`.
- **`x in s`** asks whether a slice contains `x`; there is no `.contains` method for slices.
- **`for i, c in enumerate(s)`** gives indexes and elements; `for c in s` iterates the bytes of a string.
- **`while true` with an exit inside** (`b := next() else break`) needs no `return` after the loop: the compiler knows the loop only ends where it says.
- **Conversions say what they do**: `u64(x)` only when lossless, `x.trunc(u32)` to keep the low bits, `x.checked(u8)` for an optional.
- **Tests live next to the code** in `test "…" { }` blocks; a test that allocates runs its body in an arena, or releases what it allocates.
