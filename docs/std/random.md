# Module `std/random`

[All modules](index.md)

A fast seedable generator, and helpers that draw from `Rand`.

The ambient `Rand` effect is the source of randomness: by default it is the operating
system, in tests a fixed seed. `Rng` (xoshiro256**) is a handler for `Rand`, so the
same code can be made reproducible by installing a seeded generator with `with`.

    random :: import "std/random"
    r := random.seeded(42)
    with &r {
        die := random.range(u8, 1, 7)          // 1 to 6, the same on every run
        if random.chance(0.5) { … }
    }

Every helper obtains its bits by calling `fill` once or more, so any handler of `Rand`
works. The generator is not suitable for cryptography.

## Structs

### `Rng`

```gamma
Rng :: struct {
    // private fields omitted
}
```

The state of a xoshiro256** generator. It is never all zero.

## Handlers

### `handler Rand for ^Rng`

```gamma
handler Rand for ^Rng
```

Fills `buf` from the generator: eight bytes (little-endian) per output, the tail from
one more output.

## Functions

### `seeded`

```gamma
seeded :: fn(seed: u64) -> Rng
```

A generator whose state is `seed` expanded with splitmix64. The same seed always
gives the same sequence.

### `from_rand`

```gamma
from_rand :: fn() -> Rng needs Rand
```

A generator seeded from the current `Rand` (the operating system by default).

### `next`

```gamma
next :: fn(r: ^Rng) -> u64
```

The next output of the generator.

### `next_u64`

```gamma
next_u64 :: fn() -> u64 needs Rand
```

A uniformly distributed `u64`.

### `below`

```gamma
below :: fn(T: type, n: T) -> T needs Rand where Int(T)
```

A uniform integer in [0, n), without modulo bias. `n` must be greater than 0.

### `range`

```gamma
range :: fn(T: type, lo, hi: T) -> T needs Rand where Int(T)
```

A uniform integer in [lo, hi). `lo` must be less than `hi`; the span may be the whole
range of `T` minus one value.

### `float`

```gamma
float :: fn() -> f64 needs Rand
```

A uniform `f64` in [0, 1), made from 53 random bits.

### `chance`

```gamma
chance :: fn(p: f64) -> bool needs Rand
```

`true` with probability `p` (never for `p` <= 0, always for `p` >= 1).

### `normal`

```gamma
normal :: fn(mean, stddev: f64) -> f64 needs Rand
```

A normally distributed `f64` with the given mean and standard deviation (Box-Muller).

### `shuffle`

```gamma
shuffle :: fn(T: type, items: []T) needs Rand
```

Rearranges `items` into a uniformly random order (Fisher-Yates).

### `choice`

```gamma
choice :: fn(T: type, items: []const T) -> ?T needs Rand
```

A uniformly chosen element of `items`, or `null` if it is empty.

### `weighted`

```gamma
weighted :: fn(weights: []const f64) -> ?usize needs Rand
```

An index of `weights` chosen with probability proportional to its weight, or `null`
if there are no weights or their sum is not greater than 0 (or is infinite). Negative
weights count as 0.
