# Recurring usability pain points (candidates for language/library changes)
Log a pain point when it recurs while writing Gamma: where, how often, and the candidate
fix. Flag it to the user. Minor ones with a clear design get implemented (CLAUDE.md).
Before flagging, check "Settled" below: those are decided.

## Open
- Adding an intrinsic that the prelude uses needs a 2-stage build + seed refresh (s50 sys
  worker); procedure now in docs/contributing.md. Watch.
- Code size of format templates (s56, measured): `template :: string` is a comptime
  parameter, so every function taking one has an instance per template, and every
  forwarding wrapper (str.format → List.write, kit.check → str.format) multiplies them.
  In the compiler: ~1,540 str.format templates; their instances were ~20% of the generated
  C before s56 (reduced by shared @write helpers and str.format writing itself). Possible
  language change: a checked template VALUE (type checked at the call site against the
  args, passed at run time as pieces + specs), so wrappers instantiate per arg types only.
  Not urgent; flagged to the user.
  s77 measured (compiler's emit-c, Debug; scratch tmpl.py groups instances by the C type of
  the `args` local + the type descriptors the body uses): str.format 1,422 instances =
  8.84 MB with their sink closures (18% of 49.2 MB function text) fall in 69 type groups
  (largest: `(string,)` 590, `(string, string)` 277); one body per group = 0.47 MB, saves
  ~8.4 MB. List.write 279 → 40 groups (−0.95 MB), cgen linef 175 → 24 (−0.66 MB).
  Recommended to the user: implementation-only sharing (template passed as a static
  piece table to one body per arg types; no spec change), not the language change.

## Settled — do not re-flag (reason)
Done: function value as a format value / inferred type argument is an error (s75, §16.2), stale-element-pointer through struct pointers (060), dangling-address lint + errors (044/045), pub(package) (044), composite keys
(044), `?x` (047), `[a, b]` note (045), mutated-copy lint, if-branch join (s45), flush before
blocking (049), temp array slicing (049), process-group kill + SpawnOptions env (053), `in`
joins (s48). Checked s50 as already working: `if .V(b) := x and n > 0` (§8.2 chains),
`if !e := r`, `a is not b and c`, unused constants are checked, `[_]T(…)` constants,
thread entries with fewer effects (subsumption), `?T`-in-variant diagnostic suggests `?x`,
`<` and `sort` on strings (§7.5), sorted map = std/collections/ordered_map.
By design / not worth conceding:
- Lifting into struct-typed errors, `!.V` through a struct field (071; spec §10.6, C.8).
- No shadowing (user, 045). `usize(u64)`/`.trunc` (§6.6, 32-bit targets).
- `f :: fn` only (functions are constants, second-class literals). One namespace per
  module across files. Variant at the start of a line (`T.V(…)` reads fine; §2.8).
- A name hiding a predeclared one hides it completely (§4.6); rename instead.
- `defer` may perform only `fn` ops: a helper `needs Env` could `exit` (§8.8 rationale).
- `!e` inside a nested result targets the outer error type: a type-directed choice would
  make meaning depend on the error types; the typed temporary states it.
- `make(bool|string, n)`: `make` is uninitialized storage; `make_fill` exists.
- Postfix `?` traps (that is its meaning; `try` propagates). `?string is string` (§7.6).
- `str.format(...)!` (allocation fails visibly, 037). `[_]string("a")` (one construction
  syntax; note on `[...]`). `for` over a heterogeneous tuple; tuples are not sequences.
- Arena lifetimes (no ownership system, C.8). `with … on` locals (implementation detail).
- `assert` messages are runtime strings (like `trap`); format with `eprint` first.
- getpid (unique names come from `Rand`), env var set (unsafe with threads; child env via
  SpawnOptions), `Test.fail` loc (`expect(false, msg)`), `make_dir` mode (set_mode after).

Implemented in s50 (decisions/054): `else` join, const-adding coercions in `?`/`!`, array joins (if/match/for-tuple), contextual
`needs`/`handler`/`foreign`, `{0}` + `{:q}`, local struct/union, lint stale-element-pointer,
std/text Unicode trim/fields, Channel drained, Fs canonical/read_link/symlink/set_mode/
set_modified, Proc try_wait + SpawnOptions.program, pub handles, Env on threads.
