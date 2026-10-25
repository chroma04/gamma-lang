# Idiomatic Gamma

[Contents](README.md)

The tutorial chapters say what the language *has*. This page says how experienced Gamma code *uses* it: the patterns that keep programs short and clear, and the habits from other languages that do not fit. It grows as the code base does. Most examples come from, or are modeled on, `gammac`, the compiler written in Gamma in [`compiler/`](../../compiler) (see [the self-hosting notes](../self-hosting.md)), which is meant to be read as an example of the style described here.

Every example on this page is a complete program checked by the test suite.

## Layout and naming

- **Format with `gamma fmt`.** Do not hand-align; let the formatter decide.
- **Names.** Functions, variables and modules are `snake_case`, types and variants are `PascalCase`, constants are `SCREAMING_CASE` (`MAX_EXPONENT: i64 : 100_000`).
- **A module is a directory.** Put one concern in each, and split a large module into several files in the same directory: they share one namespace, so no file needs to import another. `gammac`'s `syntax/` module holds `token.gm`, `lexer.gm`, `literal.gm` and `dump.gm`.
- **Imports go at the top of the file that uses them.** Each file of a module may repeat an import with the same name and path.
- **Document what is `pub`** with `///` comments, and the module with `//!` lines at the top of one of its files. Say what a function *gives* and what it *expects*, not how it works.

## Methods are functions in the type's module

There are no `impl` blocks. A function whose first parameter is the type, or a pointer to it, is called with method syntax. Take `^T` to change the value and `^const T` to read it:

```gamma
Counter :: struct {
    count: u32 = 0
    step: u32 = 1
}

bump :: fn(c: ^Counter) { c.count += c.step }

total :: fn(c: ^const Counter) -> u32 => c.count

main :: fn() needs Out {
    c := Counter(step: 5)
    c.bump()
    c.bump()
    print("{}\n", c.total())
}
```

```output
10
```

A short function is written with `=>`. A state machine such as a lexer is a struct plus functions on `^Lexer`: `lx.peek()`, `lx.emit(kind, lo, hi)`.

## Absence: `?T`, `else` and binding conditions

Reach for an optional before a sentinel value. The forms that use one read as English:

| Want | Write |
| --- | --- |
| the value or a default | `x := s.get(i) else 0` |
| the value, or leave | `b := lx.peek() else return` (or `else break`, `else continue`) |
| do something if present | `if c := s.get(i) { … }` |
| loop while present | `while c := lx.peek() { … }` |
| test without unwrapping | `if s.get(i) is 'x' { … }` |
| test for one of several | `if s.get(i) in (' ', '\t') { … }` |

`is` and inline-tuple `in` compare an optional with plain values directly: a missing value is simply not equal.

```gamma
is_space_at :: fn(s: string, i: usize) -> bool => s.get(i) in (' ', '\t')

first_word :: fn(s: string) -> string {
    i: usize = 0
    while c := s.get(i) {
        if c is ' ' { break }
        i += 1
    }
    return s[0..i]
}

main :: fn() needs Out {
    s :: "let x"
    print("{} {} {}\n", is_space_at(s, 3), is_space_at(s, 99), first_word(s))
}
```

```output
true false let
```

Slices have `get(i)`, `first()` and `last()`, which return optionals, so `items.last()` replaces `items[items.len - 1]` with its length check.

## Failure: results, `try` and when to trap

A function that can fail for reasons outside the program's control returns `E!T`, where `E` is a union with one variant per cause. Callers propagate with `try` or handle with `else e => …` or `match`. Errors carried by `try` lift into a caller's error union automatically when it has exactly one variant of that type.

When failure is a bug or cannot be handled sensibly, unwrap with `x!` (a result) or `x?` (an optional): it traps with the error in the message. A compiler, for example, treats running out of memory as fatal, so `gammac` writes `l.push(x)!` instead of threading `AllocError` through every function. Decide this per program and say so in its documentation; a library should usually return the error instead.

