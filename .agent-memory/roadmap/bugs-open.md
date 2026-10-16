# Open implementation bugs, gaps and wishes (current; delete entries when fixed)
Numbers are stable ids (gaps are fixed entries). Checked 2026-10-16 (#50–53 fixed in s17; s19 fixed the small dogfood papercuts).

## Performance
7. C big values: gm_out results + in-place construction + borrowed params of pure fns
   (decisions/016). Left: nested aggregate elements still go through a temp; gcc SRA is weak
   on uint8_t-array blobs (bigcopy bench: gcc 1.9 s vs clang 0.13 s).
24. Interpreter: the bytecode VM (decisions/020) replaced most tree-walking cost; what is left
    is dispatch/call-bound (~15 ns/op), plus `new`/`destroy` spread over builtins. Follow-ups
    listed in decisions/020.
48. gcc time superlinear in the size of ONE C function (9.4k-line main: ~20 s at -O1; -O0
    worse). Real fix: outline long statement runs into functions sharing the frame (like
    fiber.rs with-on outlining; defers, gm_unw exits, backtrace, stack accounting). Remaining
    C ≈14 lines/statement (checked-arith option memsets, ie/ix labels, F.lN).
49. gcc superlinear / segfaults on very deep nested C expressions (60k `.abs()` chain: 12 s;
    100k: gcc crash, reported cleanly). Flatten emitted expression chains into statements.
- Safe mode: new/destroy ≈ 4x Checked (GC alloc of `new`; `&key` in std maps escapes).

## Behaviour differences (allowed, documented)
12. Float printing at exact ties picks the larger candidate (…945.3 vs Ryu …945.2); allowed by
    §21.6.1 (shortest round-trip) but differs from Ryu/Python.
28. `print("{}", bytes)` of []u8 prints raw bytes (by §21.6.1); no numeric display option.
29. Stack-overflow backtrace depth differs between back ends (frame sizes) — stderr goldens
    for fiber overflow can't be shared.

## Wishes (unscheduled; each must clear §22 if it is a library addition)
23. std channels/queue/generator; lstat on symlinks untested; reflection.

## Dogfood papercuts (s18 examples; unscheduled)
- Struct-typed errors get no `!.Variant` shorthand / lifting (by design; friction).
- std wish: std/fiber channel + sleep-aware scheduler (Clock.sleep blocks the thread).
