# Open bugs and accepted behaviour differences (delete entries when fixed)

## Bugs
- After a syntax error inside an `if` condition (`if needs := f() {`), `gamma check` also
  reports "cannot check this kind of expression yet" for the `if`: the checker should stay
  silent on the parser's error node.
- A local alias named like a visible type (`Point :: i32` with a top-level `Point`) is
  accepted though §4.6 forbids it; fixing it may break existing code (grep the repo first).
- `gamma lint` has no unused check for local struct/union types.
- Interpreter hangs when one thread reads a child's output while another writes its stdin
  (even `cat`): tests/conformance/run/std_process_within.gm is `//! native:` for it.
  (Green threads: a blocking host syscall blocks all of them.)
- difftest seed 91451: gcc 15.2 (Debug) prints `null 229 !.Worse` where interp, clang and
  gcc 13/14 print `null 0` for `((p1 -? 1u32 else p1) & e122.sat(u32)).checked(u8)` inside
  `match … { !.Bad(e122) => …}`, p1 = 0. A gcc 15 bug or latent UB (UBSan on gcc 14
  clean); reproduce with gcc 15 (not installable here).
- Interpreter: `&mut` aliasing through raw pointers needs the documented `black_box`
  workaround; `gamma test --timeout` cannot stop a test blocked in a syscall other than
  sleep (documented).

## Friction, by design for now
- Struct-typed errors get no `!.Variant` shorthand / lifting.

## Allowed behaviour differences (documented)
- `print("{}", bytes)` of `[]u8` prints raw bytes (§21.6.1).
- Stack-overflow backtrace depth differs between back ends (frame sizes): no shared stderr
  goldens for fiber overflow.
- UB programs under forced checked/fast modes may exit differently (139 vs 134).
