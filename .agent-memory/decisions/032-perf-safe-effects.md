# 032 Safe new/destroy + VM perform (s29, thread "Safe mode and effect perf", branch claude/perf-safe-effects-16om52)

## Measured (callgrind, bench/programs; Fil-C 0.686 installed via optfil setup.sh -u)
- safe_new_destroy (1M iters) Safe: 1731 → 997 instr/iter; wall 149 → ~86 ms (Checked ~32 ms,
  402 instr/iter). Left: Fil-C heap alloc+free ~400, two indirect clause calls ~150 each
  (Fil-C indirect calls are dear), thunk GM_LD of the Allocator.
- VM: iterate.gm 1760 → ~1290 instr/element; perform.gm 816 → ~796; call.gm unchanged.

## What was done
- Fil-C blobs (types.rs BlobKind): Words (no pointers: struct of uintptr_t), Lanes (pointers
  at fixed offsets, Layouts::pointer_words: struct typed void*/uintptr_t per word), else the
  old union. TRIED all-pointer structs: 74 Safe failures ("pointer with null object") — SROA
  turns a word read both as int (union payload) and ptr into inttoptr, dropping the cap. Only
  at -O2 (O0 copies via memcpy). Safe lane is the only test that catches this: run it.
- borrow.rs steady params: by-ref param read in place when every direct call passes a const
  static, a temp (Aggregate/Array/Call), or a caller local never written/addressed/captured.
  Excludes FnRef/Closure'd fns (incl. const/global relocs), exports, trap_hook, Clause/Literal,
  captured params, debug_info. No fixpoint needed (pure callers only call pure callees).
- gm_find inline fast path for the innermost node (gm_rt.h; out of line = gm_find_outer).
- VM: vm_upvar_frame uses st.extra.env for level 1 (no clause frame made); LdUpvar word fast
  path; LdUpvar/AddrUpvar moved out of vm_slow (its prologue is ~50 instr); Ctx::find
  inline(always) + same_args; put_bytes inline 8-byte path; vm_state forgets the pooled
  St's empty extra/cx instead of dropping.
- Tried, no gain: aligned load in decl thunks instead of packed GM_LD.

## Profiling recipe
- C: `gamma build --mode=safe --dump`, then filcc -O2 -g X.c ~/.cache/gamma/gm_rt-*.o -lm
  -pthread; callgrind_annotate --auto=yes. VM: build with CARGO_PROFILE_RELEASE_DEBUG=
  line-tables-only into a separate target dir; callgrind --dump-instr=yes; map addresses with
  addr2line -a -i (script idea: aggregate by first non-/rustc/ frame).
