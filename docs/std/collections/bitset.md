# Module `std/collections/bitset`

[All modules](../index.md)

A set of small unsigned integers, one bit each.

    bs :: import "std/collections/bitset"

    s := try bs.bitset(100)              // members are 0..100
    defer s.deinit()
    s.insert(3)
    s.insert(64)
    for i in s.items() { print("{}\n", i) }     // 3, 64

The universe is fixed when the set is created: it holds the integers below the
capacity `n` given to `bitset`. The bits live in a block of 64-bit words from the
allocator that was current at creation, so no operation after construction allocates
and none can fail for lack of memory. Bits at positions `n` and above are never set,
which keeps the whole-word operations (`count`, `union_with`, …) exact.

## Structs

### `BitSet`

```gamma
BitSet :: struct {
    // private fields omitted
}
```

A set of the integers below its capacity, stored one bit each.

## Functions

### `bitset`

```gamma
bitset :: fn(n: usize) -> AllocError!BitSet \ {Alloc}
```

An empty set that can hold the integers `0..n`, using the current allocator.

### `capacity`

```gamma
capacity :: fn(b: ^const BitSet) -> usize
```

The number of integers the set can hold: its members are below this.

### `insert`

```gamma
insert :: fn(b: ^BitSet, i: usize)
```

Adds `i`. Traps if `i` is not below the capacity.

### `remove`

```gamma
remove :: fn(b: ^BitSet, i: usize)
```

Removes `i`, if it is a member.

### `contains`

```gamma
contains :: fn(b: ^const BitSet, i: usize) -> bool
```

Whether `i` is a member. Integers at or above the capacity are not.

### `count`

```gamma
count :: fn(b: ^const BitSet) -> usize
```

The number of members.

### `is_empty`

```gamma
is_empty :: fn(b: ^const BitSet) -> bool
```

Whether the set has no members.

### `clear`

```gamma
clear :: fn(b: ^BitSet)
```

Removes every member.

### `union_with`

```gamma
union_with :: fn(b: ^BitSet, other: ^const BitSet)
```

Adds every member of `other` to `b`. Traps if `other` has a member that `b`'s
capacity cannot hold.

### `intersect_with`

```gamma
intersect_with :: fn(b: ^BitSet, other: ^const BitSet)
```

Removes the members of `b` that are not in `other`.

### `next_member`

```gamma
next_member :: fn(b: ^const BitSet, from: usize) -> ?usize
```

The smallest member that is at least `from`, if there is one. Calling it again with
the member plus one steps through the set in increasing order.

### `items`

```gamma
items :: fn(b: ^const BitSet) \ {Yield(usize)}
```

The members in increasing order. The set must not change during the iteration.

### `deinit`

```gamma
deinit :: fn(b: ^BitSet)
```

Frees the set's storage through its own allocator.
