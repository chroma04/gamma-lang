# 073 — Shared bodies for `@write` template parameters (s78)

User ask: settle the "template code size" pain point; chose implementation-only sharing
(option 2) over a language change (checked template values) after s77's measurement.

## Design (check/share.gm; spec unchanged)
- `template_params(s, f)` (memo `Sema.template_params`): value params `g` of generic `f`
  written as a WriteDeferred template at least once in f's own body and otherwise only as
  run-time values there. Disqualifies: any use in a nested fn body, a call/FnRef/Closure of
  `f` itself, an instance whose args mention `g` (test: `s.subst(arg, [g→marker]) is not
  arg`), `g` inside `@static_assert` args. Types mentioning g → via instance args.
- produce_instance → `share_instance` if any tps: key (f, args with tps → `template_marker`
  = `.Value(.Str("{template}"))`) in `Sema.shared_writes`; `make_shared` creates the fn
  (`sema.add_shared_fn`, sig.instance = (f, erased) so cgen fn_key differs) with two hidden
  params per tp: table (rawptr) and text (string); mono with `MonoCx.erased` (ValueParam →
  Local(text); WriteDeferred template → Local(table) BEFORE children) and `MonoCx.sites`
  (resolve_write records (param, args ty) and builds `Write{table}` via `write_table`).
  fn_map gets (f → shared) or nested closures keep the generic as parent (C error: frame
  struct of an unemitted fn).
- Each instance keeps its id and signature; its body = Call(shared, params…, Template(t),
  Str(text) per tp). Templates checked per request against every site (check_template, same
  messages as resolve_write) → identical diagnostics (tests check/template_param_errors,
  template_shared_errors).
- TIR: `ExprKind.Template(^TemplateTable{pieces, specs, index})` (rawptr); `WriteExpr.table`
  (args = tuple elements once). for_each_child/clone/dump handle them.
- cgen: `Gen.template_table` → static `gm_tmpl` (runtime type in gm_rt.h; fields n, text,
  arg, fmt — words the runtime already uses: check_export_names rejects exports named like
  any runtime identifier; `index` broke conformance/run/export_libc_names). `write_table`
  mirrors `write`'s three paths (fn sink stubs + direct literal call; perform piece stubs;
  generic gm_buf), looping over the table.
- interp: `Machine.templates` (lowering Template → Word(1-based index)); `write_table` in
  calls.gm.

## Measured (Ir G unless noted; baseline = main after #133 built by the old compiler)
- Compiler emit-c C 54.20 → 47.26 MB (−12.8%); cold `gamma build compiler` 57.4 → 52.6 s.
- Debug compiler: check 2.351 → 2.355, fmt 1.583 → 1.586, emit-c 5.203 → 5.356 (+2.9%:
  linef/format loop vs unrolled; first version via gm_format buffers was +21%, then fn
  stubs +5%, then direct literal sink +2.9%). Binary 9.24 → 9.32 MB (thunks; -Og).
- Fast compiler: check/fmt same, emit-c 4.319 → 4.487 (+3.9%), binary 8.97 → 8.57 MB.
- User program (400k str.format, scratch fbench.gm): Debug 2.177 → 2.046, Fast 1.264 →
  1.150 (faster: the shared body path).
- format instances: 1,422 → 1,430 thunks + ~120 shared bodies; function text 48.7 → 42.2 MB.

## Gotcha
- Changing gm_rt.h struct fields that cgen emits: rebuild target/bootstrap/gamma from a
  compiler WITHOUT the old field names (an intermediate compiler emits the old ones and
  tools/test.sh's rebuild fails). Seed is unaffected (it never emits gm_tmpl).

## Not done
- Forwarding (kit.check → str.format; linef → str.format): the forwarder is not eligible
  (instance arg mentions g), so it keeps an instance per template; its callee shares.
- Thunk frames carry the generic's other locals (unused).
