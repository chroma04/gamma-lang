# Module `std/cli`

[All modules](index.md)

Command-line parsing as an iterator, getopt style.

The program describes its options in a table of `Opt` and matches over what `parse`
yields. Nothing allocates and nothing is reflected: the yielded strings are slices of
the arguments, and `help` appends the usage text to a list you own.

    cli :: import "std/cli"
    opts := [_]cli.Opt(
        cli.Opt(short: 'v', long: "verbose", help: "print more"),
        cli.Opt(short: 'o', long: "output", value: "file", help: "where to write"),
    )
    for a in cli.parse(args()[1..], opts[..]) {
        match a {
            .Opt(o) => …                      // o.index into opts, o.value ("" for flags)
            .Pos(p) => …
            .Unknown(u) => …
            .MissingValue(i) => …
            .UnexpectedValue(i) => …
        }
    }

The rules are those of GNU getopt without abbreviations or permutation: options and
positional arguments may be mixed, `--` ends the options, and a lone `-` is a
positional argument. A long option is `--name`, `--name=value` or `--name value`; short
options may be bundled (`-abc`), and one that takes a value takes the rest of its
bundle (`-ofile`) or else the next argument (`-o file`).

## Structs

### `Opt`

```gamma
Opt :: struct {
    pub short: u8 = 0
    pub long: string = ""
    pub value: string = ""
    pub help: string = ""
}
```

One option of the program.

Fields:

- `short`: The letter of the short form (`-v`), or 0 if there is none.
- `long`: The name of the long form (`--verbose`) without dashes, or "" if there is none.
- `value`: The name of the option's value in help text. An empty `value` makes the option a
  flag that takes no value.
- `help`: A description for `help`.

## Unions

### `Arg`

```gamma
Arg :: union {
    Opt: (index: usize, value: string)
    Pos: string
    Unknown: string
    MissingValue: usize
    UnexpectedValue: usize
}
```

What `parse` yields for one command-line item.

Variants:

- `Opt`: `opts[index]` was given, with its `value` ("" for a flag).
- `Pos`: A positional argument.
- `Unknown`: An option that is not in the table, as written: `--colour`, or `-q` for a letter
  in a bundle. A long option written `--name=value` is reported whole.
- `MissingValue`: `opts[index]` takes a value and the arguments ended.
- `UnexpectedValue`: The flag `opts[index]` was written `--flag=value`.

## Functions

### `parse`

```gamma
parse :: fn(args: []const string, opts: []const Opt) \ {Yield(Arg)}
```

Parses `args`, which must not include the program name (pass `args()[1..]`), and
yields one `Arg` for each option or positional argument, in order.

- `--` ends the options: everything after it is positional. A lone `-` is positional.
- `--name=value` and `--name value` give a long option its value; long names must
  match exactly. Giving a flag a value (`--flag=x`) is `UnexpectedValue`.
- `-abc` is the flags `a`, `b` and `c`; `-ofile` and `-o file` give a short option its
  value, also at the end of a bundle (`-vofile`). Anything after a short option that
  takes a value belongs to it.
- A value is taken from the next argument whatever it looks like, even `-x` or `--`.
- An option that needs a value at the end of the arguments is `MissingValue`.

### `help`

```gamma
help :: fn(out: ^List(u8), usage: string, opts: []const Opt) -> AllocError!()
```

Appends help text for `opts` to `out`: `usage: ` and `usage`, a blank line, `options:`
and one line per option, with the descriptions aligned in a column two spaces after
the longest option:

    usage: tool [options] <file>

    options:
      -v, --verbose        print more
      -o, --output <file>  where to write
          --color          use colors
      -h                   show this help

An option without a short form is indented to line up with the long forms; one
without a help text ends its line after the option; one with neither a short nor a
long form is left out.
