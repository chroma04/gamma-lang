# 010 — Fibers and continuations (§14)

## Sema
- `ctl` ops allowed. A ctl clause gets an extra final param `k: cont(R)`; its fn ret is
  `never`, so the body must have type never (error otherwise).
- `resume k(v)` / `discard k` / `@fiber` only directly in a ctl clause frame
  (`FnCx.clause.op_kind == Ctl`), not in nested fn literals.
- `with … on s` required iff a handled effect has a ctl op; `s: []u8`.
- Direct ctl performs inside `defer` are errors (calls are not tracked).
- `@min_stack` = 16384 (a usize literal). `@fiber(f, arg, stack)`: Builtin::Fiber,
  f a `^fn(A) -> never \ {S}`; S minus the clause's own effect is recorded as performed.
- TIR: `Program.ctl_ops: HashSet<(EffectId, u32)>` tells back ends which performs suspend.

## Interpreter (src/interp/fiber.rs)
- Each fiber is an OS thread (spawn_unchecked, big stack) sharing the Machine through a raw
  pointer; strict handoff over channels: exactly one thread runs at a time.
- Per-fiber machine state swapped on handoff: temps, depth, stack_floor.
- `Flow::Resume` ends a clause (defers run, errdefers don't); `Flow::Cancel` unwinds a
  discarded fiber (errdefers run). Aborted fibers get a Trap flow (no defers) and are joined.
- Continuation bytes = (fiber id, epoch); registry in Machine.fib.
- Fiber stack: Machine.fiber_stack budget, frame_cost per call (64 + locals), 8 KiB reserve.

## C back end
- Runtime: gm_rt_switch asm (x86-64 SysV, aarch64); other archs trap when switching. Fiber structs pooled,
  never freed, epoch counter never reset → stale/double consumption detected.
- cont(R) blob = {gm_fiber *, gen}. Node field `fw` = the gm_with of a with-on.
- with-on body outlined into `gm_wb{fn}_{label}(gm_fiber*)` with `#define F (*Fp)`; its
  ladder ends in `goto gm_fprop` (fiber finish), the driver loop re-runs the ladder.
- ctl perform: per-site request struct + thunk calling the clause on the driver stack.
- New exit kinds GM_RESUME (clause → driver loop switches) and GM_CANCEL.
- Crossing exit from a fiber: discard every other suspended continuation (most recent first);
  clause exit / body completion with suspended continuations → trap.
- Checked builds: GM_STACK_CHECK(&F) at fn entry vs fiber->limit (stack + 8 KiB reserve),
  emitted only when the program has ctl ops. Old PRELUDE moved into runtime/gm_rt.{h,c}.
