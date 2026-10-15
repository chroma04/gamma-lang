# Module `std/fmt`

[All modules](index.md)

Number formatting into caller-provided buffers.

`print` writes every value in one fixed way (spec §21.6.1). This module provides the
other presentations: other bases, padding, thousands separators, and fixed-point and
scientific floats with a chosen number of digits.

The integer functions are generic over `Int(T)` and support any width from 8 bits up
(narrower types cannot hold the digit constants). Floats are `f64`; an `f32` argument
widens exactly.

Every function writes into a buffer the caller supplies and returns the `string` it
wrote, a slice of that buffer. Nothing allocates and nothing performs effects. A
buffer that is too small is a trap, so each kind of output has a constant or function
giving the size that always suffices.

Floats are converted exactly: the binary value of the `f64` is expanded to its full
decimal expansion with fixed-size integer arithmetic and then rounded half to even, so
results agree with C's `printf("%.*f")`, `%.*e` and `%.*g` in glibc.

## Constants

### `INT_BUF`

```gamma
INT_BUF :: 131
```

A buffer size that holds any integer of up to 128 bits in any base, including the sign
and a `0x`, `0o` or `0b` prefix: 128 binary digits, the prefix and `-`.

## Functions

### `max_int_len`

```gamma
max_int_len :: fn(T: type) -> usize
```

The size of a buffer that always holds an integer of type `T` in any base, with sign and
prefix: one byte per bit, plus three.

### `max_grouped_len`

```gamma
max_grouped_len :: fn(T: type) -> usize
```

The size of a buffer that always holds `grouped` output for an integer of type `T`.

### `radix_with`

```gamma
radix_with :: fn(T: type, x: T, base: u32, upper: bool, prefix: bool, buf: []u8) -> string where Int(T)
```

Formats `x` in `base` (2 to 36), a minus sign first if `x` is negative.

`upper` chooses `A`-`Z` over `a`-`z` for digits above 9, and `prefix` adds `0x`, `0o`
or `0b` after the sign (it needs base 16, 8 or 2). `dec`, `hex`, `oct`, `radix` and the
other functions of that kind are this one with fixed arguments. Traps if `buf` is too small (`INT_BUF` always suffices
for types up to 128 bits, `max_int_len(T)` for any type).

### `dec`

```gamma
dec :: fn(T: type, x: T, buf: []u8) -> string where Int(T)
```

Formats `x` in decimal (`-42`).

### `hex`

```gamma
hex :: fn(T: type, x: T, buf: []u8) -> string where Int(T)
```

Formats `x` in lowercase hexadecimal (`ff`, `-1f`).

### `hex_upper`

```gamma
hex_upper :: fn(T: type, x: T, buf: []u8) -> string where Int(T)
```

Formats `x` in uppercase hexadecimal (`FF`).

### `oct`

```gamma
oct :: fn(T: type, x: T, buf: []u8) -> string where Int(T)
```

Formats `x` in octal (`777`).

### `bin`

```gamma
bin :: fn(T: type, x: T, buf: []u8) -> string where Int(T)
```

Formats `x` in binary (`101`).

### `radix`

```gamma
radix :: fn(T: type, x: T, base: u32, buf: []u8) -> string where Int(T)
```

Formats `x` in any base from 2 to 36, with lowercase digits.

### `radix_upper`

```gamma
radix_upper :: fn(T: type, x: T, base: u32, buf: []u8) -> string where Int(T)
```

Formats `x` in any base from 2 to 36, with uppercase digits.

### `prefixed`

```gamma
prefixed :: fn(T: type, x: T, base: u32, buf: []u8) -> string where Int(T)
```

Formats `x` in base 16, 8 or 2 with a `0x`, `0o` or `0b` prefix after the sign
(`-0xff`), with lowercase digits.

### `prefixed_upper`

```gamma
prefixed_upper :: fn(T: type, x: T, base: u32, buf: []u8) -> string where Int(T)
```

Like `prefixed`, with uppercase digits (`0xFF`).

### `grouped`

