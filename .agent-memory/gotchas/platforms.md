# Platforms and C compilers: pitfalls

## gcc vs clang (macOS CI uses Apple clang; local `cc` is gcc 13, clang 18 installed)
- Since s79 Checked/Fast builds use clang when on PATH (Debug: cc). aarch64 clang needs
  compiler-rt symbols (__muloti4 for i128 mul-overflow): the runtime defines a weak one.
  Check other targets with `clang --target=aarch64-linux-gnu -O2 -c x.c; llvm-nm x.o`.
- Windows-only code: `when target.os is .Windows { return x }` followed by more code is
  "unreachable code" on Windows (cross-build CI job): use `when … { } else { }`.
- Run the suites with `CC=clang` before pushing back-end changes. clang rejects things gcc
  accepts: `(T){0}` for a zero-member struct (use `(T){}`); numeric/unknown escapes in
  `_Static_assert` messages (cgen's `assert_message` handles it).
- Newer clang: `apt-get install clang-19 clang-20`. Clang ≥19 (Apple ≥17) is ~cubic on long
  op chains: functions with > 2,000 names get `optnone,noinline` (GM_LONG_FN).
- C compilers are superlinear in the size of ONE function: keep generated functions small
  (lirc.gm segments long LIR lists into C blocks and, in long functions, helpers; 075).
- macOS: `qsort_r` argument order and qsort comparison order differ from glibc; tests must
  not depend on either. gcc may CSE pure recursive calls (fib is no benchmark).
- gcc `-fstack-clash-protection` probes fault before GM_STACK_CHECK → SIGSEGV handler.

## AArch64 (`apt-get install gcc-aarch64-linux-gnu qemu-user`;
## `GAMMA_TEST_LANE=aarch64 target/bootstrap/harness -j 4 lanes/`, ~1 min)
- AArch64 GCC without a stack protector keeps the frame record at the BOTTOM of the frame,
  so `__builtin_frame_address(0)` is not above the locals; GM_STACK_CHECK picks its form by
  `#if`. Run this lane for anything touching frames or C flags.
- Debug under qemu: `qemu-aarch64 -g 12345 exe & gdb-multiarch -ex "target remote :12345"`.

## s390x (big-endian): `apt-get update && apt-get install -y gcc-s390x-linux-gnu qemu-user`,
`GAMMA_TEST_LANE=s390x target/bootstrap/harness -j 4 lanes/` (~1 min).

## Windows
- Windows CI time (s75 probe, PR #131 run 44835273320): Defender real-time protection is
  OFF on windows-latest; one `gamma build hello` 0.8 s warm / 2.9 s cold; tests ran 4-way
  parallel (5262 s test time / 1316 s wall) but each ~5x slower than alone, worst the
  `gamma test` files (50-78 s vs ~2.5 s on Linux: fresh exe each, split into up to 4 C
  units). Cause found: gm_rt.h included <windows.h>+Winsock+bcrypt in every program unit
  on Windows (GM_RT_PROGRAM light path excluded _WIN32). Fixed in s75: Windows test step
  22 min -> 2 min 36 s (run 44839104755). The job keeps the timing step and `--slowest 25`.
- Windows-only code (`when target.os is .Windows`) is not checked on Linux: run
  `CC=clang gamma check --target=x86_64-w64-mingw32 f.gm` (no mingw needed).
  `when … { return a }` then `return b` is "unreachable code" on Windows only: use `else`.
- Local run: `apt-get update; apt-get install gcc-mingw-w64-x86-64 wine64`;
  `mkdir -p /run/user/0` FIRST (no XDG runtime dir here: wine aborts with
  `free(): invalid pointer`, leaving a stray `$WINEPREFIX/wineserver` file: rm the prefix);
  `gamma build compiler --target=x86_64-w64-mingw32 -o x.exe`;
  `WINEDEBUG=-all wine x.exe run --backend=interp f.gm`. Harness under wine: wrapper script
  `exec wine harness.exe "$@"`, `GAMMA_TEST_C=0 GAMMA_TEST_COMPILER='Z:\...\gamma.exe'`
  (s84: conformance 815 pass; the 55 failures all "cannot run the C compiler").
- No C compiler inside wine: Windows zig.exe runs but its cache fails (`manifest_create
  Unexpected`), so the Windows C back end is CI-only.
- ARM64 Windows (s84): cross-build with `CC=tools/zig-cc-windows-arm64.sh` (pip ziglang
  0.17.1; it makes libsynchronization.a with `zig dlltool`, which zig's MinGW lacks).
  GitHub release downloads (llvm-mingw) are blocked from this box; PyPI works. ARM64 PE
  cannot run here (wine is x86-only): CI job `windows-arm64` (windows-11-arm, MSYS2
  CLANGARM64) is the only test. Runtime: gm_veh reads `ContextRecord->Sp`; fiber switch
  `__aarch64__ && _WIN32` saves TEB StackBase/Limit/DeallocationStack via x18.
- Windows cannot rename over a file another process has open: cache writes treat
  "rename failed but destination exists" as success (content-keyed). Never open a cached
  executable for writing (parallel cases run it; macOS gave SIGKILL).

## WASI: `apt-get install clang-18 lld-18 wasi-libc libclang-rt-18-dev-wasm32 binaryen`;
wasmtime from its GitHub release tarball. It needs `--dir=.` and
`-W max-wasm-stack=268435456`; several preopens incl. "/" misbehave (harness uses a
relative TMPDIR). gamma links WASI unoptimized (clang would run wasm-opt itself).

## Fil-C (Safe mode)
- Install: release tarball `optfil-0.686-linux-x86_64.tar.xz` from github.com/pizlonator/
  fil-c; `tar xf` the inner `fil.tar.xz -C /opt` (that is all `setup.sh -u` does; the
  classifier may refuse setup.sh). Panics exit 134. valgrind works on its binaries.
- Never put /opt/fil/bin first on PATH (gcc then uses Fil-C's ld); the Safe lane finds
  `/opt/fil/bin/filcc` itself (`GAMMA_TEST_SAFE=1`).
- It rejects top-level asm and pointer operands in inline asm; casts via integers lose the
  capability; at -O2 SROA turns a word read both as int and pointer into inttoptr (why
  blobs are typed per word, decisions/032). Test Safe traps at -O2: filcc drops accesses
  it can prove fail. filcc is superlinear in function size.
- wasm32: frames and locals are only 4-aligned; a test casting a local `[N]i32` to
  `[^]u64` traps there by stack-layout luck (pointer_arith.gm, s83).
- clang lowers `__builtin_mul_overflow` on signed `__int128` to compiler-rt `__muloti4`,
  missing from libgcc (aarch64 bootstrap link failed, s83): cgen uses gm_mul_ovf_i128.
