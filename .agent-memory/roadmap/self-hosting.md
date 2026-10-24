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

## Next: 4a leftovers (foreign, #global, vector/#soa, attrs), then 4b bodies
- 4b needs: expression checker + TIR + const evaluator; target = diagnostics of
  `gamma check` (maybe a typed dump). Big; split by expression kinds with skip protocol.
- Perf: gammac re-parses core per root (~100 KB); could cache parsed core per process
  (FileIds/NodeIds identical since core is loaded first).
- Keep: interpreted lane (found a C backend bug in s38c).
