# Known spec issues not yet resolved (decide when the milestone arrives)
- Computed array lengths from comptime params (`[N*2]T`): §16.5 forbids them (diagnosed).
  Possible extension: symbolic lengths, equal iff normalized expressions are identical.
- §6.5 "`T(x)` with a single positional argument whose type is not a tuple whose fields fill T"
  is murky for tuple types `T`. Clarify in M3.
- §21.1 puts effect ops (`exit`, `open`, `read`, `close`, `now`, `current`…) in the universe.
  Evidence so far: one dogfooding report (user op `exit` hid `Env.exit`, confusing errors).
  Mitigated s10: errors at calls of a user op that hides a universe op carry a note naming
  both (Sema::hidden_universe_op). Names kept; revisit only on more reports.
- `@write` display of `[]u8` vs `string` (identical types!) — table distinguishes "string or []u8
  as an argument itself"; both same type so consistent. OK.
- Hex float requires '.', so `0x1p4` is not a literal. Kept; maybe relax later.
- `1f32` is not a float literal (needs `.` or exponent). Kept.
