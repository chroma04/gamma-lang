# 042 Generic `#soa []T` + std/collections/soa_list (s37, 2026-10-23)

User asked "should there be a module with goodies e.g. growable container?" after #soa;
agreed plan: lift the concrete-only restriction, then a std SoaList.

## Language (spec §5.6.4 bullet "In generic code", C.8 row reworded)
- `#soa []T` / `[N]T` with T a type param (or generic type of one) = stand-in type:
  `Adt(template, [T])`, template per shape (len, is_const) in `Sema.soa_templates`.
  Concrete soa structs now also carry `[elem]` as args and `set_template(shape template)`,
  so match_params binds T for free (plus `soa_const_view` in effects.rs: `#soa []const T`
  pattern vs `#soa []S` actual, keyed on template names).
- Allowed in generic bodies: a[i], a[i]=x, len, slicing, coercions, for, copy_from, make, free.
- `a.copy_from(src)` (user asked, after the per-element-copy limitation): only array method
  on #soa; src coerced to `#soa []const S`; per column calls core/slice copy_from instance
  (length assert + overlap direction for free). SoaOp::CopyFrom. Receiver evaluated first.
  Errors: columns, a[i].f, construction, `is` on slices, @hash, formatting
  (`soa_needs_fields`). `is` on generic #soa *arrays* falls through to struct Eq (fine).
- Ops → `tir::ExprKind::SoaDeferred { op: SoaOp, args }`; operands pre-bound to temps at
  check time; `Sema::soa_expand` lowers per instance (generics.rs mono_expr) with the
  same Sema-level builders the concrete path uses. Make needs 2 temps at mono time →
  `MonoCx.locals` (swapped per nested fn in mono_fn). Back ends: `unsupported` arm.
- make/free now build the soa_alloc/soa_free call directly (instantiate with K value,
  site = `source_location`), effects recorded via `record_effects`.
- Bad element at instantiation: `subst` → `subst_soa` pushes msg to `Sema.soa_errors`,
  drained by `report_soa_errors(span)` in instantiate(), produce_instance(), ty_expr
  generic application; suffix "(in an instantiation used here)".
- Local aliases of attributed types (`Cols :: #soa []T`, `#vec`) now allowed in bodies
  (func.rs local_type_init) and `type_of_name` sees local aliases (`Cols.make(n)`).

## Library: std/collections/soa_list
- `SoaList(S) { pub items: #soa []S; block: #soa []S; alloc }`; block = whole make()
  block (cap = block.len), items = block[0..n]. Growth `with l.alloc { Cols.make(..) }`,
  doubling, min 4; growth/insert/remove/extend move elements with `copy_from` (per column). API mirrors List: push pop insert remove swap_remove extend(#soa []const S)
  reserve truncate clear deinit len is_empty capacity. extend from self handled (regrow
  copies tail before free). deinit twice / reuse after deinit fine (free of len 0 = no-op).

## Left out / follow-ups
- Array-pointer `^#soa [N]S` arg does not infer `#soa []T` (same as plain slices).
- Deep formatting/hashing of nested #soa slices (from 040).
