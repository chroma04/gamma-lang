# Bug sweeps: what works, what is noise (s86)

Yield so far: hand-written probes of core semantics found nothing (difftest covers them);
bugs were in less-trodden corners: compile-time evaluation limits, local `::` constants,
IDE output, tool CLIs, diagnostics. Look there first.

## Recipes
- Both back ends on a file: `gamma run --backend=interp f.gm` vs `gamma run f.gm`.
  Compare with `printf '%s\n'`, NOT `echo`: dash's echo turns `\n` in captured output
  into newlines, which looks like a quoting bug in `{:q}` (it is not).
- difftest: `gamma build tools/difftest -o X; X --start N --count 600 --work DIR`
  (~25 min on 4 cores); `--mode=checked` too. Use seed windows far from CI's
  (CI: 0..100 and 1000 + (run % 500) * 200).
- Sanitizers (no CI lane): `CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
  LDFLAGS="-fsanitize=address,undefined" gamma build f.gm -o x`, run with
  `ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0`. Expected noise: stack
  overflow tests, huge-allocation tests (set allocator_may_return_null=1), heap blocks
  reused as fiber stacks (example2_heap_fibers). File-system tests need the harness's
  temp cwd. s86: whole tests/conformance/run clean otherwise (~25 min, -P 3).
- libFuzzer: needs `apt-get update && apt-get install -y libclang-rt-18-dev` here; then
  `GAMMA=target/bootstrap/gamma fuzz/smoke.sh 180 parse fmt check lower`.
- fmt round trip: `gamma fmt - < f` twice must be idempotent, and a difftest program
  (`difftest --save S f.gm`) must print the same before and after (replace the file name
  in trap locations before comparing).
- LSP by hand: a small Python JSON-RPC client (initialize, didOpen, hover/definition/
  completion at positions, shutdown) is enough; see tests/harness/lsp for the real suite.
