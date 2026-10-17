# Module `std/time/zone`

[All modules](../index.md)

Time zones: converting between UTC and local civil time with TZif data.

A `Zone` is the rules of one place, read from a TZif file (RFC 8536, versions 1 to 4,
such as the files of the IANA time zone database) or from a POSIX `TZ` rule like
`CET-1CEST,M3.5.0,M10.5.0/3`. `offset_at` gives the offset from UTC in force at an
instant, `to_local` turns a UTC `DateTime` into the local one, and `from_local` goes
back, reporting a local time that happens twice (the clocks went back) or never (they
went forward) rather than guessing.

    zone :: import "std/time/zone"
    time :: import "std/time"

    paris := try zone.load("Europe/Paris")                  // needs Fs + Env + Alloc
    defer zone.deinit(&paris)
    local := zone.to_local(&paris, time.now())              // needs Clock
    buf: [zone.RFC3339_LOCAL_LEN]u8 = ---
    print("{}\n", zone.format_rfc3339_local(local, buf[..]))   // 2024-05-17T11:30:00+02:00
    match zone.from_local(&paris, local.time) {
        .Unique(secs) => print("{}\n", secs)
        _ => print("ambiguous or missing\n")
    }

Instants are Unix seconds (`i64`), so every conversion works over the whole range of
`DateTime` and not only the years that `time.to_unix`'s nanoseconds reach;
`secs_from_datetime` and `datetime_from_secs` convert a `DateTime` (UTC) to and from
them. Nanoseconds are carried along unchanged by `to_local` and ignored by `from_local`.

A zone owns its tables, allocated from the allocator that was current when it was
created, and `deinit` releases them; nothing else allocates and no operation on an
existing zone can fail. `utc` and `fixed` need no allocation and their `deinit` does
nothing. Leap seconds, which some TZif files record, are skipped: like `std/time`, a
zone counts every day as 86400 seconds, so the "right/" variants of the database
give instants up to a few tens of seconds off. The rules apply for the whole range of
instants: before the first recorded transition the first local time type is in force,
after the last one the footer rule, or the last transition's type if the footer is
empty.

## Structs

### `Offset`

```gamma
Offset :: struct {
    pub seconds: i32
    pub is_dst: bool
    pub abbreviation: string
}
```

The offset from UTC in force at some instant.

Fields:

- `seconds`: Seconds east of UTC.
- `is_dst`: Whether this is daylight saving time.
- `abbreviation`: The abbreviation in use (`CET`, `CEST`, `+03`). It points into the zone it came
  from and is valid until `deinit`. Empty for a zone made by `fixed` (other than
  `fixed(0)`, which is `UTC`).

### `LocalTime`

```gamma
LocalTime :: struct {
    pub time: time.DateTime
    pub offset: Offset
}
```

A local date and time together with the offset that was applied to UTC to get it.

### `Zone`

```gamma
Zone :: struct {
    // private fields omitted
}
```

The rules of a time zone. Make one with `load`, `local`, `parse`, `from_tz_string`,
`utc` or `fixed`; release it with `deinit`.

## Unions

### `ZoneError`

```gamma
ZoneError :: union {
    Io: IoError
    Alloc: AllocError
    Invalid: usize
}
```

Why a zone could not be made.

Variants:

- `Io`: The file could not be read.
- `Alloc`: The tables could not be allocated.
- `Invalid`: The TZif data (or the rule string) is malformed; the byte offset where it is, or 0
  when it is not tied to one place.

### `Resolved`

```gamma
Resolved :: union {
    Unique: i64
    Ambiguous: (earlier: i64, later: i64)
    Missing: (before: i64, after: i64)
}
```

What a local time means in a zone, as Unix seconds.

Variants:

- `Unique`: The local time happens once.
- `Ambiguous`: The clocks went back and the local time happens twice: the earlier and the later
  instant.
- `Missing`: The clocks went forward and the local time never happens. The values are the
  time read with the offset before the gap and with the offset after it (so `before`
  is the later instant when the clocks went forward).

## Constants

### `RFC3339_LOCAL_LEN`

```gamma
RFC3339_LOCAL_LEN :: time.RFC3339_LEN + 5
```

The size of a buffer that always holds the output of `format_rfc3339_local`.

## Functions

### `secs_from_datetime`

```gamma
secs_from_datetime :: fn(t: time.DateTime) -> i64
```

The Unix seconds of `t`, a date and time in UTC; its nanoseconds are ignored.

### `datetime_from_secs`

```gamma
datetime_from_secs :: fn(s: i64, nanosecond: u32 = 0) -> time.DateTime
```

The date and time in UTC of Unix seconds `s`, with `nanosecond` carried over. Traps if
the year does not fit an `i32`.

### `utc`

```gamma
utc :: fn() -> Zone
```

UTC: offset 0, abbreviation `UTC`. No allocation.

### `fixed`

```gamma
fixed :: fn(seconds: i32) -> Zone
```

