# 4. Types

[Contents](README.md) · [Previous: Control flow](03-control-flow.md) · [Next: Optionals and errors](05-optionals-and-errors.md)

Gamma's data types will be familiar to a C programmer, with a few deliberate differences: slices carry their length, unions carry their tag, no pointer can be null unless its type says so, and conversions between numeric types are explicit. This chapter tours arrays, slices, strings, tuples, structs, unions, pointers and conversions. The numeric types and literals are in [chapter 2](02-basics.md); optionals and results are in [chapter 5](05-optionals-and-errors.md).

## Arrays

`[N]T` is exactly `N` elements of type `T`, where `N` is a compile-time `usize` constant. Arrays are **values**: assignment, parameter passing and return copy all the elements.

```gamma
arrays_demo :: fn() \ {Out} {
    v := [_]i32(1, 2, 3)               // [_] lets the compiler count: [3]i32
    w := [_]i32(10, 20, 30)
    sum := v + w                       // elementwise arithmetic: [11, 22, 33]
    twice := v * [3]i32(fill: 2)       // no implicit broadcast; spell the fill out
    zeros := [8]u8(fill: 0)
    copy := v                          // a copy, not an alias
    copy[0] = 99                       // v[0] is still 1
    print("{} {} {}\n", (sum, twice, zeros.len))
    print("{}\n", copy is v)                // false: `is` compares arrays element by element
}
```

Array constructors are calls on the array type: `[N]T(e1, ..., eN)` with exactly `N` elements, `[_]T(...)` to infer `N`, `[N]T(fill: x)` to repeat one value, and `[N]T(..t)` to spread a tuple. The length is `a.len`, a constant. `a[i]` is bounds-checked (`i` must be an unsigned integer), and `a.get(i)` gives an optional instead of trapping:

```gamma
third :: fn(a: [4]i32) -> i32 => a.get(2) else 0
```

Unlike C, an array never decays to a pointer. Passing an array to a function that wants a slice is explicit: take a slice of it (`a[..]`) or a pointer to it (`&a`).

## Slices

`[]T` is a pointer and a length. It is what you use in C when you pass `(T *p, size_t n)`, packaged so the two cannot get separated. The fields `.ptr` and `.len` can be read but not assigned. `[]const T` is a read-only view.

```gamma
slices_demo :: fn() \ {Out} {
    data := [_]i32(1, 2, 3, 4, 5, 6)
    mid := data[1..4]                  // []i32 viewing 2, 3, 4
    mid[0] = 20                        // writes through: data[1] is now 20
    head := data[..2]                  // open-ended bounds only appear in brackets
    tail := data[4..]
    all := data[..]
    print("{} {} {} {}\n", (mid, head, tail, all.len))
}
```

Slicing checks `lo <= hi <= len`. Slicing a mutable place gives `[]T`; slicing an immutable binding or a `const` view gives `[]const T`, and `[]T` converts to `[]const T` implicitly. Slicing an array requires a *place* (a named variable, a field, `p^`, and so on); there are no hidden temporaries.

Useful methods on arrays and slices (full list in §21.3 of the [specification][s21]): `get(i)`, `first()`, `last()` (all give optionals), `fill(x)`, `copy_from(src)`, `swap(i, j)`, `index_of(x)`, `sort()`, `sort_by(less)`, `sort_by_key(key)`, `min()`, `max()`, `binary_search(x)`, and `cursor()` ([chapter 8](08-iteration.md)). Sorting is in place and allocates nothing.

```gamma
largest_two :: fn(scores: []i32) -> (i32, i32) {
    scores.sort()
    n := scores.len                    // traps below if there are fewer than two scores
    return (scores[n - 1], scores[n - 2])
}
```

## Strings

A `string` is exactly a `[]const u8`: a length and a pointer to bytes, usually UTF-8. String literals live in read-only memory for the life of the program and are not NUL-terminated. There is no character type; a "character" is a number, and you choose how to iterate.

```gamma
count_char :: fn(s: string, c: u8) -> usize {
    n := 0
    for b in bytes(s) {              // bytes(s) yields u8
        if b is c { n += 1 }
    }
    return n
}

count_runes :: fn(s: string) -> usize {
    n := 0
    for r in runes(s) {              // runes(s) yields rune (u32), decoding UTF-8
        if r is not ' ' { n += 1 }
    }
    return n
}
```

