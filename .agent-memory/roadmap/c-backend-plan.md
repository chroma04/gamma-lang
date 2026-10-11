# C back end strengthening plan (from the Oct 9 LLVM evaluation; prioritized in session 5)
Status: 1 done (s5), 3 done (s6). Order = value / cost. Keep every item differential-tested on both back ends.
1. DONE Source-level debugging: `-g` (decisions/013).
2. Faster native builds: conformance suite spends most time in cc -O2 (86 s vs ~45 s at -O0,
   runtime ~2.2k lines recompiled per program, ~0.25 s each). Ideas: Debug mode → -O1/-O0
   by default (perf of debug builds vs turnaround), precompiled runtime object/PCH cached
   by hash in target dir, parallel test builds.
3. DONE (s6, decisions/014) Toolchain/cross: CC with args, CFLAGS/LDFLAGS, target probed from
   the compiler. Left: big-endian runtime, wasm32-wasi (no fibers/threads), `--target=` flag.
4. Exit flag (Appendix D.2): calls test the TLS `gm_unw.kind` after every effectful call and
   every frame keeps locals in struct F; return the flag (or a bool) instead and keep
   uncaptured locals as C locals (needs escape info: closures/handler clauses via env).
5. Handler specialization beyond one level (decisions/012 "Deferred"): adapters/recursive
   iterators; skip gm_unw check after clauses that cannot exit.
6. Remaining big-value copies (bugs-open #7): call result temp → local, aggregate temps.
7. Debug views for tuples/tagged unions/slices (gdb pretty printers or typed mirrors).
