# M2 — "Hello, world" vertical slice (next)
Goal: `gamma run hello.gm` prints, via sema + TIR + interpreter. Smallest end-to-end path.
Scope:
- Module loading: root = single file or directory (all .gm files directly inside). No imports yet.
- Universe scope (predeclared types/functions), name resolution, decl ordering (lazy, cycle detection §3.4).
- Types: integers (all widths), bool, (), never, string/[]const u8 (enough for print), fn.
- Typing: functions (non-generic), params, locals (`::`, `:=`, typed), assignment, if/while/return/
  break/continue, calls, arithmetic/comparison/logical ops, literal inference (§6.1 incl. amendments),
  untyped constants (§6.2), unreachable-code error, expression statements must be ()/never.
- Effects: only enough to accept `main :: fn() \ {Out}` and `print` (treat Out as ambient, real handler later).
- TIR (typed IR) definition — see architecture/tir.md (to be written in M2).
- Interpreter over TIR (decision 003): integers via fixed-width ops with wrap/check, traps with location.
- `print`/`eprint` with `@write` display rules for ints/bool/string (rest later).
- `gamma check`, `gamma run`; conformance `run` tests + `check` error tests.
Exit: hello world, fib, loops, overflow trap test, literal-inference tests pass.
