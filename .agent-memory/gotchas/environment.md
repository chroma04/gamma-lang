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
- Worktree subagents (isolation: worktree) are sometimes created from the LOCAL `main` ref, which
  the fresh clone may leave stale (s14: at PR #34 while origin/main was #36). Before spawning:
  `git fetch origin main && git branch -f main origin/main`, and brief every agent to
  `git reset --hard <session branch tip>` first and verify a recent file exists.
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
- Fil-C: `curl -LO https://github.com/pizlonator/fil-c/releases/download/v0.686/optfil-0.686-linux-x86_64.tar.xz`,
  extract, `sudo ./setup.sh -u` → /opt/fil (glibc, has ucontext). Fil-C panics exit 134.
  It rejects top-level asm and pointer operands in inline asm; casts via integers lose capability.
- NEVER `pkill -f`/`pgrep -f` with a pattern that occurs in your own command line: it kills the
  shell running it (exit 144).
- filcc compile time is superlinear in function size (example2_editor's main was 7.7k C lines
  from duplicated defers: 8 min). Fixed by the shared ladder; keep generated functions small.
- Don't put /opt/fil/bin first on PATH for tests: gcc then picks Fil-C's ld and every native
  build fails. The Safe lane finds /opt/fil/bin/filcc by itself (GAMMA_TEST_SAFE=1).
- filcc drops accesses it can prove fail (zmkptr(NULL, n) deref → no trap, exit 0); test Safe
  traps at -O2, not only -O0.
- clang treats `_Static_assert` messages as unevaluated strings: numeric escapes (`\077`) and
  unknown escapes (`\ `) are errors there, gcc only warns. Use cgen::types::assert_message.
- When merging a stack of PRs, run the CC=clang conformance lane after each cgen/sema pair:
  PRs green alone can clash semantically (e.g. new type syntax × new C emission).
- Disk: the session allowance is ~40 GB. Six parallel worktree agents (≈3 GB target each)
  plus main target (6 GB) plus downloads (wine, llvm-mingw, Fil-C) filled it in s12. Remove
  merged worktrees right away; brief agents to keep one target dir and delete downloads.
- Workers share the scratchpad: give each its own subdirectory name (they deleted mine once).
- CI uses the newest stable Rust (1.99 in Oct 2026); local is 1.97. Before pushing run CI's clippy: `RUSTFLAGS="-D warnings" CARGO_TARGET_DIR=target/stable rustup run stable cargo clippy -q --all-targets` (stable toolchain installed in s12).
- Tests run as root here; CI is not root, so permission bugs (read-only checkouts) hide locally. Check such tests with `runuser -u nobody -- env HOME=/tmp/x TMPDIR=/tmp <test binary>` (chmod 1777 target/tmp, o+x on the path).
- Windows compile check without CI: `apt-get install gcc-mingw-w64-x86-64; rustup target add x86_64-pc-windows-gnu; cargo check --target x86_64-pc-windows-gnu` (libffi-sys needs the mingw gcc).
- Brief agents never to `pkill -f "cargo build"` (or any shared pattern): it kills the other
  agents' builds too (s14, twice).
- `cargo test` stops after the first failing test binary: use `--no-fail-fast` to see all.
- Newer clang for testing: `apt-get install clang-19 clang-20` (Ubuntu noble). Never time
  compiles while another background compile runs (s20 got garbage numbers).
- gdb on a running compiler: `ps -eo pid,comm | awk '$2 ~ /^clang/'` (never pgrep -f).
