# Editor support

Gamma has three kinds of editor support, plus extensions that package them for VS Code and Zed. The three are independent, so you can use any of them alone.

| Piece | What it gives you | Where it is |
| --- | --- | --- |
| TextMate grammar | Syntax highlighting in VS Code and other editors that read TextMate grammars. | `editors/vscode/syntaxes/gamma.tmLanguage.json` |
| Tree-sitter grammar | Syntax highlighting, indentation, folding and a syntax tree in Neovim, Helix, Emacs, Zed and others. | `editors/tree-sitter-gamma/` |
| Emacs mode | `gamma-ts-mode`: the grammar's highlighting, indentation and imenu in Emacs 29 and later, and the language server through Eglot. | `editors/emacs/` |
| Zed extension | The tree-sitter grammar's highlighting, outline and indentation in Zed, and the language server. | `editors/zed/` |
| Language server | Diagnostics, hover, go to definition, find references, rename, completion, signature help, document symbols and formatting, for any editor with an LSP client. | `gamma lsp` |

Source files use the extension `.gm`.

The Neovim setup below has been checked with Neovim 0.11.4 (tree-sitter highlighting,
and diagnostics, hover and go to definition from the language server), and the Helix
setup with Helix 25.07.1 (`hx --health gamma` finds the server, the parser and the
highlight queries). The VS Code extension packages with `vsce`. The Zed extension is checked by building it
and running its queries over the repository's sources, but has not been tried in Zed;
the Emacs mode is tested with Emacs 29.3 in batch mode, with a grammar regenerated for ABI 14. Reports of what works in other versions are
welcome.

## The gamma executable

