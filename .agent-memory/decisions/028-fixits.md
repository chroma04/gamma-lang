# 028 Machine-applicable fixes (s24, 2026-10-17)
- `Diagnostic.fixes: Vec<Fix>`; `Fix { message, edits: Vec<Edit { span, text }> }`.
  Builders `with_fix(msg, span, text)`, `with_fix_edits`. Parser: `report_diag`/`fail_diag`
  (fail_diag keeps fail's same-token cascade suppression).
- Human/short output does NOT show fixes (messages already say what to write; keeps every
  golden unchanged). JSON Lines via `--diag-format=json` (diag::to_json; byte offsets +
  1-based line/char columns; absent location fields for DUMMY spans).
- `gamma fix` (main.rs fix + src/fix.rs plan/apply): only diagnostics with EXACTLY one fix;
  whole fix or nothing; skip overlaps (insertions at the same point overlap); dedupe identical
  fixes (generic instantiations); editable = under pkg::find_root(input) (else input dir) and
  not under std_root(); loop ≤10 rounds re-analyzing (parse errors stop sema). Prints
  "gamma: applied N fix(es) in M file(s)" (not in json mode); exit = check result at the end.
- LSP: stateless. Fixes go in the published diagnostic's `data.fixes` as
  {title, changes:{uri:[TextEdit]}} with EXACT ranges (Analysis::range widens empty spans —
  don't use it for edits); `textDocument/codeAction` (server::code_actions) rebuilds actions
  from params.context.diagnostics; isPreferred iff one fix; honours context.only.
- Lexer drops the newline after a continuing token (`,` etc.): `LexOutput::joined_lines`
  records those offsets so the parser can tell a line-final comma.
- Fixes attached: radix prefix (lexer), `&&`/`||` (spacing added when glued), `else` pattern
  → `_`, line-final comma at a terminator, `try x else d` (two fixes), unused value `_ = `
  (stmt position only: Obligation::UnitStatement), slice `.len()`/`.ptr()` (only if text ends
  with `()`), payload variant `.V` → `.V(_)`, top-level `x := v` → `x :: v` (untyped only).
- s25 added: lone `}` in a template → `}}` (call.rs source_offset_of maps value offset to
  source; only one-byte escapes), std type without import (modules.rs import_fix/std_home:
  inserts `fs :: import "std/fs"` after last import or below leading `//` comments, reuses
  an existing alias), bare effect op → `Fs.op`, `a < b < c` → `a < b and b < c` (mid a
  name, not under `not`, 2 ops), `==`/`!=` → `is`/`is not`, `if x = v {` → `is`, `try x`
  in a test → `ok(x)` (results only; gamma fix never compiles tests: LSP/json only).
- Rejected: lone `{` (maybe a placeholder), missing `needs X` (signature edit varies),
  keyword as name, block-value (two fixes), `len(s)`, `.round` → `.to(T)` (semantics).
