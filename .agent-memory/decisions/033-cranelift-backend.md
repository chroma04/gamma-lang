# 033 Cranelift native back end (s30, thread "native back end", branch claude/project-thread-3wi9el)

User asked (via coordinator) for a native back end via Cranelift or LLVM for v0.1.0, behind a
flag, passing as much conformance as feasible. Chose Cranelift (pure Rust, no system LLVM).
Result: all 708 conformance tests pass on x86-64 and emulated AArch64 Linux, none skipped.
User docs: docs/guide/native-backend.md; design: docs/architecture.md "The Cranelift back end".

## Shape
- `--backend=cranelift` for run/build/test/bench. Module src/clif/ (mod, func, expr, ops,
  cabi, data). TIR → CLIF directly (not from VM bytecode: tied to the interp memory model).
- Reuses the C runtime: gm_rt.c (GM_RT_SEPARATE) + runtime/gm_clif.c glue (main/entries,
  descriptor-driven eq/less/hash, trap helpers, 128-bit/f16 arithmetic, write helpers) as ONE
  cached object (cgen::runtime_object_from, prefix gm_clif_rt). Still needs the C compiler for
  that object, for @asm units and as linker driver (adds -lm, -pie on Linux).
- Deps cranelift 0.135 (MSRV 1.95 = its rust-version; 0.136 needs 1.96). Features x86+arm64.
  Targets: 64-bit Linux/macOS x86-64/AArch64 only (check_target); Safe/Windows → C back end.
- No `-g` (error points to --backend=c). Coverage stays interpreter-only.

## Conventions
- Own ABI between Gamma fns: non-plain ([sret], env, cx, inst, params…); plain ([sret],
  params…). sret is a plain I64 (not StructReturn). Mem params by pointer, callee copies.
  Plain fn pointers tagged bit 0 → ALL functions aligned ≥16 (Gen::define compiles itself,
  define_function_bytes; default alignment broke tagging → bogus "stack overflow" traps).
- C ABI only at foreign calls / #export (cabi.rs): SysV eightbyte classes (MEMORY → StructArgument
  stack copy, or if regs run out), AAPCS64 HFA / 1-2 GPRs / >16 bytes Indirect, pad dummy
  params to skip registers (even-GPR for 16-aligned, all-to-stack), Win64 1/2/4/8 → int else
  by reference. Export with non-scalar C sig → Work::ExportThunk. Same symbol imported with a
  different sig (export + foreign decl) → call_c uses call_indirect.
- Variadic floats on x86-64: 10-byte raw trampoline (mov eax,8; jmp callee) per callee
  (Gen::va_trampoline). Apple AArch64 variadics: pad x0-7/v0-7 with dummies, widen to 8 bytes
  (untested locally: only CI macOS covers it).
- Fn pointer with by-value struct params passed to C → rejected (check_c_args); C calling a
  Gamma callback would disagree on the ABI.
- @asm: one C function per site (`void gm_clif_asmN(void **a)`, operands *(T*)a[i]) compiled
  -O2 with base_cflags and linked; not inlined (documented).
- enable_llvm_abi_extensions=true (i128 args). Cranelift egraph makes umin/umax.i128 which x64
  can't lower → integer selects go through FnGen::pick (branch for I128).
- 128-bit glue in gm_clif.c is pointer-based (avoids i128 ABI questions).
- Exported names: rejected if a runtime identifier (cgen::c_identifiers of runtime_source),
  `gm_` + trailing digit, or `main`.

## Testing
- Conformance Cranelift lane: strict (unsupported = failure); `//! cranelift: skip <reason>`;
  GAMMA_TEST_CRANELIFT=0 off; runs when target supported and arch==host or GAMMA_RUNNER set.
- tests/cranelift.rs: CLI, traps, C ABI cross-check against a C source package, @asm.
- CI job `aarch64` (qemu-user, -static) runs conformance + cranelift tests.
- Local aarch64: apt-get install qemu-user gcc-aarch64-linux-gnu; env as in docs/contributing.md.
- GAMMA_DUMP_CLIF=1 dumps IR.

## Performance (s30, release gamma, x86-64)
- Build: 4–13× faster than C back end (json_query debug 1.3s → 0.23s; fast 2.8s → 0.2s).
- Run: 1–6× slower (mandelbrot ≈ C; safe_call 6×: no inlining; list/map 2–2.7×).
- Ideas if perf matters: inline small plain fns at TIR level, keep more locals in SSA,
  avoid memory round trips for small aggregates (32-byte struct add bench 37× slower).
