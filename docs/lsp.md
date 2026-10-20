# Language server

`gamma lsp` runs a language server for Gamma. It speaks the
[Language Server Protocol](https://microsoft.github.io/language-server-protocol/)
over standard input and output, so any editor with an LSP client can use it. The
server is part of the `gamma` binary and needs no configuration.

## What it does

| Feature | What it does |
| --- | --- |
| Diagnostics | Errors and warnings of the compiler, as you type. Secondary locations are attached as related information. |
| Quick fixes | Code actions for the diagnostics that suggest an edit: `&&` becomes `and`, an unused value gets `_ = `, `.V` becomes `.V(_)`, `==` and `!=` become `is` and `is not`, `a < b < c` becomes `a < b and b < c`, a lone `}` in a format string becomes `}}`, a std type used without its import (`File`) gets the `fs :: import "std/fs"` line and its `fs.` prefix, an effect operation called by its bare name (`make_dir`) gets its effect (`Fs.make_dir`), and so on. A diagnostic with one fix marks it preferred (editors apply it with "fix all" or a single key); `try x else d` offers both readings. These are the fixes `gamma fix` applies. |
| Hover | The type of a name or expression. For a function: its signature with the effect set, for example `greet :: fn(n: i32) needs Out`. For a type: its declaration. The doc comment (`///`) follows. |
| Go to definition | Locals and parameters, functions, types, constants, effects and their operations, struct fields, union variants, methods, module members (`util.double`) and imports. It works across files, into the standard library and into the core library (`List`, `list`, `HashMap`, …; see below). `print` and the other builtin functions are part of the language and have no source to open. |
| Find references | Uses of a declaration in every module of the package (and of its path dependencies) that uses it, not only in the open module. |
| Rename | Renames a local, parameter, field, variant, function, type, constant or import binding, and all its uses in every module of the package. `prepareRename` shows the name that will change. Renaming is refused, with a message, for names of the core or standard library and of dependencies outside the package, for a new name that is not an identifier or is a keyword or builtin type name, and when the new name is already taken in a scope the declaration or one of its uses is in. |
| Completion | Locals in scope, the module's declarations, the core library, builtin types and keywords. After a `.`: fields and methods of a value, the `pub` names of a module, the variants of a union type, the operations of an effect. |
| Signature help | The parameters of the function being called, with the current argument marked. |
| Document symbols | The outline of a file: declarations, with fields, variants and operations nested in them. |
| Formatting | The whole document, as `gamma fmt` formats it. A document that has syntax errors is left alone (no edits), although `gamma fmt` formats any text that lexes. |

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

Git and archive dependencies (see the packages chapter of the guide) are used as `gamma.lock`
pins them, from the package store. The server never uses the network and never
changes `gamma.lock`: after adding a dependency, run `gamma fetch` once, and the
server picks it up on the next check.

Find references and rename look beyond the open module. The modules to look at
are all directories with `.gm` files under the package root (the nearest
directory with a `gamma.toml`, or the directory of the open file if there is
none) and under the roots of its path dependencies, minus hidden directories,
`target` and `node_modules`. Of these, only modules whose files mention the name are checked, and
the result of each check is kept until a file it read (or the list of files of
its directory) changes. A dependency does not know the packages that use it, so
from inside a dependency only the dependency's own modules are searched.

Declarations in the embedded core library are served from `lib/core/` next to the
standard library when that directory exists with the same text as the compiler's
copy; otherwise the text is written once to a read-only file under the cache
directory (the one `gamma cache dir` prints, in its `core` folder; a temporary
directory if the cache is turned off) and the editor opens that.

A `.gm` buffer that has not been saved is part of the module of its directory,
and a buffer in a directory that does not exist yet is checked as a module of its
own.

Details are in the "Language server" section of `docs/architecture.md`.

## Limitations

- After a syntax error the server keeps checking: the parser recovers at the next
  statement, and an unclosed bracket is closed where a declaration starts in the
  first column (`name ::`, `pub`, `test`, `handler` or an attribute). Names inside
  a statement that does not parse have no information, and a syntax error in the
  middle of a declaration header drops that declaration.
- Rename relies on the references that the server can resolve. Names in parts of a
  file that the compiler does not take (a branch of a top-level `when` that is not
  selected) and the members of builtin types are not renamed. Rename stops with an
  error if it finds a use that is not a plain name.
- Rename and references in another module are computed from the files of the
  package as they are on disk and in open buffers; modules in a dependency that
  is not a path dependency are not searched.
- The server does not watch files: a change made outside the editor to a file that is
  not open (a `git checkout`, a formatter, a new or deleted file) is seen when a
  document of a module that reads it is next opened, edited or saved.
- Completion after a `.` checks the module again when the text so far has no
  receiver for it. That takes as long as a normal check.

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
