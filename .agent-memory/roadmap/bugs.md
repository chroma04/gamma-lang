# Open bugs and accepted behaviour differences (delete entries when fixed)

## Bugs
- difftest seed 91451: gcc 15.2 (Debug) prints `null 229 !.Worse` where interp, clang and
  gcc 13/14 print `null 0` for `((p1 -? 1u32 else p1) & e122.sat(u32)).checked(u8)` inside
  `match … { !.Bad(e122) => …}`, p1 = 0. A gcc 15 bug or latent UB (UBSan on gcc 14
  clean); reproduce with gcc 15 (not installable here).

## Allowed behaviour differences (documented)
- `print("{}", bytes)` of `[]u8` prints raw bytes (§21.6.1).
- Stack-overflow backtrace depth differs between back ends (frame sizes): no shared stderr
  goldens for fiber overflow.
- UB programs under forced checked/fast modes may exit differently (139 vs 134).
