# Self-hosting roadmap (decisions/043; public doc docs/self-hosting.md)
State: stage 1 (lexer) done in s38, parity on whole repo incl. --recover.

## Next: stage 2 parser
- Port src/syntax/ast.rs (821 lines) → compiler/syntax/ast.gm; parser/{mod,expr,stmt,items,
  types,pattern,lits}.rs (~3.3k lines) → parser*.gm; ast_dump.rs (750) → ast_dump.gm.
- CLI `gammac parse [--dump]`; selfhost.rs lane comparing `gamma parse --dump` output
  (driver::dump_ast) + JSON diags over the corpus. Parser needs NodeIds like Rust (dump
  shows them? check ast_dump format first).
- Good delegation target (well specified by the Rust code + goldens): split AST+dump / expr
  / stmt+items / types+patterns across agents only after AST types are fixed by me.
## Later
- stage 3 modules/packages (needs gamma.toml parser: TOML subset), stage 4 checker (biggest;
  TIR), stage 5 C gen (reuse runtime/gm_rt.c), stage 6 bootstrap fixed point.
- Fix papercuts listed in decisions/043 when they bite again; profile gammac lexer.
