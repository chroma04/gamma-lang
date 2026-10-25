# `gamma lint`

`gamma lint` reports code that compiles but is probably not what was meant (a variable
that is never read, a comparison whose result is always the same), and code that has a
simpler form (`if c { true } else { false }`). It checks the program first, so a program
with errors gets its errors and nothing else.

```
gamma lint file.gm            # a program, and the modules it imports
gamma lint .                  # a package: every module of it, with their tests
gamma lint --fix .            # apply the fixes of the warnings that have one
gamma lint --rules            # list the rules and their default levels
```

Given a file or a directory inside a package (a directory with `gamma.toml`), `gamma
lint` checks every module of the package, as `gamma test --all` does, with their `test`
and `bench` declarations, and reports on the package's own files: not on the standard
library or on dependencies. Outside a package it reports on the input's directory.

Each warning names its rule:

```
warning[unused-variable]: the variable `total` is assigned but never read
  --> main.gm:17:5
   |
17 |     total := 0
   |     ^^^^^
  = note: if this is deliberate, name it `_total`
gamma lint: 1 warning
```

With `--diag-format=json` each diagnostic is a JSON object as for `gamma check`, with
the rule in a `"code"` field. `gamma lint` exits with status 1 if the program does not
compile or a rule at the `deny` level found something, and 0 otherwise, so it can run in
CI as `gamma lint -D .` (every warning is an error).

## Fixes

Many warnings carry a fix: an unused variable becomes `_`, `x is true` becomes `x`,
`else { if c {…} }` becomes `else if c {…}`. `gamma lint --fix` applies every warning's
fix to the package's files, checks the program again and repeats until no fix is left
(one fix can uncover another), then prints the warnings that remain. Fixes are meant
not to change what a program does. If the program does not compile after a round of fixes, the round is
undone and the fixes are tried one at a time, leaving out those after which it does not
compile; the summary says how many were left out.

The language server does not show lint warnings yet; run `gamma lint` from the editor's
task runner, or on save.

## Levels

Each rule has a level: `allow` (not reported), `warn` (a warning) or `deny` (an error).
The levels come from, in increasing order of precedence:

1. each rule's default (`gamma lint --rules`; every rule warns except `prefer-immutable`);
2. the `[lint]` table of `gamma.toml`, which maps rule names, or `all`, to levels:

   ```toml
   [lint]
   unused-parameter = "allow"
   naming = "deny"
   prefer-immutable = "warn"
   ```

3. the command line: `--allow=<rules>`, `--warn=<rules>`, `--deny=<rules>` (comma-separated
   rule names, or `all`), applied in order, and `-D` / `--deny-warnings`, which turns every
   rule that would warn into an error;
4. `#allow("rule", …)` before a top-level declaration, which turns the rules off inside it
   (`#allow("all")` turns off every rule):

   ```
   // The class of a code point; 0 is not named in the tables, but is part of the list.
   #allow("unused-declaration")
   CLASS_OTHER :: 0
   ```

   The compiler ignores `#allow`; `gamma lint` reports a name that is not one of its rules
   as an error.

A name that starts with `_` is never reported as unused: `_unused := f()` and
`fn(_event: Event)` say that the value is ignored on purpose.

## Rules

### Unused code

| Rule | Reports | Fix |
| --- | --- | --- |
| `unused-variable` | a local variable or pattern binding whose value is never read, including one that is only assigned | `x := f()` becomes `_ = f()`; a binding in a pattern, destructuring or `for` becomes `_`; an unused `else e =>` capture is removed |
| `unused-parameter` | a parameter of a named function that its body never uses. Parameters of function literals and handler clauses, and of functions used as values (passed to another function, exported), are left alone: their signature is not theirs to choose | — (name it `_x`) |
| `unused-import` | an import binding that nothing in its module uses | the import line is removed |
| `unused-declaration` | a private top-level function, type, constant, global or effect that nothing refers to | — |
| `needless-pub` | a `pub(package)` function or constant that no other module of its package names, or a `pub` one of a program package (one whose root module declares `main`, which no other package imports) that no other module names, as a function, a constant, a method or a value. Types, effects and imports are not reported, since a `pub` signature may need them | — (remove the marker) |
| `unused-label` | a label of a `for`, `while` or `with` that no `break` or `continue` names | the label is removed |

