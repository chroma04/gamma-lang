# 3. Control flow

[Contents](README.md) · [Previous: Basics](02-basics.md) · [Next: Types](04-types.md)

Gamma's control flow has few keywords and one theme: conditions are always `bool`, there is no fallthrough, and the constructs that C makes you fake with sentinel values (`NULL` checks, error codes, `goto cleanup`) have real forms. This chapter covers `if`, `while`, `for`, labels, `match`, and `defer`/`errdefer`.

## `if`

The condition must be a `bool`, and the braces are always required:

```gamma
describe :: fn(n: i32) \ {Out} {
    if n < 0 {
        print("negative\n")
    } else if n is 0 {
        print("zero\n")
    } else {
        print("positive\n")
    }
}
```

`else` may also start the next line (a line beginning with `else` continues the `if`), but the `} else {` style is conventional.

### `if` is an expression

An `if` produces a value, so the C ternary operator is not needed. Every branch must have the same type, and an `if` without an `else` has type `()`:

```gamma
sign :: fn(x: i32) -> i32 => if x < 0 { -1 } else if x > 0 { 1 } else { 0 }

shout :: fn(loud: bool) -> string {
    greeting := if loud { "HELLO" } else { "hello" }
    return greeting
}
```

The value of a block is the value of its last statement if that is an expression.

### Binding conditions

The most important form of `if` replaces C's "test for NULL, then use" pattern. A condition may be `pattern := expression`; it succeeds if the value matches the pattern, and the names it binds are in scope in the `then` block:

```gamma
lookup :: fn(id: u32) -> ?string {
    if id is 1 { return "ada" }
    return null
}

greet :: fn(id: u32) \ {Out} {
    if name := lookup(id) {
        print("found {}\n", name)        // name: string, definitely present
    } else {
        print("nobody with that id\n")   // name is not in scope here
    }
}
```

Against an optional `?T`, a bare name matches **only a present value** and binds a `T`. Against a result `E!T` it matches only success. The same works with union variants:

```gamma
Shape :: union {
    Circle: f32
    Rect: (w: f32, h: f32)
    Empty
}

radius_of :: fn(s: Shape) -> f32 {
    if .Circle(r) := s { return r }
    return 0
}
```

Several conditions can be joined with `and`. They are evaluated left to right, stop at the first failure, and each can use names bound by earlier ones:

```gamma
long_name :: fn(id: u32) -> bool {
    if name := lookup(id) and name.len > 3 {
        return true
    }
    return false
}
```

Binding conditions cannot be combined with `or` or `not`, and a binding that would always succeed (a plain name against a non-optional) is an error ("condition always succeeds").

When the binding is on a result, the final `else` can capture the error with `else e => ...`:

```gamma
ParseError :: union { Empty, BadDigit }

digit_value :: fn(c: u8) -> ParseError!u8 {
    if c in '0'..='9' { return c - '0' }
    return !.BadDigit
}

show :: fn(c: u8) \ {Out} {
    if d := digit_value(c) {
        print("digit {}\n", d)
    } else e => {
        print("not a digit: {}\n", e)       // prints .BadDigit
    }
}
```

This capture form is permitted only when the `if` has a single condition part. [Chapter 5](05-optionals-and-errors.md) covers results in depth.

> **Common mistake.** There is no way to write the C typo `if (x = 0)`. Assignment is a statement and equality is `is`, so `if x = 0 { ... }` is simply a syntax error. Likewise, `if (n)` on an integer is an error; write `if n is not 0`.

## `while`

```gamma
collatz_steps :: fn(start: u64) -> u32 {
    n := start
    steps: u32 = 0
    while n is not 1 {
        if n % 2 is 0 {
            n /= 2
        } else {
            n = 3 * n + 1
        }
        steps += 1
    }
    return steps
}
```

`while` also accepts a binding condition, which is how you drain something that produces optional values:

```gamma
sum_all :: fn(first: ?u32, rest: []const u32) -> u32 {
    total: u32 = 0
    if f := first { total += f }
    cur := rest.cursor()
    while x := cur.next() { total += x }
    return total
}
```

(`cursor()` and `next()` are explained in [chapter 8](08-iteration.md).) `while true { ... }` is an infinite loop; if it contains no `break`, its type is `never`, which is why a function can end with one and not need a `return`.

## `for`

