# Refactoring backlog (s64 audit, 2026-11-10)
Eight area audits (sonnet workers; they ran out of quota before implementing most of it).
Done in s64: needless `pub` stripped (lint 455 → 24 warnings), dead code, small dedupes
(tir.binop_symbol, bigint.bits128, ratio.zero/to_float, types.write_debug_string,
sema.generic_of/frame_up/NO_FN, slice.dedup in ide/lsp).
s65 (branch claude/amazing-clarke-5jp6b8, PR #119) DONE: #1 layouts (sema.Layout has
offsets; cgen.Layouts delegates; sema/frame.gm = frame_cost/MAX_FRAME/root_local; sema
bits_layout/field_bits), #2 CLI table (driver/flags.gm OPTIONS, scan, options_help;
main.gm split; repl uses scan), #3 partly (check/seq.gm → seq/index/array_ctor;
errors.gm → optionals.gm), #5 partly (interp/vm_compile.gm split out of vm.gm),
#6 bignum removed (literals hold bigint.Int / ratio.Rat; ratio.scaled), #7 resolve
KeyMap(V), #8 partly (cgen ExitKind; tir.sys_name shared by cgen + intrinsics; dump
tables → @variant_name). Medium items 1–7 of the helper list done (source.decode/
char_count/is_ident_*, diag.json_string, load.os_error_text, load path helpers +
skipped_dir, LINT_RULES in load/manifest, semver cmp_identifiers). Items #4 (walkers)
and #9 (harness) delegated in s65 — check the PR/branch before redoing.
Re-verify line numbers before acting; audits did NOT grep web/ (web/gen, web/play depend
on gammac: `Options.native_fallback` looked dead and is not).

## Large: what is left (s65)
3. check/: expr.gm still mixes jumps (return/break/continue ~1344–1575) and construction
   (~1576–1947) → jumps.gm / construct.gm; func.gm; iterators.gm perform_* → effects
   file. Long fns: generic_call_packed 357 lines, with_expr 197, bind_args 163,
   write_expr 263.
5. interp: two integer-arithmetic implementations (byte/128-bit ops.gm+builtins.gm vs
   word run.gm); interp.gm/run.gm mix.
8. cgen: Scope fields per kind; FnGen save/restore ×4; file splits (calls.gm →
   patterns/bits/write, arith.gm builtins); helpers.gm eq/less/hash scaffolding.
(4 walkers, 9 harness: delegated in s65, see above.)

## Medium: what is left
- check: 36 hand-built Block exprs → block_of; prelude lookups ×10 (Sema.prelude_effect);
  Frame save/restore field lists ×3 (func.gm:653); handler clause checking dup;
  index-loop builders ×4; coerce/finalize peel dup.
- sema: ScopeMark variants ×5; FnSig/FnDef/AdtInfo construction ×3–4; adt member walkers ×6;
  ambient effect name lists ×4; FnTy.variadic never true.
- parser: members() reuse for 4 brace loops; `p.restr` swap/restore ×19; StructWhen==UnionWhen.
- lsp/server.gm JSON-RPC envelope ×4, long `notification`; ide narrowest-range search ×4.
- CI: version awk ×3, hello program ×3, composite action for "Build gamma from the seed".
- `gamma lint` is CLEAN on compiler, tests/harness, tools/difftest and the `lint` suite
  keeps it so. Pubs needed only by harness/fuzz carry `#allow("needless-pub")`.

## Possible bugs: status (s64b, PR after #117)
Fixed: int literal → float exactness (ratio.from_f64; also sema/consts.gm untyped_fits
shortcut for plain constants), archive entries message, interp invalid-tag signedness /
unwrap release / env_args free, harness (kit.json_string, GAMMA_BLESS dropped,
build_profiles honours cfg.c, selfhost uses driver.base_cflags, fmt check covers
tools/difftest + fuzz), sema value_at.
Not bugs: eval Pow exponent (always u32), f16 vector lanes (sema rejects them).
Left: pkg/lsp.gm bad Content-Length → 0 (test would time out, not pass);
driver/toolchain.gm for_safe_build splits PATH on ":" (Safe is Unix-only);
native.gm cached_program calls Env.exit inside a ?string fn; target_text trailing "" field
(changing it invalidates cache keys); ci.yml:346 `gammac` fallback; a non-exact constant
EXPRESSION typed float (`x: f64 : (1 << 60) + 1`) reports "`<<` requires integer operands"
rather than the exactness error (operands take the float type; pre-existing).
