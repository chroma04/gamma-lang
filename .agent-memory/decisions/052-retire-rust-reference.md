# 052 Retiring the Rust reference; C seed bootstrap (s47, 2026-10-27)

User (thread "Decide the Rust reference compiler's future"): after gammac's interpreter
(PR #91) gammac ≈ gamma; asked what to do with src/. Recommendation page (artifact
https://claude.ai/artifact/34fVuW6LSGGLag6wL85TbK) → user approved phase 1.

## Decision: retire Rust, bootstrap gammac from a checked-in C seed
Rejected: keep (every language change twice), living seed (same), shrink to check+cgen
(keeps sema/cgen = where changes land; tools are only ~18.6k of 91.5k lines), freeze as
seed (gammac could not use new features → kills the evidence loop / dogfooding).
Loss accepted: no second front end. Replacements: conformance expectations (inline
`//~`, goldens), gammac interp vs C lanes, fuzz targets on gammac, playground via
gammac wasm32-wasi, cranelift dropped (user: out of scope).

## Phases (gate in brackets)
1. DONE s47: seed next to Rust. [CI bootstraps on ubuntu x86/arm + macOS, fixed point]
2. IN PROGRESS s47 (same branch): Close Rust-only gaps: all conformance lanes against gammac, Windows release by
   cross-compiling (--target=x86_64-windows, MinGW), playground + fuzz ported, release
   pipeline/install from seed, docs. [release + site without cargo]
3. Ship gammac as `gamma`; tag `rust-reference-final`; record final cross-check
   (Rust-built vs seed-built gammac emit identical C); drop reference comparisons from
   tests/selfhost.rs. [first language change without Rust]
4. Delete src/, Cargo, playground crate, msrv/audit jobs; port test harness to Gamma.

## Seed design (phase 1)
- bootstrap/seed/: Debug-mode C (checks on) from `GAMMA_C_UNITS=8 gammac build compiler
  --emit-c`: gammac.h (GM_RT_SEPARATE + rt_h + shared text), gammac.rt.c, gammac.c +
  gammac.1-7.c (~7 MB each, 60 MB total; GitHub warns >50 MB/file). Fast mode can't be
  split (`static inline` fns rejected by units.gm split) → Debug.
- SOURCE = `git rev-parse HEAD:compiler|lib|runtime`. bootstrap/verify.sh extracts those
  trees (git archive), gammac0 regenerates C, diff -r. Needs fetch-depth 0; x86-64 Linux
  only (fallback names in host_names embed the generating build's target.os/arch).
- GAMMA_STD must be RELATIVE (lib/std) when generating: std file names land in the C.
- Host: load.host_target() uses uname on Linux/macOS/FreeBSD builds (utsname fields 65
  bytes Linux, 256 BSDs; machine = 5th field); ptr/endianness stay compile-time. Field
  defaults are evaluated at compile time → driver.Options.target has no default now
  (main.gm passes host_target()). pkg cache_dir uses host os at run time.
- Refresh only when compiler/ stops building with the seed; two-step rule for new
  features (teach compiler + refresh, then use). tools/refresh-seed.sh [GAMMAC].
- Timings (4 cores): build.sh ~3 min (seed cc 46 s, two gammac builds ~1 min each with a
  cache in $out/cache; without a cache Debug builds are one TU → ~7 min). verify 11 s.
- FreeBSD target C differs a lot from Linux (not investigated; not a seed host).

## Phase 2 status (s47)
- Conformance + examples: GAMMA_TEST_COMPILER=<gammac> in tests/conformance.rs and
  examples.rs (run → --backend=interp first lane; test → C; no cranelift). All pass. CI
  job conformance-gammac (ubuntu+macos, seed-built gammac).
- Found+fixed: interp Net connect/accept/send/recv blocked all green threads → HostOp on
  host threads (interp/threads.gm on_host_thread).
- web/gen (Gamma site generator, path dep on compiler pkg; check-parity.sh vs Rust:
  identical 79 files). web/play (gammac interp for wasm32-wasi; JS in-memory FS from
  gamma-play-libs.json; 10.6 MB wasm vs 3.8 MB Rust; slower start, checks core each run).
  pages.yml: build job = gammac only; rust-web job keeps Rust crates + parity.
- fuzz/gammac: libFuzzer via -Dmain=gm_program_main + shim.c (SIGALRM fix); CI fuzz-gammac.
- Release + Windows cross-build: worker (see report when merged).
