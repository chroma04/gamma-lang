# Code map (current)
src/main.rs        CLI: lex | parse [--dump] | check | run [program args];
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
src/types.rs       hash-consed Ty/TyKind (global RwLock interner), AdtId/EffectId/ParamId names
src/layout.rs      Target, Layouts (size/align/offsets; optional = payload+flag, result = tag+payload)
src/float.rs       exact rational → f16/f32/f64 bits (round half even)
src/tir.rs         typed IR + for_each_child(_mut) traversals
src/sema/          semantic analysis (see architecture/sema-notes.md); cont_check.rs = static
                   one-shot check of ctl continuations (§14.2), run after check_escapes
src/interp/        tree-walking interpreter; run_main / eval_const; fiber.rs = fibers (threads)
src/cgen/          C back end (decisions/008); rtcache.rs = cached runtime objects (014); fiber.rs = with-on outlining, ctl thunks;
                   values >16 B by address (types.rs by_ref/param_cty, func.rs c_abi_wrapper);
                   debug.rs = `-g` (#line mapping, named frame fields, macro guards);
                   views.rs = `-g` debugger views; gdb_printers.py embedded in .debug_gdb_scripts
                   borrow.rs = pure fns / params read in place (decisions/015)
runtime/           gm_rt.h/.c: runtime prefixed to generated C (formatting, heap, handler
                   structs gm_node/gm_unw, fibers + asm context switch); rt_test.c
lib/core/prelude.gm  core declarations in Gamma (embedded via include_str!)
lib/std/           std modules (GAMMA_STD or lib/std); collections/{deque,heap,bitset}
tests/conformance.rs  harness (see decisions/004)
tests/conformance/{lex,parse}/  .gm tests + goldens
docs/              README-level docs (architecture.md, contributing.md, guide/)
