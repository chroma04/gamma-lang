# 075 — A lowered IR under TIR ("LIR"): scope and plan (s79); CURRENT, P0 done in s79

User asks (after 074): "scope out the lowered IR", then "start on the plan with P0 …
we're probably not going to do P5". P5 (LLVM emitter) is OFF the plan. Read 074 first.

## State after P0 (s79) — start P1 here
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
- P1 next: the VM selector LIR → Ins (per function; escapes → lower the TIR subtree
  with code.gm and EvalW it, as today). Lower `with` first (needed for `for` over
  iterators), then non-local exits, then builtins. Keep `Tir` until P2 deletes the tree.

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
