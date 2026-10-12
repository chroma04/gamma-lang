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

Template placeholders cover widths, alignment, signs, zero padding, bases 2, 8 and 16,
float precision and scientific notation (`{:>8}`, `{:08.3}`, `{:#x}`, `{:.2e}`; §21.6.1).
`std/fmt` provides presentations they do not (other bases, digit grouping, `%g`-style
general form) and text you need as a value rather than as output, written into a buffer
you supply and returned as a `string` slice of it; nothing allocates, and a buffer that is too small traps. Size constants and
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
arguments (or of a buffer you supply); only `join`, `dupe`, `replace_alloc` and the
functions that append to a `List(u8)` allocate.

```gamma
str :: import "std/str"

for line in str.lines(text) {
    if kv := str.split_once(str.trim(line), "=") {
        port := str.parse_int(u16, str.trim(kv.after)) else 8080
        print("{} -> {}\n", (kv.before, port))
    }
}
```

**Already in the language, so not repeated here:** the builtin methods of slices (spec
§21.3) work on strings: `s.starts_with(p)`, `s.ends_with(p)`, `s.index_of(byte)`,
`s.first()`, `s.last()`, `s.get(i)` and `s.utf8_valid()`; `s is t`
compares for equality, and `<`, `<=`, `>`, `>=` order strings lexicographically by byte;
`bytes(s)` and `runes(s)` iterate. Use `strip_prefix` below when you also want the rest
of the string.

- Trimming: `trim`, `trim_start`, `trim_end` (ASCII whitespace: space, `\t`, `\n`, `\r`,
  `\v`, `\f`).
- Searching: `find(s, needle) -> ?usize` (an empty needle is found at 0), `rfind` (an
  empty needle is found at `s.len`), `contains`, `find_ignore_case(s, needle)` (ASCII
  case-insensitive `find`), `count(s, needle)` (non-overlapping occurrences; an empty
  needle traps) and `strip_prefix(s, prefix)` / `strip_suffix(s, suffix)`, which give
  the rest of `s` or `null`.
- Splitting: `split_once(s, sep) -> ?(before: string, after: string)` and the iterators
  `split(s, sep)` (adjacent or terminal separators give empty pieces; an empty `sep`
  yields `s` once), `split_any(s, seps)` (splits at every byte that occurs in `seps`,
  with the same empty pieces), `lines(s)` (splits on `\n`, drops a `\r` before it, no
  empty line after a final `\n`) and `fields(s)` (runs of non-whitespace). Without an
  iterator: `split_into(s, sep, parts) -> usize` fills `parts` with at most
  `parts.len` pieces, the last taking the rest of `s`, and returns how many it wrote
  (`split_into("k=v=w", "=", p[0..2])` gives `k` and `v=w`), and `nth_field(s, sep, n)`
  is piece `n` of `split` or `null`.
- Case: `eq_ignore_case(a, b)`, and `to_lower(s, buf)` and `to_upper(s, buf)`, which
  write into `buf` and return the part used; a buffer shorter than `s` traps.
  Single bytes are classified and converted by `std/ascii`.
- Replacing: `replace(out, s, from, to)` appends `s` to a `^List(u8)` with every
  non-overlapping `from` (found from the left; empty traps) replaced by `to`, and
  `replace_alloc(s, from, to) -> AllocError![]u8` returns the result in one allocation;
  `repeat(out, s, n)` appends `n` copies of `s`. `dupe(s) -> AllocError![]u8` copies
  `s`. Results from the allocating forms are released with `free`.
- Integers: `parse_int(T, s) -> ParseError!T` for any integer type: an optional sign
  (`-` only for signed types), an optional `0x`, `0o` or `0b` prefix, digits with single
  `_` separators between them, and nothing else. The most negative value of a signed
  type parses. Errors are `ParseError`: `.Empty`, `.InvalidDigit(at: index)` (a bad
  character, a misplaced `_`, or missing digits, in which case `at` is the index where
  a digit was expected) and `.Overflow`. The first problem found from the left wins.
  `parse_radix(T, s, base)` takes a base from 2 to 36 (letters of either case are the
  digits above 9) and no prefix.
- Floats: `parse_float(T, s) -> ParseError!T` for `f16`, `f32` and `f64`. The syntax is
  an optional sign and then `inf`, `infinity` or `nan` in any case, or digits with an
  optional fraction and exponent: `12`, `1.5`, `.5`, `5.`, `1e-3`, `6.02E+23`, with
  single `_` separators between digits. There must be a digit in the mantissa and, if
  there is an `e`, in the exponent; anything else, including whitespace, is
  `.InvalidDigit(at: index)`. The result is correctly rounded, to nearest with ties to
  even, whatever the number of digits: subnormals are exact, a number too large for
  `T` is `inf` (no error) and one too small is zero. Digits that `print` writes parse
  back to the same float.
