# 6. Functions

[Contents](README.md) · [Previous: Optionals and errors](05-optionals-and-errors.md) · [Next: Effects](07-effects.md)

You have already seen plenty of functions. This chapter collects the rules: how functions are declared, how arguments are matched to parameters, how method-call syntax works without classes, and the two kinds of function *value* Gamma has: second-class `fn` values that can capture local variables, and first-class function pointers `^fn` that cannot.

## Declaring functions

A function is a value written as a **function literal** and almost always given a name with `::`:

```gamma
add    :: fn(a, b: i32) -> i32 { return a + b }
square :: fn(x: f32) -> f32 => x * x
greet  :: fn(name: string) \ {Out} => print("hi {}\n", name)
```

- `a, b: i32` declares two parameters of one type.
- There are two body forms: a block `{ ... }` that uses `return`, or `=> expr` for a single expression whose value is returned.
- `-> R` gives the return type. **If you omit it, a named top-level function returns `()`**; its return type is never inferred from the body. (A function literal or nested function written where no type is expected does take its return type from its body, and a literal passed to a parameter of function type takes it from that type.) With a block body, the end of the block must be unreachable (every path returns) unless the return type is `()`, in which case falling off the end returns `()`.
- `\ { ... }` lists the effects the function may perform ([chapter 7](07-effects.md)); omitting it means none.

> **Common mistake: forgetting `-> R`.** The compiler takes the missing arrow at its word:
>
> ```gamma
> oops :: fn(a, b: i32) {
>     return a + b          // error: this function returns (), not an integer
> }
> ```

Several results are returned as a tuple and destructured by the caller. A named tuple documents what each element is:

```gamma
divmod :: fn(n, d: u32) -> (q: u32, r: u32) => (n / d, n % d)

split_demo :: fn() -> u32 {
    q, r := divmod(17, 5)
    return q * 10 + r
}
```

Functions can be recursive and mutually recursive, and declaration order does not matter at the top level:

```gamma
fact :: fn(n: u64) -> u64 => if n <= 1 { 1 } else { n * fact(n - 1) }
```

A function whose return type is `never` must not return: every path ends in a `never` expression such as `trap(...)`, an exit, or an infinite loop.

## Parameters

Parameters are **immutable bindings**, and each one is a distinct object initialized from its argument. Changing the original through some other path during the call is never visible through the parameter, and a callee that wants to modify its copy must make one (`m := n`, see [chapter 2](02-basics.md)). Large arrays and structs are conceptually copied, but an implementation may pass them by reference when it can prove nobody can tell, so you rarely need a pointer just to avoid a copy. To let a function modify the caller's variable, pass a pointer:

```gamma
bump_all :: fn(counts: []u32) {
    for c in each_ptr(counts) { c^ += 1 }
}

reset :: fn(n: ^u32) { n^ = 0 }
```

### Defaults, named arguments and spread

A parameter may have a default, which must be a constant expression. Parameters with defaults must come after those without:

```gamma
clamp_add :: fn(a, b: i32, lo: i32 = 0, hi: i32 = 100) -> i32 {
    return (a + b).clamp(lo, hi)
}
```

At a call, **positional** arguments bind in order, then **named** arguments (`name: value`) bind by name, and every parameter must end up bound exactly once or have a default. Named arguments always come after positional ones:

```gamma
call_demo :: fn() -> i32 {
    x := clamp_add(5, 7)                   // lo and hi use their defaults
    y := clamp_add(5, 7, hi: 10)           // skip lo, set hi
    z := clamp_add(hi: 50, b: 3, a: 1)     // all by name, in any order
    return x + y + z
}
```

Arguments are evaluated left to right in the order written, each coerced to its parameter's type.

`..e` **spreads** a tuple or struct into arguments. An unnamed tuple becomes positional arguments, and a named tuple or a struct becomes named ones. A later explicit named argument overrides a spread one:

