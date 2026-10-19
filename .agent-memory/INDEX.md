# Gamma implementation — agent memory INDEX (read first)

## State (2026-10-19) — PRs #1–#55 merged; s29 PR open: tree-walker removal (branch claude/retire-tree-walker-n1cm3g)
- s30 (branch claude/project-thread-34q66w, PR open): wasm32-wasi target via C back end +
  Asyncify fibers (decisions/033); CI job `wasi`.
- s29: huge functions split into helper C functions (decisions/031, bugs-open #48 done).
- s29 (branch claude/perf-safe-effects-16om52, PR open): Safe new/destroy −42% instr, VM
  iterators −27% (decisions/032).
- s28: applied an outside design review (decisions/030): spec §1 rewrite, no array arithmetic,
  vector `is`→bool + lanes_* methods, ints ≤128, iterator args evaluated once. Open user calls
  all done (decisions/030): effect ops PR #53, freestanding/Neg/time.ms/literal fallbacks in the third PR.
- The whole spec is implemented in both back ends (interpreter = bytecode VM only since s29,
  decisions/020; C back end, gcc and clang). Every run test runs on both and must match.
  Milestones: roadmap/milestones.md (all done; per-area gaps). Decisions: decisions/.
- MSRV 1.95 (Cargo rust-version, CI job msrv). Tooling: packages (git/path/archive deps, lock, profiles), `gamma lsp`, editors/, release
  pipeline + install.sh/.ps1, gamma doc/repl/fmt/test/bench/coverage, Windows (MinGW) port.
  Left for the user: tag v0.1.0, publish the VS Code extension (Marketplace/Open VSX tokens).
- Build modes: `--mode=debug|checked|safe|fast` (Safe needs Fil-C in /opt/fil). Core modules
  (is_prelude) may use @heap_*, prelude privates, `is`/@hash on params.
- CI = fmt + clippy -D warnings + test (ubuntu, macos, windows) + Safe + s390x cross +
  editors + benchmarks. Verify locally (fmt, clippy -D warnings, cargo test, CC=clang
  conformance for cgen changes) before pushing.
- examples/: run by tests/examples.rs (cases in tests/examples/*.run, format in its
  header); new examples need a case and `gamma fmt`.
- s27: leftover wishes (decisions/029): `[E]T`, `#simd [N]T` vectors, Proc + std/process,
  std/time/zone, fiber sleep + std/fiber/generator; rejected @fields, Send rule, enum sets.
- Recent history (details in sessions/): s26 bugs #30/#31 (C matches interp), needs fix-it,
  LLDB formatters; s25 parallel Debug C units + fix-its 2; s24 `gamma fix`/json diags/LSP quick
  fixes (decisions/028); s23 C unwind after closure calls; s22 in-place elements, VM float ops;
  s21 gcc inline, Emacs, difftest CI; s20 fiber channel, fuzz/difftest; s19 papercuts.

## Next steps
1. Drive the s29 PRs to merge. Then the user tags v0.1.0 (CHANGELOG has an Unreleased
   section: fold it into 0.1.0 if tagged after it). User: enable private vulnerability
   reporting (SECURITY.md links it).
2. roadmap/todo.md: the prioritized list agreed with the user (P0 → nice). P1 done (s18);
   next P2: fuzz sema/differential, perf, Windows, Emacs.
3. VM follow-ups in decisions/020; bugs-open.md.
4. Keep docs current with every change: CHANGELOG Unreleased, docs/std regen
   (`gamma doc lib/std --all -o docs/std`), lib/ `gamma fmt`-formatted (tests/fmt.rs).

## Map
- roadmap/        todo.md (prioritized to-do), milestones.md (M0–M12 + gaps), bugs-open.md,
                  c-backend-plan.md
- architecture/   overview.md (code map — update!), sema-notes.md
- decisions/      001 Rust · 002 pipeline · 003 interpreter · 004 test format · 005 parser ambiguity
                  · 006 generics (monomorphization by TIR cloning)
                  · 007 value params + sema-time const eval · 008 C back end · 009 interop
                  · 010 fibers · 011 stack size · 012 handler specialization + local exits
                  · 013 debug info (-g) · 014 native build speed (cached runtime obj)
                  · 015 toolchain env (CC/CFLAGS/LDFLAGS) + target from the compiler
                  · 016 C perf s6 (exit flag measured, nested spec, gm_out, borrow.rs)
                  · 017 Safe mode via Fil-C (capability-clean C, ucontext fibers, Safe arena)
                  · 018 stdlib policy §22 + s7 language changes (fmt specs, op=, .to)
                  · 019 perf round s9 (where time goes, what was measured/kept/dropped)
                  · 020 bytecode VM (shape, fallbacks, measurements, follow-ups)
                  · 021 language papercuts (tuple payloads, ret inference, generic fn ptrs, cast)
                  · 022 effect-set parameters (`E: effects`, `needs E`, Sched(E))
                  · 023 tooling: packages, LSP, editor grammars, release
                  · 024 locals not named like types (+ local consts at comptime, scope isolation)
                  · 025 design review (thesis, principles; implemented: cancel, Test effect, rest
                    params, named args/inferable params, universe w/o ops, named effect sets)
                  · 026 effect syntax `needs A + B`, `needs not` (foreign only), no `..E`
                  · 027 effect-set params inferred from the caller's Sched(E); fiber channel
                  · 028 machine-applicable fixes (gamma fix, json diags, LSP quick fixes)
                  · 029 s27 wishes: admitted ([E]T, SIMD, Proc, zones, fiber sleep/generators) / rejected
                  · 030 s28 coherence review: applied + open questions for the user
                  · 031 statement-run outlining (huge functions → helpers sharing F)
                  · 032 s29 perf: Safe blob kinds, steady by-ref params, VM upvars/perform
                  · 033 wasm32-wasi target (runtime GM_WASI, Asyncify fibers, perf)
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-s08-2026-10-08..12.md (compacted + lessons) · s11-2026-10-14-p1-tooling.md
                  · s12-2026-10-14-todo-p2.md · s13-2026-10-15-bughunt.md
                  · s14-2026-10-15-docsweep.md · s15-2026-10-15-vision.md · s16-2026-10-16-needs.md
                  · s17-2026-10-16-release-prep.md · s18-2026-10-16-examples.md
                  · s19-2026-10-16-papercuts-p2.md · s20-2026-10-16-bugs-p2.md
                  · s21-2026-10-16-bugs-p2.md · s22-2026-10-16-bugs-p2.md
                  · s23-2026-10-17-difftest-fix.md · s24-2026-10-17-fixits.md · s25-2026-10-17-units.md
                  · s26-2026-10-17-bugs-lldb.md · s27-2026-10-17-wishes.md (s28: see decisions/030)
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
