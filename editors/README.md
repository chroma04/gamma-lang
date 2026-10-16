# Editor support

| Directory | Contents |
| --- | --- |
| [`vscode/`](vscode/) | The VS Code extension: a TextMate grammar (`source.gamma`), the language configuration, a client for the language server (`gamma lsp`) and a tokenizer test. |
| [`zed/`](zed/) | The Zed extension: the language definition, queries adapted from the tree-sitter grammar and a launcher for the language server (`gamma lsp`). |
| [`emacs/`](emacs/) | `gamma-ts-mode`, an Emacs 29+ major mode on the tree-sitter grammar: highlighting, indentation, imenu and an Eglot entry for the language server (`gamma lsp`), with ERT tests. |
| [`tree-sitter-gamma/`](tree-sitter-gamma/) | A tree-sitter grammar with an external scanner, highlight, locals, indent and fold queries, and tests. Used by Neovim, Helix, Emacs, Zed and others. |

See [docs/editors.md](../docs/editors.md) for how to set up each editor.
