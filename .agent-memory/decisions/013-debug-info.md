# 013 Debug information for native builds (`-g`) (session 5)
Context: LLVM evaluation (Oct 9) kept the C back end; its strongest LLVM argument was debug
info. `-g` covers line mapping and variable inspection without LLVM.
- CLI `-g` → Session.debug_info → cgen::Options.debug_info; Generated carries its Options so
  `compile(&Generated, exe)` adds `-g` and picks `-O0` (Debug mode) / `-O2` (Checked/Fast).
- cgen/debug.rs: FnGen::src_line(span) emits `#line N "<canonical .gm path>"` before each
  block stmt / tail and at fn start (files not on disk, e.g. `<core>/prelude.gm`, unmapped;
  Gen::line_directive caches paths). FnGen::end_lines() appends LINE_RESET at the end of
  each fn text (gen_body, detached). compile() runs resolve_lines: repeats the active gm
  directive before EVERY C line (a directive numbers following lines consecutively — first
  attempt without this made gdb see t.gm:26 in an 18-line file), LINE_RESET → `#line k "x.c"`.
- Names: frame field = `union { T lN; Mirror name; }` (mirror type if any → gdb prints
  {x = 3, y = 4}); mirrors (Types.debug_names) name fields after Gamma fields. Names go
  through debug::member_name (ASCII ident, not keyword/reserved/gm_/lN/wN/fN…) and
  unique_members (duplicates/shadowed → no alias). Declarations that hold them (mirror_defs,
  frames) are wrapped in `#pragma push_macro/#undef/pop_macro` for each name (glibc macros:
  linux, stdout, st_mtime, sa_handler, h_addr …). Generated code never uses alias names.
- Without -g the output is byte-identical to before. Whole suite passes with -g forced
  (gcc+clang), and conformance is ~40% faster at -O0 (compile time dominates the suite).
- Tests: tests/debug_info.rs (C text checks; gdb breakpoint + `print F.p` when gdb works).
- Not done: typed views of tuples/tagged unions/slices in gdb (still byte blobs); params
  show as a0..; `next` hits a line several times (several is_stmt rows per statement).
