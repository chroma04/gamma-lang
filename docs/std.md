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
| `Net` | TCP connections (below) | real sockets | loopback fake: an in-memory network with one host (`localhost`, `127.0.0.1` and `::1`); nothing blocks, so `accept` and `recv` give `WouldBlock` instead of waiting |
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
  until the next call). A line must fit the reader's buffer together with its `\n`,
  which holds 4095 bytes of text by default; a longer line is the error `.InvalidInput`
  (reported once: the next call skips the rest of that line and goes on).
  `io.reader_with(buf)` reads through a buffer you supply instead, for long lines.
- `io.read_to_list(&list)`: all of `In` into a `List(u8)`.
- `io.read_all() -> io.ReadError![]u8`: all of `In`, in memory from the current
  allocator (release it with `free`). `io.ReadError` is `.Io(IoError)` or
  `.Alloc(AllocError)`, like `fs.ReadError`.
- Handlers that redirect the ambient effects, so code written against `In` and
  `Out` works on files and strings unchanged:
  - `with io.FileIn(f: file) { … }`: `In` reads the file.
  - `with &out { … }` where `out := io.file_out(file)`: `Out` and `Err` write to the
    file through a 4096-byte buffer, so output reaches the file only when the buffer
    fills (or for a single piece larger than the buffer) and at `out.flush()`. Always
    flush after the `with` block and before closing the file. The first error is kept in
    the handler's `err` field, later output is dropped, and `flush` returns the kept error.
  - `with &input { … }` where `input := io.StringIn(data: text)`: `In` reads a string.
  - `with &cap { … }` where `cap := io.Capture(buf: storage[..])`: `Out` writes into a
    buffer; `cap.text()` returns what was written.
  - `with &lo { … }` where `lo := io.ListOut(list: &text)`: `Out` and `Err` append to a
    `List(u8)`, so any code that prints can build a growable string. The first
    allocation failure is kept in `lo.err` and later output is dropped.

  A handler that keeps state (`FileOut`, `StringIn`, `Capture`, `ListOut`) is installed by
  address, so it must first be bound to a name.

```gamma
f := try open("report.txt", OpenOptions(read: false, write: true, create: true))
defer close(f)
out := io.file_out(f)
with &out { write_report() }
try out.flush()
```

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

- Integers (any type of 8 bits or more, including `u256` and wider; `INT_BUF` bytes suffice up
  to 128 bits and `max_int_len(T)` for any `T`): `dec`, `hex`, `hex_upper`, `oct`, `bin`,
  `radix(x, base)` for bases 2–36, `prefixed(x, base)` with `0x`/`0o`/`0b`, and
  `grouped(x, sep)` with thousands separators. Untyped literals need a type:
  `fmt.hex(u32(255), buf)`.
- Padding any formatted text: `pad_left`, `pad_right`, `center` and `zero_pad`
  (which keeps a sign or base prefix first: `-0042`, `0x00ff`).
- Floats: `fixed(x, decimals)`, `sci(x, digits)` and `general(x, precision)`, correctly
  rounded like C's `%.*f`, `%.*e` and `%.*g`.

## `std/str`

Byte-oriented string utilities. A `string` is a `[]const u8`, so indices are byte
indices and case rules are ASCII only. Strings are slices, which have no user-defined
methods, so these are called as functions: `str.trim(s)`. Results are slices of the
arguments (or of a buffer you supply); only `join` allocates.

```gamma
str :: import "std/str"

for line in str.lines(text) {
    if kv := str.split_once(str.trim(line), "=") {
        port := str.parse_int(u16, str.trim(kv.after)) else 8080
        print("{} -> {}\n", (kv.before, port))
    }
}
```

- Trimming: `trim`, `trim_start`, `trim_end` (ASCII whitespace: space, `\t`, `\n`, `\r`,
  `\v`, `\f`).
- Searching: `find(s, needle) -> ?usize` (an empty needle is found at 0), `rfind` (an
  empty needle is found at `s.len`) and `contains`.
- Splitting: `split_once(s, sep) -> ?(before: string, after: string)` and the iterators
  `split(s, sep)` (adjacent or terminal separators give empty pieces; an empty `sep`
  yields `s` once), `lines(s)` (splits on `\n`, drops a `\r` before it, no empty line
  after a final `\n`) and `fields(s)` (runs of non-whitespace).
- Case: `eq_ignore_case(a, b)`, and `to_lower(s, buf)` and `to_upper(s, buf)`, which
  write into `buf` and return the part used; a buffer shorter than `s` traps.
