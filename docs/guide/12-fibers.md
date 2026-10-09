# 12. Fibers and continuations

[Contents](README.md) · [Previous: Compile time](11-compile-time.md) · [Next: Safety](13-safety.md)

The `fn` and `esc` operation kinds from [chapter 7](07-effects.md) either return or exit. The third kind, **`ctl`**, lets the handler *suspend* the code that performed the operation and decide later whether and when to continue it. That is the mechanism behind coroutines, generators, async I/O and schedulers, and it is how C programmers who need them end up hand-writing state machines, or reaching for `ucontext` and `makecontext`.

Suspended code needs a stack of its own, so handlers for `ctl` operations are installed together with a stack that **you provide**. Suspension is a stack switch. Nothing is allocated implicitly.

This is the most advanced chapter. If you are writing ordinary programs, you can skip it; the library you use may well do this for you.

## `with ... on`

```text
with handlers on stack { body }
```

If any handler installed by a `with` handles a `ctl` operation, the `with` must have an `on` clause giving the memory for the fiber's stack, and vice versa. `stack` is an expression of type `[]u8`.

- The body runs on a new **fiber** whose stack is that memory. The code that executes the `with`, on whatever stack it was already running on, is the `with`'s **driver**.
- `fn` and `esc` clauses run where they are performed, as usual.
- **`ctl` clauses run on the driver's stack**, as if called from the `with` statement.
- The stack memory must stay valid, and unused by anything else, until the `with` completes. Stacks never grow or move, so pointers into a suspended fiber's stack remain valid.
- The stack must be at least `@min_stack` bytes, and running out of stack is a checked error (a trap in Debug and Checked builds).

## A first example

A `ctl` operation can return a value to the suspended code, supplied by the handler. Here a handler plays the role of an "answerer" for questions asked from deep inside a computation:

```gamma
Ask :: effect {
    ask: ctl(question: string) -> u32
}

quiz :: fn() -> u32 \ {Ask} {
    a := ask("first number?")
    b := ask("second number?")
    return a + b
}

STACK_SIZE :: 16384
@static_assert(STACK_SIZE >= @min_stack, "fiber stack too small")

main :: fn() \ {Out} {
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
    print("{}\n", total)               // 30
}
```

What happens when `quiz` calls `ask(...)`:

1. The fiber running `quiz` is **suspended** at the call.
2. The `ask` clause runs on the driver's stack (`main`'s). Besides the operation's parameter, it receives a **continuation** `k`, a `cont(u32)` because the operation's result type is `u32`.
3. `resume k(n * 10)` transfers control back to the suspended fiber, where the call to `ask` now returns `10`.

A few rules are visible in this small example. A `ctl` clause has type `never`: every path must end in `resume`, an exit (`break`, labeled `break`/`continue`, `return`), a call to a `never` function such as `trap`, or an infinite loop. `resume` is a **transfer**, not a call: it ends the clause (its pending `defer`s run) and the driver switches to the fiber. And the `with` here is an expression whose value is what the body returned.

## Continuations

`cont(R)` is the type of a one-shot continuation that expects an `R`. Unlike second-class functions, continuations are first-class: you can store them in variables, structs and queues. That is what lets a scheduler hold many suspended fibers. The rules that keep this safe:

1. **One-shot.** Each suspension produces a continuation that must be consumed exactly once, by `resume` or `discard`. Consuming one twice, through any copy, is a checked error.
2. **Bounded by its `with`.** Every continuation produced under a `with` must be consumed before the `with` completes.
3. **Same `with`.** A continuation may only be consumed by code running in a `ctl` clause of a handler installed by the `with` that produced it.
4. **Same thread.** It may only be consumed on the thread that created it.

If a clause uses `k` only as the operand of `resume` or `discard`, never storing, copying or passing it, the compiler verifies statically that every path consumes it exactly once, and no run-time check is needed for it. The checks exist for the cases where you do store it, as a scheduler does.

### Cancelling with `discard`

`discard k` cancels the suspended fiber. Control switches to it, and its frames unwind from the point of suspension to the base of the fiber, running their `defer` and `errdefer` statements, exactly like an `esc` exit. Then control returns to the clause. This is how a handler abandons work without leaking whatever the work had acquired:

