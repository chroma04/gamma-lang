# Module `std/process`

[All modules](index.md)

Running other programs over the `Proc` effect (§21.7).

    process :: import "std/process"
    out := try process.output([_]string("git", "rev-parse", "HEAD"))
    defer out.deinit()
    if process.success(out.status) { print("{}", string(out.out.items)) }

`Proc.spawn` starts a program without a shell: the first element of `argv` is the program
(a name without a path separator is searched for in `PATH`), the others are its arguments.
A piped stream is used through `write`, `close_in` and `read`, and every started
child must be ended with `wait`. Tests cannot start processes: `spawn` fails with
`PermissionDenied`.

## Structs

### `Output`

```gamma
Output :: struct {
    pub status: ExitStatus
    pub out: List(u8)
    pub err: List(u8)
}
```

A finished program: its status and everything it wrote to its standard output and error.

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
