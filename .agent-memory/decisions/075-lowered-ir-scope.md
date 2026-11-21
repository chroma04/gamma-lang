# 075 — A lowered IR under TIR ("LIR"): scope and plan (s79); DONE s83 (P0–P4; P5 off)

User asks (after 074): "scope out the lowered IR", then "start on the plan with P0 …
we're probably not going to do P5". P5 (LLVM emitter) is OFF the plan. Read 074 first.

## State after P4 (s83, user: "finish up P3 and P4"): PLAN DONE
- cgen translates EVERY body from LIR (cgen/lirc.gm); TIR body translation deleted
  (−2,500 lines: expr/stmt/pattern/chain walkers, chain.gm, GAMMA_CGEN_LIR, Defer.body,
  Observed/DestCall/SpecCall/Spine, read_during_build). `lir_check` turns a leftover
  `Tir` (generic leftovers only) into an Unsup error. Seed refreshed (2f4e4f7).
- Added in s83: In/BitsGet/BitsSet/#bits Aggregate (bits_get/bits_set/bits_pack,
  in_values); vectors (vector_*_value, vector_builtin_values; shuffle lanes from Word bits);
  f16 consts (ratio.f16_value, shared with interp); @fiber (fiber_values); @asm (Str bytes
  kept per temp: LirC.str_bytes); `with … on` (lc_with_on + lc_outline_body: detached C fn,
  `#define F (*gm_Fp)`, outline_level on OpenBlock; Br out of it = start_exit to
  exit_label rung, crosses_defer also counts Br inside an `on` body; Return in it via
  ladder; reads_outer_temp check). Synthetic locals live in the frame when `lir_framed`
  (fiber with, outlines, debug, -g); gen_fn adds them to the frame struct.
- Spec clones from LIR; LIR With picks the clone like TIR's specializable: single_handler,
  spec_candidate (last top-level Call(.Fn) followed only by Store/Br/Move/Load/Return),
  FnGen.spec_next; nested_spec_of for Install(.Lit) tables.