`for` loops over *anything that yields values*. The three everyday forms need no library knowledge:

```gamma
for_demo :: fn(xs: []const i32) \ {Out} {
    for i in 0..3 {                      // 0, 1, 2   (half-open)
        print("{}\n", i)
    }
    for i in 1..=3 {                     // 1, 2, 3   (inclusive)
        print("{}\n", i)
    }
    for x in xs {                        // each element, by value
        print("{}\n", x)
    }
    for i, x in enumerate(xs) {          // (index, element) pairs
        print("{}: {}\n", (i, x))
    }
}
```

An array stored in a variable can be iterated directly too (`for p in primes`), and a slice can be iterated directly. The loop pattern introduces fresh names (shadowing is an error here too); use `_` to ignore the value. In a `for` head, `i, x` is a tuple pattern without parentheses.

The type of a range's variable comes from its bounds, falling back to `usize`. `0..n` counts up only; to walk a slice backwards use the standard iterator `reversed`:

```gamma
for x in reversed(xs) { ... }
```

How `for` works, and how you write your own iterators, is the subject of [chapter 8](08-iteration.md). The short version is that `for` is a handler for the `Yield` effect, so it compiles to the same loop you would write by hand.

### `break`, `continue` and labels

`break` leaves the innermost `while` or `for`; `continue` skips to its next iteration. Put a label in front of a loop to target an outer one:

```gamma
first_zero_row :: fn(grid: [4][4]u8) -> ?usize {
    found: ?usize = null
    outer: for row in 0..4 {
        for col in 0..4 {
            if grid[row][col] is 0 {
                found = row
                break outer
            }
        }
    }
    return found
}
```

Labels sit in front of `while`, `for` and `with` and nothing else. Labels share the namespace of local variables, so they cannot shadow either. `continue` works only on loops. When `break` or `continue` leaves a scope, that scope's `defer` statements run.

`for` loops and `with` blocks are expressions (chapters [7](07-effects.md) and [8](08-iteration.md)), so `break L v` can give a labeled one its value from deep inside. A `while` has no value, so a `break` aimed at one takes no operand:

```gamma
E :: effect { fail: esc() -> never }

first_pair :: fn(xs: []const i32, target: i32) -> i32 {
    return found: with (handler E { fail() => break -1 }) {
        for a in xs {
            for b in xs {
                if a + b is target { break found a * b }
            }
        }
        fail()
    }
}
```

## `match`

`match` selects the first arm whose pattern matches, and it must be **exhaustive**. Arms are separated by newlines; an arm's body is an expression or a block.

```gamma
PI :: 3.14159265358979323846

area :: fn(s: Shape) -> f32 => match s {
    .Circle(r)            => PI * r * r
    .Rect(w, h) if w is h => w * w          // a guard
    .Rect(w, h)           => w * h
    .Empty                => 0
}
```

Add a variant to `Shape` and every `match` over it that lacks a case stops compiling. There is no fallthrough.

Guards (`if cond`) are allowed, but an arm with a guard counts as matching nothing for the exhaustiveness check, so guarded arms are usually followed by an unguarded one. It is also an error for an arm to be unreachable because earlier arms already cover it.

### Patterns

```gamma
size_class :: fn(n: u32) -> string => match n {
    0         => "empty"
    1..10     => "small"             // half-open: 1 through 9
    10..=99   => "medium"            // inclusive: 10 through 99
    100, 200  => "round"             // commas separate alternatives
    _         => "large"
}
```

Literal patterns work for integers, `bool` and `string` (strings compare by content). Range patterns work for integers. Several comma-separated patterns share one arm. `_` matches anything without binding.

Patterns take apart tuples, structs and unions and bind names inside them:

```gamma
Point :: struct { x, y: i32 }

where_is :: fn(p: Point) -> string => match p {
    Point(x: 0, y: 0) => "origin"
    Point(x: 0)       => "on the y axis"      // unlisted fields are ignored
    Point(y: 0)       => "on the x axis"
    _                 => "elsewhere"
}

quadrant :: fn(x, y: i32) -> string => match (x >= 0, y >= 0) {
    (true, true)   => "northeast"
    (false, true)  => "northwest"
    (false, false) => "southwest"
    _              => "southeast"
}
```

For optionals and results, patterns unwrap implicitly: `null` matches the absent case, `!p` matches an error, and anything else is matched against the present or success value, so a bare name binds the payload.

