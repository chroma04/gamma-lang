# Gamma for C programmers

[Contents](README.md) · [Gamma for Rust programmers](for-rust-programmers.md)

This page is for someone who writes C for a living and wants to know what Gamma looks like from there: which C constructs map onto which Gamma constructs, what has been removed, and why. It is a map, not a tutorial. Each section gives the C idiom, its Gamma replacement with a short program you can run, and a sentence on the reason. The chapters of the [guide](README.md) cover each area in depth, and this page links to them. The [specification](../../gamma-spec.md) is the normative definition; its [Appendix C](../../gamma-spec.md#appendix-c-design-rationale) derives every rule below from the five design principles.

Every complete program on this page is run by the documentation test suite, under the interpreter and under the C back end, and its output block is the real output.

## The big picture

Gamma keeps what makes C worth using:

- **Data is bytes you can lay out.** Structs have C's layout, integers have exact widths, and a struct of C-compatible fields can be handed to C code unchanged. There is no object header, no runtime type information and no hidden indirection.
- **There is no garbage collector, no ownership system and no destructor.** You decide where objects live and when they die. Values copy bitwise, and freeing is your job.
- **Cost is visible where it is paid.** Nothing allocates behind your back, a `for` loop is a counted loop, and a function call is a call.
- **It compiles to C.** `gamma build` writes C and runs your C compiler, so the result links with C code and runs wherever a C compiler does.

What it removes is the part of C that the compiler cannot help you with:

- implicit numeric conversions, signed overflow as undefined behavior, uninitialized integers that are undefined to read;
- null as a value every pointer might hold, arrays that decay to pointers and lose their length, strings that need a terminator and a `strlen` to measure;
- `switch` fallthrough, `x & 1 == 0`, `if (x = 0)`, and the preprocessor;
- ignored return codes, `errno`, and `goto cleanup` ladders.

And it changes how *context* is handled. Everything a C function depends on besides its parameters (which allocator, where the log goes, what happens on a fatal error, which global it reads) lives in globals, function pointers with a `void *` argument, `errno` and `setjmp`. Nothing in the function's type says so. In Gamma, **everything a function depends on appears in its signature**:

```gamma fragment
load :: fn(path: string) -> LoadError!Config needs Alloc + Fs
```

That reads: it takes a path; it may fail with a `LoadError`; it allocates and touches the file system. Inputs are parameters, absence is `?T`, failure is `E!T`, and context is the effect set after `needs`. The effect set replaces globals, callbacks with `void *` and `setjmp`, and most of this page is about how.

## Translation table

| C | Gamma | Notes |
| --- | --- | --- |
| `int x = 5;` | `x := 5` or `x: i32 = 5` | `:=` mutable, `::` immutable |
| `const int x = 5;` | `x :: 5` | an immutable binding |
| `#define N 64` | `N :: 64` | a compile-time constant, re-typed at each use |
| `int`, `long`, `short`, `char` | `i32`, `i64`, `i16`, `i8` or `u8` | exact widths; `c.c_int` etc. from `std/c` for C's own |
| `unsigned`, `uint8_t`, `uint64_t` | `u32`, `u8`, `u64` | any width from 1 to 128 bits |
| `size_t`, `ptrdiff_t` | `usize`, `isize` | distinct types, not aliases of `u64` |
| `float`, `double` | `f32`, `f64` | also `f16` |
| `bool` / `_Bool` | `bool` | not an integer |
| `char *` (text) | `string` | a `[]const u8`: pointer and length, not NUL-terminated |
| `const char *` for C APIs | `cstring` | a `[^]const u8`; the literal `c"text"` |
| `T *` (one object) | `^T` | never null |
| `const T *` | `^const T` | |
| `T *` (array start) | `[^]T` | arithmetic and unchecked indexing |
| `T *` (may be `NULL`) | `?^T` | same size and representation |
| `void *` | `rawptr` | cast back with `p.cast(^T)` |
| `T a[N]` | `[N]T` | a value: it copies and never decays |
| `(T *p, size_t n)` | `[]T` | a slice; `[]const T` is read-only |
| `struct` | `struct` | same layout; constructed by field name |
| `union` (untagged) | `#c_union union` | for interop only |
| `enum` + `union` tagged by hand | `union` | tag and payload are one type, `match` is exhaustive |
| `enum { A, B }` | `union { A, B }` | a union with no payloads |
| `NULL` | `null` | only in `?T` |
| `switch` | `match` | exhaustive, no fallthrough, ranges and destructuring |
| `cond ? a : b` | `if cond { a } else { b }` | `if` is an expression |
| `for (int i = 0; i < n; i++)` | `for i in 0..n` | |
| `while (x = next())` | `while x := next()` | the binding form tests for `null` |
| `goto cleanup;` | `defer`, `errdefer` | cleanup runs on every way out |
| `goto` to leave nested loops | `break outer` | labels on loops |
| `return -1` / `errno` | `return !.Error` with `E!T` | `try` propagates |
| `x == y`, `x != y` | `x is y`, `x is not y` | structural, contents of strings and arrays |
| `&&`, `\|\|`, `!` | `and`, `or`, `not` | |
| `a ^ b` (xor) | `a ~ b` | `^` is the pointer sigil |
| `*p`, `p->f` | `p^`, `p.f` | field access dereferences one level |
| `(u8)x` | `u8(x)` if lossless; else `x.trunc(u8)`, `x.checked(u8)`, `x.sat(u8)` | no casts |
| `sizeof(T)`, `_Alignof(T)` | `@size_of(T)`, `@align_of(T)` | compile-time constants |
| `offsetof(S, f)` | `@offset_of(S, f)` | |
| `_Static_assert(c, "m")` | `@static_assert(c, "m")` | |
| `malloc(n * sizeof(T))` | `make(T, n)` | returns `AllocError![]T`; needs `Alloc` |
| `malloc(sizeof(T))` | `new(init)` | returns `AllocError!^T` |
| `free(p)` | `free(s)`, `destroy(p)` | |
| `printf("%d\n", x)` | `print("{}\n", x)` | checked against the arguments at compile time |
| `fprintf(stderr, ...)` | `eprint(...)` | performs `Err` |
| `va_list` | `values: ..A` | a typed tuple, count known at compile time |
| `static int counter;` | a struct you pass, or an effect | `#global` exists for interop and costs `Sys` |
| callback + `void *ctx` | a `fn` parameter that captures, or an effect | closures capture by reference and never escape |
| visitor callback | an iterator that calls `yield` | `for` is the handler |
| `setjmp` / `longjmp` | an `esc` operation | unwinds, running `defer` |
| `#ifdef _WIN32` | `when target.os is .Windows { ... }` | the other branch is not even type-checked |
| `#include "x.h"` | `x :: import "./x"` | no headers: a module's `pub` names are its interface |
| macros | functions, compile-time evaluation, `when` | there is no macro language |
| `main(int argc, char **argv)` | `main :: fn() needs Env`, `Env.args()` | `main` can also return `u8` or `E!()` |
| `exit(n)` | `Env.exit(n)` or `main` returning `u8` | |
| `assert(c)` | `assert(c, "msg")` | removed in Fast builds, like `NDEBUG` |
| `abort()` | `trap("msg")` | prints a location and exits with status 134 |
| `__attribute__((packed))` | `#packed struct` | also `#align(n)` |
| bit-fields | `#bits(u16) struct` | exact layout, unlike C's |
| `volatile` | `@volatile_load`, `@volatile_store` | perform `Sys` |
| `_Atomic`, `<stdatomic.h>` | `@atomic_add(p, 1, .SeqCst)` etc. | the C11 memory model |
| `extern int f(int);` | `lib :: foreign "c" { f :: fn(x: c.c_int) -> c.c_int }` | needs `Sys` |
| exporting a function | `#export("name")` | C ABI, empty effect set or `Sys` |

The rest of the page takes these in order of how often you will run into them.

## Hello, declarations and the tool

```c
#include <stdio.h>

int main(void) {
    int total = 0;
    for (int i = 0; i < 10; i++) total += i;
    printf("total = %d\n", total);
    return 0;
}
```

```gamma
main :: fn() needs Out {
    total := 0
    for i in 0..10 { total += i }
    print("total = {}\n", total)
}
```

```output
total = 45
```

Three things differ at once. A name is introduced by `::` (immutable) or `:=` (mutable), and a function is just an immutable name bound to a function value, so there is no separate declaration syntax and no forward declaration: at the top level, order does not matter. `needs Out` says that `main` writes output; `print` performs the `Out` effect, and the compiler rejects a program in which that is not declared. And `total` has no written type because literals are typed by inference over the whole function (here it becomes `i32`, the fallback when nothing else decides).

The tool is one binary, `gamma`:

```text
gamma run hello.gm                  compile through C and run (fast code)
gamma run --backend=interp hello.gm   interpret (starts at once, no C compiler needed)
gamma build hello.gm                write a native executable `hello`, next to the generated hello.c
gamma check hello.gm                type-check only
gamma test hello.gm                 run the `test` declarations
gamma fmt hello.gm                  format in place; there is one style
```

`gamma build` generates C and calls `cc`, `gcc` or `clang`, so your existing C toolchain, linker, debugger (`gamma build -g` maps the executable to the `.gm` source) and sanitizers all apply. [Chapter 1](01-getting-started.md) has the details.

Statements end at a newline, `;` is optional, and a line continues only when it ends with an operator or an opening bracket, so write the operator at the end of the line, not at the start of the next. `main` can also return a status, which replaces `return 1;`:

```gamma
main :: fn() -> u8 needs Out + Env {
    args := Env.args()                       // like argv: args[0] is the program
    name := if args.len > 1 { args[1] } else { "world" }
    print("hello, {} ({} arguments)\n", name, args.len - 1)
    return 0
}
```

```output
hello, world (0 arguments)
```

## Declarations: no shadowing, no uninitialized surprises

C lets an inner declaration hide an outer one silently. Gamma does not, within a function:

```gamma error
total_of :: fn(n: u32) -> u32 {
    total := 0
    for i in 0..n {
        total := total + i      // error: `total` is already in scope
    }
    return total
}
```

The rule removes the "which `x` did I mean?" class of bug, and it is why you will write `m := n` when you want a modifiable copy of a parameter (parameters are immutable).

Uninitialized storage is opt-in and limited to plain data:

```gamma
main :: fn() needs Out {
    buf: [8]u8 = ---                 // like `uint8_t buf[8];`, but you asked for it
    buf.fill(7)
    zeros := [4]i32(fill: 0)         // like `int zeros[4] = {0};`
    print("{} {}\n", buf, zeros)
}
```

```output
[7, 7, 7, 7, 7, 7, 7, 7] [0, 0, 0, 0]
```

`---` is allowed only for integers, floats and arrays, tuples and structs of them. Reading it gives an unspecified value but is never undefined behavior, and Debug builds fill it with `0xAA` so the mistake is easy to see. `bool`, pointers, slices and unions cannot be left uninitialized, so you cannot manufacture an invalid value by forgetting an initializer. See [chapter 2](02-basics.md).

## Integers and conversions

### Types

`int` is whatever the platform says; `i32` is 32 bits everywhere. `usize` and `isize` are the pointer-sized types, and they are distinct from `u64` and `i64` on purpose, so code that is right on a 64-bit machine does not silently break on a 32-bit one. Any width from `u1` to `u128` exists, which makes protocol fields exact. When you need C's own platform-dependent types (`int`, `long`, `size_t`, `char`), `std/c` provides them as aliases: `c.c_int`, `c.c_long`, and so on.

There is no `char` type. A character literal like `'a'` is an integer literal whose value is the code point, usable as a `u8` or a `rune` (`u32`) as long as it fits.

### No implicit conversions

C's usual arithmetic conversions are the origin of a great many bugs. In Gamma both operands of a binary operator must have the same type, and the only implicit conversions are *lossless widenings* at coercion sites (initialization, assignment, arguments, return):

```gamma error
mix :: fn(a: u8, b: u16) -> u16 {
    return a + b            // error: u8 + u16, operand types differ
}
```

You say what you mean:

```gamma
main :: fn() needs Out {
    small: u8 = 200
    big: u32 = 300
    wide := u32(small) + big            // lossless: u32(...) compiles only if every u8 fits
    print("{}\n", wide)
    print("{} {} {}\n", big.trunc(u8), big.sat(u8), big.checked(u8))
    f := 3.7
    print("{} {}\n", f.trunc(i32), f.round(i32))
}
```

```output
500
44 255 null
3 4
```

`T(x)` is allowed only when every value of the *type* of `x` fits in `T`, regardless of what the compiler could prove about the actual value; that keeps legality independent of the optimizer. Everything lossy is a named method that says what happens to the lost information: `trunc` keeps the low bits (C's cast), `sat` clamps, `checked` gives `null` if it does not fit, and `round`, `floor`, `ceil` convert floats. There are no C-style casts. See [chapter 4](04-types.md).

### Overflow is never undefined

Signed overflow in C is undefined, and compilers delete checks because of it. In Gamma plain `+ - *` and `<<` are **checked**: they trap in Debug, Checked and Safe builds, and give an unspecified-but-valid value in Fast builds. Overflow never lets the optimizer assume anything. When you want a specific behavior you choose an operator:

| Operator | On overflow | C equivalent |
| --- | --- | --- |
| `a + b` | traps (Fast: unspecified value) | signed: UB |
| `a +% b` | wraps modulo 2^N | unsigned arithmetic, or `-fwrapv` |
| `a +\| b` | saturates | a hand-written clamp |
| `a +? b` | `null` (type `?T`) | `__builtin_add_overflow` |

```gamma
main :: fn() needs Out {
    x: u8 = 250
    print("{} {} {} {}\n", x +% 10, x +| 10, x +? 10, x +? 10 else 255)
    hash: u32 = 2166136261
    for b in bytes("gamma") {
        hash = (hash ~ u32(b)) *% 16777619          // FNV-1a: wrapping is the point
    }
    print("{}\n", hash)
}
```

```output
4 255 null 255
3492353034
```

The same four flavors exist for `-`, `*` and `<<`. Use plain `+` when overflow would be a bug (the trap tells you where), `+%` for hashes and ring counters. Shifts are checked too: the shift amount must be an unsigned integer and less than the width, and it is never undefined. Division by zero, and `MIN / -1`, **always trap, in every build mode**. There is no way to disable that check, because the hardware traps anyway.

```text
$ gamma run overflow.gm
trap: integer overflow in `+`
  at overflow.gm:3:10 in main
$ echo $?
134
```

## Operators: the traps are compile errors

| Mistake in C | In Gamma |
| --- | --- |
| `x & 1 == 0` parses as `x & (1 == 0)` | `==` does not exist; `x & 1 is 0` parses as `(x & 1) is 0` because bitwise operators bind tighter than comparisons |
| `if (x = 0)` | assignment is a statement, so this is a syntax error |
| `a & b \| c`, `1 << n - 1`, `a < b < c` | compile errors: parenthesize |
| `a && b \|\| c` | compile error: mixing `and` and `or` without parentheses |
| `if (n)` for an integer | conditions must be `bool`: `if n is not 0` |
| `a ^ b` | `a ~ b` (`^` is the pointer sigil) |

```gamma
main :: fn() needs Out {
    flags: u32 = 0b1010
    n: u32 = 3
    print("{}\n", flags & 1 is 0)                      // (flags & 1) is 0
    print("{}\n", flags & (1 << n) is not 0)           // bit 3 is set
    print("{}\n", (flags | 1) ~ 0b11)                  // 0b1011 xor 0b0011
    if flags > 5 and not (flags is 7) { print("big\n") }
}
```

```output
true
true
8
big
```

```gamma error
parity :: fn(x: u32) -> bool {
    return x & 1 == 0           // error: `==` is not an operator; compare with `is`
}
```

Equality `is` is *structural*: integers and floats by value (IEEE for floats), pointers by address, structs field by field, and **strings and slices by content**. So `name is "admin"` is `strcmp(name, "admin") == 0`, and arrays compare element by element. Structs are compared with `is` but not ordered. See [chapter 2](02-basics.md).

## Control flow

### `switch` becomes `match`

```c
switch (c) {
case 'a': case 'e': case 'i': case 'o': case 'u':
    kind = 1; break;
case '0' ... '9':               /* GNU extension */
    kind = 2; break;
default:
    kind = 0;
}
```

```gamma
classify :: fn(c: u8) -> u32 => match c {
    'a', 'e', 'i', 'o', 'u' => 1
    '0'..='9'               => 2
    _                       => 0
}

main :: fn() needs Out {
    for c in bytes("a1z") { print("{} ", classify(c)) }
    print("\n")
}
```

```output
1 2 0 
```

`match` is an expression and must be **exhaustive**, so adding a variant to a union stops every `match` that forgot it from compiling. There is no fallthrough and no `break` to forget. Arms can match ranges, tuples, structs and union payloads, and can have guards (`n if n > 10 => ...`). Because a bare name in a pattern always *binds* (it never compares), the compiler rejects a bare name that names a constant: `case LIMIT:` has no silent equivalent. See [chapter 3](03-control-flow.md).

### `if` and `while` bind

The C idiom "call, test for `NULL`, use" has a form that scopes the checked value:

```gamma
find :: fn(xs: []const i32, target: i32) -> ?usize {
    for i, x in enumerate(xs) {
        if x is target { return i }
    }
    return null
}

main :: fn() needs Out {
    data := [_]i32(4, 8, 15, 16, 23, 42)
    if i := find(data[..], 15) {
        print("found at {}\n", i)          // `i` is a usize, definitely present
    } else {
        print("not found\n")
    }
    pos := find(data[..], 99) else 0
    print("{}\n", pos)
}
```

```output
found at 2
0
```

`if` is an expression (so there is no `?:`), braces are mandatory, and the condition must be a `bool`. `while x := next()` loops until the producer returns `null`. A pattern binding with `else` flattens the early-return style: `n := lookup(k) else return -1`.

### Loops and labels

`for` covers the counted loops and loops over arrays and slices. `break` and `continue` work as in C, and a label names an outer loop, which is the legitimate use of `goto`:

```gamma
main :: fn() needs Out {
    grid := [3][3]u8(fill: [3]u8(fill: 1))
    grid[1][2] = 0
    outer: for row in 0..3 {
        for col in 0..3 {
            if grid[row][col] is 0 {
                print("zero at {},{}\n", row, col)
                break outer
            }
        }
    }
}
```

```output
zero at 1,2
```

There is no `goto`. Cleanup, the other legitimate use, is `defer`.

### `defer` replaces `goto cleanup`

```c
int copy_file(const char *src, const char *dst) {
    int rc = -1;
    FILE *in = fopen(src, "rb");
    if (!in) goto out;
    FILE *out = fopen(dst, "wb");
    if (!out) goto close_in;
    /* ... copy ... */
    rc = 0;
    fclose(out);
close_in:
    fclose(in);
out:
    return rc;
}
```

`defer S` runs `S` when control leaves the enclosing block, however it leaves: falling off the end, `return`, `break`, `continue` or a `try` that propagates an error. Deferred statements run in reverse order. You write the release right after the acquisition, so the pair stays together when the function is edited:

```gamma
Pair :: struct { a, b: []u8 }

make_pair :: fn(n: usize) -> AllocError!Pair needs Alloc {
    a := try make(u8, n)
    errdefer free(a)                // only if we fail after this point
    b := try make(u8, n)
    return Pair(a: a, b: b)
}

main :: fn() -> AllocError!() needs Alloc + Out {
    p := try make_pair(16)
    defer free(p.a)
    defer free(p.b)
    print("{} {}\n", p.a.len, p.b.len)
    return ()
}
```

```output
16 16
```

`errdefer` runs only if the block is left by an error, which is exactly the "undo partial work" half of a `goto` ladder. If the second `make` fails, `a` is freed and the error goes to the caller; if it succeeds, `a` is returned alive. A `defer` that is never reached is never registered, so you never release what you did not acquire. See [chapter 3](03-control-flow.md#defer-and-errdefer).

## Pointers, arrays, slices and strings

### One pointer type per use

A C `T *` means one of four things: a pointer to one object, the start of an array, something that may be `NULL`, or an untyped blob. Gamma writes each differently:

| Meaning | Type | Operations |
| --- | --- | --- |
| one object | `^T`, `^const T` | `p^`, `p.field`; no arithmetic, no indexing |
| start of an array | `[^]T`, `[^]const T` | `p + n`, `p - q`, `p[i]` (unchecked) |
| array with length | `[]T`, `[]const T` | `s[i]` (checked), `s.len`, `s[a..b]` |
| may be absent | `?^T`, `?[^]T` | must be tested before use |
| unknown | `rawptr` | `p.cast(^T)` |

**No pointer type admits null.** `?^T` has the same size as `^T`, with null as address 0, so it is a drop-in for a C pointer that may be `NULL`; it is also what C code sees when you pass one across the boundary. `&x` takes an address, `p^` dereferences, and `p.f` dereferences one level so there is no `->`.

```gamma
Node :: struct {
    value: i32
    next: ?^Node
}

sum_list :: fn(head: ?^const Node) -> i32 {
    total := 0
    cur := head
    while n := cur {                   // `n` is a ^const Node, never null
        total += n.value
        cur = n.next
    }
    return total
}

swap :: fn(a, b: ^i32) {
    t := a^
    a^ = b^
    b^ = t
}

main :: fn() needs Out {
    c := Node(value: 3, next: null)
    b := Node(value: 2, next: &c)
    a := Node(value: 1, next: &b)
    x: i32 = 1
    y: i32 = 2
    swap(&x, &y)
    print("{} {} {}\n", sum_list(&a), x, y)
}
```

```output
6 2 1
```

Two conversions deserve a note. A pointer to an array coerces to a slice, and a slice has its pointer and length as read-only fields `.ptr` and `.len`. And the explicit pointer operations are methods, so they are easy to grep for: `p.cast(P)` (checked for alignment), `p.addr()` (the address as a `usize`) and `n.to_ptr(P)` (an integer to a `?P`, null for 0). Gamma does not track pointer lifetimes in general, but it rejects the simplest dangling pointers at compile time:

```gamma error
first_word :: fn() -> []u8 {
    buf: [16]u8 = ---
    return buf[..]              // error: returns a slice of a local variable
}
```

### Arrays are values; slices carry their length

`[N]T` is exactly `N` elements. Assigning, passing and returning an array copies it, and it never decays to a pointer. The `(T *p, size_t n)` pair you pass everywhere is one type, `[]T`, whose indexing is bounds-checked:

```c
long sum(const int *p, size_t n) {
    long t = 0;
    for (size_t i = 0; i < n; i++) t += p[i];
    return t;
}
```

```gamma
sum :: fn(xs: []const i32) -> i64 {
    total: i64 = 0
    for x in xs { total += x }          // += widens a smaller right-hand side
    return total
}

main :: fn() needs Out {
    data := [_]i32(5, 10, 15, 20, 25)
    print("{}\n", sum(data[..]))
    print("{}\n", sum(data[1..4]))
    mid := data[1..4]
    mid[0] = 100                        // a view: this writes through to `data`
    print("{} {}\n", data, data.get(9))
}
```

```output
75
45
[5, 100, 15, 20, 25] null
```

`data[1..4]` is a view with no copy (like `data + 1, 3`), and `lo <= hi <= len` is checked. `a[i]` traps when out of range, and `a.get(i)` gives an optional instead; both exist so you pick whether a bad index is a bug or an expected case. Slices also remove `memcpy` calls with a size you must keep in sync: `dst.copy_from(src)`, `buf.fill(0)`, `xs.sort()`, `xs.index_of(v)` and `xs.swap(i, j)` are methods that know the length.

In a Fast build an out-of-bounds index is undefined behavior again, as in C, because the check is removed; see the [safety section](#safety-and-build-modes).

### Many-pointers: C's `T *` when you need it

`[^]T` is C's pointer-into-an-array: it supports `p + n`, `p - q` and unchecked `p[i]`, and slicing it with both bounds gives a checked `[]T`. It exists for the C boundary and for the rare data structure that needs arithmetic. Prefer slices everywhere else; when you must, convert once at the boundary:

```gamma
checksum :: fn(p: [^]const u8, n: usize) -> u32 {
    total: u32 = 0
    for b in p[0..n] { total = total +% u32(b) }     // checked from here on
    return total
}

main :: fn() needs Out {
    msg := "gamma"
    print("{}\n", checksum(msg.ptr, msg.len))
}
```

```output
515
```

### Strings

A `string` is a `[]const u8`: a length and a pointer to (usually UTF-8) bytes in read-only memory. Literals are not NUL-terminated, so `strlen` becomes `.len` and costs nothing, a substring `s[a..b]` shares memory, and `==` on the contents is `is`:

```gamma
main :: fn() needs Out {
    s := "key=value"
    eq := 0
    for i, c in enumerate(s) {
        if c is '=' { eq = i }
    }
    key := s[0..eq]
    value := s[eq + 1..]
    print("[{}] [{}] {} {}\n", key, value, key is "key", s.len)
}
```

```output
[key] [value] true 9
```

Text is bytes. `bytes(s)` yields `u8`, `runes(s)` decodes UTF-8 into `rune` values, and `graphemes` in `std/text` yields what a reader sees as one character. For C functions there is `cstring` (a `[^]const u8`) and the literal `c"..."`; a `string` never converts to a `cstring` by accident, and a `cstring` is turned back into a string by slicing, `s[0..s.cstr_len()]`. Assembling text means writing into a buffer you own: a `[N]u8`, an allocator-backed `List(u8)` with `write` (like `snprintf`), or `str.format`. The standard library has no string-builder type. See [chapter 4](04-types.md#strings) and [chapter 16](16-standard-library.md).

## Structs, unions and enums

### Structs

Layout is C's: fields in declaration order, each aligned to its own alignment, the whole rounded up. Construction is by field name, which replaces designated initializers and cannot go stale when fields are reordered:

```gamma
Header :: struct {
    magic: u32
    version: u16
    flags: u16
    length: u64
}

@static_assert(@size_of(Header) is 16, "Header must match the on-disk format")
@static_assert(@offset_of(Header, length) is 8, "length must be 8-byte aligned")

Config :: struct {
    name: string
    retries: u32 = 3                  // defaults must be constant
    verbose: bool = false
}

main :: fn() needs Out {
    h := Header(magic: 0x474D4C, version: 1, flags: 0, length: 4096)
    c := Config(name: "test")
    d := Config(..c, verbose: true)   // this value, with one field changed
    print("{} {} {}\n", h.length, c.retries, d.verbose)
    print("{}\n", @size_of(Header))
}
```

```output
4096 3 true
16
```

Positional construction (`Vec2(1.5, -2)`) is an error, and so is leaving out a field that has no default, so there is no zero-initialization by omission. Fields are private to their module unless marked `pub`. A struct whose fields are all C-compatible has exactly the C layout, and `#packed` and `#align(n)` adjust it ([chapter 14](14-interop.md)). There is no inheritance; the first field can be declared `using` to embed another struct, which promotes its fields and lets `^Outer` coerce to `^Inner` for free. See [chapter 4](04-types.md#structs).

### Tagged unions

The C idiom of an `enum` tag next to a `union` of payloads is one type in Gamma, and the compiler keeps tag and payload together:

```c
struct Shape {
    enum { CIRCLE, RECT, EMPTY } kind;
    union { float r; struct { float w, h; } rect; } u;
};
float area(const struct Shape *s) {
    switch (s->kind) {
    case CIRCLE: return 3.14159f * s->u.r * s->u.r;
    case RECT:   return s->u.rect.w * s->u.rect.h;
    }
    return 0;   /* and hope the tag was valid */
}
```

```gamma
Shape :: union {
    Circle: f32
    Rect: (w: f32, h: f32)
    Empty
}

area :: fn(s: Shape) -> f32 => match s {
    .Circle(r)  => 3.14159 * r * r
    .Rect(w, h) => w * h
    .Empty      => 0
}

main :: fn() needs Out {
    shapes := [_]Shape(.Circle(1.0), .Rect(w: 2, h: 3), .Empty)
    for s in shapes { print("{} has area {}\n", s, area(s)) }
}
```

```output
.Circle(1) has area 3.14159
.Rect(w: 2, h: 3) has area 6
.Empty has area 0
```

You cannot read the wrong payload, because the only way in is a pattern that names the variant. A tagged union is laid out as `{ tag, payload }`. For a C-style untagged union (reading one field after writing another, for interop) there is `#c_union union { ... }`, and the safer way to reinterpret plain data is `x.bits(u32)`.

### Enums

An enum is a union with no payloads. The tag type and the values can be given, which is what you want for numbers that come from a protocol or from C:

```gamma
Color :: union(u8) { Red = 1, Green = 2, Blue = 4 }

NAMES :: [Color]string(Red: "red", Green: "green", Blue: "blue")

main :: fn() needs Out {
    c := Color.Green
    print("{} {} {}\n", NAMES[c], u8(c), @variants(Color).len)
    for v in @variants(Color) { print("{}={} ", NAMES[v], u8(v)) }
    print("\n")
    print("{} {}\n", Color.from_tag(4), Color.from_tag(3))
}
```

```output
green 2 3
red=1 green=2 blue=4 
.Blue null
```

Three C habits have a replacement here. `Color.from_tag(n)` turns an integer into an enum and gives `null` for a value that is not a tag, because integers from the network or from C may be out of range and there is no unchecked cast. `[Color]string(...)` is a table indexed by an enum, built by naming *every* variant, so it cannot have a missing or misplaced entry, and indexing it needs no bounds check. And `@variants(Color)` is the list of variants in declaration order, which is what you used X-macros for.

## Errors: `errno` becomes `E!T`

C reports "no value" with `NULL` or `-1` and "failed" with a return code and `errno`, and nothing makes you look. Gamma has two types:

- `?T` is a `T` that may be absent.
- `E!T` is a `T` or an error `E`. By convention `E` is a union with one variant per kind of failure.

An unhandled result is a compile error: an expression statement must have type `()`, so you cannot drop a return code by accident. To ignore one on purpose write `_ = f()`.

```c
int parse_port(const char *s, uint16_t *out) {
    if (*s == '\0') return -EINVAL;
    unsigned long n = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -EINVAL;
        n = n * 10 + (*s - '0');
        if (n > 65535) return -ERANGE;
    }
    *out = (uint16_t)n;
    return 0;
}
```

```gamma
ParseError :: union { Empty, BadDigit: usize, TooBig }

parse_port :: fn(s: string) -> ParseError!u16 {
    if s.len is 0 { return !.Empty }
    n: u16 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
        n = n *? 10 +? u16(c - '0') else return !.TooBig
    }
    return n
}

main :: fn() needs Out {
    for text in [_]string("8080", "", "80x", "99999") {
        match parse_port(text) {
            !.Empty        => print("empty\n")
            !.BadDigit(at) => print("bad digit at {}\n", at)
            !.TooBig       => print("too big\n")
            port           => print("port {}\n", port)
        }
    }
}
```

```output
port 8080
empty
bad digit at 2
too big
```

The success value is returned as a value, not through an out-parameter; `!e` builds an error; the error carries a payload (`BadDigit` says where) that `errno` cannot; and `*?` and `+?` give `null` on overflow, which `else return` turns into an error. `match` on a result is exhaustive like any other.

### `try`: the `if (rc) return rc;` ladder

`try x` unwraps a success and otherwise returns the error from the enclosing function, running its `defer` and `errdefer` statements on the way:

```gamma
ParseError :: union { Empty, BadDigit: usize }

ConfigError :: union { Parse: ParseError, Missing }

digits :: fn(s: string) -> ParseError!usize {
    if s.len is 0 { return !.Empty }
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
    }
    return s.len
}

count_digits :: fn(text: ?string) -> ConfigError!usize {
    s := text else return !.Missing
    return try digits(s)               // a ParseError becomes ConfigError.Parse
}

main :: fn() -> ConfigError!() needs Out {
    print("{}\n", try count_digits("8080"))
    print("{}\n", count_digits(null))
    print("{}\n", count_digits("80x"))
    return ()
}
```

```output
4
!.Missing
!.Parse(.BadDigit(2))
```

The error type of `count_digits` differs from the callee's, and that is allowed: if the target is a union with *exactly one* variant that carries the source error type, the error is wrapped into it automatically (error *lifting*). `try` is the one place where a failure leaves the function; had `main` written `try count_digits("80x")`, `main` would have returned the error, and the runtime would have written it to standard error and exited with status 1, which is what `return 1;` from `main` did. Fallbacks are `digits(s) else 0`, or `else e => ...` when the fallback depends on the error; `r!` asserts success and traps otherwise, for the places where failure cannot happen.

Allocation failure is a result too (`AllocError`), so the whole `malloc`-returned-`NULL` class is in the types.

## Memory

C's `malloc` is available to any function without saying so. In Gamma, allocation is an **effect**: `Alloc` supplies the current allocator, a function that allocates lists `needs Alloc` in its signature, and a function without `Alloc` does not allocate through the current allocator. Nothing in the language allocates implicitly: closures capture by reference, a `for` loop allocates nothing, and string literals are static.

| C | Gamma |
| --- | --- |
| `T *p = malloc(sizeof *p); *p = init;` | `p := try new(init)` |
| `T *a = malloc(n * sizeof *a);` (then initialize) | `a := try make_fill(n, x)` or `try make(T, n)` (uninitialized, plain data only) |
| `free(p)` | `destroy(p)` for `new`, `free(s)` for slices |
| `strdup`, `memdup` | `dupe(s)` |
| `realloc` growth in a loop | `list(T)`, then `push` |
| a hash table you wrote | `hm.hash_map(K, V)` from `std/collections/hash_map` |

```gamma
Point :: struct { x, y: i32 }

demo :: fn() -> AllocError!() needs Alloc + Out {
    p := try new(Point(x: 1, y: 2))
    defer destroy(p)
    p.x += 10

    buf := try make(u8, 64)
    defer free(buf)
    buf.fill(0)

    names := list(string)
    defer names.deinit()
    try names.push("ada")
    try names.push("grace")
    print("{} {} {} {}\n", p.x, buf.len, names.items.len, names.items)
    return ()
}

main :: fn() -> AllocError!() needs Alloc + Out {
    try demo()
}
```

```output
11 64 2 ["ada", "grace"]
```

Everything else about memory is as in C: objects live where you put them, values copy bitwise (a struct holding a pointer copies the pointer, so a double free is still possible), and a pointer to a local dies with its frame. There are no destructors and no move semantics. What changed is that the acquire and release sit next to each other with `defer`, and that failures are in the types.

### Allocators are replaceable

C libraries take a custom allocator through a global or a struct field of function pointers. In Gamma the caller installs one with `with`, and everything underneath that allocates through `Alloc` follows. The standard library has an arena (`std/arena`) and a fixed buffer (`std/fixed_buffer`), which makes a program with no heap possible:

```gamma
arena :: import "std/arena"
fixed_buffer :: import "std/fixed_buffer"

main :: fn() -> AllocError!() needs Alloc + Out {
    a := arena.new()                     // chunks come from the current allocator
    defer a.deinit()                     // releases everything at once
    with a.allocator() {
        words := list(u32)               // no deinit: the arena owns the memory
        for i in 0..1000 { try words.push(i) }
        print("{}\n", words.items.len)
    }

    store: [256]u8 = ---
    fb := fixed_buffer.over(store[..])   // allocates from a buffer you own
    with fb.allocator() {
        small := list(u8)
        for i in 0..100 { try small.push(i.trunc(u8)) }
        r := make(u8, 1000)
        print("{} {}\n", small.items.len, r)
    }
}
```

```output
1000
100 !.OutOfMemory
```

Long-lived structures (`list`, `hm.hash_map`) *capture* the allocator they were created with and use that same handle for every later operation, so the C bug of freeing through a different allocator than the one that produced the memory cannot arise from ordinary use (Debug builds additionally tag every allocation with its allocator and trap on a mismatch). An `Allocator` is a data pointer plus a table of three functions (allocate, resize, free), so writing your own is a few lines; see `examples/allocators.gm` for a tracking allocator.

### Leaks are test failures

Under `gamma test` the heap reports leaks, with the line that allocated each block:

```text
$ gamma test leak.gm
test "leaks" ... FAILED
    leaked 4 bytes
      4 bytes allocated at leak.gm:2:13
0 passed, 1 failed
```

and the interpreter's Debug mode traps on use after `free` and on a free through the wrong allocator. See [chapter 9](09-memory.md).

## Context: effects replace globals, callbacks and `setjmp`

This is the part of Gamma with no C counterpart, because C has no place for it. An **effect** is an interface whose implementation is supplied by the caller's context rather than by an argument. A function lists the effects it performs after `needs`; somewhere up the call chain a `with` block installs a **handler**. The compiler checks that every performed effect is handled, so there is no "unhandled" run-time case. [Chapter 7](07-effects.md) goes slowly; here are the three C patterns it replaces.

### A callback with `void *`

```c
typedef void (*log_fn)(int level, const char *msg, void *ctx);
void connect(const char *host, log_fn log, void *ctx) {
    log(1, "connecting", ctx);
    if (!*host) { log(3, "empty host", ctx); return; }
    log(0, "connected", ctx);
}
```

```gamma
Level :: union { Debug, Info, Error }

Log :: effect {
    log: fn(level: Level, msg: string)
}

connect :: fn(host: string) -> bool needs Log {
    Log.log(.Info, "connecting")
    if host.len is 0 {
        Log.log(.Error, "empty host")
        return false
    }
    Log.log(.Debug, "connected")
    return true
}

main :: fn() needs Out {
    printer :: handler Log {
        log(level, msg) => print("[{}] {}\n", level, msg)
    }
    ok := with printer { connect("example.org") }
    print("ok = {}\n", ok)

    errors := 0
    counter :: handler Log {
        log(level, msg) => {
            if level is .Error { errors += 1 }
        }
    }
    with counter { _ = connect("") }
    print("errors = {}\n", errors)
}
```

```output
[.Info] connecting
[.Debug] connected
ok = true
errors = 1
```

There is no function pointer parameter and no `void *`: the handler is a literal that captures `errors` by reference, and `connect` has no idea whether it is talking to a printer or a counter. The signature says `needs Log`, so a caller cannot forget that logging happens. The same mechanism is how `print` reaches standard output (`Out`), how `new` finds an allocator (`Alloc`), and how tests substitute a fake clock or file system.

### `static` state and globals

Mutable globals do not exist at the top level: top-level bindings are immutable and evaluated at compile time. Pass the state you need, in a struct, by pointer, or supply it through an effect as above. For interop there is `#global`, a real C-style global, and **every access performs `Sys`**, the effect that marks everything outside Gamma's model; a function without `Sys` therefore cannot observe or modify state outside its arguments, the memory they point to and its handlers. That guarantee is what lets the compiler evaluate such functions at compile time and lets tests run without mocks. See [chapter 14](14-interop.md#global-variables).

### `setjmp` and `longjmp`

An `esc` operation can exit the `with` that installed its handler, unwinding the frames in between. Unlike `longjmp`, the unwinding runs `defer` statements, and the exit point is visible in the signature:

```gamma
Abort :: effect {
    abort: esc(code: u32) -> never
}

digit :: fn(c: u8) -> u32 needs Abort {
    if c not in '0'..='9' { Abort.abort(1) }
    return u32(c - '0')
}

total :: fn(s: string) -> u32 needs Abort + Out {
    defer print("cleanup\n")
    sum := 0u32
    for c in bytes(s) { sum += digit(c) }
    return sum
}

main :: fn() needs Out {
    failed := false
    stop :: handler Abort {
        abort(code) => {
            failed = true
            break                       // leave the `with` below
        }
    }
    with stop {
        print("{}\n", total("123"))
        print("{}\n", total("12x"))
        print("not reached\n")
    }
    print("failed: {}\n", failed)
}
```

```output
cleanup
6
cleanup
failed: true
```

The second `cleanup` was printed while `Abort.abort` unwound `total`. The rule for choosing between this and a result: *if the nearest caller is the right one to decide, return `E!T`; if whoever set up the context is, perform an `esc` operation.* A parse error is the caller's business, so it is a result and costs a branch, like an error code. Aborting a whole request, exiting the process (`Env.exit`), failing a test and stopping an iteration are decided far up the stack, so they are `esc` operations.

A **trap** is different and is not an exception: `trap`, a failed bounds check and an overflow terminate the process immediately, do not unwind and do not run `defer`. They mean "this program has a bug".

The ambient effects (`Out`, `Err`, `In`, `Alloc`, `Clock`, `Rand`, `Fs`, `Net`, `Proc`, `Env`) have default handlers installed before `main`, so `main` can use them just by listing them. Handling them yourself, to capture output or fake the clock, is an ordinary `with`. See [chapter 7](07-effects.md).

## Iteration: iterators instead of visitors and state machines

C iterates arrays with an index and everything else by one of two awkward routes: a callback (`qsort`, `tree_walk(node, visit, ctx)`) or a struct holding the iterator's state, advanced by `next()` calls. Gamma has one mechanism. An iterator is a function whose effect set contains `Yield(T)`, and it produces values by calling `yield`; a `for` loop is the handler:

```gamma
words :: fn(s: string) needs Yield(string) {
    start := 0
    for i in 0..s.len {
        if s[i] is ' ' {
            if i > start { yield(s[start..i]) }
            start = i + 1
        }
    }
    if s.len > start { yield(s[start..]) }
}

main :: fn() needs Out {
    for w in words("the quick  brown fox") {
        if w is "brown" { break }
        print("[{}]\n", w)
    }
}
```

```output
[the]
[quick]
```

You write the loop you want, in the order you want, with its state in local variables; there is no `struct tokenizer`, no `void *`, and no inverted control flow. `break` out of the `for` exits the iterator and *runs its `defer` statements*, because `yield` is an `esc` operation. `for` allocates nothing, and when the iterator's body is visible (the standard ones are) it compiles to the counted loop you would have written by hand. The standard iterators are `range` (`0..n`), `each`, `enumerate`, `reversed`, `zip`, `bytes` and `runes`.

For walking two sequences in lockstep, where a `for` can only drive one, arrays and slices provide cursors with `while x := c.next()`. See [chapter 8](08-iteration.md).

## Compile time: no preprocessor

Gamma has no preprocessor, no macros and no separate `constexpr` sublanguage. The jobs of the preprocessor are split among ordinary features:

| Preprocessor use | Gamma |
| --- | --- |
| `#define N 64` | `N :: 64`, an untyped constant, re-typed at each use |
| `#define SQR(x) ((x)*(x))` | a function; `#inline` if you care |
| `#ifdef __linux__` | `when target.os is .Linux { ... }` |
| `#error "unsupported"` | `@compile_error("unsupported")` |
| `_Static_assert` | `@static_assert(cond, "message")` |
| lookup tables generated by a script | a function, called in a top-level initializer |
| X-macros over an enum | `@variants(E)`, `@variant_name(x)` |
| `#include` guards | not needed: modules |

```gamma
crc_table :: fn(poly: u32) -> [256]u32 {
    table: [256]u32 = ---
    n: u32 = 0
    while n < 256 {
        c := n
        k: u32 = 0
        while k < 8 {
            c = if c & 1 is 1 { (c >> 1) ~ poly } else { c >> 1 }
            k += 1
        }
        table[n] = c
        n += 1
    }
    return table
}

CRC_TABLE :: crc_table(0xEDB88320)        // computed by the compiler

crc32 :: fn(data: string) -> u32 {
    crc: u32 = 0xFFFFFFFF
    for b in bytes(data) {
        crc = (crc >> 8) ~ CRC_TABLE[(crc ~ u32(b)) & 0xFF]
    }
    return crc ~ 0xFFFFFFFF
}

when target.ptr_bits is 64 {
    Word :: u64
} else {
    Word :: u32
}

main :: fn() needs Out {
    print("{:08x} {}\n", crc32("123456789"), @size_of(Word))
}
```

```output
cbf43926 8
```

`crc_table` is an ordinary function with an ordinary loop. A call runs at compile time when its effect set is empty (no `Out`, no `Alloc`, no `Sys`) and its arguments are constants, so nothing marks it "constexpr", and the same function can be called at run time. The compile-time evaluator is held to the rules of the target: sizes, layout and float behavior are the target's, every check is performed, and a trap or detected undefined behavior is a *compile error*, so a compile-time bug is never a wrong constant. A runaway loop stops at a step budget (`#fuel(n)` raises it). `when` is the replacement for `#ifdef`: the condition is a compile-time `bool`, and the untaken branch is not even type-checked, so it may mention functions that exist only on another platform. `target.os`, `target.arch`, `target.endian`, `target.ptr_bits` and `target.mode` describe the build. See [chapter 11](11-compile-time.md).

## Safety and build modes

C leaves a long list of things undefined and compilers use that freedom. Gamma's undefined behavior is a short closed list (§18.4 of the [specification](../../gamma-spec.md#18-safety-checks-and-undefined-behavior)): use of an object outside its lifetime, access through a pointer that does not point to a compatible object, out-of-bounds many-pointer access, writing to read-only memory, freeing through the wrong allocator or twice, data races, violating the contract of a foreign function, and a few cases that exist only in Fast builds. **Arithmetic is not on the list**: overflow, shifts, reading `---` storage, and NaN all have defined results.

The one rule behind every check: it traps in Debug, Checked and Safe builds and is removed in Fast builds. Select the mode with `--mode`:

| Mode | Optimized | Checks | What it is for |
| --- | --- | --- | --- |
| `debug` (the default of the tool) | no | all | development: poisons uninitialized and freed memory (`0xAA`, `0xDD`), tags allocations, guard pages, leak-checking tests |
| `checked` | yes | all | what you ship by default |
| `safe` | yes | all, plus every memory access | untrusted input: use after free, double free, out-of-bounds many-pointer access all trap (built through the C back end with [Fil-C](https://fil-c.org)) |
| `fast` | yes | none (division still traps) | code you have tested under the others and measured |

```text
$ gamma run --mode=checked prog.gm
$ gamma build --mode=fast prog.gm
$ gamma test --mode=safe prog.gm
```

How does this compare with the C tools you use now?

- **Sanitizers.** The generated C is ordinary C, so `CFLAGS="-fsanitize=address,undefined" gamma run prog.gm` works (AddressSanitizer reports a heap use-after-free in a Gamma program the same way it does in C). Gamma's own arithmetic and bounds checks replace much of what UBSan is for, and Safe mode replaces much of what ASan is for, at a speed cost (typically 2 to 5 times slower than Checked).
- **`-fwrapv`, `-ftrapv`.** These are the `+%` and `+` operators, chosen per operation.
- **`NDEBUG`.** `assert(cond, msg)` is a checked operation that Fast removes, arguments and all, so never put work you need inside it. `expect` is for tests and is never removed.
- **Debuggers and profilers.** `gamma build -g` maps machine code to `.gm` lines and ships gdb and lldb pretty-printers.

What is still your job: lifetimes. Checked mode checks local facts (bounds, overflow, unwraps, cast alignment) but does not track whether a pointer is still valid. Test under Debug and, when memory safety matters more than speed, Safe. A trap is reported with a call chain and exits with status 134, the same as `abort()`:

```text
$ gamma run index.gm
trap: index 5 out of bounds for length 3
  at index.gm:4:19 in main
$ echo $?
134
```

See [chapter 13](13-safety.md).

## Calling C and being called from C

Gamma exists to coexist with C code, and the boundary is deliberately thin. [Chapter 14](14-interop.md) has the full story; this is the working subset.

**C-compatible types** may cross: the sized integers, `usize`/`isize`, `f32`/`f64`, `bool`, `^T`/`[^]T`/`rawptr`/`cstring` and `?` of each (nullable pointers), `^fn` types with C-compatible parts, structs of C-compatible fields (passed by value like C), `#c_union`, `#bits` structs and `()` (void). Not C-compatible: tagged unions, optionals of non-pointers, results, slices, tuples and arrays as parameters. Pass a pointer and a length instead. C's own types come from `std/c`: `c.c_int`, `c.c_uint`, `c.c_long`, `c.c_char`, `c.c_double`, and so on.

**Calling C.** Declare what you use in a `foreign` block; the result is a namespace. Calling foreign code is outside Gamma's model, so it needs `Sys`, unless you assert (with `needs not`) that the function touches nothing but its arguments:

```gamma
c :: import "std/c"

libc :: foreign "c" {
    strlen :: fn(s: cstring) -> usize needs not
    abs :: fn(n: c.c_int) -> c.c_int needs not
    qsort :: fn(base: rawptr, n, size: usize, cmp: ^fn(a, b: rawptr) -> c.c_int)
    snprintf :: fn(buf: [^]u8, size: usize, fmt: cstring, ...) -> c.c_int
}

compare_i32 :: fn(a, b: rawptr) -> c.c_int {
    x := a.cast(^const i32)^
    y := b.cast(^const i32)^
    if x < y { return -1 }
    if x > y { return 1 }
    return 0
}

main :: fn() needs Out + Sys {
    print("{} {}\n", libc.strlen(c"hello"), libc.abs(-42))
    nums := [_]i32(42, -7, 19, 0, 3)
    libc.qsort(&nums, nums.len, @size_of(i32), compare_i32)
    print("{}\n", nums)

    buf: [32]u8 = ---
    n: c.c_int = 7
    len := libc.snprintf(&buf, buf.len, c"%d items", n)
    print("{}\n", buf[0..(len.checked(usize) else 0)])
}
```

```output
5 42
[-7, 0, 3, 19, 42]
7 items
```

The pieces you know from C appear in a more explicit form. A `c"..."` literal is NUL-terminated; a `string` is not and never converts to a `cstring`. `&nums` becomes a `rawptr` implicitly, and coming back from `rawptr` needs the explicit `cast`. Variadic arguments must be C-compatible scalars, already promoted by you to at least `c_int` width and `f64`. Narrowing at the boundary is never implicit (`len.checked(usize)` above). A Gamma function with an empty effect set (or `Sys`) can be passed to C as a callback. `gamma run` calls such functions itself without a C compiler; for inline assembly (`@asm`) and the few calls it cannot make, it compiles instead. A package can bring its own C sources, headers and libraries through `gamma.toml` (`c-sources`, `include-dirs`, `libs`, `lib-dirs`), honored by every build mode ([chapter 15](15-packages.md#build-settings)). `examples/c_interop.gm` is a longer example.

**Being called from C.** `#export("symbol")` gives a function external linkage under that name with the C ABI. Its signature must be C-compatible, and its effect set must be empty or `Sys`, because a C caller cannot supply handlers:

```gamma
#export("gm_checksum")
checksum :: fn(data: [^]const u8, len: usize) -> u32 {
    sum: u32 = 0
    for b in data[0..len] { sum = sum +% u32(b) }
    return sum
}

main :: fn() needs Out {
    msg := "gamma"
    print("{}\n", checksum(msg.ptr, msg.len))
}
```

```output
515
```

On the C side that is `uint32_t gm_checksum(const uint8_t *data, size_t len);`. `gamma build` writes a native executable, with the generated C next to it; a program with no `main` for a freestanding target becomes an object file with the exported functions, to be linked by your own toolchain ([chapter 14](14-interop.md#freestanding-programs)).

**Layout control** is what you would do with attributes and compiler extensions in C:

```gamma
c :: import "std/c"

WireHeader :: #packed struct {          // __attribute__((packed))
    tag: u8
    length: u32
}

Page :: #align(4096) struct {           // _Alignas(4096)
    bytes: [4096]u8
}

Mode :: #bits(u16) struct {             // a bit-field with defined layout
    read, write, exec: bool
    kind: u4
    _: u9
}

Word :: #c_union union {                // a real, untagged union
    as_u32: u32
    as_f32: f32
}

main :: fn() needs Out {
    m := Mode(read: true, write: true, exec: false, kind: 3)
    w := Word(as_f32: 1.0)
    print("{} {} {} {}\n", @size_of(WireHeader), @align_of(Page), u16(m), w.as_u32)
}
```

```output
5 4096 27 1065353216
```

Atomics follow the C11 memory model, so ThreadSanitizer and your existing knowledge apply: `@atomic_add(p, 1, .SeqCst)`, `@atomic_cas(p, expected, new, .Acquire, .Relaxed)` (which returns `null` on success and the observed value on failure) and `@fence(order)`. Volatile access (`@volatile_load`, `@volatile_store`) and inline assembly (`@asm`) perform `Sys`.

## Modules and packages instead of headers and Makefiles

A header file is two things: a promise to other translation units and a place to dump the textual declarations they need. Gamma has no textual inclusion. A **module** is a directory: every `.gm` file in it shares one namespace, order does not matter, and nothing is declared twice. What other modules can use is what is marked `pub`:

```text
myapp/
    gamma.toml          package name and dependencies
    main.gm             main :: fn() ...
    geom/
        shapes.gm       pub Shape, pub area
        internal.gm     private helpers: not visible outside geom
```

```gamma fragment
geom :: import "./geom"             // a path starting with ./ is relative to this file

total :: fn(s: []const geom.Shape) -> f32 {
    sum: f32 = 0
    for x in s { sum += geom.area(x) }
    return sum
}
```

An `import` is only legal as the initializer of a top-level `::`, the name is chosen by the importer, the import graph must be acyclic, and private names (including struct fields not marked `pub`) are invisible outside their module. There are no include guards, no declaration/definition split, no link-order bugs, and no ODR violations. A package is a directory tree with a `gamma.toml`; `gamma new`, `add`, `fetch`, `update` and a lockfile replace the hand-written Makefile and the vendored copies of libraries. The standard library is imported as `std/...` (`std/arena`, `std/str`, `std/fs`, `std/json`, ...). See [chapter 15](15-packages.md).

The standard library is split in two tiers: the core (everything predeclared: `print`, `list`, `new`, `make`, slices' methods) and the `std/` modules. Nothing in it is a language feature: a `List` is a struct and some functions. [Chapter 16](16-standard-library.md) tours it.

## Testing

C tests are a separate program with a framework you picked, a Makefile target and global state to reset. In Gamma a `test` declaration lives next to the code, `gamma test` builds and runs them, and each test gets its own handlers: output is captured, the clock and the file system are fake, the allocator checks for leaks, and a failure runs the test's `defer` statements.

```gamma test
ParseError :: union { Empty, BadDigit: usize, TooBig }

parse_port :: fn(s: string) -> ParseError!u16 {
    if s.len is 0 { return !.Empty }
    n: u16 = 0
    for i, c in enumerate(s) {
        if c not in '0'..='9' { return !.BadDigit(i) }
        n = n *? 10 +? u16(c - '0') else return !.TooBig
    }
    return n
}

test "parse accepts digits" {
    expect_eq(ok(parse_port("443")), 443)
}

test "parse reports where it went wrong" {
    expect(parse_port("12a") is !.BadDigit(2))
    expect(parse_port("") is !.Empty)
    expect(parse_port("70000") is !.TooBig)
}

test "no leaks" {
    names := list(string)
    defer names.deinit()
    ok(names.push("ada"))
    expect_eq(names.items.len, 1)
}
```

`expect(cond)`, `expect_eq(actual, expected)` and `ok(result)` (which returns the success value or fails the test with the error) are ordinary functions that perform the `Test` effect, so a helper that checks things declares `needs Test` and the signature says so. Because tests replace `Out`, `Fs` and `Clock`, they are deterministic without mocking code of your own. `gamma bench` runs `bench` declarations the same way. See [chapter 13](13-safety.md#tests).

## Habits to unlearn

Each of these C habits is a compile error in Gamma, and the message says what to write instead. (Several more, such as `==`, mixed integer widths, casts and lossy conversions, appear earlier on this page.)

**Assuming an integer is a condition:**

```gamma error
nonzero :: fn(n: u32) -> u32 {
    if n {                  // error: the condition must be a `bool`
        return 1
    }
    return 0
}
```

**Writing `x = y` where a condition is expected:**

```gamma error
check :: fn(x: i32) -> bool {
    if x = 0 {              // error: assignment is a statement, not an expression
        return true
    }
    return false
}
```

**Dropping a return value:**

```gamma error
parse :: fn(s: string) -> ?u32 => null

main :: fn() {
    parse("1")              // error: the value is unused; write `_ = ...` to discard it
}
```

**Forgetting that a precedence mixture is ambiguous:**

```gamma error
mask :: fn(n: u32) -> u32 {
    return 1 << n - 1       // error: shift mixed with arithmetic; parenthesize
}
```

**Using a nullable pointer without testing it:**

```gamma error
Node :: struct {
    value: i32
    next: ?^Node
}

second :: fn(n: ^Node) -> i32 {
    return n.next.value     // error: `next` is a `?^Node`, which has no field `value`; test it first
}
```

**Falling through, or forgetting a case:**

```gamma error
Dir :: union { N, E, S, W }

name :: fn(d: Dir) -> string => match d {   // error: not exhaustive: missing .W
    .N => "north"
    .E => "east"
    .S => "south"
}
```

**Returning a pointer to a local:**

```gamma error
make :: fn() -> ^i32 {
    x: i32 = 5
    return &x               // error: returns the address of a local variable
}
```

**Calling something with an effect you did not declare:**

```gamma error
helper :: fn() {
    print("hi\n")           // error: performs Out, which `helper` neither lists nor handles
}
```

**Passing a `string` to a function that wants a `cstring`:**

```gamma error
c :: import "std/c"

libc :: foreign "c" {
    puts :: fn(s: cstring) -> c.c_int
}

main :: fn() needs Sys {
    _ = libc.puts("hello")  // error: a string is not a cstring; use c"hello"
}
```

**Positional struct construction:**

```gamma error
Vec2 :: struct { x, y: f32 }

origin :: fn() -> Vec2 {
    return Vec2(0, 0)       // error: struct construction takes field names
}
```

## Where to go next

- The [guide](README.md) chapter by chapter: [basics](02-basics.md), [control flow](03-control-flow.md), [types](04-types.md), [optionals and errors](05-optionals-and-errors.md), [effects](07-effects.md), [memory](09-memory.md), [safety](13-safety.md), [interop](14-interop.md).
- The `examples/` directory: `c_interop.gm` (libc, callbacks, bit structs), `allocators.gm` (a tracking allocator, arenas, fixed buffers), `grep.gm` and `wordfreq.gm` (small command-line tools), `sha256sum.gm` and `mandelbrot.gm` (numeric code).
- The [specification](../../gamma-spec.md), whose [Appendix C](../../gamma-spec.md#appendix-c-design-rationale) compares what a C function depends on with what a Gamma signature says (C.1) and lists the alternatives Gamma considered and rejected, with the principle behind each decision (C.8).
