# 072 — Performance round 10: the roadmap's remaining perf items (s77)

User ask: "Tackle the rest of the performance items in the roadmap" (next.md §2 after s76).
Every item was measured; this file says what was kept, dropped and why.

## Measuring (scratch scripts, rebuilt each session)
- `g` wrapper: GAMMA_STD/GAMMA_RUNTIME/GAMMA_CACHE_DIR absolute + `${GBIN}`;
  target/bootstrap/gamma alone cannot find lib/ (looks in ../lib/gamma).
- `iir.sh BIN`: callgrind Ir (M) of `run --backend=interp` on bench/programs/* + primes,
  regex, utf8. `cir.sh BIN`: check / fmt --check / emit-c of `compiler` (G).
- `calls.py OUT [rx]` (self Ir, calls, Ir/call), `callers.py OUT rx` (callers of a symbol,
  e.g. the memcpy PLT address), `diff.py A B` (per-function self Ir delta, hash stripped),
  `asm.py OUT BIN SYM` (needs `--dump-instr=yes`; positions are "instr line", Ir is the
  3rd column): per-instruction Ir of one function. Found the 11-iteration consts loop.
- NOTE: bootstrap/build.sh and `gamma build` default to DEBUG mode = gcc `-Og` (+3 passes):
  all compiler numbers here and in 068/069 are of the -Og build. Fast (-O2) is −15%.

## Results (Ir; base = main after #132 → end)
Interp: map_insert 531.4 → 432.6 M · new_destroy 378.0 → 265.8 · primes 4579 → 3147 ·
call/perform/with/iterate/list_push/regex/utf8 unchanged (±0.2%).
Compiler (Debug build): check 2.430 → 2.395 G · emit-c 5.637 → 5.566 · fmt unchanged.
Compiler C (emit-c) 54.26 → 53.78 MB. Debug C of a test program −0.7%.

## Kept
1. interp heap.gm: AllocTag stores (owner, vt, file ptr/len, line, column); the site string
   is formatted only for the mismatch trap (`tag_site`). It formatted (and leaked) one
   string per allocation in Debug runs.
2. code.gm low_builtin: `@hash` of a word int with an unsigned word seed → `.IntRot(.Hash…)`
   → bit_word computes it (same algorithm as builtins.gm hash_val/hash_u64).
3. vm_compile `.Str` to a slot: m.string_literal at compile time → `ConstSlot` (was
   StrSlot = hash lookup per execution).
4. 128-bit: vm_compile `vc_low_word` (Trunc u128/i128 → word = SlotW of the first bytes,
   LE only); BinSlot `wide` now also for Lt/Le/Gt/Ge (bool result: BinSlot exec writes 1
   byte when n.size is not 16), Shl/Shr by an unsigned word (< 128; Shl Wrap or Checked
   without overflow), signed Div/Rem (not MIN / -1); WidenSlot `ext` (bits | 128 if
   signed) widens without coerce_bytes. Test conformance/run/int128_compare_shift.gm.
5. check/exhaust.gm `first_uncovered`: variants by a bool list instead of
   column_covers per ctor (the Wild arm was O(ctors × rows), 30M Ir in check).
6. cgen calls.gm call_site: `GM_BT_AT(B, gm_srcN, line, col)` (gm_rt.h macro) instead of
   repeating the path literal per call.
7. lib/core/map.gm: grow → `grow_raw(^RawMap, size, align, hash_slot)` (the HashMap layout
   with slots as bytes; `state` is the slot's first byte, fields in declaration order);
   hash_slot is a capture-free literal calling key_hash. Passing `slot_hash(K, V, hash,
   eq)` (an instantiation) failed: "cannot check this compile-time argument yet".
   −480 KB of compiler C; Fast map_insert (1M ops) +0.15% Ir.

## Measured and dropped
- `gm_copy` inline small copy for @copy_elems (≤16 bytes: two loads/stores): memcpy calls
  11.5M → 2.3M in emit-c but the inline code cost as much (-Og does not inline a plain
  `static inline`; with always_inline: check +0.6%, emit −0.5%, +105 KB). Glibc's small
  memcpy is already ~25 Ir.
- vm_consts as one block copy: utf8 −2% but call +5%, perform +4.7%; hybrid (>4 → copy)
  still +1.6% call. Bodies average 13 consts in utf8; only removing the copy would help.
- make_fill via `s.fill(x)`: no change measurable.
- Release single-unit build (068): re-measured Fast split vs GAMMA_C_UNITS=1: check −3%,
  fmt −4.4%, emit −5%, binary +23% (8.9 vs 7.2 MB), build 53 s → 3 min 19 s. Not worth it
  (068's −15..27% predates SHARED_SMALL_BYTES and static-inline sharing).

## Closed without change (reasons)
- Sort per T (19 instances, ~700 KB of compiler C = 1.4%): erasing needs byte swaps of a
  run-time size and a double-indirect `less`; sorting would get slower for a 1.4% C cut.
- Safe new/destroy 3.6x: the cost is Fil-C's malloc/free; a runtime free-list would
  recycle objects and defeat Safe's use-after-free detection.
- Lexer (~300 Ir/token over emit/lex_word/lex_token/scan_punct/skip_trivia) and regrow_raw
  (3M calls, 100 Ir, long tail): flat profiles, no single fix (see 069).
- Interp push (vm_push_site ~170 Ir self in call.gm, 250 in utf8: memset of the frame,
  consts loop 11 iterations avg in utf8, moves with memmove for 16-byte slices): only
  structural changes remain (consts as immediates; frames without memset, which needs
  proof that registers are written before read).
- `with` (1.9k Ir/iteration): vm_perform_site recomputes per new ctx (find_ctx, cmp_ref,
  is_ctl ~270 Ir); is_ctl could be cached per site (~30 Ir). Small.
- Generated C: `str.format` instances are 6.9 MB = 14% of the compiler's C (1,422
  instances) — the open pain point "template code size"; a language change, ask first.
