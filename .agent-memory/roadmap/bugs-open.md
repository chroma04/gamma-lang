# Open implementation bugs (dogfooding, session 2). Delete entries when fixed.
7. (perf) C big values: s6 gm_out results + in-place construction + borrowed params of pure
   fns (decisions/016). Left: nested aggregate elements still go through a temp; gcc SRA is weak
   on uint8_t-array blobs (bigcopy bench: gcc 1.9 s vs clang 0.13 s).
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
    runnable from a root (`gamma test --all`?), @fn_cast.
## s7 leftovers
35. std/log ErrLog requires Clock even when time is false.
36. WaitGroup wait is `wait_all` (name clash in module); lstat on symlinks untested.
37. HashMap keeps get_or_put next to entry (second way; consider removing per §22).
38. Later (must clear §22): reflection, JSON, hashes, ordered maps.
## s8 (bug hunt) leftovers
(#39, #40 fixed: deferred match exhaustiveness via FnCx.deferred_matches; pending handler
 literals checked at first `with`, FnCx.pending_handlers + shielded_performs.)
## s13 (bug sweep) leftovers
41. §4.6: a local hiding a predeclared/top-level type name still resolves as the type in type
    position (`u8 := 3; x: u8 = 0` accepted). Sema::resolve_type knows no locals; needs a
    scoped hidden-name list (like type_params, saved/cleared in check_fn_body). Note
    type_params itself may leak into lazily resolved top-level decls (resolve_decl does not
    clear it) — check.
42. Local `K :: 4` unusable in type/comptime positions (`[K]u8`, `@static_assert`, `when`,
    value args): same root cause as 41 (module-level resolver).
43. `EvI :: Ev(i32)` effect-instantiation alias rejected ("cannot be called"); §17.5.
    Also `Yield(i32).yield(5)` qualified op on an instantiated effect.
44. Deep chains: `a.b.b.b…` check time superlinear (14 s at 40k); 1M-link call/index chains
    overflow the 1 GiB compiler stack.
45. C back end: 9k-line generated main with many constant checks takes >60 s (unprofiled).
46. Minor CLI: `gamma repl missing.gm` exits 0; `gamma test -j 0` accepted; `--mode=bogus`
    says "unknown option"; gamma fmt <dir> descends into hidden dirs/target.
47. `--locked` ignores hand-edited lock `version`/`checksum` until a checkout is needed.