- `Builder`: appends into a buffer you supply, allocating nothing.
  `b := str.builder(buf[..])`, then `b.push(s)`, `b.push_byte(c)`, `b.str()`,
  `b.len()` and `b.reset()`. `b.write(template, args)` appends formatted text, exactly as
  `print` would write it: `b.write("{} of {}", (n, total))`. Appending beyond the buffer traps.
- `join(parts, sep) -> AllocError![]u8`: the parts with `sep` between them in memory
  from the current allocator; release it with `free`.

String comparison needs no function: `a < b` orders strings by byte, and `a is b`
tests equality.

## `std/text`

Unicode text segmentation and the UTF-8 encoding of scalar values. Strings are read as UTF-8, with each maximal ill-formed
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
- Scalar values. Reading text as scalar values is `runes(s)` and checking it is
  `s.utf8_valid()`, both builtin. `decode_rune(s) -> ?(rune: u32, len: usize)` is the
  first scalar value and its byte length, and `null` for empty or ill-formed input (a
  stray continuation, a truncated or overlong sequence, a surrogate, a value above
  U+10FFFF), where `runes` would give U+FFFD. `encode_rune(cp, buf) -> ?string` writes
  the UTF-8 form of `cp` into `buf` (at least 4 bytes, or it traps) and returns the
  bytes written; it is `null` for a surrogate or a value above U+10FFFF.
  `append_rune(out, cp) -> AllocError!()` appends the encoding to a `^List(u8)` and
  traps on a value that is not a scalar value.

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

Threads (§19.6) and the synchronization between them. A thread runs a plain function
`^fn(arg: ^T) \ {Sys}` and starts with no handlers; `thread.defaults(fn() { … })` runs a
closure under the default handlers of every ambient effect.

```gamma
thread :: import "std/thread"

Counter :: struct { n: u64, m: thread.Mutex }

work :: fn(c: ^Counter) \ {Sys} {
    c.m.lock()
    c.n += 1
    c.m.unlock()
    thread.defaults(fn() { print("done\n") })
}

c := Counter(n: 0, m: thread.mutex())
t := try thread.spawn(Counter, work, &c)
t.join()
```

- `spawn(T, f, arg) -> IoError!Thread`, `t.join()`, `yield_now()`, `cpu_count() -> u32`
  (at least 1).
- `Mutex` is a plain struct (no allocation) that sleeps in the kernel while it waits:
  `mutex()`, `m.lock()`, `m.try_lock() -> bool`, `m.unlock()`.
- `Condvar`: `condvar()`, `c.wait(&m)` unlocks `m`, waits and locks it again, and may
  return without a signal, so wait in a loop (`while not ready { c.wait(&m) }`);
  `c.wait_timeout(&m, d) -> bool` is `false` if `d` passed without a signal;
  `c.signal()` wakes one waiter and `c.broadcast()` all of them.
- `WaitGroup`: `wait_group()`, `w.add(n)` before starting work, `w.done()` when a piece
  finishes (more calls than `add` trap), `w.wait_all()` returns when the count is zero.
- `Channel(T)` is a bounded blocking queue for any number of senders and receivers:
  `channel(T, capacity) -> AllocError!Channel(T)` (capacity at least 1; it keeps the
  allocator it was created with), `c.send(x) -> bool` waits while the queue is full and
  gives `false` if it is closed, `c.recv() -> ?T` waits while it is empty and gives `null`
  once it is closed and drained, `c.try_send(x) -> bool`, `c.try_recv() -> ?T`,
  `c.close()` (wakes blocked threads; values already queued are still received) and
  `c.deinit()`.
- Shared memory needs atomics or one of these types; a data race is undefined behavior.
  Handlers, second-class functions and continuations never cross threads.
- The default handlers are also available directly, for code that starts without any:
  `heap_allocator()`, `system_io()` (`In`, `Clock`, `Rand`, `Fs`, `Net`) and
  `system_output()` (`Out`, `Err`).

The synchronization types are built on two intrinsics available to the standard library,
`@futex_wait(addr, expected, timeout_ns)` and `@futex_wake(addr, n)`. The interpreter
runs threads one at a time, switching between them often and whenever one blocks, so
programs behave as they do compiled, without data races in the interpreter itself.

## `std/c`

C's platform-dependent types (`c_int`, `c_long`, `c_size_t`, …) for declaring
foreign functions (§19.1).

## `std/random`

Random numbers drawn from the ambient `Rand` effect, and a fast seedable generator that
is itself a handler for `Rand`. The same code gets OS randomness by default, a fixed seed
in tests, and a reproducible sequence when the program installs a seeded generator.

