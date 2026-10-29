# 059 — Playground speed: the 1 GiB root stack (s57)

User: playground examples took ~30 ms with the Rust build, ~1 s self-hosted, "fundamental
redesign?". Answer: no — one bug made ~90% of the time.

## Measured (Node 22, web/play built Checked, per run after the first)
- Before: every example ~570–770 ms, hello included. Native same program: 24 ms.
- Phase timing (instrumented copy of web/play/main.gm printing Clock.now() deltas):
  run_interp 535 ms for hello in wasm vs 0.76 ms native. Cause: interp/fiber.gm
  root_host_stack calloc(1 GiB) (ROOT_STACK 4 GiB → halved to MIN_ROOT_STACK 1 GiB on
  32-bit); wasi-libc dlmalloc has no mmap, so calloc memsets it all, every run (and
  grows the page's memory by 1 GiB).
- Fix: `when target.arch is .Wasm32` ROOT_STACK = MIN_ROOT_STACK = 8 MiB. Shadow stack per
  interp call ~2–4 KB; browser stack overflows at depth ~1–2k first (unchanged
  behavior); with node --stack-size=50000 the interp traps `stack overflow` cleanly.
- After: hello 45 ms, most examples 30–65 ms, life ~100, mandelbrot ~145; first run after
  load ~260 ms (V8 lazily compiles the 7.8 MB module).

## Remaining levers (not done)
- load_core: ~20 ms of every run (native also ~20 ms: parse+collect of lib/core). A
  snapshot of the loaded/checked core (e.g. Wizer-style pre-initialized memory, or a
  reactor module that keeps the instance and restores a memory copy) would remove it.
- First run: V8 lazy compile; streaming compile + code caching helps on revisits only.
- MODE=fast for web/play/build.sh (not measured).
- Reproduce: build with `CC="clang-18 --target=wasm32-wasi" web/play/build.sh DIR`
  (apt: wasi-libc libclang-rt-18-dev-wasm32 binaryen), drive play-worker.js in a node:vm
  sandbox like check.mjs but in one process, N runs per example, read `ms` of the exit msg.
