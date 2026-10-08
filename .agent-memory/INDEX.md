# Gamma implementation — agent memory INDEX (read first)

## State (end of session 1, 2026-10-08)
- Spec read fully; first critical pass done (amendments: spec-notes/amendments.md).
- M0 bootstrap done. M1 front end (lexer + parser + AST dump) in progress this session.
- Nothing beyond parsing exists yet: no sema, no interpreter, no backend.

## Next steps
1. Finish/verify M1 if incomplete (roadmap/M01-front-end.md).
2. M2 hello-world vertical slice (roadmap/M02-hello-world.md): design TIR + sema core +
   interpreter yourself (interfaces), delegate bulk.

## Map
- roadmap/        M00..M12, one file per milestone (scope, status, exit criteria)
- architecture/   overview.md (code map)
- decisions/      001 Rust single crate · 002 pipeline (sema→TIR→interp/C) · 003 interpreter
                  design · 004 conformance test format · 005 parser ambiguity resolution
- spec-notes/     amendments.md (index of spec edits) · open-issues.md (unresolved)
- sessions/       one file per session
- gotchas/        environment.md

## Conventions
- Branch per session as assigned by the harness; PR into main; CI = fmt + clippy -D warnings + test.
- Docs for humans live in docs/ and README.md; never reference this folder from them.
