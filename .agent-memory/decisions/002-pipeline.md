# 002 Pipeline: front end → sema → TIR → {interpreter, C backend}
- Sema is demand-driven (lazy per top-level decl, cycle detection naming the cycle §3.4) because
  type checking needs compile-time evaluation (array lengths, consts, generic args) which needs
  checked + lowered code of callees.
- TIR = fully typed, names resolved, coercions explicit, methods desugared, `for` desugared to
  with+handler+call, literal types final. Generic TIR is monomorphized by substitution.
- Interpreter consumes TIR: used for comptime (§17, required) AND `gamma run` (fast dev loop,
  reference semantics). C backend consumes monomorphized TIR (M7). Every run test should
  eventually run on both (differential testing).
- Why C not LLVM: portable, no heavy dep, cc optimizes; fibers via small asm runtime; wide ints
  via runtime. Revisit only if C can't express something (e.g. @stack_size precision).
