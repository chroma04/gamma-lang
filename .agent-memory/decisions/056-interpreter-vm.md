# 056 — Interpreter performance: lowered code + register machine (s52)

User ask: "work on interpreter performance; may take significant redesign".
Branch claude/interp-vm. Results (4 cores, `gamma run --backend=interp`, incl. compile):
std_text_utf8 42.6 → 6.0 s, std_parse_float_table 12.7 → 1.9, example4_regex 2.75 → 0.75;
bench/programs + scratch benches 3–7x (call 0.22 → 0.058, sieve 0.70 → 0.10, structs
2.1 → 0.5). Full harness -j3 ~135 s. `check compiler` ~10% faster (cgen changes).

## Design (compiler/interp)
- code.gm: TIR → `Node` (op, ty, size, span, place) lowered on first call in `fn_info`
  (FnInfo.body). Resolved: local offsets, field offsets/packed, index SeqKind/esz/len,
  pattern variant tags (tag_w word), word ops: IntOp/Cmp/EqW/Not/IntUn/IntConv/FloatOp for
  `IntW` (8/16/32/64 bits exactly), f32/f64, bool, pointers. `Op.Word` = constant ≤ 8 B.
- run.gm: tree evaluator over Nodes. `ev(dst)` bytes, `ew` → u64 word ("integer of its
  bytes in target order", rd_word/wr_word). ev handles word ops via ew; ew falls back to
  ev with an [8]u8 (ew_slow). ev_slow is EXHAUSTIVE (compiler checks coverage).
- vm.gm: per-function `Code` compiled from Nodes (FnInfo.vm; frame_size = locals + slots +
  8*nregs). Registers are a stack allocated at compile time (an expression leaves its value
  in the first free reg; `vc_set_top` keeps max). Slots (non-word temps: match scrutinee,
  call args/results) after locals. Instructions: word ops, LocalAt/SetLocalAt/Load/Store
  /SliceIdx/ArrIdx/PtrIdx (places), Call (Arg Reg|Slot, Dest None|Reg|Slot|Ret), Jmp/Jz,
  Pat (tree match_pat on slot), EvalW/EvalSlot/Discard/Stmt/AssignW (tree fallbacks),
  Mark/Release (scratch per statement that may use it), PushLabel/PopLabels, defers:
  DeferHeight/Defer/Unwind with Frame.defers stack (vm_unwind; vm_return runs errdefers
  if result is_err; vm_flow: break/continue to a VM loop → unwind + jump, else unwind with
  errdefers_run and propagate; Trap → no defers).
- Register file and instruction array via many-pointers (unchecked; indices valid by
  construction). nregs ≥ 1.
- Loops in VM only if unit-typed (break carries no value); value loops → tree.
- Tree constructs never contain VM code; a VM body is the whole function.

## Codegen changes (compiler/cgen), benefit all Gamma code
- calls.gm `scrutinee_addr`: match/cond patterns test a Blob place in place (no copy) when
  no arm has a guard (copying the 96-byte interp `Op` twice per dispatch was the top cost).
- calls.gm `match_switch`: ≥3 arms on a union → `switch(tag)` to first admitting arm.
- units.gm: `#inline` (always_inline) functions become C99 inline definitions in the shared
  header + `extern` decl in unit 0 (address identity kept). Small auto-`inline` functions
  NOT shared (20k defs doubled build time for 5%).
- Needs the two-stage build to see effects (old gamma builds g_a with new cgen; g_a builds
  final). Seed refresh not required (no new intrinsics).

## Gotchas found
- `?x` needed when binding optional tuple payloads (`.Let(l, ?init)`).
- A line starting with `.Variant(` continues the previous line (§2.8): write `Op.X(...)`.
- `make(T, n)` needs Plain(T); pointer element slices via list().items.
- Gamma has no overloading: VM compiler methods are `vc_*` (names collided with run.gm).
- `cont` is a reserved type name (can't be a local).
- wasm32 host (web playground CI, pages.yml): heap u8 chunks are only 4-aligned → `tmp`
  aligns by absolute address (registers are read as u64 via cast). Reproduce locally:
  apt wasi-libc libclang-rt-18-dev-wasm32 binaryen; CC="clang-18 --target=wasm32-wasi"
  web/play/build.sh; node web/check.mjs <wasm> <libs.json> [--one run file.gm].
- Editing sources while the harness runs breaks selfhost fixed-point/unit tests (transient).

## Round 2 (s53, branch claude/interp-perf-2)
- cgen func.gm: frame-locals rule is now `debug or debug_info or outlines(def)` (was
  long_body); outline.gm `runs_of`/`outlines`/`block_outlines` share the run split with
  stmt_list. The vm loop (long match, short blocks) now keeps locals in C vars.
- Checker bug found: capturing nested fn calling itself through a literal written in it
  (fnlit.gm `check_no_self_reference` now scans cx.nested from `nested_start`).
- VM instrs: CallValue, UpvarAt/SetUpvarAt (`root_levels`), Arith64/Arith32 (wrap/signed
  flags; `arith_kind`), JCmp/JEq fused branches, LocalAtD/SetLocalAtD + ScaleIdx (array
  locals indexed), Bit/Shr, AddR, CopySlot/SlotW/WSlot/JSlot, SetLocalSlot.
- `else`: vc_else_shape (Optional nullable-ptr flag or Result is_err at offs[0], payload
  offs[1]; only when n.ty is the payload type), vc_else; slot_worthy (Call/CallValue/Else)
  values go via slots into locals. vc_expr .Reg on a word that is Return/Break/Continue
  must not call vc_word (infinite recursion).
- Frame handles are lazy (`frame_handle`); `tmp` chunks size.max(CHUNK)+8.
- Results: utf8 6.0 → 4.3 s, parse_float_table 1.9 → 0.73, regex 0.75 → 0.64.

## Round 3 (s54, branch claude/interp-perf-3)
- Ins.Perform (vm_perform: Lit/Decl clause → vm_invoke with installing/is_clause_lit/self_;
  others via argv + m.perform). VM patterns (vc_pat/vc_pat_ok: Wild/Bind/Const int-bool/
  Null/Some/Ok/Err/Variant tag<=8/Fields of plain struct/tuple; JTag instr). vc_to_slot
  takes any word via vc_word+WSlot.
- WORD LOCALS AS REGISTERS: make_info gives non-addressed locals of 1..8 B an 8-aligned
  8-byte slot; regs pointer = fr.store base; register r = word r of the store; locals'
  reg = offset/8 (vc_local_reg, only if m.word_locals = target little && host little).
  Temps start at rbase (after slots); vm_compile runs twice when slots exist (rbase
  depends on slot_max). vc_operand(n, late): local's reg used directly only if later
  operands are vc_reads_only (eval order!). vc_word_to + vc_retarget (rewrite last
  instr's dest; refused if c.joined >= len, i.e. a jump lands after it).
  Invariant relied on: register values are canonical (only low size*8 bits set).
- Hot-path: no `enumerate` (iterator = handler call per element in C!), node by pointer,
  SetLocalSlot fast path, same_ref before cmp_ref in find_ctx, #inline vm_release.
- Results (g20 → g23): iterate 0.34 → 0.18, perform 0.27 → 0.15, utf8 4.34 → 3.47,
  parse_float 0.73 → 0.54, regex 0.62 → 0.55.
- Profiling recipe: scratchpad names.sh/names2.py (map gm_fN via `-g --emit-c`, numbering
  same as non-g build), cgdiff.py (per-iteration diff of two N), cases.py (cost per Ins
  variant from a -O2 -g build: GAMMA_C_UNITS=6 --emit-c, cc each unit with -g).
- FOUND: vm_run (and 2 others) > LONG_FN_NAMES (2000) → optnone on clang>=19/Apple 17.
  FIXED for vm_run: cold instrs in #noinline vm_slow (`_ =>` arm), fast paths as #inline
  helpers (arith64/32, shl/shr_fast, st_rd/st_wr, conv_word). KEEP vm_run < 2000 names
  (scratchpad vmnames.py on `--emit-c`; was ~1840 at bb32ec8). Tick must stay in vm_run
  (moving it cost 10%). cgen expr_node + check builtin@arith.gm still LONG_FN (compiler
  speed on macOS; not fixed).
- More (s54): capture-address analysis (frame.gm mark_captured_addressed via
  nested_head index of def parents) — needed because word-local registers assume
  mem!=0 ⇒ addressed; LocalAt/SetLocalAt(D) now check nothing (addressed locals via
  LocalAddr + Load/Store); slot building (Want.Slot, vc_builds, ConstSlot/ZeroSlot/
  SliceMake/RetSlot); mark elision (leaks counters; Nop + vc_compact remaps targets);
  constant registers (2-pass, Code.consts bytes copied at entry); 8-byte fields of
  locals as registers (vc_place_reg/vc_slice_regs); SliceLd/SliceSt fused; loops: no
  PushLabel/PopLabels (tree Break of size-0 value skips label_dst), DeferHeight only
  with defers, Loop(head) back-edge does the tick.
- Throwaway instruction histogram: scratchpad instr.sh builds gcount (counts by kind).
- utf8 4.34 → 2.14 s; parse_float 0.73 → 0.38.

## Not done / next candidates
- `with` still tree (per `with` ~2000 instrs: Discard → ev → with_ → call_node). Needs a
  mutable cx in vm_run + ctx popping on every exit (normal, break/continue Jmp, flows).
- Value loops still tree: deliberately skipped (4 valued `break`s in lib/examples).
- Call path ~1100 instrs/call (vm_invoke, enter, new/end_frame, tmp); further gains need
  frames without Frame objects.
- Prelude generics (copy_from, fill, push, pop) are not inlined across split C units
  (~500 instrs/code point in utf8): candidate `#inline` hints in lib/core (measure build
  time). Two other compiler functions are GM_LONG_FN (cgen expr_node, builtin).