```gamma
spread_demo :: fn() -> i32 {
    args := (5, 7)
    opts := (lo: 0, hi: 10)
    p := clamp_add(..args)                 // clamp_add(5, 7)
    q := clamp_add(1, 2, ..opts)           // clamp_add(1, 2, lo: 0, hi: 10)
    r := clamp_add(1, 2, ..opts, hi: 3)    // hi overridden
    return p + q + r
}
```

Spread works in struct construction too, which is how you write "this value, but with one field changed": `Window(..w, title: "new")`.

A parameter's default may also be `@caller_location()`, which is evaluated at each *call site*. This is how `assert` and `trap` know where they were called from, and you can use it the same way:

```gamma
require :: fn(ok: bool, msg: string = "requirement failed",
              loc: SourceLocation = @caller_location()) \ {Err} {
    if not ok {
        eprint("{} (line {})\n", (msg, loc.line))
        trap(msg)
    }
}
```

(Newlines inside the parentheses are ignored, so a long parameter list can wrap freely.)

## Methods without classes

Gamma has no classes and no member functions, but it has **method-call syntax**. For a value `e`, `e.name(args)` is resolved in this order:

1. If `e` (after at most one pointer dereference) has a struct or tuple field `name`, that is it.
2. If `name` is a built-in method of `e`'s type (`xs.sort()`, `n.checked(u8)`, `p.cast(...)`, see §21.3 of the [specification][s21]), that is it.
3. Otherwise `name` must be a **top-level function in the module that declares `e`'s type**, and `e.name(args)` means `name(e, args)`.

So a method is an ordinary function whose first run-time parameter is the type (or a pointer to it):

```gamma
Counter :: struct {
    count: u32 = 0
    step: u32 = 1
}

bump  :: fn(c: ^Counter) { c.count += c.step }
total :: fn(c: ^const Counter) -> u32 => c.count

method_demo :: fn() \ {Out} {
    c := Counter(step: 5)
    c.bump()                               // means bump(&c)
    c.bump()
    print("{}\n", c.total())               // total(&c): prints 10
    print("{}\n", Counter(step: 2).total())    // a temporary is fine for ^const
}
```

The **receiver rules** adapt `e` to the first parameter in exactly these ways and no others:

- If the parameter is `^T` or `^const T` and `e` is a place of type `T`, `&e` is passed. For `^T` the place must be mutable.
- If the parameter is `T` and `e` has type `^T` or `^const T`, `e^` is passed.
- If `e` is not a place and the parameter is `^const T`, a temporary is created and its address passed.

```gamma
mutability_demo :: fn() {
    frozen :: Counter()
    frozen.bump()          // error: bump needs a mutable place, `frozen` is immutable
    Counter().bump()       // error: mutating a temporary would lose the change
}
```

The same holds for an ordinary argument written `&e` when the parameter is `^const T`: `len2(&make_point(3))` passes the address of a temporary that lives until the call returns. Anywhere else, `&` needs a place.

Because a struct may not have both a field and a method candidate with the same name, `total` is fine here but a function named `count` taking a `Counter` would be a compile error. Methods can only be added in the module that declares the type. For a type from another module, write a free function and call it normally. Generic types work the same way; see [chapter 10](10-generics.md).

## Second-class functions: `fn`

A function literal that mentions a local variable of an enclosing function **captures it by reference**, with no allocation and no copy. The price is that such a function is **second-class**: it can be passed down the call stack, but it can never outlive the frame that created it, so it can never be dangling.

```gamma
apply_twice :: fn(f: fn(i32) -> i32, x: i32) -> i32 => f(f(x))

closure_demo :: fn() -> i32 {
    offset := 10
    return apply_twice(fn(v) => v + offset, 1)     // 21; `offset` captured
}
```

When a literal's expected type is a known function type, its parameter types (and return type) are taken from it, so `fn(v) => v + offset` needs no annotations.

What second-class functions cannot do follows from "must not outlive the frame". They may be passed as arguments, called, and captured by another function or handler literal. They may *not* be stored in a mutable variable, a struct, tuple, array, union, optional or result, returned, assigned, or captured by a function pointer or nested named function. Correspondingly, a function *type* `fn(...)` may only be written as the type of a parameter or of an immutable local:

