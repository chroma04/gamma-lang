# 011 — @stack_size (§14.8)

- Computed in sema (src/sema/stack_size.rs) so it is a true compile-time constant (array lengths,
  static_assert). Bodies reached are checked on demand via consteval's `ensure_body`.
- Context-sensitive walk over TIR: env of installed handlers (plain `with` only; a with-on adds
  its ctl clauses' cost, not its body), bindings LocalId→FnId for params/immutable locals whose
  value is a FnRef/Closure (so sort_by's `less` resolves). Upvars resolve via the unique
  activation of the owner fn on the path (no recursion ⇒ unique). ctl perform costs 0.
- Errors: recursion (fn already on path), unresolved indirect call, unknown handler (incl.
  ambient effects — f can't see entry handlers), foreign fn. Reported at the offending expr,
  label at the @stack_size, note with the call chain.
- Memo keyed (fn, env, bindings) only for parentless fns whose env clauses are parentless.
- Cost model tir::frame_cost shared with the interpreter's fiber accounting:
  256 + 2×Σlocals + Σ composite temporaries >8 bytes (calls, aggregates, arrays, variants,
  builtins) + 128 per installed handler. Result = max(@min_stack, STACK_RESERVE 8192 +
  STACK_BASE 1024 + chain). Interp then never traps on a fiber sized by @stack_size.
- C reality check (stack_size test, gcc -O2): real need 25–30 KB vs bound 47 KB. Factor 2 on
  locals matters: `[N]u8(fill: x)` makes gcc build a temp copy. Not formally sound (cc frame
  sizes are unknowable before compiling). Verified since s75 by the harness test
  native_build/stack_size_covers_the_frames_gcc_reports (stack_usage.gm): gcc
  `-fcallgraph-info=su -dumpdir`, deepest direct-call chain from a `#noinline` root vs
  bound − 9216. gcc 13: debug 14864 / checked 14560 / fast 14344 bytes vs 15504 allowed
  (tight: ~4% margin). Indirect calls have no edge in the graph, so the test uses direct
  calls only.
- core/slice introsort and sym_merge made iterative with fixed [64]usize stacks so sorting is
  usable under @stack_size.
