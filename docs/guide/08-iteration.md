# 8. Iteration

[Contents](README.md) · [Previous: Effects](07-effects.md) · [Next: Memory](09-memory.md)

C iterates with index loops, or with callbacks (`qsort`, `bsearch`, "visitor" APIs) when the container's structure is not an array. Gamma has one mechanism for both: an **iterator** is a function that produces values by performing the `Yield` effect, and a **`for` loop** is the handler. There is no iterator protocol, no `next` method to implement, and no hidden allocation.

## `Yield` and `for`

The standard library predeclares one generic effect:

```gamma
Yield :: effect(T: type) { yield: esc(x: T) }
```

It has a single `esc` operation, so a handler can resume the producer (let it continue with its next value) or stop it. Any function whose effect set contains `Yield(T)` is an iterator:

```gamma
countdown :: fn(start: u32) \ {Yield(u32)} {
    n := start
    while n > 0 {
        yield(n)
        n -= 1
    }
}

main :: fn() \ {Out} {
    for n in countdown(3) {
        print("{}\n", n)          // 3, 2, 1
    }
}
```

`for p in e { B }` runs `B` once per yielded value, binding the value to the pattern `p`. It is defined as installing a handler for `Yield` whose clause runs `B`:

- Reaching the end of `B`, or `continue`, returns from the clause: the `yield` returns and the iterator carries on with its next value.
- `break` exits the loop. Because `yield` is an `esc` operation, this *unwinds* the iterator's frames, so its `defer` statements run, and execution continues after the loop.
- `return`, a labeled `break`/`continue`, and `try` inside `B` work as you would expect and also unwind the iterator on the way out.

That is all there is to it. The language gives `for` three source forms: a call to a function with exactly one `Yield(T)` in its effect set, a range `lo..hi` or `lo..=hi`, and an array place or slice (which means `each(...)` of it). Anything else, such as `for x in some_struct`, is a compile error.

## Writing iterators

Writing an iterator is just writing the loop you want and calling `yield`. Compare this with a callback-based tokenizer in C, which has to package its state into a `void *` or invert its control flow:

```gamma
words :: fn(s: string) \ {Yield(string)} {
    start := 0
    for i in 0..s.len {
        if s[i] is ' ' {
            if i > start { yield(s[start..i]) }
            start = i + 1
        }
    }
    if s.len > start { yield(s[start..]) }
}

words_demo :: fn() \ {Out} {
    for w in words("the quick  brown fox") {
        print("[{}]\n", w)           // [the] [quick] [brown] [fox]
    }
}
```

Observe that `words` contains a `for` loop of its own (over a range), and `yield(string)` is performed inside it. There is no confusion about which loop receives what: the inner `for` handles only the `Yield(usize)` that the range produces, and a `yield(...)` in its body goes to the handler *outside* the loop, which is `words`'s caller. The rule is general: effects performed in a loop body are handled by the context outside the loop. Nested loops over iterators with the same `Yield(T)` therefore work.

An iterator can wrap another by looping over it:

```gamma
evens :: fn(xs: []const i32) \ {Yield(i32)} {
    for x in xs {
        if x % 2 is 0 { yield(x) }       // goes to evens's caller, not to the loop above
    }
}
```

An iterator can also have other effects. Callers must handle those too:

```gamma
numbered :: fn(n: u32) \ {Yield(u32), Out} {
    defer print("iterator cleaned up\n")
    for i in 1..=n { yield(i) }
}

early_exit_demo :: fn() \ {Out} {
    for i in numbered(10) {
        if i > 3 { break }               // stops the iterator, which runs its defer
    }
}
```

The `Out` that `numbered` performs becomes a requirement of `early_exit_demo`, and the `defer` runs when `break` unwinds `numbered`.

> **Common mistake: yielding the wrong type.** `Yield(u32)` and `Yield(u8)` are different effects. A function that yields a `u32` must declare `Yield(u32)`. A function may declare several, for example `\ {Yield(i32), Yield(u8)}`, but then it cannot be the source of a `for` loop, which requires exactly one `Yield(T)` in the callee's effect set.

## Loops with values and failing iterators

A `for` loop is an expression. Its type is the **return type of the iterator** (`()` for ranges and arrays). If the iterator returns normally, that is the loop's value; `break v` exits with `v`. Plain `break` is allowed only when the return type is `()`.

