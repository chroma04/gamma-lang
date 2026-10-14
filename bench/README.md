# Benchmarks

Small programs that exercise the interpreter's call, effect and allocation paths.
Each prints one number (a checksum), so a run is also a sanity check.

| Program | What it does |
|---|---|
| `empty.gm` | Prints a constant: the cost of starting up, to subtract from the others. |
| `call.gm` | 200,000 calls of a small function: the baseline for `perform.gm`. |
| `perform.gm` | 200,000 performs of an operation handled by a `with` handler. |
| `new_destroy.gm` | 20,000 `new` + `destroy` of a two-field struct through the ambient `Alloc`. |
| `list_push.gm` | 100,000 `push` onto a `List`, then a sum over `each(l.items)`. |
| `map_insert.gm` | 20,000 inserts into a `HashMap`, then 20,000 lookups. |
| `safe_call.gm`, `safe_new_destroy.gm`, `safe_list_push.gm`, `safe_map_insert.gm` | The same programs with 50 times the iterations, long enough to time a native executable. |

## Safe builds

A Safe build compiles the generated C with Fil-C, which checks every access. To compare the
cost with a Checked build, build both and time the executables rather than `gamma run`, whose
time includes compiling:

```sh
for m in checked safe; do target/release/gamma build --mode=$m -o /tmp/b.$m bench/safe_map_insert.gm; done
time /tmp/b.checked; time /tmp/b.safe
```

Under Fil-C a call into its runtime is much dearer than the access it guards, so the code
generator avoids copying aggregates that hold pointers (a slice is read in place, not copied to
a temporary first) and avoids taking the address of locals, which Fil-C moves to the heap.

## Running

```sh
cargo build --release
target/release/gamma run bench/perform.gm                 # the interpreter
target/release/gamma run --backend=c bench/perform.gm     # through the C back end
```

Compare instruction counts rather than wall time, which varies with the load of the
machine. With `valgrind` installed:

```sh
valgrind --tool=callgrind target/release/gamma run bench/perform.gm 2>&1 | grep refs
```

or with `perf`:

```sh
perf stat -e instructions:u target/release/gamma run bench/perform.gm
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
