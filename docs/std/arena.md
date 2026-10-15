# Module `std/arena`

[All modules](index.md)

An allocator that takes memory from a parent allocator in growing chunks
and gives it all back at once (§15.2).

    a := arena.new()                        // chunks from the current allocator
    defer a.deinit()                        // releases everything at once
    with a.allocator() { … }

## Structs

### `Arena`

```gamma
Arena :: struct {
    // private fields omitted
}
```

Allocates from chunks obtained from a parent allocator and releases them all at
once with `deinit`. Each new chunk is twice the size of the previous one (at least
4096 bytes, and large enough for the request). `free` and `resize` work only for the
most recent allocation (other blocks stay until `deinit`; shrinking is always allowed).

## Functions

### `new`

```gamma
new :: fn() -> Arena \ {Alloc}
```

An empty arena whose chunks come from the current allocator.

### `allocator`

```gamma
allocator :: fn(a: ^Arena) -> Allocator
```

The `Allocator` handle of `a`, which must stay in place while the handle is used.

### `deinit`

```gamma
deinit :: fn(a: ^Arena)
```

Releases every chunk; the arena is empty again and can be reused.