```gamma
Budget :: effect {
    tick: ctl()
}

job :: fn() \ {Budget, Out} {
    defer print("job: cleanup\n")
    step: u32 = 1
    while step <= 5 {
        print("job: step {}\n", step)
        tick()
        step += 1
    }
    print("job: finished\n")
}

run_limited :: fn(limit: u32) \ {Out} {
    stack: [STACK_SIZE]u8 = ---
    seen: u32 = 0
    meter :: handler Budget {
        tick(k) => {
            seen += 1
            if seen > limit {
                discard k                 // unwind the job, running its defers
                break                     // then leave the `with`
            }
            resume k()
        }
    }
    with meter on stack[..] { job() }
}
```

`run_limited(2)` prints steps 1, 2 and 3 and then `job: cleanup`: the third `tick` exceeds the budget, `discard` runs the job's `defer`, and `break` ends the `with`. The job never prints "finished". Note that `tick(k)` has the continuation as its only parameter because the operation has none of its own; a `ctl` clause always gets one extra, final parameter for `k`.

## How a `with ... on` ends

A `with ... on` completes in one of two ways:

- **The body finishes** on its fiber with a value. Control returns to the driver and the `with`'s value is that value.
- **A `ctl` clause exits** with `break` (or a labeled `break`/`continue` or `return`, in a handler literal).

In both cases, every continuation produced under the `with` must already have been consumed, or the program has violated rule 2: Debug and Checked builds trap with a message like "with completed while N fibers are suspended". To shut down cleanly, `discard` the suspended fibers first.

A third way is an exit that *crosses* the `with`: code on one of its fibers performs an `esc` operation whose target is outside the `with`. The fiber's frames are unwound, every other unconsumed continuation of the `with` is discarded (most recently created first), and then the exit continues. So exits never leave suspended fibers behind.

## Creating fibers: `@fiber`

A single fiber is enough for generators and cooperative sub-tasks. A scheduler needs to start *new* fibers. The intrinsic `@fiber(f, arg, stack)` does that. It may be used only inside a `ctl` clause, and it makes a new fiber belonging to the clause's `with`:

```text
@fiber(f, arg, stack) -> cont(())
```