```gamma
label :: fn(id: u32) -> string => match lookup(id) {
    null => "nobody"
    name => name                         // name: string
}

parse_label :: fn(c: u8) -> string => match digit_value(c) {
    !.Empty    => "empty"
    !.BadDigit => "bad digit"
    d          => if d < 5 { "low" } else { "high" }
}
```

When an arm needs several statements, use a block; its value is the arm's value:

```gamma
describe_shape :: fn(s: Shape) -> f32 \ {Out} => match s {
    .Circle(r) => {
        print("a circle\n")
        PI * r * r
    }
    .Rect(w, h) => w * h
    .Empty      => 0
}
```

> **Common mistake: a bare name always binds.** A pattern that is a bare identifier is *never* a comparison with an existing constant; it introduces a new binding that matches everything. In C, `case LIMIT:` compares; here the equivalent would silently capture. To prevent that mistake, the compiler rejects a bare identifier pattern that names a visible constant:
>
> ```gamma
> LIMIT :: 100
>
> check :: fn(n: u32) -> bool => match n {
>     LIMIT => true            // error: `LIMIT` names a constant; bare names bind
>     _     => false
> }
> ```
>
> Use a guard, `v if v is LIMIT => true`, or a qualified pattern for a constant from another module (`limits.MAX => ...`).

> **Common mistake: a method chain across arms.** Each line of a `match` body starts a new arm, even if the line begins with `.`. If an arm's expression needs to continue on the next line, wrap it in `( )` or `{ }`.

## `defer` and `errdefer`

`defer S` schedules the statement `S` to run when control leaves the enclosing block, however it leaves: falling off the end, `return`, `break`, `continue`, a `try` that propagates an error. Deferred statements of one block run in reverse order of registration, like a stack. This is Gamma's replacement for `goto cleanup`.

```gamma
main :: fn() \ {Out} {
    defer print("goodbye\n")
    defer print("cleaning up\n")
    print("working\n")
}
// prints: working, cleaning up, goodbye (each on its own line)
```

A `defer` that is never reached is never registered, so you can safely write the cleanup right after the acquisition:

```gamma
copy_into :: fn(src: []const u8) -> AllocError!usize \ {Alloc} {
    buf := try make(u8, 64)
    defer free(buf)                   // runs on every exit below
    n := src.len
    if n > buf.len { n = buf.len }
    buf[0..n].copy_from(src[0..n])
    return n
}
```

(`make` and `free` are explained in [chapter 9](09-memory.md).)

`errdefer S` runs only when the block is left *abnormally*: by returning an error value (including a `try` that propagates one) or when the frame is unwound by an early exit through it (chapter 7). It is for undoing partial work:

```gamma
make_pair :: fn() -> AllocError!(a: []u8, b: []u8) \ {Alloc} {
    first := try make(u8, 64)
    errdefer free(first)              // only if we fail after this point
    second := try make(u8, 64)        // if this fails, `first` is freed
    return (first, second)
}
```

Rules to remember:

- Deferred code must be able to run to completion. It may not contain `return`, `try`, `resume`, or a `break` or `continue` that leaves the deferred statement, and it may perform only ordinary (`fn`) effect operations. A `for` loop inside a `defer` is fine.
- A **trap** does not run deferred statements. Traps are not an exception mechanism; they terminate the process (see [chapter 13](13-safety.md)).
- `defer` in a loop body runs at the end of every iteration, because each iteration is a block.

## Statement-level `when`

`when cond { ... } else { ... }` looks like `if`, but the condition is evaluated at compile time and the untaken branch is not even type-checked. It is the replacement for `#ifdef` and is covered in [chapter 11](11-compile-time.md).

## Summary

- Conditions are `bool`; assignment is not an expression; braces are mandatory.
- `if`, `while` and `match` can bind names with patterns, which replaces NULL-check-then-use.
- `for` iterates anything that yields; `break outer` and `continue` use labels.
- `match` is exhaustive with no fallthrough, and bare names in patterns always bind.
- `defer` and `errdefer` give structured cleanup without `goto`.

Next, [chapter 4](04-types.md) tours the type system.

For the full rules, see [§8 Statements and control flow][s8] and [§9 Patterns][s9] of the specification.

[s8]: ../../gamma-spec.md#8-statements-and-control-flow
[s9]: ../../gamma-spec.md#9-patterns
