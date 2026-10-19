# 030 Coherence review (s28, 2026-10-19): what was applied, what waits on the user

Source: an outside design review of the spec against the thesis + P1–P5 (user: "act on it").
Applied in PR on branch claude/coherence-review-wyd199 (impl via 3 sonnet worktree agents):
- Spec fixes: P2 reworded (checks are the one inserted cost, all in §18.2); C.3 says `is`
  AND `in` grow with data; grammar uses `needs` (no `'\' effect_set`; `\` dropped from §2.6,
  lexer still lexes it only to give the migration error); §16.5 XOR is `~`; §21.5
  `hash_map`; duplicate §1.5 table removed.
- §1 additions: "words for logic, sigils for shapes" rule; four headline rules (op kinds =
  cost, comptime = empty effect set, for is with, results vs esc); the 3-question litmus
  test for new features ("Admitting a feature"); a first program (also
  tests/conformance/test/spec_first_program.gm — keep them identical).
- Explanations: §15.2 effect for scoped memory / value for owned; §13.4 "why three forms";
  §7.6 slice content equality as a deliberate P1 exception.
- Behavior: elementwise array arithmetic REMOVED (vectors do it); vector `is` → bool (vectors
  Eq, not Ord, `<` on vectors an error), lane compares = methods lanes_eq/ne/lt/le/gt/ge;
  ints capped at 128 bits (wide-int machinery deleted); iterator-argument calls evaluate
  their args once (§13.5, like for); computed lengths equal only syntactically after folding.
  C.8 rows added for each.
PR #52 merged. User chose "do it next" for effect ops → done in the follow-up PR (same branch):
ops not module names (sema/modules.rs Module::effect_ops; only core `yield` in scope), E.op /
m.E.op only; methods: sema/methods.rs op_method + decls.rs check_method_candidates (one
candidate per name per type). Renames: Out/Err.write, Fs.read/write, Net.close, Proc.write/read
(close_dir, close_listener, close_in kept: op names unique per effect). Awkward: fiber.Sched.
yield_now()/join() long; a bare call of an op named like a core fn (make) silently resolves
to the core fn. Still open for the user:
- Literal early-fallback list (§6.1) → compile errors: ergonomic cost (`x := 5; print("{}", x)`).
- Drop caller-context stage of effect-set inference (§16.6; decisions/027 added it for
  fiber channels: would bring back `send(Out + Clock, …)`).
- Freestanding semantics (§3.6 `.Freestanding`): new feature (entry point, no ambient
  defaults, trap hook).
- Taste: `Signed` → name for negation; ns/us/ms/secs out of the universe; A.7 rule 3
  (`x!(a)` parse depends on name resolution, P5).
Impl notes: iterator-arg hoisting = sema/call.rs hoist_iterator_call/hoist_iter_value + FnCx::iter_pre,
placed by bind_args (place_iter_pre) and generics.rs generic_call_packed; constants/globals/untyped
literals stay in the literal (heuristic `iter_stable`); places stay places. cgen elementwise.rs and
wide.rs deleted; runtime gm_w* gone; vector.rs has its own lane_loop. `is_oversized_int_name` gives
"integer types are at most 128 bits wide". Length exprs: types.rs len_expr no operand sorting.
