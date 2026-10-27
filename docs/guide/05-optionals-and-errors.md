# 5. Optionals and errors

[Contents](README.md) · [Previous: Types](04-types.md) · [Next: Functions](06-functions.md)

C signals "no value" with `NULL` or `-1` and "this failed" with an `errno` or an out-parameter, and nothing makes you check either. Gamma has two types for these two ideas, and they mirror each other:

- `?T` is a `T` that may be absent (`null`).
- `E!T` is either a success `T` or an error `E`.

Both wrap implicitly (a `T` can be used where a `?T` or `E!T` is expected), both unwrap with the same few operators, and neither can be used as its payload without saying what happens in the failure case. Exceptions do not exist in Gamma; failure is a value, and propagating it is a visible `try`.

| | Optional `?T` | Result `E!T` |
| --- | --- | --- |
| Absent / failed value | `null` | `!e` |
| Present / success value | `x` (wraps implicitly) | `x` (wraps implicitly) |
| Trust-me unwrap | `x?` | `x!` |
| Propagate | `try x` | `try x` |
| Fallback | `x else d` | `x else d`, `x else e => d` |
| Checked unwrap | `if v := x` | `if v := x { } else e => ...` |

## Optionals

```gamma
first_even :: fn(xs: []const i32) -> ?i32 {
    for x in xs {
        if x % 2 is 0 { return x }       // an i32 becomes a present ?i32
    }
    return null
}
```

A `?T` is declared with a prefix `?`, and the prefix applies to everything after it: `?^Node` is an optional pointer, and `[]?u8` is a slice of optional bytes. For non-null pointer types, `?T` has the same layout as the pointer (null is address 0), so it costs nothing and is C-compatible.

There are four ways to get a value out of an optional, in decreasing order of how often you want them:

```gamma
use_optional :: fn(xs: []const i32) needs Out {
    // 1. Test and bind: the value exists only inside the block.
    if v := first_even(xs) {
        print("first even: {}\n", v)
    }

    // 2. Supply a fallback.
    n := first_even(xs) else -1

    // 3. Leave the function if absent (the fallback has type `never`).
    w := first_even(xs) else return

    // 4. Assert that it cannot be absent. Traps if it is.
    z := first_even(xs)?

    print("{} {} {}\n", n, w, z)
}
```

`x else d` evaluates `d` only when `x` is absent. `d` may be a `T`, another `?T` (which makes the whole thing optional, so `a else b else c` chains), or something of type `never` such as `return`, `break`, `continue` or `trap(...)`. When `d` has another type that `T` reaches by only adding `const`, the result has `d`'s type: with `name: ?[]u8`, `name else "anonymous"` is a `string`, as an `if` with those two branches would be. `d` may also be a block, whose value is the fallback or which leaves the function:

```gamma
first_or_report :: fn(xs: []const i32) -> i32 needs Out {
    n := first_even(xs) else {
        print("no even number\n")
        return -1
    }
    return n
}
```

(Inside an `if` or `while` condition, where `{` starts the body, put such a fallback in parentheses.)

`x?` is a **checked operation**: if `x` is `null`, the program traps in Debug and Checked builds and produces an unspecified value in Fast builds. It says "this cannot fail here", so reserve it for places where that is actually true; to *handle* absence use `if`, `else` or `try`.

Absent values compare as you would hope: `x is null` and `x is not null` work, and two optionals are `is`-equal if both are `null` or both hold equal values.

### Propagating absence with `try`

In a function that itself returns an optional, `try x` unwraps a present value and otherwise makes the whole function return `null`:

```gamma
sum_of_first_evens :: fn(a, b: []const i32) -> ?i32 {
    x := try first_even(a)
    y := try first_even(b)
    return x + y
}
```

### Arithmetic and lookups that return optionals

Several operations produce an optional so that "did it work" is a value rather than a trap or a wrap:

