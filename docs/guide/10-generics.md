# 10. Generics

[Contents](README.md) · [Previous: Memory](09-memory.md) · [Next: Compile time](11-compile-time.md)

C does generic programming with `void *` and macros. Gamma has exactly one mechanism: **parameters whose values are known at compile time**, most often types. A function or type with such parameters is generic, and it is checked once where it is written and then compiled separately for each distinct set of arguments (monomorphization), so there is no run-time cost.

## Generic functions

A parameter of type `type` is a type parameter:

```gamma
swap_values :: fn(T: type, a, b: ^T) {
    t := a^
    a^ = b^
    b^ = t
}

largest :: fn(T: type, xs: []const T) -> ?T where Ord(T) {
    best := xs.first() else return null
    for x in xs {
        if x > best { best = x }
    }
    return best
}
```

`T` is an ordinary parameter that happens to be a type, and later parameters and the return type can mention it. The `where Ord(T)` clause is a *constraint*, explained below.

Because `T` names a type inside the body, no local, parameter or loop variable there may be called `T`. The same rule applies to every type visible at that point: the predeclared ones (`u8`, `string`, and every sized integer name such as `u24`), top-level types and aliases, and the other type parameters. Functions and constants may still be hidden by a local, but a type may not.

```gamma
f :: fn(T: type, x: T) {
    T := 3                         // error: `T` names a type; a local may not have the name of a type
}
```

Calls usually omit the type arguments, because they can be **inferred** from the value arguments:

```gamma
generic_demo :: fn(scores: []const i32) -> i32 {
    p: i32 = 1
    q: i32 = 2
    swap_values(&p, &q)                    // T = i32, inferred from ^i32
    swap_values(T: i32, &p, &q)            // or written, by name
    return largest(scores) else 0          // T = i32, from the slice
}
```

### Inference, precisely

The rules are short and strict (§16.2, §11.3):

- A compile-time parameter is **inferable** if the type of another parameter mentions it: `T` in `swap_values` and `largest` is inferable, because `a: ^T` and `xs: []const T` mention it. An inferable parameter is **never bound positionally**. Either the arguments determine it, or you write it by name: `largest(scores)` or `largest(T: i32, scores)`. Writing `largest(i32, scores)` is an error: `i32` would be taken for the slice.
- A parameter that no other parameter's type mentions cannot be inferred, so it is an ordinary positional parameter. That is why you write `list(string)`, `make(u8, 64)` and `parse_int(u8, s)`: the type appears only in the result, or nowhere.
- Inference matches each argument's type against its parameter's type structurally. A type parameter must be determined by at least one argument, and all determinations must agree **exactly**. No widening is applied.
- Untyped literals do not determine a type parameter; they take the type that the other arguments infer. If nothing but literals could determine it, it takes the literals' fallback type (`i32` for integers, `f64` for floats), and to get another type you write the parameter by name: `make_fill(T: u8, 64, 0)`.
- A function literal argument is checked after the others, so its parameter types can come from them. In `count_if(scores, fn(s) => s >= cutoff)` below, `s` has the element type of `scores`.
- Named arguments may stand anywhere in the argument list; the positional ones bind, in order, the parameters that no named argument names and that are not inferable. By convention the named type arguments come first: `swap_values(T: i32, &p, &q)`.

```gamma
pair_max :: fn(T: type, a, b: T) -> T where Ord(T) => if a > b { a } else { b }

mistakes :: fn(x: u8, y: u16) {
    _ = pair_max(x, y)             // error: T would be both u8 and u16
    _ = pair_max(x, 4)             // fine: T = u8, and the literal takes that type
    _ = pair_max(3, 4)             // fine: nothing else determines T, so it is i32
    _ = pair_max(T: u16, x, y)     // fine: T is written, so x and y widen to u16
}
```

### Checked once

The body of a generic function is type-checked *once*, where it is written, treating `T` as an opaque type that supports only what its constraints grant. Mistakes are reported in the generic function itself, even if no one calls it. This is the opposite of C++ templates, where errors appear at instantiation, deep inside a call. Instantiation never fails type-checking, with two exceptions: an untyped literal used at type `T` is checked against each instantiation (`x: T = 300` fails for `u8`), and an `@write` template (the machinery behind `print`) is checked against its arguments.

## Constraints

Without any constraint, a type parameter supports copying, declaring variables and parameters, taking addresses, forming `^T`, `[]T`, `?T`, `E!T` and `[N]T`, `@size_of(T)` and `@align_of(T)`, and passing `T` to other generics it satisfies. To use operators on `T`, say which ones with a `where` clause. The constraints are a fixed set of predicates, not something you define:

