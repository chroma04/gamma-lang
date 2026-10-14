# Module `std/fiber`

[All modules](index.md)

A cooperative round-robin scheduler for fibers (§14).

Tasks are fibers that run one at a time on the thread that installed the scheduler and
take turns when they call `yield_now`. The scheduler is a handler for the `Sched` effect:

    fiber :: import "std/fiber"

    worker :: fn(c: ^Counter) \ {Sched} {
        c.ticks += 1
        fiber.yield_now()               // let the other tasks run
        c.ticks += 1
    }

    s := fiber.scheduler()
    defer s.deinit()
    with &s on main_stack {
        fiber.spawn(Counter, worker, &a, stack_a)
        fiber.spawn(Counter, worker, &b, stack_b)
        fiber.join()                    // returns once both tasks have exited
    }

Every task needs a stack of its own (§14.1), and the code running under `with` needs one
too. A task ends when its function returns; its `defer`s run then. The `with` cannot
complete while a task is suspended, so end the body with `join()`. Only the code under
the `with` joins, not the tasks, and a task that never ends is a deadlock, which traps.
A task's `arg` and `stack` must outlive the task.

Tasks may perform other effects too, handled where the scheduler is installed. They are
the scheduler's effect-set parameter (§16.6): with `s := fiber.scheduler(\ {Out, Clock})`
a task is a `fn(arg: ^T) \ {Sched(\ {Out, Clock}), Out, Clock}` (or uses fewer of them), and
the `with &s` needs `Out` and `Clock` itself.

`Scheduler` keeps the allocator it was created with for its queue of ready tasks and
performs no `Alloc` afterwards.

## Structs

### `Scheduler`

```gamma
Scheduler :: struct(E: effects) {
    // private fields omitted
}
```

A round-robin scheduler: the ready tasks in a queue, in the order they will run. Its tasks
may perform the effects `E`.

## Effects

### `Sched`

```gamma
Sched :: effect(E: effects) {
    yield_now: ctl()
    spawn_raw: ctl(entry: ^fn(arg: rawptr) -> never \ {Sched(E), ..E}, arg: rawptr, stack: []u8)
    exit: ctl() -> never
    join: ctl()
}
```

What a task can ask of its scheduler. `spawn`, which takes a typed task function, is the
way to start a task; `spawn_raw` is the operation underneath.
`E` is what tasks may perform besides `Sched`; `Sched` alone is `Sched(\ {})`.

## Handlers

### `handler(E: effects) Sched(E) for ^Scheduler(E)`

```gamma
handler(E: effects) Sched(E) for ^Scheduler(E) \ {..E}
```

## Functions

### `scheduler`

```gamma
scheduler :: fn(E: effects) -> Scheduler(E) \ {Alloc}
```

A scheduler with no tasks, using the current allocator for its queue. Its tasks may
perform the effects `E` (none if left out).

### `deinit`

```gamma
deinit :: fn(E: effects, s: ^Scheduler(E))
```

Frees the scheduler's queue. All tasks must have ended.

### `spawn`

```gamma
spawn :: fn(T: type, E: effects, f: ^fn(arg: ^T) \ {Sched(E), ..E}, arg: ^T, stack: []u8) \ {Sched(E)}
```

Starts a task calling `f(arg)` on `stack`, to run once the current task gives way. The
task ends when `f` returns. A little of `stack` (up to 48 bytes) holds the task's
start-up data, so the stack should be a bit larger than the task needs.
