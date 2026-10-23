# Module `std/uuid`

[All modules](index.md)

Universally unique identifiers as in RFC 9562: the 128-bit value, its canonical text
form, and generators for versions 4 (random) and 7 (time-ordered).

    uuid :: import "std/uuid"
    id := uuid.v7()                                  // needs Clock + Rand
    buf: [uuid.LEN]u8 = ---
    print("{}\n", uuid.format(id, buf[..]))          // 017f22e2-79b0-7cc3-98c4-dc0c0c07398f
    back := try uuid.parse("017F22E2-79B0-7CC3-98C4-DC0C0C07398F")
    print("{}\n", back is id)

Text is always the canonical form: 36 characters, lowercase hexadecimal in groups of
8-4-4-4-12 separated by hyphens. `parse` accepts digits of either case and nothing else:
no braces, no `urn:uuid:` prefix, no form without hyphens. Randomness and the clock are
the `Rand` and `Clock` effects, so tests can substitute them. The module keeps no state
and allocates nothing.

## Structs

### `Uuid`

```gamma
Uuid :: struct {
    pub bytes: [16]u8
}
```

A UUID: sixteen bytes in the order of the RFC (most significant first), so comparing
two with `is` compares all 128 bits.

## Unions

### `ParseError`

```gamma
ParseError :: union {
    Syntax: usize
}
```

Why parsing failed.

Variants:

- `Syntax`: The text is not in the canonical form: the byte index of the first byte that is
  wrong (a non-digit where a digit belongs, a missing or misplaced hyphen), the
  length of the text if it ended too early, or `LEN` if it is longer than `LEN`.

## Type aliases

### `NIL`

```gamma
NIL :: Uuid(bytes: [_]u8(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))
```

The nil UUID, all bits zero (RFC 9562 section 5.9).

### `MAX`

```gamma
MAX :: ...
```

The max UUID, all bits one (RFC 9562 section 5.10).

## Constants

### `LEN`

```gamma
LEN :: 36
```

The length of the canonical text form, and the size of a buffer that always holds it.

## Functions

### `format`

```gamma
format :: fn(u: Uuid, buf: []u8) -> string
```

Writes the canonical text of `u` into `buf`, which must hold `LEN` bytes (a trap
otherwise), and returns the part written.

### `parse`

```gamma
parse :: fn(s: string) -> ParseError!Uuid
```

Parses the canonical 36-character form, with hexadecimal digits of either case. Any other
text is `Syntax(i)`, where `i` is the index of the first byte that does not fit, the length
of `s` if it is too short, or `LEN` if it is too long.

### `version`

```gamma
version :: fn(u: Uuid) -> u8
```

The version field of `u`, the high nibble of byte 6 (4 for `v4`, 7 for `v7`). It is
meaningful only if `is_rfc_variant(u)`.

### `is_rfc_variant`

```gamma
is_rfc_variant :: fn(u: Uuid) -> bool
```

Whether the variant field of `u` is the one of RFC 9562, the bits `10` at the top of
byte 8. `NIL` and `MAX` are not of that variant.

### `v4`

```gamma
v4 :: fn() -> Uuid needs Rand
```

A random UUID (version 4): 122 bits from `Rand.fill`, the rest fixed by the version and
variant.

### `v7`

```gamma
v7 :: fn() -> Uuid needs Clock + Rand
```

A time-ordered UUID (version 7): the 48-bit Unix time in milliseconds (`Clock.wall()`
divided by a million, rounded down) in the first six bytes, big-endian, then the version,
the variant and 74 random bits from `Rand.fill`.

UUIDs from different milliseconds sort by creation time as byte strings. Those from the
same millisecond are not ordered among themselves: the library keeps no counter. A wall
clock before 1970 (negative) counts as the epoch, so the timestamp is 0. Every other
`Clock.wall()` value (up to the year 2262) fits the 48-bit field.
