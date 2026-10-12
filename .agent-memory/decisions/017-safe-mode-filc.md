# 017 — Safe build mode via Fil-C (s6 thread "Fil-C memory safety", branch claude/project-thread-7tibkj)

Design doc for the user: /mnt/project-files/design/safe-mode.md (deliberation + plan).

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
- Runtime 10–45× slower than Checked (memcpy-heavy blobs hurt under Fil-C); compile is slow
  for big programs (see gotchas/environment.md). Follow-up: typed loads/stores instead of memcpy,
  uncaptured locals as C locals.
