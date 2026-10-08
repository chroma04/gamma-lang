# Environment / tooling gotchas
- Cloud container: Rust 1.97, gcc 13, clang 18, python3, go, node available. crates.io reachable.
- No `gh` CLI: use mcp__github__* tools (load via ToolSearch) for PRs.
- Integration tests run with cwd = package root; harness passes relative paths so diagnostics
  print `tests/conformance/...` paths.
- `cargo fmt` reformats files other agents/you hold open → re-Read before Edit.
- Before every commit run `RUSTFLAGS="-D warnings" cargo clippy -q --all-targets` (CI fails on
  any warning; `cargo fmt` can't catch clippy issues). Once pushed a needless-borrow by mistake.
- Fold .agent-memory updates into code commits; never a separate commit whose message would have to mention notes/memory.
- macOS CI compiles generated C with Apple clang; locally `cc` is gcc. clang rejects things gcc
  accepts (e.g. `(T){0}` for a zero-member struct → use `(T){}`). Before pushing C back-end
  changes run `CC=clang cargo test -q` too.
