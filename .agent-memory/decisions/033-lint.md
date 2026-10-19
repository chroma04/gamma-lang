# 033 — `gamma lint` (s30)

User docs: docs/lint.md. Contributor: docs/contributing.md "Lint rules", architecture.md.

## Shape
- 18 rules in `lint::RULES` (name, default level, summary). Two sources:
  - sema/lint.rs `analyze_for_lint` → `Finding`s, from TIR plus name-resolution sets
    `Sema::decl_uses` (lookup_module_name/module_member hits) and `Sema::local_uses`
    ((decl span, use span) in FnCx::name). TIR alone missed comptime-erased uses
    (`a.len`, `@type_of(x)`), so reads = distinct use spans minus whole-place writes.
  - lint/syntax.rs: re-lex/parse each in-scope file, walk the AST (naming, labels, parens…).
- Names appearing anywhere in a `when` span are never reported (untaken branches unchecked).
- Or-pattern later alternatives' locals are orphans (remapped) → only `bound` locals count.
- Diagnostic gained `code: Option<&str>` (`warning[rule]`, JSON "code").
- Levels: defaults < gamma.toml `[lint]` < CLI (--allow/--warn/--deny, -D) < `#allow("r"|"all")`
  on top-level items (§20.2; sema validates names).
- Scope: own package files only; std only when the input is inside std. Outside a package,
  load_package_modules is NOT run (it loaded every subdir and ICEd).

## Decisions
- prefer-immutable is Allow by default (432 hits on the repo, fixes risky): no fix for fn
  literals, `@`-initializers, type-prefix starts `[ ^ ?`, literal-only calls/tuples.
- --fix: rounds; a round that breaks compilation is undone, then careful mode = one fix per
  round, failing fixes blocked by (path, offset, rule) and counted "refused". Known refusal:
  `r := pure_foreign(i32(-4))` → `::` comptime-evaluates the foreign call (tests/lint.rs).
- identical-branches skips branches containing comments (comments may explain intent).
- Recursion-only use counts as a use (decl_uses has no context). Unused local `::` consts/
  types are not reported. Possible follow-ups: those two, LSP integration (docs say not yet),
  more rules (unused struct fields, unreachable code after return).
- Found + fixed along the way: ICE in opportunistic_const for `z :: zero()` with an inferred
  nested fn (func.rs `unsettled`: bail when a callee signature has inference vars; checking
  expression types broke local_const_comptime). Test run/local_const_of_nested_fn.
