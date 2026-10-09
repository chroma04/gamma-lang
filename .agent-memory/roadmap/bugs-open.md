# Open implementation bugs (from docs audit + dogfooding, session 2). Delete when fixed.
3. Generic `Yield(T)` inside `for` (handles Yield(u32)) not rejected (§16 / guide ch10 bad_repeat).
9. `T(5)` in generic body rejected; §16.1 says checked per instantiation. Decide.
11. top-level @compile_error in when branch: wrong message.
12. match on #c_union accepted; §5.11.1 says no match.