```gamma
ParseError :: union { Empty, BadDigit: usize }

parse_digits :: fn(s: string) -> ParseError!u32 {
    if s.len is 0 { return !.Empty }
    n: u32 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
        n = n * 10 + u32(c - '0')
    }
    return n
}

main :: fn() needs Out {
    for text in ("42", "", "4x") {
        match parse_digits(text) {
            !.Empty => print("empty\n")
            !.BadDigit(at) => print("bad digit at {}\n", at)
            n => print("{}\n", n)
        }
    }
}
```

```output
42
empty
bad digit at 1
```

A problem in the *input* of a tool (a syntax error, a bad option) is usually neither: report it and carry on, as `gammac` does with its `diag.Diagnostics`, so the user sees every problem at once.

## Matching

`match` on bytes takes character literals, ranges and alternatives, which makes scanners read like their grammar:

```gamma
Class :: union { Letter, Digit, Space, Other }

classify :: fn(c: u8) -> Class => match c {
    'a'..='z', 'A'..='Z', '_' => .Letter
    '0'..='9' => .Digit
    ' ', '\t', '\n' => .Space
    _ => .Other
}

main :: fn() needs Out {
    for c in "a1 ;" { print("{}\n", classify(c)) }
}
```

```output
.Letter
.Digit
.Space
.Other
```

- **Tables over strings.** To map an enum to text and back, write two `match` functions (`keyword_text(k)` and `keyword_from(s) -> ?Keyword`, which matches string literals). They are exhaustive, so a new variant cannot be forgotten.
- **Test a variant with `is`**, including nested payloads: `kind is .Punct(.Dot)`. Bind with `if .Punct(p) := kind { … }` or a `match` arm.
- **Guards** go after the pattern: `.Punct(p) if p in (Punct.RParen, Punct.RBracket) => …`. A guarded arm does not count toward exhaustiveness.
- **Several variants at once**: `k in (.Fn(_), .Handler(_), .Type)` tests whether `k` matches any of the patterns, as a chain of `is` tests would, and evaluates `k` once.

## Optional parts of variants

In a pattern, a name in the position of an optional matches only a *present* value (§9.3). That is what makes `if x := opt` work, and it makes `.Return(null)` and `.Return(x)` a natural pair of arms. But it also means that `.Break(label, value)`, for a payload `(label: ?string, value: ?i32)`, does not match a `break` without a label (the compiler points at `label` when the `match` is left incomplete). To bind an optional part as it is, absent value included, write `?` before its name:

```gamma
Expr :: union {
    Break: (label: ?string, value: ?i32)
    Return: ?i32
    Int: i32
}

describe :: fn(e: Expr) -> string => match e {
    .Break(?label, _) => if label is null { "break" } else { "labeled break" }
    .Return(null) => "return"
    .Return(_) => "return a value"
    .Int(_) => "integer"
}

main :: fn() needs Out {
    print("{}\n", describe(.Break(label: null, value: null)))
    print("{}\n", describe(.Break(label: "outer", value: 1)))
    print("{}\n", describe(.Return(null)))
}
```

```output
break
labeled break
return
```

When a `match` has a catch-all arm, it is complete, so the compiler has nothing to point at: `.Return(v) => …` followed by `_ => …` sends `.Return(null)` to the catch-all. `gamma lint` reports that (rule `present-only-binding`) unless an arm names the absent case or the catch-all is an empty `_ => {}`.

A payload with several optional parts that many functions take apart is often clearer as a struct of its own, which can also carry documentation per field.

A line that *starts* with `.Name` continues the previous line (§2.8), so a variant that begins a line, such as the value at the end of a block arm, is written with its type: `ast.ItemKind.When(…)`.

## Errors that unwind a recursive descent

A parser reports a syntax error once and then unwinds to a point where it can recover. Give the error a type of its own (an empty struct is enough), return `Abort!T` from every parsing function, propagate with `try`, and write the report as a function that *returns* the error value, so that failing reads `return !p.fail(…)`:

