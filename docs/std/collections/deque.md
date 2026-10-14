# Module `std/collections/deque`

[All modules](../index.md)

A double-ended queue in a growable ring buffer.

    dq :: import "std/collections/deque"

    d := dq.deque(u32)                   // captures the current allocator
    defer d.deinit()
    try d.push_back(2)
    try d.push_front(1)
    for x in d.items() { print("{}\n", x) }     // 1, 2

Like `List`, a deque keeps the allocator it was created with, performs no `Alloc`
afterwards, and reports a failed growth as `AllocError`. Operations at either end
take constant time (amortized when they grow the buffer). The ring is stored in
one block of `cap` elements; the element at logical index `i` lives in slot
`(head + i) mod cap`.

## Structs

### `Deque`

```gamma
Deque :: struct(T: type) {
    // private fields omitted
}
```

A growable double-ended queue that keeps the allocator it was created with.

### `Cursor`

```gamma
Cursor :: struct(T: type) {
    // private fields omitted
}
```

A position in a deque, for advancing through several sequences in lockstep (§13.4).
The deque must not change while the cursor is in use.

## Functions

### `deque`

```gamma
deque :: fn(T: type) -> Deque(T) \ {Alloc}
```

An empty deque using the current allocator. It allocates on the first push.

### `len`

```gamma
len :: fn(T: type, d: ^const Deque(T)) -> usize
```

The number of elements.

### `is_empty`

```gamma
is_empty :: fn(T: type, d: ^const Deque(T)) -> bool
```

Whether the deque has no elements.

### `capacity`

```gamma
capacity :: fn(T: type, d: ^const Deque(T)) -> usize
```

The number of elements the deque can hold before it must grow.

### `reserve`

```gamma
reserve :: fn(T: type, d: ^Deque(T), extra: usize) -> AllocError!()
```

Makes room for at least `extra` more elements without further allocation.

### `push_back`

```gamma
push_back :: fn(T: type, d: ^Deque(T), x: T) -> AllocError!()
```

Appends `x` at the back.

### `push_front`

```gamma
push_front :: fn(T: type, d: ^Deque(T), x: T) -> AllocError!()
```

Prepends `x` at the front.

### `pop_back`

```gamma
pop_back :: fn(T: type, d: ^Deque(T)) -> ?T
```

Removes and returns the last element.

### `pop_front`

```gamma
pop_front :: fn(T: type, d: ^Deque(T)) -> ?T
```

Removes and returns the first element.

### `front`

```gamma
front :: fn(T: type, d: ^const Deque(T)) -> ?T
```

The first element, if there is one.

### `back`

```gamma
back :: fn(T: type, d: ^const Deque(T)) -> ?T
```

The last element, if there is one.

### `get`

```gamma
get :: fn(T: type, d: ^const Deque(T), i: usize) -> ?T
```

The element at index `i`, counting from the front, if there is one.

### `get_ptr`

```gamma
get_ptr :: fn(T: type, d: ^Deque(T), i: usize) -> ?^T
```

A pointer to the element at index `i`, so it can be read or updated in place. It is
valid until the next push or `reserve`.

### `clear`

```gamma
clear :: fn(T: type, d: ^Deque(T))
```

Removes every element, keeping the storage.

### `deinit`

```gamma
deinit :: fn(T: type, d: ^Deque(T))
```

Frees the deque's storage through its own allocator.

### `items`

```gamma
items :: fn(T: type, d: ^const Deque(T)) \ {Yield(T)}
```

The elements from front to back. The deque must not change during the iteration.

### `cursor`

```gamma
cursor :: fn(T: type, d: ^const Deque(T)) -> Cursor(T)
```

A cursor at the front of `d`.

### `next`

```gamma
next :: fn(T: type, c: ^Cursor(T)) -> ?T
```

The next element, or `null` after the last.