A zone with a constant offset from UTC (`seconds` east of it), the abbreviation empty
(`UTC` for 0). No allocation.

### `deinit`

```gamma
deinit :: fn(z: ^Zone)
```

Releases the zone's tables through the allocator it was made with; the zone is `utc()`
afterwards. Does nothing for the zones of `utc` and `fixed`.

### `from_tz_string`

```gamma
from_tz_string :: fn(s: string) -> ZoneError!Zone needs Alloc
```

A zone from a POSIX TZ string such as `CET-1CEST,M3.5.0,M10.5.0/3` or `<+03>-3`: the
standard abbreviation and offset (hours west of UTC, so `EST5` is UTC-5), optionally
followed by a daylight saving abbreviation, its offset (one hour more than the standard
one if left out) and the rule for when it starts and ends, `Mm.w.d` (month, week 1 to 5
with 5 the last, weekday from 0 for Sunday), `Jn` (day 1 to 365, February 29 never
counted) or `n` (day 0 to 365, February 29 counted), each with `/time` after it
(default 02:00; a sign and an hour up to 167 are allowed). With a daylight saving
abbreviation and no rule, the United States rule applies (`M3.2.0,M11.1.0`). The rule
is evaluated per calendar year, so a start or end time that reaches into another
year is not followed there.

`Invalid(i)` is the byte index of the first thing that does not fit.

### `parse`

```gamma
parse :: fn(data: []const u8) -> ZoneError!Zone needs Alloc
```

A zone from the bytes of a TZif file (RFC 8536 versions 1 to 4). The data is copied and
may be freed afterwards. Version 2 and later files are read from their 64-bit block
and footer, version 1 files from their 32-bit one. The footer, a POSIX TZ string (see
`from_tz_string`), gives the rule for instants after the last transition. Leap second
records and the standard/wall and UT/local indicators are skipped.

`Invalid(i)` is the byte offset in `data` of the field that is wrong (the length of
`data` if it ends too early): the magic or version, a count that is zero where it must
not be, transitions out of order, a type index or abbreviation index out of range, or
a footer that is not a rule.

### `load_from`

```gamma
load_from :: fn(dir, name: string) -> ZoneError!Zone needs Fs + Alloc
```

Loads the zone `name` (such as `Europe/Paris`) from the directory `dir`. A name that is
empty, begins with `/` or has a `..` component, or a path longer than 4095 bytes, is
`Io(InvalidInput)`.

### `load`

```gamma
load :: fn(name: string) -> ZoneError!Zone needs Fs + Env + Alloc
```

Loads the zone `name` from `$TZDIR` if that is set and not empty, otherwise from
`/usr/share/zoneinfo`. See `load_from` for the names that are rejected; a missing zone
is `Io(NotFound)`.

### `local`

```gamma
local :: fn() -> ZoneError!Zone needs Fs + Env + Alloc
```

The zone the environment asks for, as the C library reads it:

- `$TZ` unset: the file `/etc/localtime`;
- `$TZ` empty: UTC;
- `$TZ` as `:name`: the zone `name` as `load` finds it;
- `$TZ` as anything else: the zone `name` if there is one, otherwise the string as a
  POSIX TZ rule (see `from_tz_string`).

A zone that does not exist, and a `$TZ` that is neither a zone nor a rule, give UTC; only
data that exists and is malformed, or running out of memory, is an error.

### `offset_at`

```gamma
offset_at :: fn(z: ^const Zone, unix_seconds: i64) -> Offset
```

The offset from UTC in force at the instant `unix_seconds`.

Before the first transition of the table the first local time type is in force; from
the last transition on, the footer rule (or the last transition's type if there is
none).

### `to_local`

```gamma
to_local :: fn(z: ^const Zone, t: time.DateTime) -> LocalTime
```

The local date and time at the UTC instant `t`, with the offset that was applied.
Traps if the local year does not fit an `i32`.

### `from_local`

```gamma
from_local :: fn(z: ^const Zone, local: time.DateTime) -> Resolved
```

What the local (wall clock) date and time `local` is in `z`: one instant (`Unique`),
two (`Ambiguous`, when the clocks went back over it) or none (`Missing`, when they
went forward over it). Only whole seconds count.

The offsets in force one day before and one day after the local time (read as UTC) are
the candidates, so two transitions less than two days apart around `local` are not
told apart.

### `format_rfc3339_local`

```gamma
format_rfc3339_local :: fn(t: LocalTime, buf: []u8) -> string
```

Formats `t` as RFC 3339 with its numeric offset, `2024-05-17T11:30:00+02:00`, into `buf`
and returns the bytes written. The fraction follows the rules of `time.format_rfc3339`.
`Z` is never used: UTC is `+00:00`. RFC 3339 offsets have only hours and minutes, so an
offset with seconds (the "local mean time" of old dates) is rounded to the nearest
minute, half away from zero. The offset must be less than 100 hours. Traps if `buf` is
too small (`RFC3339_LOCAL_LEN` bytes always suffice).