| Constraint | Satisfied by | Grants |
| --- | --- | --- |
| `Eq(T)` | every type where `is` is defined | `is`, `is not`, `in`, literal patterns, `@hash` |
| `Ord(T)` | `bool`, integers, floats, enums; tuples, arrays and slices of these | `Eq`, plus `< <= > >=` |
| `Num(T)` | integers and floats | `Ord`, plus `+ - * /` and untyped literals at `T` |
| `Int(T)` | integers | `Num`, plus `%`, overflow flavors, shifts, bitwise operators, `.trunc` / `.checked` / `.sat` |
| `Signed(T)` | signed integers and floats | `Num`, plus unary `-` |
| `Float(T)` | floats | `Num`, plus the float methods |
| `Plain(T)` | plain-data types | `---` at `T`, and `.bits` to and from `T` |

Several constraints are separated by commas: `where Num(T), Plain(T)`.

```gamma
sum_all :: fn(T: type, xs: []const T) -> T where Num(T) {
    total: T = 0                   // an untyped literal at T: granted by Num
    for x in xs { total += x }
    return total
}

first_or :: fn(T: type, xs: []const T, fallback: T) -> T {
    return xs.first() else fallback    // needs no constraint: it only copies
}
```

Remove `where Num(T)` from `sum_all` and the compiler rejects the definition (at `total: T = 0` and at `total += x`), not at some call.

## Behavior as a parameter

Operations that constraints do not cover come in as function parameters. A second-class `fn` parameter accepts closures with no allocation ([chapter 6](06-functions.md)):

```gamma
count_if :: fn(T: type, xs: []const T, pred: fn(T) -> bool) -> usize {
    n := 0
    for x in xs {
        if pred(x) { n += 1 }
    }
    return n
}

passing :: fn(scores: []const u32, cutoff: u32) -> usize {
    return count_if(scores, fn(s) => s >= cutoff)     // captures `cutoff`
}
```

The built-in slice methods are the same idea. `sort_by` takes a comparison, and `sort_by_key` takes a function producing a key whose type has an ordering. Tuples of strings and integers are ordered lexicographically, which is how you order a struct:

```gamma
Person :: struct {
    first: string
    last: string
    age: u32
}

sort_people :: fn(people: []Person) {
    people.sort_by_key(fn(p) => (p.last, p.first))     // K = (string, string)
}

oldest_first :: fn(people: []Person) {
    people.sort_by(fn(a, b) => a.age > b.age)          // a, b are ^const Person
}
```

## Generic types

The parameters of a `struct`, `union` or `effect` are always compile-time values, because they are part of the type. They are declared with `:`. A struct can take a type and a size, for instance:

```gamma
Stack :: struct(T: type, N: usize) {
    items: [N]T
    len: usize = 0
}

stack :: fn(T: type, N :: usize) -> Stack(T, N) where Plain(T) {
    return Stack(T, N)(items: ---)
}

push :: fn(T: type, N :: usize, s: ^Stack(T, N), x: T) -> bool {
    if s.len is N { return false }
    s.items[s.len] = x
    s.len += 1
    return true
}

pop :: fn(T: type, N :: usize, s: ^Stack(T, N)) -> ?T {
    if s.len is 0 { return null }
    s.len -= 1
    return s.items[s.len]
}

stack_demo :: fn() needs Out {
    st := stack(i32, 4)
    _ = st.push(10)                // method syntax: push(&st, 10)
    _ = st.push(20)
    while v := st.pop() {
        print("{}\n", v)           // 20, then 10
    }
}
```

Observe three things.

- `N :: usize` is a **compile-time value parameter**. It is declared with `::` instead of `:`, its argument must be a constant expression, and each distinct value produces its own instantiation. Inside the body it is a constant, so it can size arrays. When it appears in the type of another parameter (as `N` does in `s: ^Stack(T, N)`), it is inferable like a type parameter.
- Because the body is checked once, without knowing `N`, an array length in it is either the parameter itself or plain `usize` arithmetic on such parameters and constants: `[N]T`, `[N + 1]T` and `[R * C]T` are fine, `[@size_of(T)]u8` is an error. Lengths are compared as written (`[A + B]T` and `[B + A]T` are the same type, `[N + N]T` and `[2 * N]T` are not), and each instantiation computes them, so `[N - 1]T` with `N = 0` is a compile error where it is instantiated. Such an array is built with `[N]T(fill: x)` or `---`, since a list of elements cannot be counted against an unknown length.

```
concat :: fn(A, B :: usize, x: [A]u8, y: [B]u8) -> [A + B]u8 {
    out: [A + B]u8 = ---
    for i in 0..A { out[i] = x[i] }
    for i in 0..B { out[A + i] = y[i] }
    return out
}
```

- Inside the type's own module, methods take the generic type as a pointer, `^Stack(T, N)`, with the type's parameters as their leading compile-time parameters. Method syntax skips those parameters, which is why `st.push(10)` works and `T` and `N` are inferred.
- Two instantiations are the same type if every argument is the same: types are identical, values are `is`-equal.

