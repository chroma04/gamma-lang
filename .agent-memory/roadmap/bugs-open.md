# Open implementation bugs (dogfooding, session 2). Delete entries when fixed.
7. (perf) C: big by-value aggregates still use several copies per frame (args, GM_LD, GM_CONV);
   overflow now traps (SIGSEGV altstack handler + temp bytes in check). Measured s3: main with
   two 4 KB locals passed by value = 53 KB stack. Statements are now C blocks (temps of one
   statement reuse slots); the rest is per-call: tmp load + GM_CONV union + ABI copy. Ideas:
   -fno-strict-aliasing + pointer punning for mirrors; pass big args by pointer (D.5).
10. Usability: std/str + HashMap.get_ptr/get_or_put done; templates from comptime params done
    (WriteDeferred resolved in mono). Remaining: integer sign/abs helpers; print width specs
    (spec says library functions; Builder.write + fmt cover it).