- `s.len` is the byte length, and `s[i]` is a `u8` (bounds-checked).
- `s[a..b]` is a substring (it shares memory with `s`; nothing is copied).
- `a is b` compares **contents**, so `name is "admin"` does what you meant. `<` and friends compare bytewise.
- `s.utf8_valid()` checks encoding. A `\xHH` escape can put invalid UTF-8 in a literal on purpose.
- A `cstring` is a `[^]const u8` meant to be NUL-terminated; `c"hello"` makes one. To use it as a Gamma string, slice it: `s[0..s.cstr_len()]`.
- `print` writes a `string` argument as its bytes, but writes it quoted and escaped when it appears inside a tuple, array or optional.

Since a string is just a slice, assembling text means writing into a buffer you own (a `[N]u8` array, or memory from an allocator, [chapter 9](09-memory.md)). The core library has no string-builder type.

## Tuples

Tuples are anonymous, structural product types. Elements are accessed by position or, if the tuple is named, by name:

```gamma
tuples_demo :: fn() \ {Out} {
    pair := (3, "three")                    // (i32, string)
    point := (x: 1.5, y: 2.5)               // named; both elements default to f64
    single := (7,)                          // a one-element tuple needs the comma
    unit := ()                              // the empty tuple, also the type `()`
    print("{} {} {} {}\n", (pair.0, pair.1, point.x, point.y))
    n, label := pair                        // destructure
    print("{} {}\n", (n, label))
}
```

Two tuple types are the same if they have the same element types in the same order and the same names. A tuple is either fully named or fully unnamed. A tuple value converts to a tuple type with the same element types if *either* side is unnamed, which is why `return (n / d, n % d)` works for a function declared `-> (q: u32, r: u32)`. Two differently named tuples never convert to each other. Tuples are how functions return several values. They compare with `is` element by element, and with `<` lexicographically when their elements are orderable.

## Structs

A `struct` literal creates a new, nominal type. Fields are declared `name: Type`, optionally with a compile-time constant default, and `a, b: T` declares several at once. Fields are separated by newlines (or `;`).

```gamma
Vec2 :: struct { x, y: f32 }

Window :: struct {
    pub title: string
    size: Vec2 = Vec2(x: 800, y: 600)
    open: bool = true
}
```

### Construction is by name

`S(...)` constructs a struct, and the arguments are **named**. Fields you leave out take their defaults; a field with no default must be supplied. `..e` spreads all the fields of another struct or a named tuple, and an explicit field after it overrides:

```gamma
structs_demo :: fn() \ {Out} {
    v := Vec2(x: 1.5, y: -2)
    w := Window(title: "main")                    // size and open use defaults
    w2 := Window(..w, title: "other")             // w with a different title
    w2.open = false                               // fields are assignable on a mutable variable
    print("{}\n", v)                              // Vec2(x: 1.5, y: -2)
    print("{} {}\n", (w2.title, w2.open))
}
```

> **Common mistake.** Struct construction takes no positional arguments, so reordering the fields of a struct can never silently change what a constructor means:
>
> ```gamma
> p := Vec2(1.5, -2)       // error: no positional arguments for a struct
> q := Vec2(x: 1.5)        // error: `y` has no default and was not supplied
> ```
>
> A call with exactly one positional argument, `Vec2(v)`, is always read as a *conversion*, and there is no conversion between distinct struct types.

Fields can be left uninitialized with `---` if their type is plain data: `Buf(len: 0, data: ---)`.

### Visibility and layout

Fields are private to their module unless marked `pub`. A struct with a private field cannot be constructed, spread or matched with a struct pattern outside its module, but it can still be copied, passed around and compared with `is`.

Layout is the C layout: fields in declaration order, each aligned to its own alignment, the whole struct rounded up to its alignment. A struct whose fields are all C-compatible types is layout-compatible with the corresponding C struct. `#packed` and `#align(n)` adjust it ([chapter 14](14-interop.md)). Structs are compared with `is` (field by field) but have no ordering; to order them, sort by a tuple of keys.

Matching a struct in a pattern lists the fields you care about:

```gamma
Point :: struct { x, y: i32 }

is_axis :: fn(p: Point) -> bool => match p {
    Point(x: 0) => true
    Point(y: 0) => true
    _           => false
}
```

### Embedding with `using`

Gamma has no inheritance, but the *first* field of a struct can be declared `using`, which promotes the embedded struct's fields and lets a pointer to the outer type stand in for a pointer to the embedded one:

