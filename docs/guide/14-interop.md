# 14. Interoperating with C

[Contents](README.md) · [Previous: Safety](13-safety.md)

Gamma is meant to replace C, but nobody rewrites a whole ecosystem in a day. This chapter covers the bridge: calling C libraries, being called from C, laying out data the way C expects, and the low-level tools (global state, atomics, volatile access) that systems code needs. Everything here that steps outside Gamma's checked model is marked by the `Sys` effect, so a signature tells you where the boundary is.

## C-compatible types

A type can appear in a foreign signature or an exported function only if it is *C-compatible*:

| Gamma | C |
| --- | --- |
| `u8 u16 u32 u64`, `i8 i16 i32 i64` | `uint8_t` ... `int64_t` |
| `u128`, `i128` | `unsigned __int128`, `__int128` (where the C ABI has them) |
| `usize`, `isize` | `size_t`, `ptrdiff_t` |
| `f32`, `f64` | `float`, `double` |
| `bool` | `_Bool` |
| `^T`, `^const T`, `[^]T`, `[^]const T`, `rawptr`, `cstring`, and `?` of each | `T *`, `const T *`, `void *`, `const char *` (nullable only when `?`) |
| `^fn(...) -> R` with C-compatible parts, effect set empty or `{Sys}` | function pointer |
| `[N]T` as a struct field | `T[N]` |
| `struct` of C-compatible fields (including `#packed`, `#align`) | `struct` with the same layout |
| `#c_union` of C-compatible fields | `union` |
| `#bits(U)` struct | `U` |
| `()` as a return type | `void` |

Not C-compatible: tagged unions, optionals of non-pointers, results, slices, tuples, arrays as parameters, second-class functions and continuations. Their layouts are Gamma's business, and putting them in a foreign signature would make a promise to C that Gamma does not keep. C's platform-dependent types (`int`, `long`, `size_t`, ...) are provided as aliases by the module `std/c`: `c_int`, `c_uint`, `c_long`, `c_char`, and so on. This chapter imports it as `c`:

```gamma
c :: import "std/c"
```

Struct layout is C's: fields in declaration order, each aligned to its own alignment, the size rounded up to the struct's alignment. So a struct with C-compatible fields has the same layout as the corresponding C struct, and `?^T` is a nullable C pointer. Such structs can also be passed and returned by value, exactly as C passes the corresponding struct:

```gamma
DivT :: struct {
    quot: c.c_int
    rem: c.c_int
}

libc :: foreign "c" {
    div :: fn(n, d: c.c_int) -> DivT \ {}
}

div_demo :: fn() -> c.c_int {
    d := libc.div(17, 5)      // d.quot is 3, d.rem is 2
    return d.quot + d.rem
}
```

## Calling C: `foreign`

A `foreign` block declares functions provided by a library. The block is bound to a name like a module, and you call its members through that name:

```gamma
libc :: foreign "c" {
    puts   :: fn(s: cstring) -> c.c_int
    printf :: fn(fmt: cstring, ...) -> c.c_int
    strlen :: fn(s: cstring) -> usize \ {}          // trusted: performs no effects
    qsort  :: fn(base: rawptr, n, size: usize, cmp: ^fn(a, b: rawptr) -> c.c_int)
}
```

