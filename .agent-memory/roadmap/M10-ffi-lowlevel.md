# M10 — Interop and low-level (§19, §20)
foreign blocks, variadics, #link_name, #callconv, #export, #global/#thread_local, C-compatible type
checks, std/c aliases, atomics, fences, volatile, @asm, threads (std/thread), remaining intrinsics.

## Status (design: decisions/009)
Done: Sys effect, #global (+thread_local), foreign blocks (variadic, #link_name, trusted effect
sets, foreign #global), C-compat checks, #export (C wrappers), std/c, plain C calling convention
+ tagged widening in cgen, auto C backend in `gamma run`, #bits/#c_union in both backends,
cstr_len, atomics (+MemoryOrder in prelude), fence, volatile.
Not yet: structs by value across C ABI, #callconv, @asm, threads (std/thread), const relocs of
plain fns stored at effectful fnptr types (would need tagging), errno-style TLS foreign globals.
