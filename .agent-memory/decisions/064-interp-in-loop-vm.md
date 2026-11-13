# 064 — Interpreter redesign: Bc forms, in-loop calls, page memory (s67)

User ask: "work on interpreter performance; shoot for global minima, consider different
fundamental designs". Branch claude/optimistic-curie-kxu2yf. Wall time here is ±30% noisy:
compare callgrind Ir (scratch `ir.sh BIN...` table idea: loop2, fib21, expr, bench/programs).

## Results (Ir in M incl. ~70M startup; start → end)
loop2 555→325, expr 2737→951, iterate 393→290, call 232→143, list_push 919→352,
map_insert 2039→756, new_destroy 1102→515. Wall: mapb (400k hash-map ops) 2.41→0.62 s,
sieve 0.56→0.23, std_text_utf8 1.95→1.07, example4_regex 0.54→0.23. fib call 754→342 Ir.
conformance+examples+repl suites 75 s → 39 s.

## Design (compiler/interp)
- memory.gm: PAGE TABLE. Address space in 4096-address pages; a page = slots of one
  power-of-two class (16..2048, slot ≥ size+1 and ≥ align); bigger allocations get a run of
  pages sharing one Slot (`shift` LARGE=63). `view(addr,len,write)` = page lookup + shift,
  returns host pointer or null; `fault()` computes the precise MemError (same order as
  before). Data still malloc'd per allocation. Fully dead pages are retired FIFO; beyond
  65536 tombstones the oldest page's slots are dropped (then "unmapped"). Works for 32-bit
  targets (playground target is wasm32!). Found: globals with initial value ignored
  `#align` (cell_addr now takes the alignment).
- bc.gm: `Bc` 16 bytes {op: BcOp, f: u8, w: u16, a, b, c: u32}, ONE PER `Ins` AT THE SAME
  INDEX (jump targets shared). `bc_of` specializes on Ins data; fast arm does only the
  common case and `break`s out of the inner loop otherwise; then vm_free/vm_free_other
  (frame-free Ins, every case + traps) or vm_framed (needs Frame) run the `Ins`. Ops that
  read frame/memory bytes as LE words only if `m.word_locals` (else Free): BE hosts/targets
  run generic.
- vm_run: outer loop (generic + flows) / inner dispatch loop. Loop keeps only regs, bc, pc,
  cur (C registers; spills cost ~10 Ir/instr before). `CallRec` = one record per running
  body (base one on the host stack; callee records in scratch below the callee store):
  prev, code, regs, dst, fr, cx, base_cx, pc, f, ret_reg, buf, mark, tm.
- CALLS IN THE LOOP for every compiled body (vm_push): `CallSite` fast path for direct
  calls of frameless bodies (state 0/1/2, moves list, worked out at first run); vm_push_rec
  → vm_push_fn general (Call / Perform of Lit or Decl clause / CallValue), creating the
  Frame for bodies with Framed instructions (eager, as enter does incl. addressed params).
  Only when single thread, off fibers, depth ok; else vm_call/vm_invoke/enter (host
  recursion) as before. vm_ret (return; framed: vm_return, kill temps, end_frame),
  vm_raise (flow: vm_flow per framed body, Return(frame id) ends its call, trace push).
- FRAMELESS bodies (`Code.frameless`: no Framed instruction): addressed locals keep their
  allocation address in hidden registers after the others (`Code.hidden`,
  vm_hidden_addr/vm_drop_locals at every exit); captured frame via `Code.env_reg`
  (set at base entry from fr.env and by vm_push_fn); Up/UpSet fast ops for level 1.
- vm_compile coverage added: non-word copies (vc_copies/vc_copy_place, MemGet/MemPut,
  Assign of built values to places), Try (vc_try: levels 0, builds the error return in a
  slot → RetSlot; FnInfo.ret_ty), Unwrap (UnwrapTrap), Builtin generic instr
  (vc_builtin_ok: all that ev_builtin evaluates by value, ≤ 6 args; vm_builtin) with
  Cast/Addr/ToPtr/BlackBox as moves (+AlignCheck), Assert (AssertFail), Coerce
  PtrToPtr/TupleRename/FnPtrWiden as words, small aggregates as words via slot
  (vc_word_via_slot), Str literal (StrSlot), Div/Rem, Shr/Shl, SliceMake fast.
- cgen func.gm `probe_free`: no GM_STACK_CHECK in an `#inline` function (optimized builds)
  whose calls are all direct calls of non-#inline functions (each inlined helper repeated
  the probe: %fs load + cmp + branch). Needs two-stage build to show.

## Gotchas
- Scratch `stat.py`/`stat.sh` idea: temporary `m.stat(name)` HashMap counters at the
  generic-path entry (by Ins kind, and by node op / coercion kind / builtin for tree
  instrs) → histogram of what leaves the loop. This drove all coverage work; redo it for
  new workloads before guessing.
- A `match` on a field-less union IS a C switch (the switch line is thousands of chars;
  easy to miss when reading the C).
- `x else break` inside the match inside the inner `while` = "run this instruction the
  general way". `x = e else f => { cur = try ...; cur.pc }` works (block value).
- `---` needs plain data: arrays of slices use `[N]T(fill: v)`.
- int → pointer: `us(x).to_ptr(^T)` (optional); pointer → int: `.addr()`.
- tools/test.sh rebuilds target/bootstrap/gamma in place: keep a copy of the baseline
  binary in scratch for comparisons before the first rebuild.
- `gamma run --backend=interp compiler …` silently runs natively (foreign code).

## Not done / next candidates (measured)
- Push of a frameless direct call still ~190 Ir (record 13 fields, memset of the store,
  moves, consts copy); return ~30. Ideas: no memset (zero only hidden/env regs and what
  vm_compile cannot prove written), consts as immediates (K is cheap now), smaller record.
- Dispatch ~19 Ir/instr: head 10 (index→pointer shl/add, range check, table jump). An
  instruction-pointer variable instead of pc, superinstructions (compare+branch at loop
  bottom), would shave 20-30%.
- Builtin generic (~150 Ir): Hash, CopyElems, HeapAlloc/Free/AllocTag are the hot ones.
- Addressed locals: each first use = malloc + slot + kill (mapb: 2.5 per map op). A
  per-body stack arena for Stack allocations (inline data in the page) would remove it.
- Framed bodies: `defer` (Defer/DeferHeight/Unwind), `with` (WithLit/WithDecl/CxSave/
  CxRestore), Closure/HandlerRef values still need a Frame; lazy frames (only when the
  instruction runs) need Frame.mem to alias the hidden registers.
- ROOT_STACK 4 GB calloc could shrink now that calls rarely recurse on the host stack
  (tree evaluator and non-loop calls still do).