```gamma
Abort :: struct {}

Parser :: struct {
    text: string
    pos: usize = 0
    errors: u32 = 0
}

fail :: fn(p: ^Parser, message: string) -> Abort needs Out {
    print("error at {}: {}\n", p.pos, message)
    p.errors += 1
    return Abort()
}

digit :: fn(p: ^Parser) -> Abort!u32 needs Out {
    c := p.text.get(p.pos) else return !p.fail("expected a digit, found the end")
    if c not in '0'..='9' { return !p.fail("expected a digit") }
    p.pos += 1
    return u32(c - '0')
}

sum :: fn(p: ^Parser) -> Abort!u32 needs Out {
    total := try p.digit()
    while p.text.get(p.pos) is '+' {
        p.pos += 1
        total += try p.digit()
    }
    return total
}

main :: fn() needs Out {
    for text in ("1+2+3", "1+x", "4+") {
        parser := Parser(text: text)
        if n := parser.sum() { print("{}\n", n) }
    }
}
```

```output
6
error at 2: expected a digit
error at 2: expected a digit, found the end
```

State that must be restored on every way out, such as a nesting depth or a mode flag, is saved and restored with `defer`, which runs on a `try` that propagates as well:

```gamma fragment
parse_head_expr :: fn(p: ^Parser) -> Abort!ast.Expr needs Alloc {
    old := p.restr
    p.restr = Restr(no_brace: true)
    defer p.restr = old
    return p.parse_expr()
}
```

Behavior that varies inside a fixed frame, such as "parse one member" inside "loop to the closing brace and recover from errors", is a second-class function parameter: `p.members("union body", fn() -> Abort!() needs Alloc { … })`. The literal can use and change the caller's locals, and it costs no allocation.

## Memory: one arena per phase

Most programs allocate in phases: read a file, build a structure, print a result, forget all of it. Give each phase an arena and let the arena free everything at once. Code inside the phase allocates through the `Alloc` effect without caring where the memory comes from, and nothing needs its own `deinit`:

```gamma
arena :: import "std/arena"

words :: fn(text: string) -> List(string) needs Alloc {
    out := list(string)
    start: usize = 0
    for i, c in enumerate(text) {
        if c is ' ' {
            out.push(text[start..i])!
            start = i + 1
        }
    }
    out.push(text[start..])!
    return out
}

main :: fn() needs Alloc + Out {
    a := arena.new()
    defer a.deinit()
    with a.allocator() {
        ws := words("one two three")
        print("{} {}\n", ws.items.len, ws.items.last() else "")
    }
}
```

```output
3 three
```

Outside an arena, pair each acquisition with its release on the next line: `l := list(u8)` then `defer l.deinit()`. Tests run on a heap that reports leaks, so a missing `defer` is found by `gamma test`.

## Lists that grow under you

A `List` keeps its elements in one block and moves them to a larger block when it grows, so a slice or a pointer into `l.items` is valid only until the next `push`. That includes the place of an assignment: the place is evaluated *before* the right side (§4.7), so in

```gamma fragment
l.decls.items[id].state = l.import_decl(…)    // wrong if import_decl pushes to l.decls
```

the store goes to the block that `items` named before the call, which the push may have left. Compute the value first, then store it:

```gamma
Decl :: struct {
    name: string
    done: bool = false
}

// Adds a declaration, which may move the list's elements.
declare :: fn(decls: ^List(Decl), name: string) -> bool needs Alloc {
    decls.push(Decl(name: name))!
    return true
}

main :: fn() needs Alloc + Out {
    decls := list(Decl)
    defer decls.deinit()
    decls.push(Decl(name: "a"))!
    done := declare(&decls, "b")
    decls.items[0].done = done
    print("{} {}\n", decls.items[0].done, decls.items.len)
}
```

```output
true 2
```

The same goes for keeping `p := &l.items[i]` across a call that may add to `l`: keep the index instead.

## Pointers to copies

A loop variable and a name a pattern binds hold a *copy* of the element or the payload, in a slot that is reused. A pointer to one is fine for a call, but storing it keeps a pointer that the next iteration overwrites:

