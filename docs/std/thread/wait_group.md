# Module `std/thread/wait_group`

[All modules](../index.md)

The wait group of std/thread, which re-exports it.

## Structs

### `WaitGroup`

```gamma
WaitGroup :: struct {
    // private fields omitted
}
```

A counter of outstanding work: `add` before starting it, `done` when it finishes, and
`wait` returns when the count is back at zero.

## Functions

### `wait_group`

```gamma
wait_group :: fn() -> WaitGroup
```

A wait group with nothing outstanding.

### `add`

```gamma
add :: fn(w: ^WaitGroup, n: u32)
```

Adds `n` to the outstanding work.

### `done`

```gamma
done :: fn(w: ^WaitGroup)
```

Marks one piece of work as finished. Calling it more often than `add` is a bug and traps.

### `wait`

```gamma
wait :: fn(w: ^WaitGroup) \ {Sys}
```

Waits until no work is outstanding.
