# gamma-lang: agent operating rules

Gamma is a systems language (spec: `gamma-spec.md`) implemented in Gamma itself:
`compiler/` builds `gamma`, bootstrapped from the checked-in C seed in `bootstrap/`.
Work spans many sessions, and only what is committed survives one.

**Start every session by reading `.agent-memory/INDEX.md`**: current state, next steps,
and a map of the rest of the memory folder.

## Commands
- Build: `bootstrap/build.sh` → `target/bootstrap/gamma` (about a minute, from the seed).
- Fast edit loop: `target/bootstrap/gamma check compiler` (~2 s; uses the last BUILT
  compiler, so a compiler fix shows only after a rebuild).
- Test: `tools/test.sh [-j N] [-v] [--filter TEXT] [PREFIX...]` rebuilds `gamma` when
  `compiler/`, `lib/` or `runtime/` changed, then runs the harness (`tests/harness/`).
  Run the suites a change touches while working, and the full run before pushing.
- Also before pushing: `gamma fmt --check compiler lib examples tests/harness`,
  `gamma test compiler`, `gamma lint` clean on what you touched.
- After changing code generation or core intrinsics: refresh the seed
  (`tools/refresh-seed.sh`; see `docs/contributing.md`, "The bootstrap seed").
- `docs/contributing.md` is the full reference: suites, lanes (Safe, s390x, aarch64,
  WASI), profiling, releases. Machine and tool pitfalls: `.agent-memory/gotchas/`.

## Session workflow
1. Orient: this file, then `INDEX.md`. Check open PRs and unmerged `claude/*` branches;
   build on unmerged earlier work instead of redoing it or conflicting with it.
2. Pick one coherent chunk from `.agent-memory/roadmap/next.md` (or do what the user
   asked) and finish it. Prefer a finished, tested chunk over a larger half-done one.
3. Wrap up while there is comfortable room left: tests and checks pass, memory updated,
   commit, push to the session's branch, open a PR. `main` must always build and pass.

## Standing rules
- **The spec is living.** Fix contradictions, ambiguities and impractical features
  directly in `gamma-spec.md`; keep it coherent, and update its examples, the guide and
  the tests with it. Diagnostics follow spec chapter 23.
- **Evidence-driven design.** When a usability pain point recurs while writing Gamma
  (compiler, std, examples), log it in `.agent-memory/spec-notes/pain-points.md` and tell
  the user. If it is minor and has a clear design in the spirit of the language, make the
  change (spec, implementation, tests, docs) rather than working around it; prefer
  generalizing an existing rule over adding an exception.
- **Measure before optimizing**, and record what was measured, kept and dropped.
- **Autonomy.** Make design decisions yourself and record non-obvious ones in
  `.agent-memory/decisions/`. Stop to ask only when truly blocked (e.g. missing access),
  after finishing all unblocked work.
- **Documentation and tests travel with the change**: README, `docs/guide/`,
  `docs/architecture.md` (must name every `compiler/` module), `docs/contributing.md`,
  `CHANGELOG.md` (`## Unreleased`), regenerated `docs/std/`, and tests, including
  conformance tests derived from the spec (`tests/conformance/`; format in
  `tests/harness/conformance/conformance.gm`).

## Repository hygiene
- Nothing outside `.agent-memory/` and this file may mention them, sessions or "notes":
  not code, comments, docs, tests, commit messages or PR descriptions. Build, tests and
  CI must not depend on the memory folder. Rationale contributors need goes in `docs/`.
- Fold memory updates into the commit of the work they describe. A commit that touches
  only memory is fine only when maintaining memory is the task itself.
- Never force-push; never push a commit you may want to drop.

## Memory (`.agent-memory/`)
- It is for the agent, and it records **current truth**: rewrite stale entries, delete
  obsolete ones, move finished work out of `roadmap/` (git log and `history.md` keep
  the past). Update it as you go, not only at the end.
- Hierarchy: `INDEX.md` (≤ ~80 lines: state, next steps, map) → `roadmap/`,
  `architecture/`, `decisions/`, `spec-notes/`, `gotchas/`, `history.md`. Keep files
  focused and under ~200 lines; split them when they grow, and keep the map in
  `INDEX.md` and `decisions/README.md` accurate.
- Write entries that a future session can act on: concrete paths, commands, numbers and
  the reason, not a narrative of what happened.
