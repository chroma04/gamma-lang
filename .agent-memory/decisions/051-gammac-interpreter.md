# 051 gammac interpreter (s46, branch claude/gammac-interpreter; user: "work on an
# interpreter for gammac so we can achieve parity in things like repl")

- SUPERSEDED IN PART by 056 (s52): bodies are now lowered (code.gm) and run on a register
  machine (vm.gm) with the tree evaluator (run.gm) over lowered nodes as fallback. The
  memory model, values-as-bytes, frames, flows, fibers and threads below still hold.
- compiler/interp/: TREE-WALKER over gammac TIR (not a port of the Rust bytecode VM;
  old Rust tree-walker at `git show 7e437d5^:src/interp/{eval,stmt,calls}.rs` was the
  structural reference). Values = BYTES in target layout (unlike compiler/eval's
  structured values, decisions/046): needed to run std/core code (heap, ptr casts).
- Destination passing: `m.eval(a, e, dst)`; temporaries from per-coroutine chunked
  scratch (`tmp/mark/release`), statements and calls release. No per-value allocation.
- Locals: frame `store` in scratch; locals whose address is taken (prescan
  frame.gm mark_addressed: AddrOf/Slice-of-array/GetPtr/VecStore roots) or any upvar
  address (lazy local_addr, copies store into memory) get their own Memory allocation;
  `fr.mem[i] != 0` is authoritative.
- break/return write their value into the target's dst (frame.labels: label → dst;
  frame.ret) before raising Flow.Break/Return (no values in flows).
- Second-class values as bytes: fn = [code addr, frame handle, ctx handle]; handler =
  [frame handle, ctx handle] (lit id from TyKind.Handler); cont = [fiber id, epoch].
  Frames/ctx nodes in tables with free lists (handles fit 32-bit targets).
- Memory model = port of memory.rs (fresh addresses, tombstones); addresses need not
  match the reference (no expected output contains one).
- Fibers (fiber.gm): every interpreted fiber/thread is a HOST coroutine under one
  scheduler (`handler Coro for ^Sched`, ctl `transfer(to)`), installed by run_entry
  with the program on a 128 MiB heap host stack (debug gammac memsets heap blocks, so
  no bigger). Reason: host `resume`/`@fiber` must be lexically in a ctl clause and end
  it, so the reference's driver/fiber handoff (threads + channels) is rebuilt as
  coroutine handoff (`handoff(to, msg) -> msg`) with all switching in the one clause.
  Per-coroutine state (scratch, temps, depth, fiber_stack, stack_floor) saved on handoff.
  Stack floor must be measured in a CALLED function: locals of a `with … on` body
  belong to the enclosing C frame (they are not on the fiber stack).
- Threads (threads.gm): green threads on the same coroutines, switch every 2000
  ticks and at join/futex wait/yield; blocking host syscalls block all threads
  (reference releases its GIL there) — possible divergence for socket+thread programs.
- FFI not supported (reference falls back to native when libffi can't): unsupported.
- Unsupported constructs trap "internal error: not supported by gammac's interpreter
  yet: …"; tests/selfhost.rs programs_interpreted_by_gammac… skips those, floor
  INTERP_FLOOR.
