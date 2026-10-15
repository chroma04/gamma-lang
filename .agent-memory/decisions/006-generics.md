# 006 Generics design (M5)
- Generic fn decl: type params → ParamId (Sema.type_params during sig resolution); comptime value
  params `N :: usize` → ParamId too (value params: treat as unknown constants; array len symbolic
  → symbolic lengths are TyKind::ParamArray / LenExpr, decisions/007).
- Body checked ONCE with TyKind::Param types; constraints (Eq/Ord/Num/Int/Signed/Float/Plain)
  stored per ParamId; ops on Param checked against constraints (Numeric/Ord obligations extend).
  Literals at Param T allowed with Num(T): checked per instantiation (error at the call site).
- Calls: infer type args (§16.2 positional/inferable rules), check constraints at call,
  record instance request (generic FnId, args) → instance FnId allocated immediately
  (memo HashMap), body produced in a post-pass after all bodies checked (worklist), by
  substituting TIR: types via sema subst (Adt template → instantiated AdtId), remap nested
  FnIds/HandlerIds (clone nested FnDefs/HandlerLits per instance), check literal ranges.
- Generic types: struct/union/effect(T: type). Instantiation = fresh AdtId per (template, args)
  memoized; prog.adts[inst] has concrete fields; TyKind::Adt(inst, args) keeps args for
  identity/display. Generic bodies see Adt(template, [Param..]).
- Methods on generic types: §16.4 "methods take it as ^List(T) with type's params leading".
