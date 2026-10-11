# 014 C toolchain from the environment; target follows the compiler (session 6)
Context: c-backend-plan item 3. `Command::new($CC)` broke on `zig cc …`; layouts always host.
- src/toolchain.rs: Toolchain{cc: Vec (CC split shell-like), cflags, ldflags}. Command line:
  cc-args + back-end flags + CFLAGS (user overrides ours) + `-o exe file.c` + LDFLAGS + -l….
- Target is decided BEFORE sema (layouts, consteval, `target` module depend on it):
  Session.target ← Toolchain::target(): default toolchain (plain `cc`, no CFLAGS/LDFLAGS)
  = Target::host() with no process spawn; otherwise `$CC $CFLAGS -dM -E -x c -` and
  target_from_macros (ptr size, __BYTE_ORDER__ resolved through __ORDER_*__ names, arch/os
  macros, __int128 required, s390x int128_align 8). Sema stores it in Program.target;
  cgen and the interpreter RunConfig use prog.target.
- main.rs new_session(): check/run/test/build. Probe failure → host target, error kept and
  reported only if a native build happens (build fails up front).
- Unsupported in C back end: big-endian (runtime reads LE bytes everywhere: gm_fmt_int,
  wide ints, #bits, hash) → cgen returns Unsupported "big-endian targets"; no __int128
  (32-bit non-wasm) → probe error. wasm32 probes fine (interp/check) but runtime needs
  pthreads/fiber asm, untested.
- std/c c_char = u8 on Linux Aarch64/Riscv64.
- Verified: whole conformance + runtime suite with CC=aarch64-linux-gnu-gcc CFLAGS=-static
  under qemu-user binfmt (apt gcc-aarch64-linux-gnu qemu-user; register
  /usr/lib/binfmt.d/qemu-aarch64.conf into /proc/sys/fs/binfmt_misc/register).
- Not done: `--target=` CLI flag; big-endian runtime; per-target runner env var.
- `-fno-strict-aliasing` added to every native build: the aarch64 cross run crashed in
  es/store test at -O2 (gm list grow, typed struct copies over allocator memory); fine with
  -fno-strict-aliasing, -O1 or -fno-inline. Gamma memory is untyped, so TBAA is wrong anyway.
- Conformance `//! arch:` now compares with the toolchain's target arch (tests/conformance.rs
  target_arch), so x86-only tests skip in cross runs.
