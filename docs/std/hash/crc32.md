# Module `std/hash/crc32`

[All modules](../index.md)

## Structs

### `Crc32`

```gamma
Crc32 :: struct {
    // private fields omitted
}
```

The running state of a checksum over the bytes given to `update` so far.

## Functions

### `init`

```gamma
init :: fn() -> Crc32
```

A state for the empty input.

### `update`

```gamma
update :: fn(c: ^Crc32, data: []const u8)
```

Adds `data` to the checksum.

### `final`

```gamma
final :: fn(c: ^const Crc32) -> u32
```

The checksum of everything given to `update`. The state is unchanged, so more data can
still be added.

### `checksum`

```gamma
checksum :: fn(data: []const u8) -> u32
```

The checksum of `data`.
