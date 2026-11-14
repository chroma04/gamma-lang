# 068 — Performance round 8 (s72, branch claude/magical-hamilton-hmt1ok)

User ask: "another session of general performance/optimization-oriented refactoring".
Measured with callgrind Ir on the compiler itself (scratch `compiler_ir.sh BIN`: check /
lint / fmt --check / build --emit-c of `compiler`, check of examples/hello.gm, doc lib/std).

## Baseline → after (Ir, G)
check 4.685 → (see end), fmt 3.219, lint 5.776, emit-c 9.610, hello 0.064, doc 0.280.
Upper bound for cross-unit inlining: single-unit build (GAMMA_C_UNITS=1, 3 min):
check 3.909, fmt 2.355, emit-c 8.190 (−15..27%).

## Done
1. units.gm `SHARED_SMALL_BYTES` 1600: split_by_file treats `static inline
   __attribute__((aligned(4)))` functions (optimized builds mark fns ≤64 temps so) of at
   most that much C (without #line) as inline definitions like `#inline` ones. Thresholds
   measured (cold build s / check Ir): 0: 42.2/4.685; 1200: 46.8/4.447; 1600: 47.9/4.402;
   2500: 49.5/4.376; 4500: 64.6/4.306. Debug builds unaffected (no `static inline`).
2. Boxed big rare payloads: TokenKind.Int/Float and ast.ExprKind.Int/Float are
   `^const IntLit/FloatLit` (Token 128→48 B, ast.Expr 136→88); tir.ExprKind
   UntypedFloat/UntypedComplex (`tir.untyped_float/untyped_complex` constructors) and
   Write/WriteDeferred are pointers (tir.Expr 224→88). check −8.8% from tokens/ast alone.
3. `@hash`: was FNV-1a per byte with a helper call per scalar/byte. Now `gm_mix64(h, x)` =
   fold(hi^lo) of (h^x)*0x9E3779B97F4A7C15 as u128; ints by value in one word (128-bit:
   two), `[]u8`/`[N]u8` eight bytes per word (`gm_mix_bytes`, LE), scalars inlined into
   the composite helper (`hash_scalar`/`hash_step`/`hash_elems`). interp/builtins.gm
   `hash_u64/hash_bytes/hash_int` mirror it. `@hash` is NOT constant-evaluable in gammac
   ("cannot check the builtin .Hash at compile time yet"), so the test
   conformance/run/hash_structure.gm pins printed values (both lanes share .stdout).
   No golden output elsewhere changed (maps iterate in insertion order / tests sort).
4. FLAKE ROOT CAUSE (not perf): "core module `complex` does not declare `Complex`" was
   the fmt corpus test rewriting lib/ and tests/conformance files in place
   (`tokens(t, path, src)` wrote src back). Fixed: `tokens_of_file`. Found via mtimes +
   `strace -f -e trace=openat`. The "memory pressure" explanation in gotchas was wrong.

## Findings not acted on / next candidates
- Release binaries (release.yml) build `--mode=fast` split by file; `GAMMA_C_UNITS=1`
  would give the single-unit numbers above for a 3-minute build. Not changed (cannot
  test the macOS/clang lane here).
- Parser: every binary-operator level costs ~180 Ir per operand even with no operator
  (Abort!ast.Expr copies); lexer ~680 Ir/token.
- regrow_raw 1.2M calls / chunk_alloc 2M calls in check: many small lists.
- sema `check_method_candidates` 69M Ir in ONE call; parser `speculate` 120M.
