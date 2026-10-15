# Gamma design review: does the language have a vision?

This report looks at Gamma as a whole rather than feature by feature. The question it asks
is whether each decision in the specification (`gamma-spec.md`) follows from one idea:
**a modern C alternative with algebraic effects.** For each decision the test is whether
it can be derived from one of those words, or whether it was added to fix a local problem.

It is a review, not a change. The recommendations at the end are ranked and each says what
it would cost. Section numbers (§) refer to the specification.

---

## 1. Verdict

The core of Gamma is cohesive, and better than most languages at its stage. Effects are
not an add-on: they carry allocation, I/O, iteration, early exit, coroutines, compile-time
evaluation and testability, and the C parts (layout, plain data, bitwise copies, explicit
allocators, no hidden calls) fit around them without friction. Someone who reads the
`Alloc`, `Sys`, `for`-is-a-handler and `with … on stack` chapters can see what the
language is trying to be.

The vision is weaker in three places:

1. **The specification never states the vision.** §1 lists *sensible, fast, simple*, which
   every language claims. The idea that holds the design together (C's implicit context
   becomes explicit through effects) appears only as "the distinguishing feature", and
   Appendix C justifies decisions one at a time instead of deriving them from principles.
2. **Gamma's own features don't use its main idea where they should.** Tests, `expect`,
   the ambient operations in the universe scope and `print`'s argument tuple all solve with
   special rules what the language's own mechanisms (effects, spreads, qualified names)
   would solve uniformly.
3. **Small rules have piled up.** Many rules are each reasonable but together read as
   patches: literal-inference fallbacks, the inferable-parameter counting rule, temporary
   materialization cases, the special `try` in tests and handler-declaration precedence.
   Most of them come from a few root causes that could be addressed directly.

None of this needs a redesign. It needs a stated thesis, about eight targeted changes and
a rewritten rationale appendix.

---

## 2. The thesis, made explicit

What Gamma already is can be stated in one sentence:

