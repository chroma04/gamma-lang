# M10 — Interop and low-level (§19, §20)
foreign blocks, variadics, #link_name, #callconv, #export, #global/#thread_local, C-compatible type
checks, std/c aliases, atomics, fences, volatile, @asm, threads (std/thread), remaining intrinsics.

## Status (design: decisions/009)
Done: Sys effect, #global (+thread_local), foreign blocks (variadic, #link_name, trusted effect
sets, foreign #global), C-compat checks, #export (C wrappers), std/c, plain C calling convention
+ tagged widening in cgen, auto C backend in `gamma run`, #bits/#c_union in both backends,
cstr_len, atomics (+MemoryOrder in prelude), fence, volatile.
Threads done: std/thread (spawn/join/yield_now/Mutex/defaults); SysOps thread_spawn/join/
yield/sys_write (std modules may use SysOp intrinsics: Module.is_std); C runtime TLS for
gm_unw/gm_cur_fiber/fiber pool + gm_rt_lock for shared tables, -pthread; interp
src/interp/threads.rs = OS threads + fair ticket GIL (tick() in call_function/eval_loop,
without_gil() around blocking sys ops; streams Arc'd), fatal trap stops all threads.
Structs by value across the C ABI done: mirror C types (decisions/009), test run/ffi_structs.
#thread_local is per thread in both back ends.
Not yet: #callconv, @asm, const relocs of
plain fns stored at effectful fnptr types (would need tagging), errno-style TLS foreign globals.
