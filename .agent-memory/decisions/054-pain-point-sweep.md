# 054 Pain-point sweep (s50, 2026-10-27)

User asked: for every pain point, discard what is done / not an issue, design + implement
the rest. Triage result lives in spec-notes/pain-points.md ("Settled"). Designs:

- L1 `else` join (§10.4): fallback U that doesn't coerce to T, but T→U only adds const →
  the LEFT side is coerced to ?U / E!U (check/errors.gm else_join) so the TIR `.Else` stays
  homogeneous; no back-end change. Numeric widening is NOT lifted (`?u8 else u32` errors):
  lifting non-free coercions under `?` would hide a branch.
- L2 const-adding coercions inside `?`/`!` (§6.4 row): check/coerce.gm `adds_const`;
  emitted as `.PtrToPtr` (cgen reinterprets same-layout values, interp copies bytes, eval
  returns the value). Nullable pointers keep `.InOptional`.
- L3 arrays of one element type join as `[]const T` (§7.12): expr.gm array_elem_join;
  temporaries held by the existing §15.1 rule. `for` over a tuple now iterates at the join
  (seq.gm tuple_elem_join), elements coerced (arrays of a place-tuple slice in place).
- L4 contextual `needs`/`handler`/`foreign` (§2.4): lexer emits identifiers; parser decides
  by position (`needs` where parse_needs_opt runs; `handler` + ident / top-level `handler(`;
  `foreign` + string). COST: a newline after `needs` is no longer suppressed (§2.8 rule 1)
  since the lexer can't know. Tree-sitter: identifier = choice(word, the 3 words) + GLR conflicts; queries scope them. Checker's
  `.Error` node fix only visible in LSP (keep_going); test in harness lsp/diagnostics.
- L5 `{0}` placeholders: all-or-none, every value used, values bound once to temps in
  check/template.gm (no back-end change). L6 `{:q}` = FmtKind.Quote (rt kind 6, interp).
- L7 local struct/union: sema/adt.gm declare_local_adt; not generic, may not mention the
  fn's type/value params, no methods, no forward refs between local types.
- L8 lint `stale-element-pointer` (compiler/lint/stale.gm): sources &L.items[i]/L.items
  slices; growth = core List growing fns or `^List` arg; loops scanned twice. MISSES the
  common shape: growth via a method on the struct holding the list (`p.alloc_expr()`).
- S1 std/text is_space/trim*/fields; S6 thread.drained.
- S2–S5 (sys): Fs canonical/read_link/symlink/set_mode/set_modified + FileInfo.mode;
  Proc.try_wait + SpawnOptions.program (@proc_launch replaces @proc_spawn); pub `handle`;
  thread.defaults has Env (@env_args/@env_var/@env_exit). Needed a SEED REFRESH (prelude
  uses new intrinsics): built 2-stage, refreshed seed, bootstrap + verify.sh OK.
  Interp difference: Env.exit on a thread runs main's defers in the interpreter, not in C.
  Follow-up: harness kit/command.gm Watch thread → try_wait loop (seed now has it).
