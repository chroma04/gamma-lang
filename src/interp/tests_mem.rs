//! Tests: memory, aggregates, optionals, results, patterns.

use super::testkit::*;
use super::*;
use num_bigint::BigInt;

#[test]
fn locals_and_assignment() {
    let v = run_body(Ty::i32(), |f| {
        let x = f.local("x", Ty::i32());
        blk(
            vec![let_(x, i32e(1)), assign(local(x, Ty::i32()), add(local(x, Ty::i32()), i32e(41)))],
            Some(local(x, Ty::i32())),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 42);
}

#[test]
fn assignment_through_pointer() {
    let pt = Ty::ptr(false, Ty::i32());
    let v = run_body(Ty::i32(), |f| {
        let x = f.local("x", Ty::i32());
        let p = f.local("p", pt);
        blk(
            vec![let_(x, i32e(1)), let_(p, addr_of(local(x, Ty::i32()))), assign(deref(local(p, pt)), i32e(7))],
            Some(local(x, Ty::i32())),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 7);
}

#[test]
fn struct_fields_read_and_write() {
    let mut tb = Tb::new();
    let s = tb.add_struct("P", &[("x", Ty::i32()), ("y", Ty::u8()), ("z", Ty::int(false, 64))]);
    let v = run_in(&mut tb, Ty::int(false, 64), |f| {
        let a = f.local("a", s);
        let p = f.local("p", Ty::ptr(false, s));
        let la = local(a, s);
        blk(
            vec![
                let_(a, ex(s, ExprKind::Aggregate(vec![i32e(1), u8e(2), int(3, Ty::int(false, 64))]))),
                assign(field(la.clone(), 1, Ty::u8()), u8e(9)),
                let_(p, addr_of(la.clone())),
                // auto-deref field access through a pointer
                assign(field(deref(local(p, Ty::ptr(false, s))), 2, Ty::int(false, 64)), int(100, Ty::int(false, 64))),
            ],
            Some(add(
                add(
                    coerce(Coercion::IntWiden, field(la.clone(), 1, Ty::u8()), Ty::int(false, 64)),
                    field(la, 2, Ty::int(false, 64)),
                ),
                int(0, Ty::int(false, 64)),
            )),
            Ty::int(false, 64),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 109);
}

#[test]
fn struct_value_field_of_rvalue() {
    let mut tb = Tb::new();
    let s = tb.add_struct("Q", &[("a", Ty::u8()), ("b", Ty::i32())]);
    let agg = ex(s, ExprKind::Aggregate(vec![u8e(1), i32e(-5)]));
    let v = tb.eval(&field(agg, 1, Ty::i32())).unwrap();
    assert_eq!(int_of(&v), -5);
    let t = tuple(vec![i32e(4), i32e(5)]);
    assert_eq!(int_of(&tb.eval(&field(t, 0, Ty::i32())).unwrap()), 4);
}

#[test]
fn arrays_and_indexing() {
    let at = Ty::array(3, Ty::i32());
    let v = run_body(Ty::i32(), |f| {
        let a = f.local("a", at);
        let la = local(a, at);
        blk(
            vec![let_(a, array(vec![i32e(1), i32e(2), i32e(3)])), assign(index(la.clone(), usz(1)), i32e(20))],
            Some(add(index(la.clone(), usz(1)), index(la, usz(2)))),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 23);
    // rvalue array indexing
    assert_eq!(eval_int(&index(array(vec![i32e(5), i32e(6)]), usz(1))), 6);
    let e = run_body(Ty::i32(), |f| {
        let a = f.local("a", at);
        blk(vec![let_(a, array(vec![i32e(1), i32e(2), i32e(3)]))], Some(index(local(a, at), usz(3))), Ty::i32())
    });
    assert!(trap_msg(e).contains("index 3 out of bounds for length 3"));
    assert!(eval_trap(&index(array(vec![i32e(5), i32e(6)]), usz(2))).contains("index 2 out of bounds for length 2"));
    // array fill
    let fill = ex(Ty::array(4, Ty::u8()), ExprKind::ArrayFill(Box::new(u8e(7))));
    let v = Tb::new().eval(&fill).unwrap();
    assert_eq!(v.bytes.to_vec(), vec![7, 7, 7, 7]);
}

#[test]
fn slicing() {
    let at = Ty::array(5, Ty::i32());
    let run = |lo: Option<i128>, hi: Option<i128>, incl: bool, then: &dyn Fn(Expr) -> Expr| {
        run_body(Ty::i32(), |f| {
            let a = f.local("a", at);
            let st = Ty::slice(false, Ty::i32());
            let s = f.local("s", st);
            blk(
                vec![
                    let_(a, array((1..=5).map(i32e).collect())),
                    let_(s, slice(local(a, at), lo.map(usz), hi.map(usz), incl)),
                ],
                Some(then(local(s, st))),
                Ty::i32(),
            )
        })
    };
    let len = |s: Expr| coerce(Coercion::IntWiden, ex(Ty::usize(), ExprKind::SliceLen(Box::new(s))), Ty::usize());
    let as_i32 = |s: Expr| builtin(Builtin::Trunc, vec![len(s)], Ty::i32());
    assert_eq!(int_of(&run(Some(1), Some(4), false, &as_i32).unwrap()), 3);
    assert_eq!(int_of(&run(Some(1), Some(4), true, &as_i32).unwrap()), 4);
    assert_eq!(int_of(&run(None, None, false, &as_i32).unwrap()), 5);
    assert_eq!(int_of(&run(Some(2), None, false, &as_i32).unwrap()), 3);
    assert_eq!(int_of(&run(None, Some(2), false, &as_i32).unwrap()), 2);
    assert_eq!(int_of(&run(Some(1), Some(1), false, &as_i32).unwrap()), 0);
    assert_eq!(int_of(&run(Some(1), Some(3), false, &|s| index(s, usz(1))).unwrap()), 3);
    let msg = trap_msg(run(Some(3), Some(2), false, &as_i32));
    assert!(msg.contains("slice bounds 3..2 invalid for length 5"), "{}", msg);
    let msg = trap_msg(run(Some(0), Some(6), false, &as_i32));
    assert!(msg.contains("slice bounds 0..6 invalid for length 5"), "{}", msg);
    let msg = trap_msg(run(Some(1), Some(3), false, &|s| index(s, usz(2))));
    assert!(msg.contains("index 2 out of bounds for length 2"), "{}", msg);
    // writes through a slice reach the array
    let v = run_body(Ty::i32(), |f| {
        let a = f.local("a", at);
        let st = Ty::slice(false, Ty::i32());
        let s = f.local("s", st);
        blk(
            vec![
                let_(a, array((1..=5).map(i32e).collect())),
                let_(s, slice(local(a, at), Some(usz(2)), None, false)),
                assign(index(local(s, st), usz(0)), i32e(99)),
            ],
            Some(index(local(a, at), usz(2))),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 99);
}

#[test]
fn string_literals_are_read_only() {
    // s.ptr[0] = 1 writes to a literal.
    let e = run_body(Ty::unit(), |_| {
        let mp = Ty::many_ptr(false, Ty::u8());
        let ptr = coerce(
            Coercion::PtrToPtr,
            ex(Ty::many_ptr(true, Ty::u8()), ExprKind::SlicePtr(Box::new(string("hi")))),
            mp,
        );
        blk(
            vec![assign(
                ex(Ty::u8(), ExprKind::Index { base: Box::new(ptr), index: Box::new(usz(0)), checked: false }),
                u8e(1),
            )],
            None,
            Ty::unit(),
        )
    });
    assert!(trap_msg(e).contains("read-only"));
    // reading is fine
    let s = string("hi");
    assert_eq!(eval_int(&index(s, usz(1))), b'i' as i128);
}

#[test]
fn use_after_return_is_detected() {
    let mut tb = Tb::new();
    let pt = Ty::ptr(false, Ty::i32());
    let mut g = FnB::new("leak", pt);
    let x = g.local("x", Ty::i32());
    let body = blk(vec![let_(x, i32e(1))], Some(addr_of(local(x, Ty::i32()))), pt);
    let gid = tb.add(g.build(body));
    let e = run_in(&mut tb, Ty::i32(), |f| {
        let p = f.local("p", pt);
        blk(vec![let_(p, call(gid, vec![], pt))], Some(deref(local(p, pt))), Ty::i32())
    });
    let msg = trap_msg(e);
    assert!(msg.contains("lifetime has ended"), "{}", msg);
}

#[test]
fn null_and_wild_pointers_trap() {
    let pt = Ty::ptr(false, Ty::i32());
    let null_ptr = builtin(Builtin::Bits, vec![usz(0)], pt);
    assert!(eval_trap(&deref(null_ptr)).contains("null pointer"));
    let wild = builtin(Builtin::Bits, vec![usz(0x1234_5678)], pt);
    assert!(eval_trap(&deref(wild)).contains("not inside any allocation"));
}

#[test]
fn out_of_bounds_pointer_access_traps() {
    let at = Ty::array(2, Ty::i32());
    let mp = Ty::many_ptr(false, Ty::i32());
    // p[2] on a pointer to the first of two elements reads past the allocation.
    let e = run_body(Ty::i32(), |f| {
        let a = f.local("a", at);
        let p = f.local("p", mp);
        let to_mp = coerce(Coercion::PtrToPtr, addr_of(local(a, at)), mp);
        blk(
            vec![let_(a, array(vec![i32e(1), i32e(2)])), let_(p, to_mp)],
            Some(ex(
                Ty::i32(),
                ExprKind::Index { base: Box::new(local(p, mp)), index: Box::new(usz(2)), checked: false },
            )),
            Ty::i32(),
        )
    });
    assert!(trap_msg(e).contains("invalid memory access"));
    // p[1] is in bounds
    let v = run_body(Ty::i32(), |f| {
        let a = f.local("a", at);
        let p = f.local("p", mp);
        let to_mp = coerce(Coercion::PtrToPtr, addr_of(local(a, at)), mp);
        blk(
            vec![let_(a, array(vec![i32e(1), i32e(2)])), let_(p, to_mp)],
            Some(ex(
                Ty::i32(),
                ExprKind::Index { base: Box::new(local(p, mp)), index: Box::new(usz(1)), checked: false },
            )),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 2);
}

#[test]
fn misaligned_cast_traps() {
    let at = Ty::array(8, Ty::u8());
    let mk = |off: i128| {
        run_body(Ty::bool(), |f| {
            let a = f.local("a", at);
            let p = f.local("p", Ty::ptr(false, Ty::u8()));
            let pu8 = Ty::ptr(false, Ty::u8());
            let target = Ty::ptr(false, Ty::u32());
            blk(
                vec![
                    let_(a, ex(at, ExprKind::ArrayFill(Box::new(u8e(0))))),
                    let_(p, addr_of(index(local(a, at), usz(off)))),
                ],
                Some(eq(
                    builtin(Builtin::Cast, vec![local(p, pu8)], target),
                    builtin(Builtin::Cast, vec![local(p, pu8)], target),
                )),
                Ty::bool(),
            )
        })
    };
    assert!(mk(4).is_ok());
    assert!(trap_msg(mk(1)).contains("misaligned pointer cast"));
}

#[test]
fn constants_are_read_only_and_memoized() {
    let mut tb = Tb::new();
    let c = tb.add_const("K", Ty::i32(), add(i32e(40), i32e(2)));
    let k = ex(Ty::i32(), ExprKind::Const(c));
    assert_eq!(int_of(&tb.eval(&k).unwrap()), 42);
    // Writing through a pointer to a constant's storage traps.
    let e = run_in(&mut tb, Ty::unit(), |_| blk(vec![assign(deref(addr_of(k.clone())), i32e(5))], None, Ty::unit()));
    assert!(trap_msg(e).contains("read-only"));
    // The storage is stable: two borrows are the same address.
    let same = eq(
        builtin(Builtin::Addr, vec![addr_of(k.clone())], Ty::usize()),
        builtin(Builtin::Addr, vec![addr_of(k)], Ty::usize()),
    );
    assert!(bool_of(&tb.eval(&same).unwrap()));
}

// ---------------------------------------------------------------------------
// Optionals and results
// ---------------------------------------------------------------------------

#[test]
fn else_on_optionals_and_results() {
    let oi = Ty::optional(Ty::i32());
    let els =
        |lhs: Expr, rhs: Expr, ty: Ty| ex(ty, ExprKind::Else { lhs: Box::new(lhs), capture: None, rhs: Box::new(rhs) });
    assert_eq!(eval_int(&els(null(oi), i32e(5), Ty::i32())), 5);
    assert_eq!(eval_int(&els(some(i32e(3)), i32e(5), Ty::i32())), 3);
    // else with an optional fallback gives an optional
    let v = Tb::new().eval(&els(null(oi), some(i32e(9)), oi)).unwrap();
    assert_eq!(v.bytes.len(), 8);
    // result with error capture
    let r = Ty::result(Ty::u8(), Ty::i32());
    let e = {
        let mut f = FnB::new("t", Ty::i32());
        let cap = f.local("e", Ty::u8());
        let rhs = coerce(Coercion::IntWiden, local(cap, Ty::u8()), Ty::i32());
        let body =
            ex(Ty::i32(), ExprKind::Else { lhs: Box::new(err(u8e(7), r)), capture: Some(cap), rhs: Box::new(rhs) });
        let mut tb = Tb::new();
        let id = tb.add(f.build(body));
        tb.eval(&call(id, vec![], Ty::i32())).unwrap()
    };
    assert_eq!(int_of(&e), 7);
    assert_eq!(eval_int(&els(ok(i32e(4), r), i32e(5), Ty::i32())), 4);
    // `else` with `never` fallback
    let mut tb = Tb::new();
    let e = run_in(&mut tb, Ty::i32(), |_| els(null(oi), ret(0, i32e(77)), Ty::i32()));
    assert_eq!(int_of(&e.unwrap()), 77);
}

#[test]
fn try_propagates_optionals_results_and_lifts() {
    let mut tb = Tb::new();
    // optional
    let oi = Ty::optional(Ty::i32());
    let tr = |e: Expr, lift: Option<Lift>, ty: Ty| ex(ty, ExprKind::Try { expr: Box::new(e), lift, levels: 0 });
    let v = run_in(&mut tb, oi, |_| blk(vec![stmt(tr(null(oi), None, Ty::i32()))], Some(some(i32e(1))), oi)).unwrap();
    assert!(v.bytes[4] == 0, "expected null");
    let v =
        run_in(&mut tb, oi, |_| blk(vec![], Some(some(add(tr(some(i32e(2)), None, Ty::i32()), i32e(1)))), oi)).unwrap();
    assert!(
        v.bytes[4] == 1 && int_of(&ConstValue { ty: Ty::i32(), bytes: v.bytes[..4].to_vec(), relocs: vec![] }) == 3
    );

    // result with lifting: Parse(u8) | Io(i32)
    let f_ty = tb.add_union("LoadError", &[("Parse", Some(Ty::u8())), ("Io", Some(Ty::i32())), ("Missing", None)]);
    let inner = Ty::result(Ty::u8(), Ty::i32());
    let outer = Ty::result(f_ty, Ty::i32());
    let expected_err = err(ex(f_ty, ExprKind::Variant { variant: 0, payload: Some(Box::new(u8e(3))) }), outer);
    let got = run_in(&mut tb, outer, |_| {
        blk(vec![stmt(tr(err(u8e(3), inner), Some(Lift::Variant(0)), Ty::i32()))], Some(ok(i32e(0), outer)), outer)
    })
    .unwrap();
    let want = tb.eval(&expected_err).unwrap();
    assert_eq!(got.bytes, want.bytes);
    // success passes through
    let got = run_in(&mut tb, outer, |_| {
        blk(vec![], Some(ok(add(tr(ok(i32e(5), inner), Some(Lift::Variant(0)), Ty::i32()), i32e(1)), outer)), outer)
    })
    .unwrap();
    let want = tb.eval(&ok(i32e(6), outer)).unwrap();
    assert_eq!(got.bytes, want.bytes);
}

#[test]
fn unwrap_checks() {
    let oi = Ty::optional(Ty::i32());
    let uw = |e: Expr, is_result: bool, ty: Ty| ex(ty, ExprKind::Unwrap { expr: Box::new(e), is_result });
    assert_eq!(eval_int(&uw(some(i32e(4)), false, Ty::i32())), 4);
    assert!(eval_trap(&uw(null(oi), false, Ty::i32())).contains("unwrap of null"));
    let mut tb = Tb::new();
    let e = tb.add_union("E", &[("NotFound", None), ("Other", Some(Ty::i32()))]);
    let r = Ty::result(e, Ty::i32());
    let nf = ex(e, ExprKind::Variant { variant: 0, payload: None });
    let m = trap_msg(tb.eval(&uw(err(nf, r), true, Ty::i32())));
    assert!(m.contains("unwrap of error value .NotFound"), "{}", m);
    let other = ex(e, ExprKind::Variant { variant: 1, payload: Some(Box::new(i32e(9))) });
    let m = trap_msg(tb.eval(&uw(err(other, r), true, Ty::i32())));
    assert!(m.contains("unwrap of error value .Other(9)"), "{}", m);
    assert_eq!(int_of(&tb.eval(&uw(ok(i32e(8), r), true, Ty::i32())).unwrap()), 8);
    // Fast mode: unspecified value instead of a trap
    let v = tb.eval_mode(&uw(null(oi), false, Ty::i32()), Mode::Fast);
    assert!(v.is_err(), "eval_const always checks");
}

#[test]
fn nested_optionals() {
    let oi = Ty::optional(Ty::i32());
    let ooi = Ty::optional(oi);
    // ??i32: outer null vs inner null
    let outer_null = null(ooi);
    let inner_null = some(null(oi));
    assert!(!bool_of(&Tb::new().eval(&eq(outer_null.clone(), inner_null.clone())).unwrap()));
    assert!(bool_of(&Tb::new().eval(&eq(inner_null.clone(), inner_null)).unwrap()));
    let uw = ex(oi, ExprKind::Unwrap { expr: Box::new(some(null(oi))), is_result: false });
    let v = Tb::new().eval(&uw).unwrap();
    assert_eq!(v.bytes[4], 0);
    let _ = outer_null;
}

#[test]
fn optional_pointers_use_null_address() {
    let pt = Ty::ptr(false, Ty::i32());
    let opt = Ty::optional(pt);
    let v = Tb::new().eval(&null(opt)).unwrap();
    assert_eq!(v.bytes.to_vec(), vec![0; 8]);
    // A pointer to a local cannot escape compile-time evaluation (spec 17.4).
    let e = run_body(opt, |f| {
        let x = f.local("x", Ty::i32());
        blk(vec![let_(x, i32e(1))], Some(some(addr_of(local(x, Ty::i32())))), opt)
    });
    assert!(trap_msg(e).contains("does not outlive compile-time evaluation"));
}

// ---------------------------------------------------------------------------
// Patterns and match
// ---------------------------------------------------------------------------

fn match_int(scrut: Expr, arms: Vec<Arm>) -> Expr {
    match_(scrut, arms, Ty::i32())
}

#[test]
fn match_literals_ranges_bindings_guards() {
    let t = Ty::i32();
    let run = |x: i128| {
        let mut f = FnB::new("m", Ty::i32());
        let b = f.local("b", t);
        let body = match_int(
            i32e(x),
            vec![
                arm(pat(t, PatKind::Const(Box::new(i32e(1)))), None, i32e(100)),
                arm(
                    pat(t, PatKind::Range { lo: BigInt::from(2), hi: BigInt::from(5), inclusive: false }),
                    None,
                    i32e(200),
                ),
                arm(
                    pat(t, PatKind::Range { lo: BigInt::from(5), hi: BigInt::from(7), inclusive: true }),
                    None,
                    i32e(300),
                ),
                arm(bind(b, t), Some(lt(local(b, t), i32e(0))), i32e(-1)),
                arm(bind(b, t), None, add(local(b, t), i32e(1000))),
            ],
        );
        let mut tb = Tb::new();
        let id = tb.add(f.build(body));
        int_of(&tb.eval(&call(id, vec![], t)).unwrap())
    };
    assert_eq!(run(1), 100);
    assert_eq!(run(2), 200);
    assert_eq!(run(4), 200);
    assert_eq!(run(5), 300);
    assert_eq!(run(7), 300);
    assert_eq!(run(8), 1008);
    assert_eq!(run(-3), -1);
    assert_eq!(run(0), 1000);
}

#[test]
fn match_unions_structs_tuples_optionals_results_strings() {
    let mut tb = Tb::new();
    let shape = tb.add_union(
        "Shape",
        &[("Circle", Some(Ty::i32())), ("Rect", Some(Ty::tuple(vec![Ty::i32(), Ty::i32()]))), ("Empty", None)],
    );
    let circle = |r| ex(shape, ExprKind::Variant { variant: 0, payload: Some(Box::new(i32e(r))) });
    let rect =
        |w, h| ex(shape, ExprKind::Variant { variant: 1, payload: Some(Box::new(tuple(vec![i32e(w), i32e(h)]))) });
    let empty = ex(shape, ExprKind::Variant { variant: 2, payload: None });
    let mut area = |s: Expr| {
        let mut f = FnB::new("area", Ty::i32());
        let r = f.local("r", Ty::i32());
        let w = f.local("w", Ty::i32());
        let h = f.local("h", Ty::i32());
        let tup = Ty::tuple(vec![Ty::i32(), Ty::i32()]);
        let it = Ty::i32();
        let body = match_int(
            s,
            vec![
                arm(
                    pat(shape, PatKind::Variant { variant: 0, payload: Some(Box::new(bind(r, it))) }),
                    None,
                    mul(local(r, it), local(r, it)),
                ),
                arm(
                    pat(
                        shape,
                        PatKind::Variant {
                            variant: 1,
                            payload: Some(Box::new(pat(
                                tup,
                                PatKind::Fields(vec![(0, bind(w, it)), (1, bind(h, it))]),
                            ))),
                        },
                    ),
                    Some(eq(local(w, it), local(h, it))),
                    i32e(-1),
                ),
                arm(
                    pat(
                        shape,
                        PatKind::Variant {
                            variant: 1,
                            payload: Some(Box::new(pat(
                                tup,
                                PatKind::Fields(vec![(0, bind(w, it)), (1, bind(h, it))]),
                            ))),
                        },
                    ),
                    None,
                    mul(local(w, it), local(h, it)),
                ),
                arm(pat(shape, PatKind::Variant { variant: 2, payload: None }), None, i32e(0)),
            ],
        );
        let id = tb.add(f.build(body));
        int_of(&tb.eval(&call(id, vec![], it)).unwrap())
    };
    assert_eq!(area(circle(5)), 25);
    assert_eq!(area(rect(2, 3)), 6);
    assert_eq!(area(rect(4, 4)), -1);
    assert_eq!(area(empty), 0);

    // struct fields, optionals and results
    let it = Ty::i32();
    let st = tb.add_struct("P", &[("x", it), ("y", it)]);
    let oi = Ty::optional(it);
    let r = Ty::result(Ty::u8(), it);
    let mut f = FnB::new("misc", it);
    let a = f.local("a", it);
    let b = f.local("b", it);
    let sp = ex(st, ExprKind::Aggregate(vec![i32e(3), i32e(4)]));
    let body = blk(
        vec![],
        Some(add(
            add(
                match_int(sp, vec![arm(pat(st, PatKind::Fields(vec![(1, bind(a, it))])), None, local(a, it))]),
                match_int(
                    some(i32e(10)),
                    vec![
                        arm(pat(oi, PatKind::Null), None, i32e(-100)),
                        arm(pat(oi, PatKind::Some(Box::new(bind(b, it)))), None, local(b, it)),
                    ],
                ),
            ),
            match_int(
                err(u8e(2), r),
                vec![
                    arm(pat(r, PatKind::Ok(Box::new(wild(it)))), None, i32e(1000)),
                    arm(
                        pat(r, PatKind::Err(Box::new(pat(Ty::u8(), PatKind::Const(Box::new(u8e(2))))))),
                        None,
                        i32e(5000),
                    ),
                    arm(wild(r), None, i32e(0)),
                ],
            ),
        )),
        it,
    );
    let id = tb.add(f.build(body));
    assert_eq!(int_of(&tb.eval(&call(id, vec![], it)).unwrap()), 4 + 10 + 5000);

    // strings
    let s = Ty::string();
    let m = match_int(
        string("hello"),
        vec![
            arm(pat(s, PatKind::Const(Box::new(string("world")))), None, i32e(1)),
            arm(pat(s, PatKind::Const(Box::new(string("hello")))), None, i32e(2)),
            arm(wild(s), None, i32e(3)),
        ],
    );
    assert_eq!(eval_int(&m), 2);
}

#[test]
fn if_binding_conditions() {
    let it = Ty::i32();
    let oi = Ty::optional(it);
    let mut f = FnB::new("t", it);
    let x = f.local("x", it);
    let y = f.local("y", it);
    // if x := some(3) and y := some(4) and x < y { x + y } else { -1 }
    let cond = vec![
        CondPart::Match { pat: pat(oi, PatKind::Some(Box::new(bind(x, it)))), value: some(i32e(3)) },
        CondPart::Match { pat: pat(oi, PatKind::Some(Box::new(bind(y, it)))), value: some(i32e(4)) },
        CondPart::Bool(lt(local(x, it), local(y, it))),
    ];
    let body =
        ex(it, ExprKind::If { cond, then: Box::new(add(local(x, it), local(y, it))), else_: Some(Box::new(i32e(-1))) });
    let mut tb = Tb::new();
    let id = tb.add(f.build(body));
    assert_eq!(int_of(&tb.eval(&call(id, vec![], it)).unwrap()), 7);
    // failing second part goes to else
    let mut f = FnB::new("t2", it);
    let x = f.local("x", it);
    let cond = vec![
        CondPart::Match { pat: pat(oi, PatKind::Some(Box::new(bind(x, it)))), value: some(i32e(3)) },
        CondPart::Match { pat: pat(oi, PatKind::Some(Box::new(wild(it)))), value: null(oi) },
    ];
    let body = ex(it, ExprKind::If { cond, then: Box::new(local(x, it)), else_: Some(Box::new(i32e(-1))) });
    let id = tb.add(f.build(body));
    assert_eq!(int_of(&tb.eval(&call(id, vec![], it)).unwrap()), -1);
}

#[test]
fn let_tuple_and_assign_tuple() {
    let it = Ty::i32();
    let tt = Ty::tuple(vec![it, it]);
    let v = run_body(it, |f| {
        let a = f.local("a", it);
        let b = f.local("b", it);
        blk(
            vec![
                Stmt::LetTuple { locals: vec![Some(a), Some(b)], init: tuple(vec![i32e(1), i32e(2)]) },
                // swap
                Stmt::AssignTuple {
                    places: vec![Some(local(a, it)), Some(local(b, it))],
                    value: tuple(vec![local(b, it), local(a, it)]),
                },
            ],
            Some(add(mul(local(a, it), i32e(10)), local(b, it))),
            it,
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 21);
    let _ = tt;
}

#[test]
fn bits_structs() {
    let mut tb = Tb::new();
    // Mode :: #bits(u16) struct { read, write: bool; kind: u4; _: u10 }
    let u4 = Ty::int(false, 4);
    let m = tb.add_bits_struct(
        "Mode",
        Ty::int(false, 16),
        &[("read", Ty::bool()), ("write", Ty::bool()), ("kind", u4), ("_", Ty::int(false, 10))],
    );
    let make = ex(m, ExprKind::Aggregate(vec![boolean(true), boolean(false), int(9, u4), int(0, Ty::int(false, 10))]));
    // convert to the underlying integer
    let as_int = builtin(Builtin::Convert, vec![make.clone()], Ty::int(false, 16));
    assert_eq!(int_of(&tb.eval(&as_int).unwrap()), 1 | (9 << 2));
    // field read
    assert_eq!(int_of(&tb.eval(&field(make.clone(), 2, u4)).unwrap()), 9);
    assert!(bool_of(&tb.eval(&field(make.clone(), 0, Ty::bool())).unwrap()));
    // field write through a place
    let v = run_in(&mut tb, Ty::int(false, 16), |f| {
        let a = f.local("a", m);
        blk(
            vec![
                let_(a, make.clone()),
                assign(field(local(a, m), 1, Ty::bool()), boolean(true)),
                assign(field(local(a, m), 2, u4), int(3, u4)),
            ],
            Some(builtin(Builtin::Convert, vec![local(a, m)], Ty::int(false, 16))),
            Ty::int(false, 16),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 1 | 2 | (3 << 2));
    // bitwise or on bits structs
    let or = bin(BinOp::BitOr, make.clone(), make);
    assert_eq!(int_of(&tb.eval(&builtin(Builtin::Convert, vec![or], Ty::int(false, 16))).unwrap()), 1 | (9 << 2));
}

#[test]
fn builtins_numeric() {
    let b = |op, args, ty| Tb::new().eval(&builtin(op, args, ty));
    // trunc / checked / sat on integers
    assert_eq!(int_of(&b(Builtin::Trunc, vec![int(300, Ty::int(false, 16))], Ty::u8()).unwrap()), 44);
    assert_eq!(int_of(&b(Builtin::Sat, vec![int(300, Ty::int(false, 16))], Ty::u8()).unwrap()), 255);
    assert_eq!(int_of(&b(Builtin::Sat, vec![int(-3, Ty::i32())], Ty::u8()).unwrap()), 0);
    let ou8 = Ty::optional(Ty::u8());
    let v = b(Builtin::Checked, vec![int(300, Ty::int(false, 16))], ou8).unwrap();
    assert_eq!(v.bytes[1], 0);
    let v = b(Builtin::Checked, vec![int(200, Ty::int(false, 16))], ou8).unwrap();
    assert_eq!((v.bytes[0], v.bytes[1]), (200, 1));
    // conversions
    assert_eq!(int_of(&b(Builtin::Convert, vec![u8e(200)], Ty::i32()).unwrap()), 200);
    assert_eq!(int_of(&b(Builtin::Convert, vec![boolean(true)], Ty::u8()).unwrap()), 1);
    assert_eq!(f64_of(&b(Builtin::Convert, vec![i32e(-7)], Ty::f64()).unwrap()), -7.0);
    // float -> int
    let f = Ty::f64();
    assert_eq!(int_of(&b(Builtin::Trunc, vec![flt(-2.7, f)], Ty::i32()).unwrap()), -2);
    assert_eq!(int_of(&b(Builtin::Round, vec![flt(2.5, f)], Ty::i32()).unwrap()), 2);
    assert_eq!(int_of(&b(Builtin::Round, vec![flt(3.5, f)], Ty::i32()).unwrap()), 4);
    assert_eq!(int_of(&b(Builtin::Floor, vec![flt(-2.5, f)], Ty::i32()).unwrap()), -3);
    assert_eq!(int_of(&b(Builtin::Ceil, vec![flt(2.1, f)], Ty::i32()).unwrap()), 3);
    let m = trap_msg(b(Builtin::Trunc, vec![flt(1e10, f)], Ty::i32()));
    assert!(m.contains("float to integer conversion out of range"));
    assert!(
        trap_msg(b(Builtin::Trunc, vec![bin(BinOp::Div, flt(0.0, f), flt(0.0, f))], Ty::i32())).contains("conversion")
    );
    assert_eq!(int_of(&b(Builtin::Sat, vec![flt(1e10, f)], Ty::i32()).unwrap()), i32::MAX as i128);
    assert_eq!(int_of(&b(Builtin::Sat, vec![bin(BinOp::Div, flt(0.0, f), flt(0.0, f))], Ty::i32()).unwrap()), 0);
    // int -> float rounding and f64 -> f32
    let f32t = Ty::intern(TyKind::Float(FloatTy::F32));
    assert_eq!(f64_of(&b(Builtin::Round, vec![int(16777217, Ty::i32())], f32t).unwrap()), 16777216.0);
    assert_eq!(f64_of(&b(Builtin::Round, vec![flt(0.1, f)], f32t).unwrap()), 0.1f32 as f64);
    // bits reinterpretation
    assert_eq!(int_of(&b(Builtin::Bits, vec![flt(1.0, f32t)], Ty::u32()).unwrap()), 0x3f800000);
    // min / max / clamp / abs / abs_diff / pow
    assert_eq!(int_of(&b(Builtin::Min, vec![i32e(3), i32e(-4)], Ty::i32()).unwrap()), -4);
    assert_eq!(int_of(&b(Builtin::Max, vec![i32e(3), i32e(-4)], Ty::i32()).unwrap()), 3);
    assert_eq!(int_of(&b(Builtin::Clamp, vec![i32e(30), i32e(0), i32e(10)], Ty::i32()).unwrap()), 10);
    assert_eq!(int_of(&b(Builtin::Abs, vec![i32e(-9)], Ty::i32()).unwrap()), 9);
    assert!(trap_msg(b(Builtin::Abs, vec![i32e(i32::MIN as i128)], Ty::i32())).contains("overflow"));
    assert_eq!(int_of(&b(Builtin::AbsDiff, vec![i32e(-5), i32e(7)], Ty::u32()).unwrap()), 12);
    assert_eq!(int_of(&b(Builtin::Pow(Flavor::Checked), vec![i32e(3), int(4, Ty::u32())], Ty::i32()).unwrap()), 81);
    assert!(
        trap_msg(b(Builtin::Pow(Flavor::Checked), vec![i32e(10), int(10, Ty::u32())], Ty::i32())).contains("overflow")
    );
    assert_eq!(
        int_of(&b(Builtin::Pow(Flavor::Sat), vec![i32e(10), int(10, Ty::u32())], Ty::i32()).unwrap()),
        i32::MAX as i128
    );
    assert_eq!(int_of(&b(Builtin::Pow(Flavor::Wrap), vec![u8e(3), int(6, Ty::u32())], Ty::u8()).unwrap()), 729 % 256);
    // floats
    assert_eq!(f64_of(&b(Builtin::Sqrt, vec![flt(9.0, f)], f).unwrap()), 3.0);
    assert!(bool_of(&b(Builtin::IsNan, vec![bin(BinOp::Div, flt(0.0, f), flt(0.0, f))], Ty::bool()).unwrap()));
    assert!(bool_of(&b(Builtin::IsInf, vec![bin(BinOp::Div, flt(1.0, f), flt(0.0, f))], Ty::bool()).unwrap()));
    assert_eq!(f64_of(&b(Builtin::Fma, vec![flt(2.0, f), flt(3.0, f), flt(1.0, f)], f).unwrap()), 7.0);
    assert_eq!(f64_of(&b(Builtin::Min, vec![flt(1.0, f), bin(BinOp::Div, flt(0.0, f), flt(0.0, f))], f).unwrap()), 1.0);
    // bit counting
    assert_eq!(int_of(&b(Builtin::Clz, vec![u8e(1)], Ty::u8()).unwrap()), 7);
    assert_eq!(int_of(&b(Builtin::Ctz, vec![u8e(8)], Ty::u8()).unwrap()), 3);
    assert_eq!(int_of(&b(Builtin::Ctz, vec![u8e(0)], Ty::u8()).unwrap()), 8);
    assert_eq!(int_of(&b(Builtin::Popcount, vec![u8e(0xF3)], Ty::u8()).unwrap()), 6);
    assert_eq!(int_of(&b(Builtin::Bswap, vec![int(0x1234, Ty::int(false, 16))], Ty::int(false, 16)).unwrap()), 0x3412);
    assert_eq!(int_of(&b(Builtin::Bitreverse, vec![u8e(0b0000_0110)], Ty::u8()).unwrap()), 0b0110_0000);
}

#[test]
fn union_tag_builtins() {
    let mut tb = Tb::new();
    let dir = tb.add_union("Dir", &[("N", None), ("E", None), ("S", None)]);
    let v = ex(dir, ExprKind::Variant { variant: 2, payload: None });
    assert_eq!(int_of(&tb.eval(&builtin(Builtin::Tag, vec![v.clone()], Ty::u8())).unwrap()), 2);
    // from_tag: 1 is E, 7 is not a tag
    let od = Ty::optional(dir);
    let got = tb.eval(&builtin(Builtin::FromTag, vec![u8e(1)], od)).unwrap();
    let want = tb.eval(&some(ex(dir, ExprKind::Variant { variant: 1, payload: None }))).unwrap();
    assert_eq!(got.bytes, want.bytes);
    let got = tb.eval(&builtin(Builtin::FromTag, vec![u8e(7)], od)).unwrap();
    assert_eq!(got.bytes, tb.eval(&null(od)).unwrap().bytes);
    // enum -> integer conversion
    assert_eq!(int_of(&tb.eval(&builtin(Builtin::Convert, vec![v], Ty::i32())).unwrap()), 2);
    // get / get_ptr
    let arr = array(vec![i32e(10), i32e(20)]);
    let oi = Ty::optional(Ty::i32());
    let got = tb.eval(&builtin(Builtin::Get, vec![arr.clone(), usz(1)], oi)).unwrap();
    assert_eq!((got.bytes[4], i32::from_le_bytes(got.bytes[..4].try_into().unwrap())), (1, 20));
    let got = tb.eval(&builtin(Builtin::Get, vec![arr, usz(2)], oi)).unwrap();
    assert_eq!(got.bytes[4], 0);
}
