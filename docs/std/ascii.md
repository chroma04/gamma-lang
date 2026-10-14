# Module `std/ascii`

[All modules](index.md)

Classification and case of single bytes.

Every function takes one byte and answers for the 128 ASCII characters; bytes of 128
and above are not letters, digits, spaces or anything else here. Nothing allocates and
nothing performs effects.

    ascii :: import "std/ascii"
    if ascii.is_alpha(c) { out.push(ascii.to_lower(c)) }

## Functions

### `is_alpha`

```gamma
is_alpha :: fn(c: u8) -> bool
```

Whether `c` is `A`-`Z` or `a`-`z`.

### `is_digit`

```gamma
is_digit :: fn(c: u8) -> bool
```

Whether `c` is `0`-`9`.

### `is_alnum`

```gamma
is_alnum :: fn(c: u8) -> bool
```

Whether `c` is a letter or a decimal digit.

### `is_space`

```gamma
is_space :: fn(c: u8) -> bool
```

Whether `c` is ASCII whitespace: space, `\t`, `\n`, `\r`, `\v` or `\f`.

### `is_upper`

```gamma
is_upper :: fn(c: u8) -> bool
```

Whether `c` is `A`-`Z`.

### `is_lower`

```gamma
is_lower :: fn(c: u8) -> bool
```

Whether `c` is `a`-`z`.

### `is_hex_digit`

```gamma
is_hex_digit :: fn(c: u8) -> bool
```

Whether `c` is `0`-`9`, `a`-`f` or `A`-`F`.

### `is_punct`

```gamma
is_punct :: fn(c: u8) -> bool
```

Whether `c` is a visible character that is neither a letter nor a digit: one of
``!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~``.

### `is_print`

```gamma
is_print :: fn(c: u8) -> bool
```

Whether `c` is printable: a space or a visible character (0x20 to 0x7e).

### `is_control`

```gamma
is_control :: fn(c: u8) -> bool
```

Whether `c` is a control character: 0x00 to 0x1f, or 0x7f.

### `to_lower`

```gamma
to_lower :: fn(c: u8) -> u8
```

`c` as a lowercase letter if it is a capital, and `c` otherwise.

### `to_upper`

```gamma
to_upper :: fn(c: u8) -> u8
```

`c` as a capital if it is a lowercase letter, and `c` otherwise.

### `digit_value`

```gamma
digit_value :: fn(c: u8) -> ?u8
```

The value of `c` as a digit in a base up to 36: 0 to 9 for `0`-`9`, and 10 to 35 for
`a`-`z` or `A`-`Z`. `null` for any other byte.
