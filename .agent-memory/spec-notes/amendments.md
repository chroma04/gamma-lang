# Spec amendments made (current truth is the spec itself; this is a quick index)
- §2.1 lone CR: ill-formed everywhere; raw strings normalize CRLF→LF.
- §2.5.1 defined `width`, digit classes, lowercase prefixes, invalid-suffix rule.
- §2.6 added `...` token (foreign variadics); numeric field after `.` lexes as plain int
  (`t.0.1`); `is not` / `not in` formed by the lexer when on one line.
- §2.8 rule 3 reworded (innermost bracket decides); rule 2 exception = directly inside a match
  body; defined how a match body is recognized.
- §6.1 literal inference: shift amounts not unified with lhs (unsigned, fallback u32); index and
  slice bounds unsigned (fallback usize); coercion target ?T/E!T fixes literal to T.
- §14.7 example: `resume self.next()` was wrong per A.7 rule 10 → bind `r := self.next(); resume r()`.
- A.4 stmt grammar: when_stmt alternatives were misattached; fixed.
- A.7 rules 3 (Bang algorithm), 4 (heads, generalized), new 11 (break labels), 12 (type prefix
  before call), 13 (newlines after attributes).
- From guide review: comma-separated struct fields + optional NL after last element in any brace
  list (A intro); falling off end OK for E!() (§11.1); nested fn vs capturing literal (§11.8);
  `?T else E!T` gives E!T, `!e` error type from try context (§10.4); effect instantiation for
  untyped args inferred from available set (§12.1); &global performs Sys (§19.3); Allocator
  fields pub (§15.2); C aliases qualified `c.c_int` (§19.2); §16.6 for-loop with only concerns
  source call; cursor example uses two bindings (§13.4).
- `@write` float display format defined precisely (§21.6.1).
- §16.5: value-param types restricted (plain data w/o pointers, string, fn ptrs); `[N]T` only with
  the bare param as length; `[N]T` built with `fill:`/`---`. §16.4 refers to §16.5 for value types.
- §21.5: HashMap interface gained contains/len/entries/clear.
- §6.6: `.bits` accepts any `#bits` struct as source (resolves §5.10.5 vs §5.19 contradiction).
- §19.6/§21.1: the memory-order type is named `MemoryOrder` (core prelude).
- §21.7/§21.1: Instant/Duration methods renamed (i.after, d.plus/minus, d.to_ms) — the old names clashed (one module, no overloading: `add` on both types; `ms` as ctor and accessor); ns/us/ms/secs listed in the universe.
- §21.7: Fs interface of the reference core library listed; §7.10 rule 3 explicitly covers effect operations (they are functions of the effect's module).
- §21.7: IoError gains ConnectionRefused (appended; ECONNREFUSED differs per OS so Other(n) was not portable). Net (TCP) interface listed.
- §14.8 rewritten: precise rules (ctl performs free, with-on counts ctl clauses not body, fn args
  followed, ambient performs are errors, includes run-time reserve, ≥ @min_stack). §14.6: @fiber's
  S minus the clause's own effect is required where the handler is (a literal can't know siblings).
- §20.3: @clz/@ctz/@popcount give u32; @bswap (whole bytes)/@bitreverse keep the operand type.
- §6.2: untyped float constants carry a zero sign (Untyped::NegZero; IEEE sign rules for exact
  zero results); lowered as `-(0.0)`.
- §21.2/§15.2/§15.5: `free_aligned(T, s, align)`; allocator free/resize get the alloc-time size+align.
- s3: §14.4 exits past a with…on (from fibers OR clauses: labeled break/continue/return,
  outer esc, discard of an enclosing fiber) discard all unconsumed continuations; only a
  break to the with itself requires all consumed. §14.2 static check matches (cont_check.rs).
- s3: §5.10.5 "plain #bits" is recursive; §9.3 bare binding unwraps one level; §12.1 single
  available instantiation wins; §4.5/§4.7 bare comma RHS; §6.5 T(int var) to float.
