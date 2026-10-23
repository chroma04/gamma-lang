# Module `std/text`

[All modules](index.md)

Unicode text segmentation, and UTF-8 and UTF-16 encoding of scalar values.

A `string` carries no encoding guarantee; these functions read it as UTF-8 and treat
each maximal ill-formed subsequence as one U+FFFD, exactly like `runes`. They never
allocate, and the strings they return are slices of their argument, so ill-formed
bytes come back unchanged.

    text :: import "std/text"
    for g in text.graphemes("e\u{301}🇫🇷") { … }   // "e\u{301}", then "🇫🇷"

Grapheme clusters are the extended grapheme clusters of Unicode Standard Annex #29,
"Unicode Text Segmentation", for the Unicode version in `UNICODE_VERSION`.

## Constants

### `UNICODE_VERSION`

```gamma
UNICODE_VERSION :: "16.0.0"
```

The version of the Unicode Standard that grapheme clusters follow.

## Functions

### `grapheme_len`

```gamma
grapheme_len :: fn(s: string) -> usize
```

The byte length of the first grapheme cluster of `s`: 0 if `s` is empty, and at
least 1 otherwise. `s[0..grapheme_len(s)]` is the cluster.

### `graphemes`

```gamma
graphemes :: fn(s: string) needs Yield(string)
```

The grapheme clusters of `s`, in order: what a reader sees as one character, such
as `e` with a combining accent, a flag, or an emoji sequence joined with ZWJs. `\r\n`
is one cluster. Every byte of `s` is in exactly one cluster.

### `decode_rune`

```gamma
decode_rune :: fn(s: string) -> ?(rune: u32, len: usize)
```

The first Unicode scalar value of `s` and the number of bytes it takes, or `null` if
`s` is empty or does not start with a well-formed UTF-8 sequence (a stray
continuation byte, a truncated or overlong sequence, a surrogate, or a value above
U+10FFFF). Unlike `runes`, ill-formed input is reported, not replaced.

### `encode_rune`

```gamma
encode_rune :: fn(cp: u32, buf: []u8) -> ?string
```

Writes the UTF-8 encoding of `cp` at the start of `buf` and returns the bytes written,
a slice of `buf`; `null` if `cp` is a surrogate (U+D800 to U+DFFF) or above U+10FFFF.
Traps if `buf` is shorter than 4 bytes.

### `append_rune`

```gamma
append_rune :: fn(out: ^List(u8), cp: u32) -> AllocError!()
```

Appends the UTF-8 encoding of `cp` to `out`. Traps if `cp` is a surrogate or above
U+10FFFF.

### `utf16_len`

```gamma
utf16_len :: fn(s: string) -> usize
```

The number of UTF-16 code units that `s` encodes to: 1 for each scalar value below
U+10000 and 2 for each above. `s` is read like `runes` reads it, so each ill-formed
subsequence counts as U+FFFD, one unit.

### `encode_utf16`

```gamma
encode_utf16 :: fn(s: string, buf: []u16) -> []u16
```

Writes the UTF-16 encoding of `s` (read like `runes` reads it, so ill-formed bytes
become U+FFFD) at the start of `buf` and returns the code units written, a slice of
`buf`. Traps if `buf.len` is less than `utf16_len(s)`.

### `append_utf16`

```gamma
append_utf16 :: fn(out: ^List(u16), s: string) -> AllocError!()
```

Appends the UTF-16 encoding of `s` (read like `runes` reads it) to `out`.

### `decode_utf16`

```gamma
decode_utf16 :: fn(units: []const u16) needs Yield(rune)
```

The Unicode scalar values of the UTF-16 text `units`, in order. A high surrogate
followed by a low one is one value above U+FFFF; every unpaired surrogate (a lone
one, or a pair in the wrong order) yields U+FFFD, so the values are always scalar
values and `append_rune` accepts them. Building UTF-8 from UTF-16 is that call:

    out := list(u8)
    defer out.deinit()
    for r in text.decode_utf16(units) { try text.append_rune(&out, r) }
