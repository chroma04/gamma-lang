# Language server

`gamma lsp` runs a language server for Gamma. It speaks the
[Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
over standard input and output, so any editor with an LSP client can use it. The
server is part of the `gamma` binary and needs no configuration.

## What it does

| Feature | What it does |
| --- | --- |
| Diagnostics | Errors and warnings of the compiler, as you type. Secondary locations are attached as related information. |
| Hover | The type of a name or expression. For a function: its signature with the effect set, for example `greet :: fn(n: i32) \ {Out}`. For a type: its declaration. The doc comment (`///`) follows. |
| Go to definition | Locals and parameters, functions, types, constants, effects and their operations, struct fields, union variants, methods, module members (`util.double`) and imports. It works across files and into the standard library. |
| Find references | Uses of a declaration within the module that is open. |
| Completion | Locals in scope, the module's declarations, the core library, builtin types and keywords. After a `.`: fields and methods of a value, the `pub` names of a module, the variants of a union type, the operations of an effect. |
| Signature help | The parameters of the function being called, with the current argument marked. |
| Document symbols | The outline of a file: declarations, with fields, variants and operations nested in them. |
| Formatting | The whole document, as `gamma fmt` formats it. A document that does not parse is left alone. |

Positions use UTF-16 code units, as the protocol says. If the client offers
UTF-8, the server uses byte offsets instead.

## How it works

The unit of analysis is a module, which is a directory (language specification,
section 3.1). When you open a file, the server checks every `.gm` file in its
directory, plus the modules they import. Your unsaved edits are used instead of
the files on disk, including edits to modules that the open file imports.

Checking runs again after each change, once the editor stops sending changes. It
does not need `main`, so any module can be checked. Test declarations are
checked too.

Details are in the "Language server" section of `docs/architecture.md`.

## Limitations

- Find references does not search other modules.
- When a function has a syntax error in it, the compiler may not be able to check
  the module. Then only the syntax errors are shown until they are fixed, and
  hover and go to definition do not work.
- Definitions inside the embedded core library (`print`, `List`, and so on) have
  no file to open. Hover works for them.
- Completion after a `.` checks the module again when the text so far has no
  receiver for it. That takes as long as a normal check.
- A file that is not saved yet, and so is not in the directory, is not part of
  its module.

## Setting up an editor

Put `gamma` on your `PATH`, or give the full path to the binary. The server
exits with status 0 after `shutdown` and `exit`.

### Neovim

Neovim 0.10 or newer. Put this in `init.lua`:

```lua
vim.filetype.add({ extension = { gm = "gamma" } })

vim.api.nvim_create_autocmd("FileType", {
  pattern = "gamma",
  callback = function(args)
    vim.lsp.start({
      name = "gamma",
      cmd = { "gamma", "lsp" },
      root_dir = vim.fs.root(args.buf, { "gamma.toml", ".git" }),
    })
  end,
})
```

Then `K` shows hover, `gd` (or `vim.lsp.buf.definition()`) goes to the definition,
and `vim.lsp.buf.format()` formats the buffer.

### Helix

Add this to `~/.config/helix/languages.toml`:

```toml
[language-server.gamma]
command = "gamma"
args = ["lsp"]

[[language]]
name = "gamma"
scope = "source.gamma"
file-types = ["gm"]
roots = ["gamma.toml"]
comment-token = "//"
indent = { tab-width = 4, unit = "    " }
language-servers = ["gamma"]
```

### Emacs

With `eglot`:

```elisp
(define-derived-mode gamma-mode prog-mode "Gamma"
  (setq-local comment-start "// "))
(add-to-list 'auto-mode-alist '("\\.gm\\'" . gamma-mode))
(add-to-list 'eglot-server-programs '(gamma-mode . ("gamma" "lsp")))
```

### Other editors

Any client works that starts a command and talks LSP over its standard input and
output: use the command `gamma lsp` for files ending in `.gm`. The VS Code
extension is documented with the extension.
