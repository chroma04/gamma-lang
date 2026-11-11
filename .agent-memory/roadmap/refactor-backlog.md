# Refactoring backlog (s64 audit, 2026-11-10) — essentially DONE (s66)
s64: needless pub, dead code, small dedupes. s65 (PR #119): layouts in sema, CLI table
(driver/flags.gm), bignum removed, ast.for_each_child/tir walkers, harness Watchdog +
kit.golden, check/seq split, vm_compile split, helper dedupes.
s66 (branch claude/kind-shannon-h4x997; 5 sonnet worktree agents, one per directory):
- check: expr.gm → jumps.gm + construct.gm; func.gm → bindings.gm, assign.gm,
  effect_check.gm; perform.gm (from iterators.gm); with_expr/bind_args (ArgBinder)/
  generic_call_packed (357 → 182)/write_expr split; block_typed + block_of everywhere;
  Sema.prelude_effect; save_frame/load_frame; clause helpers; counting_loop/increment;
  coerce/finalize share steps_for.
- cgen: ScopeKind payloads (LoopLabels, WithLabels; defers/used stay on Scope since a
  match binding is a copy); FnGen Saved/save/restore + ScopeState; patterns.gm, bits.gm,
  builtins.gm split out; helpers.gm declare_helper/define_helper. C output byte-identical.
- interp: intarith.gm = 128-bit arithmetic + the shared rules (overflow_trap,
  div_zero_trap, checked_overflow, overflow_result, shift_check) used by BOTH the byte
  path and the word path (word.gm); place.gm, values.gm, access.gm, frames.gm,
  contexts.gm split out of run.gm/interp.gm. No perf change measured.
- sema/parse: ScopeMark everywhere, add_fn/bodiless_def/new_adt_info, member_count/
  member_ty, CORE_EFFECTS table (order = effect ids), FnTy.variadic REMOVED (dead);
  parse members() for block/match/when, parse_type_in/parse_expr_in (restr), generic
  ast.MemberWhen(M) replaces StructWhen/UnionWhen.
- lsp: envelope(), notification split, malformed/missing Content-Length → -32700 and
  continue (test lsp/malformed_headers…); ide Narrowest; native.gm CacheLookup (no
  Env.exit in cached_program). CI: scripts/version.sh, scripts/smoke-hello.sh, bench
  compare looks for `gamma` only; selfhost/fuzz_targets_type_check test. A composite
  "build gamma" action was tried and reverted (it wrapped one line).

## Left (small; do only when touching the code)
- check: generic_call_packed's per-argument loop (shared state); write_expr's big match;
  check/tests.gm repeats the effect-name list in another order (could use CORE_EFFECTS).
- cgen: save/restore sites in calls.gm, chain.gm, outline_run, start_exit save different
  field sets (left on purpose).

## Possible bugs: final status
Fixed in s64b/s66 (see git log). Not bugs: eval Pow exponent; f16 vector lanes;
toolchain for_safe_build splitting PATH on ":" (Safe is Unix-only); `x: f64 : (1 << 60)
+ 1` → "`<<` requires integer operands" is what §6.1 says (literals of an annotated
constant take its type; untyped-constant evaluation applies only WITHOUT annotation).
Left on purpose: target_text trailing "" field (changing it invalidates cache keys).
