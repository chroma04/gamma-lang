# M11 — Standard library
std/io, std/fs, std/net, std/fmt (hex, padding), std/math, std/time, std/text (graphemes),
std/thread, std/c; real Fs/Net/Clock/Rand default handlers; arena + fixed-buffer allocators.

## Status
- Done: In/Clock/Rand + IoError/Instant/Duration in prelude; default handlers (SystemIo via
  SysOp intrinsics, both back ends) installed by sema entry_point; test handlers (TestIo in
  run_test: empty input, fake clock, splitmix64 seed 0). Harness `.stdin` files.
- Pattern for new services: add tir::SysOp variant (signature()), interp/sys.rs, runtime
  gm_rt.c function + cgen/arith.rs Builtin::Sys arm, prelude handler clause.

## Next
1. rune type, runes(), utf8_valid(), make_aligned (core gaps).
2. Slice algorithms (§21.3: sort family, binary_search, index_of, starts/ends_with, min/max,
   cursor) — write in Gamma (core module) + method plumbing in sema/methods.rs.
3. Fs (open/read/write/close/seek/remove/rename/mkdir/stat) + in-memory test fs; then Net.
4. std/fmt, std/math, std/io (buffered reader/writer), std/time, arena allocator.