- Long lists (> LONG_LIST 256 insts incl. nested): lc_segments: SEG_WEIGHT 64 C blocks,
  temps read past a segment hoisted (`gm_hN` declared before, copied after def); in
  split_runs functions (long_body + all locals in frame), clean runs (no live temp at
  either end, no outer temp read) of ≥ LIR_MIN_RUN 60 move to helpers via outline_code
  (LIR_RUN_WEIGHT 600); an instruction heavier than a run stays inline (its lists split);
  no nested helpers (jump(label, null) can't express LIR labels); a run holding a loop's
  `continue` block in a function with nested functions stays inline (has_rung_target:
  the loop_scope rung outside jumps to it; difftest seed 14741). `outlines` = long_body
  and has_long_list(LIR). lir_of memoizes the last function (gen_fn + gen_body).
- -g: lc_line emits #line per instruction when the line changes (reset around helpers).
- Large by_ref args: whole-local Load of by_ref is lazy; Call/Write args read in place
  (lc_arg) when the temp is read once (temp_uses); kept lazies dropped after. Without it a
  2 KiB-record recursion used 6217 temp bytes vs TIR 83 (fiber overflow test); now 73.
- Constant args from static storage ported (fold_const_args/lir_const_into/
  site_const_storage); test native_build constant_large_arguments_come_from_static_storage.
- Measured s83 (callgrind, Fast clang, TIR vs LIR before deletion): sha256sum −7.2%,
  perform −5.6, julia −4.2, safe_map_insert −4.0, wordfreq −1.2, others ±0.4. Compiler
  built by LIR: `check compiler` +0.84% Ir (Debug -Og). emit-c compiler: lowering = 5.3%
  of 6.1e9 Ir, lir_body 35%.
- P3 closed: (a) done s82. (b) borrow/purity/steady and (d) may-exit/call-depth/captured
  STAY ON TIR (decided): they are whole-program; LIR is lowered per function as emitted,
  so porting = lowering everything twice (+~5% build) or holding all LIR (memory), with
  no change to the C. Revisit only if TIR bodies are dropped after checking. (c) cgen spec
  ported (above); VM spec DROPPED with numbers: perform costs a call + ~41 Ir (perform.gm
  118.9M vs call.gm 110.7M Ir incl. 31.9M startup, 200k ops); iterate ~613 Ir/element,
  mostly vm_push; specialization keeps the clause call, so ≤ ~7%; per-site cache
  (vm_perform_site) already skips the lookup.
- Possible follow-ups (not planned): statement-level C blocks for stack reuse in short
  lists; build aggregates in place (cgen "build results in place" in next.md); nested
  helpers in split functions; lower irrefutable `for` bindings flat.

## State after P3a (s82)
- compiler/lir/checks.gm `elide_checks` (called at the end of `lower`, only when
  `Lw.bounds > 0`): one in-order walk; Sym = Const | Part(local, generation, offset, ty);
  store = new generation (+ copy propagation of whole-local stores); loops havoc what they
  store at entry (note_stores), compounds havoc what they stored after (log since mark);
  facts (a<b / a<=b, unsigned only) from BrIf, If branches, and past an `if` whose other
  branch `leaves`; proof = plain consts/identity or ≤2 facts chained. Not followed:
  addressed/captured locals; `Tir` = havoc all; defer bodies not looked into (their
  stores havoc). Golden: tests/lir/checks.gm (positive + 7 must-keep cases).
- Pitfall found: eliding a check LOST the SliceLd fusion (check+load = 1 Bc) → slower.
  Added Ins/Bc StepLd/StepSt (unchecked *%p+[i] word access, vm_lir `lc_step` in lc_load/
  lc_store); in vm_run they SHARE the arm with SliceLd/SliceSt (`if i.op is .SliceLd and
  k >= len`): separate arms made gcc -Og's vm_run +1 Ir per dispatch-ish on call/iterate/
  perform (+0.9%, pure layout; -g builds showed no difference).
- Measured (callgrind Ir, same builder, base = the cleanup commit): call −0.9, iterate
  −1.3, list_push −0.5, map_insert −0.3, new_destroy −1.0, perform −0.8, safe_* −0.2..−1.2,
  with −1.6, nbody −3.0, julia −0.9, `for x in xs` micro −2.75, indexed loop micro −0.5;
  life/mandelbrot/sha256sum ±0.1; compile-heavy wordfreq +0.1, json_query +0.4 (pass cost:
  ~26k Ir per function with checks; most is the 224-byte Inst walk).
- Range checks (`xs[i..j]`) not elided yet. The irrefutable `for` binding lowers as
  `block{block{…; br}; no-arm}`; check the selector before changing lowering.

## Earlier states (details in git log of s79–s81)
- P1+P2 (s81): interp compiles every body from LIR (interp/vm_lir.gm + vm_gen.gm); tree
  evaluator, code.gm, vm_compile deleted. Selector: words in registers, larger temps in
  slots (linear scan), generic Gen/GenF ins, sub-codes for defer/with-on bodies, VmLoop per
  TIR loop/with. Register form = zero-extended; LIR Word bits sign-extended (word_bits
  masks). Kept fast paths: Wrap*, ArrayPtrToSlice, same-size coercions, Cast/Addr/ToPtr as
  Mov, RangeChk/Bounds Bc forms, string literals at compile time. Big-endian path
  (!word_locals) tested only by forcing it on x86. interp −5..−13% Ir vs tree era.
- P0 (s79): compiler/lir/ (lir.gm types, lower.gm, verify.gm, dump.gm = `gamma lir`), suite
  `lir` (tests/lir/*.gm goldens, BLESS=1; `lowers/<path>` for every conformance run program
  and example). Places = base + projections (Elem on arrays, Step = pointer arithmetic,
  Payload(off, ty)); loops are `brk: { loop { conds; cont: { body }; step } }`; match =
  block per arm + no-arm; a TIR Block becomes a LIR Block only when it has defers.

## Problem, in lines of code (compiler/, s79)
Function bodies are lowered three times, by hand, from TIR (a typed tree):
- interp: TIR → `Node` tree (code.gm 568) → `Ins` (vm_compile.gm 1976) → `Bc` (bc.gm);
  anything vm_compile does not cover runs on the tree evaluator over `Node` (run.gm 692,
  stmt.gm 319, calls.gm 395, place.gm 236, parts of ops.gm): `with … on`, value loops,
  some `try`/unwrap/assign forms (31 fallback sites, `EvalW`/`EvalSlot`/`Discard`/`Stmt`).
  Body lowering + tree evaluator ≈ 4.2k lines; the VM (vm.gm 2011, bc.gm 603) consumes it.
- cgen: walks TIR straight to C text. Body structure ≈ 5.5k lines: func 1333, expr 1435,
  calls 758, patterns 225, chain 124, outline 298, borrow 365, spec 125, fiber 216,
  arith 277, bits 66, constarg 113, fngen 169.
- eval/ (2.1k): compile-time tree walker on TIR with structured values; stays as is.
Analyses each back end does privately: cgen borrow.gm (pure, steady, read_during_build),
`Observed`/`builds_in_place`, `call_depth`/`probe_free`, `clause_may_exit`, captured /
frame locals, handler specialization (spec.gm); interp address-taken registers, frameless
bodies, slot allocation. No pass anywhere hoists or removes checks (spec D.7 "should";
the interpreter checks every index today; native relies on gcc/clang).

## Goal
One lowering of a checked, monomorphic function body (TIR as given to cgen after
prepare_codegen; instances are already separate FnDefs) into LIR, shared by the VM and
cgen, with analyses and optimizations done once on LIR. Success = the tree evaluator and
code.gm deleted, vm_compile mostly deleted, cgen body code roughly halved, interp no slower
(callgrind Ir on bench/programs + 064's set), native C no slower, compiler `emit-c` no
slower. An LLVM text emitter (074) becomes optional follow-up work on top.
Non-goals: replacing eval/; changing Ins/Bc (the VM's tuned encodings stay, LIR → Ins is
a thin selector); changing C value representation (typed structs/blobs/mirrors stay).

## Design choices (decided here, revisit with evidence)
1. STRUCTURED, not a flat CFG. Blocks/loops/`if`/`switch` regions with labelled breaks
   (Wasm-like) + explicit cleanup. Reasons: outline.gm and chain.gm split by statement
   runs and must keep working for gcc; C stays readable structured code; vm_compile
   already lowers structure to jumps; structured → LLVM CFG is mechanical; the analyses
   wanted (loop ranges for checks, purity, may-exit) are easy on structured loops.
2. Values: mutable LOCALS (as today: frame offsets, address-taken flag, captured flag)
   + single-assignment TEMPS inside a statement. No SSA/phi: C compilers and LLVM
   (mem2reg) do that; the VM's register file is locals + temps already.
3. Layout-level, typed. Every value keeps its `Ty`; places are explicit (local, upvar
   levels, deref, field at offset, index with element size, global); word vs slot is a
   property of the type (the VM's distinction, valid for C too). Target layout from
   sema/layout.gm as both back ends use now.
4. Everything implicit becomes an op:
   - checks: `Check(kind, operands, span)` for bounds, overflow (via checked arith ops),
     unwrap, cast alignment, shift range, continuation rules; Fast mode drops them at
     lowering, except division (§18.1).
   - exits: calls carry `may_exit` (from clause_may_exit / effect sets, D.2); a scope has
     its cleanup (defer/errdefer list); `break/continue/return/try` across functions are
     `Exit(kind, target levels)`. cgen maps to ladder + TLS test, VM to vm_raise/Flow.
   - effects: `Perform(effect, op, args, may_exit)`, `With(label){install…; body;
     restore}`, `CxSave/CxRestore` (VM already has these), handler values explicit.
   - `with … on`: body outlined to its own LIR function (cgen does this already; the VM
     would get it too), plus `FiberRun/Resume/Cancel` ops.
   - patterns: lowered to tag/flag/const tests and branches (both back ends do this).
   - writes (`print`, `@write`): `Write(sink, template ref, args)`; template tables
     (073) are a constant pool entry.
5. Lowered per function, lazily: the VM lowers at first call (startup of repl/test
   matters), cgen lowers all reachable functions. LIR must be cheap: arena-allocated,
   no per-node hash maps; budget = no slower than cgen's current body walk.
6. Passes on LIR, in order of value: (a) check elimination: `for i in 0..xs.len {xs[i]}`
   and induction-variable bounds (benefits the interp most); (b) borrow/purity/steady
   (port borrow.gm); (c) handler specialization (port spec.gm; the VM's `for` over an
   iterator then calls the clause directly); (d) may-exit / call-depth facts; (e) later,
   small-function inlining for the VM only if measured.

## Plan (each phase lands green on main; measure Ir before/after)
P0 (1–2 sessions): compiler/lir/ module: data types, a verifier, `gamma lir` dump (like
   `gamma tir`), lowering for scalars, locals, places, calls, if/loop/match, defer,
   returns. Unit tests + dump tests in tests/harness. No consumer yet.
P1 (2–3): VM consumes LIR: new selector LIR → Ins for the covered subset; uncovered
   functions take the old path whole (per-function switch, not per subtree, so the two
   lowerings never mix inside a body). Grow coverage to all of TIR, including the forms
   the tree evaluator runs today. Gate: conformance + examples + repl suites, Ir table.
P2 (1): delete the tree evaluator, code.gm Node, old vm_compile paths. Check the
   playground (wasm32 interp) builds and runs.
P3 (1–2): passes (a)–(c) above; interp gains measured.
P4 (3–4): cgen emits function bodies from LIR behind `GAMMA_CGEN_LIR=1`, per function,
   until all of conformance/examples/compiler self-build agree; then make it default,
   re-express outline/chain on LIR statement runs, delete the old body code. Refresh the
   seed (tools/refresh-seed.sh). Watch the C-size/compile-time guards (GM_LONG_FN,
   20,000-temp HUGE_FN, 73's template sharing).
P5 (optional, 2–3): LLVM textual IR emitter from LIR, `--backend=llvm`, clang required,
   experimental; compare with C via clang (074 says expect 0–5% run time, 15–45% build).
Total ≈ 10–15 sessions for P0–P4. Each phase is useful alone: P0–P3 already remove the
interp's second executor and add check elimination.

## Risks and how to handle them
- Differential testing weakens: today interp and cgen lower independently, so a lowering
  bug shows as a mismatch; with shared LIR both are wrong the same way. Mitigation: keep
  the LIR verifier strict; extend tools/difftest with eval/ (an independent TIR walker)
  as an oracle for the programs it can run; keep spec-derived expected outputs in
  conformance (they are absolute, not relative).
- VM speed: today's Ins come from a tuned compiler (register reuse, operand read order,
  consts as registers, frameless bodies). The selector must reproduce these; P1 gate is
  "no Ir regression on any bench program".
- cgen C shape: the C compilers' superlinear passes (architecture.md) were tamed by C
  shape (statement runs, accumulators, stubs). P4 must keep the same shapes; measure the
  10,000-statement `main` and the chain tests.
- Big-endian (s390x) and 32-bit targets: the VM's word-local trick is LE-only today;
  keep that a selector decision, not a LIR one.
- Seed churn: P4 changes all generated C; do it in one PR with the seed refresh.

## Questions for the user
- Answered s79: P5 not wanted; the detour (roadmap paused) is fine.
