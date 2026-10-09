# M11 — Standard library
std/io, std/fs, std/net, std/fmt (hex, padding), std/math, std/time, std/text (graphemes),
std/thread, std/c; real Fs/Net/Clock/Rand default handlers; arena + fixed-buffer allocators.

## Status
- Core: In/Clock/Rand/Fs/Net ambient effects (+IoError incl. ConnectionRefused, Instant,
  Duration, File/Dir/OpenOptions/FileInfo/SeekFrom, Socket/Listener). Default handlers =
  `SystemIo` over SysOp intrinsics (interp/sys.rs; runtime gm_rt.c; cgen Builtin::Sys →
  gm_<name>(slices as ptr,len)). Test handlers on ^TestIo: empty In, fake clock, splitmix Rand,
  MemFs, MemNet (non-blocking: WouldBlock). Entry wraps main with heap_allocator()+system_io().
- Pattern for a new service: tir::SysOp (signature()), interp/sys.rs, gm_rt.c gm_<name>,
  prelude handler clause; keep error codes portable (append IoError variants if needed).
- rune/runes/make_aligned; core/slice (sorts by agent: introsort + SymMerge stable).
- std: io (Reader, FileIn/FileOut/StringIn/Capture), fs, fmt (agent; exact floats), arena,
  fixed_buffer, c. docs/std.md documents all of it — keep it updated.
- In flight: std/math (agent, pure-Gamma fdlibm ports) — merge after review.

## Next
- std/time (formatting durations/dates?), std/text (graphemes), std/thread (needs M10 threads).
- `cursor()` (§13.4).
