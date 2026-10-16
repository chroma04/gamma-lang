# Gamma for Zed

A [Zed](https://zed.dev) extension for Gamma. It adds the `Gamma` language for `.gm` files and starts the language server, `gamma lsp`.

## What works

- Syntax highlighting, from the tree-sitter grammar in [`../tree-sitter-gamma`](../tree-sitter-gamma/).
- The outline (functions, types, effects, handlers, imports, tests and benchmarks), bracket matching, auto-indent, comment toggling and auto-closing brackets and quotes. Indentation is four spaces, as `gamma fmt` writes it.
- Text objects for Vim mode (`function`, `class`, `comment`).
- From the language server: diagnostics, hover, go to definition, find references, rename, completion, signature help, document symbols and formatting.

## Requirements

`gamma` must be on the `PATH` that Zed sees for the project (Zed uses the shell environment of the worktree). Install it as described in the [README](../../README.md#installing). If the executable is missing, Zed shows the error from the language server start.

## Installing as a dev extension

1. Install the Rust toolchain with `rustup`; Zed compiles the extension's WebAssembly itself.
2. In Zed, open the command palette and run `zed: install dev extension`.
3. Select the `editors/zed` directory of this repository.

Zed fetches the grammar from `extension.toml` (`[grammars.gamma]`): this repository at a fixed commit, in the subdirectory `editors/tree-sitter-gamma`. The commit (`rev`) must exist on the remote, so a change to the grammar only reaches the extension after it has been pushed and `rev` has been bumped to a commit that contains it. Changes to the queries in `languages/gamma/` and to the Rust code are picked up from the local directory (`zed: rebuild extension` after editing).

## Working on the extension

```sh
(cd ../tree-sitter-gamma && npm ci)
./check-queries.sh                          # every query against examples/ and lib/std/
cargo build --release --target wasm32-wasip2   # needs: rustup target add wasm32-wasip2
```

`check-queries.sh` runs each `.scm` file with the tree-sitter CLI over the Gamma sources of the repository. A node type or field that is not in the grammar makes it fail. The queries are adapted from `../tree-sitter-gamma/queries/`, with Zed's capture names (`@keyword`, `@property`, `@function.method`, ...). This crate is its own Cargo workspace, so building the compiler never builds it.

## Publishing

Extensions are published in the [zed-industries/extensions](https://github.com/zed-industries/extensions) registry: a pull request adds this repository as a git submodule, an entry in `extensions.toml` with `submodule = "extensions/gamma"`, `path = "editors/zed"` and the version, and runs `pnpm sort-extensions`. Whether and when to publish is up to the maintainer. Bump `version` in `extension.toml` and `Cargo.toml` for each release.
