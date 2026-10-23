# 041 Post-0.1.0 stdlib batch 1 (s36): candidates vetted against §22, user approved

User asked for a §22-checked candidate list, then approved "the first batch" (items 1–12),
with item 12 as builtin methods (not a `std/slice` module).

## Admitted (batch 1)
1. fs.remove_all — reason 2/4 (lstat, never follows symlinks; vanished entries ok; missing root = NotFound).
2. fs.temp_dir / create_temp / make_temp_dir — exclusive create + Rand names, path into caller buf.
3. net IP types (Ipv4/Ipv6/IpAddr, RFC 4291 parse, RFC 5952 format) — reasons 2+3.
4. std/uuid (RFC 9562; v4 Rand, v7 Clock+Rand; no monotonic counter: no global state).
5–6. math asinh/acosh/atanh, erf/erfc, lgamma/tgamma, frexp/ldexp/nextafter (C99).
7. complex tan, sinh/cosh/tanh, asin/acos/atan, asinh/acosh/atanh (Annex G).
8. math int helpers gcd/lcm/isqrt/ilog2/ilog10/div_floor/mod_floor (generic, checked like `*`).
9. time.parse_duration (inverse of format_duration, no floats).
10. time ISO week/ordinal dates.
11. text UTF-16 (utf16_len, encode_utf16, append_utf16, decode_utf16 iterator; no
    decode-to-UTF-8 helper: that is decode_utf16 + append_rune, §22.2 req 3).
12. Slice builtin methods in core/slice.gm (+ names in sema/methods.rs SLICE_FNS):
    upper_bound (binary_search's `at` is already the lower bound — no lower_bound),
    is_sorted(_by), reverse, rotate_left/right, dedup -> usize (swaps, so the tail keeps
    the removed elements: callers may need to free them), partition(pred) -> usize,
    select_nth(_by) (introselect; heap_sort fallback). Private quicksort partition renamed
    pivot_partition. Spec §21.3 updated.

## Deferred (tier B, need evidence or a design call)
case folding, NFC/NFD, fnmatch/glob, LEB128 in std/bytes, compensated sum + Welford stats,
random.sample, thread.Once/RwLock, fiber channel select/timed recv, bigint, semver (parse +
precedence only), uri (RFC 3986). Need a core change first: UDP/DNS in Net, signals,
is_terminal, float expect_near.

## Rejected
CSV (dialects), regex, ANSI colour/progress, iterator adapters (second way + no storable
closures), HashSet (alias of HashMap(K, ())), LRU/thread pool/slot map (policy), FNV/xxHash,
MD5/SHA-1 (until concrete demand; SHA-512 acceptable), human byte sizes, XDG dirs.
