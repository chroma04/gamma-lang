# C back end strengthening plan (from the Oct 9 LLVM evaluation; prioritized in session 5)
Status: 1 done (s5), 2 done (s6). Order = value / cost. Keep every item differential-tested on both back ends.
1. DONE Source-level debugging: `-g` (decisions/013).
2. DONE Faster native builds (decisions/014): cached runtime object + -O1 for Debug.
3. Toolchain/cross: `CC` with arguments (`zig cc -target …`, `clang --target=…`) — today
   Command::new($CC) fails on spaces; honor CFLAGS/LDFLAGS; layouts use Target::host() so a
   cross target needs Target from flags; wasm32-wasi without fibers.
4. Exit flag (Appendix D.2): calls test the TLS `gm_unw.kind` after every effectful call and
   every frame keeps locals in struct F; return the flag (or a bool) instead and keep
   uncaptured locals as C locals (needs escape info: closures/handler clauses via env).
5. Handler specialization beyond one level (decisions/012 "Deferred"): adapters/recursive
   iterators; skip gm_unw check after clauses that cannot exit.
6. Remaining big-value copies (bugs-open #7): call result temp → local, aggregate temps.
7. Debug views for tuples/tagged unions/slices (gdb pretty printers or typed mirrors).
