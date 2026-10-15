# Module `std/io`

[All modules](index.md)

Reading lines from `In`, and handlers that connect `In` and `Out` to files
and strings, so code written against the ambient effects works on any of them.

    with io.FileIn(f: file) { count_words() }      // `In` reads the file
    out := io.file_out(file)                        // `print` writes to the file, buffered
    with &out { report() }
    try out.flush()                                  // ... so flush before closing the file
    src := io.StringIn(data: "a\nb\n")
    with &src { … }                                  // `In` reads a string
    lo := io.ListOut(list: &text)                    // `print` appends to a list
    with &lo { report() }

## Structs

### `Reader`

```gamma
Reader :: struct {
    // private fields omitted
}
```

Reads from `In` through a buffer, a line at a time. `reader` reads through a buffer
of 4096 bytes kept inside the `Reader`; `reader_with` reads through a buffer you supply.

### `FileIn`

```gamma
FileIn :: struct {
    pub f: File
}
```

A handler for `In` that reads the file `f`.

### `FileOut`

```gamma
FileOut :: struct {
    pub f: File
    pub err: ?IoError = null
    // private fields omitted
}
```

A handler for `Out` (and `Err`) that writes to the file `f`, through a buffer of 4096
bytes: the file is written to only when the buffer is full (or when a single piece of
output is larger than the buffer), so most output is not written until `flush`. Call
`flush` after the `with` block and before the file is closed, or output is lost.

`Out` cannot fail, so the first error is kept in `err` and later output is dropped;
`flush` reports it. Create one with `file_out` and install it by address:

    out := io.file_out(f)
    with &out { report() }
    try out.flush()

### `StringIn`

```gamma
StringIn :: struct {
    pub data: string
    pub pos: usize = 0
}
```

A handler for `In` that reads the string `data`.

### `Capture`

```gamma
Capture :: struct {
    pub buf: []u8
    pub len: usize = 0
}
```

A handler for `Out` that keeps output in the buffer `buf` (up to its length; the
rest is dropped). `text()` gives what was written.

### `ListOut`

```gamma
ListOut :: struct {
    pub list: ^List(u8)
    pub err: ?AllocError = null
}
```

A handler for `Out` (and `Err`) that appends to the list `list`, so any code that
prints can build a growable string. The first allocation failure is kept in `err` and
later output is dropped.

## Unions

### `ReadError`

```gamma
ReadError :: union {
    Io: IoError
    Alloc: AllocError
}
```

Why `read_all` failed.

## Handlers

### `handler In for FileIn`

```gamma
handler In for FileIn \ {Fs}
```

### `handler Out for ^FileOut`

```gamma
handler Out for ^FileOut \ {Fs}
```

### `handler Err for ^FileOut`

```gamma
handler Err for ^FileOut \ {Fs}
```

### `handler In for ^StringIn`

```gamma
handler In for ^StringIn
```

### `handler Out for ^Capture`

```gamma
handler Out for ^Capture
```

### `handler Out for ^ListOut`

```gamma
handler Out for ^ListOut
```

### `handler Err for ^ListOut`

```gamma
handler Err for ^ListOut
```

## Functions

### `reader`

```gamma
reader :: fn() -> Reader
```

A reader with an empty 4096-byte buffer of its own.

### `reader_with`

```gamma
reader_with :: fn(buf: []u8) -> Reader
```

A reader that reads through `buf`, which sets the longest line it can return (see
`next_line`). Use it for input with long lines. `buf` must not be empty and must stay
valid while the reader is used.

### `next_line`

```gamma
next_line :: fn(r: ^Reader) -> IoError!?string \ {In}
```

The next line, without its `\n` (or `\r\n`), or `null` at the end of the input. The
line is valid until the next call.

A line has to fit the reader's buffer together with its `\n`: with the default buffer
the longest line is 4095 bytes. A longer line is reported as `.InvalidInput`; the call
after that skips the rest of that line and goes on with the next one, so a caller that
only wants to report the problem can simply continue. Use `reader_with` to read
longer lines.

### `read_to_list`

```gamma
read_to_list :: fn(out: ^List(u8)) -> IoError!() \ {In}
```

Reads all of `In` into `out`, appending. A failure to grow the list is `.NoSpace`.

### `read_all`

```gamma
read_all :: fn() -> ReadError![]u8 \ {In, Alloc}
```

All of `In`, in memory from the current allocator; release it with `free(data)`.

### `file_out`

```gamma
file_out :: fn(f: File) -> FileOut
```

A buffered output handler for the file `f`, with nothing buffered yet.

### `flush`

```gamma
flush :: fn(o: ^FileOut) -> IoError!() \ {Fs}
```

Writes out the buffered output. Returns the first error that has happened on the file,
in this call or earlier; it stays in `err`.

### `text`

```gamma
text :: fn(c: ^const Capture) -> string
```

What was written to the capture so far.

### `describe`

```gamma
describe :: fn(e: IoError) -> string
```

A short, lower-case description of `e`, for messages such as `error: {}`.
