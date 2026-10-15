# 13. Safety, checks and undefined behavior

[Contents](README.md) · [Previous: Fibers](12-fibers.md) · [Next: Interoperating with C](14-interop.md)

C leaves a long list of things undefined: signed overflow, out-of-range shifts, reading uninitialized integers, indexing past the end of an array, null dereference. Compilers use that freedom to delete your safety checks. Gamma's goals are to be *sensible*, then *fast*, and the safety design follows from that order: operations have meaning, checks have a single uniform rule, and undefined behavior is confined to a short, closed list that is hard to hit by accident.

## Build modes

A conforming implementation provides three build modes, and may provide a fourth, Safe. They differ only in what happens when a check fails and in diagnostics extras; a program that triggers no failing check and no undefined behavior behaves the same in all of them.

| Mode | Optimized | Checks | Extras |
| --- | --- | --- | --- |
| **Debug** | no | all | poisoned memory, allocation tags, fiber guard pages, leak-checking tests |
| **Checked** (the release default) | yes | all | none |
| **Safe** (optional) | yes | all, and every memory access | memory errors trap ([below](#safe-builds)) |
| **Fast** | yes | none, except integer division | none |

The one rule behind every check (§18.1): **it traps in Debug, Checked and Safe builds and is removed in Fast builds.** There is no syntax to disable a check locally; the way to remove checks is to choose Fast for the whole build. Implementations also remove checks they can prove cannot fail, in every mode; the design expects loops such as `for i in 0..xs.len { xs[i] }` and `for x in xs` to compile to check-free code in Checked builds (Appendix D.7 of the specification).

Your program can see the mode with `target.mode` ([chapter 11](11-compile-time.md)). How you select a mode on the command line is up to the implementation; the reference tool takes `--mode=debug|checked|safe|fast` on `run`, `build` and `test` (the default is `debug`).

## Checked operations

An operation with a precondition that is verified at run time in Debug, Checked and Safe builds, and assumed in Fast builds, is a **checked operation**. These are all of them:

| Operation | Condition checked | In Fast builds, on failure |
| --- | --- | --- |
| `+ - * <<` and unary `-` on integers (plain flavor) | no overflow | an unspecified value |
| shift by at least the width (any flavor) | amount less than the width | an unspecified value |
| `x.trunc(T)`, `.round`, `.floor`, `.ceil` from float to integer | in range, not NaN | an unspecified value |
| `x?` on an optional | not `null` | an unspecified value |
| `x!` on a result | not an error | an unspecified value |
| `a[i]` on arrays and slices | `i < len` | **undefined behavior** |
| slicing `a[lo..hi]` | `lo <= hi <= len` | **undefined behavior** |
| `p.cast(P)` | address aligned for `P` | undefined behavior on use |
| fiber stack overflow, `@min_stack` | enough stack | undefined behavior |
| continuation rules ([chapter 12](12-fibers.md)) | used once, by the right `with` | undefined behavior |
| `assert` | condition is true | removed |
| integer `/` and `%` | divisor is not 0; not `MIN / -1` | **always traps** |

An *unspecified value* is a valid value of the right type, so using it cannot cause undefined behavior by itself. A Fast-build overflow simply gives you some number. The consequence is that an optimizer can never use overflow to delete your own checks, which is the C failure mode.

Division is the exception to "checks vanish in Fast builds": dividing by zero and `MIN / -1` **trap in every mode**, since the hardware does.

### Pick the operation that says what you mean

The language gives you safe alternatives for nearly every checked operation, so you rarely need to rely on a trap:

```gamma
at :: fn(xs: []const i32, i: usize) -> i32 {
    return xs[i]                     // traps if i >= xs.len (UB in Fast)
}

at_or_zero :: fn(xs: []const i32, i: usize) -> i32 => xs.get(i) else 0

add_checked :: fn(a, b: u8) -> u8 => a + b       // traps on overflow
add_wrap    :: fn(a, b: u8) -> u8 => a +% b      // wraps: 250 + 10 is 4
add_sat     :: fn(a, b: u8) -> u8 => a +| b      // saturates: 250 + 10 is 255
add_opt     :: fn(a, b: u8) -> ?u8 => a +? b     // null on overflow

small :: fn(big: u32) -> ?u8 => big.checked(u8)  // null if it does not fit
```

Use the plain flavor when overflow would be a bug in your program: the trap tells you at the source location. Use `+%` for hashes and ring counters, `+|` for clamping quantities, and `+?` when you want to react to overflow.

Division by zero is not recoverable at the operator, so check first or use a function that returns a result:

```gamma
divide :: fn(a, b: i32) -> ?i32 {
    if b is 0 { return null }
    if a is -2147483648 and b is -1 { return null }      // MIN / -1 overflows
    return a / b
}
```

## Traps

A **trap** terminates the process at once. Debug and Checked builds first report the source location and reason on standard error. A trap is *not* an exception: it cannot be caught, it does not unwind, it does not run `defer` statements, and it never returns to any handler. Recoverable failure uses results and optionals ([chapter 5](05-optionals-and-errors.md)); a trap means "this program has a bug".

Debug builds (the default) also report how the program got there: each call leading to the trap, innermost first, with the function it lies in. Frames of the core library are left out, and a report of more than 20 calls is shortened to its first 10 and last 10, with a `  ... more calls` line between them:

```
trap: index 5 out of bounds for length 3
  at shapes.gm:12:9 in area
  called from shapes.gm:20:14 in total
  called from shapes.gm:31:19 in main
```

Running out of stack is a trap too, reported as `stack overflow`, however large the frames involved.

`trap(msg)` traps unconditionally in every build mode, and has type `never`, so it fits anywhere an expression is needed:

```gamma
level_name :: fn(n: u8) -> string => match n {
    0 => "low"
    1 => "high"
    _ => trap("level out of range")
}
```

`assert(cond, msg)` is a checked operation: it traps on a false condition in Debug, Checked and Safe builds and is **removed in Fast builds, argument and all**. That means a condition with side effects behaves differently by mode:

```gamma
first_of :: fn(xs: []const u8) -> u8 {
    assert(xs.len > 0, "first_of needs a non-empty slice")
    return xs[0]
}
```

> **Common mistake.** Never put the work you need inside `assert`. `assert(advance() is not null)` does not call `advance` in a Fast build. Compute the value first, then assert on the result.

`expect(cond, msg)` is for tests: it may only be called from a `test`, and a false condition fails the test and stops it.

## Tests

A `test` block is compiled only when building tests, runs with replacement handlers for the ambient effects ([chapter 7](07-effects.md)), and passes if it completes. It fails if an `expect` is false, a trap occurs, an error is returned, or the leak-checking allocator finds a leak. `try` works inside a test.

```gamma
test "saturating arithmetic" {
    x: u8 = 250
    expect(x +| 10 is 255)
    expect(x +% 10 is 4)
    expect(x +? 10 is null)
}

test "narrowing conversions" {
    big: u32 = 300
    expect(big.checked(u8) is null)
    expect(big.sat(u8) is 255)
    expect(big.trunc(u8) is 44)
}
```

A failing `expect` reports its source location. Test names within a module must be distinct. Because tests replace `Out`, `Fs`, `Clock` and the rest, they are deterministic without any mocking code of your own.

## Undefined behavior: the closed list

Anything that is not on the following list is *defined*: it has a meaning, is unspecified, is implementation-defined, or traps (§18.4). In particular, **integer overflow, reading `---` storage, NaN handling and shift amounts never cause undefined behavior.** The complete list of operations that can:

1. Accessing an object outside its lifetime: use after `free`, or through a pointer to a dead local or fiber stack.
2. Accessing memory through a pointer that does not point to a compatible object, including misaligned access and a wrong `downcast_unchecked`.
3. Out-of-bounds indexing or slicing in Fast builds, any many-pointer access outside its allocation, pointer arithmetic that leaves an allocation (other than one past the end), and subtracting or comparing many-pointers into different allocations.
4. Writing to a constant, a string literal, or other read-only static memory through a pointer obtained by `cast` from a `const` pointer.
5. Reading a `bool`, union, optional, result, pointer or function pointer whose bytes are not a valid value of its type (reachable only through `#c_union`, foreign code or `cast`).
6. Freeing memory through the wrong allocator, or twice.
7. A data race ([chapter 14](14-interop.md)).
8. The Fast-build cases above marked undefined behavior.
9. Violating the contract of a foreign function, or the foreign function's own undefined behavior.
10. Volatile, atomic or assembly operations used against their stated requirements.

You can see the pattern: almost everything on the list is a *memory* or *concurrency* error that cannot be checked cheaply, plus a few places where Fast builds drop a check. Arithmetic is never on it.

## What Checked mode does not check

Checked mode checks *local facts*: arithmetic, bounds, unwraps, alignment of casts, and the continuation rules. It does **not** check use-after-free, dangling pointers, wrong-allocator frees or data races. Telling these apart needs every pointer to know what it points to, which costs more than a release build should pay by default. Safe builds pay it (next section); Debug builds detect some of them with cheaper means.

Debug builds add the detection that is expensive:

- storage from `---`, freed allocations and dead stack frames are filled with recognizable patterns (`0xAA` for uninitialized, `0xDD` for freed), so stale data stands out;
- every allocation made with `new` or `make` is tagged with its allocator, and freeing it with `free` or `destroy` through a different one traps, reporting both call sites;
- every call checks that the stack (a fiber's or a thread's) has room, so running out of it traps instead of overwriting memory;
- tests use the leak-checking allocator.

A sensible workflow is to develop and test in Debug, run your tests (and untrusted inputs) under Safe where you can, ship Checked unless you have measured a cost, and use Fast only for code where you have convinced yourself (and tested under Debug) that no check can fire.

The compile-time evaluator ([chapter 11](11-compile-time.md)) always performs every check and detects undefined behavior regardless of build mode, so a check that would fail at run time fails the build instead when the code is evaluated at compile time.

## Safe builds

A Safe build (§18.7) performs every check of a Checked build and also checks every memory access. Each pointer carries, invisibly, the allocation it came from. An access traps unless it lies inside that allocation, the allocation is still live, and the access is permitted. That turns most of the undefined-behavior list into traps: use after free, a dangling pointer to a dead local or a finished fiber's stack, a many-pointer access out of bounds, a double free, a write to read-only memory, and access through a pointer made from an integer with `to_ptr`. A data race still gives unspecified values, but it cannot corrupt memory.

```gamma
main :: fn() -> AllocError!() \ {Out, Alloc} {
    p := try new(i32, 7)
    destroy(p)
    print("{}\n", p^)    // Checked: undefined behavior. Safe: traps.
}
```

Built with `gamma build --mode=safe` (or run with `gamma run --backend=c --mode=safe`), the program ends with Fil-C's own report (`filc safety error: …`) and exit status 134, the same as a Gamma trap. Source locations are not reported.

The reference tool builds Safe through the C back end with [Fil-C](https://fil-c.org), a memory-safe implementation of C. That sets the requirements:

- Linux on x86_64 or aarch64. The compiler looks for `filcc` on `PATH`, then in `/opt/fil/bin/filcc`; `CC` overrides. If the compiler it finds is not Fil-C, the build fails and says so. To install it, download a release from <https://github.com/pizlonator/fil-c/releases> (the `optfil` x86_64 tarball), extract it and run `sudo ./setup.sh`, which installs to `/opt/fil`.
- Every C library you link must itself be built with Fil-C (it ships its own libc). `@asm` with pointer operands is rejected.
- The interpreter, which has its own memory checks, treats Safe as Checked, so `gamma run --mode=safe` without `--backend=c` does not use Fil-C (a program with C sources of its own, or `--target`, is compiled natively instead). `--target` cannot be combined with `--mode=safe`.
- `target.mode is .Safe` lets code adapt at compile time.

**Granularity.** Safe builds protect allocations, not pieces of them. An allocator that carves objects out of one big block protects only the block, so an overrun into the next object is not caught. `std/arena` therefore gives every allocation its own block in Safe builds, and your own allocator can do the same with `when target.mode is .Safe { … }` ([chapter 9](09-memory.md)).

**Stacks.** Fibers run on stacks Fil-C allocates, and overflowing a thread or fiber stack traps. Fil-C's frames are bigger, so deep recursion that fits in Checked may overflow in Safe.

**Costs.** Programs typically run 2 to 5 times slower than Checked builds, and code that allocates and frees many small objects 15 times or more; they also use more memory (the allocator is garbage collected) and compile more slowly. Safe is a tool for testing and for running untrusted inputs through code where memory safety matters more than speed, not a default.

## How Gamma avoids the C footguns

| C problem | Gamma |
| --- | --- |
| Signed overflow is UB | Defined: trap, wrap, saturate or `null`, your choice |
| Null pointer dereference | Pointers are never null; `?^T` must be unwrapped |
| Uninitialized reads | `---` is opt-in, only for plain data, and reads are never UB |
| Array overruns | Slices carry lengths; `a[i]` is checked |
| Implicit narrowing | Conversions are explicit and named |
| `x & 1 == 0`, `1 << n - 1` | Precedence errors are compile errors |
| `switch` fallthrough, forgotten cases | `match` is exhaustive with no fallthrough |
| Ignored error codes | Results must be handled or discarded with `_ =` |
| Hidden allocation | Allocation is an effect in the signature |
| Leaks and double frees | `defer`, tagged allocations, leak-checking tests (not eliminated) |

## Common mistakes

- **Relying on a plain `+` to wrap.** It traps in Debug, Checked and Safe builds and gives an unspecified value in Fast builds. Say `+%`.
- **Relying on `x?` or `x!` for expected failures.** Those are for "cannot fail here". Use `if`, `else` or `try` for failures you expect.
- **Side effects inside `assert`.** They disappear in Fast builds.
- **Believing a trap can be handled.** It cannot, and `defer` does not run.
- **Using many-pointers `p[i]` as if they were checked.** They are not; convert to a slice (`p[a..b]`) at the C boundary and use that.
- **Assuming Checked means memory-safe.** It does not track lifetimes. Test under Safe if memory safety matters.

## Summary

- One rule: every check traps in Debug, Checked and Safe builds and vanishes in Fast builds; only integer division always traps.
- Overflow is never undefined. Choose among `+`, `+%`, `+|` and `+?`.
- Traps end the process; they are not exceptions and skip `defer`.
- Undefined behavior is a closed list of memory and concurrency errors.
- Debug builds add poisoning, allocation tags and leak checks; Checked mode does not track lifetimes; Safe builds (with Fil-C) trap on every memory error, at a large cost in speed.

Next, [chapter 14](14-interop.md) covers calling and being called from C.

For the full rules, see [§18 Safety, checks and undefined behavior][s18] of the specification.

[s18]: ../../gamma-spec.md#18-safety-checks-and-undefined-behavior
