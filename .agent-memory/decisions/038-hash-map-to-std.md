# 038 — HashMap moves to std; no relational constraints (s34, agreed with the user)

Question from the user: worth adding multi-parameter constraints (`Contains(C, T)`) for a
generic `in`? And why are `List`/`HashMap` core when the spec's reason ("used by examples
and the test runner") was weak?

Decision:
- Core admission rule (§22.1) = what the language definition refers to. `List` qualifies
  (`collect` returns it, `@write`/`l.write` into `List(u8)`, §22.3 text-out convention).
  `HashMap` does not → moved to `std/collections/hash_map`. No other containers promoted.
- Mechanism: `lib/core/map.gm` stays a core module (test tracker in prelude needs a map;
  core can't import std). `modules.rs` CORE_MODULES has a `bind` flag: the prelude gets a
  pub module decl `map` (hidden from the universe via UNIVERSE_HIDDEN; `universe_hint`
  skips module decls). Std module re-exports `core.map.HashMap/hash_map/hash_map_with/Entry`
  (like std/fs `core.File`); methods are documented in its `//!` module doc.
- Transition: prelude has `#deprecated` aliases `HashMap`, `hash_map`, `hash_map_with` →
  `map.*` (same declarations, so old/new spellings mix). Spec §21.1 "Transition" note.
  REMOVE them (and the note, and the check test lines) in the release after 0.1.0; then
  consider an import fix: `std_home` would need "std/collections/hash_map" and import_fix's
  alias `&path[4..]` must become the last path segment (`hm`?).
- Convention: import as `hm` (like `om`, `dq`).
- Constraints stay one-type predicates; `in` only for built-in forms (§7.7 note). C.8 rows:
  relational constraints, `in` on library containers, more core containers.
- Test: tests/conformance/check/hash_map_moved.gm.
