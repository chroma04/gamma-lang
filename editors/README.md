# Editor support

| Directory | Contents |
| --- | --- |
| [`vscode/`](vscode/) | The VS Code extension: a TextMate grammar (`source.gamma`), the language configuration, a client for the language server (`gamma lsp`) and a tokenizer test. |
| [`tree-sitter-gamma/`](tree-sitter-gamma/) | A tree-sitter grammar with an external scanner, highlight, locals, indent and fold queries, and tests. Used by Neovim, Helix, Zed and others. |

See [docs/editors.md](../docs/editors.md) for how to set up each editor.
