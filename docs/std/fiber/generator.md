# Module `std/fiber/generator`

[All modules](../index.md)

Pull-style generators: advance iterators by hand, in lockstep.

An iterator is a function that performs `Yield(T)` (§13). `for` drives it from the
consumer's side, so two iterators cannot be advanced alternately. A `Generator` runs an
iterator on a fiber of its own and hands out its values one at a time with `next()`:

    gen :: import "std/fiber/generator"

    count_to :: fn(limit: ^u32) needs Yield(u32) {
        for i in 1..=limit^ { yield(i) }
    }

    dot :: fn(a, b: ^gen.Generator(u32)) -> u32 needs gen.Pull(u32) {
        total: u32 = 0
        while x := a.next() {               // null once the iterator has returned
            y := b.next() else break
            total += x * y
        }
        return total
    }

    n := u32(5)
    m := u32(7)
    ga := gen.generator(count_to, &n, stack_a)      // nothing runs yet
    gb := gen.generator(count_to, &m, stack_b)
    total := gen.pull(main_stack, fn() => dot(&ga, &gb))

#### Shape

Fibers are structured (§14), so the code that calls `next` runs inside a `with … on`
that the library installs: `pull` runs its `body` argument, the consumer, on the stack
it is given, with a handler for the effect `Pull(T, E)`. `next(g)` performs `Pull`, which
starts the generator's producer on its own fiber the first time and resumes it later;
the producer's `yield`s become values returned by `next`. Any number of generators of
the same `T` and `E` may be used in one `pull`: that is how iterators are advanced in
lockstep.

`T` is the item type and `E` the other effects the producers perform (`Out`, `Alloc`, …),
which the code around `pull` must handle: `pull` itself `needs E`. A producer is a
function pointer `^fn(arg: ^A) needs Yield(T) + E` with its argument `arg: ^A`, not a
closure, because a fiber may outlive the frame that creates it (§14.6).

#### Stacks

Nothing allocates: `pull` takes the consumer's stack and each generator the stack of its
producer, all of which must outlive the `pull` and be at least `@min_stack` bytes (a producer
that does real work needs tens of kilobytes; `@stack_size(f)` gives a bound for a producer
`f`). A generator's stack is used from its first `next` until its producer has returned
or the `pull` ends.

#### Ending a `pull`

A producer that has not returned when the body finishes is cancelled: its `defer`s run
(§14.5), so a generator may be abandoned early, for example when two trees differ at
their first leaf. An exit that leaves the body (an `esc` operation handled outside, or
`return`/`break` when the scope is written with `driver` and `with`, below) cancels them
too (§14.4).

A generator belongs to the `pull` that first called its `next`. Using it in another one
while the first is running, or after the first has ended, if its producer had not
returned by then, traps. A generator that has not been started, or whose producer has
returned, can be used anywhere: it starts afresh, or gives `null`.

#### Writing the scope yourself

`pull` is `driver` and `with` combined. To `return` or `break` out of the consumer's code,
install the driver yourself and call `finish()` at the end of the body:

    d := gen.driver(u32, Out)
    defer d.close()
    with &d on main_stack {
        if x := ga.next() { return x }          // cancels the generators still suspended
        gen.finish()
    }

## Structs

### `Generator`

```gamma
Generator :: struct(T: type, E: effects) {
    // private fields omitted
}
```

A producer of `T`s that the consumer pulls values from with `next`. `E` is what the producer
may perform besides `Yield(T)`. Create one with `generator`.

### `Driver`

```gamma
Driver :: struct(T: type, E: effects) {
    // private fields omitted
}
```

The scope of a `pull`: the handler of `Pull(T, E)`. Use `pull`, or `driver` with `with`.

## Effects

### `Pull`

```gamma
Pull :: effect(T: type, E: effects) {
    next_raw: ctl(g: ^Generator(T, E)) -> ?T
    give: ctl(g: ^Generator(T, E), x: T)
    done: ctl(g: ^Generator(T, E)) -> never
    finish: ctl()
}
```

What generators ask of their scope. The operations are internal: `next` and `finish` are
the ones to call.

Operations:

- `next_raw`: The consumer wants the next value of `g`: starts or resumes its producer.
- `give`: The producer of `g` yields `x`.
- `done`: The producer of `g` has returned.
- `finish`: Cancels every producer still suspended in this scope.

## Handlers

### `handler(T: type, E: effects) Pull(T, E) for ^Driver(T, E)`

```gamma
handler(T: type, E: effects) Pull(T, E) for ^Driver(T, E) needs E
```

## Functions

### `generator`

```gamma
generator :: fn(T: type, A: type, E: effects, f: ^fn(arg: ^A) needs Yield(T) + E, arg: ^A, stack: []u8) -> Generator(T, E)
```

A generator that runs `f(arg)` as its producer on `stack`. Nothing starts until the first
`next`. `T` and `E` come from the effects of `f`.

### `next`

```gamma
next :: fn(T: type, E: effects, g: ^Generator(T, E)) -> ?T needs Pull(T, E)
```

The next value of `g`, running its producer until it yields, or `null` once the producer has
returned (and on every call after that). Traps if `g` belongs to another scope (see the
module header).

### `driver`

```gamma
driver :: fn(T: type, E: effects) -> Driver(T, E)
```

A scope with no generators yet, for use as `with &d on stack { … }` (see the module
header). Call `d.close()` after the `with`, and `finish()` at the end of its body.

### `close`

```gamma
close :: fn(T: type, E: effects, d: ^Driver(T, E))
```

Marks the generators of the scope whose producers have not returned as closed: using one
later traps. Call it after the `with` that installed `d` (`pull` does).

### `pull`

```gamma
pull :: fn(T: type, E: effects, R: type, stack: []u8, body: fn() -> R needs Pull(T, E)) -> R needs E
```

Runs `body`, the consumer, on `stack` with a scope for generators of `T`: inside it,
`next` can be called on any generator. When `body` returns, the producers still
suspended are cancelled. Returns the value of `body`.