This is how iterators report failure. An iterator returns a result, and the surrounding code must deal with it, because a non-`()` value cannot be silently dropped:

```gamma
Overflow :: union { TooMany }

limited :: fn(xs: []const u32, cap: usize) -> Overflow!() \ {Yield(u32)} {
    if xs.len > cap { return !.TooMany }
    for x in xs { yield(x) }
    return ()
}

total_of :: fn(xs: []const u32) -> Overflow!u32 {
    sum := 0u32
    try for x in limited(xs, 8) {
        if x is 0 { break () }           // stop early; the loop's value is a success
        sum += x
    }
    return sum
}
```

`try for ...` propagates the iterator's error out of `total_of`. Without the `try`, the loop's value of type `Overflow!()` would be an unused result, which is an error.

## Standard iterators

The core library provides these iterators (§21.4). All are `#inline`.

| Iterator | Yields |
| --- | --- |
| `range(lo, hi)`, `range_incl(lo, hi)` | integers (this is what `lo..hi` and `lo..=hi` mean) |
| `each(xs)` | the elements of `xs` by value (what `for x in xs` means) |
| `each_ptr(xs)` | `xs: []T` gives `^T`, so you can modify elements |
| `enumerate(xs)` | `(i: usize, x: T)` pairs |
| `reversed(xs)` | the elements, last to first |
| `bytes(s)`, `runes(s)` | `u8` or `rune` from a string |

```gamma
std_demo :: fn(names: []const string, scores: []u32) \ {Out} {
    for i, name in enumerate(names) {
        print("{}: {}\n", (i, name))
    }
    for name in reversed(names) {
        print("{}\n", name)
    }
    for p in each_ptr(scores) { p^ = p^ +| 10 }      // saturating add to every score
}
```

A comma-separated list of patterns in a `for` head is a tuple pattern without parentheses, so `i, name` takes apart the `(i, x)` pairs of `enumerate`. The `for` pattern must be irrefutable and may not reuse the name of a local in scope.

`return` inside a loop body leaves the whole function, unwinding the iterator:

```gamma
index_of_word :: fn(text, target: string) -> ?usize {
    i := 0
    for w in words(text) {
        if w is target { return i }
        i += 1
    }
    return null
}
```

## Lockstep iteration: cursors

Internal iteration cannot advance two iterators at once: a `for` loop is in control of one iterator and the other cannot be interleaved without a second stack. For the common case of walking two sequences together, arrays and slices provide a **cursor**, a value with a `next` method that returns `?T` (or `?^T`) and works with `while` binding conditions:

```gamma
dot :: fn(a, b: []const i32) -> i32 {
    total := 0
    ca := a.cursor()
    cb := b.cursor()
    while x := ca.next() {
        y := cb.next() else break         // stop when the shorter one runs out
        total += x * y
    }
    return total
}
```

Cursors are plain values with no effects. For general pull-style iteration over any iterator, you can run the iterator on a fiber ([chapter 12](12-fibers.md)). A library type could package that as a generator; the core library does not.

## Why this design

- **One mechanism.** Ranges, arrays and user iterators all go through `for`. There are no iterator objects, adapters or traits to learn.
- **No allocation.** The iterator's state lives in its own stack frame. `for` allocates nothing.
- **Early exit is correct.** Leaving a loop runs the iterator's cleanup, because `yield` is an `esc` operation and a `break` is an exit that unwinds.
- **Fast.** When the compiler can see the iterator's body (and the standard ones are `#inline`), the handler is statically known and its clause is inlined. A `for i in 0..n` or `for x in xs` compiles to the same counted loop you would write by hand, with bounds checks that can be removed (§13.5 and Appendix D of the [specification](../../gamma-spec.md) describe the strategy; it is non-normative).

## Summary

- An iterator is any function with `Yield(T)` in its effect set; it produces values with `yield(x)`.
- `for p in e { B }` handles `Yield(T)`; `break`, `continue`, `return` and `try` work through the iterator.
- A `for` has the iterator's return type as its value, so failing iterators cannot be ignored.
- Use cursors with `while` to advance two sequences in step.

Next, [chapter 9](09-memory.md) looks at memory and allocation.

For the full rules, see [§13 Iteration][s13] of the specification.

[s13]: ../../gamma-spec.md#13-iteration
