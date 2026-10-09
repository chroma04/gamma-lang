# Known spec issues not yet resolved (decide when the milestone arrives)
- Generic bodies checked once (§16.1) with array lengths depending on comptime params
  (`[N*2]T`): type equality needs symbolic lengths. Plan: equal iff normalized length
  expressions are identical. (M5)
- §6.5 "`T(x)` with a single positional argument whose type is not a tuple whose fields fill T"
  is murky for tuple types `T`. Clarify in M3.
- §21.1 puts `current` (Alloc op) and other effect ops in the universe; common names — fine since
  hideable, but watch for confusion.
- `@write` display of `[]u8` vs `string` (identical types!) — table distinguishes "string or []u8
  as an argument itself"; both same type so consistent. OK.
- Hex float requires '.', so `0x1p4` is not a literal. Kept; maybe relax later.
- `1f32` is not a float literal (needs `.` or exponent). Kept.
- free/destroy pass @align_of(T) to the allocator, so memory from make_aligned is freed with a smaller alignment than it was allocated with; harmless for the heap and trackers, but an allocator that needs the alignment back can't get it. Possible fix: free_aligned(s, align).
- §6.2 vs floats: `-0.0` as an exact rational constant loses its sign; there is no literal for negative zero (std/math builds it with .bits). Decide whether a negated float literal keeps the sign.