- `a +? b`, `a -? b`, `a *? b`, `a <<? b` give `null` on overflow ([chapter 2](02-basics.md)).
- `x.checked(T)` gives `null` if the value does not fit in `T` ([chapter 4](04-types.md)).
- `xs.get(i)`, `xs.first()`, `xs.last()` and `xs.index_of(v)` give `null` instead of trapping or returning `-1`.
- `Dir.from_tag(n)` gives `null` for an integer that is not an enum tag.

```gamma
scaled :: fn(xs: []const u16, i: usize, k: u16) -> u16 {
    x := xs.get(i) else return 0
    return x *? k else 65535         // saturate to the maximum on overflow
}
```

### Nested optionals

Wrapping is one level at a time. `??i32` has two distinct absent values: the outer `null`, and an inner absent value written `?i32(null)`. This matters for containers: a `HashMap(K, ?V)` lookup returns `??V`, so "missing key" and "key present with no value" are different.

## Results and errors

A result type is written `E!T`: success holds a `T`, failure holds an `E`. The error type can be anything, but the convention is a union with one variant per kind of failure, so callers can `match` on it exhaustively:

```gamma
ParseError :: union { Empty, BadDigit: usize, TooBig }

parse_u16 :: fn(s: string) -> ParseError!u16 {
    if s.len is 0 { return !.Empty }
    n: u16 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }    // `i` says where
        n = n *? 10 +? u16(c - '0') else return !.TooBig
    }
    return n                                              // wraps as a success
}
```

Three pieces of syntax appear here:

- `!e` builds an error value. With a union error type known from context, variant shorthand works: `return !.TooBig`.
- `return n` returns a success; the `u16` wraps into `ParseError!u16` implicitly.
- `n *? 10 +? u16(c - '0') else return !.TooBig` combines optional-returning arithmetic with an early error return. The `?` flavors accept an optional operand, so they chain: the result is `null` if any step overflows. This is the idiom for checked arithmetic in a function that returns a result.

Printing a result shows the success value or `!` followed by the error, for example `80` or `!.BadDigit(2)`.

### Handling a result

Everything you can do with an optional you can do with a result, with one addition: you can see the error.

```gamma
handle_results :: fn(text: string) needs Out {
    // Bind on success.
    if port := parse_u16(text) {
        print("port {}\n", port)
    } else e => {
        print("failed: {}\n", e)         // `e` is the ParseError
    }

    // Supply a fallback, ignoring the error.
    a := parse_u16(text) else 8080

    // Supply a fallback that depends on the error.
    b := parse_u16(text) else e => match e {
        .TooBig => 65535
        _       => 8080
    }

    // Assert success. Traps (reporting the error value) if it failed.
    c := parse_u16("443")!

    print("{} {} {}\n", a, b, c)
}
```

A `match` can handle every case at once. `!p` matches an error, and any other pattern is matched against the success value, so a bare name binds the success:

```gamma
report :: fn(text: string) needs Out {
    match parse_u16(text) {
        !.Empty        => print("empty input\n")
        !.BadDigit(at) => print("bad digit at {}\n", at)
        !.TooBig       => print("too big\n")
        port           => print("port {}\n", port)
    }
}
```

To test without binding, use `is`: `r is !_` is true for any error and `r is not !_` for success, and `r is !.TooBig` tests a particular variant.

> **Common mistake: discarding an error.** A result is a value, and the language does not let a statement silently drop a non-`()` value. `parse_u16(text)` on its own line is an error. To ignore a result on purpose, write `_ = parse_u16(text)`, which keeps the decision visible in the source.

## `try` and error propagation

`try x` evaluates `x`; if it holds a success, the value is the result, and otherwise the *enclosing function returns the error immediately*, running its `defer` and `errdefer` statements on the way out. It is Gamma's replacement for the `if (rc != 0) return rc;` ladder.

```gamma
ConfigError :: union { Parse: ParseError, Missing }

port_from :: fn(text: ?string) -> ConfigError!u16 {
    s := text else return !.Missing
    return try parse_u16(s)             // a ParseError becomes ConfigError.Parse
}

main :: fn() -> ConfigError!() needs Out {
    port := try port_from("8080")
    print("port {}\n", port)
    return ()
}
```

