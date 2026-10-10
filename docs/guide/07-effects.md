# 7. Effects

[Contents](README.md) · [Previous: Functions](06-functions.md) · [Next: Iteration](08-iteration.md)

This is Gamma's distinguishing feature, so this chapter goes slowly. It explains what effects are, how to declare and handle your own, the three kinds of operation, and how the built-in `Out`, `Alloc` and friends fit in. Later chapters (iteration, memory, fibers) are all applications of what you learn here.

## The idea

A C library that needs to allocate takes a custom allocator through a global or a struct field. One that logs takes a callback and a `void *`. One that can bail out in the middle of a deep call chain uses `setjmp` or threads an error code through every frame. One that iterates takes a visitor callback. Each time, the *policy* (which allocator, where logs go, what to do on failure) is supplied by the caller's context, but nothing in the function's type says so.

An **effect** is an interface whose implementation is supplied by the caller's context instead of by an argument. A function lists the effects it performs after a backslash. Somewhere up the call chain, a `with` block installs a **handler** for each. Call sites look like ordinary calls. The compiler checks that every effect is handled, so there is no "unhandled" runtime condition.

## Declaring and performing an effect

An effect is a list of **operations**, each with a name, a kind, parameters and a result type:

```gamma
Level :: union { Debug, Info, Warn, Error }

Log :: effect {
    log: fn(level: Level, msg: string)
}
```

Declaring `Log` also declares `log` as a function. Calling it **performs** the operation, and its effect set is `{Log}`. Any function that performs `log`, directly or through a call, must list `Log`:

```gamma
connect :: fn(host: string) -> bool \ {Log} {
    log(.Info, "connecting")
    if host.len is 0 {
        log(.Error, "empty host name")
        return false
    }
    log(.Debug, "connected")
    return true
}
```

This is what `\ {Out}` on `main` has meant all along: `print` performs the `Out` effect, so callers must provide it. The compiler checks that the set a function body performs is a subset of the set it declares.

> **Common mistake: a missing effect.** Calling a function with effects from one that does not declare or handle them is a compile error:
>
> ```gamma
> helper :: fn() {
>     print("hi\n")          // error: performs Out, which `helper` neither lists nor handles
> }
> ```
>
> Add `\ {Out}` to `helper`, or install a handler for `Out` around the call.

An effect declared `pub` in another module is named through the module, like its types and functions: `\ {log.Log}` in an effect set, `handler log.Log { ... }` for a handler, and `log.log(...)` to perform its operation.

## Handling an effect

A **handler literal** supplies one *clause* for each operation. A `with` expression installs handlers around a block:

```gamma
main :: fn() \ {Out} {
    printer :: handler Log {
        log(level, msg) => print("[{}] {}\n", (level, msg))
    }
    ok := with printer { connect("example.org") }
    print("ok = {}\n", ok)
}
```

This prints:

```text
[.Info] connecting
[.Debug] connected
ok = true
```

Things to notice:

- Clause parameters are bare names; their types come from the operation.
- `with h { B }` is an expression whose value is the value of `B` (here a `bool`).
- Inside `B`, every perform of `log` goes to `printer`. Outside the `with`, it does not.
- The clause itself performs `Out` (through `print`). Effects performed by a clause are handled by the handlers in scope **where the literal is written**, here `main`'s own `Out`. A clause never handles its own performs, which is what makes nested handlers for the same effect behave sensibly.

A second handler for the same effect gives different behavior to the same code. Nothing in `connect` changed:

```gamma
quiet_connect :: fn(host: string) -> bool {
    silent :: handler Log {
        log(l, m) => {}
    }
    return with silent { connect(host) }
}
```

`quiet_connect` has no effect set, because `connect`'s only effect is handled inside it.

Handler literals can capture local variables by reference and are second-class like function literals ([chapter 6](06-functions.md)): they can be named with `::` and used in `with`, but not stored or returned. A handler literal written directly in the `with` head must be parenthesized, because the first `{` after the head begins the body.

### Several handlers, and their order

`with a, b { B }` means `with a { with b { B } }`. A handler may be backed by effects that an *earlier* handler in the list supplies, never the reverse, so list providers first. Installing two handlers for the same effect in one `with` is a compile error.

## The three kinds of operation

The kind says what a handler clause is allowed to do, and therefore what the caller of the operation must be prepared for:

| Kind | A clause may | The performer |
| --- | --- | --- |
| `fn` | only return a value | always continues, with that value |
| `esc` | return a value, or **exit** the `with` | continues, or is unwound and never continues |
| `ctl` | capture the continuation and switch stacks | is suspended, and may continue later or be cancelled |

`fn` operations behave like calling a function that was found in the context. `esc` operations are early exit. `ctl` operations are coroutines and are the subject of [chapter 12](12-fibers.md). Because the kind is part of the declaration, reading an effect tells you the worst case: a function whose effects contain only `fn` operations always returns to its caller, and its frames are never unwound.

### `fn`: ask the context for something

