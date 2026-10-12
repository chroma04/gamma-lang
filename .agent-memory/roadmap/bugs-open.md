# Open implementation bugs (dogfooding, session 2). Delete entries when fixed.
7. (perf) C big values: s6 gm_out results + in-place construction + borrowed params of pure
   fns (decisions/016). Left: nested aggregate elements still go through a temp; gcc SRA is weak
   on uint8_t-array blobs (bigcopy bench: gcc 1.9 s vs clang 0.13 s).
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
## Round 5 (s4) leftovers
27. (fixed s4: perform_op checks args against the single available instance)
28. `print("{}", bytes)` of []u8 prints raw bytes (by §21.6.1); no numeric display option.
29. Stack-overflow backtrace depth differs between back ends (frame sizes) — stderr goldens
    for fiber overflow can't be shared.
30. Wishes (@min_value/@max_value done s4, tir IntLimit folded in mono): generic fn → ^fn once instantiated (`parse_int(u8)`), library tests
    runnable from a root (`gamma test --all`?), x.to(f64) widening shorthand, @fn_cast.