Generic unions work the same way, with named parameters in parentheses:

```gamma
Either :: union(L, R: type) {
    Left: L
    Right: R
}

describe :: fn(e: Either(i32, string)) -> string => match e {
    .Left(n)  => if n < 0 { "negative" } else { "number" }
    .Right(s) => s
}
```

`List(T)` and `HashMap(K, V, ...)` are generic types of exactly this kind. A `HashMap` also takes the hash and equality functions as compile-time parameters, which is how you customize behavior for a data structure without any trait mechanism:

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

case_insensitive_demo :: fn() -> AllocError!() needs Alloc {
    names := hash_map_with(K: string, u32, hash: fold_hash, eq: fold_eq)
    defer names.deinit()
    try names.put("Ada", 1)
    return ()
}
```

A function passed as a type parameter of a generic type must be a *named* function with an empty effect set. It is baked into that instantiation, so calls through it are direct.

## Generic effects

Effects can take parameters, as in `Yield(T)`, and an effect set can mention a function's type parameters:

```gamma
repeat_each :: fn(T: type, xs: []const T, times: u32) needs Yield(T) {
    for _ in 0..times {
        for x in xs { yield(x) }
    }
}
```

The outer loop handles the `Yield(u32)` of `0..times`, but only for that source call: a `yield` in a loop's body always goes to the handlers outside the loop, so `yield(x)` reaches the caller's `Yield(T)` handler.

Because generic bodies are checked once, which handler a `yield` reaches must not depend on what `T` turns out to be. The rule: two instantiations of the same effect that meet (in one effect set, or one performed inside a `with` handling the other) must be identical or *provably* different. `Yield(i32)` and `Yield(u8)` are provably different; `Yield(T)` and `Yield(u32)` are not, since `T` might be `u32`. So this is a compile error:

```gamma
bad_sum :: fn(T: type, xs: []const T) -> u32 needs Yield(T) {
    total := 0u32
    with (handler Yield(u32) { yield(v) => { total += v } }) {
        for x in xs { yield(x) }     // error: the `with` handles Yield(u32), and T might be u32
    }
    return total
}
```

### Effect-set parameters

A parameter declared `E: effects` stands for a whole set of effects. Written as a term of an effect set, it adds all of those effects, so a function can pass on whatever its callback performs:

```gamma
apply :: fn(E: effects, f: ^fn(x: i32) -> i32 needs E, x: i32) -> i32 needs E {
    return f(x)
}
```

`E` is inferable here (the type of `f` mentions it), so, like a type parameter, it is never positional. It is usually inferred from the argument (`apply(noisy, 4)` with `noisy :: fn(x: i32) -> i32 needs Out` gives `E = Out`, so that call performs `Out`). To give it, write it by name, as an effect set without `needs` (`apply(E: Out + Alloc, noisy, 4)`) or a single effect (`apply(E: Out, noisy, 4)`), and an `E` that is neither given nor inferred is empty. Generic types, effects and handlers take such parameters too, which is what lets a *stored* function pointer be polymorphic in its effects:

```gamma
Holder :: struct(E: effects) { f: ^fn(x: i32) -> i32 needs E }

run_holder :: fn(E: effects, h: Holder(E), x: i32) -> i32 needs E => h.f(x)
```

`std/fiber`'s `Sched(E)` is the main example: it lets fiber tasks perform the effects `E`, handled outside the scheduler.

A helper that tasks call, and that performs `Sched(E)` itself, takes `E` from its caller. An effect-set parameter that occurs in an effect of the function's own set is inferable, and the instantiation of that effect available at the call determines it, the way an operation call picks its instantiation:

```gamma
fiber :: import "std/fiber"

pause_twice :: fn(E: effects) needs fiber.Sched(E) {
    fiber.yield_now()
    fiber.yield_now()
}

task :: fn(n: ^u32) needs fiber.Sched(Out) + Out {
    pause_twice()                      // E = Out, from this function's `Sched(Out)`
    print("{}\n", n^)
}
```

This is how `std/fiber/channel`'s `send` and `recv` work in any task, whatever its scheduler's effects.

When the same group of effects is written in many signatures, give it a name. A top-level binding whose value is an effect set is a **named effect set**, used in other sets like a parameter, and it can be the argument of an effect-set parameter:

```gamma
Io :: Fs + Alloc + Err

report :: fn(msg: string) needs Io + Clock {
    // may perform Fs, Alloc, Err and Clock
}

