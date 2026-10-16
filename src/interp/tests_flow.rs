//! Tests: loops, labels, defer/errdefer, effects, closures.

use super::testkit::*;
use super::*;

fn u32e(v: i128) -> Expr {
    int(v, Ty::u32())
}

fn ptr_u32() -> Ty {
    Ty::ptr(false, Ty::u32())
}

/// `p^ = p^ + n` for `p: ^u32`.
fn bump(p: LocalId, n: i128) -> Stmt {
    assign(deref(local(p, ptr_u32())), add(deref(local(p, ptr_u32())), u32e(n)))
}

fn plus1(l: LocalId) -> Stmt {
    assign(local(l, Ty::usize()), add(local(l, Ty::usize()), usz(1)))
}

fn step_inc(l: LocalId) -> Expr {
    block_unit(vec![plus1(l)])
}

// ---------------------------------------------------------------------------
// Loops
// ---------------------------------------------------------------------------

#[test]
fn while_loop_with_step_and_continue() {
    let t = Ty::usize();
    let v = run_body(t, |f| {
        let i = f.local("i", t);
        let s = f.local("s", t);
        let cond = vec![CondPart::Bool(lt(local(i, t), usz(10)))];
        // sum of odd numbers below 10
        let body = vec![
            stmt(if_(eq(bin(BinOp::Rem, local(i, t), usz(2)), usz(0)), cont(lbl(0, 0)), None, Ty::unit())),
            assign(local(s, t), add(local(s, t), local(i, t))),
        ];
        blk(vec![let_(i, usz(0)), let_(s, usz(0)), stmt(loop_(0, cond, body, Some(step_inc(i))))], Some(local(s, t)), t)
    })
    .unwrap();
    assert_eq!(int_of(&v), 25);
}

#[test]
fn break_leaves_without_running_step() {
    let t = Ty::usize();
    let v = run_body(t, |f| {
        let i = f.local("i", t);
        let cond = vec![CondPart::Bool(boolean(true))];
        let body = vec![stmt(if_(eq(local(i, t), usz(3)), brk(lbl(0, 0), None), None, Ty::unit()))];
        blk(vec![let_(i, usz(0)), stmt(loop_(0, cond, body, Some(step_inc(i))))], Some(local(i, t)), t)
    })
    .unwrap();
    assert_eq!(int_of(&v), 3);
}

#[test]
fn labeled_break_and_continue() {
    let t = Ty::usize();
    let nested = |continue_outer: bool| {
        run_body(t, |f| {
            let i = f.local("i", t);
            let j = f.local("j", t);
            let count = f.local("count", t);
            let inner_body = vec![
                if continue_outer {
                    stmt(if_(eq(local(j, t), usz(1)), cont(lbl(0, 0)), None, Ty::unit()))
                } else {
                    stmt(if_(eq(mul(local(i, t), local(j, t)), usz(6)), brk(lbl(0, 0), None), None, Ty::unit()))
                },
                plus1(count),
            ];
            let inner = loop_(1, vec![CondPart::Bool(lt(local(j, t), usz(5)))], inner_body, Some(step_inc(j)));
            let outer_body = vec![assign(local(j, t), usz(0)), stmt(inner)];
            blk(
                vec![
                    let_(i, usz(0)),
                    let_(j, usz(0)),
                    let_(count, usz(0)),
                    stmt(loop_(0, vec![CondPart::Bool(lt(local(i, t), usz(5)))], outer_body, Some(step_inc(i)))),
                ],
                Some(local(count, t)),
                t,
            )
        })
        .unwrap()
    };
    assert_eq!(int_of(&nested(false)), 13);
    assert_eq!(int_of(&nested(true)), 5);
}

#[test]
fn while_with_binding_condition() {
    let it = Ty::i32();
    let oi = Ty::optional(it);
    let v = run_body(it, |f| {
        let o = f.local("o", oi);
        let x = f.local("x", it);
        let s = f.local("s", it);
        let cond = vec![CondPart::Match { pat: pat(oi, PatKind::Some(Box::new(bind(x, it)))), value: local(o, oi) }];
        let body = vec![assign(local(s, it), add(local(s, it), local(x, it))), assign(local(o, oi), null(oi))];
        blk(vec![let_(o, some(i32e(3))), let_(s, i32e(0)), stmt(loop_(0, cond, body, None))], Some(local(s, it)), it)
    })
    .unwrap();
    assert_eq!(int_of(&v), 3);
}