- `"c"` is the C standard library; other names are passed to the linker. `#link_name("sym")` on a member overrides the symbol name. `#callconv` selects another calling convention (see [Calling conventions](#calling-conventions-callconv)).
- Parameter and result types must be C-compatible. A `cstring` is a `[^]const u8`; a `c"..."` literal makes one. A regular `string` is a slice and is not NUL-terminated, so it never converts to a `cstring` by accident.
- **A foreign function's effect set is `{Sys}`** unless you annotate it. An annotation with a smaller set, like `\ {}` on `strlen`, is a *trusted assertion* by you that the C function touches nothing but its arguments. The compiler does not verify it. Foreign functions are never evaluated at compile time regardless.
- Variadic functions end in `...`. Arguments in the variadic position must be C-compatible scalars of at least `c_int` width for integers and `f64` for floats, and C's default argument promotions are not applied for you: convert explicitly.

```gamma
main :: fn() \ {Sys} {
    _ = libc.puts(c"hello from C")          // the result must be dealt with: here, discarded
    n: c.c_int = 42
    _ = libc.printf(c"%d\n", n)
}
```

`main` lists `Sys` because it calls foreign code. Note that `puts` writes to C's standard output directly, not through Gamma's `Out` effect, so a handler for `Out` does not see it. That is the sense in which `Sys` marks the edge of the model.

Narrowing at the boundary is never implicit. A Gamma `usize` length does not become a C `unsigned int` without you saying how:

```gamma
c_len :: fn(n: usize) -> c.c_uint {
    return n.checked(c.c_uint) else trap("length does not fit")
}
```

(`T(x)` checks the *type's* range, never a proven value range, so whether a conversion is legal never depends on how clever the compiler's analysis is. Use `trunc`, `checked` or `sat`, as in [chapter 4](04-types.md).)

### Callbacks

A function pointer can be passed to C when its signature is C-compatible and its effect set is empty or `{Sys}`. Here C's `qsort` calls back into a Gamma comparator:

```gamma
cmp_i32 :: fn(a, b: rawptr) -> c.c_int {
    x := a.cast(^const i32)^                // rawptr -> ^const i32, then dereference
    y := b.cast(^const i32)^
    if x < y { return -1 }
    if x > y { return 1 }
    return 0
}

sort_with_c :: fn() \ {Sys} {
    nums := [_]i32(5, 3, 9, 1)
    libc.qsort(&nums, nums.len, @size_of(i32), cmp_i32)
}
```

`&nums` converts to `rawptr` implicitly (any pointer does), and the named function `cmp_i32` converts to the required `^fn` type. Going back from `rawptr` to a typed pointer needs the explicit `cast`, which checks alignment. Gamma's own `sort()` and `sort_by()` do this without any of the casts, so prefer them in Gamma code.

### Strings from C

To use a `cstring` as a Gamma string, slice it with both bounds. A many-pointer slice needs both:

```gamma
to_string :: fn(s: cstring) -> string => s[0..s.cstr_len()]
```

The result shares memory with the C string; nothing is copied. `print` shows a `cstring` as an address, so convert it first.

## Being called from C: `#export`

`#export("sym")` gives a top-level function external linkage under the name `sym`, using the C ABI. Its signature must be C-compatible and its effect set must be empty or `{Sys}`, because C callers cannot supply handlers:

```gamma
#export("gm_checksum")
checksum :: fn(data: [^]const u8, len: usize) -> u32 {
    sum: u32 = 0
    for i in 0..len {
        sum = sum +% u32(data[i])       // unchecked many-pointer indexing: C vouches for len
    }
    return sum
}
```

On the C side this is `uint32_t gm_checksum(const uint8_t *data, size_t len);`. An exported function that needs effects installs its own handlers internally, using `Sys`-level constructors from the library (the specification's example uses `heap_allocator()` and `real_fs()` for this). A trap inside an exported function terminates the process; it does not return into C.

## Calling conventions: `#callconv`

Every function uses the target's C ABI unless told otherwise. `#callconv(.X)` picks a different convention:

- `.C` is the target's C ABI. It is the default, so writing it only documents the intent.
- `.SysV` and `.Win64` name the System V and Microsoft x64 ABIs. They exist only on x86-64 (`target.arch is .X86_64`); on any other architecture they are errors. The C ABI of the supported x86-64 targets is System V, so `.SysV` means the same as `.C` there, and `.Win64` is the one that differs.

The attribute goes on a top-level function, on a foreign member, or on a `^fn` type:

```gamma
// Gamma code that a Windows-ABI caller (a plugin host, say) calls back into.
#export("plugin_add")
#callconv(.Win64)
plugin_add :: fn(a: c.c_int, b: c.c_int) -> c.c_int => a + b

// A C function that uses the Microsoft x64 ABI.
host :: foreign "c" {
    #callconv(.Win64)
    host_log :: fn(msg: cstring)
}

// A callback of that convention.
Callback :: #callconv(.Win64) ^fn(code: c.c_int) -> c.c_int

register :: fn(cb: Callback) -> c.c_int => cb(0)
```

The convention is part of a function pointer's type. `plugin_add` used as a value has the type `#callconv(.Win64) ^fn(a: c.c_int, b: c.c_int) -> c.c_int`, and it does not convert to a plain `^fn` of the same signature or the other way round: calling through the wrong convention would corrupt the call, so the compiler refuses the assignment instead. For the same reason such a function can never become a second-class `fn` value, and `@fiber` and thread entry points accept only `.C` pointers.

A function with another convention must be an ordinary C-style function: top-level, non-generic, capturing nothing, with an effect set that is empty or `{Sys}`. Within Gamma, calls behave as always; the convention only changes how arguments and results travel across the machine boundary.

## Layout control

Attributes adjust struct layout. They are written directly before the `struct` keyword:

```gamma
WireHeader :: #packed struct {          // no padding, alignment 1
    tag: u8
    length: u32
}

Page :: #align(4096) struct {           // raise the alignment
    bytes: [4096]u8
}

@static_assert(@size_of(WireHeader) is 5, "the wire header is five bytes")
@static_assert(@align_of(Page) is 4096, "Page must be page-aligned")
```

`@size_of`, `@align_of` and `@offset_of(S, field)` are compile-time constants, so `@static_assert` can pin a layout down ([chapter 11](11-compile-time.md)).

### Bit fields: `#bits`

C bit-fields have implementation-defined layout. Gamma's `#bits(U)` struct packs its fields into the unsigned integer type `U`, starting at the least significant bit, in declaration order, with exactly specified layout:

```gamma
Mode :: #bits(u16) struct {
    read, write, exec: bool             // 1 bit each
    kind: u4
    _: u9                               // explicit padding (zero when constructed)
}

mode_demo :: fn() -> u16 {
    m := Mode(read: true, write: true, exec: false, kind: 3)
    return u16(m)                       // 27: bits 0 and 1, and 3 in bits 3 to 6
}
```

The field widths must add up to the width of `U`. The struct has the size and alignment of `U`, and converting to and from `U` is plain `U(m)` and `Mode(u)` when every field is a `bool` or integer, so there are no casts. Bitwise operators work on `#bits` structs fieldwise.

### Untagged unions and reinterpretation

`#c_union union { ... }` is C's `union`: all fields at offset 0, no tag, and no `match`. It is built with exactly one named field, and reading another field reinterprets the bytes:

```gamma
Word :: #c_union union {
    as_u32: u32
    as_f32: f32
}

float_bits :: fn(x: f32) -> u32 {
    w := Word(as_f32: x)
    return w.as_u32
}
```

When you only need to reinterpret a value, the safer spelling is the `bits` method, which requires plain-data types of the same size and can never create an invalid value: `x.bits(u32)`. Reading a `#c_union` field whose type is not plain data is undefined behavior if the bytes are not a valid value of that type.

## Global variables

Top-level bindings are immutable. Mutable static storage exists only through `#global`, which exists mainly for C interop:

```gamma
#global request_count: u32 = 0

next_request :: fn() -> u32 \ {Sys} {
    request_count += 1
    return request_count
}
```

The initializer must be a compile-time constant, and **every read or write of a `#global` performs `Sys`**. That keeps functions without `Sys` free of hidden state, which is what makes compile-time evaluation and test handlers sound. Add `#thread_local` for one instance per thread. A `#global` declaration inside a `foreign` block declares a variable defined by foreign code, with no initializer.

## Threads and atomics

Gamma adopts the **C11 memory model**: existing knowledge and tools such as ThreadSanitizer apply unchanged. Threads come from the library (`std/thread`), which requires `Sys`. A thread starts from a function pointer taking a `rawptr` and begins with no handlers installed, so it installs its own. Handlers, second-class values and continuations cannot cross threads, because only function pointers and data can be passed. A **data race** (two accesses to the same location from different threads, at least one a write and one non-atomic, not ordered by happens-before) is undefined behavior.

Atomics are intrinsics that take a pointer and an explicit memory order: `.Relaxed`, `.Acquire`, `.Release`, `.AcqRel` or `.SeqCst`.

| Intrinsic | Result |
| --- | --- |
| `@atomic_load(p, order)` | the value |
| `@atomic_store(p, v, order)` | `()` |
| `@atomic_swap(p, v, order)` | the old value |
| `@atomic_cas(p, expected, new, success, failure)` | `?T`: `null` on success, the observed value on failure |
| `@atomic_add(p, v, order)`, `_sub`, `_and`, `_or`, `_xor`, `_max`, `_min` | the old value (wrapping arithmetic) |
| `@fence(order)` | `()` |

Atomics do not require `Sys`; they are memory operations on memory the function can already reach. A counter and a spin lock:

```gamma
increment :: fn(counter: ^u32) -> u32 {
    return @atomic_add(counter, 1, .SeqCst)         // returns the previous value
}

lock :: fn(flag: ^u32) {
    while true {
        if @atomic_cas(flag, 0, 1, .Acquire, .Relaxed) is null { return }
    }
}

unlock :: fn(flag: ^u32) {
    @atomic_store(flag, 0, .Release)
}
```

Note that `@atomic_cas` reports *success* as `null` and failure as the value it saw, the opposite of what a "found" optional suggests; the type is `?T` so that the observed value is only available when the exchange failed.

## Volatile access and assembly

Memory-mapped I/O needs loads and stores that the compiler may not elide, merge or reorder. `@volatile_load(p)` and `@volatile_store(p, v)` do that; `p` points to an integer, float, pointer or `#bits` type. Both perform `Sys`. Getting a pointer from a hardware address is `n.to_ptr(P)`, which gives a `?P` that is `null` for address 0:

```gamma
UART_BASE: usize : 0x1000_0000

uart_ready :: fn() -> bool \ {Sys} {
    status := UART_BASE.to_ptr(^const u32) else return false
    return @volatile_load(status) & 1 is 1
}

uart_write :: fn(byte: u8) \ {Sys} {
    data := (UART_BASE + 4).to_ptr(^u32) else return
    @volatile_store(data, u32(byte))
}
```

`@asm(template, outputs, inputs, clobbers)` provides inline assembly in the syntax of the target's GCC-compatible assembler, with GNU C's operand constraints. Outputs are pairs of a constraint and a pointer to the place written; inputs are pairs of a constraint and a value; clobbers are strings. Each list is `()`, a single entry, or a tuple of entries:

```gamma
add :: fn(a, b: u32) -> u32 \ {Sys} {
    r := a
    @asm("addl %1, %0", ("+r", &r), ("r", b), "cc")   // x86-64
    return r
}

barrier :: fn() \ {Sys} => @asm("", (), (), "memory")
```

It needs `Sys`, misusing it is undefined behavior, and it is never evaluated at compile time. The interpreter cannot run assembly, so `gamma run` compiles programs that use it, as it does for programs with foreign functions. Use `when target.arch is …` to provide each architecture's version.

## Where the safety net ends

Everything in this chapter that crosses into C or bypasses the checks puts a contract on you: violating a foreign function's contract, or the foreign function's own undefined behavior, is undefined behavior in your program. The structure of the language keeps that confined and visible: `Sys` appears in the signature of every function that reaches outside the model, C-compatible types are an explicit subset, and the checked world sits in between.

> **Common mistakes.**
>
> - **Forgetting `Sys`** on a function that calls a foreign function or touches a `#global`.
> - **Passing a Gamma `string` as a `cstring`.** Use a `c"..."` literal, or a buffer you have NUL-terminated.
> - **Implicit narrowing** (`usize` to `c_uint`). Use `.checked(...)`, `.trunc(...)` or `.sat(...)`.
> - **Putting a slice, tuple, optional or result in a foreign signature.** Pass a `[^]T` pointer and a `usize` length instead.
> - **Variadic arguments of the wrong width.** Convert integers to at least `c_int` width and floats to `f64` yourself.
> - **Annotating a foreign function `\ {}` when it is not pure.** The compiler trusts you.

## Summary

- Foreign functions are declared in a `foreign` block and need `Sys` unless you assert otherwise.
- Only C-compatible types may cross; `std/c` provides `c_int` and friends; `?^T` is a nullable C pointer.
- `#export` exposes a Gamma function with the C ABI; its effects must be empty or `{Sys}`.
- `#callconv(.Win64)` (x86-64 only) selects another ABI for a function, a foreign member or a `^fn` type; it is part of the pointer type.
- `#packed`, `#align(n)`, `#bits(U)` and `#c_union` control layout; `x.bits(T)` reinterprets plain data.
- `#global` is mutable static storage and costs `Sys`; atomics follow C11 and do not.
- Volatile loads and stores are for hardware registers.

You have now seen the whole language. The [specification](../../gamma-spec.md) is the place to look for exact rules, and its Appendix C explains the reasoning behind the design.

For the full rules, see [§19 Interoperability and low-level facilities][s19] and [§5.10-5.11 on layout and unions][s5] in the specification.

[s5]: ../../gamma-spec.md#5-types
[s19]: ../../gamma-spec.md#19-interoperability-and-low-level-facilities