```gamma
grouped :: fn(T: type, x: T, sep: u8, buf: []u8) -> string where Int(T)
```

Formats `x` in decimal with `sep` between groups of three digits (`1,234,567`).
`max_grouped_len(T)` bytes always suffice.

### `pad_left`

```gamma
pad_left :: fn(s: string, width: usize, fill: u8, buf: []u8) -> string
```

Right-aligns `s` in a field of `width` bytes, filling on the left with `fill`.

If `s` is already `width` bytes or longer it is returned unchanged and `buf` is not
touched. Otherwise the result is the first `width` bytes of `buf`. `s` may itself be
a slice of `buf`. Widths count bytes, not characters.

### `pad_right`

```gamma
pad_right :: fn(s: string, width: usize, fill: u8, buf: []u8) -> string
```

Left-aligns `s` in a field of `width` bytes, filling on the right with `fill`. See
`pad_left` for the details.

### `center`

```gamma
center :: fn(s: string, width: usize, fill: u8, buf: []u8) -> string
```

Centers `s` in a field of `width` bytes. When the padding is odd the extra byte goes
on the right. See `pad_left` for the details.

### `zero_pad`

```gamma
zero_pad :: fn(s: string, width: usize, buf: []u8) -> string
```

Zero-pads a formatted number to `width` bytes, keeping the sign and any `0x`, `0o` or
`0b` prefix first: `-42` becomes `-0042` and `0xff` becomes `0x00ff`.

Text whose first character after the sign and prefix is not a digit of its base
(`nan`, `inf`, or hex digits without `0x`)
is padded with spaces on the left instead. See `pad_left` for the details.

### `fixed_buf_len`

```gamma
fixed_buf_len :: fn(decimals: u32) -> usize
```

The size of a buffer that always holds `fixed(x, decimals, ...)` for any finite or
non-finite `x`: a sign, up to 310 integer digits, the point and the decimals.

### `sci_buf_len`

```gamma
sci_buf_len :: fn(digits: u32) -> usize
```

The size of a buffer that always holds `sci(x, digits, ...)`: sign, `d.`, the digits
and `e+NNN`.

### `general_buf_len`

```gamma
general_buf_len :: fn(precision: u32) -> usize
```

The size of a buffer that always holds `general(x, precision, ...)`.

### `fixed`

```gamma
fixed :: fn(x: f64, decimals: u32, buf: []u8) -> string
```

Formats `x` with exactly `decimals` digits after the point and no exponent, like C's
`printf("%.*f")`: `fixed(3.14159, 2, ...)` is `3.14`, and with 0 decimals there is
no point at all.

The result is correctly rounded from the exact binary value of `x`, ties to even
(`0.125` gives `0.12`, `0.375` gives `0.38`, `2.5` with no decimals gives `2`). `-0.0`
and negative values that round to zero keep their sign (`-0.00`). `nan`, `inf` and
`-inf` are written as such. A `f32` argument is widened exactly. `fixed_buf_len(decimals)`
bytes always suffice; otherwise this traps when `buf` is too small.

### `sci`

```gamma
sci :: fn(x: f64, digits: u32, buf: []u8) -> string
```

Formats `x` in scientific notation with `digits` digits after the point, like C's
`printf("%.*e")`: `sci(1234.5, 3, ...)` is `1.234e+03` (the tie goes to the even digit),
and with 0 digits there is no point (`1e+03`). The exponent has a sign and at least two
digits. Rounding is exact, as for `fixed`; zero gives `0.000e+00`, and `nan`, `inf`
and `-inf` are written as such. `sci_buf_len(digits)` bytes always suffice.

### `general`

```gamma
general :: fn(x: f64, precision: u32, buf: []u8) -> string
```

Formats `x` with `precision` significant digits, like C's `printf("%.*g")`: it uses
scientific notation when the exponent is below -4 or at least `precision`, fixed-point
otherwise, and drops trailing zeros and a bare point (`general(0.5, 6, ...)` is `0.5`,
`general(1e10, 6, ...)` is `1e+10`). A precision of 0 counts as 1. `general_buf_len(precision)`
bytes always suffice.
