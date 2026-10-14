# Module `std/hash/crc32`

[All modules](../index.md)

The CRC-32 of ISO-HDLC (ISO 3309, ITU-T V.42), as used by zlib, gzip,
PNG and Ethernet: reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF, final
exclusive-or 0xFFFFFFFF. The checksum of "123456789" is 0xCBF43926.

    crc32 :: import "std/hash/crc32"
    c := crc32.init()
    c.update("1234")
    c.update("56789")
    c.final()                                  // 0xcbf43926
    crc32.checksum("123456789")                // the same in one call

A checksum detects accidental changes; it says nothing about deliberate ones.

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
