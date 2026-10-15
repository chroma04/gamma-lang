# Module `std/collections/ordered_map`

[All modules](../index.md)

A map that keeps its keys in order, as a B-tree.

    om :: import "std/collections/ordered_map"

    m := om.ordered_map(string, u32)              // keys ordered by `<`
    defer m.deinit()
    try m.put("pear", 3)
    try m.put("apple", 5)
    for e in m.entries() { print("{} {}\n", e.key, e.value) }     // apple, then pear
    for e in m.range("b", null) { … }             // keys from "b" on
    print("{}\n", m.first())                      // (key: "apple", value: 5)

The order of the keys comes from the key type's `<` (`ordered_map`), or from a comparison
function (`ordered_map_by`, like `sort_by` and `heap_by`): `less(a, b)` says that `a`
comes before `b`, and must be a strict weak ordering. Two keys are the same key when
neither comes before the other. The function is stored in the map, so it is a plain
function pointer. A map keeps the allocator it was created with, performs no `Alloc`
afterwards, and reports a failed allocation as `AllocError`, leaving the map as it was.

Lookups, insertions and removals take O(log n) comparisons. Iteration is in key order.
A map must not change during an iteration. Pointers from `get_ptr` are valid until the
next insertion or removal.

The tree has nodes of up to 15 keys. A node is one allocation, so a map of n entries
takes about n / 11 blocks.

## Structs

### `OrderedMap`

```gamma
OrderedMap :: struct(K, V: type) {
    // private fields omitted
}
```

A map from `K` to `V`, ordered by the comparison function it was created with.

## Functions

### `ordered_map`

```gamma
ordered_map :: fn(K, V: type) -> OrderedMap(K, V) \ {Alloc} where Ord(K)
```

An empty map ordered by `<`, using the current allocator.

### `ordered_map_by`

```gamma
ordered_map_by :: fn(K, V: type, less: ^fn(a, b: ^const K) -> bool) -> OrderedMap(K, V) \ {Alloc}
```

An empty map ordered by `less`, using the current allocator. `less(a, b)` says that
`a` comes before `b`; it should be a strict weak ordering. It is stored in the map, so it
is a plain function pointer: it cannot capture variables.

### `len`

```gamma
len :: fn(K, V: type, m: ^const OrderedMap(K, V)) -> usize
```

The number of entries.

### `is_empty`

```gamma
is_empty :: fn(K, V: type, m: ^const OrderedMap(K, V)) -> bool
```

Whether the map has no entries.

### `get`

```gamma
get :: fn(K, V: type, m: ^const OrderedMap(K, V), k: K) -> ?V
```

The value of `k`, if present.

### `get_ptr`

```gamma
get_ptr :: fn(K, V: type, m: ^OrderedMap(K, V), k: K) -> ?^V
```

A pointer to the value stored for `k`, if present, so it can be read or updated in place.
It is valid until the next insertion or removal.

### `contains`

```gamma
contains :: fn(K, V: type, m: ^const OrderedMap(K, V), k: K) -> bool
```

Whether `k` is present.

### `put`

```gamma
put :: fn(K, V: type, m: ^OrderedMap(K, V), k: K, v: V) -> AllocError!()
```

Sets the value of `k`, adding it if absent. When `k` is already present its stored key
is kept (the new one is equal under the ordering).

### `remove`

```gamma
remove :: fn(K, V: type, m: ^OrderedMap(K, V), k: K) -> ?V
```

Removes `k`, returning its value if it was present.

### `remove_entry`

```gamma
remove_entry :: fn(K, V: type, m: ^OrderedMap(K, V), k: K) -> ?(key: K, value: V)
```

Removes `k`, returning the stored key and its value if it was present.

### `first`

```gamma
first :: fn(K, V: type, m: ^const OrderedMap(K, V)) -> ?(key: K, value: V)
```

The entry with the smallest key.

### `last`

```gamma
last :: fn(K, V: type, m: ^const OrderedMap(K, V)) -> ?(key: K, value: V)
```

The entry with the largest key.

### `entries`

```gamma
entries :: fn(K, V: type, m: ^const OrderedMap(K, V)) \ {Yield((key: K, value: V))}
```

The entries in key order. The map must not change during the iteration.

### `keys`

```gamma
keys :: fn(K, V: type, m: ^const OrderedMap(K, V)) \ {Yield(K)}
```

The keys in order.

### `values`

```gamma
values :: fn(K, V: type, m: ^const OrderedMap(K, V)) \ {Yield(V)}
```

The values, in the order of their keys.

### `range`

```gamma
range :: fn(K, V: type, m: ^const OrderedMap(K, V), lo, hi: ?K) \ {Yield((key: K, value: V))}
```

The entries with `lo <= key < hi`, in key order. A bound that is `null` is left open:
`range(5, null)` yields the keys from 5 on. The map must not change during the
iteration.

### `clear`

```gamma
clear :: fn(K, V: type, m: ^OrderedMap(K, V))
```

Removes every entry and frees the nodes.

### `deinit`

```gamma
deinit :: fn(K, V: type, m: ^OrderedMap(K, V))
```

Frees the map's storage through its own allocator.
