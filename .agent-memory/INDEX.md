# Gamma implementation — agent memory INDEX (read first)

## State (2026-10-24) — PRs #1–#71 merged, v0.1.0 tagged
- s38 (PR #72 merged; then s38b on the same branch): self-hosted compiler (decisions/043,
  roadmap/self-hosting.md): compiler/ = gammac; lexer + parser byte-identical to
  `gamma lex`/`parse --dump` (+ JSON diags) on all ~980 .gm files, tests/selfhost.rs.
  docs/guide/idioms.md = growing idioms handbook (doc_examples-checked; ADD to it whenever
  a pattern is learned). `?T in (..)` fix (§7.7). s38c: stage 3 modules (`gamma modules`),
  C-backend assignment-order bug fixed. s38d: stage 4a declarations (`gamma decls`,
  compiler/types+sema+bigint+ratio, "gammac cannot" skip protocol + DECLS_FLOOR ratchet);
  `in` takes patterns (§7.7). s38e: stage 4b bodies started (`gamma tir`, compiler/tir +
  compiler/check, PR #76); s38f: patterns/for/methods/errors/sequences, TIR_FLOOR 100,
  `&e` expected-pointee hint. s38g (branch claude/project-thread-735qf0): generics in
  gammac (check/generic.gm, sema instantiate + mono walk), TIR_FLOOR 137 (PR #78 merged).
  s39 (branch claude/project-thread-nhzd4o): pain-point language changes (decisions/044:
  pub(package), needless-pub/dangling-address/present-only-binding lints, payload-binding
  note) + gammac defer, typed consts, intrinsics, fn literals/nested fns/fn pointers,
  `with` + handler literals/decls + iterators + local when + tests: TIR_FLOOR 320 (PR #79,
  ask user before merging). Next: `ctl`/fibers (`with … on`), long tail (roadmap/self-hosting.md).
- s35–s37: complex `Complex(T)` + `#vec [N]T` (039), `#soa` (040, generic 042: SoaList),
  stdlib batch 1 (041; built by sonnet subagents in worktrees). tree-sitter CLI: `npm ci`
  in editors/tree-sitter-gamma (node 22 in /opt/node22).
- s34: HashMap → std/collections/hash_map (038); s33: spec ch. 23 errors policy, spread (037).
- s32 (PR #63, branch claude/kind-allen-vuxksg): whole-codebase review; findings + status
  in roadmap/review-2026-10-20.md (P0/P1, most P2/P3 fixed; rest listed there). Fuzz
  targets in fuzz/ (manifest, archive; seeds via fuzz/seed.sh). New infra:
  tests/common (GAMMA_TEST_C=1 required in CI, GAMMA_TEST_TIMEOUT), cgen::is_runtime_symbol,
  cgen::write_generated_c/scratch_path, clif c_abi_ptr/c_thunk/c_shim.
- s31: website + playground in web/ (decisions/036); broken doc anchors fail the Website job.
- s30: `gamma lint` (033), wasm32-wasi (034), Cranelift (035), C/Rust guides checked by
  tests/doc_examples.rs (add a doc to CHECKED_DOCS to opt in). s29: outlining (031), perf (032).
  s28: outside design review applied (030).
- The whole spec is implemented in all back ends (interpreter = bytecode VM only since s29,
  decisions/020; C back end, gcc and clang; Cranelift, 64-bit Linux/macOS only, no -g).
  Every run test runs on each and must match.
  Milestones: roadmap/milestones.md (all done; per-area gaps). Decisions: decisions/.
- MSRV 1.95 (Cargo rust-version, CI job msrv). Tooling: packages (git/path/archive deps, lock, profiles), `gamma lsp`, editors/, release
  pipeline + install.sh/.ps1, gamma doc/repl/fmt/test/bench/coverage, Windows (MinGW) port.
  Left for the user: tag v0.1.0, publish the VS Code extension (Marketplace/Open VSX tokens).
- Build modes: `--mode=debug|checked|safe|fast` (Safe needs Fil-C in /opt/fil). Core modules
  (is_prelude) may use @heap_*, prelude privates, `is`/@hash on params.
- CI = fmt + clippy -D warnings + test (ubuntu, macos, windows) + Safe + s390x cross +
  aarch64 qemu (C + Cranelift) + editors + benchmarks. Verify locally (fmt, clippy -D warnings, cargo test, CC=clang
  conformance for cgen changes) before pushing.
- examples/: run by tests/examples.rs (cases in tests/examples/*.run, format in its
  header); new examples need a case and `gamma fmt`.
- Older history: sessions/ and decisions/ (s24 `gamma fix`/json diags 028, s27 wishes 029).

## Next steps
0. Drive the s32 review PR to merge; then continue roadmap/review-2026-10-20.md "Open".
1. Keep CHANGELOG Unreleased current. User: enable private vulnerability reporting.
   Diagnostics must follow spec ch. 23 (policy for new errors/hints; limits in §23.5).
1b. Self-hosting: roadmap/self-hosting.md (stage 4b bodies in progress: widen coverage).
2. roadmap/todo.md: the prioritized list agreed with the user (P0 → nice). P1 done (s18);
   next P2: fuzz sema/differential, perf, Windows, Emacs.
3. VM follow-ups in decisions/020; bugs-open.md.
4. Keep docs current with every change: CHANGELOG Unreleased, docs/std regen
   (`gamma doc lib/std --all -o docs/std`), lib/ `gamma fmt`-formatted (tests/fmt.rs).

## Map
- roadmap/        todo.md (prioritized to-do), milestones.md (M0–M12 + gaps), bugs-open.md,
                  self-hosting.md (gammac stages),
                  c-backend-plan.md, review-2026-10-20.md (s32 review findings + status)
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001–044, one topic each (filenames say it): 008 C back end, 017 Safe/Fil-C,
                  020 bytecode VM, 023 tooling, 025/030 design reviews, 033 lint, 034 wasm,
                  035 Cranelift, 036 website, 037 errors policy + array spread, 043 self-hosting, 044 s39 pain-point changes. `ls decisions/` for the rest.
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved) ·
                  pain-points.md (repeated usability pain: flag to user, user rule)
- sessions/       s01–s30 logs (compacted; s28 = decisions/030, s29/s31/s32 have none)
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu,
  macos, windows) + Safe + cross + editors + benchmarks. Use source::canonicalize, never
  Path::canonicalize (Windows \\?\ paths).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- Effects: `fn() -> T needs Alloc + Out`; set args `Sched(Out + Clock)`; pure C fn `needs not`.
- `print("{} {}", a, b)`: values are rest args. Ops always qualified: `Clock.now()`, `Log.log(..)`, `f.read(buf)`; only `yield` bare. Inferable type
  params by name, first: `new(T: Page, ---)`. Tests: `ok(x)`, no `try`.
