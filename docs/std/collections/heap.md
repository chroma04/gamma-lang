# Module `std/collections/heap`

[All modules](../index.md)

A binary heap, used as a priority queue.

    hp :: import "std/collections/heap"

    h := hp.heap(u32)                    // smallest first
    defer h.deinit()
    try h.push(5)
    try h.push(2)
    print("{}\n", h.pop())               // 2

The element that comes first is the one for which no other element is `less`.
`heap(T)` orders by `<`; `heap_by` takes the comparison as a function pointer, so a
max-heap or a heap of structs needs no wrapper type. The comparison is stored in the
heap (it is a plain function pointer: it cannot capture variables, so keep any
context in the elements themselves). Like `List`, a heap keeps the allocator it was
created with, performs no `Alloc` afterwards, and reports a failed growth as
`AllocError`.

## Structs

### `Heap`

```gamma
Heap :: struct(T: type) {
    // private fields omitted
}
```

A priority queue: a binary heap in a growable array.

## Functions

### `heap`

```gamma
heap :: fn(T: type) -> Heap(T) needs Alloc where Ord(T)
```

An empty heap that yields the smallest element first (by `<`), using the current allocator.

### `heap_by`

```gamma
heap_by :: fn(T: type, less: ^fn(a, b: ^const T) -> bool) -> Heap(T) needs Alloc
```

An empty heap that yields first the element that `less` puts before all others, using
the current allocator. `less(a, b)` says that `a` must come out before `b`; it should
be a strict weak ordering (`fn(a, b) => a^ > b^` makes a max-heap).

### `heap_from`

```gamma
heap_from :: fn(T: type, xs: []const T) -> AllocError!Heap(T) needs Alloc where Ord(T)
```

A heap holding a copy of `xs`, built in O(n) time, that yields the smallest element first.

### `heap_from_by`

```gamma
heap_from_by :: fn(T: type, less: ^fn(a, b: ^const T) -> bool, xs: []const T) -> AllocError!Heap(T) needs Alloc
```

Like `heap_from`, ordered by `less` as for `heap_by`.

### `push`

```gamma
push :: fn(T: type, h: ^Heap(T), x: T) -> AllocError!()
```

Adds `x`.

### `pop`

```gamma
pop :: fn(T: type, h: ^Heap(T)) -> ?T
```

Removes and returns the element that comes first, or `null` if the heap is empty.

### `peek`

```gamma
peek :: fn(T: type, h: ^const Heap(T)) -> ?T
```

The element that comes first, without removing it.

### `len`

```gamma
len :: fn(T: type, h: ^const Heap(T)) -> usize
```

The number of elements.

### `is_empty`

```gamma
is_empty :: fn(T: type, h: ^const Heap(T)) -> bool
```

Whether the heap has no elements.

### `clear`

```gamma
clear :: fn(T: type, h: ^Heap(T))
```

Removes every element, keeping the storage.

### `deinit`

```gamma
deinit :: fn(T: type, h: ^Heap(T))
```

Frees the heap's storage through its own allocator.
