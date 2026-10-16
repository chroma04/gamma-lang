# s17 (2026-10-16): v0.1.0 prep (branch claude/wonderful-cerf-sfn0qf)
User gave an outdated to-do list (assessed at PR #34); I triaged it against main b587221
(most P1/P2 done in s12–s16) and proposed a prioritized pre-release list. User then asked
for P0 1–3 (main CI green — it was; re-run Release workflow by hand; fold CHANGELOG) and
the P1 bugs (#50–53).
- #50: `construction_call` treated `v.f(…)`/`v[i](…)` as constructions → `calls_on_value`
  (callee Field whose base is not an Ident path, or Index). hoist_prefix now walks
  OptUnwrap/ResUnwrap/Deref and stacked prefixes (`[2]?u8(1, null)[0]`).
- #51: `declare` rejects reserved type names (incl. `cont`, local aliases `f64 :: i32`)
  after the "names a type" check.
- #52: FnCx::settle (finalize.rs) = apply pending coercions mentioning the var, then
  InferTable::settle; used by @type_of, pointer intrinsics (@atomic_*), print args,
  `with &x`, expect_eq, literal-only generic args. Spec §6.1 lists these sites.
- #53: runtime locals are pushed to Sema.local_consts as Var → comptime use says "is a
  variable…" and they hide top-level names (`n :: 2` + `n := 3; [n]u8` used 2 before!).
  Pending handler literals now also rewind local_consts. Generic "inferred as both" stops
  the call (no extra mismatch). `--help`/`-h` after any command prints help
  (help_requested in main.rs; `run file --help` goes to the program); tests/cli.rs.
  The "spurious unknown name d" part no longer reproduced (rest-args rework).
- CHANGELOG: Unreleased folded into one `## 0.1.0 - 2026-10-16` summary (no prior release,
  so Breaking/Fixed lists were dropped; git history has them). release.yml: notes from the
  version's CHANGELOG section (awk), build job checks the section exists.
- Spec gaps settled (user asked): Appendix A uses ATTR/INTRINSIC tokens (`'#global'`),
  §2.7 has their productions; `{ attribute } '^' fn_type` in A.2 (#callconv); `_:` is a
  parse error (parse_arg) — also for #bits padding, which can't be named in construction
  (§5); break-label rule was already A.7 rule 11.
