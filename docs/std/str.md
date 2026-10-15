# Module `std/str`

[All modules](index.md)

Byte-oriented string utilities.

A `string` is a `[]const u8`, and every function here works on bytes: an index is a
byte index and case rules are ASCII only. Functions that return a `string` return a
slice of their argument (or of the buffer the caller supplied), so nothing is copied
and nothing allocates, with the one exception of `join`.

Strings are slices, so these are free functions (`str.trim(s)`), not methods. The
builtin methods of slices cover the rest: `s.starts_with(p)`, `s.ends_with(p)`,
`s.index_of(byte)`, `s.utf8_valid()`, `s is t` for equality and `<` for ordering, so
this module does not repeat them.

    str :: import "std/str"
    for line in str.lines(text) { … }
    n := try str.parse_int(u32, "0xff")
    x := try str.parse_float(f64, "6.02e23")

## Structs

### `Builder`

```gamma
Builder :: struct {
    // private fields omitted
}
```

Appends text to a buffer you supply. It allocates nothing; appending beyond the
buffer's length is a trap (`"str: builder is full"`). Create one with `builder`.

To format into a builder, use it as the sink of `@write` (§21.6.1):
`@write(fn(bytes) => b.push(bytes), "{} of {}", (n, total))`.

## Unions

### `ParseError`

```gamma
ParseError :: union {
    Empty
    InvalidDigit: (at: usize)
    Overflow
}
```

Why `parse_int`, `parse_radix` or `parse_float` failed.

Variants:

- `Empty`: The string is empty.
- `InvalidDigit`: A byte that is not a digit (or a misplaced `_`, or a missing digit, or any other
  byte the syntax does not allow there) at this index; `at` is `s.len` when digits
  are missing at the end.
- `Overflow`: The integer does not fit the type. (A float too large for its type is infinite.)

## Functions

### `trim_start`

```gamma
trim_start :: fn(s: string) -> string
```

`s` without leading ASCII whitespace (space, `\t`, `\n`, `\r`, `\v`, `\f`).

### `trim_end`

```gamma
trim_end :: fn(s: string) -> string
```

`s` without trailing ASCII whitespace.

### `trim`

```gamma
trim :: fn(s: string) -> string
```

`s` without leading and trailing ASCII whitespace.

### `find`

```gamma
find :: fn(s, needle: string) -> ?usize
```

The byte index of the first occurrence of `needle` in `s`, or `null`. An empty
`needle` is found at index 0.

### `rfind`

```gamma
rfind :: fn(s, needle: string) -> ?usize
```

The byte index of the last occurrence of `needle` in `s`, or `null`. An empty
`needle` is found at `s.len`.

### `contains`

```gamma
contains :: fn(s, needle: string) -> bool
```

Whether `needle` occurs in `s`.

### `find_ignore_case`

```gamma
find_ignore_case :: fn(s, needle: string) -> ?usize
```

The byte index of the first occurrence of `needle` in `s` when ASCII letters are
compared without case, or `null`. An empty `needle` is found at index 0.

### `count`

```gamma
count :: fn(s, needle: string) -> usize
```

The number of non-overlapping occurrences of `needle` in `s`, found from the left
(`count("aaa", "aa")` is 1). Traps if `needle` is empty.

### `strip_prefix`

```gamma
strip_prefix :: fn(s, prefix: string) -> ?string
```

`s` without the leading `prefix`, or `null` if `s` does not start with it.

### `strip_suffix`

```gamma
strip_suffix :: fn(s, suffix: string) -> ?string
```

`s` without the trailing `suffix`, or `null` if `s` does not end with it.

### `split_once`

```gamma
split_once :: fn(s, sep: string) -> ?(before: string, after: string)
```

Splits `s` at the first occurrence of `sep`: the text before it and the text after
it, or `null` if `sep` does not occur. An empty `sep` splits at the start.

### `split`

```gamma
split :: fn(s, sep: string) \ {Yield(string)}
```

The pieces of `s` between occurrences of `sep`, in order. Adjacent separators give
empty pieces, and so do separators at either end (`"a,,b,"` gives `a`, ``, `b`, ``).
An empty `sep` does not split: `s` is yielded once.

### `split_any`

```gamma
split_any :: fn(s, seps: string) \ {Yield(string)}
```

The pieces of `s` between bytes that occur in `seps`, in order, with the same empty
pieces as `split` (`"a, b"` split at `", "` gives `a`, ``, `b`). If `seps` is empty,
`s` is yielded once.

### `split_into`

```gamma
split_into :: fn(s, sep: string, parts: []string) -> usize
```

Splits `s` at `sep` into at most `parts.len` pieces, stored in `parts` in order; the
last piece takes the rest of `s`, separators included. Returns the number of pieces
written, which is fewer than `parts.len` when `s` has fewer separators. An empty `sep`
does not split. This is `split` for a known number of fields, without an iterator.

### `nth_field`

```gamma
nth_field :: fn(s: string, sep: string, n: usize) -> ?string
```

The piece number `n` (counting from 0) of `s` split at `sep` as `split` does, or
`null` if there are not that many pieces.

### `lines`

```gamma
lines :: fn(s: string) \ {Yield(string)}
```

