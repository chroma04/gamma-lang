# Agent operating rules (gamma-lang)

**First, read `.agent-memory/INDEX.md`.** It holds the project state, the next steps and a map of the memory folder.

This repo implements the Gamma language (spec: `gamma-spec.md`) in Rust. Work spans many sessions; only what is committed survives.

## Session workflow
1. Orient: read this file and `.agent-memory/INDEX.md`. Check open PRs / unmerged branches; build on unmerged prior work rather than redoing or conflicting with it.
2. Pick a coherent chunk of the roadmap (`.agent-memory/roadmap/`) and do it.
3. Wrap up while there is comfortable room left: build + tests pass (`cargo fmt --check`, `cargo clippy --all-targets`, `cargo test`), update memory (INDEX.md especially), commit, push to the session's designated branch, open a PR. The default branch must always work after a PR.

## Memory rules (`.agent-memory/`, for the agent only)
- Hierarchical: INDEX.md (< ~80 lines) → roadmap/, architecture/, decisions/, spec-notes/, sessions/, gotchas/. Files focused, < ~200 lines; split when they grow; keep INDEX.md's map accurate.
- Describe current truth: rewrite stale notes, delete obsolete ones, compact old session logs.
- Update incrementally during the session, not only at the end.

## Standing rules
- The spec is a living document: fix contradictions/ambiguities/impractical features directly in `gamma-spec.md`, keep it coherent, update examples and tests. Note non-obvious amendments in `.agent-memory/spec-notes/` if useful.
- Gamma is evidence-driven: when writing Gamma (gammac, std, examples), a usability pain point hit repeatedly is flagged as a possible language change (log it in `.agent-memory/spec-notes/pain-points.md` and tell the user). If it is minor and has a clear design in the spirit of the language, implement the change directly (spec, implementation, tests, docs) instead of working around it; prefer refinements and generalizations of existing rules over exceptions and additions.
- Autonomy: make design decisions yourself; record non-obvious ones in `.agent-memory/decisions/`. Only stop to ask when truly blocked (e.g. missing access), after finishing all unblocked work.
- Repo hygiene: nothing outside `.agent-memory/` and this file may reference them, previous sessions, or "notes" — not code comments, docs, commit messages or PR descriptions. Build, tests and CI must not depend on the memory folder. Contributor-relevant rationale goes in real docs (`docs/`).
- Write real documentation as you go (README, `docs/guide/`, `docs/architecture.md`, `docs/contributing.md`) and thorough tests, including conformance tests derived from the spec (`tests/conformance/`, format documented in `tests/conformance.rs`).
- Orchestration: keep architecture, spec amendments, interfaces, integration, review and all memory writes for yourself. Delegate substantial well-specified work (components, test batches, docs sections, stdlib modules, audits) to subagents on a smaller model, with complete briefs (goal, spec sections, interfaces, owned files, how to verify, what to report). Run independent subagents in parallel when file ownership doesn't overlap. Review and test delegated output before committing. Subagents never edit `.agent-memory/`.
