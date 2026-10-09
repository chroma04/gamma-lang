# Open implementation bugs (dogfooding, session 2). Delete entries when fixed.
7. C: big by-value aggregates → many temporaries in frame → stack overflow segfault (400KB struct).
8. Huge local array `[1e12]u8`: no compile error; interp aborts with Rust alloc failure.
9. Diagnostics: `==`/`!=` → hint `is`/`is not`; block after `else`/`=>` hint; failed import cascades.
10. Usability (std): no sign/abs, string split/trim/substring, string builder, HashMap entry/get_ptr,
    print width/padding specs; temporaries of arrays can't be sliced/iterated (`for x in [_]T(..)`).
