# 027 Effect-set params inferred from context; std/fiber/channel (s20)

## Inference rule (§16.6 amended)
Problem: a helper `fn(E: effects) needs Sched(E)` called from a task got E = {} (positional,
omitted → empty), so `send(&ch, x)` style APIs needed `send(Out + Clock, &ch, x)`.
Rule: an effect-set param occurring in the args of a (non-splice) effect of the fn's own
`needs` is *inferable* (named-only, never positional); after argument inference, an
unbound one is matched against the ONE instantiation of that effect `available()` at the
call (same as perform_op_inner for ops); else empty. Only effect-set params (type params in
`needs Yield(T)` unchanged — would break `gen(u8)` calls).
Code: sema/generics.rs `effect_args_name`, `inferable_param(sig, i, &effect_params)` (all
call sites; clones the HashSet where a closure needs it), loop before `failed_arg`.
effects.rs `available` is pub(super). Breaking: std/fiber `entry(E)` → `entry(E: E)`.
Tests: check/effect_set_param_from_context.gm.

## Channel design
- Sched gains `park: ctl(w: ^Waiters)`; `wake` is a PLAIN fn (no effect): a `defer`
  may not perform Sched (§8.8 is per effect, Sched has suspending ops), and `defer
  ch.close()` is the natural idiom. So `Waiters.add(k, &self.ready)` records the
  scheduler's ready deque in the Waiters at park time; `wake` pushes there.
- Modules: std/fiber/waiters (Waiters, waiters, deinit, is_empty, wake, add — `add` is
  pub only for the scheduler) re-exported by fiber (Waiters, waiters, wake), because
  `deinit` would clash with Scheduler's (one fn per name per module; same as
  thread/wait_group). std/fiber/channel imports std/fiber; API mirrors thread.Channel
  (send -> bool false if closed, recv -> ?T, try_send, try_recv, close, len, is_closed,
  deinit; capacity >= 1 asserted; buffer reserved up front so send never allocates).
- Deadlock (all parked) = scheduler's existing "deadlock" trap. Tests run/std_fiber_channel
  (+ _deadlock, exit 134; no stderr golden because lib paths are absolute).
- Not done: sleep-aware scheduler (needs Clock in handler set; open).