> **Gamma is C in which everything a function depends on appears in its signature.**
> Its inputs are in the parameter list, its failures in `E!`, its absences in `?`, and its
> *context* (allocator, I/O, time, randomness, the consumer of its values, its scheduler,
> the outside world) in its effect set after `\`. Handlers supply that context, so it is
> explicit without being passed by hand.

Each of the three words in "modern C alternative with algebraic effects" contributes
principles. These five principles are enough to derive nearly every decision in the
specification:

| # | Principle | From | Short form |
|---|---|---|---|
| P1 | **Data is bytes you can lay out.** Values have C layout, copy bitwise, own nothing and run no code when copied or dropped. | *C* | plain data |
| P2 | **Cost is visible where it is paid.** No construct allocates, calls user code, converts, or suspends unless its syntax or signature says so. | *C* | no hidden work |
| P3 | **Context is an effect.** Anything a function uses that it is not passed appears in its effect set and is supplied by a handler. `Sys` marks the edge of the model. | *algebraic effects* | effects, not globals |
| P4 | **Mistakes are rejected, not interpreted.** Ambiguous text is a compile error, invalid states are not representable (no null, tagged unions, exhaustive `match`), and a failed run-time check traps. Arithmetic is never undefined. | *modern* | no footguns |
| P5 | **Every rule is local.** Legality never depends on global resolution or on how clever the optimizer is. Generics are checked once, and a reader can tell from the call site which code runs. | *modern* | local reasoning |

A tie-breaking rule that the specification already uses sits on top of these: **when two
features could do one job, keep one.**

Two words in the tagline show up as consequences. "Fast" follows from P1 + P2 (nothing
costs more than the C equivalent unless the program asks for it). "Simple" is the
tie-breaker. This is why §1's three adjectives don't read as a vision: two of them are
outcomes, and the third (sensible) is P4 without the specifics.

---

## 3. What already follows from the principles (keep)

These decisions are strong. Each follows from the principles, and together they are what
make Gamma recognisable.

| Decision | Derivation |
|---|---|
| `Sys` as an unhandleable effect for foreign calls, globals, volatile and asm (§12.10) | P3: the outside world is context too. It then pays off twice: a function without `Sys` has no hidden state, so **compile-time evaluation is just calling it** (§17.1) and **test handlers are sound** (§12.9). This is the best derivation in the language. |
| `for` is a `with` that installs a `Yield` handler (§13.2); there is no iterator protocol | P3 + tie-breaker: iteration is "the consumer is context". Ranges, slices and user iterators share one mechanism. |
| `fn` / `esc` / `ctl` operation kinds (§12.2) | P2: the signature states the worst case (a call, a return-flag check, or a stack switch). |
| `with … on stack` required exactly when a `ctl` operation is handled (§14.1) | P1 + P2: suspension needs a stack, and the program provides it. Nothing is allocated implicitly. |
| One-shot continuations only (§14.2, C.6) | P1: multi-shot continuations copy stacks and break pointers into them. Gamma covers exactly the one-shot subset of the classic effect applications (generators, async, early exit, dependency injection), and the boundary is a derivation, not an omission. |
| `Alloc` effect + containers capture their allocator at construction (§15.2) | P3 for scoped memory, P1 for long-lived structures. C's "pass an allocator everywhere" becomes "install one". |
| Second-class `fn` vs first-class `^fn` (§11.6) | P1 + P2: closures capture by reference and never allocate. They cannot escape, so they cannot dangle. |
| Effect polymorphism through second-class callbacks (§11.6) | P3 + tie-breaker: a callback's unlisted effects belong to the place it was written, so `retry(3, fn() => send(p))` needs no annotation. |
| No traits, no operator overloading (§16.9, C.4) | P2 + P5: `a + b` and `m.put(k, v)` never resolve to code the reader can't find. |
| `is`, `@hash` and `{}` formatting all derived from a type's structure (§7.6, §16.8, §21.6) | P5: what a trait system would resolve implicitly is fixed by the type's shape, and nothing can override it. |
| `E!T` / `?T` mirrored, with one `try` and one `else` (§10) | P4: failure and absence are values. One set of operators handles both. |
| `else` meaning "otherwise" in four places (if-else, `x else d`, `else e =>`, `P := e else d`) | Tie-breaker: one word, one meaning, used consistently. |
| "Sigils describe shapes, words describe logic": `and`/`or`/`not`/`is`, `!` freed for errors | P4: removes `=` vs `==`, `!x` vs `!=`, and `&&`/`\|\|` precedence. |
| Precedence errors instead of precedence rules (§7.3) | P4, stated directly. |
| Overflow in Fast builds gives an unspecified value, never UB (§18.4) | P4: the optimizer can't use overflow to delete the program's own safety checks. |
| Closed constraint set, generics checked once and monomorphized (§16) | P5 + P2. |
| `T(x)` legal only when lossless for the *type* (§6.5) | P5: legality never depends on range analysis. |

The main point: **the effect chapters and the C chapters agree with each other.** That is
rare. Most "C plus X" designs bolt X on and leave the seam visible. Gamma's seam is
`Sys`, and it is placed correctly.

---

## 4. Where the vision breaks (findings, ranked)

Each finding gives what the specification says, which principle it strains, a
recommendation and its cost.

### F1. The specification does not state its thesis

**Observation.** §1 lists three generic goals, and effects appear as a "distinguishing
feature". Appendix C is organised by chapter (syntax, types, functions, …), not by
principle, so the rationale reads as a list of independent choices. Part of it now
contradicts the language (F4, F7).

**Why it matters.** The user's test is whether a programmer looking at Gamma *immediately
knows what it is trying to be*. That impression comes first from what the language says
about itself. A reader of §1 learns that Gamma is "simple, fast and sensible", which
doesn't distinguish it from any other language.

**Recommendation.** Rewrite §1 around the thesis sentence and the five principles of
section 2. Reorganise Appendix C so that each subsection is a principle and lists the
decisions that follow from it, with the rejected alternatives under the principle they
violate. Use the same thesis in the README and in chapter 1 of the guide.

**Cost.** Text only.

### F2. Tests are built on special rules instead of on effects

**Observation.** §3.8 and §21.6 give tests their own semantics:

- `expect` and `expect_eq` "may be called only directly in the body of a test (not in a
  function a test calls)".
- A test's error type `E` is "the error type of the first `try` in the body (in source
  order)". A `try` on any *other* error type "does not return: on an error it writes
  `error: ` and the error value to `Err` and stops the test there".

Both rules exist because the test framework has no way to say "stop this test" from a
helper function, or to fail on an error type other than the inferred one. That is exactly
the problem an `esc` operation solves.

**Why it matters.** Tests are where a newcomer first sees handlers being swapped, so they
should show the thesis. Instead, the language's own test runner works around effects. The
"first `try` decides the error type, later `try`s behave differently" rule is the most
patch-like rule in the specification: whether a statement returns depends on whether
another statement came before it.

**Recommendation.** Add a predeclared effect:

```
Test :: effect {
    fail: esc(msg: string, loc: SourceLocation) -> never
}
```

- `expect(cond)` becomes an ordinary function `\ {Test}` that performs `fail` when `cond`
  is false. Helpers can call it, and their signature shows that they can fail a test.
- A test body is checked against the ambient effects plus `Test`. Its type is
  `fn() \ {…, Test}` with no result type, and `try` inside it is a compile error. Errors
  are surfaced with `x else e => fail(…)`, or with a core helper `ok(x)` that performs
  `fail` with the formatted error, the way `expect` does for booleans.
- The two special rules above are deleted.

**Cost.** Moderate. It touches §3.8, §21.6, the test runner and the tests that use `try` in
test bodies (rewritten mechanically to `ok(…)`).

### F3. The universe scope hides the effects it should be showing

**Observation.** §21.1 puts every operation of every ambient effect into the universe:
`current`, `write_out`, `read`, `now`, `wall`, `sleep`, `fill`, `args`, `var`, `exit`, and
all of `Fs` and `Net` (`open`, `close`, `stat`, `send`, `recv`, `connect`, …). It also
includes `File`, `Dir`, `Socket`, `Listener`, `Shutdown`, `SeekFrom` and the other types
of `Fs`/`Net`, plus `List` and `HashMap`. The specification then needs a shadowing rule
for these names (§4.6, §21.1). A tracked issue records a user's own `exit` hiding
`Env.exit` and producing confusing errors.

**Why it matters.** P3 says context is an effect, and the call site is where a reader
should see that. `now()` looks like any function call. `Clock.now()` says "this asks the
context". Bare names also cost namespace: `read`, `open`, `close`, `send`, `var` and
`fill` are among the most common names in systems code. A C-replacement universe should be
small. C's is empty.

**Recommendation.**

- Effect **names** stay in the universe. Their **operations** do not, except `yield`
  (which is syntax-adjacent and used constantly). Ambient operations are written
  qualified: `Alloc.current()`, `Clock.now()`, `Env.args()`, `Fs.open(path, opts)`. This
  form already exists (§12.1). It just becomes the only one for the universe.
- `File`/`Dir`/`Socket`/… move to `std/fs` and `std/net`. The effects can stay declared in
  core (the runtime and test runner need them) and be re-exported from those modules.
- `List` stays (literal-free languages need one growable sequence, and `collect` returns
  it). `HashMap` either stays with a clearer constructor name (see F10) or moves to
  `std/collections`.
- The universe shadowing paragraph shrinks to "locals may not be named like types".

**Cost.** Moderate: a mechanical rewrite of library and test code, plus a smaller §21.1.

### F4. Handler declarations are a small trait system, and the rationale says Gamma has none

**Observation.** §12.6 lets a type supply an implementation of an effect: `handler E for
T`. `with x` then installs every effect that has a declaration for `x`'s type. The rules
needed to make that coherent are the ones C.4 lists as the reason for rejecting traits:

- an **orphan rule**: the declaration must be in the module of `E` or of `T`;
- **coherence**: at most one declaration per `(E, T)`;
- **generic implementations**: `handler(T: type, N: usize) Yield(T) for ^Collector(T, N)`;
- **overlap resolution**: "a declaration for a specific type takes precedence over a
  generic one that also matches it".

C.4 says this machinery "would be the largest and least predictable part of the language"
and that with it "a reader cannot tell from a call which code runs". From `with &s { … }`
a reader can't tell which effects are being installed without searching two modules.

**Why it matters.** It is the clearest place where the rationale and the language
disagree. The mechanism itself is good: values that *are* handlers are what make
`Allocator`, schedulers and loggers pleasant. What is wrong is pretending it isn't
type-directed resolution, and the overlap rule, which is the least local part.

**Recommendation.**

- Keep handler declarations and say what they are in C.4: *the one place a type supplies
  an implementation, confined to a syntactically marked site (`with`), resolved against
  one value's static type, never composed.* That is a principled exception to P5, not a
  contradiction of it.
- Remove the specificity rule. A specific and a generic declaration that both match a type
  is a compile error. Without overlap, resolution is a lookup, not a search.

**Cost.** Small. Rationale text, plus one rule deleted from §12.6 (check the standard
library for any reliance on the precedence).

### F5. `print`'s tuple argument is the first thing a newcomer sees, and it looks like a mistake

**Observation.** The README's first program is:

```
print("{} squared is {}\n", (i, i * i))
```

The tuple exists because Gamma functions cannot be variadic and `print` takes one
`args: A`. The rule is principled (§11.3 already says "the argument list is a tuple"),
but the result reads as a typo, and every logging and formatting function inherits it
(`log.warn("slow request: {} ms", 250)` works only because there is one argument).

**Recommendation.** Make the existing symmetry complete. `..` already means "splice"
everywhere: spread `f(..t)` at a call, `..E` in an effect set. Add the declaration side:
**a final parameter `args: ..A` collects the remaining positional arguments into a tuple
of type `A`.**

```
print :: fn(A: type, template :: string, args: ..A) \ {Out}

