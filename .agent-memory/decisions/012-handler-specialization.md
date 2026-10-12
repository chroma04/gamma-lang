# 012 Static handler specialization (D.3) + local exits in the C back end (session 3)
## Local exits (cgen/func.rs `local_exit`)
- start_exit with frame "(&F.h)" and !outlined: walk self.scopes to the target (Loop/With by
  label; RETURN = function level), run crossed defers inline (errdefers only for RETURN with
  is_err, guarded by the is_err temp), `goto` target. No gm_unw writes ⇒ &F not taken ⇒ gcc
  SRA puts locals in registers (fib went 6x faster). Dynamic ladder unchanged for calls.
## Specialization (cgen `Spec {handler, effect}`)
- with_expr: `specializable()` = exactly one HandlerValue::Literal, no `on`, body (through
  empty blocks) is Call{Callee::Fn(f)} with f Named, has body, effects contain the handler's
  EffectRef. Sets FnGen.spec_call = (ptr to call expr, Spec); call_fn uses Gen::queue_spec
  name `gm_f{f}_h{hid}` when std::ptr::eq.
- Clone = gen_fn(g, f, Some(spec)); frame struct emitted once (Gen.frames_done); FnGen.tag =
  "{f}_h{hid}" for helper names (gm_wb/gm_pr/gm_pt/gm_fs). Gen.drain() pops fns then clones.
- perform → direct_perform: spec matches effect id, self.cx == "cx", !outlined, not ctl,
  clause param/ret types equal ⇒ `gm_fC(cx->env, cx->hctx, cx, args)` + check_unwind.
- Only single-level: callees of the clone are not specialized (they gm_find). Possible next:
  skip the gm_unw check after clauses that cannot exit; transitive specialization.
- Enabled in all modes (so debug-mode conformance tests exercise it).
- Bench (scratch, fast mode): iterator 200M yields 0.52s → 0.075s; matmul 300³×5 = C speed.
## s6: nested specialization (implemented; see 015 for details)
- Spec is an interned id (Gen::spec(SpecKey{handler, effect, outer})); clones gm_fN_sK.
- Old notes below kept for the reasoning.
## (was) Deferred: 2-level (adapters `for v in inner() { yield(g(v)) }`, recursive walks)
- Idea: in a clone with spec S, a `with` of a HandlerRef evaluated at clone top level whose
  clauses perform S.effect gets a table variant whose clauses are clause clones with spec S
  (their `cx` = hctx = the clone's cx = S's node).
- Catch: direct_perform must call exactly the clause version in the node's table; callee
  clones would need the table variant in their Spec key, and recursive iterators then mint
  unbounded variants unless depth is capped (e.g. 2). Gain ≈ 2–3 ns/element. Not done.
