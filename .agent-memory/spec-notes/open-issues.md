# Known spec issues not yet resolved (decide when the milestone arrives)
- §6.5 "`T(x)` with a single positional argument whose type is not a tuple whose fields fill T"
  is murky for tuple types `T`. Clarify in M3.
- §21.1 puts `current` (Alloc op) and other effect ops in the universe; common names — fine since
  hideable, but watch for confusion.
- `@write` display of `[]u8` vs `string` (identical types!) — table distinguishes "string or []u8
  as an argument itself"; both same type so consistent. OK.
- Hex float requires '.', so `0x1p4` is not a literal. Kept; maybe relax later.
- `1f32` is not a float literal (needs `.` or exponent). Kept.
