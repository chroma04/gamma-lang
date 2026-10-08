# M8 — Fibers and continuations (§14)
`with … on`, ctl ops, cont(T), resume/discard, @fiber, one-shot + structured-lifetime checks,
cancellation unwinding. Interpreter: needs separate stacks (see decisions/003). C runtime: asm
context switch for x86-64/aarch64 (+ ucontext fallback), guard pages in Debug.