```gamma fragment
for e in exprs { terms.push(&e)! }          // wrong: every pointer is to `e`
```

Storing such an address where it certainly outlives the binding is a compile error (§15.1), and `gamma lint` reports a call like `push` that may keep it:

```gamma error
main :: fn() needs Out {
    words := [3]string("one", "two", "three")
    last: ?^const string = null
    for w in words { last = &w }            // error: `w` ends with the iteration
    print("{}\n", last is null)
}
```

Point into the data itself, by index:

```gamma
main :: fn() needs Alloc + Out {
    words := [3]string("one", "two", "three")
    longest := list(^const string)
    for i in 0..words.len {
        if words[i].len > 3 { longest.push(&words[i])! }
    }
    print("{} {}\n", longest.items.len, longest.items[0]^)
}
```

```output
1 three
```

When the data has no stable place (a payload bound by a pattern), keep a copy of the value rather than a pointer to the binding.

The same goes for a slot that is about to be overwritten. A function that keeps the pointer it is given, such as one that wraps an expression node in a new node, must not be handed the address of the slot that receives its result:

```gamma fragment
args[0] = cx.bind_temp(&args[0], &stmts)^           // wrong: the kept pointer now sees the new value
args[0] = cx.bind_temp(new(args[0])!, &stmts)^      // the old value gets a place of its own
```

## Building text

Append to a `List(u8)` with `write`, which takes a `print` template, and write the result out once. Many small `print` calls are much slower than one large write:

```gamma
main :: fn() needs Alloc + Out {
    out := list(u8)
    defer out.deinit()
    for i in 1..4 {
        if i > 1 { out.push(' ')! }
        out.write("{}:{}", i, i * i)!
    }
    out.push('\n')!
    Out.write(out.items)
}
```

```output
1:1 2:4 3:9
```

`str.format(template, args…)` returns a fresh `[]u8` (from `std/str`) for a message that is kept, such as a diagnostic. `{:04X}`, `{:02x}` and the other format specs (§21.6.1) cover padding and bases.

## Small things

- **Multiple assignment** sets several variables at once, from a tuple or a function returning one: `p, len = .Arrow, 2`.
- **`x in s`** asks whether a slice contains `x`; there is no `.contains` method for slices.
- **`for i, c in enumerate(s)`** gives indexes and elements; `for c in s` iterates the bytes of a string.
- **`while true` with an exit inside** (`b := next() else break`) needs no `return` after the loop: the compiler knows the loop only ends where it says.
- **A keyword cannot be a name**, not even a field name: write `effect_` or `is_foreign`.
- **Do not reuse the names of predeclared types** (`string`, `rune`, `cstring`) for your own declarations: a function called `string` hides the type in its whole module.
- **Conversions say what they do**: `u64(x)` only when lossless, `x.trunc(u32)` to keep the low bits, `x.checked(u8)` for an optional.
- **Name the effects a module keeps needing**: `Load :: Alloc + Fs` is a named effect set (§12.3), and `needs Load` reads better than the sum repeated on every function.
- **A temporary array can be sliced only as an argument**: `f([2]string("a", "b")[..])` passes it for the call, but a struct field or a variable needs the array in a variable first (`names := [2]string(…)`, then `names[..]`).
- **Choose a value with an expression, not a later assignment.** `---` leaves only plain data uninitialized (§4.4), so a pointer or a slice that depends on a condition is bound once: `msg := if args.len is 0 { default_msg() } else { check(args[0]) }`, with a branch that cannot produce one leaving through `return`.
- **Name the type of a `match` whose arms differ in mutability.** The arms take the type of the first one (§7.12), so when it builds a `[]u8` (`str.format(…)!`) and a later one is a literal `string`, write `head: string = match …`.
- **A helper that uses the surrounding locals is a `::` binding**: `deref :: fn() -> ^Expr => …` captures them; function literals are second-class (§11.6) and cannot be stored with `:=`.
- **Tests live next to the code** in `test "…" { }` blocks; a test that allocates runs its body in an arena, or releases what it allocates.
