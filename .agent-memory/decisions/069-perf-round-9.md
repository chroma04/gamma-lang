# 069 — Performance round 9 (s73, branch claude/ecstatic-carson-frjfgw)

User ask: "Continue with the open performance items" (068's "next candidates").
Measured as in 068 (callgrind Ir; scratch `compiler_ir.sh BIN` needs GAMMA_STD/RUNTIME set
to absolute lib/std and runtime, else the binary looks for ../lib next to itself).

## Baseline (main after #127) → after (Ir G)
check 2.774 → 2.559 (−7.8%) · fmt --check 1.961 → 1.745 (−11%) · emit-c 7.038 → 6.195
(−12%). Steps (check / fmt / emit-c): addr_of no copy of temps 2.719/1.952/6.908; temp
views 2.684/–/6.876; place-bodied fns + return of any place 2.613/–/6.777; fmt merge cache
–/1.747/–; wrap/err in place 2.562/1.743/6.717; variants in place –/–/6.683; @write string
fast path + gen_body without copies + std find first-byte 2.559/1.745/6.195.

## Done (all cgen unless noted)
1. expr.gm `is_temp(v)` (moved from calls.gm gamma_arg): temps (`t<digits>`) are never
   assigned after their value is computed (invariant relied on by gamma_arg already), so
   `addr_of` returns `&tN` instead of copying. Killed the `T t6 = t5;` scrutinee copies.
2. `load` of a by_ref type from an address that is a temp plus constant offsets
   (`temp_piece` parses `((uint8_t *)&tN)` wrapped in `(X + K)`) returns a VIEW
   `(*(C *)(((uint8_t *)&tN) + K))` (if K % align is 0) instead of a GM_LD copy;
   `addr_of` maps a view back to its address (`view_addr`). try payloads, pattern binds
   from a temp scrutinee, `?` results now copy once.
3. func.gm: a by_ref function body that is a place (`reads_place`, same type as ret) is
   `memmove(gm_out, addr, n); goto gm_fdone` (accessors like `kind => p.tok().kind`);
   return_expr's place shortcut accepts any `reads_place` (was Local/Upvar/Deref).
4. func.gm `tagged_into` (+ `wrap_into`, `variant_into`): builds_in_place now includes
   Coerce WrapOptional/WrapOk, MakeErr and Variant. Payload: nested build only when obs is
   .Fresh; else memmove from a place (address saved in a `const uint8_t *tN` first) or one
   GM_ST of the computed value; then tag + `clear_outside` (blob_over's gap clearing on any
   lvalue; called BEFORE the payload store because it may memset whole).
5. gen_body: the probe's `GM_TEMP_BYTES` offset is recorded (`FnGen.probe_at`); header
   attributes patched on the first line only; pieces extended into g.bodies (was 2 full
   copies of every function's text + a naive find). `indent_line` (one extend).
6. `@write` (fn sink and ambient out/err stub paths): a `{}` arg of type exactly string with
   default spec calls the sink stub with ptr/len (guarded by len != 0) instead of the
   format stub + gm_format_view.
7. lib/std/str `find`: first-byte check before matches_at.
8. fmt/layout.gm: `would_merge` memoized per layout call (`MergeCache` keyed by the two
   token texts); was 22% of fmt (161k lexer runs on joined pairs).

## Also in s73: `@hash` at compile time
eval/ops.gm `hash_value` (+ `bits_of` packs #bits structs; `hash_word`/`hash_byte_words`)
mirrors interp/builtins.gm hash_val and cgen's gm_mix64: any change to the algorithm must
change all three (test run/hash_at_compile_time.gm compares const vs run time on both
lanes; run/hash_structure.gm pins values). Pointers/fn values at compile time = trap
NO_POINTER_HASH (spec §16.8 now says so, and that const = run-time hash).

## Second pass (s73, user: "finish the rest of the open items other than release")
After the first commit: check 2.559 → 2.406, fmt 1.745 → 1.553, emit-c 6.195 → 5.579
(vs main: −13% / −21% / −21%; check 0.82 → 0.66 s). Binary 8.85 → 9.07 MB; cold build 48 s
unchanged.
- lib/core/list.gm: `#inline` on extend (emit −2.8%) and push (check −2%, fmt −6%); push
  writes through `l.items.ptr[n]` with `n +% 1` (no checks in every inlined copy: +463 KB
  instead of +615 KB). A separate non-inlined `push_grown` slow path saved 113 KB but cost
  1.3% emit-c: not kept.
- cgen expr.gm write(): sink that is a `.Closure(f)` literal → pieces and string args call
  `gm_f_<fn>(c.env, ccx, NULL, slice)` directly + check_unwind (emit −2.1%).
- std/arena: chunk_alloc fast path in wrapping arithmetic, `#inline`, new chunk in
  `new_chunk_alloc` (allocates itself: #inline forbids the recursion) → −2% check.
- lexer: flush_newline only if pending; lex_word one-letter tests only for len 1 (−1%).
- SourceMap.add: OR-accumulated ascii test + reserve (small).
- reserve() in hash_text/sanitized/new_site_const/c_string (small).
Tried and dropped: first list block of 8 small elements (−0.35% Ir, +2.5% peak RSS);
lex_string reserve by pre-scan (no gain); merges_with_tail memo (no gain: most calls exit
before lexing).

## Not done / next candidates
- regrow_raw 1.2M calls (99 Ir) in check: long tail of small lists, no dominant caller.
- memcpy ~11M calls in emit-c (small appends); an inline small-copy in extend could help.
- Lexer still ~430 Ir/token: emit is a full frame function (needs Alloc for errors).
- Release single-unit build (068) still open.
