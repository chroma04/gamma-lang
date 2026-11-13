# 065 — Incremental native builds: stable C names, units by owner, unit object cache (s68)

User asked "what would it take to have incremental compilation", then "go ahead with level 1"
(level 1 = cache C-compiler output per unit; level 2 = front-end per-module caching, NOT
doing: demand-driven sema + comptime eval + per-site generic/handler specialization make
module interfaces non-local; front end is ~2 s of a ~60 s compiler build anyway).

## Plan (status in "Progress")
A. Stable C names (cgen): no global counters in names that cross functions.
   - fn: `gm_f_<sanitized name ≤20>_<hash>` from key module-dir/name + instance args (show)
     + parent key/relative span for literals/clauses/nested. Spec clones: fn name + `_s` +
     hash(handler key, effect text, outer spec key). Frames `gm_fr` + fn suffix; fg.tag too.
   - content-named: strings gm_s (bytes), typed structs gm_t (shape), mirrors gm_cs (def
     text), descriptors gm_ty (def text; acyclic: pointers carry no pointee descriptor),
     site consts gm_k (bytes), stubs gm_pf (signature).
   - keyed: helpers (kind + type show), handler tables (clause fn keys), decl tables
     (effect + for_ty show), globals (module + name), const cells (module + const name +
     type; anonymous cells: referrer + offset), gm_src (path), foreign gm_x (fn key),
     effect ids (u32 hash of effect text, nonzero, collision-checked).
   - Namer: every entity asks ONCE (callers memoize per entity); same key twice → `_2`…
     suffix by order (rare: show() ambiguity across modules). Correctness never depends on
     stability: the cache key is the whole unit text.
B. Units by owner (units.gm): each function's owner = source file of (template of) the fn;
   no source → misc unit; data + tail in unit 0. Per-unit header = only the items its
   bodies reference (transitive), canonical order (types by (depth, name), decls by name).
C. Driver: content-addressed unit objects in pkg.cache_dir (reuse rtcache.runtime_object
   with prefix "unit"), prune old unit objects; emit-c writes the same units.
D. Measure: edit one compiler file → count recompiled units / wall; docs (architecture,
   contributing), CHANGELOG, tests (units.gm unit tests, harness case for cache hits).

## Progress
- A done (commit d8b2b12; full suite green). Names: gm_f_<name24>_<hash7>, foreign gm_x_…,
  spec `<fn>_s<hash6>`, frames gm_fr<tag>; cgen/names.gm (Namer, fn_key, …).
- B+C done in working tree: units.gm `split_by_file` (Decl items, gm_names, declared_name,
  closure + canonical sort; unit 0 = once+data defs+inline externs+tail), Parts gains
  support/types/frames/members/owned; driver compile_c uses it when n>1 (non-WASI) with
  rtcache `unit_objects` (cache `unit-<key>.{c,o}`, KEPT_UNITS 4000, touch if >1 day) and
  a thread pool `compile_objects(…, jobs)`. Without a cache the by-file units are written
  next to the exe (prog.c, prog.1.c, …) and compiled uncached. Old n-way
  `split`/`split_shared` kept only for `--emit-c` with GAMMA_C_UNITS (seed format).
- Tests: units.gm unit tests (split_by_file, order independence, declared_name);
  harness native_build/split_builds_compile_only_the_units_an_edit_changes;
  build_profiles package_objects now ignores `unit-*.o`. Full suite 63 s (was 271 s:
  selfhost stage-2 build hits the cache). Seed bootstrap verified.
- BUG found: units.gm `function()` rejected headers containing substring "static" →
  readable names (gm_f_static_bytes…) made EVERY split fall back to 1 unit. Fixed: has_word.
- Measured (4 cpus, compiler, debug): cold 41 s (was 55), no-op 3.9 s, body edit 4.2 s
  (1 unit), new fn + string 6.1 s (2 units: file + unit 0 = 29k lines of data).

- s68 later: inline fns' extern definitions moved from unit 0 to their owner's unit (unit 0
  had all 2856 #inline instances: 9.2 MB, 23 s at -O2). Instances of generic fns go to
  one of 16 groups per template file by hash of key (`owner_of`, `Owner{file,group}`;
  owned path `file#k`); nested fns go with their outermost parent. NOTE `gamma build
  compiler` is a CHECKED build (compiler/gamma.toml mode). Checked numbers (4 cpus): cold
  42 s (was ~62), no-op 4.2, body edit 4.3 (1 unit), new fn+string 6.0 (2), new List
  instance 5.2 (5 units). 370 units, largest 1.3 MB.
- Flake seen once at -j 4: conformance/run/std_process "core module `complex` does not
  declare" (known memory-pressure flake, gotchas/environment.md); passes alone.

- CI difftest failure (PR #122): concurrent builds sharing one cache (difftest: 200
  programs × gcc,clang > KEPT_UNITS) — prune deleted objects another build had just found
  → "clang: no such file …unit-….o". Fix: `units_to_prune` never removes objects used in
  the last day (hits older than a day are touched). Verified seeds 3200..3400 locally.

## Not done / ideas
- Unit 0 (all data) recompiles on every new string literal (~2 s): split data by owner.
- Cold build is CPU-bound (370 cc processes); merging tiny units would cut process
  overhead but costs stability. Not measured.
- -g: types+views go to every unit (views name no gm_ deps), so -g builds prune less.
- WASI keeps the old path (asyncify links objects itself).
