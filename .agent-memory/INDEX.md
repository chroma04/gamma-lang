# Gamma implementation — agent memory INDEX (read first)

## State (2026-10-14) — PRs #1–#33 merged; s6–s10 = parallel Claude Projects threads
- s11 (PR #34, branch claude/inspiring-johnson-094ac5): the user's to-do P1 tooling items —
  package manager + gamma new/init, `gamma lsp`, editors/ (TextMate, tree-sitter, VS Code),
  release pipeline + install.sh, README refresh (decisions/023, sessions/s11). Remaining items
  of that to-do list are P2/nice (Windows, build profiles, gamma doc, REPL, coverage, …).
- 2026-10-14 merge round: #25–#33 merged in order onto main (papercuts, spec pass, C typed
  structs, interpreter FFI, effect-set params, literals, op hiding, --target/big-endian, VM
  speedups). Merge-only fixes: SavedFrame swap_frame needs open_ret; C layout asserts escape
  type names (effect sets print `\ {..}`); VM kernel test used overflowing i128 product.
- Merged threads (details in decisions/ and sessions/): bytecode VM (020), perf round (019),
  bug hunt s8, dogfooding s7 (§22 stdlib policy — apply it to every std addition; 018), Fil-C
  Safe mode (017), C perf (016), native build speed (014), cross-compiling (015), debug info
  and gdb views (013), std/text graphemes. Leftover bugs: roadmap/bugs-open.md #31–40.
- The whole spec is implemented in both back ends (interpreter + C), except "Not yet". Every run
  test runs on both and must match (gcc and clang). Milestone files roadmap/M00..M12; decisions/.
- Sessions 1–3: see sessions/. Session 4: C big values by address, std/collections, qualified
  generic names + declaration aliases + re-exported modules, dogfood round 4 (example4_*) and
  its fixes (sessions/s04). Session 5: `gamma test` runs imported same-package modules'
  tests, `--filter=` (sessions/s05).
- GitHub Actions has runners again (2026-10-12), incl. the Safe (Fil-C) job; still verify locally
  (fmt, clippy -D warnings, cargo test, CC=clang conformance) before pushing.
- Open bugs/usability list: roadmap/bugs-open.md; open design items: spec-notes/open-issues.md.
- Build modes: `--mode=debug|checked|safe|fast` (Safe needs Fil-C in /opt/fil). Core modules (is_prelude) may use @heap_*, prelude
  privates, `is`/@hash on params.

## Next steps
1. Drive PR #34 green. Next to-do candidates (P2): build profiles in gamma.toml, gamma test
   --all, gamma doc, REPL, Windows runtime, Safe-mode allocation speed, diagnostics polish.
2. bugs-open.md leftovers (#23–#40); computed array lengths done (decisions/007); VM
   follow-ups in decisions/020 (superinstructions, smaller Op, cheaper calls).
3. lib/ must stay `gamma fmt`-formatted (tests/fmt.rs checks).
4. Safe mode follow-up (decisions/017 end): constant large args static (done, PR #17);
   left: `&key` escapes in std maps, `new`'s GC alloc (allocation-heavy code ~13× Checked).

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria); bugs-open.md;
                  c-backend-plan.md (prioritized C back end weaknesses)
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
                  · 022 effect-set parameters (`E: effects`, `..E`, Sched(E))
                  · 023 tooling: packages, LSP, editor grammars, release
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       s01-2026-10-08.md · s02-2026-10-09.md · s03-2026-10-10.md · s04-2026-10-10.md
                  · s05-2026-10-11.md · s06-2026-10-11.md (cross) · s06-2026-10-12.md (C perf)
                  · s06-2026-10-12-safe.md (Safe mode) · s07-2026-10-12.md (dogfooding)
                  · s08-2026-10-12-bughunt.md (bug hunt) · s11-2026-10-14-p1-tooling.md
- gotchas/        environment.md

## Conventions
- Session branch per harness; PR into main; CI = fmt + clippy -D warnings + test (ubuntu+macos).
- Work in git worktrees for parallel agents (`isolation: worktree`); merge their branches.
- Docs for humans in docs/ and README.md; never reference this folder from them.
- `print` takes one args value: several arguments go in a tuple `print("{} {}", (a, b))`.
