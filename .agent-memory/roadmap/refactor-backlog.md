# Refactoring backlog (s64 audit, 2026-11-10)
Eight area audits (sonnet workers; they ran out of quota before implementing most of it).
Done in s64: needless `pub` stripped (lint 455 → 24 warnings), dead code, small dedupes
(tir.binop_symbol, bigint.bits128, ratio.zero/to_float, types.write_debug_string,
sema.generic_of/frame_up/NO_FN, slice.dedup in ide/lsp). Everything below is OPEN.
Re-verify line numbers before acting; audits did NOT grep web/ (web/gen, web/play depend
on gammac: `Options.native_fallback` looked dead and is not).

## Large (priority order)
1. Two layout engines: sema/layout.gm vs cgen/layout.gm (same algorithm, cgen adds
   offsets; interp uses cgen.Layouts; eval/ops.gm `offsets` + sema `field_offset` derive
   offsets a 3rd time). Merge into sema (Layout gets offsets); mind layout_gen invalidation.
   Unlocks: one frame_cost (interp/frame.gm, cgen/func.gm:353, check/stack_size.gm:306 are
   triplicates incl. MAX_FRAME, round8, root_local), bit_width/bits_field/const_bits dups.
2. CLI option table: main.gm parse loop + USAGE + repl parse_options list every option 3×
   (cranelift leftovers were the symptom). compiler_main is ~200 lines.
3. check/ structure: split seq.gm (in/for/index/array ctor), expr.gm (jumps, construct),
   func.gm, iterators.gm perform_*; errors.gm is really optionals. generic_call_packed 357
   lines, with_expr 197, bind_args 163, write_expr 263.
4. Two AST walkers (lint/syntax.gm, ide/walk.gm) → ast.for_each_child; tir.for_each_subpat
   (4 check copies + cgen/borrow.gm + lint/checked.gm); tir.any_child (7 cgen sites).
5. interp: two integer-arithmetic implementations (byte/128-bit ops.gm+builtins.gm vs word
   run.gm); vm.gm = compiler (vc_*) + runtime in one 2700-line file; interp.gm/run.gm mix.
6. bigint.Int vs bignum.Nat duplicate limb arithmetic; bignum.Rational vs ratio.Rat.
7. pkg/resolve.gm: resolver state as 5 parallel list structs with linear find/put.
8. cgen: string-typed exit kinds (func.gm start_exit "GM_RETURN"…); Scope fields per kind;
   FnGen save/restore ×4; file splits (calls.gm → patterns/bits/write, arith.gm builtins);
   helpers.gm eq/less/hash scaffolding; SysOp name tables ×3.
9. Harness: "run child with timeout" ×3 (kit/command.gm, tools/difftest/runner byte-identical,
   lsp clients) → std/process API; World scaffolding dup (build_profiles vs pkg/world.gm);
   pkg/lsp.gm second LSP client; lanes.gm second directive parser; golden/bless 3 ways.

## Medium
- UTF-8 helpers (lexer.gm decode == fmt/chars.gm; char_count/char_offset dups) → source.
- Ident/space char predicates ×5 (lexer, cgen/freestanding, cgen/units, lsp/features, repl).
- JSON string escaper: diag.gm json_string == lsp/json.gm write_string; lsp/json vs std/json.
- IO-error text tables ×4 (pkg/support, loader describe_read_error, load/os_error, cgen/wasi).
- Package-root / canonical path helpers ×4 (loader, pkg/sync find_root, lint, lsp/workspace);
  module_files/overlay copies in lsp/workspace.gm and doc.gm; skip-dir lists disagree.
- LINT_RULES in load/manifest.gm copies lint.RULES; [lint] validated twice.
- check: dump.gm tables → @variant_name; 36 hand-built Block exprs → block_of; prelude
  lookups ×10 (Sema.prelude_effect); Frame save/restore field lists ×3 (func.gm:653);
  handler clause checking dup; index-loop builders ×4; coerce/finalize peel dup.
- sema: ScopeMark variants ×5; FnSig/FnDef/AdtInfo construction ×3–4; adt member walkers ×6;
  ambient effect name lists ×4; FnTy.variadic never true.
- parser: members() reuse for 4 brace loops; `p.restr` swap/restore ×19; StructWhen==UnionWhen.
- semver cmp_pre_parts == cmp_build; manifest.gm ascii helpers/KEYWORDS copy.
- lsp/server.gm JSON-RPC envelope ×4, long `notification`; ide narrowest-range search ×4.
- CI: version awk ×3, hello program ×3, composite action for "Build gamma from the seed".
- Remaining `gamma lint compiler` warnings: receiver-style unused params, 8 pubs the
  harness/fuzz need, and 4 probable false positives (present-only-binding cgen/func.gm:319,
  stale-element-pointer + dangling-address check/program.gm, mutated-copy repl/report.gm).

## Possible bugs found (not fixed; tell user / fix with tests)
- check/finalize.gm exact_float ignores `x`: `y: f32 = 16777217` accepted, `1 << 60` as
  f64 rejected (spec: integer literal "must convert exactly").
- pkg/archive.gm:108 message says "more than 4 GiB or N entries"; only entries are checked.
- interp/format.gm fmt_adt invalid-tag prints the tag as i128 regardless of signedness;
  run.gm unwrap Fast early returns skip m.release; calls.gm env_args never freed.
- harness: pkg/cache.gm uses kit.quote for JSON; GAMMA_BLESS=0 blesses (doc.gm);
  build_profiles skips on `cc` not cfg.c; selfhost.gm C flags drifted from BASE_CFLAGS;
  tools/difftest and fuzz/*.gm are outside the fmt check.
- cgen/vector.gm f16 lanes typed double (if f16 vectors reach the back end).
- sema/generics.gm value_at ignores k on an empty path; eval Pow exponent trunc(u32).
