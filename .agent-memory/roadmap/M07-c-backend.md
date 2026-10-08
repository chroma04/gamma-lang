# M7 — Native code via C
TIR → C11 (+ GNU extensions where needed) → system cc. Runtime in `runtime/` (C). Evidence
passing (App. D.1), exit flags (D.2), static specialization where handler known (D.3),
checks per build mode (Debug/Checked/Fast), traps with locations, integers of any width
(native for ≤128 bits, runtime routines above), f16 via _Float16 or soft conversions.
`gamma build`, `gamma run --backend=c`. Differential testing: every run test executes on both
interpreter and C backend.
