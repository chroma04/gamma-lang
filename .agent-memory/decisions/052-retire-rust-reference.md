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
2. DONE s47 (PR #93): Close Rust-only gaps: all conformance lanes against gammac, Windows release by
   cross-compiling (--target=x86_64-windows, MinGW), playground + fuzz ported, release
   pipeline/install from seed, docs. [release + site without cargo]
3. DONE s48 (PR #94, pending merge): Ship gammac as `gamma`; tag `rust-reference-final`; record final cross-check
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
- Release: tools/package-gammac.sh (layout bin/gammac + lib/gamma/{std,core,runtime};
  version from compiler/gamma.toml, must equal Cargo.toml), tools/smoke-gammac.sh, release.yml
  jobs gammac / gammac-windows (cross, MinGW on ubuntu-22.04) / gammac-windows-smoke; install
  scripts GAMMA_ARCHIVE=gammac. load/exe.gm: executable() (/proc/self/exe, _NSGetExecutablePath,
  GetModuleFileNameW), runtime next to std first. Windows quoting in pkg/link.gm.
- Windows gammac runs under wine (check/fmt/interp); its C back end only tested in CI jobs
  gammac-windows-build + gammac-windows (conformance+examples). Expect fixes there.
- Pain: `when target.os` in a module that declares its own `target` → misleading "cannot
  evaluate" (worker hit 2x). Release gammac is Debug mode (like bootstrap) — maybe Fast later.

## Phase 3 plan (s48)
- Rule: no CI job runs the Rust `gamma` on Gamma sources that can change (tests, lib,
  compiler, docs); Rust is only compiled (fmt/clippy/msrv/audit/fuzz-compile) until phase 4.
- gammac names itself `gamma` in every user-visible string; release archives
  gamma-<asset> with bin/gamma (Fast mode build); Rust release job dropped.
- tests/*.rs: one helper (common) for the compiler under test = GAMMA_TEST_COMPILER;
  Rust-internal suites deleted (cranelift, ice, interp_children/deadline, architecture_doc
  code map of src/), selfhost.rs loses every *_matches_the_reference test.
- CI lanes (test, safe, s390x, aarch64, wasi, difftest, benchmarks) on seed-built gammac.
- Tag rust-reference-final = the commit before the switch; cross-check recorded in
  docs/self-hosting.md.
- Final cross-check DONE (s48) at main d09a1a6 (= tag rust-reference-final): Rust release
  `gamma build compiler` (1m50) → gammac_R; gammac_R and seed-built gammac `build compiler
  --emit-c` (GAMMA_STD=lib/std relative, same cache env) → identical 61,253,433 bytes,
  sha256 0584d6b50c606eb43f55f644abe299295c163fece27a82d609c99f0fb5b1831b.
- Rebuilding gamma with target/bootstrap/gammac and its cache: ~2m10 on 4 cores.

## Phase 3 status (s48, branch claude/retire-rust-phase-3)
- Done: tests/common gamma()/compiler()/compiler_command() (GAMMA_TEST_COMPILER required,
  panics without); tools/test.sh (bootstraps or rebuilds target/bootstrap/gamma when a
  cksum of compiler/lib/runtime changes); bootstrap outputs gamma0/gamma1/gamma; compiler
  strings say `gamma` (cache keys keep "gammac"); selfhost.rs = fixed point, unit tests,
  fmt --check, split units, packaged layout; tests/lanes.rs (GAMMA_TEST_LANE
  safe|s390x|aarch64|wasi; aarch64 lane new, untested locally: no cross tools here);
  deleted suites cranelift/ice/interp_children/interp_deadline/ffi_interp/install_layout,
  web/gen/check-parity.sh; conformance.rs lost the cranelift lane + directive.
- `run` keeps C as default (gammac behavior; tree-walking interp too slow to be the
  default); test/bench C only. Docs/CHANGELOG say so.
- CI: every job bootstraps; Rust only fmt/clippy/msrv/audit/fuzz-compile. Release: Fast
  mode `gamma build --mode=fast compiler` (single TU, untested in CI yet), archives
  gamma-<asset>, Windows cross with --mode=fast. install.sh/.ps1: no GAMMA_ARCHIVE.
- First run of all suites on gamma: 25 failures in 8 suites (long_expressions stack
  overflow, cli fmt --help/non-UTF-8 args, build_profiles cache layout, testing_cli
  interp test/bench, native_build relink + interp flush, freestanding rule, cross, lsp
  core copy) → 3 sonnet worktree workers; docs worker (brief in $SP/brief-docs.md).
- Language change for the gate: §7.7 `in` joins element/members by the §7.12 rule
  (check/seq.gm join_members); `else` fallback join NOT done (needs payload conversion in
  cgen/interp/eval: ElseExpr has no conversion) → follow-up, logged in pain-points.
- Machine: 15 GB RAM; 3 compiler builds at once (cc1 ~0.8 GB per unit) + rustc → OOM kills.
- s48 result: all 26 suites green locally on the merged compiler; CI new lanes green (s390x,
  aarch64, wasi, safe conformance, difftest, benchmarks, editors, bootstrap x3, fuzz).
  Worker fixes: compiler runs on a 1 GiB calloc'd fiber stack (main.gm, like interp root
  stack); freestanding check_freestanding_use (sema/names.gm); fmt/lsp --help, non-UTF-8
  args usage error (main.gm check_arguments_are_text); lsp core copy in cache, read-only;
  run cache key stamps -L libraries (native.gm library_stamps); interp stdin on host
  thread with several threads (HostOp.Stdin). Tests adapted: test/bench C only, package C
  objects not cached (build_profiles), cross tests use --backend=interp for layout prints.
- Base-branch bug fixed here: interp ROOT_STACK cap on 32-bit was 1 GB - 1 (< MIN) → every
  playground run trapped (main red since 8e75cc1).
- Fast-mode compiler: builds in 4.4 min (one TU), `check compiler` 1.9 s vs 5.0 s Debug.
- long_expressions suite takes ~20 min (20000-term chains slow in gammac): perf follow-up.
- Tag rust-reference-final created locally at d09a1a6 but the session's git proxy refuses
  tag pushes → user must push it (`git push origin rust-reference-final` after
  `git tag -a rust-reference-final d09a1a6`).
- Phase 4 next: delete src/, Cargo, web/playground + web/sitegen crates, msrv/audit/fuzz
  (Rust) jobs, docs/architecture.md map of src/ (+ tests/architecture_doc.rs); port the
  test harness (tests/*.rs) to Gamma; tests/common uses gamma::toolchain (replace).
