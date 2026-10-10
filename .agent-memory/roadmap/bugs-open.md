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
    ~180 ns/iteration. Fiber switches: s4 recv_soon (poll before blocking) 12 µs → ~2 µs.
12. Float printing at exact ties picks the larger candidate (…945.3 vs Ryu …945.2); allowed by
    §21.6.1 (shortest round-trip) but differs from Ryu/Python.
## Dogfood round 4 (s4) leftovers (13–22 fixed in s4)
23. Wishes (let-else done s4: StmtKind::LetElse lowered in block_expected; lexer line_binds); bind a whole
    tuple payload (`.Cat(c)` for a 2-tuple payload); interp `new` ~10 µs each (Alloc perform
    + vtable + AllocError!^T); block-bodied closure return inference; `&rvalue` for ^const
    params; i.to_f64-style helper; std channels/queue/generator; print from passing tests.
24. Interp speed: perform ≈ 0.7 µs vs call 0.1 µs, new+destroy ≈ 4 µs (s4 callgrind:
    spread over eval_small/call_function/frames; no single hotspot). Real fix = bytecode VM
    (future project); cheap bits: perform clones CtxNode, args Vec, ctl_ops SipHash lookup.
