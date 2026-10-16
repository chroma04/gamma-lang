# tree-sitter-gamma

A [tree-sitter](https://tree-sitter.github.io/) grammar for Gamma. It follows Appendix A of the language specification (`gamma-spec.md`).

The generated parser (`src/parser.c`) is committed, so using the grammar does not need Node.js or the tree-sitter CLI. Only changing it does.

## Layout

| Path | Contents |
| --- | --- |
| `grammar.js` | The grammar. |
| `src/scanner.c` | The external scanner (newlines, including the line break of a multi-line block comment; nested block comments, raw strings, two-word operators). |
| `src/parser.c`, `src/grammar.json`, `src/node-types.json`, `src/tree_sitter/` | Generated. Do not edit. |
| `queries/` | `highlights.scm`, `locals.scm`, `indents.scm`, `folds.scm`, `injections.scm`. |
| `test/corpus/` | Parse tree tests, one file per topic. |
| `test/highlight/` | Highlight tests. |
| `scripts/parse-corpus.sh` | Parses every `.gm` file in the repository. |

## Working on the grammar

```sh
npm install                  # installs the tree-sitter CLI (0.25.x)
npx tree-sitter generate     # regenerate src/ after editing grammar.js
npm test                     # corpus and highlight tests
npm run parse-corpus         # needs the gamma executable (cargo build)
```

`npx tree-sitter test -u` rewrites the expected trees of the corpus. Read the diff before committing it.

`scripts/parse-corpus.sh` asks the compiler (`gamma parse`) which files in `lib/` and `tests/conformance/` are valid. It parses those with tree-sitter and lists the files that produce `ERROR` or `MISSING` nodes. The conformance suite also holds programs that are invalid on purpose. The grammar is not meant to reject those, so the script skips the files the compiler rejects. The script exits with status 1 if a file under `lib/` fails.

## How newlines work

Spec section 2.8 decides which line breaks end a statement. The scanner and the grammar share that job.

- The grammar accepts `_newline` only in brace-delimited bodies: blocks, struct and union bodies, handler bodies and so on. Inside parentheses and brackets the scanner is never asked for a newline, so line breaks there are plain whitespace.
- A line break after a binary operator, `,`, `::`, `:=`, `=>` and similar cannot end a statement, because the grammar needs an operand next.
- The scanner implements rule 2. It drops a newline when the next line starts with `else`, `and`, `or` or `.name`, unless that line contains `:=` outside brackets.
- In a `match` body the scanner emits `_arm_newline`, which always ends the arm.

## Differences from the compiler

The grammar is permissive. It accepts some programs the compiler rejects and it shapes some trees differently.

- Mixing `and` with `or`, chaining comparisons and similar rules are not checked.
- Patterns are parsed as expressions, as they look in the source. `x is .Circle(r)` and `match` arms contain ordinary `call_expression` and `variant_expression` nodes.
- `a, b: T` in a parameter list gives two `parameter` nodes. Only the second has a type.
- In `break x`, the grammar takes `x` as the value. Whether it is a label or a value depends on the enclosing loops, which a grammar cannot see. With two operands, as in `break outer x`, the first one is the label.
- `E!T` in an expression position is an `error_union_expression`. A `!` followed by something that cannot be a type is an unwrap. Where both fit, such as `x!(a, b)`, the unwrap wins.
- `==` and `!=` are not tokens. They produce `ERROR` nodes, as they are errors in the compiler.