The language server is part of the compiler. Use the `gamma` from a [release](../README.md#installing), or build it from the repository root (it needs only a C compiler):

```sh
bootstrap/build.sh
```

For a build, put `target/bootstrap/gamma` on your `PATH`. The editor setups below start `gamma lsp`. The server speaks the Language Server Protocol over standard input and output. Highlighting does not need the executable.

## Visual Studio Code

The extension lives in `editors/vscode`. It is not on the Marketplace, so install it from source:

```sh
cd editors/vscode
npm install
npx @vscode/vsce package --skip-license
code --install-extension gamma-lang-0.1.0.vsix
```

Restart VS Code. Files ending in `.gm` open as Gamma. `gamma` code blocks in Markdown files are highlighted too.

The extension starts `gamma lsp` for every Gamma file. Two settings control it:

| Setting | Default | Meaning |
| --- | --- | --- |
| `gamma.path` | `gamma` | Path to the `gamma` executable. Set it if the executable is not on `PATH`. |
| `gamma.lsp.enable` | `true` | Set to `false` to turn the language server off and keep highlighting. |

If the executable cannot be found, the extension shows an error with a button that opens the setting. **Gamma: Restart Language Server** in the command palette restarts the server. Its log is in the *Gamma Language Server* output channel.

## Neovim

These steps need Neovim 0.10 or later. Use `tree-sitter` CLI 0.25 or later to build the parser.

### Highlighting with tree-sitter

Tell Neovim that `.gm` files are Gamma:

```lua
vim.filetype.add({ extension = { gm = 'gamma' } })
```

Build the parser and install it with its queries into your Neovim data directory. Run this from the repository root:

```sh
cd editors/tree-sitter-gamma
tree-sitter build -o ~/.local/share/nvim/site/parser/gamma.so
mkdir -p ~/.config/nvim/queries/gamma
cp queries/*.scm ~/.config/nvim/queries/gamma/
```

Create `~/.local/share/nvim/site/parser` first if it does not exist. Then start tree-sitter highlighting for the filetype:

```lua
vim.api.nvim_create_autocmd('FileType', {
  pattern = 'gamma',
  callback = function() vim.treesitter.start() end,
})
```

With [nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter) (the `main` branch), register the parser instead of building it by hand. The grammar sits in a subdirectory of the repository, so give its location:

```lua
vim.api.nvim_create_autocmd('User', {
  pattern = 'TSUpdate',
  callback = function()
    require('nvim-treesitter.parsers').gamma = {
      install_info = {
        url = 'https://github.com/chroma04/gamma-lang',
        location = 'editors/tree-sitter-gamma',
        queries = 'queries',
      },
    }
  end,
})
```

Then run `:TSInstall gamma`. Check the nvim-treesitter documentation if your version uses different keys.

### Language server

On Neovim 0.11 or later:

```lua
vim.lsp.config('gamma', {
  cmd = { 'gamma', 'lsp' },
  filetypes = { 'gamma' },
  root_markers = { 'gamma.toml', '.git' },
})
vim.lsp.enable('gamma')
```

On Neovim 0.10, start the client from a `FileType` autocommand:

```lua
vim.api.nvim_create_autocmd('FileType', {
  pattern = 'gamma',
  callback = function(args)
    vim.lsp.start({
      name = 'gamma',
      cmd = { 'gamma', 'lsp' },
      root_dir = vim.fs.root(args.buf, { 'gamma.toml', '.git' }),
    })
  end,
})
```


## Helix

Add this to `~/.config/helix/languages.toml`, or to `.helix/languages.toml` in a project:

```toml
[language-server.gamma]
command = "gamma"
args = ["lsp"]

[[language]]
name = "gamma"
scope = "source.gamma"
file-types = ["gm"]
roots = ["gamma.toml"]
comment-tokens = ["//", "///"]
block-comment-tokens = { start = "/*", end = "*/" }
indent = { tab-width = 4, unit = "    " }
language-servers = ["gamma"]

[[grammar]]
name = "gamma"

[grammar.source]
git = "https://github.com/chroma04/gamma-lang"
rev = "main"
subpath = "editors/tree-sitter-gamma"
```

Use a commit hash for `rev` if you want a fixed version. Fetch and build the grammar:

```sh
hx --grammar fetch
hx --grammar build
```

Helix reads queries from its runtime directory, so copy them:

```sh
mkdir -p ~/.config/helix/runtime/queries/gamma
cp editors/tree-sitter-gamma/queries/{highlights,locals,injections}.scm ~/.config/helix/runtime/queries/gamma/
```

Do not copy `indents.scm` and `folds.scm`. They use nvim-treesitter capture names. Helix uses a different set of indent captures.

Run `hx --health gamma` to check the setup.

## Zed

`editors/zed/` is a Zed extension: highlighting, outline, bracket matching, indentation and text objects from the tree-sitter grammar, and the language server `gamma lsp`, which the extension starts from the `gamma` on your `PATH`. Until it is in Zed's extension registry, install it with the command palette's `zed: install dev extension`, choosing the `editors/zed` directory of a checkout. Zed builds the extension's WebAssembly itself, so it needs a Rust toolchain installed through `rustup`. See `editors/zed/README.md`.

## Emacs

`editors/emacs/gamma-ts-mode.el` is a major mode for Emacs 29.1 and later, built on the tree-sitter grammar. It gives highlighting, indentation (four spaces), imenu and navigation by declaration, and registers `gamma lsp` with Eglot.

Install the grammar, then load the mode from a checkout:

```elisp
(add-to-list 'load-path "~/src/gamma-lang/editors/emacs")
(require 'gamma-ts-mode)
(treesit-install-language-grammar 'gamma)
```

`.gm` files open in `gamma-ts-mode`, and `M-x eglot` starts the language server from the `gamma` on your `PATH`. Emacs builds linked against tree-sitter older than 0.25 cannot load the committed parser (ABI 15) and need it regenerated with `--abi 14`. See `editors/emacs/README.md` for the details, a `use-package` snippet and the tests.

## Other editors

- Any editor with an LSP client can run `gamma lsp` over standard input and output.
- Tools that read TextMate grammars can load `editors/vscode/syntaxes/gamma.tmLanguage.json`. The scope name is `source.gamma`. This includes Shiki, which highlights code on documentation sites.
- Tools that use tree-sitter can build `editors/tree-sitter-gamma/src/parser.c` and `scanner.c` into a shared library. The queries in `editors/tree-sitter-gamma/queries/` use the nvim-treesitter capture names.

## GitHub

GitHub highlights code with the grammars of [Linguist](https://github.com/github-linguist/linguist). Linguist does not know Gamma yet, so GitHub shows `.gm` files and `gamma` code blocks without highlighting.

A `.gitattributes` line such as `*.gm linguist-language=Gamma` does not fix this. The `linguist-language` attribute only selects among the languages Linguist already knows. An unknown name has no effect.

To get highlighting on GitHub, Gamma must be added to Linguist. A contribution adds the language to `lib/linguist/languages.yml` and registers a grammar. Linguist accepts TextMate grammars, so `editors/vscode/syntaxes/gamma.tmLanguage.json` can be the starting point. Linguist also requires evidence that the language is used in public repositories. Its contributing guide states the current threshold.
