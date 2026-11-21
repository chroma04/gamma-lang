# 075 — A lowered IR under TIR ("LIR"): scope and plan (s79); CURRENT, P0–P3a + P4a done

User asks (after 074): "scope out the lowered IR", then "start on the plan with P0 …
we're probably not going to do P5". P5 (LLVM emitter) is OFF the plan. Read 074 first.

## State after P4a (s82, user: "go ahead with the rest of P3 and P4")
- cgen/lirc.gm: bodies from LIR when `Options.lir` (driver: env GAMMA_CGEN_LIR=1|report)
  and `lir_unsupported` is null; else TIR, per function (gen_body in func.gm picks; extends
  in_frame with false for synthetic locals; spec clones, split_runs (long bodies), -g and
  Clause defs stay TIR). CI ubuntu job runs conformance/ examples/ selfhost/ with the flag.
- Coverage: conformance run + examples 8285/9598 bodies; compiler self-build 8218/9110.
  Fallback reasons (count, self-build): nested functions 491 (has_nested: a fn that is a
  parent — its loops/withs could be exit targets; needs Loop/With ladder rungs), test of
  parts 203 (In/BitsGet/BitsSet), defer 77, 128-bit consts 58, builtins 28 (Sys, Get…,
  atomics, Fiber, Asm), aggregate compare 21 (Bin < on blobs), effects/exits 14.
- Gates passed: full tools/test.sh with the flag; LIR-built compiler emits byte-identical
  C to the TIR-built one; default-path C unchanged vs main (refactors are pure).
- Shared helpers now take values not ^Expr: int_binary(span,t,..), unary_value,
  coerce_value, builtin_values (atomics stay in `builtin`), call_fn(rt, e?, f, vals),
  perform(span,..), write_values/write_table_values + WriteTo sink, global_addr,
  convert/from_tag/bit_count/pow(span,t,..), rotate/black_box(t,..).
- lirc design: temps → C temps; labels → gm_lbN + goto; Loop = for(;;); lazies (Lazy.Load
  of by_ref types, Lazy.Variant) computed on first value use, flushed before any
  non-`reads_only` op (except the value a Store/Return reads) and before nested lists,
  dropped at list end; Eq with a lazy variant = tag (+payload) compare in place; Store of
  a lazy load = memmove; Return of large = memmove to gm_out + goto gm_fdone; Call(d) then
  Return(d)/Store(local,d) = call writes the destination (fg.dest_call_lir → call_dest;
  foreign calls don't take it: `dest_taken`). Store(p, Uninit) = memset in place (a 20 KB
  `---` array copied through a temp overflowed a fiber stack).
- Measured (callgrind `check compiler`, Debug builds of the compiler by the same
  builder): LIR-built 2.002e9 vs TIR-built 1.988e9 Ir (+0.7%; was +11.8% before lazies,
  +3.75% before result destinations); the rest is mostly gcc inlining shifts (`inline`
  hint at ≤64 names; LIR bodies use more names). C text −0.5%. Fast/clang not measured.
- Next for P4: Loop/With scopes + ladder for nested-function parents; Defer (segments);
  With/Install/Exit/Resume/Cancel; In/Bits; 128-bit; outline/chain on LIR statement runs
  (long bodies); spec clones (P3c); -g line directives; then default on, delete TIR body
  code, refresh seed. P3b (borrow/purity) is still computed on TIR for the prologue — port
  when the TIR path goes.
- Lowering wart seen in C: `for i in` binds through a match block with a `no-arm` trap
  after it (block{block{…;br}; no-arm}); harmless, could lower irrefutable bindings flat.

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
- P3 left: (b) borrow/purity/steady and (d) may-exit only matter for cgen → do with P4;
  (c) handler specialization for the VM (`for` over an iterator calls the clause directly)
  is the remaining VM win, bigger work. Range checks (`xs[i..j]`) not elided yet. The
  irrefutable `for` binding lowers as `block{block{…; br}; no-arm}` (a jump per iteration
  in the VM?) — check the selector before changing lowering.

## State after P1 + P2 (s81)
- The interpreter compiles EVERY body from LIR (interp/vm_lir.gm; no switch any more).
  Deleted: code.gm (TIR→Node), vm_compile.gm, run.gm/stmt.gm (tree evaluator), the tree
  parts of calls/place/ops/fiber/sys, Frame.labels, and Ins EvalW/EvalSlot/Discard/Stmt/
  AssignW/Pat/RetEval/Defer(Node)/BinSlot/WidenSlot/ScaleIdx/SliceIdx/ArrIdx/JSlot/JTag/
  Mark/Release/Nop/... (with their Bc forms). node.gm keeps a descriptor `Node` (ty, size,
  span; op only Builtin/Perform/Str/CStr/Leftover for vm_builtin/vm_perform/StrSlot).
