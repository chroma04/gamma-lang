# Known spec issues not yet resolved (decide when the milestone arrives)
- §21.1 puts effect ops (`exit`, `open`, `read`, `close`, `now`, `current`…) in the universe.
  Evidence so far: one dogfooding report (user op `exit` hid `Env.exit`, confusing errors).
  Mitigated s10: errors at calls of a user op that hides a universe op carry a note naming
  both (Sema::hidden_universe_op). Names kept; revisit only on more reports.
- `@write` display of `[]u8` vs `string` (identical types!) — table distinguishes "string or []u8
  as an argument itself"; both same type so consistent. OK.
