# 049 — Temporary arrays as slices everywhere; flush before blocking (s45c)

Design doc approved by the user (Claude Doc "Gamma design notes: flushing output and
slicing temporaries", https://claude.ai/artifact/MD8rzwHbz4PNjLiYSrimcC).

## Arrays → `[]const T`
- `coerce` (src/sema/expr.rs, compiler/check/coerce.gm): positional array → const slice
  when elems unify; `array_as_slice` (seq.rs/.gm) used by coerce and call args.
- Any temporary array can be sliced (`slice_expr`): read-only slice. Old error "only an
  array variable (a place) can be sliced" is gone from slice_expr (slice_of keeps it).
- Lowering: hidden immutable local `<temporary array>` (seq::TEMP_ARRAY), pushed to
  `FnCx.block_temps`; expression becomes `Block{ Assign(t, arr); Slice(t) }`.
  `block_expected` declares new temps (`Let{init: None}`) before each statement; temps
  made in a value block's TAIL are left for the parent statement (spec §15.1 "outward").
  Body boundaries call `declare_temps(0, body)`: fn_body, fn literal, clause
  (finish_clause / handlers.gm), handler_decl clauses. `block_temps` saved in frames.
- Const initializers (`const_init`): old AddrOfTemp path; evaluator rejects kept ptrs.
- Escapes: escape.rs/escape.gm + lint_escape.rs/dangling.gm special-case the name.
- `&e` for `^const T` stays call-only (is_const_ref_arg lost its Slice case).
- Known gap: `break v` carrying a temp slice out of a loop body is not checked (UB).

## Flushing
- gm_flush() at the start of futex_wait (3 impls), thread_join, net connect/accept/recv,
  proc read_out/wait, fs_read (POSIX + Windows); interp sys.rs flushes for the same
  SysOps + Sleep. Test: tests/native_build.rs output_is_flushed_before_waiting.
- No `Out.flush`, no std flush function (option C deferred until a real need).
