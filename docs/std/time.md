# Module `std/time`

[All modules](index.md)

Calendar dates and times, RFC 3339 text, and durations as text.

Dates are in the proleptic Gregorian calendar and times are UTC: there are no time
zones (they need the tz database), no leap seconds and no daylight saving. `Clock`'s
`wall()` is Unix time in nanoseconds, which `from_unix` turns into a `DateTime` and
`to_unix` back. Everything is plain arithmetic: nothing allocates, and only `now`
performs an effect. Text goes into a buffer the caller supplies and the result is the
slice written; each kind of output has a constant giving a size that always suffices.

    time :: import "std/time"
    t := time.now()                                    // \ {Clock}
    buf: [time.RFC3339_LEN]u8 = ---
    print("{}\n", time.format_rfc3339(t, buf[..]))     // 2024-05-17T09:30:00Z
    d := try time.parse_date("2024-02-29")
    next := time.add_months(d, 12)                     // 2025-02-28

## Structs

### `Date`

```gamma
Date :: struct {
    pub year: i32
    pub month: u8
    pub day: u8
}
```

A calendar date. Functions that take a `Date` expect a valid one (see `is_valid`)
unless they say otherwise.

Fields:

- `month`: 1 to 12.
- `day`: 1 to 31, and at most the length of the month.

### `DateTime`

```gamma
DateTime :: struct {
    pub date: Date
    pub hour: u8
    pub minute: u8
    pub second: u8
    pub nanosecond: u32
}
```

A date with a time of day in UTC.

Fields:

- `hour`: 0 to 23.
- `minute`: 0 to 59.
- `second`: 0 to 59.
- `nanosecond`: 0 to 999_999_999.

## Unions

### `Weekday`

```gamma
Weekday :: union {
    Monday
    Tuesday
    Wednesday
    Thursday
    Friday
    Saturday
    Sunday
}
```

A day of the week.

### `ParseError`

```gamma
ParseError :: union {
    Syntax: usize
    Range: usize
}
```

Why parsing a date or time failed.

Variants:

- `Syntax`: The text does not have the expected shape; the byte index of the first byte that
  does not fit, or the length of the text if it ended too early.
- `Range`: The shape is right but a field is out of range (month 13, hour 24, February 30);
  the byte index where that field starts. A text with a syntax error reports it
  before any range error.

## Constants

### `RFC3339_LEN`

```gamma
RFC3339_LEN :: 30
```

The size of a buffer that always holds the output of `format_rfc3339`:
`YYYY-MM-DDTHH:MM:SS.fffffffffZ`.

### `DURATION_LEN`

```gamma
DURATION_LEN :: 32
```

The size of a buffer that always holds the output of `format_duration`.

## Functions

### `is_leap_year`

```gamma
is_leap_year :: fn(year: i32) -> bool
```

Whether `year` is a leap year: divisible by 4, except centuries not divisible by 400.

### `days_in_month`

```gamma
days_in_month :: fn(year: i32, month: u8) -> u8
```

The number of days in `month` (1 to 12) of `year`.

### `is_valid`

```gamma
is_valid :: fn(d: Date) -> bool
```

Whether `d` names a real day: the month is 1 to 12 and the day is within the month.

### `days_from_civil`

```gamma
days_from_civil :: fn(d: Date) -> i64
```

The number of days from 1970-01-01 to `d`, negative before it (Howard Hinnant's
`days_from_civil`).

### `civil_from_days`

```gamma
civil_from_days :: fn(days: i64) -> Date
```

The date `days` days after 1970-01-01 (before it when negative); the inverse of
`days_from_civil`.

### `weekday`

```gamma
weekday :: fn(d: Date) -> Weekday
```

The day of the week of `d`.

### `add_days`

```gamma
add_days :: fn(d: Date, n: i64) -> Date
```

`d` moved by `n` days, which may be negative.

### `add_months`

```gamma
add_months :: fn(d: Date, n: i32) -> Date
```

`d` moved by `n` months, which may be negative. The day is kept unless the target
month is shorter, when it becomes the last day of that month: January 31 plus one
month is February 28 or 29.

### `from_unix`

```gamma
from_unix :: fn(ns: i64) -> DateTime
```

The date and time `ns` nanoseconds after 1970-01-01T00:00:00Z. Negative values round
toward the past: -1 is 1969-12-31T23:59:59.999999999.

### `to_unix`

```gamma
to_unix :: fn(t: DateTime) -> i64
```

The nanoseconds from 1970-01-01T00:00:00Z to `t`; the inverse of `from_unix`. Traps
if the result does not fit an `i64` (roughly the years 1678 to 2262).

### `now`

```gamma
now :: fn() -> DateTime \ {Clock}
```

The current date and time from the `Clock`'s wall clock.

### `format_rfc3339`

```gamma
format_rfc3339 :: fn(t: DateTime, buf: []u8) -> string
```

Formats `t` as RFC 3339 in UTC, `2024-05-17T09:30:00Z`, into `buf` and returns the
bytes written. A fraction is added only when `t.nanosecond` is not zero, with 3, 6
or 9 digits, the fewest that hold it exactly (`.500`, `.000001`). The year must be
in 0 to 9999 and the fields valid. Traps if `buf` is too small (`RFC3339_LEN` bytes
always suffice).

### `format_date`

```gamma
format_date :: fn(d: Date, buf: []u8) -> string
```

Formats `d` as `YYYY-MM-DD` into `buf` (10 bytes suffice) and returns the bytes
written. The year must be in 0 to 9999.

### `parse_date`

```gamma
parse_date :: fn(s: string) -> ParseError!Date
```

Parses `YYYY-MM-DD` (four-digit year, years 0000 to 9999).

A text that is not of that shape is `Syntax` with the position of the first wrong
byte; a month outside 1 to 12 is `Range(5)` and a day that the month does not have
is `Range(8)`.

### `parse_rfc3339`

```gamma
parse_rfc3339 :: fn(s: string) -> ParseError!DateTime
```

Parses an RFC 3339 timestamp and gives it in UTC.

The form is `YYYY-MM-DDTHH:MM:SS`, optionally followed by `.` and 1 to 9 fraction
digits, and then either `Z` or an offset `+HH:MM` / `-HH:MM`; the offset is applied
so the result is UTC. The separator may be `T`, `t` or a space, and `Z` may be `z`.
A leap second (`:60`) is out of range.

Errors: `Syntax(i)` for the first byte that does not fit (the length of the text if it
ended too early, and the index of the first byte after a complete timestamp if there
is more); otherwise `Range(i)` where `i` is the start of the first field that is out of
range: month (5), day (8), hour (11), minute (14), second (17), or the offset's hour or
minute; `Range(0)` if the offset moves the moment out of the years 0 to 9999.

### `format_duration`

```gamma
format_duration :: fn(d: Duration, buf: []u8) -> string
```

Formats `d` into `buf` and returns the bytes written, in a form like Go's: `0s`,
`250ns`, `1.5us`, `1.5ms`, `3.25s`, `1m0s`, `1h2m3.5s`. Below a second it is one number
with the largest unit (`ns`, `us` or `ms`) that leaves the integer part non-zero;
from a second up it is hours, minutes and seconds, leaving out the hours when they
are zero (and the minutes when both are). Fractions have no trailing zeros. Traps if
`buf` is too small (`DURATION_LEN` bytes always suffice).
