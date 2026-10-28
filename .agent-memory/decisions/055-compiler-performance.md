# 055 — Making the self-hosted compiler fast (s51)

Baseline (s51 start, 4 cores): `check compiler` 4.3 s; `build compiler` 62 s (≈10 s front end
+ cgen, rest gcc -O1 on 62 MB of C in 4 units); full `tools/test.sh -j 4` 28.5 min wall, of
which `long_expressions/native_long_sum` alone 24 min (CPU), long chains ~30 min total.

## Algorithmic fixes (front end)
- check/expr.gm `expr`: `mentions_complex_constant` (whole-subtree walk + try_untyped at the
  leaves) ran at EVERY operator node → O(n²) (cubic-ish with lookups). Now `FnCx.complex_free`
  is set while checking the operands of an operator tree that mentions no complex constant;
  any non-operator node (call, block, …) clears it for its insides (wrapper `expr` →
  `expr_kind`). Remaining quadratic case: `1j + y + y + …` (true at root, try_untyped null).
- check/expr.gm `module_of`: walked the whole `.c.c.c` chain per access → O(n²). Now finds the
  root name with a loop and memoizes the last non-module path in `FnCx.value_path` (pointer
  identity, `p is e`; hit when asked again for it or for its base).
- load/loader.gm: module scopes had a linear `lookup` (13.5% of `check compiler`). Now
  `Module.index: HashMap(string, usize)` (first decl of each name), maintained by `add_named`
  (all scope pushes go through it; loader.gm + core.gm).
- sema/layout.gm: `type_size_ok` cleared `layout_cache` per call and `layout` re-pushed up
  to ty.id nulls (9M pushes). Now generation stamps (`layout_gen`, `CachedLayout`).
Result: chains 20000–40000 terms check in < 1 s; long_expressions + long_functions suites
30 min → 38 s; `check compiler` (Debug) 4.25 → 3.55 s.

## Compiler built in Checked mode
- Debug overhead in the compiler binary was ~25% of instructions (frame poison memset,
  0xAA heap fill, backtrace records — all spec §18.3) plus -O1. Checked = all checks, -O2.
- `compiler/gamma.toml` `[build] mode = "checked"`: bootstrap, test.sh, CI, refresh-seed all
  build it Checked. `check compiler` 1.65 s (2.6x), emit 4.6 s vs 10.6.
- Single-TU -O2 of 57 MB C took 5–7 min, so optimized builds now split too when funcs text
  > `OPTIMIZED_UNIT_BYTES` (4 MiB; cgen/units.gm `optimized_unit_count`, honors
  GAMMA_C_UNITS like Debug; Safe never splits). Split Checked build of the compiler: ~70 s
  wall, runs exactly as fast as single TU. `external` now externalizes the generated
  `static inline __attribute__((aligned(4)))` small functions (loses the hint, fine).
- The GAMMA_C_UNITS=3 CI lane now also splits Checked/Fast conformance builds.
- selfhost fixed_point no longer forces GAMMA_C_UNITS=1 (would be one 5-min -O2 TU):
  compares `--emit-c` of stage 1 and stage 2. unit_tests_pass runs `test --mode=debug`
  (leak checks). Debug build of the compiler (`--mode=debug`) gives trap call chains.
- Seed refreshed (needed for @copy_elems below): now Checked-mode C, 8 units ~57 MB.
  bootstrap/build.sh 3:15 → 2:58 and yields a Checked compiler; verify.sh passes.

## @copy_elems (core-only intrinsic, tir.Builtin.CopyElems)
- Profile of `--emit-c compiler` (Checked): core `List.regrow` 25% + `List.extend` 8% of
  instructions — element-at-a-time copies with checks. `@copy_elems(dst: []T, src: []const T)`
  (in_prelude only; check/intrinsics.gm copy_intrinsic; lengths equal by the caller):
  cgen memmove (arith.gm), interp one read+write via a tmp (heap.gm), eval element-wise
  load/store reading all first (eval/ops.gm). Used by core list regrow/extend/insert/
  remove/into_slice, slice copy_from, prelude dupe. emit 4.65 → 4.28 s; also speeds every
  compiled Gamma program's lists. Test: conformance run/copy_from_at_compile_time.gm.
- Adding it needed the documented two-stage build (old core → stage A; full change with A)
  and a seed refresh in the same PR.
- First try (copy_from only) showed no gain: profile before choosing the call site.

## Side fixes
- `gamma build -g compiler` trapped: views.gm `is_aggregate` treated `?^T` (nullable pointer,
  repr .Ptr, no offsets) as aggregate; debug.gm `guard` emitted `#undef defined`.

## Profiling recipe
`gamma build -g compiler -o $SP/gcg` (now Checked: -O2 -g, ~1m45; C kept as gcg*.c) →
`valgrind --tool=callgrind --callgrind-out-file=F $SP/gcg <cmd>`; `callgrind_annotate F`.
Names are gm_fN: map each def line (regex `^(?:static )?[^ #].*?\b(gm_f\d+)\(` ending `{`)
to its first `#line L "file"` and read `name ::` at file:L (core funcs have <core> paths:
read lib/core). `--tree=caller` is very slow on big profiles. Debug -g builds (-O0) have
`gm_bt B = {.., "name"` but distort costs (poison memsets).

## Not done / next candidates
- Interpreter: ~500 instructions per TIR node (byte-wise u128 int ops, type lookups per
  node: kind/size/int_info, tmp fill(0), Flow results). 1.8x from Checked only. A real speedup
  needs pre-resolved per-node info or compiling TIR to a closure/register form. Conformance
  `run` tests (interp lane) are now most of the suite's CPU (std_text_utf8 ~65 s alone).
- Debug-mode generated C is ~62 MB for the compiler (B.file/line per call, poison, unwind
  checks); smaller Debug C = faster Debug builds of big programs.
- `1j + y + y + …` chains are still quadratic (complex root, non-constant).