Notice that `port_from` returns `ConfigError`, but `parse_u16` fails with `ParseError`. That is allowed because of **error lifting**: if the target error type is a union with *exactly one* variant whose payload type is the source error type, the error is wrapped into that variant automatically. The same lifting applies to `return !e` and to any other place an `E!T` is converted to an `F!T`. Lifting goes one level only, and it requires exactly one match. If two variants carried a `ParseError`, the lift would be ambiguous and a compile error, and you would write the choice yourself:

```gamma
Both :: union { First: ParseError, Second: ParseError }

pick_first :: fn(s: string) -> Both!u16 {
    value := parse_u16(s) else e => return !.First(e)
    return value
}
```

If `main` returns a result and ends with an error, the runtime writes the error's type name and variant to standard error and exits with status 1.

Some rules for `try`:

- It works only in a function that returns an optional (with an optional operand) or a result (with a result operand whose error type is the function's, or lifts into it). In any other function it is a compile error.
- It never converts between the two. If a lookup gives `null` and you want an error, say so: `v := lookup(k) else return !.Missing`. In the other direction, `parse(s) else null` turns a result into an optional, discarding the error.
- It is a prefix operator at the same level as unary minus, so `try a.b()` applies to `a.b()`, and `try x + 1` means `(try x) + 1`.
- Unparenthesized `try x else d` is an error because it could mean either `(try x) else d` or `try (x else d)`. Write the one you mean.
- Inside a function literal or a handler clause, `try` returns from the literal's function or, for a handler literal's clause, from the enclosing function ([chapter 7](07-effects.md)).

> **Common mistake: `try` in `main` with no result.** `try` needs somewhere to send the error. In `main :: fn() needs Out { ... }` there is nowhere, so `try` is a compile error. Either declare `-> E!()` for `main` (above), or handle the result with `else`, `match` or `if`. The same holds for a `test` body, which returns nothing: use `ok(x)`, which gives the success value of a result or fails the test with the error ([chapter 13](13-safety.md)).

## Optional chains and iterators

Optionals and results compose with the loop constructs from [chapter 3](03-control-flow.md). `while line := next_line() { ... }` runs until the producer returns `null`. An iterator that can fail returns a result, which the `for` loop's value carries:

```gamma
try for token in tokens(src) {
    use_token(token)
}
```

(`tokens` is an iterator you would write, returning a result, and `use_token` is any function; [chapter 8](08-iteration.md) explains why a `for` can have a value.)

## Results or effects?

Gamma has algebraic effects, so you might expect failure to be one. It is not, and the line between the two is simple: **if the nearest caller is the right one to decide what to do, return a result; if whoever set up the context is, perform an `esc` operation** ([chapter 7](07-effects.md)). A parse error or a missing file is the caller's business, so it is a value in the return type, and propagating it costs a branch, like an error code in C. Abandoning a whole request, exiting the process (`Env.exit`), failing a test (`ok`, `expect`, [chapter 13](13-safety.md)) and stopping an iteration (`break` out of a `for`) are decided far up the stack, so they are `esc` operations, and the frames in between run their `defer`s on the way out.

## Summary

- `?T` for absence, `E!T` for failure. Both wrap implicitly; both unwrap via `if`/`while` binding, `else`, `try`, `match`, or the trapping `x?` / `x!`.
- `try` propagates and runs cleanup; error types lift into unions with one matching variant.
- Overflow, conversions and lookups come in forms that return optionals, so "can fail" is part of the type.
- Ignoring a result takes an explicit `_ =`.

Next, [chapter 6](06-functions.md) returns to functions in detail.

For the full rules, see [§10 Optionals and results][s10] and [§9 Patterns][s9] of the specification.

[s9]: ../../gamma-spec.md#9-patterns
[s10]: ../../gamma-spec.md#10-optionals-and-results
