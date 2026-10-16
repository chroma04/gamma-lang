# Module `std/json`

[All modules](index.md)

JSON as in RFC 8259 (and ECMA-404): a parser into a value tree, and a writer.

    json :: import "std/json"

    doc := try json.parse("{\"name\": \"gamma\", \"tags\": [1, 2.5]}")
    defer doc.deinit()
    name := doc.root.get("name")                      // ?Value; .String("gamma")

    out := list(u8)
    defer out.deinit()
    try json.write(&out, doc.root, 2)                 // the tree again, indented

    w := json.writer(&out, 0)                         // or build output directly
    try w.begin_object()
    try w.key("id")
    try w.write_int(7)
    try w.end_object()                                // {"id":7}

Parsing. `parse` accepts exactly the texts the RFC's grammar describes: any JSON value
at the top level, surrounded by the four whitespace bytes (space, tab, line feed,
carriage return) and nothing else. It rejects everything the RFC rejects: trailing
commas, comments, single quotes, leading zeros and a leading `+` in numbers, `.5` and
`1.`, `NaN` and `Infinity`, unescaped control characters in strings, and unterminated
input. Further choices where the RFC leaves room:

  - The text must be valid UTF-8 inside strings (the RFC requires UTF-8); a byte order
    mark is not skipped.
  - A `\u` escape that is a lone surrogate, or a surrogate pair that is not a high
    followed by a low surrogate, is an error (`InvalidSurrogate`), since strings of
    this library are UTF-8. A correct pair is decoded to one scalar value.
  - Duplicate keys in an object are all kept, in order. `get` finds the last one, as
    most JSON implementations do.
  - Nesting deeper than `max_depth` (default 512) is an error, so hostile input cannot
    exhaust the stack.
  - A number keeps its literal text. It is converted on demand, with `to_i64` or
    `to_f64`, which say whether the conversion lost anything.

A parsed `Document` owns all its memory, taken from the allocator current when `parse`
was called, and gives it back at once with `deinit`. Strings and slices inside the tree
are valid until then.

Errors carry the byte offset of the problem; `position` turns an offset into a line and
column for messages.

## Structs

### `Number`

```gamma
Number :: struct {
    pub text: string
}
```

A JSON number, kept as its literal text (which the grammar has been checked for).

### `Member`

```gamma
Member :: struct {
    pub key: string
    pub value: Value
}
```

One member of an object.

### `Document`

```gamma
Document :: struct {
    pub root: Value
    // private fields omitted
}
```

A parsed text: the root value and the memory behind it.

### `Position`

```gamma
Position :: struct {
    pub line: usize
    pub column: usize
}
```

A line and a column, counting from 1; the column counts bytes.

### `Writer`

```gamma
Writer :: struct {
    // private fields omitted
}
```

Appends JSON text to a list. It tracks commas, colons, indentation and the nesting, so
the output is well formed as long as the calls are: values go where a value is
expected (at the top level, in an array, or after a `key`), and every `begin_` is
closed by its `end_`. Calls that break these rules, or nest deeper than
`MAX_WRITE_DEPTH`, trap.

## Unions

### `Value`

```gamma
Value :: union {
    Null
    Bool: bool
    Number: Number
    String: string
    Array: []Value
    Object: []Member
}
```

A JSON value.

### `ParseError`

```gamma
ParseError :: union {
    UnexpectedEnd: usize
    UnexpectedByte: usize
    InvalidNumber: usize
    InvalidEscape: usize
    InvalidSurrogate: usize
    ControlCharacter: usize
    InvalidUtf8: usize
    TrailingData: usize
    TooDeep: usize
    Memory: AllocError
}
```

Why parsing failed. Every payload is the byte offset of the problem in the input.

Variants:

- `UnexpectedEnd`: The input ended inside a value, or contains no value at all.
- `UnexpectedByte`: A byte that cannot start or continue the construct being read.
- `InvalidNumber`: A malformed number: a leading zero, a missing digit, a bare sign.
- `InvalidEscape`: A bad escape in a string: an unknown letter after the backslash, or a `\u` without
  four hexadecimal digits.
- `InvalidSurrogate`: A `\u` escape that is a lone surrogate (the offset of its backslash).
- `ControlCharacter`: A control character (below U+0020) in a string, which must be escaped.
- `InvalidUtf8`: A byte sequence in a string that is not UTF-8.
- `TrailingData`: Something other than whitespace after the value.
- `TooDeep`: Arrays and objects nested deeper than the limit.
- `Memory`: The memory could not be obtained.

### `WriteError`

```gamma
WriteError :: union {
    Memory: AllocError
    NonFinite
    InvalidUtf8
}
```

Why writing failed.

