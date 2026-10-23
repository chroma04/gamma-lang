# Examples

Complete, runnable Gamma programs, from a one-liner to multi-file modules and a
package. Each starts with a doc comment that says what it shows and how to run it.
Run them from this directory:

```sh
cd examples
gamma run hello.gm
gamma run wordfreq.gm --top 5 < data/wordfreq-sample.txt
gamma test grep.gm                  # most examples have tests too
gamma run --backend=c mandelbrot.gm # compiled through C, for speed
```

Both back ends print the same output. The test suite runs every example both ways
and compares the output with the files in [`../tests/examples/`](../tests/examples/),
which also show what each example prints.

## Command-line tools

| Example | What it does | What it shows |
| --- | --- | --- |
| [`hello.gm`](hello.gm) | prints a greeting | `main`, `needs Out` |
| [`wordfreq.gm`](wordfreq.gm) | the most frequent words of standard input | `hash_map` and `entry`, `sort_by`, an arena, a `Yield` iterator, `std/cli`, exit statuses |
| [`grep.gm`](grep.gm) | lines of files that contain a string (`-i`, `-n`, `-c`) | `std/fs`, error unions and `match` on them, tests on the in-memory file system |
| [`sha256sum.gm`](sha256sum.gm) | prints or checks SHA-256 digests | streaming `std/hash`, `std/encoding/hex`, reading in fixed-size pieces, a `fn` parameter as a reader |
| [`json_query.gm`](json_query.gm) | selects a value from a JSON document by a path like `.store.books[1].title` | `std/json`, errors with positions, `--pretty` printing |

## Effects and memory

| Example | What it does | What it shows |
| --- | --- | --- |
| [`logging.gm`](logging.gm) | an order-processing service that logs | a user-defined effect, handler literals and `handler … for` declarations, an `esc` operation for early exit, capturing a log in tests |
| [`generators.gm`](generators.gm) | Fibonacci numbers, primes and a tree walk as iterators | `Yield` and `for`, lazy adapters, `break` out of an iterator, a `ctl` coroutine with `resume` and `cancel` |
| [`allocators.gm`](allocators.gm) | the same work under different allocators | a tracking allocator as a `handler Alloc`, `std/arena`, `std/fixed_buffer`, containers that keep their allocator, the leak-checking test allocator |

## Concurrency and systems

| Example | What it does | What it shows |
| --- | --- | --- |
| [`fibers.gm`](fibers.gm) | a generate → square → sum pipeline | `std/fiber` schedulers with an effect set, channels between fibers, `Clock`, the fake clock in tests |
| [`threads.gm`](threads.gm) | counts primes on several threads | `std/thread`: `spawn`, `join`, `WaitGroup`, `Channel`, atomics |
| [`echo.gm`](echo.gm) | a TCP server that echoes lines in upper case, and its client | `std/net` on the loopback interface, a server thread, the in-memory network in tests |
| [`c_interop.gm`](c_interop.gm) | calls `strlen`, `strtol`, `snprintf`, `qsort` and more | `foreign` blocks, `needs not`, callbacks into Gamma, C structs by value, `#bits` |

## Larger programs

| Example | What it does | What it shows |
| --- | --- | --- |
| [`calc/`](calc/) | a calculator with variables, reading lines from standard input | a module of several files: a lexer, a precedence-climbing parser and an evaluator; tagged unions, exhaustive `match`, an AST in an arena, error positions |
| [`bank/`](bank/) | applies deposits, withdrawals and transfers and prints balances | a package (`gamma.toml`) with a submodule, `pub`, error unions with payloads, a type that keeps its allocator |
| [`life.gm`](life.gm) | Conway's Game of Life on a torus | fixed-size arrays, tables computed at compile time, `@static_assert`, methods |
| [`mandelbrot.gm`](mandelbrot.gm) | the Mandelbrot set in ASCII | floating point, loops, `--backend=c` for speed |
| [`julia.gm`](julia.gm) | a Julia set in ASCII | `Complex(f64)`: operators, `abs()` |
| [`nbody.gm`](nbody.gm) | a leapfrog simulation of a sun and three planets | math vectors (`#vec [3]f64`): operators, `dot`, `norm` |

Run a module directory or a package by naming the directory: `gamma run calc <
data/calc-input.txt`, `gamma test bank`.

To add an example, see "Examples" in [`../docs/contributing.md`](../docs/contributing.md).
