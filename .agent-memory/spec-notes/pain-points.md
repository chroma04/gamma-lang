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
