# Module `std/process`

[All modules](index.md)

Running other programs over the `Proc` effect (§21.7).

    process :: import "std/process"
    out := try process.output([_]string("git", "rev-parse", "HEAD"))
    defer out.deinit()
    if process.success(out.status) { print("{}", string(out.out.items)) }

`Proc.spawn` starts a program without a shell: the first element of `argv` is the program
(a name without a path separator is searched for in `PATH`), the others are its arguments.
With `SpawnOptions.program` set, that file is run instead and `argv[0]` is only the name the
child sees (like `execv(path, argv)`); a `program` without a separator is searched for in
`PATH` too. A piped stream is used through `write`, `close_in` and `read`, and every started
child must be ended with `wait`, or with a `try_wait` that returned its status (`null` while
the child still runs). Tests cannot start processes: `spawn` fails with
`PermissionDenied`.

A program that may not finish is run with `output_within`, which kills it (with everything
it started, if `SpawnOptions.group` is set) once a time limit is up; `watch` puts the same
limit on a child that the caller drives itself.

## Structs

### `Output`

```gamma
Output :: struct {
    pub status: ExitStatus
    pub out: List(u8)
    pub err: List(u8)
    pub timed_out: bool = false
}
```

A finished program: its status and everything it wrote to its standard output and error.

Fields:

- `timed_out`: The program was killed because it outlived the limit of `output_within`.

### `Watchdog`

```gamma
Watchdog :: struct {
    // private fields omitted
}
```

The time limit on a running child: a thread that kills the child (with everything in its
process group if it was started with `group`) once the limit is up, unless it is disarmed
first.

## Unions

### `RunError`

```gamma
RunError :: union {
    Io: IoError
    Alloc: AllocError
}
```

Why running a program to completion failed.

## Type aliases

### `Child`

```gamma
Child :: core.Child
```

A started child process (§21.7).

### `Stdio`

```gamma
Stdio :: core.Stdio
```

What a child's standard stream is connected to: `Inherit`, `Null` or `Pipe`.

### `Stream`

```gamma
Stream :: core.Stream
```

One of a child's output streams: `Stdout` or `Stderr`.

### `SpawnOptions`

```gamma
SpawnOptions :: core.SpawnOptions
```

How `Proc.spawn` starts a process: the three streams, the working directory and the
environment.

### `Chunk`

```gamma
Chunk :: core.Chunk
```

What `Proc.read` reports: the stream the bytes are from, and how many it stored.

### `ExitStatus`

```gamma
ExitStatus :: core.ExitStatus
```

How a child ended: `Exited(code)` or `Signaled(signal)`.

## Functions

### `deinit`

```gamma
deinit :: fn(o: ^Output)
```

Frees the output.

### `success`

```gamma
success :: fn(s: ExitStatus) -> bool
```

True if the program ended with exit code 0.

### `output`

```gamma
output :: fn(argv: []const string, opts: SpawnOptions = SpawnOptions()) -> RunError!Output needs Proc + Alloc
```

Runs the program to completion and collects what it writes to its standard output and
error (`opts.stdout` and `opts.stderr` are replaced by pipes; `opts.stdin` is used as
given). Release the result with `deinit`. If collecting fails, the child is killed and
waited for.

### `status`

```gamma
status :: fn(argv: []const string, opts: SpawnOptions = SpawnOptions()) -> IoError!ExitStatus needs Proc
```

Runs the program to completion, with `opts` as given, and returns how it ended.

### `output_within`

```gamma
output_within :: fn(argv: []const string, limit: Duration, opts: SpawnOptions = SpawnOptions(), input: ?[]const u8 = null) -> RunError!Output needs Proc + Alloc + Clock + Sys
```

Runs the program like `output`, but kills it once it has run for `limit` (with its whole
process group if `opts.group` is set; the program otherwise leaves its own children
running, and they may keep the pipes open). `timed_out` of the result tells that the limit
ended it. With `input`, the bytes are written to the program's standard input, which is
then closed (`opts.stdin` is replaced by a pipe); the writing runs on a thread of its own,
so a program that writes much before it reads all its input does not block. Release the
result with `deinit`. Needs `Sys` for the threads.

### `watch`

```gamma
watch :: fn(child: Child, limit: Duration) -> RunError!Watchdog needs Alloc + Proc + Sys
```

Starts a watchdog that kills `child` after `limit`. If it cannot be started, the child is
killed and waited for, and the error is returned.

### `disarm`

```gamma
disarm :: fn(w: ^Watchdog) -> bool needs Sys
```

Stops the watchdog, once, and returns whether it killed the child. Call it before the
child is waited for (`wait` releases the child's handle, which a later child may get), and
do not use the watchdog afterwards.
