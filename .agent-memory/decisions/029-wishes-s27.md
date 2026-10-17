# 029 Leftover language/library wishes (s27): what was admitted, what was rejected

Sources: todo.md "Nice", bugs-open #23 + dogfood papercuts, spec C.9.

## Admitted
- `[E]T` enum-indexed arrays (language; §5.6). Index by an enum value, built by naming every
  variant or `fill:`; positions = declaration order (@variants). Done by me (sema lowering:
  backends see a plain array). Reason: C's `names[COLOR_COUNT]` tables, made P4-safe
  (no positional mismatch, no bounds check, exhaustive construction).
- `Proc` ambient effect + `std/process` (§12.9, §21.7; reason 4 platform boundary + reason 1).
  Own pipe ops (`write_in`, `read_out` polling both streams, `close_in`) so the effect is
  replaceable as a whole (Child pipes are not Fs Files). Test handler: spawn → PermissionDenied.
- `std/time/zone`: TZif (RFC 8536) + POSIX TZ footer; reason 2 (calendar arithmetic).
- `std/fiber` sleep: `Sched.sleep_raw(deadline) -> bool idle`; the scheduler never reads the
  clock (handler needs only E); the sleeping task performs Clock itself, blocking the thread
  only when told idle. Sleepers checked once per round so busy tasks don't starve them.
- `std/fiber/generator`: pull-style generators (§13.4 anticipated `Generator(T)`); consumer
  runs inside `pull(stack, body)`'s `with … on`, producers are `@fiber`s from `^fn(arg)` (+arg).

- SIMD `#simd [N]T` (user asked to reconsider; first rejected, then ADMITTED): an array
  kind (TyKind::Array kind: ArrayKind::Simd), so storage/index/print reuse arrays. Layout
  N·size, align = size (fixed, P1 ok); not C-compatible. Every operator lane-wise (`is`
  and `<` give `#simd [N]bool` masks; vectors are not Eq/Ord); no int `/ %` (no SIMD
  instruction, P2); methods reduce_sum/min/max, all/any/count, select, min/max/abs,
  shuffle, store; `V.load(slice)`. Not a second way: arrays can't express masks/select/
  shuffles. C: vector extensions for unchecked ops, lane loops for checked ones.

## Rejected (spec C.8 rows added)
- `@fields(T)` reflection: without field types it drives no code; with them it needs
  type-valued loops = a compile-time sublanguage (§17 has none). Jobs covered structurally
  (is/@hash/@write) or by explicit code.
- Packed enum bit sets: `[E]bool`, `#bits` structs and std/collections/bitset cover the jobs.
- `Send`-like rule for migrating fibers: values own nothing (P1), so no type rule can tell
  whether memory a fiber reaches is shared; a fiber's handlers live on its thread. Threads
  exchange data explicitly (std/thread). Spec §14.9 unchanged; C.9 entry removed.
- TLS, compression: §22.4 (packages). Struct-typed errors `!.Variant`: by design (no variants).
