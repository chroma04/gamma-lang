# Generated C and the C runtime: pitfalls
- A constant is a place (`static gm_cN`): `&CONST` must not copy it.
- Defers run by a shared ladder must save/clear `gm_unw`.
- gammac keeps ALL locals and big temporaries of a function in one frame struct in Debug
  (GM_STACK_CHECK cost shows it): a big `match` function pays for every arm per call.
  Split hot recursive functions into a thin dispatcher + `#noinline` helpers.
- Debug builds memset heap blocks (0xAA): take huge lazily-committed buffers from libc
  `calloc` (interp `host_stack`, `ROOT_STACK`).
- The interpreter uses 10–13 KB of host C stack per Gamma call that recurses on the host
  (tree fallback, non-loop calls). The root coroutine stack (`ROOT_STACK`) is 4 GB
  calloc'ed lazily, halving to 1 GB if refused (1 GB on 32-bit, 8 MB on wasm) so MAX_DEPTH 200k
  fits; deep_recursion.gm failed on Windows with 1 GB.
- GCC 13 does not merge copies of blob structs (byte arrays read with memcpy defeat SRA):
  avoid temporaries that only copy (decisions/068, 069).
- A new intrinsic used by `lib/core` needs a two-stage build + seed refresh
  (docs/contributing.md "The bootstrap seed"). After cgen changes, refresh the seed.