print("{} squared is {}\n", i, i * i)
```

This is typed, monomorphized, allocates nothing and is not C varargs. It replaces one
special case (the `@write` "n ≥ 2 means a tuple" rule) with a general one. Forwarding is
`print(template, ..args)`, which already exists.

**Cost.** Moderate: one parameter form in §11.2/§11.3/§16.2, plus migrating calls (a
formatter rewrite can do it mechanically).

### F6. Rules have accumulated around compile-time parameters and literals

These rules are each defensible, but together they make the language feel patched. They
share two root causes.

**Root cause A: type parameters are ordinary positional parameters.** Because
`max :: fn(T: type, xs: []const T)` takes `T` positionally, the call `max(scores)` needs
§16.2's counting rule ("let *r* be the number of non-inferable parameters without
defaults, and *n* …"). The same cause produces method receivers skipping compile-time
parameters (§7.10), `@caller_location` parameters being bound "only by name" (§11.2), and
the default-typed-parameter rule (§16.5).

*Recommendation:* **an inferable parameter is never bound positionally.** It is inferred,
or given by name (`max(T: i64, xs)`). Non-inferable compile-time parameters
(`list(string)`, `make(u8, n)`) stay positional. The counting rule disappears, and the
receiver and `@caller_location` rules become special cases of the general rule instead of
exceptions. *Cost:* moderate. Calls that pass an inferable type positionally
(`new(Page, ---)`, `f(u16, 1, 300)`) become named. Measure how many there are first.

**Root cause B: literal inference over a whole function body with four fallbacks.**
§6.1 unifies literal type variables across the body ("the order of statements does not
matter") and then falls back to `usize` (ranges, lengths, indices, slicing bounds), `u32`
(shift amounts), `i32` or `f64`. Unification itself serves P4 well (`count := 0` never
wraps), but it puts non-local inference into a language whose principle is local
reasoning, and the fallback list is a set of rules to memorize.

*Recommendation:* keep unification and shrink the fallbacks to two cases: **`usize` for
anything used as a size, index, bound or shift amount; otherwise `i32`/`f64`.** The `u32`
shift fallback adds a rule for no benefit, since shift amounts accept any unsigned type.
Have the language server show inferred literal types inline, so the non-local result is
visible. *Cost:* small.

### F7. The rationale rejects effect variables, and §16.6 adds them

**Observation.** C.9 lists "Effect variables / row polymorphism" as rejected, and §12.3
says "there are no effect variables". §16.6 then adds `E: effects` parameters and `..E`
splicing, which is row polymorphism resolved by monomorphization. `std/fiber` depends on
it (`Sched(E)`).

**Why it matters.** Effect-set parameters are a good feature and follow from the
principles (P2 + P5: the effect set is fixed per instantiation and checked once). But a
reader who finds a rejected feature in the language stops trusting the rationale.

**Recommendation.** Restate the policy: *Gamma has effect polymorphism in two forms,
dynamic through second-class callbacks and static through effect-set parameters. It has
no effect variables in the types of run-time values.* Update §12.3, §12.8 and C.9/C.10.
**Cost:** text only.

### F8. Two meanings of "discard"

**Observation.** §7.14: "To discard a value deliberately, assign it to `_`". §14.5:
`discard k` *cancels* a suspended fiber and unwinds its frames, running its `defer`s. The
section text itself uses "cancel" and "cancellation".

**Recommendation.** Rename the keyword to `cancel`. **Cost:** small (keyword, grammar,
`std/fiber`, tests).

### F9. "Mutation is visible at the call" holds except for the receiver

**Observation.** §7.2 requires an explicit `a[..]` to pass an array for a `[]T` parameter
"so that passing an array for mutation is visible at the call", and allows temporaries
only for read-only (`^const T`, `[]const T`) parameters. §7.10 then inserts `&e`
automatically for a `^T` receiver, so `names.push(x)` mutates `names` with nothing
visible at the call.

**Why it matters.** It is the right convenience (method syntax on a place is how every C
successor works), but the rule as written says something else. Several of the §7.2
special cases (temporary materialization for `&e`, arrays passed as `[]const T`) are
really one principle that the specification doesn't state.

**Recommendation.** State the principle once, in §7.2: *a read-only argument may be a
temporary or an array, and the compiler adapts it. A mutable argument must be a named
mutable place, written explicitly, except the receiver of a method call, which is the
subject of the sentence.* Then list the current cases as consequences. **Cost:** text
only.

### F10. Smaller frictions

| Item | Principle | Recommendation |
|---|---|---|
| `map(K, V)` constructs a `HashMap`, while "map" means "apply a function" to most readers, especially in a language with iterators | P4 (no false friends) | `hash_map(K, V)` / `hash_map_with(…)`, matching `list(T)` → `List(T)` |
| `is` is Python's word for *identity*, and Gamma uses it for *value* equality | P4 | Keep (the gain over `==` is large and the guide explains it), but say so in the first chapter of the guide, not just Appendix C |
| `is` on slices compares contents, unlike every other pointer-bearing type, and on a large struct it is an O(n) loop the operator doesn't show | P2 | Keep (the string case decides it), but state in C.4 that `is` is the one operator whose cost scales with the data |
| `@variants`, `@variant_name` and `@type_name` are reflection, while §22.2 lists reflection among the mechanisms "the language deliberately lacks" | F1 | Define the line: closed compile-time introspection of *shape* is part of the structural-derivation principle, and open run-time reflection is not |
| §13.3 says "there is no other iterator protocol", then §13.4 introduces cursors | Tie-breaker | Rephrase: cursors are ordinary methods that `while` uses through binding conditions. `for` knows only `Yield`. That is already true, so only the wording changes |
| `Signed(T)` is satisfied by floats | P4 | Rename to `Neg(T)` (it grants unary `-`) or document why |
| Overflow flavors are operators for `+ - * <<` (`+?`) but methods for `pow` (`pow_checked`) | Tie-breaker | Keep the operators, and name the method forms by the same sigil story (`pow_wrap`, `pow_sat`, `pow_opt`) |
| Error lifting (§10.6) is an implicit, type-directed conversion in a language that otherwise has none | P5 | Keep: it is local, unambiguous and checked at the site. Name it in C.2 as the one implicit conversion between distinct nominal types, and why it is allowed |
| No way to name an effect set: the full ambient set `{Out, Err, Alloc, Env, In, Clock, Rand, Fs, Net}` is written out in full in several places | P3 at scale | Allow `Io :: \ {Fs, Alloc, Err}` as a top-level binding, spliced as `\ {..Io}`. It reuses the §16.6 machinery and addresses the main practical complaint about effect systems, the annotation tax |
| `discard`, `esc` and `ctl` are abbreviations, while every other keyword is a full word | Taste | Rename `discard` (F8). `esc` and `ctl` can stay, since they label kinds and are seen mostly in declarations |

---

## 5. Results versus effects: a boundary to state, not to change

A language built on algebraic effects that reports errors with `E!T` results invites the
question of whether failure should be an effect. It should not. That answer is a
derivation, but the specification never gives it.

- **Results** are for outcomes the *immediate caller* must decide about: a parse failed or
  a file was missing. They are data (P1), visible in the return type (P2), and they cost a
  branch, as an error code does in C.
- **`esc` effects** are for *policies decided far up the stack*: abort this request, exit
  the process, fail this test (F2), stop this iteration. Their cost is the exit-flag
  protocol, which the signature shows.

The rule to write into Appendix C is: *if the nearest caller is the right one to handle
it, return a result. If whoever installed the context is, perform an `esc` operation.*
`Env.exit` and `Yield` already follow it, and `Test.fail` (F2) would too.

---

## 6. What a newcomer should see first

The README's example (`for i in 1..=3 { print(…) }` with `\ {Out}`) shows the syntax but
not the idea. The first program a reader sees should show the thesis in under 25 lines:
a function whose signature lists its context, a handler that supplies it, and a test that
swaps it.

```
fs    :: import "std/fs"
arena :: import "std/arena"

