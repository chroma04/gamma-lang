# 037 Toy LLVM back end: is LLVM's optimizer worth the baggage? (s33, branch dev)

User asked for "a toy LLVM backend; I want to see if the optimizer is worth the baggage".
User docs + full result tables: docs/llvm-experiment.md (keep it the single source of numbers).

## Shape (src/llvm/: mod.rs driver/link, func.rs lowering; runtime/gm_llvm.c glue)
- TIR → *textual* IR, `clang -x ir -O$GAMMA_LLVM_OPT` (default 2; GAMMA_LLVM_CLANG,
  GAMMA_DUMP_LLVM=1). No Rust deps. Linked with gm_rt.c + gm_llvm.c (cached object, prefix
  gm_llvm_rt); target check reuses clif::check_target.
- `--backend=llvm` for run/build only (Options.llvm in main.rs; `gamma test/bench` reject it,
  exit 2). Never served from the program cache.
- Subset: ints 8–64, bool, f32/f64, [N]T (nested), ^ to those; top-level fns, direct calls,
  if / match on scalars (Const, Range, Or, guard — `for` binds via a 1-arm Match!) / loops /
  break / continue / return / trap; flavors plain/wrap/sat (no mul-sat, no `?`); Write to
  ambient Out only (default specs) via gm_ll_write_* and gm_write_out. Everything else →
  Unsupported "not supported by the LLVM back end: …".
- Locals = entry-block allocas (mem2reg does the rest). Array inits written in place
  (store_into: ArrayFill → memset if all-zero else loop; Array → per element).
- Checks mirror clif: llvm.*.with.overflow + gm_trap, gm_ll_trap_index/shift (same messages),
  div-by-zero always, stack check via llvm.frameaddress vs @gm_stack_limit (TLS) in checked
  modes. No backtraces in Debug.
- Tests: tests/llvm.rs (+ tests/llvm/subset.gm): output == interpreter in all modes at -O0/-O2,
  trap stderr byte-identical to the interpreter. Skips without clang.

## Findings (x86-64, clang 18): see docs/llvm-experiment.md
- LLVM -O2 vs Cranelift: 1.1–4.6× (matmul, fib, sieve, collatz big; hash/mandel ~1.1×).
- -O1 ≈ -O2 ≈ -O3. matmul: inlining + all bounds/overflow checks proven away.
- LLVM -O2 ≈ C back end with CC=clang everywhere → a real LLVM back end buys no run time.
- Verdict recorded in the doc: not worth it; instead do TIR-level range-check elision and
  small-fn inlining (helps Cranelift + VM). Possible roadmap item.
