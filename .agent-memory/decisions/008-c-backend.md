# 008 C backend design (M7)
Module `src/cgen/` (codegen) + runtime `runtime/gm_rt.h`, `runtime/gm_rt.c` (embedded via
include_str!, concatenated into ONE translation unit with the generated code). Build: write
`<tmp>/prog.c`, run `cc -std=gnu11 -O2 -w -fno-strict-aliasing prog.c -o out -lm` ($CC overrides). GNU C needed
(statement exprs, zero-size arrays, __int128, __builtin_*_overflow). Host target only.

## Values
- Scalars native: ints by storage (u8..u64, __int128 for 65..128; >128 unsupported for now),
  usize/isize = uintptr_t/intptr_t, f32/f64 (f16 = _Float16), bool = uint8_t, all data pointers
  and fn pointers = `void*` (cast at use).
- Everything else (slices, arrays, tuples, optionals, results, structs, unions, unit) is a blob
  `typedef struct { _Alignas(A) uint8_t b[S]; } gm_S_A;` (one typedef per size/align; unit =
  size 0). Layout = layout.rs exactly; fields read/written with memcpy at offsets
  (GM_LD/GM_ST macros). Same bytes as the interpreter ⇒ differential testing is meaningful.
- Second-class fn value = gm_closure {code, env, cx} (3p, matches layout). Handler value =
  {env, cx} (2p); the literal id is static from TyKind::Handler(id).

## Functions and frames
- Every Gamma fn → `static RET fN(void *env, gm_node *cx, [gm_node *inst if Clause], params…)`
  (s6: a >16 B result goes through a leading `void *gm_out` and RET is void; decisions/015).
  Uniform so fn pointers/closures can be called without knowing the target.
- Locals live in a frame struct `struct frN { gm_frame h; …locals; ret slot; with slots }`, a C
  local `F`; `h.up` = env (parent frame), `h.clause_lit` flag. Upvar{levels} walks h.up.
- Non-local control: thread-local `gm_unw {kind, frame, label, is_err, code}`. Break/Continue/
  Return/Try/Exit set it (value stored first into the target frame's slot or with-result slot)
  and `goto` the current site's cleanup ladder. After every call that may unwind (callee has
  effects, closure call, perform) `if (gm_unw.kind) goto ladder_site`. Ladder: run active defers
  innermost-out (errdefers guarded by gm_errdefer_runs(&F.h) mirroring interp errdefers_run),
  at each loop/with boundary test whether this frame+label is the target (clear, goto exit or
  continue point), at function level handle RETURN to this frame, else return (propagate).
  Traps never unwind (fflush stdout, print, exit 134).

## Effects
- gm_node {eff id, kind(lit/decl/native), clauses[], env, hctx, with_frame, with_label,
  result*, native, next}. Context = gm_node* (NULL empty). `with` builds nodes on the C stack.
  Perform: find node by effect-instance id (codegen numbers distinct EffectRefs) → call clause
  `(env, hctx, node, args)`; decl clauses get `self` bytes (stored in the with frame) first.
- Closure call: ctx = closure.cx + nodes for the fn type's effects found in caller cx (copies
  pushed on stack), as interp call_value.
- Native ambient: Out/Err write, Env args/var/exit — runtime functions. Alloc via prelude.
- `with … on`, ctl, resume/discard, foreign calls: "not supported by the C backend yet".

## Other
- @write: runtime gm_format(buf, &type_desc, ptr, top) driven by generated static type
  descriptors (gm_type); output must equal interp/display.rs byte for byte.
- Consts: static blobs initialized at startup from ConstValue bytes + relocs (gm_init()).
- Integer checks inline via __builtin_*_overflow + range checks for odd widths; Fast mode drops
  them; div-by-zero/MIN/-1/bounds always checked.
- `gamma build [-o out] file`, `gamma run --backend=c`. Harness runs every run test on both
  backends (skip list tests/conformance/c-skip.txt, to shrink to empty).
