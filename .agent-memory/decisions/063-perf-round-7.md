# 063 — Performance round 7 (s62, branch claude/kind-hamilton-ya5z7d)

User ask: review for performance/efficiency/good practice, act on findings, radical
refactors allowed, evidence first. Measured with callgrind (Ir; wall is ±5-10% noisy here).

## Results (Ir, compiler as input; wall in parentheses)
- `build --emit-c compiler` 11.39G → 8.51G, -25% (2.52 → 2.12 s). cgen part ≈ -40%.
- `lint compiler` ≈ 9.9G → 5.2G, -46% (1.86 → 1.33 s). Output byte-identical.
- `check compiler` 4.35G → 4.17G (flat profile; only the adt instance map).
- Formatting micro-bench (str.format + List.write loops): Checked -48%, Fast -32%.
- Generated C of the compiler 49.28 → 49.07 MB; binary 8.55 → 8.68 MB; self-build ~same.

## What changed
- runtime `gm_format_view(b, scratch, t, v, &data)`: string → own bytes, int/bool → stack
  scratch (GM_VIEW_SCRATCH 48), else gm_format into `b`. Used by cgen calls.gm
  `fn_format_stub` and `piece_stub` 'f' (was: malloc'd gm_buf per formatted piece, 4.6M
  malloc/free in emit-c). `gm_int_dec`: ≤64-bit path without __int128 division.
  3 new runtime names in sema/runtime_names.gm (count 302).
- std str.format: `l.reserve(template.len + @size_of(A))` (was 3-5 regrows per result).
- lint stale.gm `growth_summaries`: worklist over incoming edges (`join_paths` formats
  only new growths) instead of a fixpoint loop formatting every (edge, growth) per pass
  (was ~30% of lint). `grow_by_call` returns early when `sc.live` is empty.
- load `Loader.parsed: ?^List(Parsed)` (file, tokens, tree) set by linting.gm; lint.run
  takes it instead of re-lexing/parsing every scoped file.
- cgen: `fg.linef(template, ..args)` (func.gm; @write straight into fg.out; 234 sites were
  `fg.line(str.format(..)!)`); `CTypes.cty` cached by type id (`make_cty`);
  `fg.fresh_tmp()` + `local_lv` use name tables in Gen (tmp_names/frame_names/local_names);
  `frame_locals(f)` and `long_body(g, f)` memoized in Gen (were 2x and 3x body walks per fn).
- sema `adt_instances: HashMap((u32, []const Ty), u32)` for instantiate_adt (was linear).

## Measured, not taken
- List first block ≥ 32 bytes for small elements: little gain once str.format reserves;
  changed 3 expected outputs (leak sizes, custom_allocator) → reverted.
- Suite wall (122 s at -j 4) = the selfhost fixed-point test (100 s under contention; one
  self-build). No object cache for units exists (cache dir holds runtime objects and `run`
  programs only). A content-addressed unit cache would make that test and `tools/test.sh`
  rebuilds near-instant when the generated C is unchanged, but units share a header and
  global numbering (gm_fN, string ids), so small edits change every unit: needs stable
  per-unit naming first. Not started.
- cgen whole-body walks (fn_refs, mutated_locals, direct_calls, collect_upvars, weight,
  has_fiber_with via tir.for_each_child + closure): ~290M self in for_each_child of 8.5G;
  merging them is a larger refactor.
- Remaining str.format in cgen for expression strings (values are C text): inherent to the
  string-building design; one alloc + copy each now.

## Recipe notes
- docs/contributing.md "Profiling the compiler" now has the callgrind recipe.
- Scratch `names.py`/`top.py`: map gm_fN → first non-C `#line` after the definition in the
  `-g` build's gcg*.c; inclusive numbers are useless for recursive passes.
- Baseline for wall numbers: `git worktree add` main, build with target/bootstrap/gamma0.
- Don't edit compiler/ while tools/test.sh runs (selfhost tests read the sources).