```gamma
Entity :: struct {
    id: u32
    x, y: f32
}

Player :: struct {
    using base: Entity
    name: string
    health: i32 = 100
}

move_by :: fn(e: ^Entity, dx, dy: f32) {
    e.x += dx
    e.y += dy
}

player_demo :: fn() {
    p := Player(base: Entity(id: 1, x: 0, y: 0), name: "ada")
    move_by(&p, 1.5, 0)             // ^Player coerces to ^Entity for free
    p.x += 1                        // `p.x` means `p.base.x`
}
```

The embedded field is at offset 0, so the coercion costs nothing. Going the other way, from `^Entity` back to `^Player`, uses `e.downcast_unchecked(Player)`, which is exactly as unchecked as its name says.

## Unions and enums

A `union` is a tagged union: each variant has a name and either a payload type or none. A union with no payloads is an enum.

```gamma
Shape :: union {
    Circle: f32
    Rect: (w: f32, h: f32)
    Empty
}

Dir :: union { N, E, S, W }                       // an enum
Errno :: union(i32) { Ok = 0, Perm = 1, NoEnt = 2 }   // explicit tag type and values
```

Construct a variant with `.Name(payload)` when the union type is known from context, or `Shape.Circle(1.0)` otherwise. A tuple payload's elements are written directly. Take values apart with `match`, `if pattern := e` or test them with `is`:

```gamma
unions_demo :: fn(s: Shape) \ {Out} {
    unit_circle := Shape.Circle(1.0)
    box: Shape = .Rect(w: 2, h: 3)
    if s is .Circle(_) { print("a circle\n") }
    if s is .Empty { print("nothing\n") }
    if .Rect(w, h) := box { print("{} by {}\n", (w, h)) }
    print("{}\n", unit_circle)                     // .Circle(1)
}

turn_right :: fn(d: Dir) -> Dir => match d {
    .N => .E
    .E => .S
    .S => .W
    .W => .N
}
```

Rules that differ from C:

- A pattern on the right of `is` may not bind names (`s is .Circle(r)` is an error); use `if .Circle(r) := s`.
- `d in (.N, .S)` tests an enum against several values; enums also compare with `<` by tag value.
- Integers become enums only through `Dir.from_tag(x)`, which returns `?Dir` and is `null` for a value that is not a tag. There is no unchecked cast, because values from C or the network may be out of range. Going the other way is `u8(d)` (an enum converts losslessly to its tag type) and `d.tag()`.
- A tagged union is laid out as `{ tag, payload }`; an enum is just its tag.

```gamma
errno_name :: fn(code: i32) -> string {
    e := Errno.from_tag(code) else return "unknown"
    return match e {
        .Ok    => "ok"
        .Perm  => "permission denied"
        .NoEnt => "no such file"
    }
}
```

(Untagged C-style unions exist for interop and are described in [chapter 14](14-interop.md).)

## Pointers

Gamma has several pointer types, each saying exactly what it allows.

| Type | Points to | Arithmetic | Indexing |
| --- | --- | --- | --- |
| `^T` | one `T` | no | no |
| `^const T` | one `T`, read-only through this pointer | no | no |
| `[^]T` | the first of an unknown number of `T` | yes | `p[i]`, unchecked |
| `[^]const T` | the same, read-only | yes | unchecked |
| `rawptr` | unknown | no | no |

**No pointer type admits null.** A nullable pointer is spelled `?^T`, `?[^]T`, and so on; it has the same size as the pointer, with null represented as address 0, so it is a drop-in for a C pointer that may be `NULL`.

`&x` takes an address, and `p^` dereferences. Field access auto-dereferences exactly one level, so `p.f` means `p^.f`.

```gamma
swap_ints :: fn(a, b: ^i32) {
    t := a^
    a^ = b^
    b^ = t
}

pointers_demo :: fn() {
    x: i32 = 1
    y: i32 = 2
    swap_ints(&x, &y)                 // x is 2, y is 1
}
```

A linked list shows `?^T` and the binding condition from [chapter 3](03-control-flow.md) working together to replace the NULL check:

```gamma
Node :: struct {
    value: i32
    next: ?^Node
}

sum_list :: fn(head: ?^const Node) -> i32 {
    total := 0
    cur := head
    while n := cur {                  // n: ^const Node, definitely not null
        total += n.value
        cur = n.next
    }
    return total
}

list_demo :: fn() -> i32 {
    c := Node(value: 3, next: null)
    b := Node(value: 2, next: &c)
    a := Node(value: 1, next: &b)
    return sum_list(&a)               // 6
}
```

A **many-pointer** `[^]T` is C's `T *` used as an array: it supports `p + n`, `p - n`, `p - q` and unchecked indexing `p[i]`, and slicing it with both bounds, `p[a..b]`, produces a `[]T`. Use it at C boundaries; prefer slices in Gamma code.

