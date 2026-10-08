# Known spec issues not yet resolved (decide when the milestone arrives)
- @stack_size (§14.8) with a C backend: we can't know cc's frame sizes before compiling.
  Idea: estimate conservatively in Gamma, then verify with `cc -fstack-usage` after compiling
  and fail the build if exceeded. (M12)
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
