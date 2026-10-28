# Open implementation bugs, gaps and wishes (current; delete entries when fixed)
Numbers are stable ids (gaps are fixed entries). Checked 2026-10-16 (#50–53 fixed in s17; s19 fixed the small dogfood papercuts).

## Performance
7. C big values: gm_out results + in-place construction + borrowed params of pure fns
   (decisions/016); s21: args that are fields of locals/derefs/params passed in place
   (expr.rs in_place_root), small fns `static inline` in non-debug builds (gcc -O2 did not
   inline them: bigcopy-style bench gcc 1.13 → 0.33 s, clang 0.4 s). s22: array/slice/
   many-ptr elements passed in place too (in_place_root; run/big_value_index_args).
   Nested aggregates (`M(a: V(..), b: mk())`) are already built in place (checked s22).
24. Interpreter (s21: perform 873 → 817 instr/op over a call's ~616; s24 re-profiled: vm_perform
    ~450 instr inclusive, no allocation, spread over ~40 bookkeeping steps of 1–16 instr —
    St reset, FrameExtra move, Ctx clone, call-site fields; no single fix, left alone): the bytecode VM (decisions/020) replaced most tree-walking cost; what is left
    is dispatch/call-bound (~15 ns/op), plus `new`/`destroy` spread over builtins. Follow-ups
    listed in decisions/020.
48. C compilers superlinear in the size of ONE C function (9.4k-line main: ~20 s gcc -O1;
    gcc -O0 "expand vars" quadratic). s19 mitigations: long op chains (>256 links) go through
    an accumulator with one block per link (cgen/chain.rs; fixed clang lifetime-marker blowup),
    GM_HUGE_FN (>20k temps: gcc no-tree-pta/no-thread-jumps), -Wno-free-nonheap-object in
    BASE_CFLAGS (stops gcc's waccess pass). 100k-term sum: gcc 187→25 s, clang >8 min→17 s.
    s29: statement runs outlined (cgen/outline.rs, decisions/031): 10k-stmt main gcc Debug
    87→4 s, Fast 44→11 s, clang 87→5 / 83→9 s. Left: by-ref (struct) chains not flattened. (s20: trap sites
    name the file via one `static const char gm_srcN[]` per file: 100k terms 31→27 s gcc.) Clang ≥19 (Apple ≥17;
    macos-latest = macOS 26 image): EarlyCSE → simplifyInstruction(icmp) → isKnownNonEqual →
    computeKnownBits/adjustKnownBitsForSelectArm ~cubic in chain length (abs chain 10k links
    178 s, 50k hours → macOS CI 12 min → 2h48). Shape tweaks (asm barriers, overflow builtins,
    opaque conds) did NOT help. Fix (PR #44, merged): GM_LONG_FN
    (>2,000 names, func.rs LONG_FN_NAMES) = optnone,noinline on those clangs; 50k ≈ 35 s.
    Real programs: max ~770 names/function (examples/calc). (Old #49 "deep nested C
    expressions" was a misdiagnosis: cgen already spills every operand into temps.)
- Safe mode: new/destroy ≈ 3.6x Checked net of startup (s21 measured: Fil-C malloc+free is
  3.7x glibc on its own; by-ref `site` param copies and handler blob loads cost ~50–90 instr
  each under Fil-C; `&key` in std maps escapes).

## Behaviour differences (allowed, documented)
28. `print("{}", bytes)` of []u8 prints raw bytes (by §21.6.1); no numeric display option.
29. Stack-overflow backtrace depth differs between back ends (frame sizes) — stderr goldens
    for fiber overflow can't be shared.
(#12 was stale: f16 checked exhaustively vs an exact nearest/ties-even reference in s26,
 run/float_print_ties covers it. #30 empty Out chunks and #31 huge-frame message fixed in s26: C skips empty formatted
 pieces, spec says pieces are non-empty; C emits a trap-only body when tir::frame_cost > MAX_FRAME.)

## Correctness gaps
(#54 fixed in s32: types over isize::MAX of the target are a sema error.)

## Dogfood papercuts (s18 examples; unscheduled)
- Struct-typed errors get no `!.Variant` shorthand / lifting (by design; friction).
- s49: after a syntax error inside an `if` condition (`if needs := f() {`), `gamma check`
  also reports "gamma cannot check this kind of expression yet" for the `if` (checker meets
  the parser's error node). Should stay silent after a parse error.
- s50: a local alias named like a visible type (`Point :: i32` with a top-level `Point`)
  is accepted, though §4.6 forbids it; fixing it may break existing code (check repo first).
- s50: `gamma lint` has no unused check for local struct/union types.
- s50: difftest seed 91451 (CI rotating seeds, PR #102): gcc 15.2 (Debug) prints
  `null 229 !.Worse` where interp/clang/gcc 13/14 give `null 0` for
  `((p1 -? 1u32 else p1) & e122.sat(u32)).checked(u8)` inside `match … { !.Bad(e122) => …}`
  with p1 = 0. Program C identical on main → pre-existing; gcc-15 not installable here.
  Either a gcc 15 bug or latent UB (UBSan on gcc 14 clean). Reproduce with gcc 15.
