# Module `std/fiber`

[All modules](index.md)

A cooperative round-robin scheduler for fibers (§14).

Tasks are fibers that run one at a time on the thread that installed the scheduler and
take turns when they call `Sched.yield_now`. The scheduler is a handler for the `Sched` effect:

    fiber :: import "std/fiber"

    worker :: fn(c: ^Counter) needs Sched {
        c.ticks += 1
        fiber.Sched.yield_now()               // let the other tasks run
        c.ticks += 1
    }

    s := fiber.scheduler()
    defer s.deinit()
    with &s on main_stack {
        fiber.spawn(Counter, worker, &a, stack_a)
        fiber.spawn(Counter, worker, &b, stack_b)
        fiber.Sched.join()                    // returns once both tasks have exited
    }

Every task needs a stack of its own (§14.1), and the code running under `with` needs one
too. A task ends when its function returns; its `defer`s run then. The `with` cannot
complete while a task is suspended, so end the body with `Sched.join()`. Only the code under
the `with` joins, not the tasks, and a task that never ends is a deadlock, which traps.
A task's `arg` and `stack` must outlive the task.

Tasks may perform other effects too, handled where the scheduler is installed. They are
the scheduler's effect-set parameter (§16.6): with `s := fiber.scheduler(Out + Clock)`
a task is a `fn(arg: ^T) needs Sched(Out + Clock) + Out + Clock` (or uses fewer of them), and
the `with &s` needs `Out` and `Clock` itself.

`Scheduler` keeps the allocator it was created with for its queue of ready tasks and its
heap of sleeping tasks, and performs no `Alloc` afterwards.

A task can wait for another without polling: `Sched.park(&w)` suspends it in a `Waiters`
queue, and it does not run again until a task calls `wake(&w)`, which makes it ready.
`std/fiber/channel` builds a bounded channel between tasks on them.

#### Sleeping

`Clock.sleep` blocks the whole thread. `sleep` and `sleep_until` suspend only the calling
task, so the others keep running meanwhile:

    napper :: fn(arg: ^Job) needs fiber.Sched(Clock) + Clock + Out {
        fiber.sleep(ms(10))                 // `E` comes from the caller's `Sched(Clock)`
        print("awake\n")
    }

    s := fiber.scheduler(Clock)             // tasks that sleep need `Clock` in `E`
    defer s.deinit()

Only the sleeping tasks perform `Clock`: the scheduler never reads the clock itself, so a
program whose tasks never sleep pays nothing for it. Sleeping tasks wait in a heap ordered
by deadline, ties in the order they went to sleep. The scheduler wakes a sleeper in two
cases:

- **Idle.** No task is ready, so the earliest sleeper is told to block until its
  deadline (it calls `Clock.sleep` itself).
- **Check.** Tasks are ready, so the earliest sleeper is asked to look at the clock and
  see whether it is due. A check happens once per round: after as many dispatches as
  there were ready tasks at the previous check (or when a task went to sleep), so every
  ready task runs once between two checks and busy tasks cannot starve the sleepers. A
  check costs one clock read and two task switches per round, and wakes the earliest
  sleeper only; if several are due, they wake in deadline order, one per round. A
  sleeper that is not yet due goes back to its place in the heap.

## Structs

### `Scheduler`

```gamma
Scheduler :: struct(E: effects) {
    // private fields omitted
}
```

A round-robin scheduler: the ready tasks in a queue, in the order they will run, and the
sleeping tasks in a heap by deadline (see `sleep`). Its tasks may perform the effects `E`.

## Type aliases

### `Waiters`

```gamma
Waiters :: waiters_module.Waiters
```

Tasks waiting for something, in the order they started to wait (see `park` and
`wake`). It keeps the allocator it was created with.

## Effects

### `Sched`

```gamma
Sched :: effect(E: effects) {
    yield_now: ctl()
    spawn_raw: ctl(entry: ^fn(arg: rawptr) -> never needs Sched(E) + E, arg: rawptr, stack: []u8)
    exit: ctl() -> never
    join: ctl()
    park: ctl(w: ^Waiters)
    sleep_raw: ctl(deadline: Instant) -> bool
}
```

What a task can ask of its scheduler. `spawn`, which takes a typed task function, is the
way to start a task; `spawn_raw` is the operation underneath.
`E` is what tasks may perform besides `Sched`; `Sched` alone is `Sched` of the empty set.

Operations:

- `park`: Suspends the current task at the back of `w` until `wake` takes it out.
- `sleep_raw`: The operation underneath `sleep_until`: suspends the task until the scheduler picks it
  again. The answer says why: `true` if no other task could run (the task should block
  until `deadline`), `false` if it should check whether `deadline` has passed.

## Handlers

### `handler(E: effects) Sched(E) for ^Scheduler(E)`

```gamma
handler(E: effects) Sched(E) for ^Scheduler(E) needs E
```

## Constants

### `waiters`

```gamma
waiters :: waiters_module.waiters
```

An empty queue of waiting tasks, using the current allocator.

### `wake`

```gamma
wake :: waiters_module.wake
```

Makes the task at the front of `w` ready to run once the current task gives way; false
if no task waits there. It needs no effect, so a `defer` may wake tasks.

## Functions

### `scheduler`

```gamma
scheduler :: fn(E: effects) -> Scheduler(E) needs Alloc
```

A scheduler with no tasks, using the current allocator for its queues. Its tasks may
perform the effects `E` (none if left out).

### `deinit`

```gamma
deinit :: fn(E: effects, s: ^Scheduler(E))
```

Frees the scheduler's queues. All tasks must have ended.

### `spawn`

```gamma
spawn :: fn(T: type, E: effects, f: ^fn(arg: ^T) needs Sched(E) + E, arg: ^T, stack: []u8) needs Sched(E)
```

Starts a task calling `f(arg)` on `stack`, to run once the current task gives way. The
task ends when `f` returns. A little of `stack` (up to 48 bytes) holds the task's
start-up data, so the stack should be a bit larger than the task needs.

### `sleep_until`

```gamma
sleep_until :: fn(E: effects, deadline: Instant) needs Sched(E) + Clock
```

Suspends the current task until `deadline`, letting the other tasks run meanwhile. It
returns at once if `deadline` has passed. The scheduler's effects must include `Clock`
(`fiber.scheduler(Clock)`), because the task reads the clock itself; `E` is taken from the
caller's `Sched(E)`.

### `sleep`

```gamma
sleep :: fn(E: effects, d: Duration) needs Sched(E) + Clock
```

Suspends the current task for at least `d`, letting the other tasks run meanwhile. See
`sleep_until`.
