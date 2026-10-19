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
to the core fn. Rest done after the user's "go ahead" (third PR): freestanding (§3.7.1; cgen generate_object,
GM_FREESTANDING runtime, #trap_hook, tests/freestanding.rs with harness.c; wasm32-wasi counts as
freestanding now — add a Target field if WASI should be hosted); early literal fallback conflicts
reported at range bounds (infer.rs settled_at; `{}` format args don't settle, specs do); Signed→Neg;
ns/us/ms/secs only in std/time. KEPT effect-set inference from caller context (same rule as op
instantiation, local), only rewrote §16.6 prose. NOT changed: A.7 rule 3 (`x!(a)` resolved by name
resolution) — a local fix needs new syntax for error-union types in expressions; low value.
