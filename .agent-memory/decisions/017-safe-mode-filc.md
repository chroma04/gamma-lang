# 017 — Safe build mode via Fil-C (s6 thread "Fil-C memory safety", branch claude/project-thread-7tibkj)


## Decision
- 4th mode `--mode=safe` (optional per spec §1.4), NOT a rename of Checked. Semantics in §18.7:
  every Checked check + every memory access checked against the pointer's allocation
  (Fil-C InvisiCaps). Interpreter treats Safe as Checked (its own address-based checks).
- Depend on Fil-C (0.686, /opt/fil glibc distribution) instead of a homebrew scheme.
  `Toolchain::for_safe_build`: CC if set, else filcc on PATH, else /opt/fil/bin/filcc.
  `Target.filc` from `__FILC__`; main.rs errors if a Safe build's compiler is not Fil-C.
- Generated C must be capability-clean: never round-trip pointers via integers.
  GM_PTR_ADD (zmkptr), GM_PTR_TAG/UNTAG (zorptr/zandptr), blobs = unions with void* lanes
  under filc (types.rs blob_typedefs), slices formatted via memcpy of the pointer.
- Runtime: zgc_aligned_alloc; thread handles = table slot+1; fibers via ucontext
  (gm_filc_running per thread; Fil-C allocates the fiber stack, `stack` gives size only);
  GM_NO_FRAME_PROBES under __FILC__; gm_write_out fflushes (Fil-C panics skip our flush); no gdb script section (top-level asm crashes filcc).
- f16: Fil-C miscompiles __truncdfhf2 etc → -mf16c (f16c_flag) + software GM_F16.
- std/arena: `when target.mode is .Safe` → block_alloc (one heap block per allocation,
  linked records) so Fil-C protects each object; chunk_alloc otherwise.
- Tests: `//! mode: safe` tests (tests/conformance/safe/) are C-only, run when Fil-C found;
  GAMMA_TEST_SAFE=1 adds the lane (default/checked-mode run+test tests also as Safe, stderr not
  compared); `//! safe: skip <reason>` opts out. CI job "safe" installs optfil (setup.sh -u).

## Known costs / follow-ups (plan step 5)
- Thread "Faster Fil-C codegen" (branch claude/filc-faster-codegen-ewo3pj) did:
  1. Ladder emitted once per scope (func.rs `end_scope` rungs gm_ldN_K / gm_llN / gm_lad);
     exits are one goto; local exits crossing a defer take the ladder. example2_editor
     main 7.7k → 1.1k C lines; filcc 8 min → 29 s.
  2. GM_LD/GM_ST under __FILC__ = packed-struct typed access (memcpy on a local made F
     escape → GC alloc per call). fib(35) Safe 3.1 s → 0.09 s (clang 0.05).
  3. GM_INT_TO_PTR (to_ptr) = empty-asm-laundered cast: with typed loads LLVM folded
     inttoptr(ptrtoint p) → p (capability kept); zmkptr(NULL,n) made filcc -O1+ DROP the
     faulting access (exit 0!) — filcc deletes statically-failing checks.
- s12: place slices read in place (map/list 2–3x faster). Remaining: new/destroy ~4x
  Checked (borrow.rs `site` copy; needs a static-constant-at-all-call-sites analysis).
- Earlier remaining: trees 16× (new/destroy = GC alloc + by-ref args like the 40-byte location
  struct escaping), hmap 4.3×, sieve 3.9×. Uncaptured locals as C locals: done (few %,
  c-backend-plan.md end). By-address temps (same PR #17): by-value aggregates measured
  SLOWER under Fil-C (by_ref threshold 64: trees 3.9→5.2 s; microbench 40B by value 65 ns
  vs &tmp 15 ns: Fil-C copies aggregate args through allocated memory). Done instead:
  literal-only by_ref args → static `gm_kN` (cgen/constarg.rs, init in gm_init_consts);
  under filc no MAX_C_LOCAL (no stack check), so a large escaping local escapes alone.
  Trees Safe ≈ −20%. Tried & dropped: callee passing its by-ref param pointer through
  (new→tag `site`): no measurable gain, needs caller-side no-alias protocol.
  Remaining: GC alloc of `new` itself; `&key` (^const K) in std maps escapes per call.
