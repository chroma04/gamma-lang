# 9. Memory

[Contents](README.md) · [Previous: Iteration](08-iteration.md) · [Next: Generics](10-generics.md)

Gamma manages memory the way C does: you decide where objects live and when they die. What changes is that allocation is explicit, checked in function signatures, and replaceable by the caller. There is no garbage collector, no ownership system and no destructor magic, and also no `malloc` that any function can call without telling you.

## Where objects live

| Storage | Created by | Lives until |
| --- | --- | --- |
| static | string literals, top-level constants, `#global` | program exit |
| automatic | locals, parameters, temporaries | the end of the enclosing block |
| fiber stack | locals of code running on a fiber ([chapter 12](12-fibers.md)) | as automatic |
| allocated | an allocator | freed through the same allocator |

Automatic storage works exactly as in C, and so does the danger: a pointer to a local is invalid once the block ends. Gamma does not track lifetimes in general, but it rejects the direct cases at compile time: returning `&x`, `x[..]` or a slice of a local or parameter of the function being returned from.

```gamma
bad_slice :: fn() -> []u8 {
    local: [16]u8 = ---
    return local[..]          // error: returns a slice of a local variable
}
```

Using a dangling pointer is one of the few sources of undefined behavior ([chapter 13](13-safety.md)); Debug builds poison dead stack frames and freed memory to catch most such uses.

Many programs need no heap at all. A fixed buffer on the stack is an ordinary array, and a slice of it is an ordinary slice:

```gamma
format_into :: fn(out: []u8) -> usize {
    msg :: "ok"
    n := msg.len
    if n > out.len { n = out.len }
    out[0..n].copy_from(msg[0..n])
    return n
}

stack_demo :: fn() -> usize {
    scratch: [64]u8 = ---
    return format_into(scratch[..])
}
```

## Allocators and the `Alloc` effect

An `Allocator` is a first-class handle: a pointer to some state plus a table of three functions (allocate, resize, free). The **`Alloc` effect** supplies the *current* allocator. Its one operation is `current: fn() -> Allocator`. The ambient default for `Alloc` is the system heap, so `main` can simply list `Alloc` and allocate; tests get a heap that reports leaks.

The core functions that allocate perform `Alloc`, which is why you see `\ {Alloc}` in signatures:

| Function | Meaning |
| --- | --- |
| `new(T, init)` | allocate one `T` holding `init`, giving `AllocError!^T` |
| `make(T, n)` | allocate `n` uninitialized `T` (plain-data `T` only), giving `AllocError![]T` |
| `make_fill(T, n, x)` | allocate `n` copies of `x`, for any `T` |
| `make_aligned(T, n, align)` | like `make`, with a larger alignment |
| `free(s)` | release a slice from `make` or `make_fill` |
| `free_aligned(s, align)` | release a slice from `make_aligned`, with the same `align` |
| `destroy(p)` | release a pointer from `new` |

Allocation can fail, so each of these returns a result and you handle it the usual way, with `try` in a function that returns an `AllocError!...`:

```gamma
Point :: struct { x, y: i32 }

heap_demo :: fn() -> AllocError!() \ {Alloc, Out} {
    p := try new(Point, Point(x: 1, y: 2))
    defer destroy(p)                    // release on every path out
    p.x += 10

    buf := try make(u8, 256)            // uninitialized; fine for plain data
    defer free(buf)
    buf.fill(0)

    print("{} {}\n", (p.x, buf.len))    // 11 256
    return ()
}
```

A few details:

- `new(T, ---)` leaves the object uninitialized, and `make` returns uninitialized storage (Debug builds fill it with `0xAA`), so both are restricted to plain-data types. For anything else, such as `bool` or pointers, use `make_fill`: `flags := try make_fill(bool, 8, false)`.
- `free` and `destroy` must receive exactly what the allocator returned. Freeing an empty slice does nothing.
- If the allocation fails, the `defer` after it is never reached, so you never free what you did not get. When a function allocates something to *return* to its caller, use `errdefer` instead of `defer`, so the memory is released only if a later step fails ([chapter 3](03-control-flow.md)).

The `defer` right after acquisition is Gamma's standard replacement for the `goto cleanup` ladder. Nothing runs implicitly: a value that is not freed stays allocated.

> **Common mistake.** Gamma has no move semantics or destructors, so values copy bitwise like in C. Copying a struct that holds a pointer copies the pointer; freeing through both copies is a double free, which is undefined behavior (Debug builds detect it).
>
> ```gamma
> double_free :: fn() -> AllocError!() \ {Alloc} {
>     a := list(u8)
>     b := a                  // b shares a's storage
>     a.deinit()
>     b.deinit()              // undefined behavior: freed twice
>     return ()
> }
> ```

## Swapping the allocator

Because `Alloc` is an effect, the caller decides where memory comes from. `Allocator` is itself a handler for `Alloc` (the core library declares `handler Alloc for Allocator`), so any `Allocator` value can be used in a `with`:

```gamma
make_in :: fn(a: Allocator, n: usize) -> AllocError![]u8 {
    return with a { make(u8, n) }       // allocated from `a`, whatever the context is
}
```

