# The standard library

The **core library** (spec §21) is visible in every module without an import: the
ambient effects, allocation, iterators, `List`, `HashMap`, formatting with `print`,
and the builtin methods of slices, numbers and pointers. The rest of the standard
library is a set of modules under `std/`, imported by path:

```gamma
io :: import "std/io"
fs :: import "std/fs"
```

The `gamma` tool finds them in the directory named by `$GAMMA_STD`, or in `lib/std`
next to the compiler's sources.

## Ambient effects and their handlers

`main` may perform any ambient effect (§12.9); the runtime installs the default
handlers, and tests run with the test handlers instead.

| Effect | Operations | Default handler | Test handler |
| --- | --- | --- | --- |
| `Out`, `Err` | `write_out`, `write_err` | stdout, stderr | captured |
| `In` | `read(buf) -> IoError!usize` (0 at the end) | stdin | empty input |
| `Clock` | `now() -> Instant`, `wall() -> i64`, `sleep(d)` | system clocks | fake clock; `sleep` advances it |
| `Rand` | `fill(buf)` | the OS's generator | fixed seed |
| `Fs` | files and directories (below) | the real file system | in-memory file system |
| `Net` | TCP connections (below) | real sockets | — |
| `Env` | `args()`, `var(name)`, `exit(code)` | process arguments and environment | empty |
| `Alloc` | `current() -> Allocator` | the system heap | leak-checking heap |

Durations are built with `ns(n)`, `us(n)`, `ms(n)` and `secs(n)`; `i.since(earlier)`,
`i.after(d)`, `d.plus(e)`, `d.minus(e)` and `d.to_ms()` work with them.

### Files: `Fs`

```gamma
f := try open("notes.txt", OpenOptions(read: false, write: true, create: true, truncate: true))
defer close(f)
_ = try f.write_to("hello\n")
```

`open(path, options)` returns a `File`; `read_from`, `write_to` and `seek` are called
as methods on it. `stat(path)` gives a `FileInfo` (`size`, `is_dir`, `modified`);
`remove`, `rename`, `make_dir` and `remove_dir` change the tree; `open_dir`,
`next_entry` (a name into your buffer, 0 after the last) and `close_dir` list a
directory without allocating. Failures are `IoError` values.

### The network: `Net`

```gamma
l := try listen("127.0.0.1", 0)          // any free port
port := try l.local_port()
c := try connect("localhost", port)
s := try l.accept()
_ = try c.send("ping")
n := try s.recv(buf[..])                 // 0 once the peer has closed
```

`close_socket` and `close_listener` release the handles. Hosts are names or address
literals; a failed name lookup is `.Other(0)`.

## `std/io`

- `io.reader()` and `r.next_line()`: read `In` a line at a time (the line is valid
  until the next call; lines longer than 4096 bytes come in pieces).
- `io.read_to_list(&list)`: all of `In` into a `List(u8)`.
- Handlers that redirect the ambient effects, so code written against `In` and
  `Out` works on files and strings unchanged:
  - `with io.FileIn(f: file) { … }`: `In` reads the file.
  - `with &io.FileOut(f: file) { … }`: `Out` and `Err` write to the file; the first
    error is kept in the handler's `err` field.
  - `with &io.StringIn(data: text) { … }`: `In` reads a string.
  - `with &io.Capture(buf: storage[..]) { … }`: `Out` writes into a buffer;
    `capture.text()` returns what was written.

## `std/fs`

- `fs.read_all(path) -> ReadError![]u8`: a whole file, in memory from the current
  allocator (release it with `free`).
- `fs.write_all(path, bytes)`, `fs.append(path, bytes)`, `fs.copy(from, to)`,
  `fs.exists(path)`, and `fs.write_all_to(file, bytes)`, which retries short writes.

## `std/fmt`

`print` writes each value one fixed way (§21.6.1). `std/fmt` provides the other
presentations, written into a buffer you supply and returned as a `string` slice of
it; nothing allocates, and a buffer that is too small traps. Size constants and
functions (`INT_BUF`, `max_int_len(T)`, `fixed_buf_len(d)`, …) say how large is enough.

```gamma
fmt :: import "std/fmt"

buf: [fmt.INT_BUF]u8 = ---
print("{}\n", fmt.prefixed(u32(255), 16, buf[..]))     // 0xff
fb: [400]u8 = ---
print("{}\n", fmt.fixed(2.675, 2, fb[..]))             // 2.67 (the exact binary value is below 2.675)
```

- Integers (any type from 8 to 128 bits): `dec`, `hex`, `hex_upper`, `oct`, `bin`,
  `radix(x, base)` for bases 2–36, `prefixed(x, base)` with `0x`/`0o`/`0b`, and
  `grouped(x, sep)` with thousands separators. Untyped literals need a type:
  `fmt.hex(u32(255), buf)`.
- Padding any formatted text: `pad_left`, `pad_right`, `center` and `zero_pad`
  (which keeps a sign or base prefix first: `-0042`, `0x00ff`).
- Floats: `fixed(x, decimals)`, `sci(x, digits)` and `general(x, precision)`, correctly
  rounded like C's `%.*f`, `%.*e` and `%.*g`.

## `std/c`

C's platform-dependent types (`c_int`, `c_long`, `c_size_t`, …) for declaring
foreign functions (§19.1).
