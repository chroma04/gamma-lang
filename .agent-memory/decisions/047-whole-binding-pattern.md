# 047 — `?x` pattern: bind an optional or result as it is (s41, user-approved)

- Pain point (10+ hits): a bare name at a `?T`/`E!T` position matches only present/success
  (§9.3), so code bound whole payloads to read optional parts.
- `?x` (AST PatternKind Whole / gammac `.Whole`, dump `bind-whole`) matches anything of
  `?T` or `E!T` and binds it unwrapped-not; lowers to TIR `Bind` at the optional type, so
  TIR, exhaustiveness, back ends unchanged. Irrefutable. Error against other types
  ("`?x` binds an optional or a result as it is, but this is `T`; write `x`").
  At top of a binding condition → "condition always succeeds".
- Results included (one rule: "no implicit unwrap"), though the `?` evokes optionals.
- No struct-field shorthand `S(?x)` (write `f: ?x`). Kept minimal.
- Not-exhaustive note and present-only-binding lint note suggest `?x`; NO machine fix for
  the lint (changing where null goes changes behaviour; `gamma lint --fix` applies fixes).
- tree-sitter already parses `?x` (as optional_type) — no grammar change.
- gammac AST/TIR payload structs (BreakExpr etc.) kept: they mirror Rust's named-field
  variants; only tuple-payload sites in eval/ were rewritten.
