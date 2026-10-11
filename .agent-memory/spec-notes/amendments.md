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