```gamma
Button :: struct {
    on_click: fn()          // error: a second-class function type cannot be a field
}
```

In practice this is rarely limiting: callbacks in C (a function plus a `void *` context) are almost always passed *down* into a call, and that is the case `fn` handles. What it buys is that effects work with callbacks: an `fn` parameter's declared effects are handled by the callee, while everything else it does is handled where it was written. [Chapter 7](07-effects.md) explains this.

## Function pointers: `^fn`

When you need a function value that can be stored, returned, put in a table or handed to C, use a **function pointer** `^fn(...)`. It is one code address, captures nothing, and is first-class.

```gamma
double :: fn(x: i32) -> i32 => x * 2
negate :: fn(x: i32) -> i32 => -x

Op :: struct {
    name: string
    apply: ^fn(x: i32) -> i32
}

table_demo :: fn() \ {Out} {
    ops := [_]Op(
        Op(name: "double", apply: double),
        Op(name: "negate", apply: negate),
        Op(name: "inc", apply: fn(x: i32) -> i32 => x + 1),   // captures nothing, so it converts
    )
    for op in ops {
        print("{}: {}\n", (op.name, op.apply(10)))
    }
}
```

A named function converts implicitly to a function pointer (or to a second-class `fn`). A function literal converts to a pointer only if it captures nothing. Function pointers can be compared with `is`, and when their signatures use C-compatible types they are ABI-compatible with C function pointers ([chapter 14](14-interop.md)). A pointer's type fixes the effects of calling it exactly; there is no way to store a function that is polymorphic in its effects (§12.8).

| | `fn(...)` | `^fn(...)` |
| --- | --- | --- |
| Captures locals | by reference | never |
| Storable, returnable | no | yes |
| Callbacks that perform arbitrary effects | yes | only the effects in its type |
| Passable to C | no | yes, if C-compatible |

### Nested named functions

A `name :: fn(...)` binding written inside a function body is a **nested function**. It is equivalent to a private top-level function: it may use top-level names, compile-time constants and its own parameters and locals, but not the enclosing function's other locals. It is first-class, so it converts to a function pointer. If the body does use another local of the enclosing function, the binding is instead an immutable local holding a second-class `fn` value, which cannot call itself.

```gamma
sum_squares :: fn(xs: []const i32) -> i32 {
    sq :: fn(v: i32) -> i32 => v * v      // nested; captures nothing
    total := 0
    for x in xs { total += sq(x) }
    return total
}
```

## Function attributes

`#inline` asks for inlining at every call site (a compile error where that is impossible, as for a recursive function), `#noinline` forbids it, `#cold` marks a function as unlikely to be called, and `#fast_math` allows float reassociation inside the function. They never change meaning, except `#fast_math`, whose results may differ. Attributes go on their own line or just before the declaration:

```gamma
#inline
lerp :: fn(a, b, t: f32) -> f32 => a + (b - a) * t
```

`#deprecated("message")`, on any declaration, makes every use of it a compile-time warning that quotes the message:

```gamma
#deprecated("use `lerp`")
mix :: fn(a, b, t: f32) -> f32 => lerp(a, b, t)
```

## Summary

- Functions are values bound with `::`. `-> R` is required unless the function returns `()`, and `\ {...}` lists its effects.
- Calls take positional, then named arguments; defaults must be constants; `..e` spreads tuples and structs.
- Methods are module functions called with dot syntax; receiver adjustments are limited to taking or dereferencing one address.
- `fn(...)` values capture by reference but only flow downward; `^fn(...)` values capture nothing and can go anywhere.

Next, [chapter 7](07-effects.md) explains effects, the feature that makes callbacks and I/O cooperate.

For the full rules, see [§11 Functions][s11] and §7.10-7.11 of [§7 Expressions][s7] in the specification.

[s7]: ../../gamma-spec.md#7-expressions
[s11]: ../../gamma-spec.md#11-functions
[s21]: ../../gamma-spec.md#21-the-core-library