- Lowering now total for monomorphic bodies (suite `lir` fails on any `tir` line): new LIR
  ops With/Install (handlers installed in a With's install list; body on a fiber if `on`),
  Exit (break/continue/return to another frame; `continue` to InstallingWith = clause
  returns), Resume/Cancel, DebugOnly + Assert (assert), BitsGet/BitsSet (#bits fields,
  nested chains), In, Template; Value.Big (u128/i128 constants); Payload(off, ty) and
  Const(name, span, ty) carry types; get/get_ptr/first/last are written out; AssignTuple,
  AddrOfTemp (synthetic addressed local), try at levels>0, `else` lifting into ?T / E!T.
  Fixed: optional payload offset (0, was offs[1]).
- Selector: words in registers; larger temps in slots (lir_slots: linear scan by position,
  freed at release); per-position scratch (`need`, `until` = With end for Install);
  generic Ins `Gen`/`GenF` (vm_gen.gm) read operands as store bytes (register r = bytes at
  r*8 when word_locals; else WSlot to scratch — the big-endian path); sub-codes
  (Code.sub/subs) for defer and with-on bodies share frame + registers, run by vm_sub
  (CallRec.dbase = defers height at start); Code.loops gets a VmLoop per TIR loop/with
  (DeferHeight/CxSave at the brk block when the body has defers/withs; at/size = result
  local for label_dst). Br out of blocks emits Unwind/CxRestore; out of a sub-code = Exit.
- Register canonical form: rd_word = zero-extended bytes; LIR Word bits are sign-extended
  → word_bits() masks them. Load→register aliasing is off for captured locals.
- Fast paths kept from vm_compile (each measured): Wrap/WrapOptional/WrapOk into slots
  (ZeroSlot+WSlot) and word optionals (Bit or flag); ArrayPtrToSlice; same-size coercions
  (CopySlot); Cast/Addr/ToPtr as Mov (+AlignCheck); Trunc of 128-bit = SlotW; RangeChk
  and Bounds with Bc forms (Rng/Bnd); string literals resolved at compile time; empty
  aggregates emit nothing; IntBit for rot/hash/bit counts.
- Measured (callgrind Ir, `run --backend=interp`, main's build vs this, same Debug build):
  bench/programs call −8.6%, iterate −7.3, list_push −12.5, map_insert −5.3, new_destroy
  −4.9, perform −8.0, with −8.8, safe_call −11.4, safe_list_push −14.1, safe_map_insert
  −5.8, safe_new_destroy −5.5, empty 0; examples nbody −78.5 (was on the tree), life
  −3.3, mandelbrot −2.1; short compile-heavy runs json_query +1.0, wordfreq +1.1,
  sha256sum +0.7 (lowering + analysis cost per first call). Profiling aid used (not
  committed): count `str.format("{}", ins)` of Ins reaching vm_free / GenOps in vm_gen.
- Not done / follow-ups (Machine.temps/kill_temps/CallRec.tm removed in s82): P3 passes (bounds-check elimination first); P4 cgen on LIR;
  the big-endian path (!word_locals) was tested only by forcing word_locals off on x86.

## State after P0 (s79)
- compiler/lir/: lir.gm (types, place_type, value_type, dst_of), lower.gm (Ctx/context,
  lower), verify.gm (verify = structure + load/store/bounds types; verify_structure;
  unit tests), dump.gm (program_dump for `gamma lir`, same fn set as `gamma tir` +
  instances; `!` lines = verifier faults). main.gm Command.Lir. Suite `lir`
  (tests/harness/lir: golden tests/lir/*.gm → .lir with BLESS=1; `lowers/<path>` for
  every tests/conformance/run program and example, ~5 s total).
- Decisions taken in P0 (beyond the design below): places = base + projection list;
  `Elem` only on arrays, `Step` = pointer arithmetic (a many-pointer's element may be an
  array: found by the type checks); `Payload(off)` for optional/result(ok AND err)/union
  payloads (place_type gives up below result/union payloads); index Values may be any
  integer type (u8..u128), length is usize; `Tir` escape for everything not lowered
  (escape() marks locals addressed by AddrOf/array Slice inside the escaped subtree);
  a TIR Block becomes a `Block` op only when it has defers; loops are
  `brk: { loop { conds; cont: { body }; step } }` so `continue` = `br cont`;
  `match` = `end: { a0: { tests→br a0; body; br end } … no-arm }`; if-let/cond chains =
  `end: { els: { conds→br els; then; br end } else }`; single bool `if` = `If` op.
  `try` levels>0, `break`/`continue`/`return` with levels>0 and InstallingWith escape.
- Coverage (sweep of conformance run + examples incl. the std functions they use):
  542,355 instructions, 2,331 escapes (1 in 230): with 621, builtin (not by_value:
  Assert/Sys/Get…/Asm/Fiber/Cover/Unreachable/StaticAssert) 660, constants 514 (u128,
  f16, untyped), non-local break/return/continuation ~310, the rest small. 0 verifier
  faults. Scratch sweep: run `gamma lir` per file, grep `^!` and `tir <kind>$`.

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
