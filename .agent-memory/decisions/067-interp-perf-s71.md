# 067 — Interpreter performance round (s71)

User ask: "continue with interpreter performance" (after 064). Branch claude/magical-cori-ruv0uq.

## Measuring (scratch recipe; scratch is wiped between sessions)
- `ir.sh BIN...`: callgrind Ir per program, 4 in parallel: loop2/fib micro, bench/programs/*,
  conformance example4_num_primes, example4_regex, std_text_utf8 (GAMMA_STD/GAMMA_RUNTIME
  must point at the tree: a binary outside target/ cannot find lib/).
- Build variants with the baseline binary + target/bootstrap/cache (~30 s incremental).
  cgen changes need a two-stage build to show.
- Asm-level: `valgrind --dump-instr=yes`, parse `fn=`/`cfn=` names and `calls=` lines
  (skip the line after), join with objdump of gm_f_vm_run_*; with a `-g` build, addr2line
  → max count per vm.gm line = per-op execution histogram.

## Done (Ir M, start → end: utf8 13478→~11400, regex 2786→2429, iterate 296→192,
## perform 261→157, with 518→485, loop2 156→142, primes 7176→5143)
- Loop dispatches through `ip: [^]Bc` (bc_at / bc_pc in bc.gm), pc only computed for the
  slow path and calls. Head went 11 → 7 x86 instructions, ip in a callee-saved register.
- cgen + interp: `p.cast(P)` skips the alignment check when the source pointee is at least
  as aligned (each bc_at had a `test $3` + branch).
- WithLit with at = NO_REG for a HandlerRef literal: installs (id, fr, cx) directly.
- vc_branch: `and`/`or`/`not` conditions compile to jump chains (cmp_opposite for
  integer compares; floats fall back to Jnz).
- CallSite for Perform (state 3, keyed by cx + CtxNode.gen; same clause f → only env /
  callee_cx / self_ updated) and CallValue (state 4, keyed by f, `alt` second entry
  swapped in: regex alternates two closures per site). Handler decls: self_ copied to
  param 0 (self_off). Failures → state 2 (perform) or gen=1 memo (value).
- UpvarSlot (free) and ClosureSlot (framed) instead of EvalSlot tree runs.
- BinSlot (operands to slots, binop; `wide` 1/2 = u128/i128 fast path wide_op on LE,
  overflow/div0/shifts → generic binop), WidenSlot (IntWiden word → 16 bytes);
  vc_builds lists Binary/Closure/Upvar so lets/assigns of them avoid Stmt.
- Tests: conformance/run/call_sites_varying.gm, int128_mixed_ops.gm.

## Tried, rejected
- Word loops instead of fill/copy_from in vm_push_site: +10% Ir on utf8 (GCC).
- Covering all 256 u8 cases to drop the switch range check: GCC folds them back.
- `default: __builtin_unreachable` for exhaustive matches would drop 2 Ir/dispatch but
  loses the checked-mode trap; not done.

## Open candidates
- Exact op histogram: apply scratch stat.patch idea (m.opc[op.tag()] in the loop, eprint
  in run_entry; pairs via prevop). utf8: dispatch ~10 Ir of ~24 per instruction.
- Loop rotation (Loop+cond pair 12M/325M in utf8) needs a thread check on back edges.
- vm_push_fn for framed callees ~440 Ir self (memset of store, new_frame); bodies are
  framed by ClosureSlot/defer/with.
- primes now: vm_builtin generic (u128 `.trunc`), shifts/compares of u128 still generic.
- Builtins (hash, heap alloc) in map_insert/new_destroy via vm_builtin generic path.
