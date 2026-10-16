# Module `std/fiber/channel`

[All modules](../index.md)

A bounded channel between the tasks of a `std/fiber` scheduler.

    fiber :: import "std/fiber"
    chan :: import "std/fiber/channel"

    ch := try chan.channel(u32, 4)      // room for 4 values
    defer ch.deinit()
    // a producer task:  for i in 0..10 { _ = ch.send(i) }  ch.close()
    // a consumer task:  while x := ch.recv() { … }

`send` waits while the channel is full and `recv` while it is empty; a waiting task is
parked (`fiber.park`) and runs again only once another task changes the channel, so
waiting costs nothing. Only one task runs at a time, so no locking is involved; the
channel belongs to the tasks of one scheduler (`std/thread` has a channel between
threads). `send` and `recv` perform the scheduler's `Sched(E)`, whose `E` they take
from the task that calls them; the other operations perform no effect, so a `defer`
may close a channel.

A channel keeps the allocator it was created with and performs no `Alloc` afterwards.

## Structs

### `Channel`

```gamma
Channel :: struct(T: type) {
    // private fields omitted
}
```

A bounded first-in first-out queue of values between tasks. Once closed, sends fail
and receives drain what is left.

## Functions

### `channel`

```gamma
channel :: fn(T: type, capacity: usize) -> AllocError!Channel(T) needs Alloc
```

An open channel holding up to `capacity` (at least 1) values, using the current
allocator.

### `deinit`

```gamma
deinit :: fn(T: type, c: ^Channel(T))
```

Frees the channel. No task may be waiting on it.

### `send`

```gamma
send :: fn(T: type, E: effects, c: ^Channel(T), x: T) -> bool needs fiber.Sched(E)
```

Sends `x`, waiting while the channel is full. Returns `false` if the channel is closed.

### `recv`

```gamma
recv :: fn(T: type, E: effects, c: ^Channel(T)) -> ?T needs fiber.Sched(E)
```

Receives the oldest value, waiting while the channel is empty. Returns `null` once the
channel is closed and drained.

### `try_send`

```gamma
try_send :: fn(T: type, c: ^Channel(T), x: T) -> bool
```

Sends `x` if there is room right now; returns whether it did.

### `try_recv`

```gamma
try_recv :: fn(T: type, c: ^Channel(T)) -> ?T
```

Receives a value if one is waiting right now.

### `close`

```gamma
close :: fn(T: type, c: ^Channel(T))
```

Closes the channel and wakes every task waiting on it: senders then fail, and
receivers get the values still in it, then `null`. Closing twice is allowed.

### `len`

```gamma
len :: fn(T: type, c: ^const Channel(T)) -> usize
```

The number of values in the channel.

### `is_closed`

```gamma
is_closed :: fn(T: type, c: ^const Channel(T)) -> bool
```

Whether the channel was closed.