```gamma
Ids :: effect {
    next_id: fn() -> u32
}

ids_demo :: fn() \ {Out} {
    counter := 100u32
    ids :: handler Ids {
        next_id() => {
            counter += 1
            counter
        }
    }
    with ids {
        a := next_id()
        b := next_id()
        print("{} {}\n", (a, b))           // 101 102
    }
}
```

A clause body is an expression or a block. A block's value is its last expression, and that is the value the perform returns. Statements such as assignments are not expressions, so a clause that needs them uses a block, as here.

### `esc`: exit early

An `esc` clause can `break` out of the `with` that installed the handler. This is Gamma's replacement for exceptions and `setjmp`/`longjmp`, and unlike them it runs cleanup code and is visible in types:

```gamma
Fail :: effect {
    fail: esc(msg: string) -> never
}

digit :: fn(c: u8) -> u32 \ {Fail} {
    if c not in '0'..='9' { fail("not a digit") }
    return u32(c - '0')
}

sum_digits :: fn(s: string) -> ?u32 {
    total := 0u32
    ok := true
    stop :: handler Fail {
        fail(msg) => {
            ok = false
            break                          // exit the `with` below
        }
    }
    with stop {
        for c in bytes(s) { total += digit(c) }
    }
    if ok { return total }
    return null
}

fail_demo :: fn() \ {Out} {
    print("{}\n", sum_digits("1234"))      // 10
    print("{}\n", sum_digits("12x4"))      // null
}
```

The operation `fail` is declared `-> never`, because a handler for it can only exit, so the code after a `fail(...)` call is unreachable. When the clause executes `break`, every frame between the perform and the `with` is **unwound**: innermost first, each frame's pending `defer` and `errdefer` statements run (the same way as for an early return), and control continues after the `with`. This is plain code running; there are no unwinding tables.

`esc` clauses can also return a value normally, in which case the perform returns it and the performer continues. What a clause may do is summarized here (§12.7.1 has the exact table):

| The clause ends by | `fn` | `esc` | `ctl` |
| --- | --- | --- | --- |
| producing a value (falling off the end) | the perform returns it | the perform returns it | not allowed |
| `break` (unlabeled, at clause level) | not allowed | exit the installing `with` | exit the `with` |
| `break L`, `continue L`, `return` (handler literals only) | not allowed | exit to that target | likewise |
| `resume k(v)` | no | no | continue the suspended code ([chapter 12](12-fibers.md)) |

"At clause level" means not inside a loop within the clause; inside one, `break` refers to that loop as usual. A `return` in a handler literal's clause returns from the function that contains the literal, and `try` behaves the same way.

`break` can carry a value, because `with` is an expression: `x := with h { ... }` takes either the block's value or the value of a `break v`. The two must have the same type.

## Handlers for types: `handler ... for`

A handler literal is convenient but anonymous and local. When a *type* naturally implements an effect, declare it once with a **handler declaration**, and then use values of that type directly in `with`:

```gamma
CountingLog :: struct {
    warnings: u32 = 0
    errors: u32 = 0
}

handler Log for ^CountingLog {
    log(self, level, msg) => match level {
        .Warn  => { self.warnings += 1 }
        .Error => { self.errors += 1 }
        _      => {}
    }
}

work :: fn() \ {Log} {
    log(.Info, "starting")
    log(.Warn, "disk almost full")
    log(.Error, "write failed")
}

stats_demo :: fn() \ {Out} {
    stats := CountingLog()
    with &stats { work() }
    print("{} warnings, {} errors\n", (stats.warnings, stats.errors))
}
```

The first clause parameter (`self`) has the handler's type; the rest are the operation's parameters. A handler declaration must appear in the module that declares the effect or the one that declares the type. A declaration may add `\ {A}` after the type to say which effects its clauses perform (those are handled *outside* the `with` that installs it). This is how the allocator types in [chapter 9](09-memory.md) work, and it is also how you give a struct a handler without writing a handler literal at every use. Clauses of a declaration have no enclosing function, so `return` and captures are not available there, but an unlabeled `break` still exits the `with`.

A generic type gets one declaration for all its instances by listing the parameters after `handler`, written as for a generic struct:

```gamma
Collector :: struct(T: type, N: usize) { items: [N]T, len: usize = 0 }

handler(T: type, N: usize) Yield(T) for ^Collector(T, N) {
    yield(self, x) => if self.len < N { self.items[self.len] = x; self.len += 1 }
}

collect_demo :: fn() \ {Out} {
    c := Collector(u32, 4)(items: [4]u32(fill: 0))
    with &c { for i in 0..10 { yield(u32(i)) } }      // keeps 0, 1, 2 and 3
    print("{}\n", c.len)                              // 4
}
```

## Ambient effects

Some effects are predeclared, and the runtime installs a default handler for each before calling `main`:

