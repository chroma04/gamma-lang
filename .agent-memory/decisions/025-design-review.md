# 025 Design review (s15) — ADOPTED and implemented (the review doc was removed from the repo in s16; this note is its record)

Thesis: "C in which everything a function depends on appears in its signature". Principles
P1 plain data, P2 visible cost, P3 context is an effect, P4 mistakes rejected, P5 local
rules; tie-breaker one feature per job. Spec §1 states them; Appendix C is organized by them.

Implemented (all in PR #38, branch claude/tender-cannon-qqxawd):
- `cancel k` (was `discard k`); runtime gm_cancel_cont. Handler decls may not overlap
  (handler_decl.rs may_unify / template_instance_is). Shift-amount literal fallback usize.
- `Test` effect in prelude {note, fail: esc}; expect/ok are prelude fns, expect_eq builtin
  lowers to Test performs; run_test/run_bench install a Test handler (failure runs defers,
  skips leak report). Test bodies ret (), `try` there → error with ok() hint. Source
  locations name the innermost named fn (FnCx::location_function).
- Rest params `args: ..A` (ParamInfo.rest; call::pack_rest_args packs rest args into one
  tuple arg before binding, both bind_args and generic_call_with). print/eprint/@write take
  values as rest args: exactly n values or one spread `..t`; WriteDeferred args = tuple.
- Inferable comptime params (mentioned by another param's type, generics::inferable_param)
  never positional; named args allowed anywhere; generic inference order: named types,
  run-time args, fn literals, comptime values. Hints for `f(u8)`/`max(i32, xs)`.
- Universe: effect ops hidden except yield (Sema::universe_hidden, UNIVERSE_HIDDEN list incl.
  Fs/Net types + Instant/Duration methods); `core` binding (prelude module) visible to std
  only; std/fs, std/net re-export `pub File :: core.File` etc. `hash_map`, `hash_map_with`.
- Named effect sets: DeclValue::EffectSet; `..Io` in resolve_effect_set; effect-set args.
Deviations (documented in report §8): Test has `note`; pow_checked kept ("checked" =
optional rule); Signed kept; op names kept (Out/Err share a module).
