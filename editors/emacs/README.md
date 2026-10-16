# gamma-ts-mode

An Emacs major mode for Gamma, built on Emacs's own tree-sitter support and the grammar in [`../tree-sitter-gamma/`](../tree-sitter-gamma/).

It gives you:

- highlighting at the four standard `treesit-font-lock-level` levels, from the grammar's `highlights.scm`;
- indentation (four spaces), matching the output of `gamma fmt`;
- `//` comments, imenu (functions, types, modules, handlers, tests and benchmarks), `beginning-of-defun` and `end-of-defun`;
- the language server `gamma lsp` registered with Eglot.

It needs Emacs 29.1 or later, built with tree-sitter (`(treesit-available-p)` returns `t`), and a tree-sitter library whose ABI version the grammar supports (see below).

## Install the mode

Put `gamma-ts-mode.el` on your `load-path`, for example from a checkout of this repository:

```elisp
(use-package gamma-ts-mode
  :load-path "~/src/gamma-lang/editors/emacs"
  :mode "\\.gm\\'"
  :hook (gamma-ts-mode . eglot-ensure))
```

Without `use-package`:

```elisp
(add-to-list 'load-path "~/src/gamma-lang/editors/emacs")
(require 'gamma-ts-mode)
```

The mode claims `*.gm` files.

## Install the grammar

Either let Emacs build it (this needs a C compiler and `git`):

```
M-x treesit-install-language-grammar RET gamma RET
```

`gamma-ts-mode` adds the entry `gamma` to `treesit-language-source-alist`, which points at this repository's `editors/tree-sitter-gamma/src`. The library lands in `~/.emacs.d/tree-sitter/`.

Or build it yourself from a checkout and tell Emacs where it is:

```sh
cd editors/tree-sitter-gamma
mkdir -p ~/.emacs.d/tree-sitter
cc -fPIC -shared -O2 -I src src/parser.c src/scanner.c \
    -o ~/.emacs.d/tree-sitter/libtree-sitter-gamma.so
```

(Use `.dylib` on macOS.) If you put it elsewhere, add that directory to `treesit-extra-load-path`.

If the grammar is missing, opening a `.gm` file shows a warning with these steps and the buffer stays in a plain `prog-mode`.

The committed parser is generated with tree-sitter ABI 15. Emacs 29 links tree-sitter 0.20 to 0.22, which load ABI 14 at most, so on such a build regenerate the parser with `npx tree-sitter-cli generate --abi 14` in a copy of `editors/tree-sitter-gamma` and build that copy. (`treesit-install-language-grammar` builds from the committed sources, so it needs an Emacs linked against tree-sitter 0.25 or later, or a grammar that was generated with the older ABI.)

## Language server

With Eglot (built into Emacs 29), `M-x eglot` in a Gamma buffer starts `gamma lsp`; the `gamma` executable must be on `exec-path`. To use another path, set `gamma-ts-mode-lsp-command`:

```elisp
(setq gamma-ts-mode-lsp-command '("/path/to/gamma" "lsp"))
```

## Options

| Variable | Default | Meaning |
| --- | --- | --- |
| `gamma-ts-mode-indent-offset` | `4` | Spaces per indentation step. |
| `gamma-ts-mode-lsp-command` | `("gamma" "lsp")` | Command Eglot runs. |

Set `treesit-font-lock-level` to 3 or 4 for more highlighting (the default is 3; 4 adds operators, brackets and delimiters).

## Tests

The tests use ERT. They need the compiled grammar, found through `GAMMA_TS_GRAMMAR_DIR`; without it they are skipped. From the repository root:

```sh
GAMMA_TS_GRAMMAR_DIR=/path/to/dir-with-libtree-sitter-gamma \
  emacs --batch -L editors/emacs -l ert -l editors/emacs/test/gamma-ts-mode-tests.el \
  -f ert-run-tests-batch-and-exit
```

Besides highlighting and imenu, the tests re-indent every file under `examples/` and `lib/std/` and check that the result is unchanged, both for the files as they are and with all indentation removed first.

To check the byte compiler and `checkdoc`:

```sh
emacs --batch -L editors/emacs --eval '(setq byte-compile-error-on-warn t)' \
  -f batch-byte-compile editors/emacs/gamma-ts-mode.el
emacs --batch -L editors/emacs --eval '(checkdoc-file "editors/emacs/gamma-ts-mode.el")'
```
