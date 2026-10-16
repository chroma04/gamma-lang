# Open implementation bugs, gaps and wishes (current; delete entries when fixed)
Numbers are stable ids (gaps are fixed entries). Checked 2026-10-16 (#50–53 fixed in s17; s19 fixed the small dogfood papercuts).

## Performance
7. C big values: gm_out results + in-place construction + borrowed params of pure fns
   (decisions/016). Left: nested aggregate elements still go through a temp; gcc SRA is weak
   on uint8_t-array blobs (bigcopy bench: gcc 1.9 s vs clang 0.13 s).
24. Interpreter: the bytecode VM (decisions/020) replaced most tree-walking cost; what is left
    is dispatch/call-bound (~15 ns/op), plus `new`/`destroy` spread over builtins. Follow-ups
    listed in decisions/020.
48. C compilers superlinear in the size of ONE C function (9.4k-line main: ~20 s gcc -O1;
    gcc -O0 "expand vars" quadratic). s19 mitigations: long op chains (>256 links) go through
    an accumulator with one block per link (cgen/chain.rs; fixed clang lifetime-marker blowup),
    GM_HUGE_FN (>20k temps: gcc no-tree-pta/no-thread-jumps), -Wno-free-nonheap-object in
    BASE_CFLAGS (stops gcc's waccess pass). 100k-term sum: gcc 187→25 s, clang >8 min→17 s.
    Left: outline long statement runs into functions sharing the frame (defers, gm_unw exits,
    backtrace, stack accounting); trap sites repeat the absolute path string (38 MB C for 100k
    terms, ~10 s gcc parse); by-ref (struct) chains not flattened. (Old #49 "deep nested C
    expressions" was a misdiagnosis: cgen already spills every operand into temps.)
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