- `parse_int(T, s) -> ParseError!T` for any integer type: an optional sign (`-` only
  for signed types), an optional `0x`, `0o` or `0b` prefix, digits with single `_`
  separators between them, and nothing else. The most negative value of a signed type
  parses. Errors are `ParseError`: `.Empty`, `.InvalidDigit(at: index)` (a bad
  character, a misplaced `_`, or missing digits, in which case `at` is the index where
  a digit was expected) and `.Overflow`. The first problem found from the left wins.
- `Builder`: appends into a buffer you supply, allocating nothing.
  `b := str.builder(buf[..])`, then `b.push(s)`, `b.push_byte(c)`, `b.str()`,
  `b.len()` and `b.reset()`. `b.write(template, args)` appends formatted text, exactly as
  `print` would write it: `b.write("{} of {}", (n, total))`. Appending beyond the buffer traps.
- `join(parts, sep) -> AllocError![]u8`: the parts with `sep` between them in memory
  from the current allocator; release it with `free`.

## `std/text`

Unicode text segmentation. Strings are read as UTF-8, with each maximal ill-formed
subsequence taken as one U+FFFD as `runes` does; results are slices of the argument, so
ill-formed bytes come back unchanged, and nothing allocates.

```gamma
text :: import "std/text"

n: usize = 0
for g in text.graphemes("e\u{301}🇫🇷👨\u{200d}👩\u{200d}👧") { n += 1 }   // 3
```