- s4: §3.2 pub module bindings are members (`top.s.f`); §3.5 a binding naming a declaration
  (type/generic type/fn/effect/op/module, `x` or `m.x`) is an alias of it (decls.rs
  aliased_decl). `m.G(args)` works in type and value positions (ty_expr generic_decl,
  expr type_of_name).
- s4: §4.5 pattern bindings with else (let-else) + §2.8 rule-2 exception for '.V(..) :=' lines; §5.9 tuple-expression element coercion; §10.4 result else ?T; §3.8 test error type; §12.6 return/try in decl clauses; §12.7 enforced.
- s5: §3.8 `gamma test` runs the tests of the root module and of every same-package module it
  imports (transitively); std and dependency packages excluded. Names distinct per module.
- s6: App. D.2 exit flag may be a per-thread record; test skipped when the callee cannot exit.
- s6 Safe mode: §1.4 optional 4th mode Safe (= Checked wherever spec says "Debug and Checked");
  §3.6 `.Safe`; §6.6 to_ptr grants no access in Safe; §14.1 impl may run fibers on own memory;
  §18.1/18.4/18.5/18.6 adjusted, new §18.7 (Safe semantics per §18.4 item, allocator
  granularity); §19.2 foreign libs built for Safe; §19.5 @asm limits; B.4 row; App. C rule.
- s7: new §22 (stdlib admission + conventions); §21.6.1 format specs; §3.8 try in tests,
  expect_eq, leak sites; §4.7 op= widening; §7.2 array args to []const; §3.2 repeated
  imports; §6.6/§21.3 to(F), unsigned_abs; §7.5 bool ordered; §21.1/21.2 dupe; §21.5 List/map.
- s8 bug hunt: §16.5 array lengths computed from type params rejected in types; §11.6
  second-class fn not part of another type, not `&`, not returned from literals; §12.5 no
  top-level handler literals; §5.11 from_tag enums only; §12.7/§8.8 clause exits + esc/ctl
  performs in defer; App./§16.7 depth limits (mono 256, ADT inst 64).
- s9 spec pass: §6.5 tuple types `T(x)` always construct (spread `T(..t)` to convert); §21.1
  universe list synced with prelude; grammar gaps (else block,
  `_`/`---` primaries, when in union bodies) fixed; many § cross-refs fixed.
- s10 literals/flavors: §2.5.2 `1f32` (decimal int + float suffix is a float; hex keeps `f`
  as a digit) and `0x1p4` (hex float fraction optional); §7.4.2/§10.7 `?` flavors take `?T`
  operands (both evaluated, null if either null; lowered in sema to a block + if-let, so
  no back end changes). bool was already Ord (s7), so that todo half needed nothing.
- s11: Appendix A file production allows no final NL; A.4 stmt takes attributes before a binding (rejected by §20.2). Grammar agent's other spec/compiler gaps (not amended): (i32) as type is grouping via postfix_expr; '#' 'global' notation; attrs before any type parse then error (callconv test); `_` as arg name; break label vs value is scope-dependent.
- s12: §22.4 "cryptography" excludes fixed, unkeyed digests/checksums (std/hash admitted).
- s13: §11.1 top-level fns never infer their return type; §2.5.3 `'é'` example; §4.6/§21.1
  no local binding may be named like a visible type (incl. every sized int name; reverses the
  s9 "locals may hide reserved names" rule — decisions/024); local constants usable at
  compile time; effect aliases `Bytes :: Yield(u8)`.
- s14 audit: §1.4 reference impl's --mode/--target; §3.1 single .gm file as root; §3.6 exact
  target.os/arch sets (+ .Other); removed "string literal initializes [N]u8" (never
  implemented); §21.1 every universe type barred for locals; §21.5 only List/list/HashMap/
  map/map_with re-exported; §21.6 expect/expect_eq only directly in a test body; §12.9 Clock
  wall; A.1 bench; examples fixed (@variant_name, capture_output, FileLogger).
- s15 (design review): §1 thesis + principles; App C by principle; §3.8 Test effect; §11.2 rest
  params; §11.3 named args anywhere, positional binds non-inferable; §16.2 inferable never
  positional; §12.3 named effect sets; §12.6 no overlapping handler decls; §14.5 `cancel`;
  §21.1 universe without ops/Fs-Net types; §21.6.1 template = one value per placeholder;
  §7.2 mutation-visible principle; §6.1 shift fallback usize.
