# Code map (current, 2026-10-15). Contributor-facing description: docs/architecture.md.
src/main.rs        CLI: lex|parse|check|run|build|test|bench|fmt|doc|new|init|fetch|update|add|
                   remove|cache|repl|lsp|help|version. Exit 0 ok, 1 compile errors, 2 usage.
                   new_session() picks toolchain/target/mode; test_native for C tests.
src/lib.rs         module list
src/source.rs      SourceMap, FileId, Span{file,lo,hi}; source::canonicalize (never
                   Path::canonicalize: Windows \\?\ paths)
src/diag.rs        Diagnostic{severity,message,span,labels,notes,fixes}, render(Human|Short|Json), to_json;
src/fix.rs         gamma fix: plan(diags, editable) → non-overlapping edits per file; apply;
                   LONG_LINE/WINDOW show long source lines as a window
src/symbol.rs      Symbol interner (global Mutex, leaked strs)
src/driver.rs      Session{sources,diags,ids,mode,debug_info,target,packages,overlays}; load/lex/parse
src/syntax/        token.rs; lexer/{mod,literal,tests}.rs (newline rules §2.8); ast.rs (types
                   are Exprs, NodeId per node); parser/{mod,expr,items,lits,pattern,stmt,types}.rs
                   (MAX_POSTFIX_CHAIN in mod.rs); ast_dump.rs (S-expr goldens)
src/types.rs       hash-consed Ty/TyKind (lock-free KINDS table), AdtId/EffectId/ParamId;
                   mark/release_since for the LSP
src/toolchain.rs   Toolchain (CC/CFLAGS/LDFLAGS, --target, Safe/filcc), target probe (decisions/015)
src/layout.rs      Target, Layouts (size/align/offsets)
src/float.rs       exact rational → f16/f32/f64 bits
src/tir.rs         typed IR + traversals; frame_cost/STACK_RESERVE shared by interp + stack_size
src/sema/          checking (architecture/sema-notes.md). One file per area, each with a //!
                   header: decls, modules, imports, ty_expr (names_type), func (FnCx), expr,
                   call, methods, construct, pattern/matching/exhaust, seq, effects,
                   handler_decl, fnlit, generics, infer/finalize/untyped, consteval,
                   stack_size, cont_check (§14.2), escape, foreign, tests_decl, adt, builtins;
                   ide/ builds the editor index after checking (decisions/023)
src/interp/        interpreter: vm/ (bytecode: mod, compile, compile_flow, exec, heap) is the
                   only engine (tree-walker removed s29, decisions/020); calls/ops/builtins/
                   fast(small ints)/sys/proc hold the value-level helpers; memory.rs byte memory +
                   provenance; fiber.rs (thread per fiber); threads.rs (GIL); ffi.rs (libffi,
                   decisions/009); sys.rs (SysOps); coverage.rs; consts.rs (constants →
                   ConstValue; eval_const in mod.rs); vm/heap.rs fast new/destroy
src/cgen/          C back end (decisions/008): mod, func, expr, arith, wide, elementwise,
                   types (typed C structs, blobs), helpers, fiber (with-on outlining, ctl
                   thunks), borrow (pure-fn params read in place, 016), constarg (static
                   gm_kN args), rtcache (cached runtime object + cached programs, 014),
                   units (Debug split into parallel C units: Generated.parts, s25),
                   outline (long statement runs → gm_cN_K helpers sharing F, decisions/031),
                   debug + views + gdb_printers.py + lldb_formatters.py (`-g`, 013; build writes gamma_lldb.py), csrc (C sources from gamma.toml)
src/clif/          Cranelift back end (decisions/033): mod (Gen, reps, sigs, exports, link), func
                   (frames, ladder, thunks), expr, ops (operators, builtins, @asm), cabi (C ABI
                   classes), data; runtime/gm_clif.c glue linked with gm_rt.c.
src/pkg/           package manager: manifest, lock, git, archive, resolve, edit (add/remove),
                   build ([build]/[profile.*]), cache (clean), scaffold (new/init), cli
src/lsp/           `gamma lsp`: transport, position, workspace, analysis, features, refs,
                   corelib, server; src/ide.rs = index model
src/doc/           `gamma doc`: collect, markdown, html, md_html
src/repl/          `gamma repl`: mod, input (classify), report
src/fmt.rs         `gamma fmt`;  src/testing.rs `gamma test`/`bench`;  src/coverage.rs
                   `--coverage`;  src/ice.rs internal-compiler-error reports
runtime/           gm_rt.h/.c: runtime prefixed to generated C (formatting, heap, handlers
                   gm_node/gm_unw, fibers asm/ucontext, threads, Win32 + POSIX sys); rt_test.c
lib/core/          prelude.gm, list.gm, map.gm, slice.gm (embedded via include_str!)
lib/std/           std modules (GAMMA_STD, <prefix>/lib/gamma/std, or lib/std)
editors/           vscode extension + TextMate grammar; tree-sitter-gamma (CI job "editors")
tools/             package-release.sh, install.sh/.ps1, gen_float_table.py, gen_text_tables.py
tests/             conformance.rs (format in its header) + conformance/{lex,parse,check,run,
                   test,safe}; per-tool suites (pkg, lsp, repl, doc, fmt, cross, native_build, …)
docs/              architecture.md, contributing.md, versioning.md, lsp.md, editors.md,
                   guide/ (16 chapters), std.md + std/ (generated by `gamma doc`)
