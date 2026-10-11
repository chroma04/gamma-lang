# 014 Native build speed: cached runtime object for Debug builds (s6)
- Measured (4 cores, gcc 13): conformance 90 s → 44 s (clang 51 s); interp-only part ~10 s.
  Per program, the runtime's reachable code was ~60% of cc time.
- Debug builds: generated TU = `#define GM_RT_SEPARATE 1` + gm_rt.h + generated code, at -O1
  (-O0 with -g), linked with `gm_rt-<hash>.o` from the cache (src/cgen/rtcache.rs). Runtime
  object always -O2 (+ -g with -g). Key = hash(pkg version, runtime source, $CC string,
  `cc --version` stdout, flags). Atomic tmp+rename; the .c stays next to the .o for debuggers.
- Checked/Fast: single TU at -O2 as before (whole-program inlining of runtime helpers).
- No cache (GAMMA_CACHE_DIR="" or no HOME) or runtime compile failure → silent fallback to
  single TU. tests/native_build.rs asserts the separate path is really taken (so a broken
  GM_API linkage can't hide behind the fallback); tests/runtime.rs compiles separately -Werror.
- -O0 vs -O1 for Debug: suite 32 s vs 44 s, but -O0 programs ran 2.5–4x slower (primes,
  sorting); -O1 ≈ -O2 at run time. Chose -O1.
- Runtime linkage: GM_API (static | external) on functions + var definitions in gm_rt.c;
  GM_API_VAR (static | extern) for var declarations in gm_rt.h; generated code defines
  `GM_API int gm_checks`. New runtime functions called by generated code must be GM_API and
  declared in gm_rt.h (helpers private to gm_rt.c stay `static`).
- Tests already ran in parallel (thread pool in tests/conformance.rs); no change needed.
- When CFLAGS support lands (plan item 3), those flags must go into rt_flags() (cache key).
- Not done: PCH for gm_rt.h + system headers (~50 ms/program parse), gc-sections for the
  bigger Debug binaries.