// ---------------------------------------------------------------------------
// defer / errdefer
// ---------------------------------------------------------------------------

/// Runs `callee(&x)` and returns `x`.
fn run_with_counter(tb: &mut Tb, callee: FnId, extra: Vec<Expr>) -> u32 {
    let v = run_in(tb, Ty::u32(), |f| {
        let x = f.local("x", Ty::u32());
        let mut args = vec![addr_of(local(x, Ty::u32()))];
        args.extend(extra);
        blk(vec![let_(x, u32e(0)), stmt(call(callee, args, Ty::unit()))], Some(local(x, Ty::u32())), Ty::u32())
    })
    .unwrap();
    int_of(&v) as u32
}

#[test]
fn defers_run_in_reverse_order() {
    let mut tb = Tb::new();
    let mut f = FnB::new("f", Ty::unit());
    let p = f.param("p", ptr_u32());
    let upd = |n: i128| assign(deref(local(p, ptr_u32())), add(mul(deref(local(p, ptr_u32())), u32e(10)), u32e(n)));
    let body = blk(vec![defer(block_unit(vec![upd(1)])), defer(block_unit(vec![upd(2)]))], None, Ty::unit());
    let id = tb.add(f.build(body));
    assert_eq!(run_with_counter(&mut tb, id, vec![]), 21);
}

