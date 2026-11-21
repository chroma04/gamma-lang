# Decisions index (one topic per file; numbers are stable, gaps are deleted records)
Each records what was decided and why, with measurements. Decisions before 043 were made
on the retired Rust implementation and name src/*.rs files; gammac mirrors those modules.
Status words: CURRENT (holds today), PARTLY (read with the later one), HISTORY (context).

## Language design (spec rationale)
- 005 parser ambiguities (spec A.7) · 006 generics · 007 value params + sema-time eval
- 018 stdlib admission policy §22 + s7 language changes · 021 s9 papercuts
- 022 effect-set params · 027 effect-set params inferred from context; fiber channel
- 024 locals may not be named like types · 025 design review (thesis, P1–P5)
- 026 `needs A + B` effect syntax · 029 wishes admitted/rejected · 030 coherence review
- 037 errors policy (ch. 23), array spread · 038 HashMap to std, no relational constraints
- 039 complex + `#vec` math vectors · 040 `#soa` · 042 generic `#soa []T` + SoaList
- 041 stdlib batch 1 · 044 s39 pain-point changes · 045 dangling addresses are errors;
  no shadowing stays · 047 `?x` pattern · 049 temp arrays as slices; flush before blocking
- 054 pain-point sweep · 060 stale-element-pointer through struct pointers
- 071 struct-typed errors: no lifting, no variant shorthand (REJECTED)
- 061 spreads in tuple expressions

## Compiler architecture
- 002 pipeline (why C, not LLVM) · 043 self-hosting plan (HISTORY) · 046 compile-time
  evaluator (structured values) · 052 retiring Rust, C seed bootstrap · 066 fast bootstrap
  (seed = the build's units)
- C back end: 008 design · 009 FFI/#global/callconv · 010 fibers · 011 @stack_size ·
  012 handler specialization + local exits · 013 debug info (`-g`) · 014 cached runtime
  object · 015 toolchain from the environment, cross targets · 016 exits/big values ·
  017 Safe mode via Fil-C · 031 statement-run outlining · 034 wasm32-wasi ·
  048 gammac C back end · 050 coverage · 065 incremental C units (stable names, cache)
- Interpreter: 051 gammac interpreter (PARTLY: memory model, fibers, threads still hold) ·
  056 lowered code + register machine · 064 Bc forms, in-loop calls, page memory
  (CURRENT; "Not done" list) · 067 interp perf round ("Open candidates")

## Tooling
- 023 packages, LSP, editors, release · 028 fix-its · 033 lint · 036 website + playground
  · 053 Gamma test harness, SpawnOptions env + process groups · 059 playground root stack
  · 070 s74: tests in the interpreter, LSP supervisor, package C object cache; one
    revision per repository stays

## Performance rounds (measured; each lists what was tried and dropped)
- 032 Safe new/destroy (Fil-C blob typing lesson) · 055 compiler perf baseline ·
  057 round 4 · 058 round 5 (C size) · 062 round 6 · 063 round 7 · 068 round 8
  (compiler −41%; measuring recipe) · 069 round 9 (copy elision) · 072 round 10
  (every remaining roadmap item measured: kept / dropped / closed; scripts) · 073 shared
  bodies for `@write` template parameters (compiler C −13%)
