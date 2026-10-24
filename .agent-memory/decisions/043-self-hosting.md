# 043 Self-hosted compiler `gammac` in compiler/ (s38, 2026-10-24)

User asked: "lay the groundwork for a self-hosted compiler; doubly the ultimate dogfooding test".

## Decisions
- Location `compiler/` (package `gammac`, gamma.toml), modules mirror src/: source/, diag/,
  bignum/, syntax/ (token, lexer, literal, dump in one module). main.gm = CLI.
- **Parity contract**: every stage byte-identical to the Rust reference on its stable dump
  (tokens / ast dump / check diags / program output) + diagnostics in JSON format (has all
  fields: spans, labels, notes, fixes; keys sorted alphabetically like serde_json BTreeMap).
  Corpus = every .gm in tests/conformance, lib, examples, bench, compiler (~980 files).
  Reference changes on purpose → change gammac in the same PR.
- tests/selfhost.rs: builds gammac (Debug mode, checks on; as fast as Fast here) via C backend (common::c_backend_available)
  else `gamma run compiler` (17 s); reference computed in-process (driver::Session,
  sources.add(rel path) so paths match); multi-file mode `==> path <==` headers on both
  streams; reports first differing line. Also `gamma test compiler` + formatted check.
  Both plain and `--recover` (lex_recovering) lanes.
- Conventions: arena per compilation (main.gm `lex_file`); OOM traps via `x!`; input errors
  are diagnostics; std only. Messages built with str.format(...)!.
- Exact literal values: own bignum (Nat = List(u32) limbs; Rational reduced by cancelling
  2s/5s only, since denominators are powers of 2 or 10). mul_pow chunks by largest power
  in a limb (10^9) so 1e100000 stays fast.
- Stage plan (docs/self-hosting.md): 1 lexer DONE, 2 parser + ast dump DONE (s38b), 3 modules/packages,
  4 checker (diags of `gamma check`), 5 C back end reusing runtime/, 6 bootstrap fixed point
  (stage2 C == stage3 C).

## Dogfooding findings (s38)
- VM crash: `x!` on `E!()` → exec.rs `extract` with size 0 indexed past regs. Fixed
  (early return), test run/unwrap_unit_result.gm. Nothing tested `r!` on a unit result before.
- Papercuts: `?u8 in (..)` FIXED (s38b, §7.7). Slices already have last()/first(); `x in s`
  is the contains. `while true` + returns needs no trailing return (correct).
- Perf: native gammac lexes 2.4 MB in 0.8 s (~3 MB/s), far slower than Rust; profile later.

## Stage 2 decisions (s38b)
- Modules: ast/ (tree + dump) and parse/ separate from syntax/: methods share their
  module's namespace, so Parser.span etc. would clash with Lexer's; also mirrors src/.
- PResult → `Abort!T` (Abort = empty struct); `fail`/`expected` RETURN the Abort value,
  call sites write `return !p.fail(..)`. nest() → `try p.enter(); defer p.leave()`;
  with_restr → save + `defer p.restr = old` (or restore before `try` when the result is
  needed first). speculate(T, p, fn() -> Abort!T) generic over a second-class closure;
  `members(p, what, body: fn() -> Abort!())` factors the brace-list recovery loops.
- Tuple payloads holding optionals → struct payloads (ElseExpr, SliceExpr, BreakExpr,
  ResumeExpr, IntrinsicExpr, ArrayTypeExpr, WhileStmt, PathPattern, StructWhen/UnionWhen):
  §9.3 makes a bare name at an optional position match only present values.
- diag: push() now dedupes (severity, span, message) and honors suppress/unsuppress, as
  src/diag.rs; NodeIds kept (not dumped) for later side tables.
- Language fix found by dogfooding: `?T in (a, b)` converts members like `is` (§7.7).

## Stage 3 decisions (s38c): modules and packages
- Parity target = new `gamma modules ROOT` (user approved "the dump"): sema::load_modules =
  load_prelude + load_root + BFS over modules resolving ONLY import decls (sorted DeclId) +
  check_import_cycles; dump in load order (module/dir/package/std/import alias "path" dir).
  Load order matters: diagnostics sort by FileId = load order. Paths: `<std>` prefix,
  package roots relative to cwd, `/` separators.
- gammac compiler/load/: loader.gm (mirrors sema/modules.rs + imports.rs), paths.gm (Rust Path
  semantics; SEP '\\' on Windows via top-level `when`), toml.gm (TOML subset; valid docs
  only), when.gm (target fields, @size_of, literals, module constants; else an explicit
  "gammac cannot evaluate" error). canonicalize = normalize(cwd/p) if it exists (no
  symlink resolution; std/fs has no realpath). GAMMA_STD passed by the test.
- Known gaps (in docs/self-hosting.md): git/url deps (need lock + pkg manager), malformed
  manifest messages (reference uses toml_edit + semver), general when conditions.
- Corpus = program_roots() in tests/selfhost.rs (~785 roots: conformance non-lex/parse files,
  .test dirs via main/, examples, bench, every lib/std module dir, compiler).
- New conformance tests: check/top_level_declaration_errors.gm, check/module_load_errors.test
  (git-dep error can't be a conformance test: `gamma check` auto-fetches).

## Dogfooding findings (s38c)
- C BACKEND BUG fixed: assignment evaluated the place AFTER the RHS (place_addr is a lazy C
  expression string) — violates §4.7; interp + Cranelift were right. Fix: cgen func.rs
  assign_addr() spills the address to `uint8_t *tN` when !fixed_address(place) &&
  may_write(value). Test run/assign_place_before_value.gm. Found because interpreted gammac
  differed from native gammac (the interpreted lane is worth keeping!).
- gammac had the matching use-after-move bug (`l.decls.items[id].state = l.import_decl(..)`
  while import_decl grows decls) → handbook "Lists that grow under you".
- Naming a function `string` hides the type module-wide → confusing cascade (cycle error,
  "cannot infer null"). Not fixed (no obvious fix); handbook note.

## Stage 4a decisions (s38d): declarations
- New reference dump `gamma decls` (sema::declarations, decl_dump.rs); user said "move onto
  the next stage" — dump designed like `gamma modules`. Values shown: types with fields/
  variants/tags, generic params, effects with ops, fn sigs with param names/defaults
  (`= …`), `const T`, `untyped int|float n/d|bool|complex`, `module m`, `error`.
- Reference fix: Sema::new creates EffectId::splice first (effect sets sort by id; splice
  id used to depend on process history → in-process test vs CLI differ).
- Skip protocol ("gammac cannot") + floor ratchet instead of all-or-nothing parity: lets
  stage 4 grow across sessions. Rule: gammac never emits a guessed real error.
- Language: `x in (.A(_), .B)` — inline-tuple members may be pattern forms (§7.7), lowered
  to a match (matching.rs in_patterns). Found writing gammac (`kind in (.Fn(_), ...)`).
- Dogfooding: storing `&loop_var` / `&pattern_binding` gives dangling-ish pointers to a
  reused slot (bit gammac twice) → handbook "Pointers to copies"; escape analysis does not
  catch it (candidate for a lint/escape rule later). Temp array slices only as call args.
  `if c { str.format(..)! } else { "" }` fails ([]u8 vs string branches) — annotate.
