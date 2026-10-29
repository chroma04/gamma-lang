# Repeated usability pain points (candidates for language/library improvements)
Standing user rule: Gamma is evidence-driven; a pain point hit more than once while writing
Gamma gets flagged here (and to the user) as a possible improvement. Note how often and where.

s50 (user: "discard what is done or not an issue, design + implement the rest") triaged the
whole list; decisions/054 records the designs. Only OPEN items stay below this header.

## Settled — do not re-flag (reason)
Done earlier: dangling-address lint + errors (044/045), pub(package) (044), composite keys
(044), `?x` (047), `[a, b]` note (045), mutated-copy lint, if-branch join (s45), flush before
blocking (049), temp array slicing (049), process-group kill + SpawnOptions env (053), `in`
joins (s48). Checked s50 as already working: `if .V(b) := x and n > 0` (§8.2 chains),
`if !e := r`, `a is not b and c`, unused constants are checked, `[_]T(…)` constants,
thread entries with fewer effects (subsumption), `?T`-in-variant diagnostic suggests `?x`,
`<` and `sort` on strings (§7.5), sorted map = std/collections/ordered_map.
By design / not worth conceding:
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

Done s58: stale-element-pointer follows growth through struct-pointer calls (decisions/060).

## Implemented in s50 (decisions/054)
`else` join, const-adding coercions in `?`/`!`, array joins (if/match/for-tuple), contextual
`needs`/`handler`/`foreign`, `{0}` + `{:q}`, local struct/union, lint stale-element-pointer,
std/text Unicode trim/fields, Channel drained, Fs canonical/read_link/symlink/set_mode/
set_modified, Proc try_wait + SpawnOptions.program, pub handles, Env on threads.

## Open
- Adding an intrinsic that the prelude uses needs a 2-stage build + seed refresh (s50 sys
  worker); procedure now in docs/contributing.md. Watch.
- A function name used as a format argument compiles and prints the function's address
  (`str.format("…{}", loc)` where a local `loc` was meant but missing: the method `loc` of
  the same module was found). Hit once (s55, moving cgen arms into helpers; the C compiler
  caught it). Candidate: formatting a function value is an error or a lint.
- Code size of format templates (s56, measured): `template :: string` is a comptime
  parameter, so every function taking one has an instance per template, and every
  forwarding wrapper (str.format → List.write, kit.check → str.format) multiplies them.
  In the compiler: ~1,540 str.format templates; their instances were ~20% of the generated
  C before s56 (reduced by shared @write helpers and str.format writing itself). Possible
  language change: a checked template VALUE (type checked at the call site against the
  args, passed at run time as pieces + specs), so wrappers instantiate per arg types only.
  Not urgent; flagged to the user.
