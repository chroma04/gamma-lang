# 031 Statement-run outlining (s29, bugs-open #48)

- cgen/outline.rs. Trigger: fn body > LONG_BODY=1500 TIR expr nodes (outline::long_body,
  also forces frame_locals all-in-frame). Blocks with > RUN_WEIGHT=400 nodes: greedy runs
  of statements ≤400 nodes → `static GM_NOINLINE int gm_c{tag}_{k}(struct gm_frN *gm_Fp,
  env, cx, inst[, gm_out][, borrowed aI][, gm_bt *gm_Bp][, gm_node *gm_hcx])`, body under
  `#define F (*gm_Fp)` (+ `B`). Runs < 50 nodes stay inline; a stmt > 400 stays inline
  (its inner blocks split).
- Exits: FnGen::jump(label, scope index) is the ONLY emitter of gotos that can leave a
  statement (ladder, local_exit, gm_fdone). Label outside the innermost run (scope index <
  run.depth, or None = function label) → `return k`; caller `switch` re-jumps via jump()
  (nests). New goto sites that can leave a statement must use jump().
- runs stack set aside (without_runs) in detached (with-on bodies, @fiber starts) and
  defer bodies; splitting disabled while checking a Defer stmt (output discarded).
- self.cx != "cx" (inside `with`) → passed as gm_hcx so spec checks (`cx == "cx"`) stay off.
- Disabled with -g (debuggers need `F` as a real variable). GM_LONG_FN/HUGE now per C
  function (FnGen.names); inline decision still on the total counter.
- Ordinary programs (examples/calc etc.) produce no helpers. Not tested under Fil-C locally
  (no /opt/fil); tests/long_functions.rs isn't in the Safe CI job.