```gamma
sum_raw :: fn(p: [^]const i32, n: usize) -> i32 {
    total := 0
    for i in 0..n { total += p[i] }   // unchecked: you vouch for n
    return total
}
```

### Pointer coercions

These conversions happen implicitly: `^T` to `^const T` (and likewise for `[^]T` and `[]T`); a pointer to an array `^[N]T` to `[]T`, `[]const T` or `[^]T`; `^T` to `[^]T`; any pointer to `rawptr`; and each of these inside `?`. A chain of these counts as one coercion, so `^u8` goes straight to `[^]const u8`.

Everything else is explicit and spelled as a method: `p.cast(P)` changes the pointee type, constness or kind (the address must be suitably aligned, which is checked), `p.addr()` gives the `usize` address, and `n.to_ptr(P)` turns a `usize` into a `?P` (null for 0). Casting away `const` is possible with `cast`; writing through a pointer derived that way to read-only memory is undefined behavior.

Gamma does not track pointer lifetimes in general, but it rejects the simplest dangling-pointer bugs: returning `&x`, `x[..]`, or a slice of a local or parameter is a compile error.

## Conversions

Because there are no implicit numeric conversions in expressions, you convert on purpose, and the spelling tells you whether information can be lost.

**`T(x)` is lossless or it does not compile.** It is permitted only when every value of the source *type* fits in `T`, regardless of what the compiler can prove about the actual value:

```gamma
lossless_demo :: fn(a: u8, b: i16, flag: bool, d: Dir) {
    w := u32(a)             // u8 -> u32
    s := i32(b)             // i16 -> i32
    f := f64(b)             // i16 fits in a 53-bit significand
    n := u8(flag)           // true -> 1, false -> 0
    t := u8(d)              // an enum to its tag type
    // i8(a)                // error: not every u8 fits an i8
    // u32(b)               // error: a negative i16 does not fit
}
```

`usize` and `isize` are checked against *every* supported target (pointer width of at least 32 bits), so `usize(x)` is legal for `x` up to `u32`, and wider values use the methods below.

**Lossy conversions are named methods**, so each one says what happens to the lost information:

| Method | Meaning |
| --- | --- |
| `x.trunc(T)` | integers: keep the low bits. Floats: round toward zero, checked to fit |
| `x.round(T)`, `x.floor(T)`, `x.ceil(T)` | float to integer (checked); `round` also does integer to float and float to narrower float |
| `x.checked(T)` | `?T`: `null` if the value does not fit |
| `x.sat(T)` | clamp to `T`'s range (NaN becomes 0) |
| `x.bits(T)` | reinterpret the bytes of one plain-data type as another of the same size |

```gamma
lossy_demo :: fn(big: u32, f: f64, g: f32) \ {Out} {
    a := big.trunc(u8)                    // low 8 bits, always defined
    b := big.checked(u8) else 255         // 255 if it does not fit
    c := big.sat(u8)                      // clamps to 255
    d := f.trunc(i32)                     // 3.7 -> 3 (traps if out of range or NaN)
    e := f.round(i32)                     // 3.7 -> 4
    raw := g.bits(u32)                    // the IEEE bit pattern of an f32
    print("{} {} {} {} {} {}\n", (a, b, c, d, e, raw))
}
```

There is no conversion from an integer to `bool` (write `x is not 0`), none from integers to unions (use `from_tag`), none between distinct struct types, and none from a slice to a pointer (use `.ptr`).

> **Common mistake.** There are no C-style casts. `(u8)x` and `x as u8` do not exist. Write `u8(x)` if it is lossless, or choose `x.trunc(u8)`, `x.checked(u8)` or `x.sat(u8)` to say what should happen when it is not.

## Summary

- Arrays are values, slices are `(ptr, len)`, strings are `[]const u8`, and none of them converts implicitly to a pointer.
- Structs are constructed by name; `using` gives embedding; unions are tagged and matched exhaustively.
- Pointers are never null unless spelled `?`; `[^]T` is for C-style arithmetic.
- `T(x)` is lossless, and everything lossy is a named method.

Next, [chapter 5](05-optionals-and-errors.md) covers optionals and error results.

For the full rules, see [§5 Types][s5] and [§6 Literals, constants and conversions][s6] of the specification.

[s5]: ../../gamma-spec.md#5-types
[s6]: ../../gamma-spec.md#6-literals-constants-and-conversions
[s21]: ../../gamma-spec.md#21-the-core-library
