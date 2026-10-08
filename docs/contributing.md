# Contributing

## Building

The compiler is a single Rust crate. With a stable Rust toolchain:

```sh
cargo build            # debug build (lightly optimized)
cargo build --release
```

The binary is `target/<profile>/gamma`. `cargo run -- <args>` builds and runs it.

## Checks

Continuous integration runs, on Linux and macOS:

```sh
cargo fmt --all -- --check
cargo clippy --all-targets      # warnings are errors in CI
cargo test --all-targets
```

Run them before sending a change. Formatting uses the settings in
`rustfmt.toml`.

## Tests

There are two kinds of tests.

**Unit tests** live next to the code they test, in `#[cfg(test)]` modules.

**Conformance tests** are Gamma source files under `tests/conformance/`,
grouped into directories by area (`lex/`, `parse/`, …). The runner,
`tests/conformance.rs`, runs each file through the `gamma` binary and checks the
outcome against expectations written in the file:

```gamma
//! stage: parse
//! dump

main :: fn() {
    x := 1 + 2 * 3
    y := a & b | c          //~ ERROR mixing bitwise operators
}
```

| Directive | Meaning |
| --- | --- |
| `//! stage: lex\|parse\|check\|run` | how far to run the compiler (default `run`) |
| `//! dump` | compare the stage's dump with a golden file: `<name>.tokens` (lex) or `<name>.ast` (parse) |
| `//! exit: N` | expected exit status of a `run` test (default 0) |
| `//~ ERROR text` | an error whose message contains `text` is reported on this line |
| `//~ WARNING text` | likewise for a warning |

Every error and warning the compiler reports must be expected by an annotation,
and every annotation must be matched. A `run` test's expected standard output is
the sibling file `<name>.stdout`.

Useful environment variables:

```sh
GAMMA_TEST_FILTER=parse/match cargo test --test conformance   # run a subset
BLESS=1 cargo test --test conformance                         # rewrite golden files
```

Only bless after checking that the new output is correct; `git diff` shows what
changed.

When adding a feature, add conformance tests that follow the specification
section by section: valid programs that exercise each rule, and invalid programs
that check each compile error the section requires.

## The specification

`gamma-spec.md` is the definition of the language. If an implementation question
reveals a contradiction, gap or impractical rule in the specification, fix the
specification in the same change (keeping it self-consistent and updating its
examples), rather than letting the implementation silently diverge.

## Code style

- Follow the existing structure described in [`architecture.md`](architecture.md).
- Public items get doc comments; other comments explain *why*, not *what*.
- Diagnostics are lowercase, name the construct involved, and suggest a fix when
  there is an obvious one.
- No `unwrap()` on user-controlled input: report a diagnostic instead.
