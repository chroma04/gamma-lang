# Module `std/fs`

[All modules](index.md)

Whole-file operations over the `Fs` effect (§21.7).

## Structs

### `Entry`

```gamma
Entry :: struct {
    pub name: string
    pub kind: FileKind
}
```

One entry of a directory: its name (valid until the next entry is produced) and what it
is.

### `WalkEntry`

```gamma
WalkEntry :: struct {
    pub path: string
    pub kind: FileKind
    pub depth: u32
}
```

An entry found by `walk`: its path (the root joined with the names down to it), what it
is, and its depth, which is 0 for the entries directly in the root.

## Unions

### `ReadError`

```gamma
ReadError :: union {
    Io: IoError
    Alloc: AllocError
}
```

Why reading a whole file failed.

## Type aliases

### `File`

```gamma
File :: core.File
```

An open file: a handle that the `Fs` effect's operations take (§21.7).

### `Dir`

```gamma
Dir :: core.Dir
```

An open directory, read with `Fs.next_entry`.

### `OpenOptions`

```gamma
OpenOptions :: core.OpenOptions
```

How `Fs.open` opens a file: `OpenOptions(write: true, create: true)`.

### `FileKind`

```gamma
FileKind :: core.FileKind
```

What a path names: a file, a directory, a symbolic link, or something else.

### `FileInfo`

```gamma
FileInfo :: core.FileInfo
```

What `Fs.stat` and `Fs.lstat` report about a path.

### `DirEntry`

```gamma
DirEntry :: core.DirEntry
```

A directory entry from `Fs.next_entry`: the length of its name, and what it is.

### `SeekFrom`

```gamma
SeekFrom :: core.SeekFrom
```

Where `Fs.seek` counts from.

## Functions

### `read_all`

```gamma
read_all :: fn(path: string) -> ReadError![]u8 needs Fs + Alloc
```

The contents of the file at `path`, in memory from the current allocator; release it
with `free(data)`.

### `write_all_to`

```gamma
write_all_to :: fn(f: File, bytes: []const u8) -> IoError!() needs Fs
```

Writes all of `bytes` to `f`, retrying short writes. A write that makes no progress
(returns 0) fails with `.Other(0)`.

### `write_all`

```gamma
write_all :: fn(path: string, bytes: []const u8) -> IoError!() needs Fs
```

Replaces the contents of the file at `path` with `bytes`, creating it if needed.

### `append`

```gamma
append :: fn(path: string, bytes: []const u8) -> IoError!() needs Fs
```

Appends `bytes` to the file at `path`, creating it if needed.

### `exists`

```gamma
exists :: fn(path: string) -> bool needs Fs
```

Whether something exists at `path`. A symbolic link counts only if its target exists.

### `copy`

```gamma
copy :: fn(from, to: string) -> IoError!() needs Fs
```

Copies the file at `from` to `to`, replacing `to`. Copying a file onto itself leaves it
unchanged.

### `entries`

```gamma
entries :: fn(path: string) needs Yield(IoError!Entry) + Fs
```

The entries of the directory at `path`, in the order the operating system gives them
(never `.` or `..`). It opens the directory and closes it again, also when the loop is
left early. An error (opening the directory, or reading an entry) is yielded as an
error and ends the iteration.

    for r in fs.entries("src") {
        e := try r
        print("{}\n", e.name)
    }

### `walk`

```gamma
walk :: fn(root: string) needs Yield(IoError!WalkEntry) + Fs + Alloc
```

Every entry below the directory `root` (which is not itself yielded), depth first and
in pre-order: a directory comes before what it contains, and the entries of one
directory are in the order the operating system gives them. Symbolic links are not
followed; they are yielded with the kind `.Symlink`. A `path` is valid until the next
entry is produced. A directory that cannot be read yields its error and the walk
goes on with the rest; a failure to list `root` itself, or to allocate (`.NoSpace`), yields
an error and ends the walk. Everything allocated and every directory opened is released when the
walk finishes or the loop is left early.

    for r in fs.walk("src") {
        e := try r
        if e.kind is .File { print("{}\n", e.path) }
    }

### `make_dir_all`

```gamma
make_dir_all :: fn(path: string) -> IoError!() needs Fs
```

Creates the directory `path` and any missing parents, like `mkdir -p`. Directories that
exist already are fine; something else at `path`, or at one of its parents, is an
error.

### `write_atomic`

```gamma
write_atomic :: fn(path: string, bytes: []const u8) -> IoError!() needs Fs + Rand
```

Replaces the contents of the file at `path` with `bytes` so that no reader ever sees a
partial file: the bytes go to a new file next to `path` (named after it with a random
suffix), which is synced and then renamed over `path`. The new file is removed again
if anything fails. The suffix comes from the `Rand` effect. `path` may be at most 4000
bytes long (`InvalidInput` otherwise).
