# 020 Interpreter bytecode VM (thread "bytecode VM", branch claude/project-thread-rh6kp0)

## Shape (decided 2026-10-13)
- Code in src/interp/vm/: mod.rs (Op set, Code, CodeSlot, code_of), compile.rs (locals
  classification, expressions, places, calls), compile_flow.rs (blocks, statements, defers,
  patterns, loops, with, break/continue/return, finish = Nop removal + jump resolution),
  exec.rs (activations St, dispatch loop vm_run<LE>, vm_slow, unwinding).
- Per-function register bytecode, compiled lazily on first call (per Machine; const-eval
  machines compile too). `call_function` dispatches to `vm_call_values` when code exists;
  compiled→compiled calls go `vm_call_regs` (no Value boxing). Rust recursion per Gamma call.
- Tree-walker stays as (a) whole-function fallback (compile returns Unsupported(file,line)),
  (b) `EvalTree` op for with…on, resume, discard, @fiber, Sys builtins, by-value array get;
  (c) `TreeStmt` op: a statement whose value is too big for registers (>64 KiB) — compile
  retries with that statement on `tree_stmts` (up to 64). Locals mentioned by (b)/(c) are
  pinned to the Frame.
- Locals: Reg (≤64 B, not captured/addressed/place-assigned/second-class), Frame (existing
  Slot machinery), Val (second-class, frame slot). Scan must look into Loop bodies too
  (`block_of`): Loop.body is a Block, not an ExprKind::Block (bug found once).
- Registers: Vec<u64>; n-byte value = ceil(n/8) words of its target-layout image, last word
  zero-padded (every op keeps it; iput masks). Address registers hold numeric addresses;
  raw_ptr (pointer image word == address) only on LE && 8-byte pointers, else PtrToAddr.
  Small-int ops use ival/iput with const LE (BE swaps).
- A Frame (Rc) is still made per activation (ids for flows, Frame locals, closure env).
- Unwinding: scope table innermost-first (pushed when the scope closes): Guard (defer code:
  non-trap flows → "control flow escaped a deferred statement"), Defer{seg} (segment code
  emitted after the body, ends in EndSeg, errdefers via errdefers_run), Label (loop/with
  landing; loop mark reg always set at entry). Return to own frame caught after scopes.
- In-function break/continue/return → jumps with inline defer copies (errdefers on return
  only if the value is an error: JmpByte on the flag); RestoreCtx when leaving `with`s;
  break kills temps of the left statements (statement MarkTemps patched from a Nop).
- Assign/Let compile straight into a Reg local only when `single_write(value)` (Let always).
- Steps: +1 per op only when a budget is set (vm_run<LE, COUNT>); checked at JmpTick (loop
  back edges) and call prologue.
- Small literals: constant registers after the locals (find_consts), filled from Code.init.
  `part()`/`sub()` return views (sub-registers) for word-aligned pieces when no dst wanted.
- Statement temps: MarkTemps/KillTemps patched from Nops only if the statement made temps.
- Convert/Trunc of small ints compile to Widen/Trunc ops; Builtin args ≤4 go in a stack array.
- Frame slots only up to the highest non-Reg local (Code.nslots).
- `GAMMA_INTERP=tree` forces the tree-walker; `GAMMA_VM_DUMP=name|*` dumps code and says
  why a function fell back.

## Status
- All tests pass under both engines (2026-10-13). Benchmark (scratch script: 12 slow run
  conformance tests + dogfood sha256/wordfreq): tree 34.1 s → VM v1 14.2 s → tuned 10.0 s.
  std_text_utf8 3.3 s is the worst; ~15 ns/op, dispatch- and call-bound.
- Tests: tests/conformance/run/interp_engine_edges.gm (TreeStmt, clause break through calls,
  try in a clause, conversions). CI runs conformance with GAMMA_INTERP=tree too.
- Follow-ups: superinstructions (cmp+branch, load+op), shrink Op (40 B), avoid the Rc Frame
  per activation when no Frame locals, cheaper Value boundary for builtins/calls to tree code.
- No run conformance test falls back to the tree-walker for a whole function.

## s10 VM speedups (perf follow-ups done)
- i64 kernels with cold overflow path; raw-ip dispatch reading hot registers unchecked, guarded
  by `Code::valid()` (register/jump-target validation) + sentinel trap at code end. `Op` 40→24 B
  (side tables, size assert). Fused `TickBr`; `AddI/SubI/MulI/BinI` immediates (unmeasured gain).
- Lazy frames (no Rc frame without frame locals); reg→reg arg copies; in-place aggregates;
  perform / fn ptr / closure calls straight from registers. `GAMMA_VM_PAIRS` counts op pairs.
- Callgrind: call 1150→340 instr, perform 1730→900, new/destroy 22k→16k. Micro 1.6–2.2×.
- Next: lisp-style code hits slow ops (LdLocal, AddrLocal, LdUpvar, Builtin, EqG, Str, arg vecs
  into tree-walked code); new/destroy cost spread across builtins.

## s22 float ops
- Profiled examples with `--features vm-profile` + GAMMA_VM_PROFILE=1 GAMMA_VM_PAIRS=1:
  mandelbrot was 71% BinG (floats had no fast path). Added BinF/CmpF/BrF/NegF for f32/f64
  (f16 stays generic). f32 computed in f64 then rounded = correctly rounded for + - * /.
  Negated float conditions: BrF over a Jmp (never invert a float cmp: NaN). Float loop
  1.00 s → 0.12 s. Register words hold target-order bytes: BE swaps (fval/fput in exec.rs);
  checked with `gamma run --target=s390x-linux-gnu` (interp emulates BE).
- life.gm profile: AddI/BinI/IndexAddr/Load/BrI mixes — no single dominant slow op left.
- RotI for @rotl/@rotr on small ints (sha256sum: Builtin was 11.6% of ops): 2 MB 2.88 → 1.54 s.
  Other bit intrinsics (clz/ctz/popcount/bswap) stay Builtin: 2 uses in lib/examples.
