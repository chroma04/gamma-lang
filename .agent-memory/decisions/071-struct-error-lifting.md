# 071 Struct-typed errors get no lifting (s75, 2026-11-20): REJECTED, CURRENT

Question (user): lift errors into struct-typed error types, and/or allow `!.Variant`
shorthand through a struct's union field (`!.Memory(e)` for
`CalcError(kind: .Memory(e), pos: 0)`).

Evidence counted: across lib/, compiler/, examples/, tools/ only 4 sites wrap an error into
a struct error after `else e =>` (3 in examples/calc/eval.gm, 1 in compiler/load/toml.gm).
Struct errors in the repo (TomlError, ManifestError, FormatError, ReqError, ContError,
TemplateError, CalcError, json SyntaxError) all carry a position/message next to the kind.

Decision: rejected. Spec §10.6 says only a union is a lifting target; C.8 row added; guide
ch. 5 shows the idiom (a function building the error from what the site knows).
Why: the other fields are context (where it happened); a lift would have to default them
(`pos: 0`), and a made-up position reads as a real one (P4); union lifting adds nothing
that is not in the value. Variant shorthand through a field is a second, hidden rule of
lookup (which field? nested?) for 4 sites. Lifting FROM a struct into a union variant
already works (payload type match is by type, any type).
Do not re-open without new evidence (many sites where the defaults are genuinely right).
