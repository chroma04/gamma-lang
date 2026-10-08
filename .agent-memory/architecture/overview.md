# Code map (current)
src/main.rs        CLI: lex | parse [--dump] | check | run (check/run not implemented yet);
                   --diag-format=human|short. Exit 0 ok, 1 compile errors, 2 usage.
src/lib.rs         module list
src/source.rs      SourceMap, FileId, Span{file,lo,hi} (byte offsets), line_col
src/diag.rs        Diagnostic{severity,message,span,labels,notes}, Diagnostics (suppress/truncate
                   for speculative parsing), render(Human|Short)
src/symbol.rs      Symbol interner (global Mutex, leaked strs; thread-safe)
src/driver.rs      Session{sources,diags,ids}; load/lex/parse; dump_tokens; escape_bytes
src/syntax/token.rs   Token/TokenKind/Keyword/Punct (incl IsNot, NotIn, Ellipsis, Uninit), literals
src/syntax/lexer.rs   lex(file, src, diags) -> LexOutput{tokens, docs}; newline rules §2.8
src/syntax/ast.rs     AST (types are Exprs); NodeId on every node; NodeIdGen shared per session
src/syntax/parser/    parse_file(file, &LexOutput, &mut NodeIdGen, diags) -> ast::File
src/syntax/ast_dump.rs  dump_file -> stable S-expr (golden tests)
tests/conformance.rs  harness (see decisions/004)
tests/conformance/{lex,parse}/  .gm tests + goldens
docs/              README-level docs (architecture.md, contributing.md, guide/)
