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
- Worktree subagents (isolation: worktree) are created from the repo's default branch (old main here), not the session branch: brief them to `git reset --hard <session branch tip>` first.
- Force-push is denied by the permission classifier: never push a commit you may want to drop.
  Never make memory-only commits (their messages can't avoid mentioning notes); fold memory
  edits into code commits.
- Cross testing: `apt-get install gcc-aarch64-linux-gnu qemu-user gdb-multiarch` works; register
  binfmt with `cat /usr/lib/binfmt.d/qemu-aarch64.conf > /proc/sys/fs/binfmt_misc/register`, then
  `CC=aarch64-linux-gnu-gcc CFLAGS=-static cargo test --test conformance` runs natively-built
  tests under qemu. Debug: `qemu-aarch64 -g 12345 exe & gdb-multiarch -ex "target remote :12345"`.
- Don't edit src/ while a background `cargo test` runs: the CC=clang second run rebuilds
  with the half-edited sources. Docs/memory edits are fine.
- To find which gcc pass breaks generated C: compile with -fno-<pass> flags one by one, then
  bisect functions with `__attribute__((optimize("no-strict-aliasing")))` (s6 found main()).
