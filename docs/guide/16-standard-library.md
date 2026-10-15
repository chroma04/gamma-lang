# 16. The standard library

[Contents](README.md) · [Previous: Packages](15-packages.md)

The earlier chapters used the library as they went. This one is a tour of all of it: what lives where, what each part is for, and a short, complete example of each. The exact functions of every module are in the [library reference](../std.md); the rule for what may be in the library at all is [§22 of the specification](../../gamma-spec.md#22-the-standard-library-scope-and-admission).

Every example here is a whole program. Save one as `tour.gm` and run it with `gamma run tour.gm`, or compile it with `gamma run --backend=c tour.gm`: the output is the same.

## Two tiers

- The **core library** is in every module without an import: the ambient effects (`Out`, `Alloc`, `Fs`, ...), allocation (`new`, `make`, `free`, ...), the iterators of [chapter 8](08-iteration.md), `List` and `HashMap`, `print`, `assert` and `expect`, durations (`ms(10)`), and the builtin methods of numbers, slices and strings. The language definition needs these.
- The **standard modules** are imported by path: `str :: import "std/str"`. Each has one subject and costs nothing in programs that do not import it. They are written in Gamma, so a program that imports `std/str` or `std/json` gets the same behavior in the interpreter and in compiled code.

Anything beyond that (HTTP, TLS, compression, databases, image formats, ciphers) belongs in [packages](15-packages.md); §22.4 lists what the standard library leaves out and why.

Four conventions hold across the library, so that learning one module teaches the rest:

1. **No hidden allocation.** A function that allocates says so with `\ {Alloc}` or takes an allocator. Functions that produce text or bytes write into a buffer you supply, or append to a `List(u8)`.
2. **Failure is a value.** Parsers take a `string` and return `E!T` with an error that says where the problem is; a trap means a documented precondition was broken.
3. **Containers capture the allocator** when they are created, like `List` does, and never perform `Alloc` afterwards.
4. **Sequences are iterators**, not allocated arrays, unless you ask for a list.

## Formatted output

`print` writes to `Out` and `eprint` to `Err`. The template is a compile-time string with `{}` placeholders; with one placeholder the argument stands alone, with several it is a tuple. Any value prints, structs and unions included:

```gamma
Point :: struct { x, y: f64 }

main :: fn() \ {Out} {
    print("{} scored {}\n", "Ada", 1200)
    print("{}\n", Point(x: 1.5, y: -2))
    print("{} {} {}\n", [_]u8(1, 2, 3), Maybe.Some(4), "text")
    print("{:<8}|{:>6}|{:^7}|\n", "left", 42, "mid")
    print("{:08.3} {:+} {:#06x} {:b} {:.2e}\n", -3.14159, 5, 255, 5, 1234.5)
    print("{{braces}} and a tab:\t.\n")
}

Maybe :: union { None, Some: u32 }
```

```
Ada scored 1200
Point(x: 1.5, y: -2)
[1, 2, 3] .Some(4) text
left    |    42|  mid  |
-003.142 +5 0x00ff 101 1.23e3
{braces} and a tab:	.
```

A spec after the colon sets alignment (`<`, `>`, `^`) with an optional fill character, a width, `+`, a base (`x`, `X`, `o`, `b`, with `#` for the prefix), `0` padding, a float precision (`.2`) or scientific notation (`e`). A spec that does not fit its argument is a compile error. The full list is §21.6.1.

### `@write`: formatting into your own sink

`print` is `@write` aimed at `Out`. `@write(sink, template, ..args)` takes the same template and arguments, but calls a function you give it with the pieces of output. It allocates nothing, which makes it the base of every formatting function:

```gamma
main :: fn() \ {Out} {
    total: usize = 0
    @write(fn(b) { total += b.len }, "{} items at {:.2}\n", 3, 9.5)
    print("that text is {} bytes\n", total)

    // Pieces go to the sink as they are produced.
    @write(fn(b) { print("[{}]", b) }, "{}-{}", 12, "x")
    print("\n")
}
```

```
that text is 16 bytes
[12][-][x]
```

The template of a function that forwards to `print` or `@write` is a compile-time parameter, so a mistake in it is reported at the call that wrote it:

```gamma
shout :: fn(A: type, template :: string, args: ..A) \ {Out} {
    print("!! ")
    print(template, ..args)
    print("\n")
}

main :: fn() \ {Out} {
    shout("disk {} is {}% full", "sda", 91)
}
```

To keep text rather than print it, append to a list, or format into a new allocation:

```gamma
str :: import "std/str"

main :: fn() -> AllocError!() \ {Out, Alloc} {
    line := list(u8)
    defer line.deinit()
    try line.write("{:>5}|{:<5}|", 42, "ab")
    try line.write("{}", true)
    print("{}\n", line.items)

    s := try str.format("{} + {} = {}", 2, 3, 5)
    defer free(s)
    print("{}\n", s)
}
```

`std/io` has `Out` handlers for the same job when the printing is done by code you do not control: `io.ListOut` collects what a block prints into a list, and `io.Capture` into a buffer.

### `std/fmt`

The templates leave out a few presentations that programs ask for: other bases, digit grouping, `%g`-style general form, and text you want as a value. `std/fmt` writes these into a buffer you supply and returns the used part:

```gamma
fmt :: import "std/fmt"

main :: fn() \ {Out} {
    buf: [fmt.INT_BUF]u8 = ---
    print("{}\n", fmt.grouped(u32(1234567), ',', buf[..]))
    print("{}\n", fmt.radix(u32(255), 36, buf[..]))
    print("{}\n", fmt.pad_left("7", 4, '0', buf[..]))
    fb: [400]u8 = ---
    print("{}\n", fmt.fixed(2.675, 2, fb[..]))
    print("{}\n", fmt.general(0.000012345, 4, fb[..]))
}
```

```
1,234,567
73
0007
2.67
1.234e-05
```

(`2.675` is below 2.675 in binary, so rounding to two places gives 2.67.)

## Methods on numbers, slices and strings

Numbers, arrays, slices, strings and pointers have methods built into the language (§21.3). They are listed there; these are the ones used all the time.

```gamma
main :: fn() \ {Out} {
    // Integers: conversions say what they do on overflow.
    big: i32 = 300
    print("{} {} {}\n", big.trunc(u8), big.checked(u8), big.sat(u8))
    print("{} {} {}\n", big.min(7), big.clamp(0, 255), (-5).abs())
    print("{}\n", big.to(f64) / 7)

    // Floats.
    print("{} {} {}\n", 2.7.round(i32), 2.7.floor(i32), 9.0.sqrt())

    // Slices and arrays.
    xs := [_]i32(5, -3, 9, 0, 7)
    xs[..].sort()
    print("{}\n", xs)
    print("{} {} {}\n", xs.first(), xs.last(), xs.get(10))
    print("{} {}\n", xs[..].min(), xs[..].max())
    print("{}\n", xs[..].binary_search(7))
    print("{}\n", xs[..].index_of(9))

    words := [_]string("pear", "fig", "banana")
    words[..].sort_by(fn(a, b: ^const string) -> bool => a^.len < b^.len)
    print("{}\n", words)

    buf: [6]u8 = ---
    buf[..].fill('.')
    buf[0..3].copy_from("abc")
    print("{}\n", buf[..])

    // Strings are byte slices, so these work on them too.
    path := "src/main.gm"
    print("{} {} {}\n", path.starts_with("src/"), path.ends_with(".gm"), path.index_of('/'))
    print("{} {}\n", path.utf8_valid(), path < "tests")
}
```

`checked(T)` gives `null` when the value does not fit, `trunc(T)` drops the high bits, `sat(T)` clamps. The comparison operators order strings and slices by element, and `is` compares them by value.

## Memory: `List`, `HashMap`, arenas

`List(T)` and `HashMap(K, V)` are in the core library ([chapter 9](09-memory.md) covers them). The pieces used most:

```gamma
main :: fn() -> AllocError!() \ {Out, Alloc} {
    names := list(string)
    defer names.deinit()
    try names.push("ada")
    try names.extend([_]string("grace", "alan"))
    names.items.sort()
    print("{} {}\n", names.items.len, names.items)

    last := names.pop()
    print("{} {}\n", last, names.remove(0))

    // Word counts: `entry` stores 0 for a new key and points at the value.
    counts := map(string, u32)
    defer counts.deinit()
    for w in [_]string("a", "b", "a", "c", "a", "b") {
        e := try counts.entry(w, 0)
        e.value^ += 1
    }
    print("a={} b={} z={}\n", counts.get("a"), counts.get("b"), counts.get("z"))

    // Maps are unordered; sort the keys when the order matters.
    keys := try collect(fn() => counts.keys())
    defer keys.deinit()
    keys.items.sort()
    print("{}\n", keys.items)
}
```

Allocators are values. `std/arena` hands out memory from chunks and gives everything back at once, which suits a structure built in one phase and dropped in another; `std/fixed_buffer` allocates from a buffer you own, so a program can run with no heap at all:

```gamma
arena :: import "std/arena"
fixed_buffer :: import "std/fixed_buffer"

main :: fn() -> AllocError!() \ {Out, Alloc} {
    a := arena.new()                       // chunks come from the current allocator
    defer a.deinit()                       // releases every chunk at once
    with a.allocator() {
        words := list(u32)                 // no deinit needed: the arena owns the memory
        for i in 0..1000 { try words.push(i) }
        print("{}\n", words.items.len)
    }

    store: [256]u8 = ---
    fb := fixed_buffer.over(store[..])
    with fb.allocator() {
        small := list(u8)
        for i in 0..100 { try small.push(i.trunc(u8)) }
        print("{} of {} bytes used\n", fb.bytes_used() > 100, store.len)
        r := make(u8, 1000)
        print("a 1000-byte block: {}\n", r)
    }
}
```

```
1000
true of 256 bytes used
a 1000-byte block: !.OutOfMemory
```

## Iterators

The iterators of the core library are `range`, `each`, `enumerate`, `reversed`, `zip`, `bytes` and `runes`; the functions `collect`, `count` and `nth` run an iterator you hand them. Library modules produce sequences the same way, so these compose:

```gamma
str :: import "std/str"

main :: fn() -> AllocError!() \ {Out, Alloc} {
    for i, name in enumerate([_]string("ada", "grace")) { print("{}:{} ", i, name) }
    print("\n")

    for a, b in zip([_]u32(1, 2, 3), [_]string("one", "two")) { print("{}={} ", a, b) }
    print("\n")

    text := "alpha beta  gamma\ndelta"
    fields := try collect(str.fields(text))         // an iterator is passed as a call
    defer fields.deinit()
    print("{} fields, the third is {}\n", fields.items.len, fields.items[2])
    print("{} lines, second {}\n", count(str.lines(text)), nth(str.lines(text), 1))

    // Early exit: the iterator stops and runs its cleanup.
    for w in str.split("a,b,c,d", ",") {
        if w is "c" { break }
        print("{} ", w)
    }
    print("\n")
}
```

## Text: `std/str`, `std/ascii`, `std/text`

`std/str` has the byte-oriented string functions. Results are slices of their arguments, so nothing is copied unless the name says so (`join`, `replace_alloc`, `format`):

```gamma
str :: import "std/str"

main :: fn() -> AllocError!() \ {Out, Alloc} {
    s := "  key = value  "
    print("[{}]\n", str.trim(s))
    if kv := str.split_once(str.trim(s), "=") {
        print("[{}] [{}]\n", str.trim(kv.before), str.trim(kv.after))
    }
    print("{} {} {}\n", str.find("hello", "ll"), str.contains("hello", "z"), str.count("banana", "an"))
    print("{} {}\n", str.strip_prefix("--verbose", "--"), str.strip_suffix("a.txt", ".md"))
    print("{}\n", str.eq_ignore_case("Gamma", "gAMMA"))

    buf: [16]u8 = ---
    print("{}\n", str.to_upper("shout", buf[..]))

    out := list(u8)
    defer out.deinit()
    try str.replace(&out, "a-b-c", "-", "+")
    print("{}\n", out.items)

    joined := try str.join([_]string("x", "y", "z"), ", ")
    defer free(joined)
    print("{}\n", joined)
}
```

Parsing numbers is exact. `parse_int` takes any integer type; `parse_float` rounds correctly for any number of digits. The error says where the text went wrong:

```gamma
str :: import "std/str"

main :: fn() \ {Out} {
    print("{}\n", str.parse_int(u8, "200"))
    print("{}\n", str.parse_int(u8, "256"))
    print("{}\n", str.parse_int(i32, "-0x1F"))
    print("{}\n", str.parse_int(i32, "12a"))
    print("{}\n", str.parse_float(f64, "6.02e23"))
    print("{}\n", str.parse_float(f64, "1.5x"))
    port := str.parse_int(u16, "oops") else 8080         // a default on any error
    print("{}\n", port)

    // A Builder formats into a buffer you own.
    buf: [32]u8 = ---
    b := str.builder(buf[..])
    b.write("{}/{}", 3, 4)
    b.push_byte('!')
    print("{}\n", b.str())
}
```

```
200
!.Overflow
-31
!.InvalidDigit(at: 2)
6.02e23
!.InvalidDigit(at: 3)
8080
3/4!
```

`std/ascii` classifies and converts single bytes (`ascii.is_digit(c)`, `ascii.to_upper(c)`). Real text is Unicode, and `std/text` reads it that way:

```gamma
text :: import "std/text"

main :: fn() \ {Out} {
    s := "e\u{301}🇫🇷👨\u{200d}👩\u{200d}👧!"
    n: usize = 0
    for g in text.graphemes(s) { n += 1 }
    print("{} bytes, {} scalar values, {} characters as a reader sees them\n", s.len, count(runes(s)), n)
    if r := text.decode_rune("€5") { print("U+{:X} takes {} bytes\n", r.rune, r.len) }
    buf: [4]u8 = ---
    print("{}\n", text.encode_rune(0x1F600, buf[..]))
}
```

`graphemes` implements Unicode's extended grapheme clusters (UAX #29): a letter with its accents, a flag, an emoji family are each one cluster. Use it for anything the user counts or cuts as "characters". `runes(s)` and `s.utf8_valid()` are core.

## Files, paths and standard input

File access goes through the `Fs` effect, so a test can swap in an in-memory file system. The helpers in `std/fs` cover whole files and directory walks; the raw operations (`open`, `read_from`, `write_to`, `seek`, `stat`, `lstat`, ...) are in the core:

```gamma
fs :: import "std/fs"
io :: import "std/io"
path :: import "std/path"

main :: fn() -> fs.ReadError!() \ {Out, Fs, Rand, Alloc} {
    dir := "/tmp/gamma-tour-16"
    try fs.make_dir_all(dir)
    file := "/tmp/gamma-tour-16/notes.txt"

    try fs.write_all(file, "first\nsecond\n")
    try fs.append(file, "third\n")
    data := try fs.read_all(file)
    defer free(data)
    print("{} bytes, {} lines\n", data.len, count(lines_of(data)))

    info := try stat(file)
    print("size {} kind {}\n", info.size, info.kind)

    // write_atomic: readers see the old file or the new one, never half of it.
    try fs.write_atomic(file, "replaced\n")

    for r in fs.entries(dir) {
        e := try r
        print("entry {} ({})\n", e.name, path.extension(e.name))
    }

    if !e := fs.read_all("/tmp/gamma-tour-16/missing") { print("error: {}\n", e) }

    try remove(file)
    try remove_dir(dir)
}

lines_of :: fn(data: []const u8) \ {Yield(string)} {
    start: usize = 0
    for i in 0..data.len {
        if data[i] is '\n' {
            yield(data[start..i])
            start = i + 1
        }
    }
}
```

`std/path` works on path strings without touching the file system: `base_name`, `dir_name`, `extension`, `stem`, `clean`, `join`, `relative_to`:

```gamma
path :: import "std/path"

main :: fn() -> AllocError!() \ {Out, Alloc} {
    p := "/usr/lib/libc.so.6"
    print("{} {} {} {}\n", path.base_name(p), path.dir_name(p), path.extension(p), path.stem(p))
    buf: [64]u8 = ---
    print("{}\n", path.clean("a//b/./c/../d", buf[..]))
    joined := try path.join_alloc([_]string("data", "2026", "report.txt"))
    defer free(joined)
    print("{} {}\n", joined, path.relative_to("/srv/www/css", "/srv/www"))
}
```

Standard input is the `In` effect. `io.reader()` reads it a line at a time; `io.read_all()` takes it whole. Handlers redirect the same code to a string or a file, so a function written against `In` and `Out` is testable:

```gamma
io :: import "std/io"

shout_lines :: fn() -> IoError!() \ {In, Out} {
    r := io.reader()
    while line := try r.next_line() {
        print("{}!\n", line)
    }
}

main :: fn() \ {Out} {
    input := io.StringIn(data: "one\ntwo\nthree")
    with &input {
        shout_lines() else e => print("failed: {}\n", io.describe(e))
    }
}
```

`io.describe(e)` turns an `IoError` into a short message for `error: ...` lines.

## Time and randomness

Durations and instants are core; `Clock` is the effect that supplies them (a test handler gives a fake clock, so `sleep` costs nothing). `std/time` adds the calendar, in UTC, with RFC 3339 text:

```gamma
time :: import "std/time"

main :: fn() -> time.ParseError!() \ {Out, Clock} {
    started := now()
    sleep(ms(5))
    took := now().since(started)
    print("slept at least 5 ms: {}\n", took.to_ms() >= 5)

    t := time.from_unix(1_700_000_000 * 1_000_000_000)
    buf: [time.RFC3339_LEN]u8 = ---
    print("{} {}\n", time.format_rfc3339(t, buf[..]), time.weekday(t.date))

    d := try time.parse_date("2024-01-31")
    db: [10]u8 = ---
    print("{}\n", time.format_date(time.add_months(d, 1), db[..]))   // clamps to the month's end
    dur: [time.DURATION_LEN]u8 = ---
    print("{}\n", time.format_duration(secs(3725) .plus(ms(500)), dur[..]))
    r := try time.parse_rfc3339("2024-05-17T11:30:00+02:00")          // converted to UTC
    print("{}\n", time.format_rfc3339(r, buf[..]))
}
```

Randomness is the `Rand` effect, filled from the operating system by default and from a fixed seed in tests. `std/random` builds the usual draws on it without modulo bias, and has a seedable generator that is itself a `Rand` handler, for reproducible runs:

```gamma
random :: import "std/random"

main :: fn() \ {Out} {
    r := random.seeded(42)
    with &r {
        print("{} {}\n", random.range(u8, 1, 7), random.below(u32, 100))
        xs := [_]u32(1, 2, 3, 4, 5, 6)
        random.shuffle(u32, xs[..])
        print("{}\n", xs)
        print("{}\n", random.choice(string, [_]string("rock", "paper", "scissors")))
    }
}
```

The same seed gives the same sequence on every run and every platform.

## Logging

`std/log` is logging as an effect: code writes records, and whoever runs it chooses what happens to them. Records below the handler's level cost almost nothing, because nothing is formatted for them:

```gamma
log :: import "std/log"

work :: fn() \ {log.Log} {
    log.debug("not shown at the default level")
    log.info("starting {}", "up")
    log.warn("{} of {} retries used", 2, 3)
}

main :: fn() \ {Out, Err, Alloc} {
    plain := log.ErrLog()                       // lines on standard error
    with &plain { work() }

    // Collect the records instead: handy in a test.
    text := list(u8)
    defer text.deinit()
    sink := log.ListLog(list: &text, min: .Debug)
    with &sink { work() }
    print("{}", text.items)
}
```

`log.StampedLog` adds the time from the `Clock` effect to each line; `log.ErrLog` needs only `Err`.

## Threads and fibers

`std/thread` starts operating-system threads. A thread runs a function pointer with one argument; shared data is protected by a `Mutex`, announced with a `Condvar`, counted with a `WaitGroup`, or passed through a bounded `Channel`:

```gamma
thread :: import "std/thread"

Job :: struct {
    results: thread.Channel(u64)
    pending: thread.WaitGroup
    base: u64
}

worker :: fn(j: ^Job) \ {Sys} {
    sum: u64 = 0
    for i in 0..1000 { sum += i * j.base }
    _ = j.results.send(sum)
    j.pending.done()
}

Fail :: union { Io: IoError, Mem: AllocError }

main :: fn() -> Fail!() \ {Out, Sys, Alloc} {
    j := Job(results: try thread.channel(u64, 8), pending: thread.wait_group(), base: 1)
    defer j.results.deinit()

    j.pending.add(3)
    for _ in 0..3 {
        t := try thread.spawn(Job, worker, &j)
        t.detach()                          // never joined; `pending` says when it is done
    }
    j.pending.wait()                        // all three have sent
    j.results.close()

    total: u64 = 0
    while v := j.results.recv() { total += v }
    print("total {}\n", total)

    // A mutex-protected counter, with joined threads.
    c := Counter(n: 0, m: thread.mutex())
    ts: [4]thread.Thread = ---
    for i in 0..4 { ts[i] = try thread.spawn(Counter, count_up, &c) }
    for i in 0..4 { ts[i].join() }
    print("counter {}\n", c.n)
}

Counter :: struct {
    n: u64
    m: thread.Mutex
}

count_up :: fn(c: ^Counter) \ {Sys} {
    for _ in 0..1000 {
        c.m.lock()
        c.n += 1
        c.m.unlock()
    }
}
```

```
total 1498500
counter 4000
```

A thread starts with no handlers installed; `thread.defaults(body)` installs the usual ones for a thread that prints or allocates. `t.detach()` lets a thread run on its own: its resources are released when it ends. When `main` returns the process exits, and a detached thread still running is stopped where it is, without running its `defer`s, so wait for the ones whose work you need.

`std/fiber` is a cooperative scheduler for [fibers](12-fibers.md): tasks that take turns on one thread by calling `fiber.yield_now()`. It is the scheduler sketched in chapter 12, ready to use.

## Collections

`List` and `HashMap` are core. Four more containers are modules of their own, because their functions are methods and a module has one namespace:

```gamma
dq :: import "std/collections/deque"
hp :: import "std/collections/heap"
bs :: import "std/collections/bitset"
om :: import "std/collections/ordered_map"

Job :: struct {
    name: string
    cost: u32
}

main :: fn() -> AllocError!() \ {Out, Alloc} {
    // A double-ended queue: constant time at both ends.
    d := dq.deque(u32)
    defer d.deinit()
    try d.push_back(2)
    try d.push_back(3)
    try d.push_front(1)
    print("{} {} {}\n", d.pop_front(), d.pop_back(), d.len())

    // A priority queue: the cheapest job first.
    h := hp.heap_by(Job, fn(a, b: ^const Job) -> bool => a.cost < b.cost)
    defer h.deinit()
    try h.push(Job(name: "compile", cost: 30))
    try h.push(Job(name: "lint", cost: 5))
    try h.push(Job(name: "test", cost: 20))
    while j := h.pop() { print("{} ", j.name) }
    print("\n")

    // A set of small integers, one bit each.
    seen := try bs.bitset(100)
    defer seen.deinit()
    seen.insert(42)
    seen.insert(7)
    print("{} {} {}\n", seen.contains(42), seen.contains(8), seen.count())
    for i in seen.items() { print("{} ", i) }
    print("\n")

    // A map that keeps its keys in order, with range queries.
    m := om.ordered_map(u32, string)
    defer m.deinit()
    try m.put(30, "thirty")
    try m.put(10, "ten")
    try m.put(20, "twenty")
    try m.put(40, "forty")
    print("{} {}\n", m.first(), m.last())
    for e in m.range(15, 40) { print("{}={} ", e.key, e.value) }   // 15 <= key < 40
    print("\n")
    for e in m.entries() { print("{} ", e.key) }
    print("\n")
}
```

`ordered_map` orders by `<`, or by a function you give it (`om.ordered_map_by(K, V, less)`, as `heap_by` does). Lookups, insertions and removals take O(log n) comparisons.

## Bytes and encodings

`std/bytes` reads and writes integers in a chosen byte order and takes a record apart with a cursor. `std/encoding/hex` and `std/encoding/base64` are RFC 4648. All of them write into a buffer you supply or append to a list:

```gamma
bytes :: import "std/bytes"
hex :: import "std/encoding/hex"
base64 :: import "std/encoding/base64"

main :: fn() -> AllocError!() \ {Out, Alloc} {
    packet := list(u8)
    defer packet.deinit()
    try bytes.append_int(u16, &packet, 0xCAFE, .Big)
    try bytes.append_int(u32, &packet, 7, .Little)
    try packet.extend("hi")

    hexbuf: [32]u8 = ---
    print("{}\n", hex.encode(packet.items, hexbuf[..], false))     // cafe070000006869

    r := bytes.Reader(data: packet.items)
    magic := r.int(u16, .Big) else return ()
    len := r.int(u32, .Little) else return ()
    body := r.take(2) else return ()
    print("{:x} {} {}\n", magic, len, body)
    print("{}\n", r.take(5))                                       // too few bytes left: null

    b64: [32]u8 = ---
    text := base64.encode("foobar", b64[..], base64.Options())
    print("{}\n", text)
    raw: [24]u8 = ---
    print("{}\n", base64.decode(text, raw[..], base64.Options()))
    print("{}\n", base64.decode("Zm9v!", raw[..], base64.Options()))

    short: [4]u8 = ---
    print("{}\n", hex.decode("4g", short[..]))
}
```

### Checksums and digests: `std/hash`

`std/hash` has the two fixed, unkeyed functions programs use to check data: CRC-32 (the checksum of zlib, gzip and PNG) and SHA-256. Each works in one call or as a stream you feed in pieces, with the same result however the data is split. Digests are plain `[32]u8` arrays; `std/encoding/hex` makes them text:

```gamma
hash :: import "std/hash"
hex :: import "std/encoding/hex"

main :: fn() \ {Out} {
    print("{:08x}\n", hash.crc32.checksum("123456789"))

    d := hash.sha256.sum("abc")
    out: [64]u8 = ---
    print("{}\n", hex.encode(d[..], out[..], false))

    s := hash.sha256.init()
    for piece in [_]string("a", "", "b", "c") { s.update(piece) }
    print("{}\n", s.final() is d)
}
```

```
cbf43926
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
true
```

These are for detecting corruption and identifying content. Ciphers, keyed MACs, signatures and password hashing are not in the standard library; use a package that is maintained for it.

### JSON: `std/json`

`std/json` parses RFC 8259 JSON into a tree of `Value`s and writes it back. The parser accepts exactly what the RFC allows (no trailing commas, no comments, no leading zeros) and its errors carry the byte offset. A `Document` owns the memory of its tree and frees it all at once:

```gamma
json :: import "std/json"

main :: fn() -> json.WriteError!() \ {Out, Alloc} {
    text := "{\"name\": \"gamma\", \"tags\": [1, 2.5, \"x\\u00e9\"], \"n\": 12}"
    doc := json.parse(text) else e => {
        print("bad json: {}\n", e)
        return ()
    }
    defer doc.deinit()

    if name := doc.root.get("name") {
        match name {
            .String(s) => print("name = {}\n", s)
            _ => print("name is not a string\n")
        }
    }
    if v := doc.root.get("n") {
        match v {
            .Number(n) => print("n = {}\n", n.to_i64())             // null unless it is an integer
            _ => {}
        }
    }
    tags := doc.root.get("tags") else return ()
    if t := tags.at(1) {
        match t {
            .Number(n) => {
                f := n.to_f64()
                print("{} exact={}\n", f.value, f.exact)
            }
            _ => {}
        }
    }

    // Errors say where.
    bad := "[1, 2,]"
    if !e := json.parse(bad) {
        p := json.position(bad, 6)
        print("{} at line {} column {}\n", e, p.line, p.column)
    }

    // Write the tree back (the last argument is the indent; 0 is compact).
    out := list(u8)
    defer out.deinit()
    try json.write(&out, doc.root, 0)
    print("{}\n", out.items)
}
```

Output can also be produced without building a tree, with a `Writer` that keeps the commas, colons and escaping right:

```gamma
json :: import "std/json"

main :: fn() -> json.WriteError!() \ {Out, Alloc} {
    out := list(u8)
    defer out.deinit()
    w := json.writer(&out, 2)               // indent by two spaces; 0 for compact
    try w.begin_object()
    try w.key("name")
    try w.write_string("Ada \"the first\"")
    try w.key("scores")
    try w.begin_array()
    for s in [_]i32(90, 85, 100) { try w.write_int(s) }
    try w.end_array()
    try w.key("ratio")
    try w.write_float(0.1 + 0.2)
    try w.end_object()
    print("{}\n", out.items)
}
```

A float is written in the shortest form that reads back as the same value, NaN and infinities are an error, and a number parsed from text keeps its exact literal when written again. See the [reference](../std.md#stdjson) for how duplicate keys, lone surrogates and the nesting limit are treated.

## The rest of the tour

| Module | For |
| --- | --- |
| `std/cli` | command-line options as an iterator: short and long flags, values, positional arguments, generated help |
| `std/math` | `sin`, `cos`, `exp`, `log`, `pow`, `sqrt`, ... on `f64`, written in Gamma so every back end gives identical results |
| `std/net` | `send_all` and `recv_exact` over the `Net` effect (tests get an in-memory network) |
| `std/bytes`, `std/encoding/*`, `std/hash`, `std/json` | binary data and formats (above) |
| `std/c` | C's platform-dependent types (`c_int`, `c_long`, ...) for [foreign declarations](14-interop.md) |

A short example of `std/cli` and `std/math` together:

```gamma
cli :: import "std/cli"
math :: import "std/math"

main :: fn() \ {Out} {
    opts := [_]cli.Opt(
        cli.Opt(short: 'v', long: "verbose", help: "print more"),
        cli.Opt(short: 'o', long: "output", value: "file", help: "where to write"),
    )
    given := [_]string("-vo", "out.txt", "input.gm", "--colour")
    for a in cli.parse(given[..], opts[..]) {
        match a {
            .Opt(o) => print("option {} value '{}'\n", opts[o.index].long, o.value)
            .Pos(p) => print("file {}\n", p)
            .Unknown(u) => print("unknown {}\n", u)
            .MissingValue(i) => print("--{} needs a value\n", opts[i].long)
            .UnexpectedValue(i) => print("--{} takes no value\n", opts[i].long)
        }
    }
    print("{:.4} {:.4} {}\n", math.sin(1.0), math.exp(1.0), math.pow(2.0, 10.0))
}
```

When you reach for something the library does not have, check [§22](../../gamma-spec.md#22-the-standard-library-scope-and-admission): the admission rules say which kinds of code belong in the library (what many programs need, what is hard to get right, shared types, the platform boundary) and which belong in packages.

## Summary

- The core is in every module; everything else is `import "std/<module>"`, one subject per module.
- Text and bytes go into your buffers or `List(u8)`; allocation shows in signatures; failures are values with positions.
- `print`, `@write`, `List.write` and `str.format` share one template language; `std/fmt` adds presentations the templates leave out.
- Builtin methods cover numbers, slices and strings; `std/str`, `std/text`, `std/path` and `std/bytes` cover the rest of text and data.
- Files, time, randomness, logging and standard input are effects, so tests substitute them.
- Containers capture their allocator; sequences are iterators.
- `std/hash` and `std/json` cover checksums, digests and JSON; cryptography proper stays in packages.
