# C back end strengthening plan (from the Oct 9 LLVM evaluation; prioritized in session 5)
Status: 1, 7 done (s5), 2, 3, 4–6 done (s6; 4–6: decisions/016). Order = value / cost. Keep every item differential-tested on both back ends.
1. DONE Source-level debugging: `-g` (decisions/013).
2. DONE Faster native builds (decisions/014): cached runtime object + -O1 for Debug.
3. DONE (s6, decisions/015) Toolchain/cross: CC with args, CFLAGS/LDFLAGS, target probed from
   the compiler. `--target=` and big-endian done s10 (015). Left: wasm32-wasi (no fibers/threads).
4. DONE s6 differently (measured flag = wash; static test removal instead). Exit flag (Appendix D.2): calls test the TLS `gm_unw.kind` after every effectful call and
   every frame keeps locals in struct F; return the flag (or a bool) instead and keep
   uncaptured locals as C locals (needs escape info: closures/handler clauses via env).
5. DONE s6 (nested SpecKey, transitive calls, clause_may_exit). Handler specialization beyond one level (decisions/012 "Deferred"): adapters/recursive
   iterators; skip gm_unw check after clauses that cannot exit.
6. DONE s6 (gm_out, expr_into, borrow.rs). Remaining big-value copies (bugs-open #7): call result temp → local, aggregate temps.
7. DONE Debug views for tuples/tagged unions/slices (decisions/013, "Views").

Rest of 4 DONE (thread "Uncaptured locals", branch claude/project-thread-oemqrj):
uncaptured locals are C locals `gm_lN` (Gen::frame_locals, FnGen::local_lv). In F: Upvar
targets; locals > 32 bytes (MAX_C_LOCAL: GM_STACK_CHECK counts C locals in temp_bytes, so a
[20000]u8 task buffer above F gave a false fiber-stack overflow in std_fiber); all locals if
the fn has a `with … on`, in Debug (0xDD dead-frame fill) and -g. Stress-tested by forcing
the split in Debug too (full conformance).
AddrOf locals need not be in F (C local lives as long as F). Gain modest as predicted:
Safe trees 4.05→3.90 s, sieve 0.89→0.84, hmap 2.29→2.21; closure bench Checked −12%.
`ret`/`wN` stay in F (clauses reach them via frame_field).
