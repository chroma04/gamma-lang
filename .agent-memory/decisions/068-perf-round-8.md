# 068 — Performance round 8 (s72, branch claude/magical-hamilton-hmt1ok)

User ask: "another session of general performance/optimization-oriented refactoring".
Measured with callgrind Ir on the compiler itself (scratch `compiler_ir.sh BIN`: check /
lint / fmt --check / build --emit-c of `compiler`, check of examples/hello.gm, doc lib/std).

## Baseline → after (Ir G; wall s, best of 5; fixed-point compiler from the new seed)
check 4.685 → 2.774 (1.21 → 0.76 s) · lint 5.776 → 3.622 (1.65 → 1.04) · fmt 3.219 → 1.961
(0.46 → 0.22) · emit-c 9.610 → 7.038 (2.65 → 1.81) · hello check 64.0M → 31.3M · doc
280M → 143M. Binary 8.47 → 8.85 MB, seed 74 → 83 MB, cold compiler build 42 → ~46 s,
bootstrap/build.sh 58 → 46 s, suite 168 → 107 s (-j 4).
Programs (Ir, checked / fast): map_insert 12.38M → 7.78M / 8.29M → 4.36M; wordfreq on the
spec 45.6M → 35.1M / 38.6M → 31.8M; perform 4.36M → 2.95M (checked); call 1.155M → 0.354M
(checked: = start-up). sha256sum, json_query, list_push unchanged.
Upper bound for cross-unit inlining measured at the start: single-unit build
(GAMMA_C_UNITS=1, 3 min): check 3.909, fmt 2.355, emit-c 8.190 (−15..27% vs 4.685).

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

5. Parser: `binary_follows(kind)` — parse_cmp/range/bit/shift/add each parse a prefix
   operand and return it unless an operator of levels 8–3 follows; `L_from(first)` are the
   levels given the first operand. `binds_ahead()` (a `:=` on the line at bracket depth 0)
   gates the speculative pattern parse of parse_let_else / parse_cond_part (was 120M Ir
   of rollbacks). keyword_from switches on the first letter. sema check_method_candidates
   was O(n²) (69M Ir in one call) → map keyed (adt, name). check 3.76 → 3.41.
6. BASE_CFLAGS gains `-fno-stack-protector` (Ubuntu gcc defaults to -strong; every blob is
   a byte array so every function had a canary): −6%. bootstrap/build.sh mirrors the
   flags. Package C sources (pkg/link.gm) use only tc.cflags → compiler defaults.
7. Shallow probes: cgen func.gm `call_depth(g, f)` (memo Gen.depth_memo; direct `.Fn`
   calls only, ≤ SHALLOW_DEPTH 3; Perform/Indirect/Write/With/Resume/Cancel/Fiber/Asm/Sys,
   foreign and recursive = unknown) → `GM_STACK_CHECK_SHALLOW(&F, n)` (gm_rt.h: no probe
   if sizeof(F)+n ≤ GM_SHALLOW_FRAME 256). 1,480 of ~10,000 compiler functions. −6%.
   Test conformance/run/shallow_calls_near_stack_limit.gm.
8. helpers.gm `define_helper` emits `static inline`; units.gm SMALL_FN = "static inline "
   (any small static inline function is shared; `external()` strips it). −7.7% check,
   −15% fmt (string `is` was a cross-unit call).
9. Seed refreshed (tools/refresh-seed.sh target/bootstrap/gamma; verify.sh OK).
Checked locally besides the suite: CC=clang compiler build + conformance subset; s390x
lane (apt-get update; apt-get install gcc-s390x-linux-gnu qemu-user;
GAMMA_TEST_LANE=s390x harness lanes/ → 495 pass, 48 s).

## Measuring recipe used (scratch, rebuilt each session)
- Split builds give every function an external `gm_f_<name>_<hash>` symbol, so callgrind
  on an ordinary build shows names: no `-g` build needed (and `-g` distorts).
- `calls.py OUT [regex] [n]`: parses the callgrind out file (fn=/cfn=/calls= lines) and
  prints self Ir, calls and Ir/call per function — call counts found the O(n²) loop, the
  speculation waste and the per-level parser cost.
- cgen changes show only in a compiler built by a compiler that has them: build X1 with
  the old binary, X2 with X1, measure X2 (each ~35–50 s with the shared cache).
- Disassemble one hot function (`objdump -d --no-show-raw-insn BIN | awk '/<name>:/…'`):
  that showed the canary, the repeated probes and the duplicated blob copies.

## Findings not acted on / next candidates
- Release binaries (release.yml) build `--mode=fast` split by file; `GAMMA_C_UNITS=1`
  would give the single-unit numbers above for a 3-minute build. Not changed (cannot
  test the macOS/clang lane here).
- Parser levels that remain (else/logic/not/cmp + primary/postfix) still cost 140–180 Ir
  each: `Abort!ast.Expr` (96 B) is copied 4–5 times per level (t3 → t4 → payload → F.l1 →
  F.ret → gm_out). GCC 13 does not merge copies of blob structs (byte arrays read with
  memcpy defeat SRA): each copy is stored 2–4 times. A cgen pass that drops the
  `t4 = t3` temporaries / builds results in place would help ALL generated code.
- Lexer ~600 Ir/token (emit 95, skip_trivia 50, lex_token 48, flush_newline 43, push 43).
- regrow_raw 1.2M calls (99 Ir self) / chunk_alloc 2M calls (51) in check: many small
  lists. In emit-c: List(u8).extend 12.7M calls (39 Ir, not inlined: 3.9 KB of C),
  memcpy 13M, sink stubs gm_pf* 340M, regrow_raw 3.2M calls. `@write` to a closure
  literal could call the closure's function directly (stub per closure instead of the
  shared indirect one): est. −5..7% of emit-c. Not done.
- `@hash` is not constant-evaluable in gammac although §16.8 says it is (roadmap/bugs-open).
- HashMap find ~150 Ir self per lookup (hash + probe + eq inlined): fine.