// Count the lines of a file. The signature says everything it touches.
count_lines :: fn(path: string) -> fs.ReadError!usize \ {Fs, Alloc} {
    text := try fs.read_all(path)          // allocates through the current allocator
    defer free(text)
    n: usize = 0
    for b in bytes(text) { if b is '\n' { n += 1 } }
    return n
}

main :: fn() -> fs.ReadError!() \ {Fs, Alloc, Out} {
    scratch := arena.new()                 // C-style: you choose where memory comes from
    defer scratch.deinit()
    with scratch.allocator() {
        print("{} lines\n", try count_lines("README.md"))
    }
}

test "counts lines" {                      // the test handlers give an in-memory Fs
    ok(fs.write_all("a.txt", "x\ny\n"))
    expect(ok(count_lines("a.txt")) is 2)
}
```

From this a reader can see C (explicit memory, bytes, `defer`), modernity (results,
`try`, tests in the language) and the effects thesis (the `\ {…}` row, `with`, test
handlers replacing the file system), all of it plainly derived. It uses the F2 (`ok`) and
F5 recommendations. Without them, the example needs a tuple and a `try` whose meaning
depends on its position.

---

## 7. Recommended order of work

| Step | Change | Type | Size |
|---|---|---|---|
| 1 | F1, F7, F9, results-versus-effects (§5), and the rationale lines in F4/F10: rewrite §1 and Appendix C around the thesis and the five principles | text | small |
| 2 | F8 `discard` → `cancel`; F4 remove handler overlap precedence; F6-B single shift fallback | language, small | small |
| 3 | F2 `Test` effect, `expect` as a function, `ok(…)`; delete the special `try` rule | language | medium |
| 4 | F5 rest parameters `args: ..A`; migrate `print`, `log` and `@write` callers | language | medium |
| 5 | F3 qualified ambient operations, `Fs`/`Net` types out of the universe, F10 `hash_map` | library + spec | medium |
| 6 | F6-A inferable parameters never positional (after measuring how many calls it affects) | language | medium |
| 7 | F10 named effect sets | language | small–medium |

Step 1 alone changes how Gamma presents itself. Steps 2–5 remove the places where its own
features don't use its main idea. After step 5, every special rule named in this report is
either deleted or stated as a consequence of a principle.
