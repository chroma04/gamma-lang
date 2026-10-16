# Module `std/fiber/waiters`

[All modules](../index.md)

The queue of waiting tasks of std/fiber, which re-exports it.

## Structs

### `Waiters`

```gamma
Waiters :: struct {
    // private fields omitted
}
```

Tasks waiting for something, in the order they started to wait (see `park` and
`wake` of `std/fiber`). It keeps the allocator it was created with.

## Functions

### `waiters`

```gamma
waiters :: fn() -> Waiters needs Alloc
```

An empty queue of waiting tasks, using the current allocator.

### `deinit`

```gamma
deinit :: fn(w: ^Waiters)
```

Frees the queue. No task may be waiting in it.

### `is_empty`

```gamma
is_empty :: fn(w: ^const Waiters) -> bool
```

Whether no task waits in the queue.

### `wake`

```gamma
wake :: fn(w: ^Waiters) -> bool
```

Makes the task at the front ready to run once the current task gives way; false if
no task waits. It needs no effect, so a `defer` may wake tasks.

### `add`

```gamma
add :: fn(w: ^Waiters, k: cont(()), ready: ^queue.Deque(cont(())))
```

Adds suspended task `k` at the back; `ready` is its scheduler's queue of ready tasks.
This is the scheduler's side of `park`.
