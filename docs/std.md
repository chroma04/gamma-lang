# The standard library

The **core library** (spec §21) is visible in every module without an import: the
ambient effects, allocation, iterators, `List`, formatting with `print`,
and the builtin methods of slices, numbers and pointers. The rest of the standard
library is a set of modules under `std/`, imported by path:

```gamma
io :: import "std/io"
fs :: import "std/fs"
```

The `gamma` tool finds them in the directory named by `$GAMMA_STD`; otherwise, for an
installed release (`<prefix>/bin/gamma`), in `<prefix>/lib/gamma/std`; otherwise in
`lib/std` next to the sources the compiler was built from.

The reference for every module (its types, effects, handlers, constants and functions,
with their documentation) is generated from the doc comments of `lib/std` and lives in
[`std/`](std/index.md); start at the [module index](std/index.md). The rest of this page
covers what the modules do not: the ambient effects and the facilities built into the
language.

## Ambient effects and their handlers

`main` may perform any ambient effect (§12.9); the runtime installs the default
handlers, and tests run with the test handlers instead.

| Effect | Operations | Default handler | Test handler |
| --- | --- | --- | --- |
| `Out`, `Err` | `write(bytes)` | stdout, stderr | captured |
| `In` | `read(buf) -> IoError!usize` (0 at the end) | stdin | empty input |
| `Clock` | `now() -> Instant`, `wall() -> i64`, `sleep(d)` | system clocks | fake clock; `sleep` advances it |
| `Rand` | `fill(buf)` | the OS's generator | fixed seed |
| `Fs` | files and directories (below) | the real file system | in-memory file system |
| `Net` | TCP connections (below) | real sockets | loopback fake: an in-memory network with one host (`localhost`, `127.0.0.1` and `::1`); nothing blocks, so `accept` and `recv` give `WouldBlock` instead of waiting |
| `Env` | `args()`, `var(name)`, `exit(code)` | process arguments and environment | no arguments or variables; `exit` ends the test, which passes only with code 0 |
| `Alloc` | `current() -> Allocator` | the system heap | leak-checking heap |

Durations are built with `time.ns(n)`, `time.us(n)`, `time.ms(n)` and `time.secs(n)` (from
`std/time`); `i.since(earlier)`,
`i.after(d)`, `d.plus(e)`, `d.minus(e)` and `d.to_ms()` work with them.

### Files: `Fs`

```gamma
f := try Fs.open("notes.txt", OpenOptions(read: false, write: true, create: true, truncate: true))
defer f.close()
_ = try f.write("hello\n")
```

`Fs.open(path, options)` returns a `File`; `read`, `write`, `seek` and `close` are called
as methods on it (`f.read(buf)` is `Fs.read(f, buf)`). `stat(path)` gives a `FileInfo` (`size`, `kind`, `modified`), where
`kind` is a `FileKind`: `.File`, `.Dir`, `.Symlink` or `.Other`; `lstat(path)` is the
same without following a final symbolic link. `remove`, `rename`, `make_dir` and
`remove_dir` change the tree. `open_dir`, `next_entry` and `close_dir` list a
directory without allocating: `next_entry(buf)` stores a name in `buf` and returns a
`DirEntry` (`len` of the name and its `kind`), or `null` after the last entry.
`f.truncate(size)` sets a file's length, `f.sync()` flushes it to the storage device,
and `cwd(buf)` stores the working directory in `buf` and returns its length
(`.InvalidInput` if it does not fit). Failures are `IoError` values; `.TimedOut` is
what a network call gives when its timeout runs out.

### The network: `Net`

```gamma
l := try Net.listen("127.0.0.1", 0)      // any free port
port := try l.local_port()
c := try Net.connect("localhost", port)
s := try l.accept()
_ = try c.send("ping")
n := try s.recv(buf[..])                 // 0 once the peer has closed
```

`s.close()` (`Net.close`) and `Net.close_listener` release the handles. Hosts are names or address
literals; a failed name lookup is `.Other(0)`. `s.shutdown(how)` closes one direction of
a connection (`how` is a `Shutdown`: `.Read`, `.Write` or `.Both`); after `.Write` the
peer's `recv` returns 0 once it has read everything, while the other direction keeps
working. `s.set_timeouts(recv, send)` bounds how long `recv` and `send` wait (`null`:
forever); a call that runs out of time fails with `.TimedOut`. `s.peer(buf)` stores the
other end's address as `host:port` in `buf` (`127.0.0.1:8080`, `[::1]:8080`) and returns
its length.

The test handlers are in-memory: nothing blocks, so timeouts are accepted and ignored;
`shutdown` has the same effect on `recv` and `send` as it has on real sockets, and
`peer` reports `127.0.0.1` with the port of the other end.

## Builtin methods

Numbers, slices, pointers and unions have methods built into the language (§21.3),
so no module repeats them. The ones people look for most:

- Integers: `x.trunc(T)`, `x.checked(T)`, `x.sat(T)` (narrowing), `x.to(f64)` (to a
  float), `bits(T)`, `min`, `max`, `clamp`, `abs`, `unsigned_abs`, `abs_diff`, `pow` and
  its `pow_wrap`, `pow_sat` and `pow_checked` forms.
- Floats: `round(T)`, `floor(T)`, `ceil(T)`, `trunc(T)` (to integers, checked),
  `checked(T)`, `sat(T)`, `to(F)`, `bits(T)`, `is_nan`, `is_inf`, `is_finite`, `sign_bit`,
  `min`, `max`, `abs`, `sqrt`. (`std/math` has the `f64` functions that are not built in.)
- Slices and strings: `len`, `get(i)`, `first`, `last`, `starts_with`, `ends_with`,
  `index_of`, `copy_from`, `fill`, `swap`, `sort`, `sort_by`, `sort_by_key`, the stable
  sorts, `min`, `max`, `binary_search`, `upper_bound`, `is_sorted`, `reverse`,
  `rotate_left`, `rotate_right`, `dedup`, `partition`, `select_nth`, `utf8_valid`. `is`
  compares strings and slices
  by value and `<` orders them.

## Formatting into your own sink: `@write`

`print` is `@write` aimed at `Out`. `@write(sink, template, args)` takes the same
templates and arguments (including format specs such as `{:>8}` and `{:.2}`) and calls
`sink`, a function literal taking `[]const u8`, with the pieces of output, allocating
nothing:

```gamma
total: usize = 0
@write(fn(b) { total += b.len }, "{} items at {:.2}\n", n, price)   // measures the text
```

For text you keep, `List(u8).write` appends to a list, `str.format` returns a new
allocation, and `io.ListOut` and `io.Capture` are `Out` handlers that collect whatever
code prints.

## Where the module reference comes from

`docs/std/` is written by `gamma doc` (see `gamma help doc`) from the `pub` declarations
of `lib/std`, their `///` comments and the `//!` comment at the top of each module.
Document the library in those comments, not in this directory, and regenerate after
changing them:

```
gamma doc lib/std --all -o docs/std
```

`tools/test.sh --test doc` fails while the checked-in files are out of date. The same
command documents your own packages: `gamma doc <package root> -o <dir>`
(`--format=html` for a browsable site).
