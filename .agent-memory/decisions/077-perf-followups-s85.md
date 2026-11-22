# 077: s85 performance follow-ups (roadmap §2 cleared)

User (s85): "Do the performance follow-ups in the roadmap." All four items done or dropped
with numbers. Baseline = main @ ca7e285 built by itself (Debug -Og); "new" = a compiler
built by a compiler with the changes (codegen changes show one generation later).

## Done
1. Results built in place (cgen, `lc_dest` in lirc.gm, was `lc_call_dest`): producers
   Call(.Fn) (as before) + Aggregate / Wrap / Variant / Coerce(WrapOptional|WrapOk) with
   uses = 1, by_ref type; consumers Return (gm_out without defers, `F.ret` with defers →
   `lc_return_built` does is_err + start_exit) and whole-local Store (not addressed,
   captured, borrowed). Mechanism: `LirC.build_dest` → `lc_build(make)` sets
   `FnGen.blob_dest`, which `blob`/`blob_over` (`blob_var`) take; `dest_taken` tells if
   it was used (bits/zero-size/ptr-repr don't take it → old path). Operands are read
   before the first store, so aliasing (`x = f(x)` passes &x as both gm_out and by_ref
   arg) stays safe: callees still write gm_out only at the end.
   Compiler C: memmove 10,601 → 5,277; `memmove(gm_out` 5,464 → 273; `memmove(&F.ret`
   141 → 6. parse_head_expr frame 151 → 55 B.
2. LIR `let` result hint (`Lw.into`/`result_for` in lir/lower.gm): `x := if/match/else`
   stores branch values straight into x (only `let`, a fresh local; type must match);
   the trailing `load x` is popped. Removes the l2→l3→l1 copy chain of
   `x := f() else …`. Golden tests/lir/effects.lir re-blessed.
3. Statement-run C blocks (`lc_runs`): in short lists (≤ LONG_LIST) with ≥ 3 insts and
   ≥ 2 top-level blob dsts, runs between points where no top-level temp is live (and not
   between an lc_dest pair) that write a blob get `{ }`. Outer lazies are computed before
   the block (a flush inside would declare them in it); the list's own pending are dead.
   Liveness via stamped arrays (`run_gen`/`run_last`), blob flag lazily (`is_blob`).
   Stack totals of the compiler's C (exact build flags, one unit, -fstack-usage):
   -Og 2,592,400 → 2,420,768 (−6.6%); -O2 2,106,592 → 2,036,944 (−3.3%).
4. Template forwarding (check/share.gm `forwards`, `forward_call`, called from
   program.gm mono_expr): g counts as a template use when passed as EVERY template param
   of an instance of another sharing generic and in no other arg (top level only, not
   f itself). In f's shared body the call becomes Call(G_shared, args + table/text locals);
   G's sites are copied to f's sites (param mapped), so f's instances check templates
   against G's writes (same messages, at the user's call). `template_params` memoizes
   empty first (cycle guard: mutual forwarders keep instances). Thunks (instance bodies)
   now get only the param locals. Test: native_build forwarded_templates_share_one_body,
   conformance check/template_shared_errors (`note`).

## Measured totals (Ir, callgrind, Debug compiler)
- check compiler 1.9679 G → 1.9639 G (−0.2%); emit-c 5.827 → 6.004 G (+3.0%: lc_runs'
  run detection ≈ +110M (dst_of, is_blob, visit), forwarding/thunks ≈ +20M, more C
  text). C text 50.28 → 51.11 MB (+1.6%, braces + indentation).
- Cold `gamma build compiler` (4 cores): 48.9/49.5 s without lc_runs vs 48.5/48.9 s with.
- CFLAGS/CC env overrides change the build (CC wrapper loses gcc detection → no
  GCC_DEBUG_PASSES; CFLAGS too): measure stack by compiling the emit-c output by hand
  with BASE_CFLAGS + -Og + GCC_DEBUG_PASSES (+ -fstack-usage). See gotchas.

## Dropped
- Nested helpers in split long functions: a run's instructions each weigh ≤
  LIR_RUN_WEIGHT (600), heavier ones stay inline and get their own helpers, so a helper is
  bounded (~4×600 insts). Measured: 40 ifs × 550 statements (22k) builds in 14.0 s vs
  14.6 s for the same statements flat; largest helper 1,297 lines. Nothing to gain.
- In-place payload copy for `return Some(local)` (load + store of an 88-byte payload →
  one memmove): needs proof the source is not a by_ref param aliasing gm_out; not done.
- Building an Aggregate straight into the payload of the Wrap that consumes it (two-level
  chain `aggregate → coerce WrapOptional → return`): not done; one 48-byte copy left.
