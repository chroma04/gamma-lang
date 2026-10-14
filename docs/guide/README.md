# The Gamma Guide

Gamma is a systems programming language meant to replace C. It keeps what makes C useful (explicit memory, C-compatible layout, no garbage collector, no hidden allocation, predictable cost) and removes the parts that make C dangerous: implicit numeric conversions, null by default, undefined signed overflow, fallthrough, and operator-precedence traps such as `x & 1 == 0`. Its distinguishing feature is **algebraic effects with handlers**, which Gamma uses for the jobs C does with globals, callbacks plus `void*`, `setjmp`, and hand-written state machines: allocation, I/O, logging, iteration, early exit and coroutines.

This guide is for experienced programmers, especially C programmers, who want to learn how to *use* Gamma. Each chapter introduces one area with short, realistic examples and a sentence or two on why the design is the way it is. It is a tutorial, not a reference: when you need the exact rule, the guide points to the numbered section of the [language specification](../../gamma-spec.md), which is the normative definition.

> **Status of the implementation.** The reference implementation in this repository is under construction. The `gamma` tool type-checks programs, runs them with an interpreter (`gamma run file.gm`), and compiles them natively through C (`gamma build file.gm`, or `gamma run --backend=c file.gm`). The whole language works through C; the interpreter runs everything except calls to foreign code and inline assembly, for which `gamma run` compiles the program instead. [Chapter 1](01-getting-started.md) has the details.

## Contents

| Chapter | What it covers |
| --- | --- |
| [1. Getting started](01-getting-started.md) | Hello world, the `gamma` tool and its current state, source files and modules at a glance |
| [2. Basics](02-basics.md) | Bindings, no shadowing, literals and literal inference, integer and float types, operators and overflow flavors, newlines and statements |
| [3. Control flow](03-control-flow.md) | `if` and `while` (as expressions and with binding conditions), `for`, labels, `match`, `defer` and `errdefer` |
| [4. Types](04-types.md) | Arrays, slices, strings, tuples, structs, unions and enums, pointers, conversions |
| [5. Optionals and errors](05-optionals-and-errors.md) | `?T`, `E!T`, unwrapping, `else`, `try`, error lifting, patterns |
| [6. Functions](06-functions.md) | Parameters, defaults, named arguments, spread, methods, second-class functions and function pointers |
| [7. Effects](07-effects.md) | Effects, handlers, the three operation kinds, `with`, ambient effects, `Sys`, effect polymorphism |
| [8. Iteration](08-iteration.md) | `Yield`, `for` as a handler, writing iterators, standard iterators, early exit, cursors |
| [9. Memory](09-memory.md) | Allocators and the `Alloc` effect, `new`/`make`/`free`, `List` and `HashMap`, cleanup with `defer` |
| [10. Generics](10-generics.md) | Type parameters, inference, constraints, generic types, compile-time value parameters, life without traits |
| [11. Compile time](11-compile-time.md) | Compile-time evaluation, constants, `when`, `target`, `@static_assert` |
| [12. Fibers](12-fibers.md) | `with ... on`, `ctl` operations, continuations, `resume` and `discard`, a scheduler sketch |
| [13. Safety](13-safety.md) | Build modes, checked operations, traps, what is undefined behavior |
| [14. Interoperating with C](14-interop.md) | Foreign functions, C-compatible types, `#export`, `#global`, atomics, volatile access |

## Conventions used in this guide

- Code blocks are tagged `gamma`. A line comment starting with `// error:` marks code that the specification says is a compile error; it is shown so you recognize the mistake.
- `...` inside a code block stands for elided code and is not Gamma syntax.
- Section references such as "§10.6" refer to the [specification](../../gamma-spec.md). Appendix C of the specification explains the design reasoning in more depth.
- [Editor support](../editors.md) describes syntax highlighting and the language server for common editors.
- Boxes titled **Common mistake** describe C habits that Gamma turns into compile errors.
- Where an example needs a helper that the language does not define (for instance a queue type for a scheduler), the text says so and calls it "a function you would write" or similar. The core library is exactly what [§21](../../gamma-spec.md#21-the-core-library) defines; this guide does not use any other library functions.

## A first taste

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

main :: fn() \ {Out} {
    shapes := [_]Shape(.Circle(1.0), .Rect(w: 2, h: 3), .Empty)
    for s in shapes {
        print("{} has area {}\n", (s, area(s)))
    }
}
```

`::` binds names, `T(...)` constructs values, `match` must be exhaustive, and `\ {Out}` says that `main` performs output. The chapters that follow explain each of these pieces.
