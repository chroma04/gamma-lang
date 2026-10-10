# Open implementation bugs (dogfooding, session 2). Delete entries when fixed.
7. (perf) C big values: s4 passes values >16 bytes by address between Gamma functions
   (Types::by_ref; plain fns get a C-ABI wrapper gm_fN_c via Gen::needs_wrapper/addr_name),
   place args passed as the place when later args are pure (no_writes), return of a place
   copies straight into F.ret. Bench: 57→29 KB (work+fill), callee 12→4 KB. Remaining: call
   result temp then copy into the local (t1 → F.l0); fill/aggregate construction temps.
10. Usability: std/str + HashMap.get_ptr/get_or_put done; templates from comptime params done
    (WriteDeferred resolved in mono). Remaining: integer sign/abs helpers; print width specs
    (spec says library functions; Builder.write + fmt cover it).
11. Interpreter speed: s3 fast paths (interp/fast.rs: ≤64-bit ints as i128, word loads/stores,
    inline small allocs, address cache, &TyInfo, frame pool) took a simple loop from 700 to
    ~180 ns/iteration. Fiber switches still ~34 µs (thread handoff per switch).
12. Float printing at exact ties picks the larger candidate (…945.3 vs Ryu …945.2); allowed by
    §21.6.1 (shortest round-trip) but differs from Ryu/Python.
## Dogfood round 4 (s4) findings, open unless struck
13. C: `for` over a user iterator inside a handler-DECLARATION clause → missing frame slot
    `gm_frN has no member lK` (or wrong output). Repro in example4_fx report: Say/Wrapper.
14. `with a, b`: a's handler must supply effects b's clauses need (§12.7) — currently error.
15. `return` in a handler-declaration clause accepted (§12.6 says no enclosing fn).
16. `x is null` rejected for ?T with non-Eq T (e.g. ?cont) — null test needs no Eq.
17. Elementwise array ops on `[N]T` with generic value param N rejected.
18. `is` on types (`i32 is i32`, `A is B` with type params) unsupported.
19. native_dir temp dir uses only pid → collisions; use unique dir. `gamma test | head` panics.
20. Tuple expressions don't push expected element types: `(a: 1, b: 2)` for (u8, ?u8), null.
21. `r else null` on a result: misleading "cannot infer null" diagnostic.
22. test body error type = first try's error (§3.8 unclear) → specify union?
23. Wishes: let-else for refutable patterns; bind whole tuple payload; interp `new` ~10µs;
    block-bodied closure return inference; `&rvalue` for ^const params; i.to_f64 helper.
