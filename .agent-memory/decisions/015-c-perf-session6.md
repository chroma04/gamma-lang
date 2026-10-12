# 015 C back end performance (session 6): exits, nested specialization, big values
Bench files were in scratchpad (adapter/tree/fib/closure/bigcopy .gm); numbers gcc, fast mode.
## Exit flag (D.2): measured, not adopted
- Hand C microbench (recursive fib with esc effect): returned {v, flag} pair vs TLS gm_unw
  check: gcc ~15% faster, clang 25-30% slower (cmov chain on the flag). Real benches showed
  no TLS cost (escape loop identical to plain). So gm_unw stays; spec D.2 amended to allow a
  per-thread record and skipping the test when the callee cannot exit.
- Gen::clause_may_exit(c, outer) (mod.rs): body scan; exits with levels>0, Break
  InstallingWith, With/Resume/Discard/Fiber/Write, indirect calls, may_unwind fn calls,
  performs other than direct ones (recursing into the spec'd clause) ⇒ may exit. direct_perform
  omits check_unwind when false.
## Nested specialization (decisions/012 deferred item)
- FnGen::passed_spec: in a clone at its own cx (self.cx=="cx", !outlined), direct call to a
  Named fn with body whose effects contain the spec's EffectRef ⇒ same spec clone.
- FnGen::nested_spec: `with` of exactly one HandlerRef literal (no `on`, no ctl ops) at the
  clone's own cx, depth < 3, some clause uses the spec effect (uses_effect scan) ⇒ node gets
  table gm_hl{h}_s{S} of clause clones spec'd S (their cx = hctx = clone's cx = S node), and
  the callee spec = SpecKey{h, eff, outer: Some(S)}. direct_perform calls
  clause_name(clause, key.outer). Adapter bench 1.39 s → 0.34 s (clang 1.02 → 0.17).
## Big values
- gm_out: by_ref (>16 B blob) results: signature `void f(void *gm_out, [env,cx,inst,] …)`
  for every Gamma fn (plain-internal too; needs_wrapper now = any by_ref param or ret, the
  wrapper gm_fN_c keeps the C ABI). fn_ptr_type, call_indirect hidden path, perform clause
  call, direct_perform, decl thunks gm_dt (void + gm_out), gm_entry follow. Epilogue:
  gm_fret: memcpy(gm_out,&F.ret); gm_fdone: return; gm_fprop: return.
- FnGen::expr_into(e, dest) / builds_in_place: Call (via dest_call ptr-identity → call_dest,
  set after args are evaluated, consumed by result_var), Aggregate/Array (all elements to
  temps, then memset if padding, then stores), Block without Defer stmts. Used by Let, Assign
  (dest = *(T*)place), Return (slot, or *gm_out + goto gm_fdone when no_defers & levels 0 &
  !outlined), gen_body. Return of a place with no defers: memmove(gm_out, addr) + gm_fdone.
  Invariant: dest written only after everything in e is evaluated (assignment semantics);
  exits leave it unchanged.
- borrow.rs: Gen::borrowed_params(f): by_ref param, f pure (no effects, Named/Literal w/o
  parent, stores only into own locals, calls only pure fns, no With/Closure/Write/atomics…),
  param not stored/AddrOf/sliced (params are immutable in Gamma anyway). FnGen::local_addr
  maps it to `aN`. bigcopy 3.36 s → 1.90 s (clang 0.13 s: gcc's SRA weak on byte blobs).
## gcc miscompile ⇒ -fno-strict-aliasing
- handler_specialization_nested.gm hung in Fast mode with gcc 13 -O2 (old code too): a slice
  length stored into a temp was dropped (DSE/stack reuse). -fno-strict-aliasing,
  -fno-tree-dse or -fstack-reuse=none fixed it; not reproducible in a small file. Adopted
  -fno-strict-aliasing in cgen::compile (no measurable cost; one bench got 25x faster).
