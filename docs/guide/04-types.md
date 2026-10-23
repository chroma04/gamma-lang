# 4. Types

[Contents](README.md) · [Previous: Control flow](03-control-flow.md) · [Next: Optionals and errors](05-optionals-and-errors.md)

Gamma's data types will be familiar to a C programmer, with a few deliberate differences: slices carry their length, unions carry their tag, no pointer can be null unless its type says so, and conversions between numeric types are explicit. This chapter tours arrays, slices, strings, tuples, structs, unions, pointers and conversions. The numeric types and literals are in [chapter 2](02-basics.md); optionals and results are in [chapter 5](05-optionals-and-errors.md).

## Arrays

`[N]T` is exactly `N` elements of type `T`, where `N` is a compile-time `usize` constant. Arrays are **values**: assignment, parameter passing and return copy all the elements.

```gamma
arrays_demo :: fn() needs Out {
    v := [_]i32(1, 2, 3)               // [_] lets the compiler count: [3]i32
    w := [_]i32(10, 20, 30)
    sum := [3]i32(v[0] + w[0], v[1] + w[1], v[2] + w[2])   // arrays have no arithmetic: [11, 22, 33]
    zeros := [8]u8(fill: 0)
    copy := v                          // a copy, not an alias
    copy[0] = 99                       // v[0] is still 1
    print("{} {}\n", sum, zeros.len)
    print("{}\n", copy is v)                // false: `is` compares arrays element by element
}
```

Array constructors are calls on the array type: `[N]T(e1, ..., eN)` with exactly `N` elements, `[_]T(...)` to infer `N`, `[N]T(fill: x)` to repeat one value, and `[N]T(..t)` to spread a tuple. The length is `a.len`, a constant. `a[i]` is bounds-checked (`i` must be an unsigned integer), and `a.get(i)` gives an optional instead of trapping:

```gamma
third :: fn(a: [4]i32) -> i32 => a.get(2) else 0
```

Unlike C, an array never decays to a pointer. Passing an array to a function that wants a slice is explicit: take a slice of it (`a[..]`) or a pointer to it (`&a`).

### Vectors

`#simd [N]T` is a vector: an array of `N` lanes whose operators work lane by lane and compile to the processor's vector instructions. Lanes are fixed-width integers, `f32`, `f64` or `bool`; `N` is a power of two and a vector holds at most 64 bytes. Give the type a name and construct it like an array:

```gamma
F4 :: #simd [4]f32

vectors_demo :: fn() needs Out {
    a := F4(1, 2, 3, 4)
    b := F4(fill: 2.5)
    print("{}\n", a * b)                          // [2.5, 5, 7.5, 10]
    big := a.lanes_gt(b)                           // a mask: [false, false, true, true]
    print("{} {}\n", big.count(), big.select(a, b))   // 2 [2.5, 2.5, 3, 4]
    print("{}\n", (a * a).reduce_sum())           // 30
}
```

Arrays have no arithmetic (`a + b` on arrays is an error): use a vector, a math vector (below), or a loop. Comparing vectors works as for arrays: `a is b` is one `bool`, true when every pair of lanes is equal (so a `NaN` lane makes it false), and vectors can be hash map keys. Vectors are not ordered, so `<` is an error; the methods `.lanes_eq(w)`, `.lanes_ne(w)`, `.lanes_lt(w)`, `.lanes_le(w)`, `.lanes_gt(w)` and `.lanes_ge(w)` compare lane by lane and give a mask `#simd [N]bool`. A mask is not a condition: write `m.all()` or `m.any()`. A scalar is never spread over the lanes (`a * 2` is an error; write `a * F4(fill: 2)`), and integer lanes have no `/` or `%`, since no processor divides integer lanes in one instruction. `F4.load(xs[i..])` reads four floats from a slice and `v.store(out[i..])` writes them back; both check the slice is long enough. Integer lanes keep the checks of their scalar operators, so `+` traps on overflow in Debug and Checked builds; `+%` wraps and `+|` saturates.

### Math vectors

`#vec [N]T` is a math vector: an array of 2, 3 or 4 integers or floats whose operators work element by element. It is meant for coordinates, colors and the like. Where a `#simd` vector promises instructions and so restricts its shape (power-of-two length, aligned to the whole vector), a math vector promises a layout: it is exactly `[N]T`, so it can be a field of a C struct, a vertex in a buffer or a record in a file. Use `#simd` when you are tuning a loop and `#vec` when you are writing geometry.

```gamma
Vec3 :: #vec [3]f32

reflect :: fn(d, n: Vec3) -> Vec3 => d - 2 * d.dot(n) * n

lerp :: fn(a, b: Vec3, t: f32) -> Vec3 => a + (b - a) * t

math_vec_demo :: fn() needs Out {
    ray := Vec3(1, -1, 0)
    up := Vec3(0, 1, 0)
    print("{}\n", reflect(ray, up))                // [1, 1, 0]
    print("{}\n", lerp(Vec3(fill: 0), ray, 0.5))   // [0.5, -0.5, 0]
    print("{} {}\n", ray.norm(), ray.cross(up))     // 1.4142135 [-0, 0, 1]
}
```