- `graphemes(s)` yields the extended grapheme clusters of `s` (Unicode Standard Annex
  #29): what a reader sees as one character, such as a letter with combining marks, a
  flag, a Hangul syllable written in jamo, an Indic conjunct or an emoji ZWJ sequence.
  `\r\n` is one cluster. Every byte of `s` is in exactly one cluster.
- `grapheme_len(s)` is the byte length of the first cluster of `s` (0 if `s` is empty),
  for stepping through text without an iterator.
- `UNICODE_VERSION` is the version of the Unicode Standard the rules and tables follow
  (currently `"16.0.0"`).

## `std/collections`

Three containers beyond `List` and `HashMap`, each in its own module, because their
functions are methods and a module has one namespace: `std/collections/deque`,
`std/collections/heap` and `std/collections/bitset`. They follow the conventions of
`List`: construction captures the current allocator, nothing allocates afterwards,
growth returns `AllocError!()`, `deinit` frees through the stored allocator, and the
fields are private.

```gamma
dq :: import "std/collections/deque"
hp :: import "std/collections/heap"
bs :: import "std/collections/bitset"

d := dq.deque(u32)                    // a queue for a breadth-first search
defer d.deinit()
try d.push_back(1)
while x := d.pop_front() { … }

h := hp.heap_by(Job, fn(a, b: ^const Job) -> bool => a.cost < b.cost)
defer h.deinit()
try h.push(job)

seen := try bs.bitset(1000)           // members are 0..1000
defer seen.deinit()
seen.insert(42)
```

**`deque`**: a growable ring buffer; both ends take constant (amortized) time.

- `deque(T)`, `push_back(x)`, `push_front(x)` (`AllocError!()`), `pop_back()`,
  `pop_front()` (`?T`), `front()`, `back()` (`?T`), `get(i) -> ?T`,
  `get_ptr(i) -> ?^T` (valid until the next push or `reserve`), `len()`, `is_empty()`,
  `capacity()`, `reserve(extra)`, `clear()`, `deinit()`.
- `items()` yields the elements front to back (`for x in d.items()`); `c := d.cursor()`
  and `c.next() -> ?T` advance through it in step with other sequences.

**`heap`**: a binary heap; the element that comes first is popped first.

- `heap(T)` (needs `Ord(T)`) pops the smallest by `<`. `heap_by(T, less)` takes
  `less: ^fn(a, b: ^const T) -> bool`, true when `a` must come out before `b`: a named
  function or a lambda that captures nothing. Both give the same type, `Heap(T)`.
  `heap_from(T, xs)` and `heap_from_by(T, less, xs)` build a heap from a slice in
  O(n) (`AllocError!Heap(T)`).
- `push(x)` (`AllocError!()`), `pop() -> ?T`, `peek() -> ?T`, `len()`, `is_empty()`,
  `clear()`, `deinit()`.

**`bitset`**: a set of the integers below a capacity fixed at creation, one bit each in
64-bit words. Operations after construction cannot fail.

- `bitset(n) -> AllocError!BitSet`, `capacity()`, `insert(i)` (traps if `i >= n`),
  `remove(i)` and `contains(i)` (both accept any `i`; integers beyond `n` are not
  members), `count()` (popcount), `is_empty()`, `clear()`, `deinit()`.
- `union_with(other)` and `intersect_with(other)` update the receiver in place; the
  capacities may differ, but `union_with` traps if `other` has a member the receiver
  cannot hold.
- `items()` yields the members in increasing order; `next_member(from) -> ?usize` is the
  smallest member at least `from`.

## `std/arena` and `std/fixed_buffer`

Allocators to install with `with` (§15.2):

```gamma
arena :: import "std/arena"
fixed_buffer :: import "std/fixed_buffer"

a := arena.new()                  // chunks come from the current allocator
defer a.deinit()                  // gives them all back
with a.allocator() { … }

store: [4096]u8 = ---
fb := fixed_buffer.over(store[..])
with fb.allocator() { … }         // allocations come from `store`; fb.reset() reuses it
```

Both free or resize only their most recent allocation; everything else stays until
`deinit` (an arena) or `reset` (a fixed buffer). A full fixed buffer fails with
`OutOfMemory`; an arena takes a bigger chunk from its parent.

## `std/math`

Elementary functions on `f64`, written in Gamma (ports of the FreeBSD/fdlibm algorithms),
so results are bit-for-bit the same in the interpreter, in compiled code and at compile
time. Every function is within 1 ulp of the exact result (`pow`: within 2 ulp); special values
follow C99 Annex F.

- Constants: `pi`, `e`, `tau`, `ln2`, `ln10`, `sqrt2`, `inf`, `nan`.
- `sqrt`, `cbrt`, `hypot`; `exp`, `exp2`, `expm1`, `log`, `log2`, `log10`, `log1p`, `pow`.
- `sin`, `cos`, `tan` (with full argument reduction), `asin`, `acos`, `atan`, `atan2`;
  `sinh`, `cosh`, `tanh`.
- `floor`, `ceil`, `trunc`, `round` (half away from zero, as in C; the builtin `x.round(T)` rounds ties to even), `fmod`/`rem`, `copysign`, `abs`.

## `std/thread`

Threads (§19.6). A thread runs a plain function `^fn(arg: rawptr) \ {Sys}` and starts with
no handlers; `thread.defaults(fn() { … })` runs a closure under the default handlers of
every ambient effect.

```gamma
thread :: import "std/thread"

work :: fn(arg: rawptr) \ {Sys} {
    counter := arg.cast(^u64)
    _ = @atomic_add(counter, 1, .Relaxed)
    thread.defaults(fn() { print("done\n") })
}

n: u64 = 0
t := try thread.spawn(work, (&n).cast(rawptr))
t.join()
```

- `spawn(f, arg) -> IoError!Thread`, `t.join()`, `yield_now()`.
- `Mutex`: `mutex()`, `m.lock()`, `m.try_lock()`, `m.unlock()`.
- Shared memory needs atomics or a mutex; a data race is undefined behavior. Handlers,
  second-class functions and continuations never cross threads.
- The default handlers are also available directly, for code that starts without any:
  `heap_allocator()`, `system_io()` (`In`, `Clock`, `Rand`, `Fs`, `Net`) and
  `system_output()` (`Out`, `Err`).

The interpreter runs threads one at a time, switching between them often and whenever
one blocks, so programs behave as they do compiled, without data races in the
interpreter itself.

## `std/c`

C's platform-dependent types (`c_int`, `c_long`, `c_size_t`, …) for declaring
foreign functions (§19.1).

## `std/path`

POSIX path strings with `/` as the separator. Every function is lexical: it never
looks at the file system, performs no effect, and returns a slice of its argument (or of
a buffer you supply).

```gamma
path :: import "std/path"

path.base_name("/usr/lib/libc.so.6")       // "libc.so.6"
path.dir_name("/usr/lib/libc.so.6")        // "/usr/lib"
path.extension("libc.so.6")                // "6"
buf: [64]u8 = ---
path.clean("a//b/./c/../d", buf[..])       // "a/b/d"
```

- `path.SEP` is `'/'`; `is_absolute(p)` tests for a leading `/`.
- `base_name(p)` is the last component, ignoring trailing slashes (`a/b/` gives `b`, `/`
  gives `/`, the empty path gives the empty path). `dir_name(p)` is the rest, as POSIX
  `dirname` has it (`a/b` gives `a`, `a` gives `.`, `/a` gives `/`, `a/b/` gives `a`).
- `extension(p)` is the part of the base name after its last dot, without the dot; it is
  empty when there is none, for a leading dot (`.bashrc`) and for a trailing one (`a.`).
  `stem(p)` is the base name without the dot and extension.
- `components(p)` iterates over the non-empty components (an absolute path yields `/`
  first); `.` components are skipped, and a relative path of nothing but `.` yields a
  single `.`.
- `clean(p, buf)` normalises like Go's `path.Clean`: repeated slashes collapse, `.` goes,
  `x/..` is resolved, `..` above the root of an absolute path is dropped, trailing
  slashes go, and an empty result is `.`. The result is written to `buf`, which must
  hold at least `max(p.len, 1)` bytes and not overlap `p`.
- `join(&list, parts)` appends the parts to a `List(u8)` separated by single slashes:
  empty parts are skipped, an absolute later part does not restart the path (its leading
  slashes are dropped), and nothing else is cleaned. `join_alloc(parts)` returns the same
  text in new memory (release it with `free`).
- `relative_to(p, base) -> ?string` is what remains of `p` after `base` and a slash when
  `p` is inside `base` component by component (`a/bc` is not inside `a/b`), `.` when they
  are equal, and `null` otherwise. It compares the text as written.

## `std/bytes`

Binary data: integers and floats in a chosen byte order, and a cursor for taking records
apart.

```gamma
bytes :: import "std/bytes"

n := bytes.read_int(u32, header[0..4], .Big)
r := bytes.Reader(data: packet)
kind := r.byte() else return !.Short
len := r.int(u16, .Little) else return !.Short
body := r.take(usize(len)) else return !.Short
```

- `Endian` is `.Little` or `.Big`.
- `read_int(T, b, e)` reads a `T` from the first `@size_of(T)` bytes of `b`, and
  `write_int(T, b, x, e)` stores one; both trap if `b` is too short. `append_int(T, &list,
  x, e)` appends the bytes to a `List(u8)` (`AllocError!()`). `T` is any integer type that
  fills its storage (8, 16, 32, 64 or 128 bits).
- `read_f32`, `read_f64`, `write_f32` and `write_f64` do the same for IEEE 754 floats.
- `Reader` is a cursor, `bytes.Reader(data: bytes)` with a public `pos`. Its functions
  return `null` when too few bytes remain and then leave `pos` alone: `take(n)` gives the
  next `n` bytes as a slice of `data`, `int(T, e)` an integer, `byte()` one byte;
  `skip(n)` returns `false` instead; `rest()` is what has not been taken.

## `std/encoding/base64`

Base64 (RFC 4648 sections 4 and 5), into a buffer you supply or appended to a list.

```gamma
base64 :: import "std/encoding/base64"

buf: [64]u8 = ---
text := base64.encode("foobar", buf[..], base64.Options())      // "Zm9vYmFy"
raw: [48]u8 = ---
data := try base64.decode(text, raw[..], base64.Options())      // "foobar"
```

- `Options(alphabet: .Standard | .Url, pad: true)`; the default is the standard alphabet
  with `=` padding. `Alphabet` is `.Standard` (`+` `/`) or `.Url` (`-` `_`).
- `encoded_len(n, o)` is the length of the encoding of `n` bytes. `encode(src, dst, o)`
  writes it into `dst` (which must be that long; a trap otherwise) and returns the part
  written; `encode_append(&list, src, o)` appends to a `List(u8)`.
- `decoded_len_max(n)` is the buffer size that always holds the decoding of `n`
  characters. `decode(src, dst, o) -> DecodeError![]u8` needs `dst` of that size.
  `decode_append(&list, src, o)` appends, with the error type `AppendError`, which is
  `.Decode(DecodeError)` or `.Alloc(AllocError)`; nothing is appended if `src` is invalid.
- With `pad: true` decoding requires the padding; with `pad: false` it is optional.
  Only canonical encodings are accepted: the unused bits of the last character must be zero.
- `DecodeError`: `.InvalidByte(i)` for a byte outside the alphabet; `.InvalidLength` for a
  count of characters no data produces (a lone character at the end, or missing padding
  with `pad: true`); `.InvalidPadding(i)` for a misplaced or surplus `=` (at the first
  offending one) or non-zero unused bits (at the last data character). Positions are
  byte offsets in `src`. Checks run in the order bytes, length, padding.

## `std/encoding/hex`

Hexadecimal (RFC 4648 section 8), two digits per byte.

```gamma
hex :: import "std/encoding/hex"

buf: [16]u8 = ---
text := hex.encode("Hi!", buf[..], false)           // "486921"
raw: [8]u8 = ---
data := try hex.decode("4869", raw[..])             // "Hi"
```

- `encode(src, dst, upper)` writes `2 * src.len` digits into `dst` (a trap if it is
  shorter), capital letters when `upper`, and returns the part written;
  `encode_append(&list, src, upper)` appends to a `List(u8)`.
- `decode(src, dst) -> DecodeError![]u8` accepts either case and needs `dst` to hold
  `src.len / 2` bytes; `decode_append(&list, src)` appends, with the error type
  `AppendError` (`.Decode(DecodeError)` or `.Alloc(AllocError)`), appending nothing if
  `src` is invalid.
- `DecodeError` is `.InvalidByte(i)` (the offset of the first bad digit) or `.OddLength`;
  a bad digit is reported before an odd length.
