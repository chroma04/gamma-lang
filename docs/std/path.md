# Module `std/path`

[All modules](index.md)

POSIX path strings, with `/` as the separator.

Every function works on the bytes of a `string` and is purely lexical: nothing looks
at the file system, nothing performs an effect, and nothing allocates unless it says
so. Functions that return a `string` return a slice of their argument (or of the
buffer the caller supplied).

    path :: import "std/path"
    path.base_name("/usr/lib/libc.so.6")        // "libc.so.6"
    path.dir_name("/usr/lib/libc.so.6")         // "/usr/lib"
    path.extension("libc.so.6")                 // "6"
    buf: [64]u8 = ---
    path.clean("a//b/./c/../d", buf[..])        // "a/b/d"

## Constants

### `SEP`

```gamma
SEP :: '/'
```

The path separator.

## Functions

### `is_absolute`

```gamma
is_absolute :: fn(p: string) -> bool
```

Whether `p` starts at the root (begins with `/`).

### `base_name`

```gamma
base_name :: fn(p: string) -> string
```

The last component of `p`; trailing slashes are ignored (`a/b/` gives `b`). The base
name of `/` (or of any path of slashes only) is `/`, and of the empty path it is
the empty path.

### `dir_name`

```gamma
dir_name :: fn(p: string) -> string
```

All but the last component of `p`, as POSIX `dirname` has it: `a/b` gives `a`, `a`
gives `.`, `/a` gives `/`, `/` gives `/`, and `a/b/` gives `a`. The empty path gives
`.`.

### `extension`

```gamma
extension :: fn(p: string) -> string
```

The extension of the base name of `p`, without the dot: `a.tar.gz` gives `gz`. It is
empty when there is none; a leading dot does not start one (`.bashrc`) and neither
does a trailing dot (`a.`).

### `stem`

```gamma
stem :: fn(p: string) -> string
```

The base name of `p` without its extension and the dot before it: `a.tar.gz` gives
`a.tar`.

### `components`

```gamma
components :: fn(p: string) needs Yield(string)
```

The non-empty components of `p`, in order. An absolute path yields `/` first. A `.`
component is skipped, except that a relative path made of nothing else yields a
single `.`. `..` is yielded like any other component.

### `clean`

```gamma
clean :: fn(p: string, buf: []u8) -> string
```

`p` with repeated slashes collapsed, `.` components removed, and `x/..` resolved
lexically; `..` that would rise above the root of an absolute path is dropped, and
leading `..` of a relative path stay. The result is written into `buf` and is the
part of it that was used; an empty result is `.`. Traps unless `buf` holds at least
`p.len` bytes (and one at least). `buf` must not overlap `p`. Trailing slashes are
removed. The result is never longer than `p`, except for the `.` that replaces an
empty path.

### `join`

```gamma
join :: fn(out: ^List(u8), parts: []const string) -> AllocError!()
```

Appends `parts` to `out`, separated by single slashes. Empty parts are skipped; a
slash is inserted between two parts unless the text before ends with one already.
A later part that starts with `/` does not restart the path: its leading slashes are
dropped (a part of slashes only is skipped). Nothing else is cleaned (use `clean`).

### `join_alloc`

```gamma
join_alloc :: fn(parts: []const string) -> AllocError![]u8 needs Alloc
```

`join` into new memory from the current allocator; release it with `free`.

### `relative_to`

```gamma
relative_to :: fn(p, base: string) -> ?string
```

`p` relative to `base`: what remains of `p` after the prefix `base` and a slash, when
`p` lies inside `base` component by component (`a/bc` is not inside `a/b`); `.` when
the two are equal; `null` otherwise. The comparison is on the text as written, so
neither path is cleaned, and the remainder is a slice of `p` (with its own trailing
slashes). Trailing slashes of `base` are ignored. An empty `base` contains only the
empty path.