run_holder_io :: fn(h: Holder(Io), x: i32) -> i32 needs Io => h.f(x)
```

A named set is only a compile-time name for a list of effects: it is not a type or a value, and it cannot mention an effect-set parameter.

The same once-checked rule applies: performing `E` inside a `with` that handles `Out` is an error, because `E` might contain `Out`. `E` is not a type or a value, so it cannot appear outside an effect set.

## Restrictions

- A `when` condition may not depend on a type parameter or compile-time value parameter of an enclosing generic function ([chapter 11](11-compile-time.md)). Use separate functions.
- A generic function is not a value by itself, but one instantiation is: give every compile-time argument and leave out the run-time ones (`p: ^fn(s: string) -> ?u8 = parse_int(u8)`, or `q: ^fn(x: u8) -> u8 = double(T: u8)` where `T` is inferable and so written by name), or assign the bare name where a function pointer type is expected, and the type arguments are taken from that type (`r: ^fn(x: u8) -> u8 = double` for `double :: fn(T: type, x: T) -> T`). There is no other partial application of type arguments; write a non-generic wrapper: `largest_i32 :: fn(xs: []const i32) -> ?i32 => largest(xs)`.
- Type arguments must be first-class, run-time types. A second-class `fn(...)` type cannot be a type argument, so you cannot store a closure in `Box(fn() -> i32)`.
- Recursion between instantiations must terminate: an instantiation that keeps requiring a strictly larger type argument is a compile error after an implementation-defined depth.

## Why there are no traits

If you are coming from Rust, C++ or Go, you probably expect user-defined traits, concepts or interfaces. **Gamma has none**, by design. A trait system bundles five jobs. Four already have a direct, visible mechanism in Gamma. The fifth, *implicit, compositional resolution of implementations*, is what Gamma omits, because its rules (coherence, overlap, associated types, effect sets on trait methods) are the largest and least predictable part of the languages that have them, and because you could no longer tell from `m.put(k, v)` which code runs. Appendix C.4 of the specification makes the argument in full. Here is what to do instead (§16.9):

| Need | Gamma idiom |
| --- | --- |
| Bound a generic body to operations on `T` | a closed constraint (`where Ord(T)`) or a function parameter |
| An algorithm that needs per-call behavior | a second-class function parameter: `xs.sort_by(fn(a, b) => a.score > b.score)` |
| Order structs | order a tuple of keys: `people.sort_by_key(fn(p) => (p.last, p.first))` |
| Hash or compare keys | nothing to write: `@hash` and `is` are structural; custom ones are `HashMap` parameters |
| A data structure with fixed, consistent behavior | a compile-time parameter of the type |
| Print or log any value | `print("{}", x)` formats every type structurally; custom display is a function returning a string |
| Dispatch over a closed set of cases at run time | a tagged union and `match` |
| Dispatch over an open set (plugins, drivers) | a struct holding a data pointer and a `^const` vtable of `^fn`s, as `Allocator` does |
| An implementation supplied by the caller's context | an effect and a handler ([chapter 7](07-effects.md)) |
| Generic arithmetic over your own number types | pass the operations as a constant struct |
| Methods on a type | functions in its module, called with method syntax |
| Extend another module's type | a free function |

The "constant struct of operations" idiom deserves an example, since it is the closest thing to a typeclass:

```gamma
Ring :: struct(T: type) {
    zero: T
    add, mul: ^fn(a, b: T) -> T
}

add_sat :: fn(a, b: i32) -> i32 => a +| b
mul_sat :: fn(a, b: i32) -> i32 => a *| b

SAT_RING :: Ring(i32)(zero: 0, add: add_sat, mul: mul_sat)

dot :: fn(T: type, ring: Ring(T), xs, ys: []const T) -> T {
    acc := ring.zero
    for i in 0..xs.len {
        acc = ring.add(acc, ring.mul(xs[i], ys[i]))
    }
    return acc
}

use_dot :: fn(xs, ys: []const i32) -> i32 => dot(SAT_RING, xs, ys)
```

The cost is one explicit argument. In exchange, every call tells you exactly which `add` runs. By the same reasoning, Gamma has no operator overloading: an operator never calls user code, so `a + b` always costs what it looks like it costs. Vectors (`#simd [N]T`) are the one place where an operator works on many values, and the language, not user code, defines how.

## Summary

- A generic function or type has `type` or `::` parameters; it is checked once and monomorphized.
- Type arguments are inferred from value arguments, exactly; literals do not determine them. An inferable parameter is never positional: infer it, or write it by name (`largest(T: i32, xs)`).
- Constraints (`Eq`, `Ord`, `Num`, `Int`, `Signed`, `Float`, `Plain`) are a closed set; for anything else pass a function or a constant struct.
- Generic types take compile-time parameters, including function pointers, which is how maps customize hashing.
- There are no traits; the table above lists what to use instead.

Next, [chapter 11](11-compile-time.md) covers compile-time evaluation.

For the full rules, see [§16 Generics][s16] of the specification, and Appendix C.4 for the rationale.

[s16]: ../../gamma-spec.md#16-generics
