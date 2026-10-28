;;; gamma-ts-mode.el --- Major mode for Gamma using tree-sitter  -*- lexical-binding: t; -*-

;; Copyright (C) 2025 The Gamma authors

;; Author: The Gamma authors
;; URL: https://github.com/chroma04/gamma-lang
;; Version: 0.1.0
;; Package-Requires: ((emacs "29.1"))
;; Keywords: languages, tree-sitter

;; This file is not part of GNU Emacs.

;; Permission is hereby granted, free of charge, to any person obtaining a copy
;; of this software and associated documentation files, to deal in the software
;; without restriction.  The software is provided "as is", without warranty of
;; any kind.

;;; Commentary:

;; `gamma-ts-mode' is a major mode for the Gamma programming language.  It is
;; built on the tree-sitter support that ships with Emacs 29 and later
;; (`treesit'), and uses the grammar in `editors/tree-sitter-gamma' of the
;; Gamma repository.
;;
;; Install the grammar once with
;;
;;   M-x treesit-install-language-grammar RET gamma RET
;;
;; The mode provides syntax highlighting at the four standard
;; `treesit-font-lock-level' levels, indentation (four spaces), imenu,
;; navigation by declaration and `//' comments.  It also registers the
;; language server `gamma lsp' with Eglot.

;;; Code:

(require 'treesit)
(require 'rx)
(require 'subr-x)

(defgroup gamma-ts nil
  "Major mode for the Gamma programming language."
  :group 'languages
  :prefix "gamma-ts-")

(defcustom gamma-ts-mode-indent-offset 4
  "Number of spaces for each indentation step in `gamma-ts-mode'."
  :type 'natnum
  :safe 'natnump
  :group 'gamma-ts)

