# 057 — Performance round 4 (s55, branch claude/perf-round-4)

User ask: `with` in the VM, cheaper calls, `#inline` on prelude functions (measure build
time first), split the two GM_LONG_FN compiler functions. Numbers: callgrind Ir of
`gamma run --backend=interp` (bench/programs; scratch nocall.gm = call.gm minus the call).

## Results (start → end)
- `with` entry ~4,400 → ~650 instrs; with.gm 1345M → 597M; a loop inside a `with` ran in
  the tree before (1399M → 585M for 200k performs inside one `with`).
- call ~1,180 → ~627 instrs/call (call.gm 382M → 259M); perform.gm 466M → 372M,
  iterate.gm 504M → 437M; parse_float_table 6295M → 5543M (-12%, mostly the outline fix).
- No GM_LONG_FN function left in the compiler's C.

## What changed
- cgen expr.gm/arith.gm: big match arms of `expr_node` and `builtin` → helper functions
  (`write`, `else_expr`, `return_expr`, `atomic`, `pow`, …). Script-extracted; a free
  name `loc` silently bound to the method `loc` (see pain-points) — check helpers for
  locals of the old function (`t`, `loc`, `tys`, `v0`, `at`).
- VM `with` (vm.gm `vc_with`): CxSave(r) / WithLit / WithDecl / body into a value slot /
  CxRestore(r). The `with` is an entry of `Code.loops` (`is_with`, value slot `at`/`size`):
  vm_flow's Break to its label jumps to its CxRestore; `label_dst` (run.gm) falls back to
  that slot, so no LabelDst push. Loops got a `cx` register (CxSave before head, Nop'd if
  no `with` inside, `NO_CX`); `vc_leave` emits Unwind + CxRestore(loop.cx) before compiled
  break/continue jumps that cross `with`s. vm_run keeps `act` (current Act); vm_slow and
  vm_flow take `^Act` + `base` (the body's cx); leaving the body pops to base (vm_flow,
  vm_return after defers). Deferred got `cx` (each defer body runs in its push context).
  WithDecl's value lives in the frame slot (not heap-copied; slot reserved for the body).
  `&local` (AddrOf of addressed local/Mem place) compiled (`vc_addressable`).
  `with … on` stays tree. handler_decl/handler_lit return pointers (were struct copies).
- Calls: `vm_call` is a fused fast path (vm_invoke + enter) for: info.vm present, no
  addressed params, arity ok, cost ok, depth/stack ok, not on a fiber, one thread; else
  vm_invoke. Shared #inline helpers vm_put_args/vm_direct/vm_dest. fn_info, tmp,
  new_frame, end_frame, local_addr are #inline fast paths with #noinline slow parts
  (make_fn_info, tmp_next, alloc_frame/grow_mem_buf, end_frame_slow, local_alloc).
  Frame pool = intrusive list (Frame.next_free, Machine.pooled). vm_run: one exit after
  the loop (Ret* `break`), inline "no defers and same cx" check; consts copied by words.
- cgen outline.gm `runs_of`: a block is split only if its SMALL statements (≤ RUN_WEIGHT)
  total > RUN_WEIGHT. Before, vm_run's 6 prologue statements were outlined (beside the
  huge loop), which forced ALL its locals (pc, ins) into the frame struct (`outlines`).
  Check with `grep -o "int gm_c[0-9]*_" *.c` on a build: only gm_c392 (a long fn) left.
- core: `#inline` on slice fill/copy_from and list pop; `fill` writes through `s.ptr`
  (no bounds checks → memset in Checked builds). `push` NOT #inline: measured all four
  +11% compiler build time (45.7 → 50.8 s) for -1.8% Ir of `check compiler`; without push
  build time unchanged, -1.2% check, -2.9% utf8, -4.3% call. push now calls regrow
  directly when full (no reserve call).

## Budget / gotchas
- vm_run ≈ 1,894 names (limit 2000); interp `builtin@ops.gm` (eval) ≈ 2,000 (unchanged,
  at the edge). Measure: max fresh-number per C function (scratch fnsizes.py idea:
  regex `\b(t|ie|ix|lb|lc|…)(\d+)\b` per function body).
- `GM_LONG_FN` grep must allow parens: `grep '^[^#;]*GM_LONG_FN[^;]*{$'`.
- `-g` builds keep all locals in frame structs: profiles of small functions are
  distorted. For line-level costs compile the non-g `--emit-c`/`-o` C units with
  `cc <BASE_CFLAGS> -O2 -g` (runtime: `#define GM_RT_SEPARATE 1` + include gm_rt.h and
  gm_rt.c), link -lm -pthread, run under callgrind --dump-instr=yes.
- Function-level self-cost diff of two programs (callgrind_annotate --inclusive=no
  --auto=no, names from a -g build's #line → `name ::`) isolates per-feature costs.

## Tried and reverted: calls inside one vm_run loop
- VmState (act, base, code, dst, call, pending) + VmCall record (caller fields, pc, dest,
  mark) after the callee's store in scratch; vm_push/vm_pop; `Leave` as every body's last
  instruction for flows to an in-loop caller. Correct (after reading args BEFORE zeroing
  the direct result register — vm_direct clobbers arg 0, which shares the register), but
  call ~731 instrs vs ~627 recursive, and the call-free dispatch loop +2.7% (`vs` fields
  in memory, nested loop). The bookkeeping in Checked Gamma C (frame fields, scratch,
  record writes: ~5-15 instrs per Gamma statement) costs as much as the C prologue and
  Flow!() copies it saves. Not worth it unless the per-call bookkeeping itself shrinks.

## Not done / next
- Call ~627: left are prologues + GM_STACK_CHECK of vm_call/vm_run (16 KB frame), Flow!()
  result copies, frame field setup (new_frame ~15 stores), scratch tmp + zeroing. A
  cheaper call needs less per-call state (e.g. no Frame object for leaf VM bodies).
- `with … on` and value loops still tree.
