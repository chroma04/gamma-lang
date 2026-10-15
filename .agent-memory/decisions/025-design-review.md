# 025 Design review (s15, docs/design-review.md) — proposals, NOT yet adopted

User asked for an interrogation of the design's "vision" (ideal: modern C alternative with
algebraic effects). Output is a report only; no spec/impl changes made. Awaiting user
go-ahead before implementing. Thesis proposed: "C in which everything a function depends
on appears in its signature." Principles P1 plain data, P2 visible cost, P3 context is an
effect, P4 mistakes rejected, P5 local rules; tie-breaker "one feature per job".

Ranked proposals (report §7):
1. Text: rewrite spec §1 + Appendix C by principle; fix C.9 vs §16.6 (effect-set params
   ARE static row polymorphism); state results-vs-esc boundary; state mutation-visibility
   principle (§7.2) with receiver exception; handler decls = the one type-directed
   resolution (C.4); reflection line (@variants is shape introspection).
2. Small: `discard k` -> `cancel k`; drop specific-over-generic handler-decl precedence
   (overlap = error); literal fallback: drop u32 shift fallback (usize instead).
3. `Test` effect (`fail: esc(...) -> never`), `expect` as ordinary fn \ {Test}, `ok(x)`
   helper; delete §3.8 first-try error-type rule and "expect only directly in test body".
4. Rest params `args: ..A` (print("{} {}", a, b)); `..` = splice everywhere.
5. Ambient ops out of universe (qualified `Clock.now()`, keep `yield`); Fs/Net types to
   std/fs, std/net; `map` -> `hash_map`.
6. Inferable params never positional (removes §16.2 r/n counting rule) — measure first.
7. Named effect sets `Io :: \ {Fs, Alloc}` spliced `..Io`.
