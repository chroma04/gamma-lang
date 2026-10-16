;;; gamma-ts-mode-tests.el --- Tests for gamma-ts-mode  -*- lexical-binding: t; -*-

;;; Commentary:

;; Run these from the repository root; the command is in README.md in the
;; parent directory.  Set the environment variable GAMMA_TS_GRAMMAR_DIR to the
;; directory that holds the compiled grammar.
;;
;; The tests are skipped when the grammar cannot be loaded.

;;; Code:

(require 'ert)
(require 'treesit)
(require 'imenu)
(require 'gamma-ts-mode)

(defvar eglot-server-programs)

(defconst gamma-ts-tests--root
  (expand-file-name
   "../../.." (file-name-directory (or load-file-name buffer-file-name)))
  "The root of the repository.")

(let ((dir (getenv "GAMMA_TS_GRAMMAR_DIR")))
  (when (and dir (not (string-empty-p dir)))
    (add-to-list 'treesit-extra-load-path (expand-file-name dir))))

(defun gamma-ts-tests--require-grammar ()
  "Skip the running test unless the Gamma grammar can be loaded."
  (unless (treesit-language-available-p 'gamma)
    (ert-skip "The tree-sitter grammar for Gamma is not available")))

(defmacro gamma-ts-tests--with-source (source &rest body)
  "Run BODY in a `gamma-ts-mode' buffer holding SOURCE, fontified."
  (declare (indent 1))
  `(with-temp-buffer
     (insert ,source)
     (gamma-ts-mode)
     (setq-local treesit-font-lock-level 4)
     (treesit-font-lock-recompute-features)
     (font-lock-mode 1)
     (font-lock-ensure)
     ,@body))

(defun gamma-ts-tests--face-at (text &optional occurrence)
  "Return the face at the start of TEXT.
Take the match number OCCURRENCE, which defaults to 1."
  (save-excursion
    (goto-char (point-min))
    (dotimes (_ (or occurrence 1))
      (search-forward text))
    (get-text-property (- (point) (length text)) 'face)))

(defun gamma-ts-tests--has-face (face value)
  "Return non-nil if VALUE, a face or list of faces, includes FACE."
  (if (listp value) (memq face value) (eq face value)))

(defconst gamma-ts-tests--sample
  "//! A module comment.
/* block
   comment */
Point :: struct {
    x: f64
    y: f64
}

Shape :: union {
    Circle: f64
    Empty
}

Log :: effect {
    write: fn(msg: string)
}

MAX_SIZE :: 100

add :: fn(a: i32, b: i32) -> i32 {
    return a + b
}

main :: fn() needs Out {
    n := add(1, 2)
    p := Point(x: 1.5, y: 2.0)
    s := \"hi\\n\"
    if n is 3 and true { print(\"{}\", p.x) }
    q := @size_of(Point)
}

test \"adds\" {
    x := 1
}
"
  "A sample Gamma program covering most token classes.")

;;; Highlighting

(ert-deftest gamma-ts-mode-font-lock-keywords ()
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--with-source gamma-ts-tests--sample
    (should (eq (gamma-ts-tests--face-at "return") 'font-lock-keyword-face))
    (should (eq (gamma-ts-tests--face-at "struct") 'font-lock-keyword-face))
    (should (eq (gamma-ts-tests--face-at "effect") 'font-lock-keyword-face))
    (should (eq (gamma-ts-tests--face-at "needs") 'font-lock-keyword-face))
    (should (eq (gamma-ts-tests--face-at "and") 'font-lock-keyword-face))
    (should (eq (gamma-ts-tests--face-at "fn") 'font-lock-keyword-face))))

(ert-deftest gamma-ts-mode-font-lock-comments-and-strings ()
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--with-source gamma-ts-tests--sample
    (should (eq (gamma-ts-tests--face-at "A module") 'font-lock-comment-face))
    (should (eq (gamma-ts-tests--face-at "comment */")
                'font-lock-comment-face))
    (should (eq (gamma-ts-tests--face-at "hi") 'font-lock-string-face))
    (should (eq (gamma-ts-tests--face-at "\\n") 'font-lock-escape-face))))

(ert-deftest gamma-ts-mode-font-lock-declarations ()
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--with-source gamma-ts-tests--sample
    (should (eq (gamma-ts-tests--face-at "add") 'font-lock-function-name-face))
    (should (eq (gamma-ts-tests--face-at "main")
                'font-lock-function-name-face))
    (should (eq (gamma-ts-tests--face-at "Point") 'font-lock-type-face))
    (should (eq (gamma-ts-tests--face-at "Shape") 'font-lock-type-face))
    (should (eq (gamma-ts-tests--face-at "write")
                'font-lock-function-name-face))
    (should (eq (gamma-ts-tests--face-at "MAX_SIZE") 'font-lock-constant-face))
    (should (eq (gamma-ts-tests--face-at "Circle") 'font-lock-constant-face))))

(ert-deftest gamma-ts-mode-font-lock-expressions ()
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--with-source gamma-ts-tests--sample
    (should (eq (gamma-ts-tests--face-at "add" 2)
                'font-lock-function-call-face))
    (should (eq (gamma-ts-tests--face-at "1.5") 'font-lock-number-face))
    (should (eq (gamma-ts-tests--face-at "true") 'font-lock-constant-face))
    (should (eq (gamma-ts-tests--face-at "f64") 'font-lock-type-face))
    (should (eq (gamma-ts-tests--face-at "@size_of")
                'font-lock-builtin-face))
    (should (eq (gamma-ts-tests--face-at "->") 'font-lock-operator-face))
    (should (eq (gamma-ts-tests--face-at ".x") 'font-lock-delimiter-face))
    (should (eq (gamma-ts-tests--face-at "x)") 'font-lock-property-use-face))))

(ert-deftest gamma-ts-mode-font-lock-levels ()
  (gamma-ts-tests--require-grammar)
  (with-temp-buffer
    (insert "main :: fn() { return 1 }\n")
    (gamma-ts-mode)
    (setq-local treesit-font-lock-level 1)
    (treesit-font-lock-recompute-features)
    (font-lock-mode 1)
    (font-lock-ensure)
    (should (eq (gamma-ts-tests--face-at "main") 'font-lock-function-name-face))
    (should-not (gamma-ts-tests--face-at "return"))
    (setq-local treesit-font-lock-level 2)
    (treesit-font-lock-recompute-features)
    (font-lock-flush)
    (font-lock-ensure)
    (should (eq (gamma-ts-tests--face-at "return") 'font-lock-keyword-face))))

;;; Editing commands

(ert-deftest gamma-ts-mode-comment-dwim ()
  (gamma-ts-tests--require-grammar)
  (with-temp-buffer
    (insert "x := 1\n")
    (gamma-ts-mode)
    (should (equal comment-start "// "))
    (goto-char (point-min))
    (comment-region (point-min) (point-max))
    (should (equal (buffer-string) "// x := 1\n"))))

(ert-deftest gamma-ts-mode-indent-basic ()
  (gamma-ts-tests--require-grammar)
  (with-temp-buffer
    (insert "main :: fn() {\nx := 1\nif x is 1 {\nreturn\n}\n}\n")
    (gamma-ts-mode)
    (indent-region (point-min) (point-max))
    (should (equal (buffer-string)
                   (concat "main :: fn() {\n    x := 1\n    if x is 1 {\n"
                           "        return\n    }\n}\n")))))

;;; Navigation and imenu

(ert-deftest gamma-ts-mode-imenu ()
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--with-source gamma-ts-tests--sample
    (let* ((index (imenu--make-index-alist t))
           (names (lambda (category)
                    (mapcar #'car (cdr (assoc category index))))))
      (should (equal (funcall names "Function") '("add" "main")))
      (should (equal (funcall names "Type") '("Point" "Shape" "Log")))
      (should (equal (funcall names "Test") '("adds"))))))

(ert-deftest gamma-ts-mode-defun-navigation ()
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--with-source gamma-ts-tests--sample
    (goto-char (point-min))
    (search-forward "return a")
    (should (equal (treesit-defun-name (treesit-defun-at-point)) "add"))
    (beginning-of-defun)
    (should (looking-at "add ::"))))

(ert-deftest gamma-ts-mode-registrations ()
  (should (assq 'gamma treesit-language-source-alist))
  (should (equal (cdr (assoc "x.gm" auto-mode-alist #'string-match-p))
                 'gamma-ts-mode))
  (require 'eglot nil t)
  (when (featurep 'eglot)
    (should (seq-find (lambda (entry)
                        (let ((modes (car entry)))
                          (memq 'gamma-ts-mode
                                (mapcar (lambda (m) (if (consp m) (car m) m))
                                        (if (listp modes) modes (list modes))))))
                      eglot-server-programs))))

;;; Indentation round trip over formatted sources

(defun gamma-ts-tests--source-files ()
  "Return the formatted Gamma sources in the repository."
  (let ((default-directory gamma-ts-tests--root))
    (append (directory-files-recursively "examples" "\\.gm\\'")
            (directory-files-recursively "lib/std" "\\.gm\\'"))))

(defun gamma-ts-tests--reindent-diff (file &optional strip)
  "Re-indent FILE and return a list of (LINE EXPECTED GOT) for changed lines.
With STRIP non-nil, remove all indentation before re-indenting."
  (with-temp-buffer
    (insert-file-contents (expand-file-name file gamma-ts-tests--root))
    (let ((expected (split-string (buffer-string) "\n")))
      (when strip
        (goto-char (point-min))
        (while (not (eobp))
          (skip-chars-forward " \t")
          (delete-region (line-beginning-position) (point))
          (forward-line 1)))
      (gamma-ts-mode)
      (indent-region (point-min) (point-max))
      (let ((got (split-string (buffer-string) "\n"))
            (line 0)
            result)
        (while (and expected got)
          (setq line (1+ line))
          (unless (equal (car expected) (car got))
            (push (list line (car expected) (car got)) result))
          (setq expected (cdr expected) got (cdr got)))
        (nreverse result)))))

(defun gamma-ts-tests--check-round-trip (strip)
  "Check that re-indenting every source file reproduces it.
With STRIP non-nil, the indentation is removed first."
  (let ((files (gamma-ts-tests--source-files))
        (failures nil)
        (bad-lines 0))
    (should files)
    (dolist (file files)
      (let ((diff (gamma-ts-tests--reindent-diff file strip)))
        (when diff
          (setq bad-lines (+ bad-lines (length diff)))
          (push (format "%s: %d lines differ, first at %d:\n  want %S\n  got  %S"
                        file (length diff) (car (car diff))
                        (nth 1 (car diff)) (nth 2 (car diff)))
                failures))))
    (message "Indentation round trip: %d of %d files unchanged, %d lines differ"
             (- (length files) (length failures)) (length files) bad-lines)
    (when failures
      (ert-fail (format "%d of %d files change when re-indented:\n%s"
                        (length failures) (length files)
                        (mapconcat #'identity (nreverse failures) "\n"))))))

(ert-deftest gamma-ts-mode-indent-formatted-sources ()
  "Re-indenting `gamma fmt' output leaves it unchanged."
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--check-round-trip nil))

(ert-deftest gamma-ts-mode-indent-stripped-sources ()
  "Indenting sources that have lost their indentation restores it."
  (gamma-ts-tests--require-grammar)
  (gamma-ts-tests--check-round-trip t))

(provide 'gamma-ts-mode-tests)

;;; gamma-ts-mode-tests.el ends here
