# M12 — Debug extras, tooling, polish
Debug build extras (§18.3: poisoning, allocation tags, guard pages), optional
`cc -fstack-usage` verification of @stack_size (decisions/011), diagnostics quality pass, performance, formatter, maybe LSP,
packaging/release.

## Done in M12 scope
- #deprecated (Sema::check_deprecated at decl_expr/callee/module_member/type uses, deduped),
  FnHints{fast_math, inline, cold} on FnDef (copied to generic instances; cgen attributes +
  GM_FAST_MATH_ATTR/_BODY), #inline recursion check (Sema::check_inline after check_all).
