# 060 — stale-element-pointer follows growth through struct pointers (s58)

Pain point (open since s50): `e := &p.exprs.items[i]; p.alloc_expr(); e.kind = …` was not
reported: the lint only marked lists passed as a `^List` argument.

Chosen: per-function growth summaries (`growth_summaries` in compiler/lint/stale.gm), computed
once per lint run before the scans (checked.gm `run_checks`).
- A summary is a set of (param index, field path) — path in `place_key` form (".3.0").
- One walk over every non-instance body records (a) direct growth (core grows() list fns
  on arg 0, or any program fn given `^List`) rooted at a mutable-pointer parameter, and
  (b) call edges caller(param, path) → callee(arg) for non-core callees (instances mapped
  to their template with `generic_of`). Then a fixpoint over the edges.
- Path depth capped at 4 fields (MAX_GROWTH_DEPTH): recursion through pointer fields would
  otherwise grow paths forever.
- Core callees use only the `grows()` name list (their bodies are not summarized), so the
  deliberate non-growing set (pop, clear, truncate, …) stays exact.
- Rejected: "any `^Struct` call grows every list under it" — floods the compiler with false
  positives (`p.peek()`).

Not followed (documented in docs/lint.md): aliases (`l := &p.exprs; l.push()` inside the
callee), calls through function values, indexed places, upvars.

Result on the compiler: 5 new findings, all reachable paths (resolving a decl/import may push
to `s.defs`, `s.fns`, `l.other_items`) though unlikely at run time since imports load eagerly;
fixed by reordering / indexing again (lint/checked.gm, lint/stale.gm, sema/foreign.gm
apply_callconv, check/tests.gm, check/handler_decls.gm). Lint run on compiler/ stays ~2.5 s.
