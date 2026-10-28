# Code map (2026-10-28). The full, maintained map is docs/architecture.md (tested: it must
# name every compiler/ module). This file holds only agent-facing pointers.

- compiler/ = `gamma` (Gamma, package gammac, built Checked via compiler/gamma.toml).
  main.gm dispatch; syntax/ parse/ ast/ load/ (modules, core.gm loads lib/core from disk,
  Module.index hash of scope) sema/ (decls, layout cache with generations) check/ (bodies →
  TIR; FnCx in func.gm; expr.gm `expr` wrapper → `expr_kind`) tir/ eval/ (comptime,
  structured values) interp/ (tree-walker over bytes) cgen/ (C; units.gm splits) driver/
  (native.gm compile_c: unit policy, runtime object cache) pkg/ testing/ fmt/ doc/ lint/
  linting/ fix/ repl/ ide/ lsp/.
- lib/core (prelude, list, map, slice, complex) may use core-only intrinsics (@heap_*,
  @alloc_tag/@alloc_check, @copy_elems). A new one used by lib/core needs the two-stage
  build + seed refresh (docs/contributing.md "seed").
- bootstrap/ seed (Checked C, 8 units) + build.sh/verify.sh; tools/test.sh → tests/harness.
- runtime/gm_rt.{h,c}: C runtime; Debug extras (poison, 0xAA, backtraces) are spec §18.3.
- The Rust implementation is gone (tag rust-reference-final); older decisions mention src/.
