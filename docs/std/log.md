# Module `std/log`

[All modules](index.md)

Logging as an effect.

Code writes records with `info`, `warn` and the like, and whoever runs it chooses what
happens to them by installing a handler for `Log`: print them to standard error, collect
them in a list, or drop them. A record is formatted like `print` does, piece by piece
into the handler, so logging allocates nothing, truncates nothing and keeps no global state.

    log :: import "std/log"

    sink := log.ErrLog(min: .Warn)              // records of level Warn and up
    with &sink {                                // installed by address, since it has state
        log.info("starting")                    // dropped: below the minimum
        log.warn("slow request: {} ms", 250)    // WARN slow request: 250 ms
    }

A handler decides in `begin` whether it wants a record of a given level. Only if it does
is the message formatted, so a filtered-out record costs next to nothing.

## Structs

### `ErrLog`

```gamma
ErrLog :: struct {
    pub min: Level = .Info
}
```

A handler that prints records of level `min` and above to standard error, one per line:
`INFO message`. It needs only the `Err` effect; use `StampedLog` to add the time.

### `StampedLog`

```gamma
StampedLog :: struct {
    pub min: Level = .Info
}
```

Like `ErrLog`, but every line starts with the current UTC time from the `Clock` effect,
to the second: `2026-10-12T04:38:42Z INFO message`.

### `ListLog`

```gamma
ListLog :: struct {
    pub list: ^List(u8)
    pub min: Level = .Debug
    pub err: ?AllocError = null
}
```

A handler that appends records of level `min` and above to `list`, one line each, as
`INFO message`. The first allocation failure is kept in `err` and later output is dropped.

### `Discard`

```gamma
Discard :: struct {}
```

A handler that wants no records: nothing is formatted or written.

## Unions

### `Level`

```gamma
Level :: union {
    Debug
    Info
    Warn
    Error
}
```

The importance of a record, from least to most. Levels compare with `<`.

## Effects

### `Log`

```gamma
Log :: effect {
    begin: fn(level: Level) -> bool
    write: fn(bytes: []const u8)
    end: fn()
}
```

Where records go. `log` calls `begin`; if that returns `true` it passes the message to
`write` in pieces and ends the record with `end`.

## Handlers

### `handler Log for ^ErrLog`

```gamma
handler Log for ^ErrLog \ {Err}
```

### `handler Log for ^StampedLog`

```gamma
handler Log for ^StampedLog \ {Err, Clock}
```

### `handler Log for ^ListLog`

```gamma
handler Log for ^ListLog
```

### `handler Log for ^Discard`

```gamma
handler Log for ^Discard
```

## Functions

### `log`

```gamma
log :: fn(A: type, level: Level, template :: string, args: ..A) \ {Log}
```

Writes a record of `level`, formatted from `template` and `args` as `print` does.

### `debug`

```gamma
debug :: fn(A: type, template :: string, args: ..A) \ {Log}
```

Writes a `Debug` record.

### `info`

```gamma
info :: fn(A: type, template :: string, args: ..A) \ {Log}
```

Writes an `Info` record.

### `warn`

```gamma
warn :: fn(A: type, template :: string, args: ..A) \ {Log}
```

Writes a `Warn` record.

### `error`

```gamma
error :: fn(A: type, template :: string, args: ..A) \ {Log}
```

Writes an `Error` record.
