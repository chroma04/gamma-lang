# Module `std/thread`

[All modules](index.md)

Threads and the synchronization built on futexes (§19.6).

A thread runs a plain function `^fn(arg: ^T) \ {Sys}` and starts with no handlers
installed; `defaults` installs the usual ones:

    work :: fn(arg: ^State) \ {Sys} {
        thread.defaults(fn() \ {Out, Alloc} { print("hello from a thread\n") })
    }
    t := try thread.spawn(State, work, &state)
    t.join()

Only function pointers and data cross threads: handlers, second-class functions and
continuations stay on the thread that made them. Shared memory needs atomics
(`@atomic_*`) or one of the types below; a data race is undefined behavior.

`Mutex`, `Condvar` and `WaitGroup` are plain structs without allocation, usable as
fields; `Channel` is a bounded queue between threads.

## Structs

### `Thread`

```gamma
Thread :: struct {
    // private fields omitted
}
```

A running thread, to be joined.

### `Mutex`

```gamma
Mutex :: struct {
    // private fields omitted
}
```

A lock. `lock` waits, in the kernel when it has to, until the mutex is free. The state
is 0 when unlocked, 1 when locked and 2 when locked with threads possibly waiting.

### `Condvar`

```gamma
Condvar :: struct {
    // private fields omitted
}
```

A condition variable: threads wait on it, holding a `Mutex`, until another thread
signals that what they wait for may have happened.

### `WaitGroup`

```gamma
WaitGroup :: struct {
    // private fields omitted
}
```

A counter of outstanding work: `add` before starting it, `done` when it finishes, and
`wait_all` returns when the count is back at zero.

### `Channel`

```gamma
Channel :: struct(T: type) {
    // private fields omitted
}
```

A bounded blocking queue for any number of sending and receiving threads. It keeps the
allocator it was created with. Once closed, sends fail and receives drain what is left.

## Functions

### `spawn`

```gamma
spawn :: fn(T: type, f: ^fn(arg: ^T) \ {Sys}, arg: ^T) -> IoError!Thread \ {Sys}
```

Starts a thread calling `f(arg)`.

### `join`

```gamma
join :: fn(t: Thread) \ {Sys}
```

Waits for `t` to finish.

### `yield_now`

```gamma
yield_now :: fn() \ {Sys}
```

Lets other threads run.

### `cpu_count`

```gamma
cpu_count :: fn() -> u32 \ {Sys}
```

The number of processors available to the program (at least 1).

### `defaults`

```gamma
defaults :: fn(body: fn() \ {Out, Err, Alloc, In, Clock, Rand, Fs, Net}) \ {Sys}
```

Runs `body` under the default handlers of the ambient effects: the system heap,
standard output and error, and the real input, clocks, randomness, files and network.

### `mutex`

```gamma
mutex :: fn() -> Mutex
```

An unlocked mutex.

### `lock`

```gamma
lock :: fn(m: ^Mutex) \ {Sys}
```

Takes the mutex, waiting for it if another thread holds it.

### `try_lock`

```gamma
try_lock :: fn(m: ^Mutex) -> bool
```

Takes the mutex if it is free; returns whether it did.

### `unlock`

```gamma
unlock :: fn(m: ^Mutex)
```

Releases the mutex, which the calling thread holds.

### `condvar`

```gamma
condvar :: fn() -> Condvar
```

A condition variable nobody waits on.

### `wait`

```gamma
wait :: fn(c: ^Condvar, m: ^Mutex) \ {Sys}
```

Unlocks `m`, waits for a signal, and locks `m` again. It may also return without a
signal, so call it in a loop that tests the condition: `while not ready { c.wait(&m) }`.

### `wait_timeout`

```gamma
wait_timeout :: fn(c: ^Condvar, m: ^Mutex, d: Duration) -> bool \ {Sys}
```

Like `wait`, but gives up after `d`; returns `false` if it did so without a signal.
`m` is locked again either way.

### `signal`

```gamma
signal :: fn(c: ^Condvar)
```

Wakes one thread waiting on `c`, if there is one.

### `broadcast`

```gamma
broadcast :: fn(c: ^Condvar)
```

Wakes every thread waiting on `c`.

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

### `wait_all`

```gamma
wait_all :: fn(w: ^WaitGroup) \ {Sys}
```

Waits until no work is outstanding.

### `channel`

```gamma
channel :: fn(T: type, capacity: usize) -> AllocError!Channel(T) \ {Alloc}
```

A channel holding up to `capacity` (at least 1) values, using the current allocator.

### `send`

```gamma
send :: fn(T: type, c: ^Channel(T), x: T) -> bool \ {Sys}
```

Sends `x`, waiting while the channel is full. Returns `false` if the channel is closed.

### `recv`

```gamma
recv :: fn(T: type, c: ^Channel(T)) -> ?T \ {Sys}
```

Receives the oldest value, waiting while the channel is empty. Returns `null` once the
channel is closed and drained.

### `try_send`

```gamma
try_send :: fn(T: type, c: ^Channel(T), x: T) -> bool \ {Sys}
```

Sends `x` if there is room right now; returns whether it did.

### `try_recv`

```gamma
try_recv :: fn(T: type, c: ^Channel(T)) -> ?T \ {Sys}
```

Receives a value if one is waiting right now.

### `close`

```gamma
close :: fn(T: type, c: ^Channel(T)) \ {Sys}
```

Closes the channel: later sends fail, and blocked senders and receivers wake up.

### `deinit`

```gamma
deinit :: fn(T: type, c: ^Channel(T))
```

Frees the channel's buffer through its own allocator. No thread may use the channel
afterwards.
