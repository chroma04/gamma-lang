# M11 — Standard library
std/io, std/fs, std/net, std/fmt (hex, padding), std/math, std/time, std/text (graphemes),
std/thread, std/c; real Fs/Net/Clock/Rand default handlers; arena + fixed-buffer allocators.

## Status
- Done: In/Clock/Rand + IoError/Instant/Duration in prelude; default handlers (SystemIo via
  SysOp intrinsics, both back ends) installed by sema entry_point; test handlers (TestIo in
  run_test: empty input, fake clock, splitmix64 seed 0). Harness `.stdin` files.
- Pattern for new services: add tir::SysOp variant (signature()), interp/sys.rs, runtime
  gm_rt.c function + cgen/arith.rs Builtin::Sys arm, prelude handler clause.

- Done: rune alias, runes() (maximal-subpart U+FFFD), make_aligned; core/slice module
  (swap/fill/copy_from, sorts, search, utf8_valid) via sema/methods.rs slice_fn → core_fn.
  Efficient sorts + big test delegated (worktree agent); merge when it reports.
- Missing from §21.3: `cursor()` (§13.4), `downcast_unchecked`.

- Done: Fs effect (File/Dir/OpenOptions/FileInfo/SeekFrom) with SystemIo default handler over
  fs_* SysOps (interp: Rust std::fs + handle table in SysState; C: POSIX, dir table).
  Effect ops callable as methods (methods.rs op_method). Not yet in test_effects (needs MemFs).

## Next
3. In-memory test Fs (MemFs in prelude, add Fs to test_effects/run_test); then Net.
4. std/fmt, std/math, std/io (buffered reader/writer), std/time, arena allocator.
