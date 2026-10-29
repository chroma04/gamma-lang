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

## Round 2 (user: "do snapshot + first run; fast mode only if measured")
- Core snapshot: web/play/main.gm `play init` → prepare() into `#global prepared`
  (heap; Prepared holds ^sm/^diags/^Loader/^Sema); init must RETURN from main, not
  Env.exit (leaves gm_unw.kind set in memory → later export returns garbage status).
  Worker: init instance vs fresh instance, diff 64 KiB pages → snapshot chunks; each run =
  fresh Instance + memory.grow + copy chunks + call export `gamma_play_run` (not _start:
  wasi-libc crt1 `_start.started` traps on a 2nd start). Export ends with libc exit(status)
  (flushes stdio; JS gets Exit(code)). PlayEnv handler: no args/vars.
- Compiler: `#export` on Wasi = `export_name` + `gm_start()` (shared with main via
  static started) + trampoline gm_xt<k> through GM_RUN_EXPORT (gm_run_export[_fibers];
  fibers variant drives gm_wasi_drive unless gm_wasi_running; drive now resets
  gm_wasi_running=NULL on return). Without the driver an export that switches fibers
  unwound straight out to JS. wasm-ld wraps command exports with ctors/dtors (seen).
  Harness test wasi/exports_without_main (wasmtime --invoke; CI only — wasmtime download
  was refused locally; verified with play-worker's WASI shim in node instead).
- Warm-up: after "ready", worker runs WARM_UP program quietly: first run ~halved
  (hello 101→77, effects 144→67, mandelbrot 552→336 ms).
- MODE=fast measured (medians, 3 alternating rounds): runs 0.87–1.53x (noise), load
  0.82x (module 6.8 vs 7.8 MB). Not adopted.
- Result (Node, warm): hello ~11 ms, most examples 10–30, comptime ~30–55, allocators
  ~60, life ~90, mandelbrot ~200. Rust era: ~30 ms.

## Remaining levers (not done)
- First run still ~70 ms (V8 tier-up). Browser code caching helps on revisits.
- mandelbrot/life dominated by interpretation itself (VM work, decisions/056–058).
- Reproduce: build with `CC="clang-18 --target=wasm32-wasi" web/play/build.sh DIR`
  (apt: wasi-libc libclang-rt-18-dev-wasm32 binaryen), drive play-worker.js in a node:vm
  sandbox like check.mjs but in one process, N runs per example, read `ms` of the exit msg.
