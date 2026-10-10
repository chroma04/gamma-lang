# Open implementation bugs (dogfooding, session 2). Delete entries when fixed.
7. (perf) C: big by-value aggregates still use several copies per frame (args, GM_LD, GM_CONV);
   overflow now traps (SIGSEGV altstack handler + temp bytes in check). Measured s3: main with
   two 4 KB locals passed by value = 53 KB stack. Statements are now C blocks (temps of one
   statement reuse slots); the rest is per-call: tmp load + GM_CONV union + ABI copy. Ideas:
   -fno-strict-aliasing + pointer punning for mirrors; pass big args by pointer (D.5).
10. Usability: std/str + HashMap.get_ptr/get_or_put done; templates from comptime params done
    (WriteDeferred resolved in mono). Remaining: integer sign/abs helpers; print width specs
    (spec says library functions; Builder.write + fmt cover it).
11. Interpreter speed: s3 fast paths (interp/fast.rs: ≤64-bit ints as i128, word loads/stores,
    inline small allocs, address cache, &TyInfo, frame pool) took a simple loop from 700 to
    ~180 ns/iteration. Fiber switches still ~34 µs (thread handoff per switch).
12. Float printing at exact ties picks the larger candidate (…945.3 vs Ryu …945.2); allowed by
    §21.6.1 (shortest round-trip) but differs from Ryu/Python.
