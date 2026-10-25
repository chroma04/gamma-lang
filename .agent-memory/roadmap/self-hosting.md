# Self-hosting roadmap (decisions/043; public doc docs/self-hosting.md)
State (s38b): stage 1 lexer + stage 2 parser done; parity (dump + JSON diags) on all ~980
.gm files, plain and --recover. gammac modules: source, diag, bignum, syntax (tokens,
lexer, token dump), ast (tree + dump), parse (parser), main.gm (lex|parse CLI).
Port took ~1 session by hand (no subagents): 1:1 file mapping to src/syntax/parser/*.rs
made it mechanical; matched on first full-corpus run.

## Stage 3 done (s38c): `gammac modules` == `gamma modules` on ~785 program roots.
Gaps: git/url deps, malformed-manifest messages, general `when` conds (decisions/043).

## Stage 4a declarations (s38d): `gammac decls` == `gamma decls` (src/sema/decl_dump.rs)
- Reference `gamma decls` = load_prelude + load_root + resolve_declarations (check_all's
  first loop + method candidates + import cycles + pub signatures), no bodies, no core
  full-resolve. Dump: per non-core module, scope in DeclId order, `name :: <value>`.
- gammac: compiler/types (interner keyed by byte encoding in HashMap(string,u32), display
  w/ budget), compiler/sema (sema/names/ty_expr/adt/fnsig/generics/untyped/consts/layout/
  dump), compiler/bigint + ratio (sonnet subagent; by-value API, field `negative`),
  load/core.gm (core from `$GAMMA_CORE` or std/../core; target module text).
- Skip protocol: unsupported constructs report "gammac cannot ..." → test skips root;
  DECLS_FLOOR in tests/selfhost.rs (set 630; survey had 635 SAME / 157 UNSUP / 0 DIFF of
  792). consts.gm never emits real errors: unknown → unsupported.
- Survey tooling idea: run gammac decls per root in parallel, compare with
  `gamma decls --diag-format=json` (script in session scratch, easy to rewrite).
- Remaining UNSUP buckets (s38d): typed consts calling fns (~77, need bodies+eval),
  foreign blocks 22, vector types 18, defaults 9, #soa 8, #global 6, array lengths that
  are typed exprs 5, #export/#fuel/#callconv/#trap_hook, @type_of.

## Stage 4b bodies (s38e, in progress): `gammac tir` == `gamma tir` (src/sema/tir_dump.rs)
- Reference `gamma tir` = sema::typed_program: check_all (+tests_enabled if root has no
  main) + check_inline/escapes/continuations; dump only if no errors. Fns sorted by span;
  label `name@path:l:c`; instance `label[args]`; one line per expr `kind ops : type`.
- gammac: compiler/tir (TIR data, for_each_child), compiler/check (func/expr/call/coerce/
  infer/finalize/program/dump). Sema gained defs (List(tir.FnDef) parallel to fns),
  pending_bodies (popped LIFO like ref), inline_fns, default_exprs, complex_of,
  is_ambient; loader keeps other_items (tests/handlers/intrinsics → unsupported).
- PR #76 merged (infra, 55). s38f added: patterns (pattern/exhaust/matching.gm), match,
  binding conds, `is`/`in` patterns, for over ranges/arrays/slices/tuples/range values,
  index/slice/.len/.ptr, array construction, methods (methods.gm: builtins, module fns,
  effect ops, E.op), errors.gm (else/try/!e/?/!/lift), escape.gm. `&e` passes expected
  pointee as hint (ref + gammac + spec §7.2). TIR_FLOOR = 100 (0 DIFF of 784).
- s38g generics DONE: generic bodies (type params scoped, numeric_params), generic_call
  (port of generic_call_packed: receiver binds, ranks, literal fallbacks, `()` defaults,
  value params via comptime_value + normalize_value, constraints, inst_only), instances
  = FnSig with `instance` set + empty def (dump skips, label `f@..[args]`), mono walk in
  program.gm (instantiates nested calls, depth 256, per-instance literal/.bits/type-size
  checks; core templates have no bodies → not followed), site defaults (SourceLocation
  aggregate), methods of generic types (fits_receiver by template), core slice methods
  (slice_fn), `G(T)` in bodies (type_of_name, constructions, struct patterns),
  ValueParam TIR + len_value, `T(lit)`, generic fns as values. 137 SAME / 0 DIFF of 794.
  Still unsupported: effect-set params (18), rest params (25), spread (55), generic
  effect ops (28 roots), iterator args.
- s39: defer/errdefer (FnCx in_defer, defer_label_base, fn_performs), typed constant
  initializers (sema/consts.gm const_check fast path; else annotated ones go to
  Sema.pending_consts and check/consts.gm checks them as exprs before bodies; accepted
  only if `cannot_fail` (no calls/trapping ops) — the reference evaluates, gammac does
  not), `E.V` untyped consts, intrinsics (check/intrinsics.gm: layout/limit/bit/float/
  black_box/type_name; params → unsupported), `?T(x)` → unsupported. 190 SAME / 0 DIFF.
  Then fn literals (check/fnlit.gm: frames via enter_fn/exit_fn, FnCx.outer, Upvar,
  Closure, nested fns finished in `finish`), call_value/Callee.Indirect, and `with` +
  handler literals (check/handlers.gm; ClauseInfo per frame, find_jump across frames,
  Target.InstallingWith, LabelBreak.depth, FnCx.handler_breaks, return_target levels).
  Handler decls (check/handler_decls.gm: declared before main, clauses checked after
  bodies, Sema.handler_decls/pending_clauses/decl_clause_breaks/decl_with_checks; only
  non-generic, templates just set Sema.handler_templates). Unsupported: generic effects,
  ctl ops, `with … on`, handler decls for generic types, `with` in generic fns, handler
  literals bound to locals (Rust pending_handlers, 8 roots). 239 SAME / 0 DIFF.
  Iterators (check/iterators.gm: for_iterator = clause "<for body>" of a HandlerLit for
  Yield(T), ClauseInfo.for_label/break_ty, hoist_args/lift_temps; perform_generic for
  bare `yield(x)` via `available` = declared of all frames; iterator_calls +
  iterator_call_as_value messages; location_function skips `<…>` frames). 278 / 0 DIFF.
  Local `when` (loader's when_cond; only the taken branch). Tests: the Rust `gamma tir`
  dropped test items (tests_enabled set after load); fixed (modules.rs always collects),
  gammac check/tests.gm mirrors check_tests (`test "n"` fns, `<bench>` fn calling
  run_bench, Sema.in_test try message). 320 SAME / 0 DIFF.
  Sole-blocker roots now: `with … on` 14, intrinsic calls 11, vector types 10,
  @size_of(T) 9, handler literals in locals 8, tuple payloads 8, iterator args 8,
  foreign blocks 8, #bits 7.
- s40 (branch claude/admiring-wright-4lo3vq): type-param intrinsics (SizeOf/IntLimit/
  TypeName TIR), tuple payloads + tuple constructions (construct_tuple), optional
  arithmetic (chained_opt_op), `ctl` + resume/cancel + `with … on` + cont_check.gm,
  handler literals bound with `::` (pending_handlers, swap_frame, shielded performs,
  FnCx.handled), rest params + spreads (spread.gm: pack_rest_args with synthetic ast,
  spread_parts, generic spreads), comptime string args (ConstKey.Str), expect_eq,
  intrinsics batch (sonnet subagent: atomics/asm/@write/sys/heap/offset_of/variants...),
  monomorphization now REWRITES a tir.clone_expr copy per instance (program.gm
  mono_expr/mono_fn/mono_handler; FnDef.nested) and copies nested fns of generic fns
  (dumped as `<fn>@..literal in g@..[args]`). 420 SAME / 0 DIFF.
- Survey script: $SP/one.sh per root (gammac tir vs gamma tir --diag-format=json), xargs
  -P6 over roots.txt (= program_roots); rewrite if scratch is gone (~60 s).
- Biggest remaining buckets after s38g (roots mentioning): intrinsics 225 (+@size_of
  137), catch-all exprs 222, defer 196, with 182, `yield` 157, iterators 145, typed
  consts 115, computed callees 115 + fn pointers 95 + local values 51 (= call_value with
  Callee.Indirect, dump "call indirect"), handler decls 103, local when 76, tuple
  payloads 70. Single-blocker roots are a long tail (intrinsics 19, with 13, tests 10,
  defer 10): unlocking needs several features.
- Gotcha hit 7x: a bare name at an optional position below the top of a pattern matches
  only present values; bind the whole payload (`.Break(b)`) — lint present-only-binding.
- Perf: gammac re-parses core per root (~100 KB); could cache parsed core per process
  (FileIds/NodeIds identical since core is loaded first).
- Keep: interpreted lane (found a C backend bug in s38c).