Variants:

- `Memory`: The output list could not grow.
- `NonFinite`: A float that is NaN or infinite, which JSON cannot represent.
- `InvalidUtf8`: A string that is not valid UTF-8.

## Constants

### `MAX_WRITE_DEPTH`

```gamma
MAX_WRITE_DEPTH :: 512
```

The deepest nesting a `Writer` supports. It equals the default `max_depth` of `parse`, so
every document `parse` accepts with the default limit can be written back; a document
parsed with a larger `max_depth` may be too deep for a `Writer`.

## Functions

### `position`

```gamma
position :: fn(text: string, offset: usize) -> Position
```

The line and column of byte `offset` in `text` (an offset past the end is the end).
A line ends at a line feed.

### `get`

```gamma
get :: fn(v: Value, key: string) -> ?Value
```

The value of member `key` of an object, or `null` if `v` is not an object or has no such
member. With duplicate keys, the last one.

### `at`

```gamma
at :: fn(v: Value, i: usize) -> ?Value
```

The element `i` of an array, or `null` if `v` is not an array or `i` is out of range.

### `deinit`

```gamma
deinit :: fn(d: ^Document)
```

Releases the document's memory. The root and everything reached from it are invalid
afterwards.

### `to_i64`

```gamma
to_i64 :: fn(n: Number) -> ?i64
```

The number as an `i64`, if it denotes an integer in range: `12`, `-0`, `1e3` and
`5.0` do; `1.5`, `1e-1` and `9223372036854775808` do not.

### `to_f64`

```gamma
to_f64 :: fn(n: Number) -> (value: f64, exact: bool)
```

The number as an `f64`, correctly rounded, and whether nothing was lost: `exact` is
true when the `f64` prints (shortest round-trip form) as the same decimal number as the
literal, so `0.1` and `1e3` are exact and `0.1000000000000000000001`,
`9007199254740993` and `1e400` (which is infinite) are not. A literal that is zero
gives 0 or -0 and is exact.

### `parse`

```gamma
parse :: fn(text: string, max_depth: usize = 512) -> ParseError!Document needs Alloc
```

Parses `text`, which must be one JSON value.

### `writer`

```gamma
writer :: fn(out: ^List(u8), indent: usize = 0) -> Writer
```

A writer appending to `out`. With `indent` 0 the output is compact; otherwise each
element goes on its own line, indented by `indent` spaces per level, and a space follows
each colon.

### `begin_object`

```gamma
begin_object :: fn(w: ^Writer) -> WriteError!()
```

Starts an object.

### `begin_array`

```gamma
begin_array :: fn(w: ^Writer) -> WriteError!()
```

Starts an array.

### `end_object`

```gamma
end_object :: fn(w: ^Writer) -> WriteError!()
```

Ends the innermost object, which must be open and have no key waiting for its value.

### `end_array`

```gamma
end_array :: fn(w: ^Writer) -> WriteError!()
```

Ends the innermost array, which must be open.

### `key`

```gamma
key :: fn(w: ^Writer, name: string) -> WriteError!()
```

Writes an object member's name; its value follows.

### `write_null`

```gamma
write_null :: fn(w: ^Writer) -> WriteError!()
```

Writes `null`.

### `write_bool`

```gamma
write_bool :: fn(w: ^Writer, b: bool) -> WriteError!()
```

Writes `true` or `false`.

### `write_string`

```gamma
write_string :: fn(w: ^Writer, s: string) -> WriteError!()
```

Writes a string, escaping `"`, `\` and the control characters. Everything else,
non-ASCII text included, is written as it is. The string must be valid UTF-8.

### `write_int`

```gamma
write_int :: fn(T: type, w: ^Writer, x: T) -> WriteError!() where Int(T)
```

Writes an integer of any size.

### `write_float`

```gamma
write_float :: fn(T: type, w: ^Writer, x: T) -> WriteError!() where Float(T)
```

Writes a float in the shortest form that reads back as the same value (the form
`print` uses), so `2.0` is `2` and `1e21` is `1e21`. NaN and infinities are an error.

### `write_number`

```gamma
write_number :: fn(w: ^Writer, n: Number) -> WriteError!()
```

Writes a number literal as it is. The text must be a valid JSON number, such as the
`text` of a parsed `Number` (anything else makes the output invalid JSON).

### `write_value`

```gamma
write_value :: fn(w: ^Writer, v: Value) -> WriteError!()
```

Writes a whole value tree.

### `write`

```gamma
write :: fn(out: ^List(u8), v: Value, indent: usize = 0) -> WriteError!()
```

Appends the JSON text of `v` to `out`: compact when `indent` is 0, otherwise indented
as for `writer`.
