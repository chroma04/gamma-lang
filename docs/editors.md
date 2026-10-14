# Editor support

Gamma has three kinds of editor support. They are independent, so you can use any of them alone.

| Piece | What it gives you | Where it is |
| --- | --- | --- |
| TextMate grammar | Syntax highlighting in VS Code and other editors that read TextMate grammars. | `editors/vscode/syntaxes/gamma.tmLanguage.json` |
| Tree-sitter grammar | Syntax highlighting, indentation, folding and a syntax tree in Neovim, Helix, Emacs, Zed and others. | `editors/tree-sitter-gamma/` |
| Language server | Diagnostics, hover, go to definition, find references, rename, completion, signature help, document symbols and formatting, for any editor with an LSP client. | `gamma lsp` |

Source files use the extension `.gm`.

The Neovim setup below has been checked with Neovim 0.11.4 (tree-sitter highlighting,
and diagnostics, hover and go to definition from the language server), and the Helix
setup with Helix 25.07.1 (`hx --health gamma` finds the server, the parser and the
highlight queries). The VS Code extension packages with `vsce`; the Zed and Emacs
instructions have not been tried yet. Reports of what works in other versions are
welcome.

## The gamma executable

The language server is part of the compiler. Build it from the repository root:

```sh
cargo build --release
```

Then put `target/release/gamma` on your `PATH`. The editor setups below start `gamma lsp`. The server speaks the Language Server Protocol over standard input and output. Highlighting does not need the executable.

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

On Neovim 0.8 to 0.10, start the client from a `FileType` autocommand:

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

`vim.fs.root` needs Neovim 0.10. On older versions use `vim.fs.dirname(vim.fs.find({ 'gamma.toml', '.git' }, { upward = true })[1])`.

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

Zed adds languages through extensions. An extension for Gamma needs an `extension.toml` with a `[grammars.gamma]` entry that names the repository and a commit, and a `languages/gamma/` directory with a `config.toml` and the query files. This repository does not ship that extension yet. The grammar is in a subdirectory, so check that your Zed version can load a grammar from a path inside a repository. The language server is `gamma lsp`.

## Emacs

Emacs 29 and later can build the tree-sitter parser:

```elisp
(add-to-list 'treesit-language-source-alist
             '(gamma "https://github.com/chroma04/gamma-lang" nil "editors/tree-sitter-gamma/src"))
(treesit-install-language-grammar 'gamma)
```

No major mode is provided. For the language server, any LSP client works with the command `gamma lsp`.

## Other editors

- Any editor with an LSP client can run `gamma lsp` over standard input and output.
- Tools that read TextMate grammars can load `editors/vscode/syntaxes/gamma.tmLanguage.json`. The scope name is `source.gamma`. This includes Shiki, which highlights code on documentation sites.
- Tools that use tree-sitter can build `editors/tree-sitter-gamma/src/parser.c` and `scanner.c` into a shared library. The queries in `editors/tree-sitter-gamma/queries/` use the nvim-treesitter capture names.

## GitHub

GitHub highlights code with the grammars of [Linguist](https://github.com/github-linguist/linguist). Linguist does not know Gamma yet, so GitHub shows `.gm` files and `gamma` code blocks without highlighting.

A `.gitattributes` line such as `*.gm linguist-language=Gamma` does not fix this. The `linguist-language` attribute only selects among the languages Linguist already knows. An unknown name has no effect.

To get highlighting on GitHub, Gamma must be added to Linguist. A contribution adds the language to `lib/linguist/languages.yml` and registers a grammar. Linguist accepts TextMate grammars, so `editors/vscode/syntaxes/gamma.tmLanguage.json` can be the starting point. Linguist also requires evidence that the language is used in public repositories. Its contributing guide states the current threshold.
