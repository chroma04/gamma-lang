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

## Not done / next candidates
- Slices copied through temps with &t loads in Gamma's C.
- VM: Perform still tree (EvalW) — the utf8 hot path is `for r in runes(e)` (perform +
  With); value loops; With; LetTuple/Assign of non-words via Stmt.
- Mark/Release per statement; call path (enter/new_frame/end_frame ~575 instrs/call).