```gamma
random :: import "std/random"

r := random.seeded(42)
with &r {
    die := random.range(u8, 1, 7)             // 1 to 6; the same on every run
    if random.chance(0.5) { print("heads\n") }
}
```

- `Rng` is a xoshiro256** generator (not for cryptography). `seeded(seed)` expands a
  `u64` with splitmix64 (the state is never all zero), `from_rand()` seeds from the
  current `Rand`, and `next(&r)` gives the next `u64`. `^Rng` handles `Rand`: `fill` writes
  eight little-endian bytes per output, the tail from one more.
- `next_u64()` is a uniform `u64`. `below(T, n)` is uniform in [0, n) without modulo
  bias (`n` must be positive), `range(T, lo, hi)` is uniform in [lo, hi) for any integer
  type up to 64 bits including signed ones and the full span (`lo < hi`).
- `float()` is uniform in [0, 1) from the top 53 bits, `chance(p)` is true with
  probability `p`, `normal(mean, stddev)` is Box-Muller.
- `shuffle(T, items)` is Fisher-Yates, `choice(T, items)` gives `?T` (`null` if empty),
  and `weighted(weights)` gives the index `?usize` chosen with probability proportional
  to its weight (negative weights count as 0; `null` if empty or the sum is not
  positive).

## `std/time`

Calendar dates and times in the proleptic Gregorian calendar, in UTC only: there are no
time zones (they need the tz database) and no leap seconds. `Clock`'s `wall()` is Unix
time in nanoseconds, which `from_unix` turns into a `DateTime`. Text goes into buffers
the caller supplies.

```gamma
time :: import "std/time"

t := time.now()                                     // \ {Clock}
buf: [time.RFC3339_LEN]u8 = ---
print("{}\n", time.format_rfc3339(t, buf[..]))      // 2024-05-17T09:30:00Z
d := try time.parse_date("2024-01-31")
print("{}\n", time.format_date(time.add_months(d, 1), buf[..]))   // 2024-02-29
```

- Types: `Weekday` (`.Monday` to `.Sunday`), `Date { year: i32, month, day: u8 }`,
  `DateTime { date, hour, minute, second, nanosecond }`.
- Calendar: `is_leap_year(year)`, `days_in_month(year, month)`, `is_valid(d)`,
  `weekday(d)`, `days_from_civil(d)` and `civil_from_days(n)` (days since 1970-01-01,
  correct before it), `add_days(d, n)`, and `add_months(d, n)`, which clamps the day to
  the target month (January 31 plus a month is February 28 or 29).
- Unix time: `from_unix(ns)` (negative values round toward the past: -1 is
  1969-12-31T23:59:59.999999999), `to_unix(t)`, and `now()`.
- Formatting: `format_rfc3339(t, buf)` writes `YYYY-MM-DDTHH:MM:SSZ`, with a fraction of
  3, 6 or 9 digits only when the nanoseconds are not zero (years 0 to 9999);
  `RFC3339_LEN` (30) always suffices. `format_date(d, buf)` writes `YYYY-MM-DD`.
  `format_duration(d, buf)` writes a `Duration` like `1h2m3.5s`, `1.5ms`, `250ns` or
  `0s` (microseconds are `us`); `DURATION_LEN` (32) always suffices.
- Parsing: `parse_date(s)` and `parse_rfc3339(s)` return `ParseError!T`, where
  `ParseError` is `Syntax: usize` (byte position of the first byte that does not fit) or
  `Range: usize` (position of the field that is out of range). `parse_rfc3339` accepts
  `T`, `t` or a space as separator, 1 to 9 fraction digits and `Z`, `z` or a `+HH:MM`
  offset, which it applies to give UTC.

## `std/cli`

Command-line parsing as an iterator, in the style of getopt. The program describes its
options in a table and matches over what `parse` yields; nothing allocates.

```gamma
cli :: import "std/cli"

main :: fn() \ {Out, Env} {
    opts := [_]cli.Opt(
        cli.Opt(short: 'v', long: "verbose", help: "print more"),
        cli.Opt(short: 'o', long: "output", value: "file", help: "where to write"),
    )
    for a in cli.parse(args()[1..], opts[..]) {
        match a {
            .Opt(o) => print("option {} value {}\n", (opts[o.index].long, o.value))
            .Pos(p) => print("file {}\n", p)
            .Unknown(u) => print("unknown option {}\n", u)
            .MissingValue(i) => print("--{} needs a value\n", opts[i].long)
            .UnexpectedValue(i) => print("--{} takes no value\n", opts[i].long)
        }
    }
}
```

- `Opt { short: u8, long: string, value: string, help: string }`: the short letter (0 for
  none), the long name without dashes ("" for none), the name of the value in help
  text ("" makes the option a flag), and a description.
