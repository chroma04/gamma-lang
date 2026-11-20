# Running parallel worker agents: what went wrong before
- Worktree agents may start from a STALE local `main` or an old base. Push the session
  branch and `git fetch origin main && git branch -f main origin/main` first; tell each
  worker to `git merge --ff-only <session branch>` (or reset to it) and to verify that a
  recent file exists before working.
- Memory: at most two building/testing workers at once on this box (see build-and-test.md).
- Give each worker its own scratchpad subdirectory (a worker once deleted another's files).
- Worker shells may refuse compound commands (heredocs, `export … &&`, `flock` chains):
  have them write wrapper scripts in their scratch subdirectory.
- Workers that hit an account usage limit keep their worktree state; resume them later
  with SendMessage. Wide fan-outs (6–8 workers) have used a whole 5-hour quota in 10–45
  min: have audit workers write reports to files.
- Integrating several branches: PRs green alone can clash semantically; run the `CC=clang`
  conformance lane after merging each pair that touches sema and cgen.
