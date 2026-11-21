# 075 — A lowered IR under TIR ("LIR"): scope and plan (s79); PROPOSED, not started

User ask (after 074): "scope out the lowered IR". Nothing implemented. Read 074 first.

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

## Open questions for the user (not blocking P0)
- Is P5 (LLVM) wanted at all, or only P0–P4?
- OK to spend ~10–15 sessions with roadmap §1/§3 work paused?
