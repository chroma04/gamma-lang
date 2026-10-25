# 048 gammac stage 5: a C back end (s43, 2026-10-25)

User asked: "Try and get Gamma to compile itself." → stage 5 (code generation) + stage 6
(bootstrap) of decisions/043.

## Decisions
- compiler/cgen/ = port of src/cgen/ (same names, same C text) WITHOUT the optimizations:
  no specialization (Spec), no borrowed params, no outlining, no chain shortcuts, no
  constarg, no -g/views, no Safe/Fil-C, no freestanding, no units/rtcache. They change
  speed, not behavior. Parity contract for this stage = behavior (program output + exit
  status), not C text: FnIds/AdtIds differ between gammac and Rust anyway.
- Files: gen.gm (mod.rs), types.gm (CTypes = Rust Types), layout.gm (Layouts w/ offsets),
  program.gm (Prog/Func view over Sema; Rust Program), consts.gm (tir.Value cells →
  bytes + relocs into gm_cN storage, lazily registered; anonymous literal cells typed from
  the pointer), fngen.gm (FnGen/Scope-as-struct/Observed), func/expr/calls/arith/vector/
  helpers/fiber.gm (ported by sonnet subagents from the matching .rs).
- Gamma module = one namespace: Rust Gen::loc → `src_loc`; arith's free `cty` renamed.
- check/codegen.gm: `entry_point` (the `<entry>` wrapper with heap_allocator/system_io
  handler decls, as sema/mod.rs) and `prepare_codegen` (worklist from the entry: ensure
  every reachable body is checked — core bodies included — and produce generic instances
  that check_program's monomorphize dropped because core templates had no body yet).
- Foreign fns: Sema.foreign_fns records lib + link name (sema/foreign.gm). #global not
  tracked yet (unsupported in cgen).
- CLI: `gammac build [--mode=debug|checked|fast] [-o EXE] [--emit-c] ROOT`: writes
  EXE.c = runtime/gm_rt.h + gm_rt.c + generated (one unit), runs $CC with the BASE_CFLAGS
  + -pthread + -O1 (debug) / -O2. Runtime dir: $GAMMA_RUNTIME or <std>/../../runtime.
- Additions found while integrating: `#export` (Sema.export_fns parallel to exports;
  gen_exports in gen.gm), `#global`/foreign vars (Sema.globals: GlobalInfo by decl; TIR
  `.Global(name, decl)`; values evaluated in prepare_codegen via eval_typed; storage in
  consts.gm gen_consts incl. thread-local accessors), `#bits` const values (consts.gm
  bits_of), `target.mode` from --mode (Loader.mode, load/when.gm), check_bodies KEEPS
  core pending bodies (re-pushed) so ensure_body finds them, FnCx.eq_allowed (`is` on
  params in core, as Rust).
- Result (s43): 475/475 applicable conformance run tests pass via gammac build; bootstrap
  fixed point (stage2 C == stage3 C, 36.7 MB; clang-built stage2 gives the same C).
  Timing: C generation ~5 s, cc -O1 of the single unit ~95 s (gcc) / ~125 s (clang).
- Tests: tests/selfhost.rs `programs_compiled_by_gammac_behave_as_expected` (all applicable
  run tests, parallel, unix only) and `gammac_compiles_itself_to_a_fixed_point` (unix).
