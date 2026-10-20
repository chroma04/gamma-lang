# 036 Website + playground (s31, 2026-10-19)

User asked for an in-repo GitHub Pages site, plain but beautiful, playground if possible.

## Shape
- web/: layout.html, pages/{index,playground}.html (front-matter comment line 1),
  static/ (style.css, play.js, play-worker.js, favicon.svg) → _site/assets/,
  examples/ (tour programs + list.txt menu; `| exit 134` third column), check.mjs (node:wasi,
  one child process per run — Node's WASI segfaulted with several instances in one process).
- web/sitegen: own [workspace] crate (pulldown-cmark + gamma lexer for highlighting; GitHub
  slugs; .md links → .html, other repo files → GitHub blob/tree URLs; README.md → index.html;
  guide order = by file name; link+anchor check fails the build). Written by a subagent.
- web/playground: own [workspace] crate, wasm32-wasip1 (set in its .cargo/config.toml with
  -zstack-size=32 MiB). Reads source from stdin; `run` or `fmt`. std embedded via build.rs
  as Session.overlays under /std with GAMMA_STD=/std (no preopens; fs calls just fail).
  ~3.2 MB release wasm; hello runs in ~100 ms in node.
- Workflow .github/workflows/pages.yml: PRs build+check; main deploys (actions/deploy-pages).
  User must set Settings → Pages → Source = GitHub Actions.

## Compiler changes (all cfg(target_family = "wasm"))
- Cargo: cranelift-* and libffi are target-specific deps (not wasm); lib.rs skips clif.
- interp/ffi_wasm.rs stands in for ffi.rs. run_entry/eval_const run inline (no thread);
  STACK_SIZE 24 MiB / margin 2 MiB on wasm. proc.rs: kill/wait_readable fallbacks.
- Not supported in the playground: fibers (`with … on`, std/fiber, generators), threads,
  Fs/Net/processes, C. Follow-up idea: fibers via Asyncify (wasm-opt) or JSPI, and an
  in-memory Fs like the test runner's (then `gamma test` mode in the playground).

## Look
Paper (#fbfaf7) + ink, one accent (terracotta #b4441f / #ef8a5f dark), serif prose
(Charter stack), system sans UI, mono code with ligatures OFF (`font` shorthand resets
font-variant-ligatures — re-disable after it). Body class is `page-<layout>` (a bare `doc`
class collided with the `.doc` grid once). Screenshots via playwright at
/opt/node22/lib/node_modules/playwright + `python3 -m http.server -d _site`.