- s16: effect-set syntax `needs A + B` everywhere (§5.14, §11.1, §12.3, §12.6, §16.6, §19.2,
  A.4/A.7, B tables, C.7); `needs not` foreign-only; `..E` splice removed; keyword list gains `needs`.
- s20: §16.6 effect-set param in an effect of the fn's own set (`needs Sched(E)`) is
  inferable, from the one available instantiation at the call (decisions/027).
- s28 coherence review (decisions/030): §1 first program + style rule + litmus; P2 wording; §5.2 ints ≤128;
  §5.6.2/§7.4.6 vectors `is`→bool, lanes_* methods, no array arithmetic; §13.5 args once; §16.5 lengths
  syntactic, `~`; A.3/A.5 `needs` grammar; §21.5 hash_map; §13.4, §15.2, §7.6 explanations; C.8 rows.
- s30 WASI (decisions/034): `Os` gains `.Wasi`; new §3.7.2 "Hosts without some services"
  (no processes/sockets/threads → `IoError.Unsupported`; fibers work); `IoError.Unsupported`.
- s33 (decisions/037): §11.4 array/vector spread (not `[E]T`, not ParamArray); §7.11, §11.2,
  §21.6.1 follow. New ch. 23 errors policy (23.1 kinds, 23.2 what is an error, 23.3 report,
  23.4 no foreign/look-alike guesses except listed, 23.5 limits table). §2.6 `&&`/`||`, `\`
  bullet removed; §10.2 `!` not negation; §3.1 empty dir not a module; §5.10 no self by value;
  §19.2 name charsets; §19.4 runtime names = limitation; §20.2 `#allow` names checked by lint
  only; B.2 `needs` row removed; B.3 35 keywords; C.8 row `+` vs `,`.
- s34 (decisions/038): §21.5 now "Lists" (List only, real rationale); HashMap → std
  (§16.4 example `hm.`; §16.9/C.6 tables; §3.5 alias example `Seq :: List`; §21.1 tables +
  Transition note on deprecated names); §7.7 `in` built-in only; C.8 three rows.
- s35: §5.3.1 Complex(T), §5.6.3 `#vec [N]T` math vectors, §7.4.7 their operators; §16.3
  Float implies Neg; §7.5/§16.3 math vectors not ordered; §19.1 table rows; §20.2 `#vec`;
  A grammar `#vec`; C.2 bullets; C.8 matrices/quaternions/scalar-add/bounded-array rows
  (decisions/039). "vector" alone in the spec still means `#simd`.
- s35: §5.6.4 `#soa` (decisions/040); §20.2 `#soa` row; A grammar; C.2 bullet; C.8 rows (field reflection note, SoA containers/AoSoA, addressable elements).
- s37: §5.6.4 "In generic code" bullet (generic `#soa []T`, decisions/042); element-type bullet drops "concrete"; C.8 SoA-containers row reworded.
- s40: §15.1 rejects storing an address of a local where it certainly outlives it, and returning one inside a value (decisions/045).
- s41: §9.1/§9.2/§9.3/§9.4/A.6 `?x` binds an optional or result as it is (decisions/047).
- s45: §7.12 without an expected type, an `if`/`match` takes the first non-never branch type all others coerce to (fallback: the first). Strictly more programs accepted; same types where all coerced to the first.
- s50 (decisions/054): §6.4 row const-adding coercions inside `?`/`!`; §7.12 array join to `[]const T` + "joins" used by §7.7/§8.4/§10.4; §8.4 tuple iterated at its join; §10.4 fallback joins; §2.4/§2.8/A/B.3 `needs`/`handler`/`foreign` contextual (newline after `needs` no longer suppressed); §21.6.1 numbered placeholders + `q`; §3.5/§4.6/§7.10/§11.8 local struct/union; §21.7/§12.9/§19.6 Fs/Proc ops, pub handles, Env on threads.