| Effect | Operations | Default handler | In tests |
| --- | --- | --- | --- |
| `Alloc` | `current: fn() -> Allocator` | the system heap | leak-checking heap |
| `Out`, `Err` | `write_out` / `write_err` bytes | stdout, stderr | captured |
| `In` | `read` into a buffer | stdin | empty input |
| `Clock` | `now`, `wall`, `sleep` | system clock | fake clock |
| `Rand` | `fill` a buffer | OS-seeded generator | fixed seed |
| `Fs`, `Net` | files, sockets | the real ones | in-memory fakes |
| `Env` | `args`, `var`, `exit` | process environment | empty |
| `Sys` | none | none | not replaceable |

`main` and `test` bodies can perform any of these without handling them. Everything else must either handle them or declare them. Because they are ordinary effects, you can handle any of them yourself, and that is what makes code testable without mocks: the same function runs against the real file system in production and an in-memory one in a test.

Here is a function that runs a callback and counts the bytes it would print, by installing its own handler for `Out` around the callback:

```gamma
count_output :: fn(body: fn() \ {Out}) -> usize {
    total := 0
    counter :: handler Out {
        write_out(chunk) => { total += chunk.len }
    }
    with counter { body() }
    return total
}

measure_demo :: fn() \ {Out} {
    n := count_output(fn() { print("hello\n") })
    print("{} bytes\n", n)                 // 6 bytes
}
```

The hello is swallowed by `counter` and never reaches standard output.

### `Sys`: the edge of the model

`Sys` is an effect with no operations that **cannot be handled**. It marks everything that escapes Gamma's model: calling foreign (C) functions, reading or writing `#global` variables, volatile memory access, inline assembly, creating threads. A function without `Sys` in its effect set cannot observe or modify state outside its arguments, the memory they point to, and its handlers. That is a strong guarantee for reasoning and for testing, and it is also what lets such functions be run by the compiler ([chapter 11](11-compile-time.md)). `main` may list `Sys`. See [chapter 14](14-interop.md).

## Effects and callbacks

A common worry with effect systems is that every higher-order function must be generic over the effects of its callbacks. Gamma avoids that through the second-class `fn` parameters from the previous chapter. The rule: **a callback's effects are handled where the callback was written, except for the ones its parameter type lists, which the callee handles.**

```gamma
repeat :: fn(times: u32, body: fn()) {
    for _ in 0..times { body() }
}

repeat_demo :: fn() \ {Out} {
    repeat(3, fn() => print("hi\n"))       // Out is repeat_demo's requirement, not repeat's
}
```

`repeat` declares no effects, and still accepts a callback that prints, because that `Out` belongs to the context of the call, which is `repeat_demo`. Contrast `count_output` above, whose parameter is `fn() \ {Out}`: by listing `Out` in the parameter type, `count_output` promises to handle it, and it does. A callee that wants to *provide* an effect to its callback lists the effect in the callback's type; a callee that just calls it lists nothing.

This is why Gamma has no effect variables or effect-polymorphism syntax: second-class callbacks give you the same expressiveness with no extra notation. A **function pointer** `^fn(...) \ {E}`, being first-class, has a fixed effect set that is handled by whoever calls it. A pointer or named function whose effects are a subset of `E` can be used where `^fn(...) \ {E}` is expected.

Effects may be generic. `Yield(T)` in the next chapter is a generic effect, and an effect set may mention type parameters of an enclosing generic function (`\ {Yield(T)}`).

## Static guarantees

- Every performed effect is in the enclosing function's declared set or handled by an enclosing `with`, and every caller in turn handles or declares it. There is no runtime "unhandled effect".
- A function whose effects contain no `esc` or `ctl` operation, and which calls no second-class function parameter, always returns to its caller (or traps, or loops forever).
- A function with an empty effect set (and no `Sys`) can be evaluated at compile time.

## Common mistakes

- **Forgetting the effect annotation** on a function that calls `print`, `new`, or a function of yours that performs an effect.
- **Putting a handler literal directly in a `with` head** without parentheses.
- **Missing or extra clauses.** A handler must have exactly one clause for each operation of its effect.
- **Expecting a clause to handle its own performs.** It does not; a clause runs in the context outside its handler.
- **Using statements as a clause body.** `log(l, m) => count += 1` is not valid, because assignment is not an expression; write `=> { count += 1 }`.

## Summary

- An effect is an interface supplied by the caller's context; `\ {E}` lists what a function performs, and `with` installs handlers.
- Operation kinds `fn`, `esc` and `ctl` tell you whether a perform returns, may exit, or may suspend.
- Handlers come from handler literals or from `handler E for T` declarations.
- `Out`, `Alloc`, `Fs` and the other ambient effects are ordinary effects; `Sys` marks what escapes the model.
- Callbacks passed as `fn(...)` carry their own effects with them, so no effect variables are needed.

Next, [chapter 8](08-iteration.md) uses `esc` to explain `for`.

For the full rules, see [§12 Effects and handlers][s12] of the specification.

[s12]: ../../gamma-spec.md#12-effects-and-handlers
