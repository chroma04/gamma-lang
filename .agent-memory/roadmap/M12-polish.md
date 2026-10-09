# M12 — Debug extras, tooling, polish
Debug build extras (§18.3: poisoning, allocation tags, guard pages), optional
`cc -fstack-usage` verification of @stack_size (decisions/011), diagnostics quality pass, performance, formatter, maybe LSP,
packaging/release.

## Done in M12 scope
- #deprecated (Sema::check_deprecated at decl_expr/callee/module_member/type uses, deduped),
  FnHints{fast_math, inline, cold} on FnDef (copied to generic instances; cgen attributes +
  GM_FAST_MATH_ATTR/_BODY), #inline recursion check (Sema::check_inline after check_all).
- §18.3 done: dead frames 0xDD (C debug memset at exit; interp kills frame allocs), allocation
  tags (prelude new/make*/free/destroy take `site = @caller_location()`, bound only by name —
  ParamInfo::is_site; Builtin::AllocTag/AllocCheck; interp Machine.alloc_tags, C gm_atags table),
  stack probes count as guard-page equivalent.
- Native `gamma test` (main.rs test_native, cgen generate_tests; harness runs test stage on C too).
