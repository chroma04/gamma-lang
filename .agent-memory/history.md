# Project history (compact; details in git log, decisions/ and the CHANGELOG)
Sessions are numbered sNN; PR numbers are chroma04/gamma-lang pull requests.

## Rust reference era (s1–s37, Oct 8–23 2026)
- s1–s8: spec critical pass; Rust implementation M0–M12 (front end, sema, TIR, interpreter,
  C back end, effects, generics, comptime, fibers, modules/tests, FFI, std); Safe mode via
  Fil-C (017); stdlib policy §22 (018).
- s9–s16: perf rounds, bytecode VM, tooling (packages, LSP, editors, release: 023),
  design review → thesis + principles in spec §1 (025), `needs` effect syntax (026).
- s17–s30: v0.1.0 prep and dogfood examples, fix-its (028), lint (033), wasm32-wasi (034),
  coherence review (030), Cranelift back end (later removed).
- s31–s37: website + playground (036), errors policy ch. 23 (037), HashMap to std (038),
  complex + `#vec` (039), `#soa` (040, 042), stdlib batch (041).

## Self-hosting (s38–s50, Oct 24–27)
- s38–s42: gammac in compiler/ (043): lexer, parser, loader, declarations, checker to
  800/800 parity with the reference; comptime evaluator (046); `?x` pattern (047).
- s43–s46: C back end (048) and bootstrap; port of every `gamma` command; coverage (050);
  interpreter (051).
- s47–s50: Rust retired, C seed bootstrap (052; tag rust-reference-final); test harness in
  Gamma (053); pain-point sweep (054).

## Self-hosted era (s51–s73, Oct 28 – Nov 14)
- Perf rounds: compiler (055, 057, 058, 062, 063, 068, 069), interpreter VM (056, 064, 067),
  playground root stack (059), incremental C units (065), fast bootstrap (066).
- Refactor rounds s64–s66 (done); stale-element-pointer lint (060); tuple spreads (061);
  0.2.0 released (CHANGELOG 2026-10-30).
- s72 (PR #127): perf round 8, compiler check −41%. s73 (PR #128): perf round 9 (copy
  elision in cgen, fmt memo, const `@hash`).
- The idioms handbook was removed in s63 (user: directionless): learned patterns go into
  the guide chapter they belong to (01 naming/modules, 03 match, 05 trap-vs-return, 09
  arena/list pitfalls).
- s75 (PR #131): struct-error lifting rejected (071); function values not formatted or
  inferred (§16.2); `test --backend=interp` native fallback; foreign `#thread_local`;
  constant-table fn tagging fix; linear complex chains; @stack_size vs gcc test; `lower`
  fuzz target; guide for Zig programmers; Windows CI timing probe.
- s76: 0.3.0 release prep: changelog regrouped (Breaking first), version bump everywhere,
  universe HashMap names removed (announced in 0.2.0), `gammac` → `gamma` in comments.
- s77: perf round 10 (072): interp hash/alloc-tag/u128/string-literal fast paths
  (map_insert −19%, new_destroy −30%, primes −31%), exhaustiveness by variant index,
  erased HashMap growth, GM_BT_AT; dropped gm_copy, consts block copy, single-unit release.
- s78: template sharing (073): one body per value types for template-parameter functions
  (str.format, List.write, print wrappers); compiler C −13%, cold build −8%.
- s79–s80: LIR (075) P0 (compiler/lir, `gamma lir`) and P1a (word-only bodies from LIR).
- s81: LIR P1+P2: lowering total (with/exit/assert/bits/…); interp selector covers every
  body (slots, generic Gen ins, sub-codes); tree evaluator, code.gm, vm_compile deleted.
- s82: dead Machine.temps removed; LIR P3a: lir/checks.gm bounds-check elimination +
  VM StepLd/StepSt (benches −0.2..−3%); P4a: cgen/lirc.gm, C bodies from LIR behind
  GAMMA_CGEN_LIR (90% of bodies, self-build identical, +0.7% Ir).
- s83: LIR P4 done: cgen translates every body from LIR (in/#bits, vectors, f16, @fiber,
  @asm, with-on as detached C fn, spec clones, long lists in segments/helpers, -g lines,
  large args in place, const args static); TIR body code deleted (−2.5k lines); seed
  refreshed. Fast benches equal or −1..−7%. P3 closed: analyses stay on TIR, VM handler
  specialization measured (≤7% on iterate) and dropped.
- s84 (076): roadmap sweep: §18.4 untyped memory; vector sqrt/trunc/round/floor/ceil/sat
  + two-vector shuffle; `test --backend=interp` forks per test (112 s -> 7 s); interp
  child-process calls on host threads (std_process_within interpreted); slicing-check
  elision; single-decl perform site (−3% with.gm); examples/rosetta.gm; tree-sitter on
  difftest programs in CI. Measured and dropped: interp frame memset / const copy.
- s85 (077): perf follow-ups: results built in place (memmoves in compiler C halved),
  `let` result hint in LIR, statement-run C blocks (frames −3..−7%), template forwarding
  (`log.info` → `log.log` share), thunks without unused locals. Dropped with numbers:
  nested helpers. Roadmap §2 empty.
