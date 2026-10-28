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
- Editing sources while the harness runs breaks selfhost fixed-point/unit tests (transient).

## Not done / next candidates
- Per-op cost is dominated by Gamma's C (frame struct F in memory: GM_STACK_CHECK(&F)
  takes its address; slices copied through temps with &t loads). A cgen change keeping
  locals in C variables would speed everything (big, risky).
- VM: Perform/CallValue still tree (EvalW); value loops; With; non-word Let via Stmt.
- Superinstructions (compare-and-branch, ops on locals) not done.
