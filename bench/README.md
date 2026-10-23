# Benchmarks

Two kinds of benchmark: `benches.gm` holds `bench` declarations that time the same
operations (`gamma bench bench/`, which CI runs on every pull request and compares with the
base branch), and `programs/` holds small programs for counting instructions, which exercise the interpreter's call, effect and allocation paths.
Each prints one number (a checksum), so a run is also a sanity check.

| Program | What it does |
|---|---|
| `programs/empty.gm` | Prints a constant: the cost of starting up, to subtract from the others. |
| `programs/call.gm` | 200,000 calls of a small function: the baseline for `perform.gm`. |
| `programs/perform.gm` | 200,000 performs of an operation handled by a `with` handler. |
| `programs/iterate.gm` | A `for` over 200,000 elements of an iterator: a `yield` performed per element, its clause updating a captured variable. |
| `programs/new_destroy.gm` | 20,000 `new` + `destroy` of a two-field struct through the ambient `Alloc`. |
| `programs/list_push.gm` | 100,000 `push` onto a `List`, then a sum over `each(l.items)`. |
| `programs/map_insert.gm` | 20,000 inserts into a `HashMap`, then 20,000 lookups. |
| `programs/safe_call.gm`, `programs/safe_new_destroy.gm`, `programs/safe_list_push.gm`, `programs/safe_map_insert.gm` | The same programs with 50 times the iterations, long enough to time a native executable. |

## Safe builds

A Safe build compiles the generated C with Fil-C, which checks every access. To compare the
cost with a Checked build, build both and time the executables rather than `gamma run`, whose
time includes compiling:

```sh
for m in checked safe; do target/release/gamma build --mode=$m -o /tmp/b.$m bench/programs/safe_map_insert.gm; done
time /tmp/b.checked; time /tmp/b.safe
```

Under Fil-C a call into its runtime is much dearer than the access it guards, so the code
generator avoids copying aggregates that hold pointers (a slice is read in place, not copied to
a temporary first) and avoids taking the address of locals, which Fil-C moves to the heap.

## Timing: `gamma bench`

```sh
cargo build --release
target/release/gamma bench bench/              # compiled via C in Fast mode
target/release/gamma bench --filter=perform bench/
```

Each line is the mean time of one pass of the body with the spread of 20 samples. Wall time
varies with the load of the machine; CI prints the ratio to the base branch as information
and never fails on it.

## Running the programs

```sh
cargo build --release
target/release/gamma run bench/programs/perform.gm                 # the interpreter
target/release/gamma run --backend=c bench/programs/perform.gm     # through the C back end
```

Compare instruction counts rather than wall time, which varies with the load of the
machine. With `valgrind` installed:

```sh
valgrind --tool=callgrind target/release/gamma run bench/programs/perform.gm 2>&1 | grep refs
```

or with `perf`:

```sh
perf stat -e instructions:u target/release/gamma run bench/programs/perform.gm
```

The total includes compiling the program and the core library, which `empty.gm` measures
(about 58 million instructions); the cost of an operation is
`(program - empty) / iterations`. `callgrind_annotate` on the `callgrind.out.*` file
shows where the instructions go; a release build with `CARGO_PROFILE_RELEASE_DEBUG=line-tables-only`
gives line-level detail.

`gamma run --mode=fast` and `--mode=debug` select the build mode; the numbers above are
for the default (Debug), which also records the allocator and call site of each block.
`GAMMA_VM_DUMP=name` prints the bytecode of the functions called `name`, and a build with
`--features vm-profile` counts the instructions run when `GAMMA_VM_PROFILE=1` (see
`docs/contributing.md`).
