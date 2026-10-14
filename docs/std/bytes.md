# Module `std/bytes`

[All modules](index.md)

Reading and writing binary data.

Integers and floats are converted to and from bytes in the order you choose, so the
same code works on every target. The functions here take a byte slice and read or write
at its start; `Reader` is a cursor for taking a record apart field by field.

    bytes :: import "std/bytes"
    n := bytes.read_int(u32, header[0..4], .Big)
    r := bytes.Reader(data: packet)
    kind := r.byte() else return !.Short
    len := r.int(u16, .Little) else return !.Short
    body := r.take(usize(len)) else return !.Short

Integers are stored in `@size_of(T)` bytes, so `T` should be 8, 16, 32, 64 or 128
bits wide (an odd width such as `u24` occupies the next power of two). Nothing here
allocates except `append_int`, which grows the list you pass.

## Structs

### `Reader`

```gamma
Reader :: struct {
    pub data: []const u8
    pub pos: usize = 0
}
```

A cursor over `data`: `pos` is the number of bytes taken so far. Each function below
returns `null` when fewer bytes remain than it needs, and then leaves `pos` as it was.

## Unions

### `Endian`

```gamma
Endian :: union {
    Little
    Big
}
```

The order of the bytes of a stored number.

Variants:

- `Little`: The least significant byte first.
- `Big`: The most significant byte first.

## Functions

### `read_int`

```gamma
read_int :: fn(T: type, b: []const u8, e: Endian) -> T where Int(T)
```

The `T` stored in the first `@size_of(T)` bytes of `b`. Traps if `b` is shorter.

### `write_int`

```gamma
write_int :: fn(T: type, b: []u8, x: T, e: Endian) where Int(T)
```

Stores `x` in the first `@size_of(T)` bytes of `b`. Traps if `b` is shorter.

### `append_int`

```gamma
append_int :: fn(T: type, out: ^List(u8), x: T, e: Endian) -> AllocError!() where Int(T)
```

Appends the `@size_of(T)` bytes of `x` to `out`.

### `read_f32`

```gamma
read_f32 :: fn(b: []const u8, e: Endian) -> f32
```

The `f32` stored in the first 4 bytes of `b` (IEEE 754 binary32). Traps if `b` is
shorter.

### `read_f64`

```gamma
read_f64 :: fn(b: []const u8, e: Endian) -> f64
```

The `f64` stored in the first 8 bytes of `b` (IEEE 754 binary64). Traps if `b` is
shorter.

### `write_f32`

```gamma
write_f32 :: fn(b: []u8, x: f32, e: Endian)
```

Stores `x` in the first 4 bytes of `b`. Traps if `b` is shorter.

### `write_f64`

```gamma
write_f64 :: fn(b: []u8, x: f64, e: Endian)
```

Stores `x` in the first 8 bytes of `b`. Traps if `b` is shorter.

### `take`

```gamma
take :: fn(r: ^Reader, n: usize) -> ?[]const u8
```

The next `n` bytes, a slice of `data`.

### `int`

```gamma
int :: fn(T: type, r: ^Reader, e: Endian) -> ?T where Int(T)
```

The next `@size_of(T)` bytes as a `T`.

### `byte`

```gamma
byte :: fn(r: ^Reader) -> ?u8
```

The next byte.

### `rest`

```gamma
rest :: fn(r: ^const Reader) -> []const u8
```

The bytes not yet taken (a slice of `data`).

### `skip`

```gamma
skip :: fn(r: ^Reader, n: usize) -> bool
```

Skips `n` bytes; `false` (and no change) if fewer remain.
