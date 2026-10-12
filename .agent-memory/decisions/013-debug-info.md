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
- Views (s5, thread "Better debugger views"): cgen/views.rs. Frame alias = Types::debug_view
  (not the ABI mirror). Packed structs, members at Gamma offsets, unnamed bit-fields as
  padding (gdb hides them), alignment 1 → frame layout unchanged; _Static_assert sizeof.
  Union tag = packed C enum `Type_Variant` (dedup `Type2_…` for generic instances; extra
  `__size` enumerator forces a wider tag's size; tags > 4 bytes stay ints). Pointer targets
  are reserved + defined after (pending), or immediately when later needed by value
  (example2_editor broke without that). View struct tags carry a kind suffix (_slice,
  _string, _optional, _result, _union) that gdb_printers.py matches; script embedded as
  inline .debug_gdb_scripts (ELF only) — gdb loads it only under auto-load safe-path.
  Rejected: enumerators named just `Variant` (collide: Ok/Err across unions); C bitfield
  enum tags (break layout). lldb: views only, no printers.
- Verify -g changes with GAMMA_TEST_DEBUG_INFO=1 cargo test --test conformance (gcc+clang).
- Not done: params show as a0..; `next` hits a line several times; #bits structs show as
  their integer; lldb summary providers.
