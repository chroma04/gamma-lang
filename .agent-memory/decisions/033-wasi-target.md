# 033 wasm32-wasi target (s30)

Route: the C back end with clang --target=wasm32-wasi + wasi-libc sysroot (wasi-sdk via
WASI_SDK_PATH, or system clang). Runner Wasmtime (toolchain::WASI_RUNNER). target.os `.Wasi`
(probed from `__wasi__`; bare wasm32-unknown-unknown stays Freestanding).

## Runtime (GM_WASI in gm_rt.h/.c)
- No threads: rt lock no-op, GM_TLS empty, spawn → ENOTSUP → IoError.Unsupported (-13; Other
  moved to index 13), futex wait sleeps, cpu_count 1. Net/Proc stubs return ENOTSUP.
- Stack: module stack 64 MiB, --stack-first; bounds __stack_low/__stack_high. Wasmtime needs
  max-wasm-stack 256 MiB so our check fires first.
- Trap: proc_exit rejects >=126, so report then __builtin_trap (wasmtime exits 134, its
  report stripped by toolchain::strip_wasm_trap_report).
- Fn-pointer tag: wasm fn addrs are table indices → tag bit is the top bit (GM_PTR_TAG_BIT,
  GM_PTR_TAGGED), not bit 0.

## Fibers via Binaryen Asyncify
- Global driver gm_wasi_drive at stack bottom; every context (main + fibers) is re-entered
  from it. gm_rt_switch (noinline, topmost runtime fn) starts unwind; each ctx's Asyncify
  buffer lives in its own stack [low, sp). Buffer reaching frames → stack overflow trap.
- Entry fns called via volatile fn pointers so LLVM cannot inline into the driver.
- Link WITHOUT -O (clang would run wasm-opt -O2 first; inlining broke switching), then
  `wasm-opt --asyncify --pass-arg=asyncify-imports@gamma.none <opt>`. Only modules importing
  module "asyncify" are transformed; cgen emits `#define GM_RUN_MAIN gm_run_main_fibers` only
  when the C contains `gm_with_go(` so fiber-free programs reference no asyncify import.
- Cost: unwind+rewind ∝ depth of both stacks. Stress (s30): scheduler under 2000 frames:
  16.6 s wasm vs 0.06 s native; shallow 0.43 s vs 0.05 s. Kept (documented in guide ch. 1).
  Future idea: a local/asymmetric driver at the `with` site so the outer stack is not
  unwound; complex because fibers of nested `with`s switch non-nestedly. Alternative later:
  the wasm stack-switching proposal once Wasmtime ships it by default.

## Tests
tests/wasi.rs (skips without clang-wasi/wasm-opt/wasmtime); conformance directives
`os: wasi`, `ptr_bits: 64`, `needs: threads|sockets`; CI job `wasi`.