`f` is a **function pointer** `^fn(a: A) -> never \ {S}`, `arg` is its argument, and `stack` is the memory for the new fiber. The result is a continuation which, when resumed, starts the call `f(arg)` on that stack. `A` must be a first-class type; a second-class closure cannot be smuggled into a fiber that may outlive the frame that made it, which is why fibers start from a function pointer plus a data argument, as threads do in C. `f` returns `never`: a fiber ends by performing a `ctl` operation whose clause discards it (a library `spawn` wraps the user's function to do that). The new fiber runs in the handler context of the `with`'s body, so it can perform the effects that the `with` handles.

## A scheduler sketch

The specification's own example is a cooperative round-robin scheduler. This is a trimmed version of it, as a sketch. `Queue`, `new_scheduler`, `next` and `spawn_task` are **library code that you would write**; they are not defined by the core library. Assume a `Queue(T)` that has a `push` method and needs no effects (for example a fixed-capacity ring buffer), and that `next` removes and returns a ready continuation.

```gamma
Sched :: effect {
    yield_now: ctl()
    spawn: ctl(entry: ^fn(arg: rawptr) -> never \ {Sched}, arg: rawptr, stack: []u8)
    exit:  ctl() -> never
    join:  ctl()                       // wait until every spawned task has exited
}

Scheduler :: struct {
    ready:  Queue(cont(()))
    joiner: ?cont(())
    live:   usize
}

handler Sched for ^Scheduler {
    yield_now(self, k) => {
        self.ready.push(k)             // put myself at the back of the queue
        r := self.next()               // run whoever is at the front
        resume r()
    }
    spawn(self, entry, arg, stack, k) => {
        self.ready.push(@fiber(entry, arg, stack))
        self.live += 1
        resume k()
    }
    exit(self, k) => {
        discard k                      // this task is over: unwind it
        self.live -= 1
        if self.live is 0 {
            if j := self.joiner {
                self.joiner = null
                resume j()
            }
        }
        r := self.next()
        resume r()
    }
    join(self, k) => {
        if self.live is 0 { resume k() }
        self.joiner = k
        r := self.next()
        resume r()
    }
}
```

Several points of the design show up here:

- The continuations are stored in a queue inside the scheduler, which is why `cont(())` is first-class.
- `resume r()` is written with `r` bound to a name first. `resume k(v)` takes the continuation from a postfix expression *without its final call*, so you cannot write `resume self.next()()`.
- Every clause ends in `resume`, and since `resume` transfers rather than calls, the scheduler can switch between any number of fibers **without its own stack growing**.
- `exit` uses `discard` on the exiting task's continuation, because a fiber ends by being cancelled; this runs the task's `defer`s.

A program using it hands the scheduler a main stack, spawns tasks, and `join`s them. Because the `with` cannot complete while a task is suspended, `join` is what keeps it alive:

```gamma
Counter :: struct { ticks: u32 = 0 }

worker :: fn(c: ^Counter) \ {Sched} {
    c.ticks += 1
    yield_now()                        // let the other task run
    c.ticks += 1
}

run_two :: fn(main_stack, stack_a, stack_b: []u8) -> u32 {
    sched := new_scheduler()
    a := Counter()
    b := Counter()
    with &sched on main_stack {
        spawn_task(worker, &a, stack_a)       // library wrapper around `spawn` and `exit`
        spawn_task(worker, &b, stack_b)
        join()
    }
    return a.ticks + b.ticks                  // 4
}
```

A `join` at the end means the `with` completes only after both tasks have exited, so every continuation has been consumed.

## Sizing stacks and other notes

- A fiber stack is just a slice you supply: a static array, a slice of a larger array, or memory from an allocator. `@min_stack` is the smallest stack the implementation supports.
- `@stack_size(f)` is a compile-time upper bound on the stack a call of the named function `f` needs, including everything it calls and the reserve described below, and never less than `@min_stack`. A stack of that size is enough for a fiber that runs `f`:

  ```gamma
  // Enough for a fiber made by `@fiber(task_entry, …)`, whatever `task_entry` calls.
  task_stack: [@stack_size(task_entry)]u8 = ---
  ```

  The bound follows the calls `f` can make. Performs of `ctl` operations cost nothing here (the fiber suspends and the clause runs on the driver); other performs count as calls to their clauses when the handler is installed by a `with` on the way. Function values passed down as arguments, such as the comparison given to `sort_by`, are followed too. Recursion, calls through function pointers, performs whose handler `f` cannot see (such as `print`, which goes to the ambient `Out` handler), and foreign calls are compile errors, reported where they occur.
- Fibers do not migrate between threads. Each thread has its own handlers, and a `with` and all its fibers belong to the thread that executed it.
- `defer` statements cannot perform `ctl` operations, so cancellation can never suspend.
- Debug and Checked builds check each call against the fiber's stack, so running out of it traps instead of overwriting other memory. The reference implementation keeps the lowest 8 KiB of a fiber stack in reserve for reporting the overflow, so `@min_stack` (16 KiB) leaves room for only a few calls; give fibers that do real work tens of kilobytes or more.
- A pull-style generator over any iterator can be built from a fiber, but the core library does not provide one.

> **Common mistakes.**
>
> - **A `ctl` handler without `on`, or `on` without a `ctl` handler.** The two must go together.
> - **A stack that is too small, or that is reused** while the `with` is running.
> - **Finishing the `with` with fibers still suspended.** `discard` them or `join` them first.
> - **Consuming a continuation twice,** or from a clause of a different `with`.
> - **A `ctl` clause that does not end by transferring or exiting.** Its type is `never`.

## Summary

- `ctl` operations suspend the performer; `with h on stack { ... }` provides the stack for the fiber that runs the body.
- A `ctl` clause gets a one-shot continuation and ends with `resume k(v)`, `discard k` plus an exit, or another exit.
- Continuations are first-class so schedulers can queue them; one-shot use is checked.
- `@fiber(f, arg, stack)` starts new fibers from function pointers.
- Nothing allocates: you provide every stack.

Next, [chapter 13](13-safety.md) collects what is checked, what traps, and what is undefined.

For the full rules, see [§14 Fibers and continuations][s14] of the specification.

[s14]: ../../gamma-spec.md#14-fibers-and-continuations
