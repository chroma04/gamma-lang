# 003 Interpreter design (implemented; docs/architecture.md has the current description)
- Tree-walking over TIR with Rust recursion; run on a thread with a big stack (e.g. 256 MiB).
- Memory model (needed for §17/§18.4 UB detection and faithful layout): Miri-like. Allocations
  (stack slots, statics, heap) = byte vectors + provenance map (offset → alloc id for stored
  pointers) + liveness flag. Pointer value = (alloc id, offset). Loads/stores encode/decode by the
  target layout (§5). Scalars in registers as an enum (Int(BigInt or i128+width), Float, Ptr...).
  Start simpler if needed, but keep the layout-faithful byte memory as the target design.
- Effects: handler stack passed explicitly; esc exits = Rust `Err(Unwind{target})` propagated
  through eval, running defers; returns/breaks similarly as control-flow enum.
- Fibers: chosen OS thread per fiber with strict handoff (decisions/010).
- Since s9 the default engine is a bytecode VM over the same memory; the tree-walker is the
  fallback (decisions/020).
