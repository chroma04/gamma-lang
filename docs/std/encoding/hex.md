# Module `std/encoding/hex`

[All modules](../index.md)

Base16 as in RFC 4648 section 8, two hexadecimal digits per byte.

    hex :: import "std/encoding/hex"
    buf: [16]u8 = ---
    text := hex.encode("Hi!", buf[..], false)          // "486921"
    raw: [8]u8 = ---
    data := try hex.decode("4869", raw[..])            // "Hi"

Encoding and decoding write into a buffer you supply and return the part of it that was
written, or append to a `List(u8)`. Nothing else allocates, and nothing performs
effects.

## Unions

### `DecodeError`

```gamma
DecodeError :: union {
    InvalidByte: usize
    OddLength
}
```

Why decoding failed.

Variants:

- `InvalidByte`: A byte that is not a hexadecimal digit, at this offset in the input.
- `OddLength`: The input has an odd number of digits.

### `AppendError`

```gamma
AppendError :: union {
    Decode: DecodeError
    Alloc: AllocError
}
```

The error of `decode_append`: the input was invalid, or the list could not grow.

## Functions

### `encode`

```gamma
encode :: fn(src: []const u8, dst: []u8, upper: bool) -> string
```

Encodes `src` into `dst`, which must hold `2 * src.len` bytes (a trap otherwise), with
capital digits if `upper` is set, and returns the part of `dst` written.

### `encode_append`

```gamma
encode_append :: fn(out: ^List(u8), src: []const u8, upper: bool) -> AllocError!()
```

Appends the encoding of `src` to `out`.

### `decode`

```gamma
decode :: fn(src: string, dst: []u8) -> DecodeError![]u8
```

Decodes `src`, digits of either case, into `dst`, which must hold `src.len / 2` bytes
(a trap otherwise), and returns the part of `dst` written. An invalid digit is
reported before an odd length.

### `decode_append`

```gamma
decode_append :: fn(out: ^List(u8), src: string) -> AppendError!()
```

Appends the decoding of `src` to `out`, with the rules of `decode`. If `src` is
invalid nothing is appended.
