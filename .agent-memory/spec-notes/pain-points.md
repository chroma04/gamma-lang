# Repeated usability pain points (candidates for language/library improvements)
Standing user rule: Gamma is evidence-driven; a pain point hit more than once while writing
Gamma gets flagged here (and to the user) as a possible improvement. Note how often and where.

STATUS s39 (decisions/044): the four below were addressed — dangling-address lint, §9.3
note + diagnostic, pub(package) + needless-pub lint, and composite keys already worked.
Still open: pointers into a growing List across push (lint can't see it). s40: certain
dangling stores are now compile errors (decisions/045).

- **Pointers to copies / to slots** (≥9x across s38–s40; s40 subagent: `&e` of a pattern
  binding pushed into a list → bogus inference errors): `&loop_var`, `&pattern_binding`,
  `&list.items[i]` across a push, and `x[i] = f(&x[i])^` where f keeps the pointer (s38g
  finish_call). Escape analysis catches none. Idea: a lint/escape rule for storing the
  address of a loop variable or binding; warn on `p = f(&p)` shapes. Handbook covers it.
- **Optional parts in tuple payloads**: DONE s41, user-approved `?x` pattern (decisions/047).
- **Cross-module helpers must be `pub`** (s38e, s38g: ~15 sema fns made pub just so
  check/ can call `cx.s.f()`): no package-private visibility. Idea: package-level
  visibility (`pub(package)`), since a package's modules are one unit.
- **Small maps / composite keys** (s38d types interner, s38g fn instances, binds): HashMap
  needs a hand-built byte-string key for struct/tuple keys, and tiny maps become List +
  linear lookup helpers. Idea: hashing derived for plain structs/tuples (`@hash` exists
  for core) so HashMap((u32, []Ty), u32) works; or a small assoc-list type in std.
- **Function literals must be bound with `::`** (2x): `f := fn(..)` is an error; the
  message is clear, so low priority.
- No shadowing: user keeps the rule on principle (decisions/045); do not re-flag.
- **Variant at the start of a line in a block-valued match arm** (s40, 6 sites at once in
  tir/clone.gm: `{ … \n .Call(c, …) }` reads as `c(...).Call`): note explains it; use
  `T.V(…)`. Low priority.
- `[a, b]`: DONE s40, parse note `[_]i32(1, 2)` (decisions/045).
- **One namespace across a module's files** (s41): the evaluator could not live in
  check/ because `unsupported`, `block`, `stmt`, `coerce`, `load`, `builtin` already exist
  there and there is no overloading → moved to its own module (fine design anyway). Low
  priority; flagged to the user once.
- **`str.format` gives `[]u8`, literals are `string`** (s42, 2x): mixing them in a list or
  `in` needs `n: string = str.format(…)!`. Low priority; the message names both types.
- **`a is not b and c` on bools** (s42, subagent): needs parentheses; easy to forget.
- **No `---` for pointers / no definite assignment** (s42, W1 subagent): `x: ^Expr = ---`
  is rejected, so a pointer set in both branches of an `if` needs a helper function or an
  `if` expression. Idea: definite-assignment analysis for locals declared without a value.
  Seen once; watch for repeats.
- **Copying a struct that owns a List forks it silently** (s43, 4 sites by 3 subagents at
  once): `tys := fg.g.ty.s.tys` then `tys.ptr(…)` interned into the COPY → later
  `index out of bounds` in `kind`. Needed `tys := &…`. Idea: lint `mutated-copy` (a local
  initialized by copying a place whose type holds a List/pointer-owning struct, then
  passed as `^T` to a method). Flagged to the user (s43); user said implement → DONE s43:
  lint `mutated-copy` (src/sema/lint_copy.rs; buffer = struct with mutable slice +
  Allocator; exemptions: read whole, assigned whole, captured, original place assigned).
- **`usize(u64)` rejected** (s43, ~10 sites in consts.gm): by design (§6.6, 32-bit
  targets); `.trunc(usize)` everywhere for indices computed in u64. Watch.
- **No way to set a child's argv[0]** (s45, gammac run): `Proc.spawn` uses argv[0] as both
  the program and the name the child sees, so `gammac run` cannot show the source path as
  the program name as `gamma run` does. Idea: `SpawnOptions.program: ?string` (the file
  to run; argv[0] is then only the name), like execv(path, argv). Seen once.
- **`x is .V(b)` does not bind** (s45, W-fmt subagent, ~6x): needs `if .V(b) := x`, so
  `if prev is .Tok(p) and n > 0` becomes nested ifs. Idea: binding patterns in `and`
  chains of conditions (`if .Tok(p) := prev and n > 0`?) — check spec §8.2 first; maybe
  already allowed with `:=` in chains. Watch.
- **`for x in (a, b)` over a homogeneous tuple** not accepted (s45 W-fmt, once): use an array.
- **`assert` takes no message/format args; no `{:?}` escaped-debug spec** (s45 W-fmt, tests).
- **No realpath/canonicalize in std/fs** (s45 W-doc): module names from symlinked paths
  differ from gamma's. Library gap; candidate `fs.canonical(path)`.
- **No Unicode-aware trim/split_whitespace in std/str** (s45 W-fmt and W-doc both
  hand-wrote them: 2x) → candidate for std/text. **`is !e` cannot bind** (W-doc).
  **load/paths.gm helpers not pub** (fixed by making them pub if reused).
- **`?T` field in a variant pattern** (s45 W-lint, ~8x): `.Let(local, init)` with
  `init: ?T` is not exhaustive / `if x := init` "always succeeds"; must write `?init`.
  Same family as the s38 present-only-binding gotcha (7x). Strong candidate for a better
  diagnostic (suggest `?init`) — the error is clear but the mistake keeps recurring.
- **Containers created outside an arena storing arena strings** (s45 W-lint): segfault
  (dangling keys). Lifetime problem, not syntax; Debug free checks did not catch it.
- `make(bool, n)` (bool not Plain), `[N]T(.V)` fill not valid (W-lint, once each).
- **`str.format` → `[]u8` vs literal `string` in if-branches** (4x+) — FIXED s45: §7.12
  branch join takes the first branch type every other branch coerces to (sema
  `join_type`, gammac check `join_type`); conformance run/branch_type_join. Mixing in a
  list or `in` still needs an annotation.
- s45 W-pkg: no env var list/set, chmod, readlink, utimes, getpid in std (ran processes);
  postfix `?` on an optional traps instead of returning (expected Rust meaning; candidate:
  warn on `?` of `?T` in a `?T` fn); `x is y` between `?string` and `string` accepted
  (unclear; candidate error); `make(string, n)` rejected (not Plain) → List needed;
  `Test.fail` needs `loc`. Watch for repeats.
- **Proc has no try_wait / process-group kill** (s45 W-test): `--timeout` needs a watchdog
  thread; children of a test are not killed. Library gap.
- No sorted set / `keys_sorted` (W-debug); `[_]T(...)` count inference for constants (W-debug).
- s45b: no flush for buffered stdout (W-lsp) — RESOLVED s45c (decisions/049): runtime +
  interp flush before every blocking op, spec §12.9 "Buffered output"; lsp uses Out.
- s45b: thread entry points must be exactly `needs Sys` (W-lsp, 2x); `Channel` cannot tell
  empty from closed (W-lsp); no realpath in Fs (W-lsp, W-cross readlink); temporary array
  slicing — RESOLVED s45c (decisions/049: array → []const T at every coercion site); `make_dir` has
  no mode; constant initializers unchecked until used (tooling must force them).
- s46 (interp): a module function named like a predeclared one hides it completely
  (§4.6/§21.1): interp's `trap` method made the prelude `trap(msg)` unreachable in the
  whole module (3x; used `@unreachable()`/`?`). Candidate: a way to name the universe
  scope (e.g. `core.trap`) or let method-style declarations not hide free functions.
  Logged, not changed. Also `with … on` body locals live in the enclosing C frame (not on
  the fiber stack) — surprising when measuring stacks (implementation detail).
- **`[]u8` results don't flow into `string`** (s47, sitegen worker 4x): `x: string = r else ""`
  and `p := f()` where f returns `[]u8` need an annotation/conversion. Related to the
  str.format pain point above (2x in s42). Now ≥6 total → FLAGGED to user (s47). Idea: let
  `[]u8` coerce to `string` (or make str.format/read_all return string).
- **User-written `Fs` handlers can't make `fs.File`/`fs.Dir`** (s47, playground worker 2x):
  their `handle` fields are private, so a virtual file system must live outside Gamma (JS).
  Idea: a pub constructor for handler authors (`fs.File.from_handle`). FLAGGED (s47).
- **Nested results need typed temporaries** (s47, me, interp/sys.gm): returning `!e` or `!.V`
  inside `Flow!(IoError!T)` targets the outer error type; existing code already writes
  `failed: IoError!usize = !e; return failed` (≥3 sites). Watch.