#[test]
fn defer_runs_on_return_and_value_is_preserved() {
    let mut tb = Tb::new();
    let mut f = FnB::new("f", Ty::i32());
    let p = f.param("p", ptr_u32());
    let body = blk(vec![defer(block_unit(vec![bump(p, 5)])), stmt(ret(0, i32e(42)))], None, Ty::i32());
    let id = tb.add(f.build(body));
    let v = run_in(&mut tb, Ty::i32(), |g| {
        let x = g.local("x", Ty::u32());
        let r = g.local("r", Ty::i32());
        blk(
            vec![let_(x, u32e(0)), let_(r, call(id, vec![addr_of(local(x, Ty::u32()))], Ty::i32()))],
            Some(add(local(r, Ty::i32()), builtin(Builtin::Trunc, vec![local(x, Ty::u32())], Ty::i32()))),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 47);
}

#[test]
fn errdefer_runs_only_for_error_returns() {
    let mut tb = Tb::new();
    let r = Ty::result(Ty::u8(), Ty::unit());
    // fail=true: `return !1`; fail=false: falls off the end with a success.
    let mut f = FnB::new("g", r);
    let p = f.param("p", ptr_u32());
    let fail = f.param("fail", Ty::bool());
    let body = blk(
        vec![
            errdefer(block_unit(vec![bump(p, 100)])),
            defer(block_unit(vec![bump(p, 1)])),
            stmt(if_(local(fail, Ty::bool()), ret(0, err(u8e(1), r)), None, Ty::unit())),
        ],
        Some(ok(unit(), r)),
        r,
    );
    let id = tb.add(f.build(body));
    assert_eq!(run_with_counter(&mut tb, id, vec![boolean(true)]), 101);
    assert_eq!(run_with_counter(&mut tb, id, vec![boolean(false)]), 1);

    // `return ok` explicitly also does not run errdefer
    let mut f = FnB::new("g2", r);
    let p = f.param("p", ptr_u32());
    let body = blk(vec![errdefer(block_unit(vec![bump(p, 100)])), stmt(ret(0, ok(unit(), r)))], None, r);
    let id = tb.add(f.build(body));
    assert_eq!(run_with_counter(&mut tb, id, vec![]), 0);

    // returning null from an optional-returning function is not an error
    let oi = Ty::optional(Ty::i32());
    let mut f = FnB::new("g3", oi);
    let p = f.param("p", ptr_u32());
    let body = blk(vec![errdefer(block_unit(vec![bump(p, 100)])), stmt(ret(0, null(oi)))], None, oi);
    let id = tb.add(f.build(body));
    assert_eq!(run_with_counter(&mut tb, id, vec![]), 0);

    // try propagating an error runs errdefer
    let mut f = FnB::new("g4", r);
    let p = f.param("p", ptr_u32());
    let body = blk(
        vec![
            errdefer(block_unit(vec![bump(p, 100)])),
            stmt(ex(Ty::u8(), ExprKind::Try { expr: Box::new(err(u8e(1), r)), lift: None, levels: 0 })),
        ],
        Some(ok(unit(), r)),
        r,
    );
    let id = tb.add(f.build(body));
    assert_eq!(run_with_counter(&mut tb, id, vec![]), 100);
}

#[test]
fn errdefer_skipped_by_break_and_continue() {
    let mut tb = Tb::new();
    let mut f = FnB::new("f", Ty::unit());
    let p = f.param("p", ptr_u32());
    let i = f.local("i", Ty::usize());
    let t = Ty::usize();
    let body_stmts = vec![
        errdefer(block_unit(vec![bump(p, 100)])),
        defer(block_unit(vec![bump(p, 1)])),
        stmt(if_(eq(local(i, t), usz(2)), brk(lbl(0, 0), None), None, Ty::unit())),
        plus1(i),
        stmt(cont(lbl(0, 0))),
    ];
    let body = blk(
        vec![let_(i, usz(0)), stmt(loop_(0, vec![CondPart::Bool(boolean(true))], body_stmts, None))],
        None,
        Ty::unit(),
    );
    let id = tb.add(f.build(body));
    // three iterations, each running its `defer` once, no errdefer
    assert_eq!(run_with_counter(&mut tb, id, vec![]), 3);
}

/// Effect `Esc { bail: esc(x: i32) -> never }` whose handler exits the `with` with `x`.
fn esc_setup(tb: &mut Tb) -> (EffectRef, HandlerId) {
    let e = effect_ref("Esc");
    let mut c = FnB::new("bail", Ty::never()).kind(FnKind::Clause);
    let x = c.param("x", Ty::i32());
    let body = brk(crate::tir::Target::InstallingWith, Some(local(x, Ty::i32())));
    let cid = tb.add(c.build(body));
    let h = tb.add_handler(&e, vec![cid]);
    (e, h)
}

#[test]
fn esc_exit_unwinds_frames_running_defers_and_errdefers() {
    let mut tb = Tb::new();
    let (e, h) = esc_setup(&mut tb);
    // g2 performs bail(7)
    let mut g2 = FnB::new("g2", Ty::unit());
    let p2 = g2.param("p", ptr_u32());
    let body = blk(
        vec![errdefer(block_unit(vec![bump(p2, 100_000)])), defer(block_unit(vec![bump(p2, 100)]))],
        Some(perform(&e, 0, vec![i32e(7)], Ty::never())),
        Ty::unit(),
    );
    let g2id = tb.add(g2.build(body));
    // g1 calls g2
    let mut g1 = FnB::new("g1", Ty::unit());
    let p1 = g1.param("p", ptr_u32());
    let body = blk(
        vec![errdefer(block_unit(vec![bump(p1, 1_000_000)])), defer(block_unit(vec![bump(p1, 1_000)]))],
        Some(call(g2id, vec![local(p1, ptr_u32())], Ty::unit())),
        Ty::unit(),
    );
    let g1id = tb.add(g1.build(body));
    // f: with h { errdefer ..; defer ..; g1(p) }
    let mut f = FnB::new("f", Ty::i32());
    let p = f.param("p", ptr_u32());
    let inner = blk(
        vec![errdefer(block_unit(vec![bump(p, 10_000_000)])), defer(block_unit(vec![bump(p, 10)]))],
        Some(call(g1id, vec![local(p, ptr_u32())], Ty::unit())),
        Ty::unit(),
    );
    let w = with_(0, vec![HandlerValue::Literal(ex(Ty::unit(), ExprKind::HandlerRef(h)))], inner, Ty::i32());
    let fid = tb.add(f.build(w));
    // The two inner frames run errdefer and defer; the block of `f` inside the `with` is left like
    // an ordinary break: defer only.
    let v = run_in(&mut tb, Ty::u32(), |g| {
        let x = g.local("x", Ty::u32());
        let r = g.local("r", Ty::i32());
        blk(
            vec![let_(x, u32e(0)), let_(r, call(fid, vec![addr_of(local(x, Ty::u32()))], Ty::i32()))],
            Some(add(local(x, Ty::u32()), builtin(Builtin::Trunc, vec![local(r, Ty::i32())], Ty::u32()))),
            Ty::u32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 100_000 + 100 + 1_000_000 + 1_000 + 10 + 7);
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------

fn handler_ref(h: HandlerId) -> HandlerValue {
    HandlerValue::Literal(ex(Ty::unit(), ExprKind::HandlerRef(h)))
}

#[test]
fn handler_literal_fn_operation() {
    let mut tb = Tb::new();
    let e = effect_ref("Log");
    let mut c = FnB::new("log", Ty::i32()).kind(FnKind::Clause);
    let n = c.param("n", Ty::i32());
    let cid = tb.add(c.build(add(local(n, Ty::i32()), i32e(1))));
    let h = tb.add_handler(&e, vec![cid]);
    let w = with_(0, vec![handler_ref(h)], perform(&e, 0, vec![i32e(41)], Ty::i32()), Ty::i32());
    assert_eq!(int_of(&tb.eval(&w).unwrap()), 42);
}

#[test]
fn handler_literal_captures_and_mutates_locals() {
    let mut tb = Tb::new();
    let e = effect_ref("Log");
    let cnt_ty = Ty::u32();
    let mut c = FnB::new("log", Ty::unit()).kind(FnKind::Clause);
    let n = c.param("n", cnt_ty);
    let cid_next = tb.next_fn();
    let _ = cid_next;
    // clause body: cnt += n  (cnt is local 0 of the enclosing function, one level up)
    let body = assign(upvar(1, LocalId(0), cnt_ty), add(upvar(1, LocalId(0), cnt_ty), local(n, cnt_ty)));
    let cid = tb.add(c.build(blk(vec![body], None, Ty::unit())));
    let h = tb.add_handler(&e, vec![cid]);
    let v = run_in(&mut tb, cnt_ty, |f| {
        let cnt = f.local("cnt", cnt_ty);
        assert_eq!(cnt, LocalId(0));
        let w = with_(
            0,
            vec![handler_ref(h)],
            blk(
                vec![stmt(perform(&e, 0, vec![u32e(5)], Ty::unit())), stmt(perform(&e, 0, vec![u32e(6)], Ty::unit()))],
                None,
                Ty::unit(),
            ),
            Ty::unit(),
        );
        blk(vec![let_(cnt, u32e(0)), stmt(w)], Some(local(cnt, cnt_ty)), cnt_ty)
    })
    .unwrap();
    assert_eq!(int_of(&v), 11);
}

#[test]
fn innermost_handler_wins_and_clauses_run_outside_their_with() {
    let mut tb = Tb::new();
    let e = effect_ref("Log");
    // H1: log(n) = n + 100
    let mut c1 = FnB::new("log1", Ty::i32()).kind(FnKind::Clause);
    let n1 = c1.param("n", Ty::i32());
    let c1id = tb.add(c1.build(add(local(n1, Ty::i32()), i32e(100))));
    let h1 = tb.add_handler(&e, vec![c1id]);
    // H2: log(n) = log(n) + 1  -- the inner `log` goes to H1, not H2 itself
    let mut c2 = FnB::new("log2", Ty::i32()).kind(FnKind::Clause);
    let n2 = c2.param("n", Ty::i32());
    let c2id = tb.add(c2.build(add(perform(&e, 0, vec![local(n2, Ty::i32())], Ty::i32()), i32e(1))));
    let h2 = tb.add_handler(&e, vec![c2id]);
    let inner = with_(0, vec![handler_ref(h2)], perform(&e, 0, vec![i32e(1)], Ty::i32()), Ty::i32());
    let outer = with_(0, vec![handler_ref(h1)], inner, Ty::i32());
    // The handler literal values are evaluated in their own frames, so run inside a function.
    let v = run_in(&mut tb, Ty::i32(), |_| outer.clone()).unwrap();
    assert_eq!(int_of(&v), 102);
}

#[test]
fn handler_declaration_with_self() {
    let mut tb = Tb::new();
    let e = effect_ref("Counter");
    let mut c = FnB::new("add", Ty::unit()).kind(FnKind::Clause);
    let sp = c.param("self", ptr_u32());
    let n = c.param("n", Ty::u32());
    let body = assign(deref(local(sp, ptr_u32())), add(deref(local(sp, ptr_u32())), local(n, Ty::u32())));
    let cid = tb.add(c.build(blk(vec![body], None, Ty::unit())));
    let d = tb.add_handler_decl(&e, ptr_u32(), vec![cid]);
    let v = run_in(&mut tb, Ty::u32(), |f| {
        let c = f.local("c", Ty::u32());
        let w = with_(
            0,
            vec![HandlerValue::Decl { decls: vec![d], value: addr_of(local(c, Ty::u32())) }],
            blk(
                vec![stmt(perform(&e, 0, vec![u32e(5)], Ty::unit())), stmt(perform(&e, 0, vec![u32e(7)], Ty::unit()))],
                None,
                Ty::unit(),
            ),
            Ty::unit(),
        );
        blk(vec![let_(c, u32e(0)), stmt(w)], Some(local(c, Ty::u32())), Ty::u32())
    })
    .unwrap();
    assert_eq!(int_of(&v), 12);
}

#[test]
fn second_class_closure_captures_by_reference() {
    let mut tb = Tb::new();
    let kty = fn_ty(vec![], Ty::unit(), vec![]);
    let k = FnB::new("inc", Ty::unit()).kind(FnKind::Literal);
    let body = assign(upvar(1, LocalId(0), Ty::u32()), add(upvar(1, LocalId(0), Ty::u32()), u32e(1)));
    let kid = tb.add(k.build(blk(vec![body], None, Ty::unit())));
    let v = run_in(&mut tb, Ty::u32(), |f| {
        let x = f.local("x", Ty::u32());
        let c = f.local("c", kty);
        blk(
            vec![
                let_(x, u32e(1)),
                let_(c, ex(kty, ExprKind::Closure(kid))),
                stmt(call_indirect(local(c, kty), vec![], Ty::unit())),
                stmt(call_indirect(local(c, kty), vec![], Ty::unit())),
            ],
            Some(local(x, Ty::u32())),
            Ty::u32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 3);
}

#[test]
fn function_pointers_and_named_fn_values() {
    let mut tb = Tb::new();
    let mut d = FnB::new("double", Ty::i32());
    let x = d.param("x", Ty::i32());
    let did = tb.add(d.build(mul(local(x, Ty::i32()), i32e(2))));
    let pty = fnptr_ty(vec![Ty::i32()], Ty::i32());
    let v = run_in(&mut tb, Ty::i32(), |f| {
        let p = f.local("p", pty);
        blk(
            vec![let_(p, coerce(Coercion::FnToPtr, ex(pty, ExprKind::FnRef(did)), pty))],
            Some(call_indirect(local(p, pty), vec![i32e(21)], Ty::i32())),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 42);
    // second-class value from a named function
    let sty = fn_ty(vec![Ty::i32()], Ty::i32(), vec![]);
    let v = run_in(&mut tb, Ty::i32(), |f| {
        let c = f.local("c", sty);
        blk(
            vec![let_(c, coerce(Coercion::FnToPtr, ex(sty, ExprKind::FnRef(did)), sty))],
            Some(call_indirect(local(c, sty), vec![i32e(5)], Ty::i32())),
            Ty::i32(),
        )
    })
    .unwrap();
    assert_eq!(int_of(&v), 10);
    // calling a bogus function pointer traps
    let bogus = builtin(Builtin::Bits, vec![usz(0x42)], pty);
    let e = run_in(&mut tb, Ty::i32(), |_| call_indirect(bogus.clone(), vec![i32e(1)], Ty::i32()));
    assert!(trap_msg(e).contains("invalid function pointer"));
    let null_fn = builtin(Builtin::Bits, vec![usz(0)], pty);
    let e = run_in(&mut tb, Ty::i32(), |_| call_indirect(null_fn.clone(), vec![i32e(1)], Ty::i32()));
    assert!(trap_msg(e).contains("null function pointer"));
}

/// `run(k)` installs a handler returning 2 around `k()`; `main` installs one returning 1 around
/// the closure's creation. A closure that lists the effect in its type sees the caller's handler.
fn closure_context(listed: bool) -> i128 {
    let mut tb = Tb::new();
    let e = effect_ref("Get");
    let mk_handler = |tb: &mut Tb, n: i128| {
        let c = FnB::new("get", Ty::i32()).kind(FnKind::Clause);
        let cid = tb.add(c.build(i32e(n)));
        tb.add_handler(&e, vec![cid])
    };
    let h1 = mk_handler(&mut tb, 1);
    let h2 = mk_handler(&mut tb, 2);
    let kty = fn_ty(vec![], Ty::i32(), if listed { vec![e.clone()] } else { vec![] });
    let k = FnB::new("k", Ty::i32()).kind(FnKind::Literal);
    let kid = tb.add(k.build(perform(&e, 0, vec![], Ty::i32())));
    let mut run = FnB::new("run", Ty::i32());
    let kp = run.param("k", kty);
    let rbody = with_(0, vec![handler_ref(h2)], call_indirect(local(kp, kty), vec![], Ty::i32()), Ty::i32());
    let runid = tb.add(run.build(rbody));
    let main_body =
        with_(0, vec![handler_ref(h1)], call(runid, vec![ex(kty, ExprKind::Closure(kid))], Ty::i32()), Ty::i32());
    let v = run_in(&mut tb, Ty::i32(), |_| main_body.clone()).unwrap();
    int_of(&v)
}

#[test]
fn closure_effects_listed_come_from_caller_unlisted_from_definition() {
    assert_eq!(closure_context(true), 2);
    assert_eq!(closure_context(false), 1);
}

#[test]
fn for_style_iteration_with_early_break_unwinds_the_iterator() {
    let mut tb = Tb::new();
    let y = effect_ref("Yield");
    let u = Ty::u32();
    // count(p, n) needs Yield: defer *p += 1000; i := 0; while i < n { yield(i); i += 1 }
    let mut count = FnB::new("count", Ty::unit());
    let p = count.param("p", ptr_u32());
    let n = count.param("n", u);
    let i = count.local("i", u);
    let loop_body =
        vec![stmt(perform(&y, 0, vec![local(i, u)], Ty::unit())), assign(local(i, u), add(local(i, u), u32e(1)))];
    let cbody = blk(
        vec![
            defer(block_unit(vec![bump(p, 1000)])),
            let_(i, u32e(0)),
            stmt(loop_(0, vec![CondPart::Bool(lt(local(i, u), local(n, u)))], loop_body, None)),
        ],
        None,
        Ty::unit(),
    );
    let cid = tb.add(count.build(cbody));
    // yield clause: if x == 1 { continue }; if x == 3 { break }; sum += x
    let mut cl = FnB::new("yield", Ty::unit()).kind(FnKind::Clause);
    let x = cl.param("x", u);
    let clause_body = blk(
        vec![
            stmt(if_(eq(local(x, u), u32e(1)), cont(crate::tir::Target::InstallingWith), None, Ty::unit())),
            stmt(if_(eq(local(x, u), u32e(3)), brk(lbl(1, 0), None), None, Ty::unit())),
            assign(upvar(1, LocalId(0), u), add(upvar(1, LocalId(0), u), local(x, u))),
        ],
        None,
        Ty::unit(),
    );
    let clid = tb.add(cl.build(clause_body));
    let h = tb.add_handler(&y, vec![clid]);
    let v = run_in(&mut tb, u, |f| {
        let sum = f.local("sum", u);
        assert_eq!(sum, LocalId(0));
        let pl = f.local("pl", u);
        let w =
            with_(0, vec![handler_ref(h)], call(cid, vec![addr_of(local(pl, u)), u32e(10)], Ty::unit()), Ty::unit());
        blk(vec![let_(sum, u32e(0)), let_(pl, u32e(0)), stmt(w)], Some(add(local(sum, u), local(pl, u))), u)
    })
    .unwrap();
    // x = 0, 2 are summed (1 is skipped, 3 breaks); the iterator's defer ran once.
    assert_eq!(int_of(&v), 2 + 1000);
}

#[test]
fn return_from_a_clause_returns_from_the_enclosing_function() {
    let mut tb = Tb::new();
    let e = effect_ref("Esc");
    // clause: return 99 from the function containing the handler literal (one level up)
    let mut c = FnB::new("bail", Ty::never()).kind(FnKind::Clause);
    let _x = c.param("x", Ty::i32());
    let cid = tb.add(c.build(ret(1, i32e(99))));
    let h = tb.add_handler(&e, vec![cid]);
    let mut g = FnB::new("g", Ty::unit());
    let p = g.param("p", ptr_u32());
    let gbody = blk(
        vec![errdefer(block_unit(vec![bump(p, 100)])), defer(block_unit(vec![bump(p, 1)]))],
        Some(perform(&e, 0, vec![i32e(0)], Ty::never())),
        Ty::unit(),
    );
    let gid = tb.add(g.build(gbody));
    let mut f = FnB::new("f", Ty::i32());
    let p = f.param("p", ptr_u32());
    let fbody = blk(
        vec![
            defer(block_unit(vec![bump(p, 10)])),
            stmt(with_(0, vec![handler_ref(h)], call(gid, vec![local(p, ptr_u32())], Ty::unit()), Ty::unit())),
        ],
        Some(i32e(0)),
        Ty::i32(),
    );
    let fid = tb.add(f.build(fbody));
    let v = run_in(&mut tb, Ty::u32(), |m| {
        let x = m.local("x", Ty::u32());
        let r = m.local("r", Ty::i32());
        blk(
            vec![let_(x, u32e(0)), let_(r, call(fid, vec![addr_of(local(x, Ty::u32()))], Ty::i32()))],
            Some(add(local(x, Ty::u32()), builtin(Builtin::Trunc, vec![local(r, Ty::i32())], Ty::u32()))),
            Ty::u32(),
        )
    })
    .unwrap();
    // g unwound (errdefer 100 + defer 1), f's own defer 10 ran normally, f returned 99
    assert_eq!(int_of(&v), 100 + 1 + 10 + 99);
}

// ---------------------------------------------------------------------------
// Recursion, budgets
// ---------------------------------------------------------------------------

fn recursive_sum(tb: &mut Tb) -> FnId {
    let t = Ty::int(false, 64);
    let id = tb.next_fn();
    let mut f = FnB::new("rec", t);
    let n = f.param("n", t);
    let body = if_(
        eq(local(n, t), int(0, t)),
        int(0, t),
        Some(add(int(1, t), call(id, vec![sub(local(n, t), int(1, t))], t))),
        t,
    );
    tb.add(f.build(body))
}

#[test]
fn deep_recursion_works() {
    let mut tb = Tb::new();
    let id = recursive_sum(&mut tb);
    let t = Ty::int(false, 64);
    let v = tb.eval(&call(id, vec![int(100_000, t)], t)).unwrap();
    assert_eq!(int_of(&v), 100_000);
}

#[test]
fn runaway_recursion_traps_with_stack_overflow() {
    let mut tb = Tb::new();
    let id = recursive_sum(&mut tb);
    let t = Ty::int(false, 64);
    let m = trap_msg(tb.eval(&call(id, vec![int(100_000_000, t)], t)));
    assert!(m.contains("stack overflow"), "{}", m);
}

#[test]
fn step_budget_is_enforced() {
    let mut tb = Tb::new();
    let t = Ty::usize();
    let mut f = FnB::new("spin", Ty::unit());
    let i = f.local("i", t);
    let body = blk(
        vec![let_(i, usz(0)), stmt(loop_(0, vec![CondPart::Bool(boolean(true))], vec![plus1(i)], None))],
        None,
        Ty::unit(),
    );
    let id = tb.add(f.build(body));
    let mut c = cfg();
    c.step_budget = Some(10_000);
    let r = eval_const(&tb.prog, &c, &call(id, vec![], Ty::unit()));
    assert!(r.unwrap_err().message.contains("compile-time evaluation exceeded its step budget"));
    // enough budget for a finite computation
    let id2 = recursive_sum(&mut tb);
    let t64 = Ty::int(false, 64);
    c.step_budget = Some(100_000);
    assert!(eval_const(&tb.prog, &c, &call(id2, vec![int(100, t64)], t64)).is_ok());
    c.step_budget = Some(100);
    assert!(eval_const(&tb.prog, &c, &call(id2, vec![int(100, t64)], t64)).is_err());
}

#[test]
#[ignore = "performance probe: run with --ignored --nocapture"]
fn perf_probe_loop() {
    let t = Ty::int(false, 64);
    let mut tb = Tb::new();
    let mut f = FnB::new("sum", t);
    let i = f.local("i", t);
    let s = f.local("s", t);
    let step = block_unit(vec![assign(local(i, t), add(local(i, t), int(1, t)))]);
    let body = vec![assign(local(s, t), bin(BinOp::Add(Flavor::Wrap), local(s, t), local(i, t)))];
    let b = blk(
        vec![
            let_(i, int(0, t)),
            let_(s, int(0, t)),
            stmt(loop_(0, vec![CondPart::Bool(lt(local(i, t), int(1_000_000, t)))], body, Some(step))),
        ],
        Some(local(s, t)),
        t,
    );
    let id = tb.add(f.build(b));
    let start = std::time::Instant::now();
    let v = tb.eval(&call(id, vec![], t)).unwrap();
    eprintln!("sum = {} in {:?}", int_of(&v), start.elapsed());
}
