# Gamma for Visual Studio Code

Syntax highlighting and language server support for [Gamma](../../README.md) (`.gm` files).

## Features

- Syntax highlighting from a TextMate grammar: comments (line, nested block, doc), strings with escapes and `{}` format placeholders, raw and C strings, character literals, every number form (`0x1p4`, `1_000u32`, `1f32`), keywords, attributes, intrinsics, declarations, types, effect sets and the overflow-flavored operators (`+%`, `*|`, `<<?`, ...).
- Highlighting of `gamma` code blocks in Markdown.
- Bracket matching, auto-closing pairs, comment toggling, indentation and doc comment continuation.
- Language server features through `gamma lsp`: diagnostics, hover, go to definition, completion, document symbols and formatting.

## Requirements

The language server needs the `gamma` executable. Build it from the repository root with `cargo build --release`, then put `target/release/gamma` on your `PATH` or set `gamma.path`. Highlighting works without it.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `gamma.path` | `gamma` | Path to the `gamma` executable. |
| `gamma.lsp.enable` | `true` | Start `gamma lsp` for Gamma files. |

Changing either setting restarts the server. The command **Gamma: Restart Language Server** restarts it by hand. The server's log is in the **Gamma Language Server** output channel.

## Install from source

Package and install the extension:

```sh
cd editors/vscode
npm install
npx @vscode/vsce package --allow-missing-repository --skip-license
code --install-extension gamma-lang-0.1.0.vsix
```

Or link the directory into your extensions folder (run `npm install` first, which installs the language client):

```sh
ln -s "$PWD" ~/.vscode/extensions/gamma-lang.gamma-lang-0.1.0
```

Restart VS Code afterwards.

## Develop

Open `editors/vscode` in VS Code and press F5 to start an Extension Development Host.

`npm test` tokenizes `test/fixtures/sample.gm` with the grammar and checks selected scopes. It also tokenizes every file in `lib/` and fails if the grammar marks anything as illegal.
