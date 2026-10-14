# Module `std/encoding/base64`

[All modules](../index.md)

Base64 as in RFC 4648, with the standard alphabet (section 4)
or the URL and filename safe one (section 5), with or without `=` padding.

    base64 :: import "std/encoding/base64"
    buf: [64]u8 = ---
    text := base64.encode("foobar", buf[..], base64.Options())      // "Zm9vYmFy"
    raw: [48]u8 = ---
    data := try base64.decode(text, raw[..], base64.Options())      // "foobar"

Encoding and decoding write into a buffer you supply and return the part of it that was
written, or append to a `List(u8)`. Nothing else allocates, and nothing performs
effects. `encoded_len` and `decoded_len_max` give the buffer sizes that always suffice.

## Structs

### `Options`

```gamma
Options :: struct {
    pub alphabet: Alphabet = .Standard
    pub pad: bool = true
}
```

How to encode and decode. By default: the standard alphabet, with padding.

Fields:

- `pad`: Whether encoding ends with `=` to make the length a multiple of 4. When decoding,
  `true` requires that padding and `false` also accepts input that has it.

## Unions

### `Alphabet`

```gamma
Alphabet :: union {
    Standard
    Url
}
```

Which 64 characters stand for the values: the standard alphabet uses `+` and `/` for
62 and 63, the URL alphabet uses `-` and `_`.

### `DecodeError`

```gamma
DecodeError :: union {
    InvalidByte: usize
    InvalidLength
    InvalidPadding: usize
}
```

Why decoding failed. Positions are byte offsets into the input.

Variants:

- `InvalidByte`: A byte that is not in the alphabet.
- `InvalidLength`: The number of characters cannot come from encoding any data: a single character
  left over, or missing padding when `pad` is set.
- `InvalidPadding`: A `=` in the wrong place or too many of them (the position of the first
  offending one), or, with the position of the last data character, trailing bits
  that are not zero (which no encoder produces).

### `AppendError`

```gamma
AppendError :: union {
    Decode: DecodeError
    Alloc: AllocError
}
```

The error of the `_append` functions: the input was invalid, or the list could not
grow.

## Functions

### `encoded_len`

```gamma
encoded_len :: fn(n: usize, o: Options) -> usize
```

The length of the encoding of `n` bytes.

### `encode`

```gamma
encode :: fn(src: []const u8, dst: []u8, o: Options) -> string
```

Encodes `src` into `dst`, which must hold `encoded_len(src.len, o)` bytes (a trap
otherwise), and returns the part of `dst` written.

### `encode_append`

```gamma
encode_append :: fn(out: ^List(u8), src: []const u8, o: Options) -> AllocError!()
```

Appends the encoding of `src` to `out`.

### `decoded_len_max`

```gamma
decoded_len_max :: fn(n: usize) -> usize
```

The size of a buffer that always holds the decoding of `n` characters.

### `decode`

```gamma
decode :: fn(src: string, dst: []u8, o: Options) -> DecodeError![]u8
```

Decodes `src` into `dst`, which must hold `decoded_len_max(src.len)` bytes (a trap
otherwise), and returns the part of `dst` written.

With `o.pad` set the input must end with the padding that makes its length a multiple
of 4, and without it padding is optional. Padding that is partly there, too long or
not at the end is an error. Only the canonical encoding is accepted: the unused bits
of the last character must be zero. The input is checked as a whole before anything is
written, and the first problem, in the order bytes, length, padding, is reported.

### `decode_append`

```gamma
decode_append :: fn(out: ^List(u8), src: string, o: Options) -> AppendError!()
```

Appends the decoding of `src` to `out`, with the rules of `decode`. If `src` is
invalid nothing is appended.
