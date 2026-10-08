# Environment / tooling gotchas
- Cloud container: Rust 1.97, gcc 13, clang 18, python3, go, node available. crates.io reachable.
- No `gh` CLI: use mcp__github__* tools (load via ToolSearch) for PRs.
- Integration tests run with cwd = package root; harness passes relative paths so diagnostics
  print `tests/conformance/...` paths.
- `cargo fmt` reformats files other agents/you hold open → re-Read before Edit.
