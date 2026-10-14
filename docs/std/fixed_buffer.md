# Module `std/fixed_buffer`

[All modules](index.md)

An allocator over a buffer the caller owns (§15.2).

    store: [4096]u8 = ---
    fb := fixed_buffer.over(store[..])
    with fb.allocator() { … }               // allocations come from `store`

## Structs

### `FixedBuffer`

```gamma
FixedBuffer :: struct {
    // private fields omitted
}
```

Allocates from a buffer the caller owns, front to back. Freeing or resizing works
only for the most recent allocation (others are kept until `reset`); when the
buffer is full, allocation fails with `OutOfMemory`.

## Functions

### `over`

```gamma
over :: fn(buf: []u8) -> FixedBuffer
```

A fixed-buffer allocator over `buf`.

### `allocator`

```gamma
allocator :: fn(fb: ^FixedBuffer) -> Allocator
```

The `Allocator` handle of `fb`, which must stay in place while the handle is used.

### `reset`

```gamma
reset :: fn(fb: ^FixedBuffer)
```

Forgets every allocation: the whole buffer is free again.

### `bytes_used`

```gamma
bytes_used :: fn(fb: ^const FixedBuffer) -> usize
```

The bytes allocated so far, including alignment padding.
