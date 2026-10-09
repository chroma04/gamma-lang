# M8 — Fibers and continuations (§14)
`with … on`, ctl ops, cont(T), resume/discard, @fiber, @min_stack, one-shot + structured-lifetime
checks, cancellation unwinding. Design: decisions/010.

## Status: done (both back ends)
- Sema: effects.rs (resume_expr, discard_stmt, fiber_intrinsic, clause_body, with on/ctl rule),
  handler_decl.rs clause_signature (ctl clause: extra `k: cont(R)`, ret never).
- Interpreter: src/interp/fiber.rs (thread per fiber). C: src/cgen/fiber.rs + runtime fibers.
- Fiber stack overflow: C checks `&F` vs fiber limit at fn entry (checked builds, only when the
  program has ctl ops); interp charges tir::frame_cost per call. Reserve tir::STACK_RESERVE 8 KiB.
- @stack_size: src/sema/stack_size.rs (decisions/011). Tests run/stack_size, check/stack_size.
- Tests: run/fibers.gm (generator, ask, break+discard, scheduler w/ @fiber + decl handler,
  crossing esc exit, nested with-on, closures, string resume), run/fiber_{resumed_twice,
  left_suspended,stack_overflow}.gm, check/fibers.gm.

## Not done
- Static "k used only by resume/discard" verification (runtime checks always on instead).
- Rule 4 (same thread): continuations are per-thread by construction (fiber tables are thread-local).
- @stack_size not verified against `cc -fstack-usage` (model is generous; see decisions/011).
- ctl performs inside defers only rejected when direct (calls not tracked).
- Non-x86-64/aarch64 C targets: fibers trap at run time.
