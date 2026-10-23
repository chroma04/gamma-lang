# Module `std/collections/soa_list`

[All modules](../index.md)

A growable structure-of-arrays list: a `List` whose elements are stored by field.

    sl :: import "std/collections/soa_list"

    Particle :: struct { pos, vel: #vec [2]f32; alive: bool }

    ps := sl.soa_list(Particle)          // captures the current allocator
    defer ps.deinit()
    try ps.push(Particle(pos: #vec [2]f32(fill: 0), vel: #vec [2]f32(1, 0), alive: true))
    for i in 0..ps.len() { ps.items[i].pos += ps.items[i].vel }
    xs: []#vec [2]f32 = ps.items.pos     // a whole column, contiguous

`items` is a `#soa []S` slice of the elements (§5.6.4): `items[i]` is element `i`,
`items[i].f` its field `f` in place, and `items.f` the column of `f`. The list grows
like `List`: it keeps the allocator it was created with, performs no `Alloc`
afterwards, reports a failed growth as `AllocError`, and at least doubles its
capacity when it grows, so that pushing takes amortized constant time. All the
columns live in one block of `capacity()` elements, which growing replaces, so a
slice of `items` or of one of its columns is valid only until the list next grows.

## Structs

### `SoaList`

```gamma
SoaList :: struct(S: type) {
    pub items: #soa []S
    // private fields omitted
}
```

A growable list of `S` values stored by field, which keeps the allocator it was
created with. `S` is a struct that `#soa` can store (§5.6.4).

Fields:

- `items`: The elements: `items.len` is the length and `for x in l.items` iterates.

## Functions

### `soa_list`

```gamma
soa_list :: fn(S: type) -> SoaList(S) needs Alloc
```

An empty list using the current allocator. It allocates on the first push.

### `len`

```gamma
len :: fn(S: type, l: ^const SoaList(S)) -> usize
```

The number of elements.

### `is_empty`

```gamma
is_empty :: fn(S: type, l: ^const SoaList(S)) -> bool
```

Whether the list has no elements.

### `capacity`

```gamma
capacity :: fn(S: type, l: ^const SoaList(S)) -> usize
```

The number of elements the list can hold before it must grow.

### `reserve`

```gamma
reserve :: fn(S: type, l: ^SoaList(S), extra: usize) -> AllocError!()
```

Makes room for at least `extra` more elements without further allocation.

### `push`

```gamma
push :: fn(S: type, l: ^SoaList(S), x: S) -> AllocError!()
```

Appends `x`.

### `extend`

```gamma
extend :: fn(S: type, l: ^SoaList(S), xs: #soa []const S) -> AllocError!()
```

Appends the elements of `xs`, which may be part of the list itself.

### `insert`

```gamma
insert :: fn(S: type, l: ^SoaList(S), i: usize, x: S) -> AllocError!()
```

Inserts `x` at index `i`, moving the later elements up; traps if `i` is more than the
length (`i` equal to the length appends).

### `remove`

```gamma
remove :: fn(S: type, l: ^SoaList(S), i: usize) -> S
```

Removes and returns the element at index `i`, moving the later elements down. `i` must
be less than the length.

### `swap_remove`

```gamma
swap_remove :: fn(S: type, l: ^SoaList(S), i: usize) -> S
```

Removes and returns the element at index `i`, moving the last element into its place
(constant time, but the order changes). `i` must be less than the length.

### `truncate`

```gamma
truncate :: fn(S: type, l: ^SoaList(S), n: usize)
```

Keeps the first `n` elements (all of them if there are fewer).

### `pop`

```gamma
pop :: fn(S: type, l: ^SoaList(S)) -> ?S
```

Removes and returns the last element.

### `clear`

```gamma
clear :: fn(S: type, l: ^SoaList(S))
```

Removes every element, keeping the storage.

### `deinit`

```gamma
deinit :: fn(S: type, l: ^SoaList(S))
```

Frees the list's storage through its own allocator and leaves it empty.
