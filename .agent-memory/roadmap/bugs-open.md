# Open implementation bugs (dogfooding, session 2). Delete entries when fixed.
7. (perf) C: big by-value aggregates still use several copies per frame (args, GM_LD, GM_CONV);
   overflow now traps (SIGSEGV altstack handler + temp bytes in check). gcc uses ~4x size.
10. Usability (std): no sign/abs, string split/trim/substring, string builder, HashMap entry/get_ptr,
    print width/padding specs. (for over array temporaries: done; slicing temporaries stays an error by §7.2.)