(defcustom gamma-ts-mode-lsp-command '("gamma" "lsp")
  "Command that starts the Gamma language server for Eglot."
  :type '(repeat string)
  :group 'gamma-ts)

(defconst gamma-ts-mode-grammar-source
  '("https://github.com/chroma04/gamma-lang" nil "editors/tree-sitter-gamma/src")
  "Where `treesit-install-language-grammar' fetches the Gamma grammar.
The list is (URL REVISION SOURCE-DIR).")

(add-to-list 'treesit-language-source-alist
             (cons 'gamma gamma-ts-mode-grammar-source))

;;; Syntax table

(defvar gamma-ts-mode--syntax-table
  (let ((table (make-syntax-table)))
    ;; Comments are `//' to end of line and nested `/* ... */'.
    (modify-syntax-entry ?/  ". 124b" table)
    (modify-syntax-entry ?*  ". 23n"  table)
    (modify-syntax-entry ?\n "> b"    table)
    (modify-syntax-entry ?\" "\""     table)
    (modify-syntax-entry ?\\ "\\"     table)
    (modify-syntax-entry ?_  "_"      table)
    (modify-syntax-entry ?#  "_"      table)
    (modify-syntax-entry ?@  "_"      table)
    (dolist (c '(?+ ?- ?% ?& ?| ?^ ?~ ?< ?> ?= ?! ??))
      (modify-syntax-entry c "." table))
    table)
  "Syntax table for `gamma-ts-mode'.")

;;; Font lock

(defconst gamma-ts-mode--keywords
  '("if" "else" "match" "when" "while" "for" "in" "return" "break" "continue"
    "resume" "cancel" "with" "on" "defer" "errdefer" "try" "fn"
    "struct" "union" "effect" "where" "test" "bench"
    "import" "pub" "package" "using" "const" "ctl" "esc" "and" "or" "not" "is")
  "Keywords of Gamma.")

(defconst gamma-ts-mode--builtin-type-regexp
  (rx bos
      (or (seq (any "ui") (any "1-9") (* (any "0-9")))
          "usize" "isize" "f16" "f32" "f64" "bool" "never" "type" "rawptr"
          "string" "cstring" "rune" "cont")
      eos)
  "Names of the predeclared types.")

(defconst gamma-ts-mode--type-name-regexp
  (rx bos (any "A-Z") (* (any "A-Za-z0-9_")) eos)
  "Capitalized names, which by convention are types and effects.")

(defconst gamma-ts-mode--constant-name-regexp
  (rx bos (any "A-Z") (+ (any "A-Z0-9_")) eos)
  "Names written in capitals, which by convention are constants.")

(defun gamma-ts-mode--match-p (regexp node)
  "Return non-nil if the text of NODE matches REGEXP, ignoring case folding."
  (let ((case-fold-search nil))
    (string-match-p regexp (treesit-node-text node t))))

(defun gamma-ts-mode--builtin-type-p (node)
  "Return non-nil if NODE names a predeclared type."
  (gamma-ts-mode--match-p gamma-ts-mode--builtin-type-regexp node))

(defun gamma-ts-mode--constant-name-p (node)
  "Return non-nil if NODE is written in capitals only."
  (gamma-ts-mode--match-p gamma-ts-mode--constant-name-regexp node))

(defun gamma-ts-mode--type-name-p (node)
  "Return non-nil if NODE is a capitalized name with a lower case letter."
  (and (gamma-ts-mode--match-p gamma-ts-mode--type-name-regexp node)
       (not (gamma-ts-mode--constant-name-p node))))

(defvar gamma-ts-mode--font-lock-settings
  (treesit-font-lock-rules
   :language 'gamma
   :feature 'comment
   '((comment) @font-lock-comment-face
     (block_comment) @font-lock-comment-face)

   :language 'gamma
   :feature 'comment
   '((doc_comment) @font-lock-doc-face)

   :language 'gamma
   :feature 'string
   '([(string_literal) (c_string_literal) (raw_string_literal)]
     @font-lock-string-face
     (character_literal) @font-lock-string-face)

   :language 'gamma
   :feature 'escape-sequence
   :override t
   '((escape_sequence) @font-lock-escape-face)

   :language 'gamma
   :feature 'attribute
   '((attribute_name) @font-lock-preprocessor-face)

   :language 'gamma
   :feature 'definition
   '((const_declaration
      name: (identifier) @font-lock-function-name-face
      value: (function_expression))
     (const_declaration
      name: (identifier) @font-lock-type-face
      value: [(struct_type) (union_type) (effect_type) (attributed_type)])
     (const_declaration
      name: (identifier) @font-lock-variable-name-face
      value: [(import_expression) (foreign_block)])
     (foreign_member name: (identifier) @font-lock-function-name-face)
     (operation name: (identifier) @font-lock-function-name-face)
     (handler_clause name: (identifier) @font-lock-function-name-face)
     (handler_declaration effect: (identifier) @font-lock-type-face)
     (handler_expression effect: (identifier) @font-lock-type-face)
     (parameter name: (identifier) @font-lock-variable-name-face)
     (clause_parameters (identifier) @font-lock-variable-name-face)
     (constraint name: (identifier) @font-lock-type-face)
     (field_declaration name: (identifier) @font-lock-property-name-face)
     (variant name: (identifier) @font-lock-constant-face))

   :language 'gamma
   :feature 'keyword
   `([,@gamma-ts-mode--keywords] @font-lock-keyword-face
     ;; Contextual keywords (names elsewhere).
     (effect_set "needs" @font-lock-keyword-face)
     (handler_expression "handler" @font-lock-keyword-face)
     (handler_declaration "handler" @font-lock-keyword-face)
     (foreign_block "foreign" @font-lock-keyword-face))

   :language 'gamma
   :feature 'builtin
   '((intrinsic_name) @font-lock-builtin-face
     (discard) @font-lock-builtin-face)

   :language 'gamma
   :feature 'constant
   '((boolean_literal) @font-lock-constant-face
     (null) @font-lock-constant-face
     (uninitialized) @font-lock-constant-face
     (variant_expression name: (identifier) @font-lock-constant-face)
     ((identifier) @font-lock-constant-face
      (:pred gamma-ts-mode--constant-name-p @font-lock-constant-face)))

   :language 'gamma
   :feature 'number
   '([(integer_literal) (float_literal)] @font-lock-number-face)

   :language 'gamma
   :feature 'function
   '((call_expression
      function: (identifier) @font-lock-function-call-face)
     (call_expression
      function: (field_expression
                 field: (identifier) @font-lock-function-call-face))
     (call_expression
      function: (result_unwrap_expression
                 (identifier) @font-lock-function-call-face)))

   :language 'gamma
   :feature 'type
   '(((identifier) @font-lock-type-face
      (:pred gamma-ts-mode--builtin-type-p @font-lock-type-face))
     (scoped_type name: (identifier) @font-lock-type-face)
     (generic_type type: (identifier) @font-lock-type-face)
     ((identifier) @font-lock-type-face
      (:pred gamma-ts-mode--type-name-p @font-lock-type-face)))

   :language 'gamma
   :feature 'property
   '((field_expression field: (identifier) @font-lock-property-use-face)
     (named_argument name: (identifier) @font-lock-variable-use-face)
     (named_element name: (identifier) @font-lock-property-use-face)
     (named_type name: (identifier) @font-lock-property-use-face))

   :language 'gamma
   :feature 'label
   '((label (identifier) @font-lock-constant-face))

   :language 'gamma
   :feature 'operator
   '([ "+" "-" "*" "/" "%" "+%" "-%" "*%" "+|" "-|" "*|" "+?" "-?" "*?"
       "<<" ">>" "<<%" "<<|" "<<?" "&" "|" "~" "<" "<=" ">" ">="
       ".." "..=" "..." "=" ":=" "+=" "-=" "*=" "/=" "%=" "<<=" ">>="
       "&=" "|=" "~=" "+%=" "-%=" "*%=" "<<%=" "+|=" "-|=" "*|=" "<<|="
       "=>" "->" "::" "!" "?" "^"]
     @font-lock-operator-face)

   :language 'gamma
   :feature 'bracket
   '(["(" ")" "[" "]" "{" "}"] @font-lock-bracket-face)

   :language 'gamma
   :feature 'delimiter
   '([";" "," ":" "."] @font-lock-delimiter-face)

   :language 'gamma
   :feature 'error
   :override t
   '((ERROR) @font-lock-warning-face))
  "Tree-sitter font-lock settings for `gamma-ts-mode'.")

;;; Indentation

(defun gamma-ts-mode--line-indentation-anchor (_node _parent &rest _)
  "Return the position of the first non-blank character of the current line.
Used to keep the indentation of lines inside comments and strings."
  (save-excursion
    (back-to-indentation)
    (point)))

(defconst gamma-ts-mode--indent-block-regexp
  (rx bos
      (or "block" "match_body" "handler_body" "field_declaration_list"
          "variant_list" "operation_list" "declaration_block" "foreign_block"
          "arguments" "parameters" "tuple_expression" "tuple_type"
          "clause_parameters" "index_expression" "slice_expression"
          "parenthesized_expression")
      eos)
  "Node types whose children are indented one step inside the node.")

(defconst gamma-ts-mode--indent-continuation-regexp
  (rx bos
      (or "field_expression" "binary_expression" "and_expression"
          "else_expression" "not_expression"
          "assignment_statement" "match_arm" "let_condition"
          "condition_chain" "const_declaration" "typed_declaration"
          "short_declaration" "return_expression" "error_union_expression"
          "error_union_type" "named_argument" "named_element"
          "defer_statement" "function_expression" "call_expression"
          "where_clause" "effect_set" "variant" "field_declaration")
      eos)
  "Node types whose continuation lines are indented one step.")

(defvar gamma-ts-mode--indent-rules
  `((gamma
     ((parent-is "source_file") column-0 0)
     ((node-is ,(rx bos (or "}" ")" "]") eos)) parent-bol 0)
     ((parent-is "block_comment") gamma-ts-mode--line-indentation-anchor 0)
     ((parent-is "raw_string_literal") gamma-ts-mode--line-indentation-anchor 0)
     ((parent-is "string_literal") gamma-ts-mode--line-indentation-anchor 0)
     ((node-is "block_comment") parent-bol gamma-ts-mode-indent-offset)
     ((parent-is ,gamma-ts-mode--indent-block-regexp)
      parent-bol gamma-ts-mode-indent-offset)
     ((parent-is ,gamma-ts-mode--indent-continuation-regexp)
      parent-bol gamma-ts-mode-indent-offset)
     (no-node parent-bol gamma-ts-mode-indent-offset)
     (catch-all parent-bol 0)))
  "Tree-sitter indentation rules for `gamma-ts-mode'.")

;;; Navigation and imenu

(defun gamma-ts-mode--value-type-p (node type-regexp)
  "Return non-nil if the value of const declaration NODE matches TYPE-REGEXP."
  (when-let* ((value (treesit-node-child-by-field-name node "value")))
    (string-match-p type-regexp (treesit-node-type value))))

(defun gamma-ts-mode--function-p (node)
  "Return non-nil if NODE declares a function."
  (gamma-ts-mode--value-type-p node "\\`function_expression\\'"))

(defun gamma-ts-mode--type-p (node)
  "Return non-nil if NODE declares a struct, union or effect type."
  (gamma-ts-mode--value-type-p
   node "\\`\\(?:struct_type\\|union_type\\|effect_type\\|attributed_type\\)\\'"))

(defun gamma-ts-mode--module-p (node)
  "Return non-nil if NODE binds an import or a foreign block."
  (gamma-ts-mode--value-type-p
   node "\\`\\(?:import_expression\\|foreign_block\\)\\'"))

(defun gamma-ts-mode--defun-name (node)
  "Return the name of the declaration NODE, or nil."
  (pcase (treesit-node-type node)
    ((or "const_declaration" "typed_declaration" "short_declaration")
     (when-let* ((name (treesit-node-child-by-field-name node "name")))
       (treesit-node-text name t)))
    ((or "test_declaration" "bench_declaration")
     (when-let* ((name (treesit-node-child-by-field-name node "name")))
       (string-trim (treesit-node-text name t) "\"" "\"")))
    ("handler_declaration"
     (let ((effect (treesit-node-child-by-field-name node "effect"))
           (type (treesit-node-child-by-field-name node "type")))
       (when effect
         (concat (treesit-node-text effect t)
                 (and type (concat " for " (treesit-node-text type t)))))))))

;;; Language server

(defvar eglot-server-programs)

(defun gamma-ts-mode--register-eglot ()
  "Register the Gamma language server in `eglot-server-programs'."
  (add-to-list 'eglot-server-programs
               `((gamma-ts-mode :language-id "gamma")
                 . ,(lambda (&optional _interactive _project)
                      gamma-ts-mode-lsp-command))))

(with-eval-after-load 'eglot
  (gamma-ts-mode--register-eglot))

;;; Mode

(defun gamma-ts-mode--warn-missing-grammar ()
  "Tell the user how to install the missing Gamma grammar."
  (display-warning
   'gamma-ts-mode
   (concat "The tree-sitter grammar for Gamma is not available.\n"
           "Install it with M-x treesit-install-language-grammar RET gamma RET,\n"
           "or build editors/tree-sitter-gamma and add the directory that\n"
           "holds libtree-sitter-gamma to `treesit-extra-load-path'.")
   :warning))

;;;###autoload
(define-derived-mode gamma-ts-mode prog-mode "Gamma"
  "Major mode for editing Gamma code, using tree-sitter.

\\{gamma-ts-mode-map}"
  :group 'gamma-ts
  :syntax-table gamma-ts-mode--syntax-table
  (if (not (treesit-ready-p 'gamma t))
      (gamma-ts-mode--warn-missing-grammar)
    (treesit-parser-create 'gamma)
    ;; Comments.
    (setq-local comment-start "// ")
    (setq-local comment-end "")
    (setq-local comment-start-skip "\\(?://+\\|/\\*+\\)\\s *")
    (setq-local comment-use-syntax t)
    ;; Indentation.
    (setq-local indent-tabs-mode nil)
    (setq-local treesit-simple-indent-rules gamma-ts-mode--indent-rules)
    ;; Navigation.
    (setq-local treesit-defun-type-regexp
                (rx bos (or "const_declaration" "handler_declaration"
                            "test_declaration" "bench_declaration")
                    eos))
    (setq-local treesit-defun-name-function #'gamma-ts-mode--defun-name)
    ;; Imenu.
    (setq-local treesit-simple-imenu-settings
                '(("Function" "\\`const_declaration\\'"
                   gamma-ts-mode--function-p nil)
                  ("Type" "\\`const_declaration\\'"
                   gamma-ts-mode--type-p nil)
                  ("Module" "\\`const_declaration\\'"
                   gamma-ts-mode--module-p nil)
                  ("Handler" "\\`handler_declaration\\'" nil nil)
                  ("Test" "\\`test_declaration\\'" nil nil)
                  ("Bench" "\\`bench_declaration\\'" nil nil)))
    ;; Font lock.
    (setq-local treesit-font-lock-settings gamma-ts-mode--font-lock-settings)
    (setq-local treesit-font-lock-feature-list
                '((comment definition)
                  (keyword string)
                  (builtin constant escape-sequence function number type
                           attribute label)
                  (property operator bracket delimiter error)))
    (treesit-major-mode-setup)))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.gm\\'" . gamma-ts-mode))

(provide 'gamma-ts-mode)

;;; gamma-ts-mode.el ends here
