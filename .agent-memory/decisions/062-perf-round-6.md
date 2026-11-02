# 062 — Performance round 6 (s61, branch claude/nifty-brahmagupta-1ymgmj)

User ask: "another round of performance-focused optimizations" (open-ended).
Measured with callgrind on an `-O2 -g` build of the split `--emit-c` units (recipe below).

## Results
- `check compiler` Ir 5.26G → 4.22G (-20%), wall 1.46 → 1.23 s. Hello-world compile
  64M → 57M Ir. `--emit-c compiler` 3.47 → 2.98 s.
- conformance/ + examples/ suites (-j4): 52.5 → 42 s (Debug -Og is most of it).
- Cost: compiler binary 7.9 → 8.5 MB, Checked self-build 49 → 52 s (inline hints, below).

## What changed
- cgen units.gm `function`: a split optimized build kept `static inline` small functions
  external but DROPPED the hint; gcc -O2 then would not inline even within the unit
  (parser `tok()`/`kind()` were 5% of check). Now the definition is `inline __attribute__…`
  (external definition: the shared header's prototype has no `inline`, C11 6.7.4).
- cgen calls.gm `variant_equal` (from expr.gm `.Eq`): `x is "lit"` → len + memcmp
  (`str_equal_at`, also string patterns in `pat` `.Const`); `x is .V(p)` → tag (normalized)
  && payload (eq helper for blobs, `==` else, bool as !=0) read in place when the payload
  is `frame_pure`. Before: build the 96-byte literal (memset) + generic gm_eq.
- helpers.gm `bytewise_eq` (ints with bits == storage*8, NOT floats/ptrs): slice and array
  eq → memcmp; byte-slice `<` → memcmp. String eq was gm_eq2 per byte (10M calls).
- check iterators.gm `enumerate_loop`: `for … in enumerate(xs)` (core `enumerate`, by name
  + is_prelude module) → the slice index loop with item `(i: i, x: s[i])` (Aggregate),
  no handler. 333 uses in the repo. Other iterators (each_ptr, reversed…) too rare.
- lexer `run` reserves `src.len / 4 + 16` tokens (Token = 112 B; ~4 B/token measured).
- core list.gm regrow_raw: tries `vt.resize` in place first (spec §21.5 says so now).
  conformance run/example3_bump_allocator.stdout changed (its allocator counts resizes).
- Debug C at `-Og` (driver native.gm opt_level, pkg link.gm) + gcc-only
  GCC_DEBUG_PASSES (-finline-functions-called-once, -ftree-bit-ccp, -fif-conversion);
  gcc detected by rtcache.gm `is_gcc(compiler_version)` ("Free Software Foundation", not
  clang); the version is now computed once in compile_c and passed to runtime_object.
  wasm-opt gets -O1 for -Og. sha256 Debug: -O1 200 ms, -Og 840, -Og+passes 250.
  jq.c compile: -O1 1.70 s user, -Og+passes 1.20 s. -O1's time is spread over passes.

## Measured, not taken
- Plain `-Og`: runtime 3-4x slower on hot loops (no inlining of called-once fns).
- `#pragma GCC optimize` instead of flags: may reset -fwrapv etc. (semantic) — avoided.
- Arena chunk_alloc (~50 instr/alloc, 2M calls in check compiler) and regrow_raw
  (~85 instr, 1.3M calls) left: checks, no single fix.

## Profiling recipe (accurate for small functions; `-g` builds distort them)
`GAMMA_C_UNITS=8 gamma build --emit-c compiler -o D/g` → `gcc <BASE_CFLAGS> -pthread
-O2 -g -c` each g*.c → link -lm -pthread → callgrind. Names: a `gamma build -g compiler`
by the SAME compiler numbers functions identically (scratch names.py maps gm_fN to the
first `#line` after the definition). Core functions have no #line: read their C.
callgrind_annotate --tree=caller gives callers; memcpy goes through the PLT (look at the
PLT address's callers). Generation N's C reflects generation N-1's cgen/checker.