Construct and index it like an array; `is` compares whole vectors, and `Vec3(xs)` and `[3]f32(v)` convert to and from an array. The operators `+ - *` and `/` (and the others the element type has, with the same overflow checks per element) take two vectors of one type; `*` and `/` also take a scalar, so `2 * d` scales every element. There is no `v + 1`: adding a scalar to every element is rarely what a formula means, and writing `v + Vec3(fill: 1)` says so. The methods are `dot`, `cross` (three elements), `norm` (float elements), and `min`, `max` and `abs` element by element. A math vector is not ordered (`<` is an error) and has none of the array methods.

Matrices and quaternions are not built in. They are library types, ordinary structs of math vectors with functions, since the language only defines the operators that cost a handful of scalar operations.

### Complex numbers

`Complex(T)`, for a float type `T`, is a struct `{ re, im }` of the core library on which the language defines `+`, `-`, `*`, `/` and unary `-`. Its layout is that of C's `float _Complex` and `double _Complex`. Like a math vector, `*` takes a scalar on either side and `/` on the right (`2 * z`, `z / 2`); `z + 1` is an error, for the same reason as `v + 1`. Division is Smith's algorithm, which does not overflow for large divisors. The methods are `conj()` and `abs()` (the magnitude, computed like C's `hypot`); `exp`, `log`, `sqrt` and the rest of complex analysis are in `std/complex`.

```gamma
C64 :: Complex(f64)

/// Iterations of z = z*z + c until z leaves the circle of radius 2, at most `limit`.
mandelbrot_steps :: fn(c: C64, limit: u32) -> u32 {
    z := C64(re: 0, im: 0)
    for n in 0..limit {
        if z.abs() > 2 { return n }
        z = z * z + c
    }
    return limit
}

complex_demo :: fn() needs Out {
    a := C64(re: 1, im: 2)
    b := C64(re: 3, im: -1)
    print("{}\n", a * b)                    // Complex(re: 5, im: 5)
    print("{}\n", a / b)                    // Complex(re: 0.1, im: 0.7000000000000001)
    print("{} {}\n", a.conj().im, b.abs())  // -2 3.1622776601683795
    print("{}\n", mandelbrot_steps(C64(re: -1, im: 0), 100))   // 100
}
```

A complex number is a struct in every other respect: it is constructed by name, its parts are fields (`z.im = 0`), and `is` compares the parts as floats. It is not ordered, and there is no imaginary literal.

## Slices

`[]T` is a pointer and a length. It is what you use in C when you pass `(T *p, size_t n)`, packaged so the two cannot get separated. The fields `.ptr` and `.len` can be read but not assigned. `[]const T` is a read-only view.

```gamma
slices_demo :: fn() needs Out {
    data := [_]i32(1, 2, 3, 4, 5, 6)
    mid := data[1..4]                  // []i32 viewing 2, 3, 4
    mid[0] = 20                        // writes through: data[1] is now 20
    head := data[..2]                  // open-ended bounds only appear in brackets
    tail := data[4..]
    all := data[..]
    print("{} {} {} {}\n", mid, head, tail, all.len)
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

For what a reader sees as one character (`é` written as `e` plus a combining accent, a flag, an emoji sequence), iterate grapheme clusters with `graphemes` from `std/text`; each one is a substring.

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
tuples_demo :: fn() needs Out {
    pair := (3, "three")                    // (i32, string)
    point := (x: 1.5, y: 2.5)               // named; both elements default to f64
    single := (7,)                          // a one-element tuple needs the comma
    unit := ()                              // the empty tuple, also the type `()`
    print("{} {} {} {}\n", pair.0, pair.1, point.x, point.y)
    n, label := pair                        // destructure
    print("{} {}\n", n, label)
}
```

Two tuple types are the same if they have the same element types in the same order and the same names. A tuple is either fully named or fully unnamed. A tuple value converts to a tuple type with the same element types if *either* side is unnamed, which is why `return (n / d, n % d)` works for a function declared `-> (q: u32, r: u32)`. Two differently named tuples never convert to each other. A tuple *expression* written where a tuple type is expected converts each element on its own, so `(a: 1, b: null)` fits `(a: u8, b: ?u8)`, and `return (n, v)` with `v: u8` fits `-> (u32, ?u8)`. Tuples are how functions return several values. They compare with `is` element by element, and with `<` lexicographically when their elements are orderable.

## Structs

A `struct` literal creates a new, nominal type. Fields are declared `name: Type`, optionally with a default, a constant that is computed once at compile time (a default that fails to evaluate is a compile error), and `a, b: T` declares several at once. Fields are separated by newlines (or `;`).

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
structs_demo :: fn() needs Out {
    v := Vec2(x: 1.5, y: -2)
    w := Window(title: "main")                    // size and open use defaults
    w2 := Window(..w, title: "other")             // w with a different title
    w2.open = false                               // fields are assignable on a mutable variable
    print("{}\n", v)                              // Vec2(x: 1.5, y: -2)
    print("{} {}\n", w2.title, w2.open)
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
unions_demo :: fn(s: Shape) needs Out {
    unit_circle := Shape.Circle(1.0)
    box: Shape = .Rect(w: 2, h: 3)
    if s is .Circle(_) { print("a circle\n") }
    if s is .Empty { print("nothing\n") }
    if .Rect(w, h) := box { print("{} by {}\n", w, h) }
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
- `@variants(Dir)` is a constant array holding every variant of an enum in declaration order, so `for d in @variants(Dir)` visits them all and `@variants(Dir).len` counts them. `@variant_name(x)` gives the active variant's name as a `string` (`"E"` for `Dir.E`) for any tagged union, with or without payloads.
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

### Arrays indexed by an enum

Where C keeps a table indexed by an enumeration constant (`names[COLOR_COUNT]`), Gamma has `[E]T`: one element per variant of the enum `E`, indexed by `E` values. It is built by naming every variant, so an entry cannot be forgotten or listed against the wrong variant, and indexing it needs no bounds check:

```gamma
NAMES :: [Dir]string(N: "north", E: "east", S: "south", W: "west")

compass_demo :: fn() needs Out {
    visits := [Dir]u32(fill: 0)
    visits[.E] += 1                                // `.E` is a `Dir`, from the index
    visits[turn_right(.E)] += 2
    for d in @variants(Dir) { print("{}: {}\n", NAMES[d], visits[d]) }
}
```

An integer index is a compile error. The elements sit in declaration order (the order of `@variants`), and `a[..]` gives them as a slice when you do want positions, for example to call a slice method; there is no other slicing, no array method and no implicit conversion to a slice. `[Dir]bool` doubles as a set of directions.

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
| `x.round(T)`, `x.floor(T)`, `x.ceil(T)` | float to integer, rounding as named (checked) |
| `x.to(F)` | integer or float to the float type `F`: the nearest value, never traps (`i.to(f64)` for an index `i`) |
| `x.checked(T)` | `?T`: `null` if the value does not fit |
| `x.sat(T)` | clamp to `T`'s range (NaN becomes 0) |
| `x.bits(T)` | reinterpret the bytes of one plain-data type as another of the same size |

```gamma
lossy_demo :: fn(big: u32, f: f64, g: f32) needs Out {
    a := big.trunc(u8)                    // low 8 bits, always defined
    b := big.checked(u8) else 255         // 255 if it does not fit
    c := big.sat(u8)                      // clamps to 255
    d := f.trunc(i32)                     // 3.7 -> 3 (traps if out of range or NaN)
    e := f.round(i32)                     // 3.7 -> 4
    raw := g.bits(u32)                    // the IEEE bit pattern of an f32
    print("{} {} {} {} {} {}\n", a, b, c, d, e, raw)
}
```

There is no conversion from an integer to `bool` (write `x is not 0`), none from integers to unions (use `from_tag`), none between distinct struct types, and none from a slice to a pointer (use `.ptr`).

> **Common mistake.** There are no C-style casts. `(u8)x` and `x as u8` do not exist. Write `u8(x)` if it is lossless, or choose `x.trunc(u8)`, `x.checked(u8)` or `x.sat(u8)` to say what should happen when it is not.

## Type aliases

Binding a type expression to a name makes an alias, a second name for the same type, not a new type:

```gamma
Bytes :: []u8
Matrix3 :: [3][3]f64

alias_demo :: fn(xs: []const i32) {
    Elem :: @type_of(xs[0])             // a local alias: i32
    total: Elem = 0
    for x in xs { total += x }
}
```

Only `struct`, `union` and `effect` literals create new types, and they are declared at top level. Aliases may also appear inside a function, where they are visible for the rest of the block.

## Summary

- Arrays are values, slices are `(ptr, len)`, strings are `[]const u8`, and none of them converts implicitly to a pointer.
- Structs are constructed by name; `using` gives embedding; unions are tagged and matched exhaustively.
- `[E]T` is an array indexed by an enum, built by naming every variant.
- Pointers are never null unless spelled `?`; `[^]T` is for C-style arithmetic.
- `T(x)` is lossless, and everything lossy is a named method.

Next, [chapter 5](05-optionals-and-errors.md) covers optionals and error results.

For the full rules, see [§5 Types][s5] and [§6 Literals, constants and conversions][s6] of the specification.

[s5]: ../../gamma-spec.md#5-types
[s6]: ../../gamma-spec.md#6-literals-constants-and-conversions
[s21]: ../../gamma-spec.md#21-the-core-library
