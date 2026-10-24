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
- Survey script: $SP/one.sh per root (gammac tir vs gamma tir --diag-format=json), xargs
  -P6 over roots.txt (= program_roots); rewrite if scratch is gone (~60 s).
- Biggest remaining buckets (roots mentioning): generic calls 301 + generic bodies 261 +
  methods of generic types 127 + core slice methods 134 (all = generics: instantiation,
  labels `f@..[args]`, instance dumps excluded); catch-all exprs 194 (break down:
  intrinsics, fn literals...), intrinsics 179, defer 178, with 172, `calls of X` 156
  (builtins like list/new/free/enumerate), iterators 136, handler decls 102, consts 88.
- Gotcha hit 6x: tuple payload with ?T element + bare binding in match unwraps; use struct
  payloads for variants with optional parts (handbook rule).
- Perf: gammac re-parses core per root (~100 KB); could cache parsed core per process
  (FileIds/NodeIds identical since core is loaded first).
- Keep: interpreted lane (found a C backend bug in s38c).
