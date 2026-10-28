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
- Seed NOT refreshed (still Debug C; verify.sh uses SOURCE trees, independent).

## Side fixes
- `gamma build -g compiler` trapped: views.gm `is_aggregate` treated `?^T` (nullable pointer,
  repr .Ptr, no offsets) as aggregate; debug.gm `guard` emitted `#undef defined`.

## Profiling recipe
`gamma build -g compiler -o $SP/gammag` (-O0, C kept as gammag*.c); map gm_fN → Gamma names
from the C (`gm_bt B = { gm_bt_top, "name"`); `valgrind --tool=callgrind`; callgrind_annotate
(--tree=caller for memset callers). Scripts were in the s51 scratchpad (fmap.py/ann.py):
regex `^(?:static )?[^ ].*?\b(gm_f\d+)\(` on lines ending `{`, then the next B line.