`make_in` declares no `Alloc` effect: it handles it itself with the allocator it was given. This is how arenas and fixed-buffer allocators work: some library provides a value of type `Allocator` (for example `arena.allocator()` as the specification's examples write it; the core library itself defines no arena type), and you install it for a region of code:

```gamma
scratch_demo :: fn(arena: Allocator) -> AllocError!() \ {Alloc} {
    names := list(string)               // captures the current allocator: the heap
    defer names.deinit()
    with arena {
        tmp := try make(u8, 256)        // from the arena
        tmp.fill(0)
        try names.push("ada")           // still grows on the heap
    }
    return ()
}
```

`new`, `make` and `free` always use whatever allocator is current at the call, which suits memory that dies inside the `with` that provided it. **Long-lived structures capture their allocator when they are constructed** and use that same handle for every later operation. `names` above was created outside the `with arena`, so `push` keeps growing it on the heap even inside the block. That is the design that prevents the C bug of freeing memory through a different allocator than the one that produced it. Debug builds additionally tag every allocation with its allocator and trap on a mismatch.

A function without `Alloc` in its effect set does not allocate through the current allocator, though it may allocate through an `Allocator` value it was handed. So `\ {Alloc}` in a signature is a reliable "this function may allocate" marker.

## `List` and `HashMap`

The core library provides two containers, both capturing the current allocator at construction and never performing `Alloc` afterwards. Neither is magical: they are structs with ordinary functions, called with method syntax.

```gamma
list_demo :: fn() -> AllocError!() \ {Alloc, Out} {
    names := list(string)               // constructed with the current allocator
    defer names.deinit()
    try names.push("ada")               // push can fail to allocate
    try names.push("grace")
    for name in names.items {           // `items` is the slice of elements
        print("{}\n", name)
    }
    if last := names.pop() {            // pop returns ?T
        print("popped {}\n", last)
    }
    return ()
}

map_demo :: fn() -> AllocError!() \ {Alloc, Out} {
    ages := map(string, u32)
    defer ages.deinit()
    try ages.put("ada", 36)
    try ages.put("grace", 45)
    if age := ages.get("ada") {         // get returns ?V
        print("ada is {}\n", age)
    }
    _ = ages.remove("grace")            // remove returns the old value, if any
    return ()
}
```

The element and key types are written explicitly where the compiler cannot infer them (`list(string)`, `map(string, u32)`). `List(T)` has a public `items` slice (so `items.len` is the length) and private bookkeeping fields. A map hashes keys with the language's structural `@hash` and compares them with `is`; the key type must support `is`. A custom hash and equality can be supplied as parameters of the type with `map_with`; see [chapter 10](10-generics.md).

Besides `put`, `get` and `remove`, a map has `contains(k)`, `len()`, `clear()` (which keeps the storage) and `entries()`, an iterator over `(key, value)` pairs in an unspecified order: `for e in ages.entries() { print("{} {}\n", (e.key, e.value)) }`.

Because maps compare with `is`, a float key never finds a NaN, as `nan is nan` is false. Key such maps by `x.bits(u64)` instead.

## Nothing allocates behind your back

No language construct allocates heap memory. Closures capture by reference, so there is no environment to allocate. Continuations live on stacks that you provide. A `for` loop allocates nothing. String literals are static. Only a call to a function that performs `Alloc`, or one that is handed an `Allocator`, allocates. This is the property that lets you read a signature and know the cost.

It also makes leak checking practical. Tests run with a heap that notices leaks, so a missing `deinit` becomes a test failure:

```gamma
test "list of names" {
    names := list(string)
    defer names.deinit()
    try names.push("ada")
    expect(names.items.len is 1)
}
```

Remove the `defer` line and the test fails with a leak report.

## Alignment and layout

Every object is aligned to at least its type's alignment, and allocators must honor any alignment up to the page size that you ask for: `make_aligned(T, n, align)`. `@size_of(T)` and `@align_of(T)` are compile-time intrinsics. Struct layout follows the C rules, so a struct allocated with `new` has exactly the layout C code would expect ([chapter 14](14-interop.md)).

## What can still go wrong

Gamma removes C's *implicit* memory hazards but not all of them. These are undefined behavior (§18.4):

- using memory after it was freed, or after its stack frame ended;
- freeing through the wrong allocator, or twice;
- out-of-allocation pointer arithmetic or a many-pointer access out of bounds;
- a data race.

Checked builds check local facts (array bounds, overflow, unwraps) but do not track lifetimes; Debug builds poison freed memory and tag allocations, which catches a lot but costs time. See [chapter 13](13-safety.md).

## Summary

- Memory comes from static data, the stack, or an allocator; allocating functions say `\ {Alloc}`.
- Pair every acquisition with a `defer` (or `errdefer`) on the next line.
- The caller can swap the allocator with `with`; long-lived containers remember the allocator they were created with.
- No construct allocates implicitly, and there is no ownership tracking, so double frees and use-after-free remain your responsibility (with Debug builds and tests to help).

Next, [chapter 10](10-generics.md) covers generic code.

For the full rules, see [§15 Memory and allocation][s15] and §21.2 and §21.5 of [§21 The core library][s21] in the specification.

[s15]: ../../gamma-spec.md#15-memory-and-allocation
[s21]: ../../gamma-spec.md#21-the-core-library
