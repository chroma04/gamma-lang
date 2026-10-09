# M6 — Compile-time evaluation complete (§17)
step budget + #fuel, constant restrictions (§17.4: const pointers into static data only),
provenance-tracking memory to detect all UB (§18.4) at compile time, opportunistic local consts,
`when` (top-level, struct/union bodies, statements), `target` module, @static_assert,
@compile_error, @type_of/@type_name, @location/@caller_location.

## Status
Done: `when` (top level incl. nested, struct/union bodies, statements; generic-param check),
`target` module (generated source; DeclValue::Module; `m.x` in values/types/calls),
top-level consts evaluated in sema (ConstDef.value set), #fuel, §17.4 type check (not the
"pointer into static data" check — interp's to_const_value handles relocs), @static_assert,
@compile_error, @location/@caller_location (Sema.caller_site), @type_of, @unreachable,
@expect_*. Not yet: opportunistic local consts, compile-time call-stack in errors,
provenance-tracking UB detection, `target.mode` from a real build-mode flag.
