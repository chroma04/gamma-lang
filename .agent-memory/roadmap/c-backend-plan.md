# C back end strengthening plan (from the Oct 9 LLVM evaluation; prioritized in session 5)
Status: 1, 7 done (s5), 2 done (s6), 4–6 done (s6, decisions/015). Order = value / cost. Keep every item differential-tested on both back ends.
1. DONE Source-level debugging: `-g` (decisions/013).
2. DONE Faster native builds (decisions/014): cached runtime object + -O1 for Debug.
3. Toolchain/cross: `CC` with arguments (`zig cc -target …`, `clang --target=…`) — today
   Command::new($CC) fails on spaces; honor CFLAGS/LDFLAGS; layouts use Target::host() so a
   cross target needs Target from flags; wasm32-wasi without fibers.
4. DONE s6 differently (measured flag = wash; static test removal instead). Exit flag (Appendix D.2): calls test the TLS `gm_unw.kind` after every effectful call and
   every frame keeps locals in struct F; return the flag (or a bool) instead and keep
   uncaptured locals as C locals (needs escape info: closures/handler clauses via env).
5. DONE s6 (nested SpecKey, transitive calls, clause_may_exit). Handler specialization beyond one level (decisions/012 "Deferred"): adapters/recursive
   iterators; skip gm_unw check after clauses that cannot exit.
6. DONE s6 (gm_out, expr_into, borrow.rs). Remaining big-value copies (bugs-open #7): call result temp → local, aggregate temps.
7. DONE Debug views for tuples/tagged unions/slices (decisions/013, "Views").

Not done from 4: uncaptured locals as C locals (would let gcc keep them in registers when F
escapes via with nodes/closures). Needs: Local refs via FnGen::local_addr (exists), keep in F
anything referenced by Upvar, by a `with … on` body (outlined fn reaches F via gm_Fp) or
AddrOf; all in F with -g. Expected gain modest (benches: tree walk loop index/slice reloads).
