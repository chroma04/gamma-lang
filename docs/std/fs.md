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

Why reading a whole file failed (`io.ReadError` is the same type).

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

## Constants

### `TEMP_EXTRA`

```gamma
TEMP_EXTRA :: 17
```

The bytes `create_temp` and `make_temp_dir` need in their buffer beyond the length of
`prefix`: a buffer of `dir.len + prefix.len + TEMP_EXTRA` bytes always suffices (a
separator and 16 hex digits).

## Functions

### `read_all`

```gamma
read_all :: fn(path: string) -> ReadError![]u8 needs Fs + Alloc
```

The contents of the file at `path`, in memory from the current allocator; release it
with `free(data)`.

### `canonical_alloc`

```gamma
canonical_alloc :: fn(path: string) -> ReadError![]u8 needs Fs + Alloc
```

The absolute path of `path` with symbolic links, `.` and `..` resolved, in memory from the
current allocator; release it with `free(path)`. `Fs.canonical` is the form that writes into
a buffer you supply. The path must exist.

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

### `remove_all`

```gamma
remove_all :: fn(path: string) -> IoError!() needs Fs + Alloc
```

Removes `path` and everything in it: a file or a symbolic link is removed, a directory
is emptied and then removed, like `rm -rf`. Symbolic links are never followed (a link to
a directory is removed as a link and its target is left alone). An entry that vanishes
while the removal runs is not an error. If `path` itself does not exist the result is
`.NotFound`, which a caller that does not mind can ignore. Any other failure ends the
removal at once, leaving the rest in place. The directories being emptied are tracked in
memory from the current allocator (`.NoSpace` if it runs out), not on the call stack,
and all of them are closed again on every exit.

### `temp_dir`

```gamma
temp_dir :: fn() -> string needs Env
```

The directory for temporary files. On Windows it is the value of `TEMP`, else `TMP`,
else `C:\Windows\Temp`; elsewhere it is `TMPDIR`, else `/tmp`. A variable that is empty
counts as unset. The result is not checked to exist.

### `create_temp`

```gamma
create_temp :: fn(dir, prefix: string, buf: []u8) -> IoError!(file: File, path: string) needs Fs + Rand
```

Creates a new file in `dir`, opened for reading and writing, and returns it with its
path. The name is `prefix` followed by 16 random lowercase hex digits (from `Rand`);
the path is `dir`, a `/` (unless `dir` is empty or ends with one) and the name, written
to the start of `buf` and returned as the used part. The file is created exclusively, so
an existing file is never opened; a name that is taken is replaced by a new random one,
up to 16 times, and then the result is `.AlreadyExists`. A `buf` shorter than
`dir.len + prefix.len + TEMP_EXTRA` bytes, or a `prefix` with a separator in it (`/`, and
on Windows also `\`), gives
`.InvalidInput` and creates nothing. The caller closes the file and removes it.

### `make_temp_dir`

```gamma
make_temp_dir :: fn(dir, prefix: string, buf: []u8) -> IoError!string needs Fs + Rand
```

Creates a new directory in `dir` and returns its path, named as for `create_temp`
(`prefix` and 16 random hex digits) and written to `buf` in the same way, with the same
retries and errors. The caller removes it, for instance with `remove_all`.

### `write_atomic`

```gamma
write_atomic :: fn(path: string, bytes: []const u8) -> IoError!() needs Fs + Rand
```

Replaces the contents of the file at `path` with `bytes` so that no reader ever sees a
partial file: the bytes go to a new file next to `path` (named after it with a random
suffix), which is synced and then renamed over `path`. The new file is removed again
if anything fails. The suffix comes from the `Rand` effect. `path` may be at most 4000
bytes long (`InvalidInput` otherwise).