The lines of `s`, split on `\n` with a `\r` before it dropped. A final line without
a terminator is yielded; there is no empty line after a terminating `\n`.

### `fields`

```gamma
fields :: fn(s: string) \ {Yield(string)}
```

The maximal runs of non-whitespace bytes of `s` (ASCII whitespace separates them);
empty fields are skipped.

### `eq_ignore_case`

```gamma
eq_ignore_case :: fn(a, b: string) -> bool
```

Whether `a` and `b` are equal when ASCII letters are compared without case.

### `to_lower`

```gamma
to_lower :: fn(s: string, buf: []u8) -> string
```

`s` with ASCII capitals made lowercase, written into `buf`; returns the part of
`buf` written. Traps if `buf` is shorter than `s`.

### `to_upper`

```gamma
to_upper :: fn(s: string, buf: []u8) -> string
```

`s` with ASCII lowercase letters made capitals, written into `buf`; see `to_lower`.

### `replace`

```gamma
replace :: fn(out: ^List(u8), s, from, to: string) -> AllocError!()
```

Appends `s` to `out` with every non-overlapping occurrence of `from` (found from the
left) replaced by `to`. Traps if `from` is empty.

### `replace_alloc`

```gamma
replace_alloc :: fn(s, from, to: string) -> AllocError![]u8 \ {Alloc}
```

`s` with every non-overlapping occurrence of `from` replaced by `to`, in memory from
the current allocator (release it with `free`). Traps if `from` is empty.

### `repeat`

```gamma
repeat :: fn(out: ^List(u8), s: string, n: usize) -> AllocError!()
```

Appends `n` copies of `s` to `out`.

### `parse_int`

```gamma
parse_int :: fn(T: type, s: string) -> ParseError!T where Int(T)
```

Parses the integer in `s` as a `T`.

The syntax is an optional sign (`+`, or `-` for signed types only), an optional
`0x`, `0o` or `0b` prefix (any case) and one or more digits of that base (decimal
without a prefix); a single `_` may separate two digits (`1_000`). Nothing else is
allowed, not even whitespace. The most negative value of a signed type parses. The
first problem found, scanning from the left, is the error.

### `parse_radix`

```gamma
parse_radix :: fn(T: type, s: string, base: u32) -> ParseError!T where Int(T)
```

Parses the integer in `s` as a `T` in the given `base`, from 2 to 36 (a larger or
smaller base traps). Digits above 9 are letters of either case. The syntax is
`parse_int`'s without the prefix: an optional sign, then one or more digits with
single `_` separators between them; `parse_radix(u8, "0xff", 16)` is an
`.InvalidDigit(at: 1)` because `x` is not a base 16 digit.

### `parse_float`

```gamma
parse_float :: fn(T: type, s: string) -> ParseError!T where Float(T)
```

Parses the decimal number in `s` as a `T` (`f16`, `f32` or `f64`).

The syntax is an optional sign, then either `inf`, `infinity` or `nan` in any case,
or digits with an optional fraction and an optional exponent: `12`, `1.5`, `.5`,
`5.`, `1e-3`, `6.02E+23`. There must be a digit in the mantissa and in the exponent,
and a single `_` may separate two digits (`1_000.5`). Nothing else is allowed, not
even whitespace; an unexpected byte, a missing digit or a misplaced `_` is
`.InvalidDigit` with the index (`s.len` when the string ends early).

The result is the float nearest to the exact value of the decimal number, with ties
to even, for any number of digits: subnormal values are exact and a number too large
for `T` is infinite (with its sign). A number too small for `T` is zero.

### `builder`

```gamma
builder :: fn(buf: []u8) -> Builder
```

A builder that appends to `buf`, starting empty.

### `push`

```gamma
push :: fn(b: ^Builder, s: string)
```

Appends the bytes of `s`. Traps if they do not fit.

### `push_byte`

```gamma
push_byte :: fn(b: ^Builder, c: u8)
```

Appends one byte. Traps if the builder is full.

### `str`

```gamma
str :: fn(b: ^const Builder) -> string
```

What has been appended so far (valid until the builder is next changed).

### `len`

```gamma
len :: fn(b: ^const Builder) -> usize
```

The number of bytes appended.

### `reset`

```gamma
reset :: fn(b: ^Builder)
```

Forgets everything appended, keeping the buffer.

### `write`

```gamma
write :: fn(A: type, b: ^Builder, template :: string, args: A = ())
```

Appends `args` formatted by `template`, as `print` would write them (§21.6.1):
`b.write("{} of {}", (n, total))`. Traps like `push` if the result does not fit.

### `format`

```gamma
format :: fn(A: type, template :: string, args: A = ()) -> AllocError![]u8 \ {Alloc}
```

Formatted text (§21.6.1) in memory from the current allocator (release it with
`free`): `str.format("{}-{:03}", (name, n))`. To append to a list instead, use
`List(u8).write`.

### `join`

```gamma
join :: fn(parts: []const string, sep: string) -> AllocError![]u8 \ {Alloc}
```

The `parts` with `sep` between them, in memory from the current allocator. The result
is a `[]u8` (it converts to a `string` where one is wanted) so that it can be given
back with `free`.
