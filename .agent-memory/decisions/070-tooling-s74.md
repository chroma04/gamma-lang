# 070 Tooling round s74 (CURRENT)

## `test`/`bench --backend=interp`: one gamma process per test
- Parent (testing/run.gm `run_interp_tests`) re-runs its own command line with the hidden
  flag `--run-test=j` inserted after the command word (`run_exe` replaces argv[2]); the
  child checks the program again and interprets selected test j (`driver.run_interp_entry`).
- Why a process, not in-process interpretation: traps/`exit` end only that test, `--timeout`
  kills it, `-j` works, and output capture is the same code as for native tests.
- Why a flag, not an env var: an env var is inherited by programs the test spawns (a test
  that runs `gamma test` would see it).
- Cost: each child re-checks the program (compiler package ~1 s/test; 174 compiler tests
  in 46 s at -j3, 172 pass, the 2 failing reach foreign C and are refused). Small packages:
  faster than a C build (bigint filter: 4.3 s interp vs 8.5 s C).
- Not done: per-test native fallback for foreign code; sharing the checked program.

## LSP supervisor
- Traps cannot be caught (`#trap_hook` is freestanding-only), so `gamma lsp` =
  lsp/supervisor.gm forwarding to `gamma lsp --worker`. Kept for replay: initialize,
  initialized, didChangeConfiguration, didChangeWorkspaceFolders, per uri didOpen + last
  full didChange (server advertises full sync, change=1). Replayed initialize's response
  is dropped (first response of a new child). A crash with no new client message since
  the child started ("fresh") does not restart at once: the next client message does.
  Cap: 50 crashes per session.

## Package C objects cached
- `native/obj-<key>.o` + `.deps` (key: path, text, cc, CFLAGS, flags, `cc --version`);
  `-MD` lists headers, `.deps` stores 16-hex sha256 per file; any mismatch recompiles.
  The cache cleaner already handled `native/obj-*`. `Link.temporary` = what
  `remove_objects` deletes (cached objects must survive).

## Rejected: several revisions of one repository in a graph
- The guide (15-packages "One revision per repository") makes it a rule: no side-by-side
  versions (two copies of a module = two distinct sets of types). No user evidence; keep.