- `parse(args, opts)` yields `Arg`: `Opt: (index, value)` (`value` is "" for flags),
  `Pos: string`, `Unknown: string` (as written: `--colour`, or `-q` from a bundle),
  `MissingValue: usize`, and `UnexpectedValue: usize` (`--flag=x`). Pass `args()[1..]`.
- Rules: `--` ends the options and a lone `-` is positional; long options are
  `--name=value` or `--name value` and must match exactly (no abbreviations); short
  options bundle (`-abc`) and take a value as `-ofile` or `-o file`, also at the end of a
  bundle (`-vofile`); options and positional arguments may be mixed. A value is taken
  from the next argument whatever it looks like.
- `help(&out, usage, opts)` appends `usage: …`, a blank line, `options:` and one aligned
  line per option (`  -o, --output <file>  where to write`) to a `List(u8)`, returning
  `AllocError!()`.

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

## `std/ascii`

Classification and case of single bytes, for the 128 ASCII characters; a byte of 128
or more is in no class and has no case. Nothing allocates and nothing performs effects.

```gamma
ascii :: import "std/ascii"

for c in bytes(word) {
    if ascii.is_alpha(c) { out.push(ascii.to_lower(c)) }
}
```

- Classes, each `fn(c: u8) -> bool`: `is_alpha`, `is_digit`, `is_alnum`, `is_upper`,
  `is_lower`, `is_hex_digit`, `is_space` (space, `\t`, `\n`, `\r`, `\v`, `\f`, the set
  `std/str` trims), `is_print` (0x20 to 0x7e), `is_punct` (printable, but not a letter,
  digit or space) and `is_control` (0x00 to 0x1f and 0x7f).
- Case: `to_lower(c)` and `to_upper(c) -> u8` change letters and leave other bytes
  alone.
- `digit_value(c) -> ?u8` is the value of `c` as a digit in a base up to 36: 0 to 9 for
  `0`-`9`, 10 to 35 for `a`-`z` or `A`-`Z`, and `null` for any other byte.

## `std/fiber`

A cooperative round-robin scheduler for fibers (§14), the one sketched in the
[fibers chapter](guide/12-fibers.md). Tasks run one at a time on the thread that
installed the scheduler and take turns when they call `yield_now`.

```gamma
fiber :: import "std/fiber"

worker :: fn(c: ^Counter) \ {fiber.Sched} {
    c.ticks += 1
    fiber.yield_now()
    c.ticks += 1
}

s := fiber.scheduler()
defer s.deinit()
with &s on main_stack {
    fiber.spawn(Counter, worker, &a, stack_a)
    fiber.spawn(Counter, worker, &b, stack_b)
    fiber.join()
}
```

- `Sched` is the effect: `yield_now()` puts the task at the back of the queue and runs
  the next; `exit()` ends the task and `join()` waits until every task has exited (only
  the code under the `with` joins); `spawn_raw(entry, arg, stack)` is the operation
  behind `spawn`.
- `spawn(T, f, arg, stack)` starts a task calling `f(arg)` on `stack`, to run once the
  current task gives way. The task ends, running its `defer`s, when `f` returns. A few
  bytes at the start of `stack` hold the task's start-up data. A task performs only
  `Sched`, so it shares data through the pointer it is given.
- `Scheduler` is the handler's state, `scheduler()` makes one (keeping the current
  allocator for its queue of ready tasks) and `deinit(&s)` frees the queue. A scheduler
  with no ready task when one is needed (a task waiting forever) traps.

## `std/log`

Logging as an effect: code writes records, and the installed handler decides where they
go and which are wanted. Nothing allocates, truncates or keeps global state.

```gamma
log :: import "std/log"

with log.ErrLog(min: .Warn) {
    log.info("starting")                    // dropped
    log.warn("slow request: {} ms", 250)    // standard error: WARN slow request: 250 ms
}
```

- `Level` is `Debug`, `Info`, `Warn` or `Error`, in that order (`<` works).
- `Log` is the effect: `begin(level) -> bool` says whether the handler wants the record,
  `write(bytes)` receives the message in pieces and `end()` closes the record.
- `log(level, template, args)` formats like `print` and writes a record; it formats
  nothing when `begin` says no. `debug`, `info`, `warn` and `error` are the same with a
  fixed level.
- `ErrLog(min: .Info, time: false)` prints `LEVEL message` lines to `Err` (with
  `2026-10-12T04:38:42Z ` in front when `time` is true, from the `Clock` effect); records
  below `min` are dropped.
- `ListLog(list: &out, min: .Debug)` appends the same lines to a `List(u8)`, which is
  handy in tests; the first allocation failure is kept in its `err` field.
- `Discard()` wants no records at all.
- An inner `with` overrides the outer handler for its body.