A use in a part of the program that is evaluated at compile time counts: `a.len` of an
array, `@type_of(x)`, `@size_of(T)`. Because the branches of a `when` that are not taken
are not checked, a name written anywhere in a `when` is never reported as unused.

### Suspicious code

| Rule | Reports | Fix |
| --- | --- | --- |
| `dangling-address` | the address of a local, or a slice of a local array, that may be kept after the local ends: assigned through a pointer or a slice, or passed for a parameter whose type is a type parameter (`list.push(&x)`, `map.put(k, &x)`) to a call that also gets a mutable pointer to longer-lived storage. A loop variable or a pattern binding is a copy, whose storage is reused by the next iteration or gone after its scope (§15.1). Where the address certainly outlives the local (stored in a local of an enclosing scope or a global, or returned inside a value), it is a compile error instead. Pointers into a growing list that a later `push` moves are not detected | — (point into the original, `&items[i]`, or keep the value) |
| `present-only-binding` | a bare name in the position of an optional (or a result) below the top of a `match` arm's pattern, such as `.Return(v)` for `Return: ?Expr`, which matches only a present (or success) value (§9.3), when no other arm names the absent case there and a catch-all arm with a non-empty body takes it. A catch-all `_ => {}` ignores the rest on purpose and is not reported | — (add an arm such as `.Return(null)`, or write `.Return(?v)` to bind the optional as it is) |
| `self-comparison` | a value compared with itself (`n is n`, `p.x < p.x`), whose result is known; floats are left alone, since `x is x` is false for NaN | — |
| `self-assignment` | a place assigned to itself (`x = x`) | — |
| `constant-condition` | an `if` or `while` whose condition is the literal `true` or `false` (other than the loop `while true`); a condition known at compile time is written with `when` | — |
| `useless-defer` | a `defer` that is the last statement of its block, so it runs at once, or an `errdefer` there, which can never run | — |
| `identical-branches` | an `if` whose branches are all the same | — |
| `empty-branch` | an `if` with an empty body and no `else`, or an empty `else` (a block holding a comment is not empty) | the empty `else` is removed |

### Simpler forms

| Rule | Reports | Fix |
| --- | --- | --- |
| `bool-comparison` | a `bool` compared with `true` or `false` (`done is false`); an optional `?bool` compared with `true` is not reported | `x is true` → `x`, `x is false` → `not x` |
| `needless-bool` | `if c { true } else { false }`, `if c { return false } else { return true }` and the like | `c`, `return not c`; `not (a is b)` is written `a is not b` |
| `double-negation` | `not not x` | `x` |
| `collapsible-else-if` | an `else` block that holds only an `if` | `else if` |
| `unnecessary-parens` | parentheses around a whole condition, matched or iterated value, initializer, assigned or returned value (kept where they span several lines, or around a construct with `{` in the head of an `if`, `while`, `for` or `match`, where they are needed) | the parentheses are removed |
| `prefer-immutable` (allowed by default) | a variable declared with `:=` that is never assigned, nor borrowed or sliced mutably, so `::` would do | `:=` → `::`, `x: T = e` → `x: T : e`; no fix where the initializer is a literal or a call with literal arguments, which `::` would evaluate at compile time |

### Naming

`naming` reports names not written in the usual case: types, effects and union variants in
UpperCamelCase (`TokenKind`, `.EndOfInput`); functions, operations, fields, parameters and
variables in snake_case (`read_line`). Top-level and local `::` constants may also be
SCREAMING_SNAKE_CASE (`MAX_DEPTH`), and a local `::` binding may name a type
(`Row :: [3]u16`). Type parameters (`T: type`), compile-time value parameters (`N ::
usize`), the members of `#c_union`s (which mirror a C union's) and the functions of
`foreign` blocks are left alone. The warning suggests the name in the expected case; the
language server's rename changes every use.
