# M5 — Generics, allocation, core containers
type params, comptime value params (§16.5), constraints (closed set §16.3), checked-once generic
bodies with opaque type params, inference (§16.2), generic types/unions/effects, distinctness
(§16.6), monomorphization; Alloc effect, Allocator vtable, new/make/free/destroy (§21.2), List and
HashMap in Gamma (`lib/core`), @hash (§16.8), sort/sort_by/... builtins.

## Status
Done: type params, constraints, inference, generic structs/unions, monomorphization, Alloc,
Allocator, heap allocator, new/make/make_fill/free/destroy, List(T) (push/pop/clear/deinit).
Remaining: comptime value params, distinctness §16.6, generic effects beyond Yield-style (check),
HashMap/@hash/map, make_aligned, sort builtins, runes iterator.
