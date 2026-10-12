# C back end strengthening plan (from the Oct 9 LLVM evaluation; prioritized in session 5)
Status: 1 done (s5); 4, 5, 6 done in s6 (decisions/014); 2, 3, 7 by parallel s6 threads. Order = value / cost. Keep every item differential-tested on both back ends.
1. DONE Source-level debugging: `-g` (decisions/013).
2. Faster native builds: conformance suite spends most time in cc -O2 (86 s vs ~45 s at -O0,
   runtime ~2.2k lines recompiled per program, ~0.25 s each). Ideas: Debug mode → -O1/-O0
   by default (perf of debug builds vs turnaround), precompiled runtime object/PCH cached
   by hash in target dir, parallel test builds.
3. Toolchain/cross: `CC` with arguments (`zig cc -target …`, `clang --target=…`) — today
   Command::new($CC) fails on spaces; honor CFLAGS/LDFLAGS; layouts use Target::host() so a
   cross target needs Target from flags; wasm32-wasi without fibers.
4. DONE s6 differently (measured flag = wash; static test removal instead). Exit flag (Appendix D.2): calls test the TLS `gm_unw.kind` after every effectful call and
   every frame keeps locals in struct F; return the flag (or a bool) instead and keep
   uncaptured locals as C locals (needs escape info: closures/handler clauses via env).
5. DONE s6 (nested SpecKey, transitive calls, clause_may_exit). Handler specialization beyond one level (decisions/012 "Deferred"): adapters/recursive
   iterators; skip gm_unw check after clauses that cannot exit.
6. DONE s6 (gm_out, expr_into, borrow.rs). Remaining big-value copies (bugs-open #7): call result temp → local, aggregate temps.
7. Debug views for tuples/tagged unions/slices (gdb pretty printers or typed mirrors).

Not done from 4: uncaptured locals as C locals (would let gcc keep them in registers when F
escapes via with nodes/closures). Needs: Local refs via FnGen::local_addr (exists), keep in F
anything referenced by Upvar, by a `with … on` body (outlined fn reaches F via gm_Fp) or
AddrOf; all in F with -g. Expected gain modest (benches: tree walk loop index/slice reloads).
