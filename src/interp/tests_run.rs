//! Tests: `@write` formatting, `run_main`, and compile-time constants.

use super::testkit::*;
use super::*;
use crate::symbol::Symbol;

/// A program whose ambient `Out`/`Err`/`Env` effects are set up.
struct Amb {
    tb: Tb,
    out: EffectRef,
    err: EffectRef,
    env: EffectRef,
}

fn amb() -> Amb {
    let mut tb = Tb::new();
    let out = effect_ref("Out");
    let err = effect_ref("Err");
    let env = effect_ref("Env");
    tb.prog.ambient.out = Some(out.effect);
    tb.prog.ambient.err = Some(err.effect);
    tb.prog.ambient.env = Some(env.effect);
    Amb { tb, out, err, env }
}

impl Amb {
    fn sink(&self) -> WriteSink {
        WriteSink::Perform { effect: self.out.clone(), op: 0 }
    }

    fn print(&self, pieces: &[&str], args: Vec<Expr>) -> Stmt {
        stmt(write_to(self.sink(), pieces, args))
    }

    /// Runs a unit `main` with the given body statements.
    fn run_stmts(&mut self, stmts: Vec<Stmt>) -> (Outcome, String, String) {
        let f = FnB::new("main", Ty::unit());
        let body = blk(stmts, None, Ty::unit());
        let id = self.tb.add(f.build(body));
        self.tb.run(id, &[])
    }
}

/// Prints `arg` with `@write` (built against the program that runs) and returns stdout.
fn show(build: impl FnOnce(&mut Tb) -> Expr) -> String {
    let mut a = amb();
    let arg = build(&mut a.tb);
    let p = a.print(&["", ""], vec![arg]);
    let (o, out, err) = a.run_stmts(vec![p]);
    assert_eq!(o, Outcome::Exit(0), "stderr: {}", err);
    out
}

// ---------------------------------------------------------------------------
// @write display rules
// ---------------------------------------------------------------------------

#[test]
fn display_scalars() {
    assert_eq!(show(|_| i32e(-42)), "-42");
    assert_eq!(show(|_| u8e(200)), "200");
    assert_eq!(
        show(|_| bigint(num_bigint::BigInt::from(1) << 199usize, Ty::int(false, 200))),
        (num_bigint::BigInt::from(1) << 199usize).to_string()
    );
    assert_eq!(show(|_| boolean(true)), "true");
    assert_eq!(show(|_| boolean(false)), "false");
    assert_eq!(show(|_| unit()), "()");
}

#[test]
fn display_floats() {
    let f = Ty::f64();
    assert_eq!(show(|_| flt(1.5, f)), "1.5");
    assert_eq!(show(|_| flt(-2.0, f)), "-2");
    assert_eq!(show(|_| flt(0.001, f)), "0.001");
    assert_eq!(show(|_| flt(1e21, f)), "1e21");
    assert_eq!(show(|_| flt(1e20, f)), "100000000000000000000");
    assert_eq!(show(|_| flt(2.5e-7, f)), "2.5e-7");
    assert_eq!(show(|_| flt(1e-6, f)), "0.000001");
    assert_eq!(show(|_| un(UnOp::Neg, flt(0.0, f))), "-0");
    assert_eq!(show(|_| flt(0.0, f)), "0");
    assert_eq!(show(|_| bin(BinOp::Div, flt(0.0, f), flt(0.0, f))), "nan");
    assert_eq!(show(|_| bin(BinOp::Div, flt(1.0, f), flt(0.0, f))), "inf");
    assert_eq!(show(|_| bin(BinOp::Div, flt(-1.0, f), flt(0.0, f))), "-inf");
    let f32t = Ty::intern(TyKind::Float(FloatTy::F32));
    assert_eq!(show(|_| flt(0.1f32 as f64, f32t)), "0.1");
    assert_eq!(show(|_| flt(0.1f32 as f64, f)), "0.10000000149011612");
    let h = Ty::intern(TyKind::Float(FloatTy::F16));
    assert_eq!(show(|_| flt(num::round_float(0.1, FloatTy::F16), h)), "0.1");
    assert_eq!(show(|_| flt(1.0 / 3.0, f)), "0.3333333333333333");
    assert_eq!(show(|_| flt(123456789.0, f)), "123456789");
}

#[test]
fn display_strings_raw_at_top_level_and_quoted_nested() {
    assert_eq!(show(|_| string("hi there")), "hi there");
    let st = Ty::slice(false, Ty::u8());
    let _ = st;
    assert_eq!(
        show(|tb| {
            let s = tb.add_struct("User", &[("name", Ty::string()), ("age", Ty::u8())]);
            ex(s, ExprKind::Aggregate(vec![string("Ada \"A\"\n"), u8e(36)]))
        }),
        "User(name: \"Ada \\\"A\\\"\\n\", age: 36)"
    );
    // optional / result / tuple / array of strings are quoted
    assert_eq!(show(|_| some(string("x"))), "\"x\"");
    assert_eq!(show(|_| tuple(vec![string("a"), i32e(1)])), "(\"a\", 1)");
    assert_eq!(show(|_| array(vec![string("a"), string("b")])), "[\"a\", \"b\"]");
    let r = Ty::result(Ty::string(), Ty::i32());
    assert_eq!(show(move |_| err(string("bad"), r)), "!\"bad\"");
    assert_eq!(show(move |_| ok(i32e(3), r)), "3");
    // escapes
    assert_eq!(show(|_| some(string("tab\there\\ \u{1}"))), "\"tab\\there\\\\ \\x01\"");
}

#[test]
fn display_aggregates() {
    assert_eq!(show(|_| tuple(vec![i32e(1), boolean(true)])), "(1, true)");
    assert_eq!(show(|_| tuple(vec![i32e(1)])), "(1,)");
    assert_eq!(show(|_| array(vec![i32e(1), i32e(2), i32e(3)])), "[1, 2, 3]");
    assert_eq!(show(|_| slice(array(vec![u8e(1), u8e(2), u8e(3)]), Some(usz(1)), None, false)), "\u{2}\u{3}");
    assert_eq!(show(|_| slice(array(vec![i32e(1), i32e(2), i32e(3)]), Some(usz(1)), None, false)), "[2, 3]");
    assert_eq!(show(|_| ex(Ty::array(0, Ty::u8()), ExprKind::Array(vec![]))), "[]");
    // named tuple
    let named = Ty::intern(TyKind::Tuple {
        elems: Box::new([Ty::i32(), Ty::i32()]),
        names: Some(Box::new([Symbol::intern("q"), Symbol::intern("r")])),
    });
    assert_eq!(show(move |_| ex(named, ExprKind::Aggregate(vec![i32e(3), i32e(4)]))), "(q: 3, r: 4)");
    // structs, with nested struct, private-or-not irrelevant
    assert_eq!(
        show(|tb| {
            let v2 = tb.add_struct("Vec2", &[("x", Ty::f64()), ("y", Ty::f64())]);
            ex(v2, ExprKind::Aggregate(vec![flt(1.5, Ty::f64()), flt(-2.0, Ty::f64())]))
        }),
        "Vec2(x: 1.5, y: -2)"
    );
    assert_eq!(
        show(|tb| {
            let inner = tb.add_struct("In", &[("a", Ty::u8())]);
            let outer = tb.add_struct("Out", &[("i", inner), ("n", Ty::optional(Ty::u8()))]);
            let iv = ex(inner, ExprKind::Aggregate(vec![u8e(1)]));
            ex(outer, ExprKind::Aggregate(vec![iv, null(Ty::optional(Ty::u8()))]))
        }),
        "Out(i: In(a: 1), n: null)"
    );
    assert_eq!(show(|tb| ex(tb.add_struct("Empty", &[]), ExprKind::Aggregate(vec![]))), "Empty()");
}

#[test]
fn display_unions_optionals_results_pointers() {
    assert_eq!(
        show(|tb| {
            let d = tb.add_union("Dir", &[("N", None), ("S", None)]);
            ex(d, ExprKind::Variant { variant: 1, payload: None })
        }),
        ".S"
    );
    let shape = |tb: &mut Tb| {
        tb.add_union(
            "Shape",
            &[
                ("Circle", Some(Ty::f64())),
                (
                    "Rect",
                    Some(Ty::intern(TyKind::Tuple {
                        elems: Box::new([Ty::f64(), Ty::f64()]),
                        names: Some(Box::new([Symbol::intern("w"), Symbol::intern("h")])),
                    })),
                ),
                ("Pair", Some(Ty::tuple(vec![Ty::u8(), Ty::u8()]))),
                ("Empty", None),
            ],
        )
    };
    assert_eq!(
        show(|tb| {
            let s = shape(tb);
            ex(s, ExprKind::Variant { variant: 0, payload: Some(Box::new(flt(2.5, Ty::f64()))) })
        }),
        ".Circle(2.5)"
    );
    assert_eq!(
        show(|tb| {
            let s = shape(tb);
            let TyKind::Adt(id, _) = s.kind() else { unreachable!() };
            let AdtKind::Union { variants, .. } = &tb.prog.adt(*id).kind else { unreachable!() };
            let named = variants[1].payload.unwrap();
            let payload = ex(named, ExprKind::Aggregate(vec![flt(2.0, Ty::f64()), flt(3.0, Ty::f64())]));
            ex(s, ExprKind::Variant { variant: 1, payload: Some(Box::new(payload)) })
        }),
        ".Rect(w: 2, h: 3)"
    );
    assert_eq!(
        show(|tb| {
            let s = shape(tb);
            let payload = tuple(vec![u8e(1), u8e(2)]);
            ex(s, ExprKind::Variant { variant: 2, payload: Some(Box::new(payload)) })
        }),
        ".Pair(1, 2)"
    );
    assert_eq!(
        show(|tb| {
            let s = shape(tb);
            ex(s, ExprKind::Variant { variant: 3, payload: None })
        }),
        ".Empty"
    );
    assert_eq!(show(|_| some(i32e(5))), "5");
    assert_eq!(show(|_| null(Ty::optional(Ty::i32()))), "null");
    assert_eq!(
        show(|tb| {
            let e = tb.add_union("E", &[("NotFound", None)]);
            err(ex(e, ExprKind::Variant { variant: 0, payload: None }), Ty::result(e, Ty::i32()))
        }),
        "!.NotFound"
    );
    // pointers print as hex addresses
    let out = show(|_| ex(Ty::many_ptr(true, Ty::u8()), ExprKind::SlicePtr(Box::new(string("abc")))));
    assert!(out.starts_with("0x"), "{}", out);
    assert_eq!(show(|_| null(Ty::optional(Ty::ptr(false, Ty::u8())))), "null");
    assert_eq!(show(|_| builtin(Builtin::Bits, vec![usz(0xbeef)], Ty::ptr(false, Ty::u8()))), "0xbeef");
}

#[test]
fn write_pieces_are_interleaved_with_arguments() {
    let mut a = amb();
    let p = a.print(&["x = ", ", y = ", "\n"], vec![i32e(1), string("two")]);
    let (o, out, _) = a.run_stmts(vec![p]);
    assert_eq!(o, Outcome::Exit(0));
    assert_eq!(out, "x = 1, y = two\n");
}

#[test]
fn write_to_function_sink() {
    let mut a = amb();
    // sink :: fn(bytes: []const u8) => write_out(bytes)
    let sty = fn_ty(vec![Ty::string()], Ty::unit(), vec![a.out.clone()]);
    let mut s = FnB::new("sink", Ty::unit()).kind(FnKind::Literal);
    let bytes = s.param("bytes", Ty::string());
    let sid = a.tb.add(s.build(perform(&a.out, 0, vec![local(bytes, Ty::string())], Ty::unit())));
    let mut f = FnB::new("main", Ty::unit());
    let sl = f.local("s", sty);
    let body = blk(
        vec![
            let_(sl, ex(sty, ExprKind::Closure(sid))),
            stmt(write_to(WriteSink::Fn(Box::new(local(sl, sty))), &["n=", "!"], vec![i32e(7)])),
        ],
        None,
        Ty::unit(),
    );
    let id = a.tb.add(f.build(body));
    let (o, out, _) = a.tb.run(id, &[]);
    assert_eq!(o, Outcome::Exit(0));
    assert_eq!(out, "n=7!");
}

#[test]
fn write_to_a_user_handler_sink() {
    // The sink is called once per non-empty piece and formatted argument.
    let mut tb = Tb::new();
    let w = effect_ref("W");
    let mut c = FnB::new("w", Ty::unit()).kind(FnKind::Clause);
    let b = c.param("b", Ty::string());
    let n = Ty::usize();
    let body = blk(
        vec![
            assign(upvar(1, LocalId(0), n), add(upvar(1, LocalId(0), n), usz(1))),
            assign(
                upvar(1, LocalId(1), n),
                add(upvar(1, LocalId(1), n), ex(n, ExprKind::SliceLen(Box::new(local(b, Ty::string()))))),
            ),
        ],
        None,
        Ty::unit(),
    );
    let cid = tb.add(c.build(body));
    let h = tb.add_handler(&w, vec![cid]);
    let v = run_in(&mut tb, n, |f| {
        let calls = f.local("calls", n);
        let total = f.local("total", n);
        let wr = write_to(WriteSink::Perform { effect: w.clone(), op: 0 }, &["ab", "", "c"], vec![i32e(12), i32e(-3)]);
        let w = with_(0, vec![HandlerValue::Literal(ex(Ty::unit(), ExprKind::HandlerRef(h)))], wr, Ty::unit());
        blk(
            vec![let_(calls, usz(0)), let_(total, usz(0)), stmt(w)],
            Some(add(mul(local(calls, n), usz(100)), local(total, n))),
            n,
        )
    })
    .unwrap();
    // chunks: "ab", "12", "" (skipped), "-3", "c"  -> 4 calls, 2+2+2+1 bytes
    assert_eq!(int_of(&v), 400 + 7);
}

// ---------------------------------------------------------------------------
// run_main
// ---------------------------------------------------------------------------

#[test]
fn hello_world() {
    let mut a = amb();
    let p = a.print(&["hello, world\n"], vec![]);
    let (o, out, err) = a.run_stmts(vec![p]);
    assert_eq!(o, Outcome::Exit(0));
    assert_eq!(out, "hello, world\n");
    assert_eq!(err, "");
}

#[test]
fn eprint_goes_to_stderr() {
    let mut a = amb();
    let st = stmt(write_to(WriteSink::Perform { effect: a.err.clone(), op: 0 }, &["oops"], vec![]));
    let (o, out, err) = a.run_stmts(vec![st]);
    assert_eq!(o, Outcome::Exit(0));
    assert_eq!((out.as_str(), err.as_str()), ("", "oops"));
}

#[test]
fn main_returning_u8_is_the_exit_status() {
    let mut tb = Tb::new();
    let f = FnB::new("main", Ty::u8());
    let id = tb.add(f.build(u8e(7)));
    assert_eq!(tb.run(id, &[]).0, Outcome::Exit(7));
}

#[test]
fn main_returning_an_error_exits_with_1_and_reports() {
    let mut a = amb();
    let e = a.tb.add_union("ConfigError", &[("Missing", None), ("Bad", Some(Ty::i32()))]);
    let r = Ty::result(e, Ty::unit());
    let f = FnB::new("main", r);
    let id = a.tb.add(f.build(err(ex(e, ExprKind::Variant { variant: 1, payload: Some(Box::new(i32e(3))) }), r)));
    let (o, out, err) = a.tb.run(id, &[]);
    assert_eq!(o, Outcome::Exit(1));
    assert_eq!(out, "");
    assert_eq!(err, "error: .Bad(3)\n");
    // success
    let f = FnB::new("main2", r);
    let id = a.tb.add(f.build(ok(unit(), r)));
    assert_eq!(a.tb.run(id, &[]).0, Outcome::Exit(0));
}

#[test]
fn env_exit_unwinds_to_main_running_defers() {
    let mut a = amb();
    let mut f = FnB::new("main", Ty::unit());
    let _ = &mut f;
    let sink = a.sink();
    let body = blk(
        vec![
            defer(write_to(sink.clone(), &["bye"], vec![])),
            errdefer(write_to(sink.clone(), &["!"], vec![])),
            stmt(perform(&a.env, 2, vec![u8e(3)], Ty::never())),
            stmt(write_to(sink, &["unreachable"], vec![])),
        ],
        None,
        Ty::unit(),
    );
    let id = a.tb.add(f.build(body));
    let (o, out, _) = a.tb.run(id, &[]);
    assert_eq!(o, Outcome::Exit(3));
    // the exit is an unwind, so errdefer runs too
    assert_eq!(out, "!bye");
}

#[test]
fn env_args_and_var() {
    let mut a = amb();
    let sl = Ty::slice(true, Ty::string());
    let mut f = FnB::new("main", Ty::unit());
    let args = f.local("args", sl);
    let sink = a.sink();
    let body = blk(
        vec![
            let_(args, perform(&a.env, 0, vec![], sl)),
            stmt(write_to(
                sink.clone(),
                &["", " ", " ", ""],
                vec![
                    ex(Ty::usize(), ExprKind::SliceLen(Box::new(local(args, sl)))),
                    index(local(args, sl), usz(0)),
                    index(local(args, sl), usz(1)),
                ],
            )),
            stmt(write_to(
                sink,
                &[" ", ""],
                vec![perform(&a.env, 1, vec![string("GAMMA_SURELY_UNSET_VARIABLE")], Ty::optional(Ty::string()))],
            )),
        ],
        None,
        Ty::unit(),
    );
    let id = a.tb.add(f.build(body));
    let (o, out, _) = a.tb.run(id, &["alpha", "beta"]);
    assert_eq!(o, Outcome::Exit(0));
    assert_eq!(out, "2 alpha beta null");
}

#[test]
fn traps_do_not_run_defers_and_report_location() {
    let mut a = amb();
    let sink = a.sink();
    let mut f = FnB::new("main", Ty::unit());
    let body = blk(
        vec![
            defer(write_to(sink, &["cleanup"], vec![])),
            stmt(ex(Ty::never(), ExprKind::Trap { message: Box::new(string("boom")) })),
        ],
        None,
        Ty::unit(),
    );
    let _ = &mut f;
    let id = a.tb.add(f.build(body));
    let (o, out, _) = a.tb.run(id, &[]);
    match o {
        Outcome::Trap(t) => assert_eq!(t.message, "boom"),
        other => panic!("{:?}", other),
    }
    assert_eq!(out, "");
}

#[test]
fn assert_builtin() {
    let ok = builtin(Builtin::Assert, vec![boolean(true), string("fine")], Ty::unit());
    assert!(Tb::new().eval(&ok).is_ok());
    let bad = builtin(Builtin::Assert, vec![boolean(false), string("x must be positive")], Ty::unit());
    assert_eq!(trap_msg(Tb::new().eval(&bad)), "assertion failed: x must be positive");
}

#[test]
fn globals_are_static_storage() {
    let mut tb = Tb::new();
    tb.prog.globals.push(GlobalDef {
        name: Symbol::intern("G"),
        ty: Ty::i32(),
        span: crate::source::Span::DUMMY,
        init: Some(i32e(5)),
        thread_local: false,
    });
    let g = ex(Ty::i32(), ExprKind::Global(GlobalId(0)));
    let body = blk(
        vec![assign(g.clone(), add(g.clone(), i32e(1))), assign(g.clone(), add(g.clone(), i32e(1)))],
        Some(builtin(Builtin::Trunc, vec![g.clone()], Ty::u8())),
        Ty::u8(),
    );
    let f = FnB::new("main", Ty::u8());
    let id = tb.add(f.build(body));
    assert_eq!(tb.run(id, &[]).0, Outcome::Exit(7));
    // not at compile time
    assert!(trap_msg(tb.eval(&g)).contains("#global"));
}

#[test]
fn debug_mode_fills_uninitialized_storage() {
    let mut tb = Tb::new();
    let f = FnB::new("main", Ty::u8());
    let id = tb.add(f.build(ex(Ty::u8(), ExprKind::Uninit)));
    tb.prog.main = Some(id);
    let mut c = cfg();
    c.mode = Mode::Debug;
    let (mut o, mut e) = (Vec::new(), Vec::new());
    assert_eq!(run_main(&tb.prog, &c, &[], &mut o, &mut e), Outcome::Exit(0xAA));
}

#[test]
fn unsupported_features_trap_cleanly() {
    let mut tb = Tb::new();
    let e = ex(Ty::never(), ExprKind::Resume { cont: Box::new(unit()), value: Box::new(unit()) });
    assert!(trap_msg(tb.eval(&e)).contains("not supported by the interpreter yet"));
    let f = FnB::new("ext", Ty::unit())
        .kind(FnKind::Foreign { lib: Symbol::intern("c"), link_name: Symbol::intern("ext") });
    let mut def = f.build(unit());
    def.body = None;
    let id = tb.add(def);
    assert!(trap_msg(tb.eval(&call(id, vec![], Ty::unit()))).contains("foreign"));
}

#[test]
fn hash_is_consistent_with_equality() {
    let h = |e: Expr| {
        int_of(
            &Tb::new().eval(&builtin(Builtin::Hash, vec![e, int(7, Ty::int(false, 64))], Ty::int(false, 64))).unwrap(),
        )
    };
    assert_eq!(h(string("hello")), h(string("hello")));
    assert_ne!(h(string("hello")), h(string("hellp")));
    assert_eq!(h(tuple(vec![i32e(1), i32e(2)])), h(tuple(vec![i32e(1), i32e(2)])));
    assert_eq!(h(flt(0.0, Ty::f64())), h(un(UnOp::Neg, flt(0.0, Ty::f64()))));
    assert_ne!(h(some(i32e(1))), h(null(Ty::optional(Ty::i32()))));
}

// ---------------------------------------------------------------------------
// eval_const
// ---------------------------------------------------------------------------

#[test]
fn const_struct_value() {
    let mut tb = Tb::new();
    let s = tb.add_struct("S", &[("a", Ty::u8()), ("b", Ty::int(false, 32)), ("c", Ty::f64())]);
    let v = tb
        .eval(&ex(s, ExprKind::Aggregate(vec![u8e(1), int(0x0102_0304, Ty::int(false, 32)), flt(2.5, Ty::f64())])))
        .unwrap();
    assert_eq!(v.ty, s);
    assert!(v.relocs.is_empty());
    assert_eq!(v.bytes.len(), 16);
    assert_eq!(v.bytes[0], 1);
    assert_eq!(&v.bytes[4..8], &[4, 3, 2, 1]);
    assert_eq!(f64::from_le_bytes(v.bytes[8..16].try_into().unwrap()), 2.5);
}

#[test]
fn const_string_has_a_relocation_to_its_bytes() {
    let tb = Tb::new();
    let v = tb.eval(&string("hello")).unwrap();
    assert_eq!(v.bytes.len(), 16);
    assert_eq!(u64::from_le_bytes(v.bytes[8..16].try_into().unwrap()), 5);
    assert_eq!(u64::from_le_bytes(v.bytes[0..8].try_into().unwrap()), 0);
    assert_eq!(v.relocs.len(), 1);
    let r = &v.relocs[0];
    assert_eq!((r.offset, r.addend), (0, 0));
    match &r.target {
        StaticData::Bytes(b) => assert_eq!(&**b, b"hello"),
        other => panic!("{:?}", other),
    }
    // a subslice points into the middle
    let sub = slice(string("hello"), Some(usz(2)), None, false);
    let v = tb.eval(&sub).unwrap();
    assert_eq!(v.relocs[0].addend, 2);
    assert_eq!(u64::from_le_bytes(v.bytes[8..16].try_into().unwrap()), 3);
    // C strings include the terminating NUL
    let c = ex(Ty::many_ptr(true, Ty::u8()), ExprKind::CStr(std::rc::Rc::from(&b"ab"[..])));
    let v = tb.eval(&c).unwrap();
    match &v.relocs[0].target {
        StaticData::Bytes(b) => assert_eq!(&**b, b"ab\0"),
        other => panic!("{:?}", other),
    }
}

#[test]
fn const_relocations_inside_aggregates() {
    let mut tb = Tb::new();
    let s = tb.add_struct("Entry", &[("id", Ty::u8()), ("name", Ty::string())]);
    let arr = array(vec![
        ex(s, ExprKind::Aggregate(vec![u8e(1), string("one")])),
        ex(s, ExprKind::Aggregate(vec![u8e(2), string("two")])),
    ]);
    let v = tb.eval(&arr).unwrap();
    assert_eq!(v.relocs.len(), 2);
    assert_eq!(v.relocs[0].offset, 8);
    assert_eq!(v.relocs[1].offset, 24 + 8);
    // optional slice
    let os = Ty::optional(Ty::string());
    let v = tb.eval(&some(string("x"))).unwrap();
    assert_eq!(v.relocs.len(), 1);
    assert!(tb.eval(&null(os)).unwrap().relocs.is_empty());
}

#[test]
fn const_function_pointer_relocation() {
    let mut tb = Tb::new();
    let f = FnB::new("callee", Ty::unit());
    let id = tb.add(f.build(unit()));
    let pty = fnptr_ty(vec![], Ty::unit());
    let v = tb.eval(&coerce(Coercion::FnToPtr, ex(pty, ExprKind::FnRef(id)), pty)).unwrap();
    assert_eq!(v.relocs, vec![Reloc { offset: 0, target: StaticData::Fn(id), addend: 0 }]);
}

#[test]
fn const_referencing_constants() {
    let mut tb = Tb::new();
    let table_ty = Ty::array(3, Ty::u8());
    let table = tb.add_const("TABLE", table_ty, array(vec![u8e(10), u8e(20), u8e(30)]));
    let tref = ex(table_ty, ExprKind::Const(table));
    // element read through the constant
    assert_eq!(int_of(&tb.eval(&index(tref.clone(), usz(2))).unwrap()), 30);
    // a constant holding a pointer to another constant
    let pty = Ty::ptr(true, table_ty);
    let ptr_const = tb.add_const("P", pty, ex(Ty::ptr(true, table_ty), ExprKind::AddrOf(Box::new(tref.clone()))));
    let v = tb.eval(&ex(pty, ExprKind::Const(ptr_const))).unwrap();
    assert_eq!(v.relocs, vec![Reloc { offset: 0, target: StaticData::Const(table), addend: 0 }]);
    // reading through that pointer sees the table
    let deref_e = index(deref(ex(pty, ExprKind::Const(ptr_const))), usz(1));
    assert_eq!(int_of(&tb.eval(&deref_e).unwrap()), 20);
    // a pre-evaluated value is used as is
    let mut tb2 = Tb::new();
    let c = tb2.add_const("K", Ty::i32(), i32e(1));
    tb2.prog.consts[c.index()].value = Some(ConstValue { ty: Ty::i32(), bytes: vec![9, 0, 0, 0], relocs: vec![] });
    assert_eq!(int_of(&tb2.eval(&ex(Ty::i32(), ExprKind::Const(c))).unwrap()), 9);
}

#[test]
fn const_dependency_cycle_is_an_error() {
    let mut tb = Tb::new();
    let c = tb.add_const("A", Ty::i32(), i32e(0));
    tb.prog.consts[c.index()].init = add(ex(Ty::i32(), ExprKind::Const(c)), i32e(1));
    assert!(trap_msg(tb.eval(&ex(Ty::i32(), ExprKind::Const(c)))).contains("depends on itself"));
}

#[test]
fn const_table_built_by_a_function() {
    // squares :: fn() -> [5]u32 { a := [5]u32(fill: 0); for i in 0..5 { a[i] = i*i } ; a }
    let mut tb = Tb::new();
    let at = Ty::array(5, Ty::u32());
    let us = Ty::usize();
    let mut f = FnB::new("squares", at);
    let a = f.local("a", at);
    let i = f.local("i", us);
    let sq = builtin(Builtin::Trunc, vec![mul(local(i, us), local(i, us))], Ty::u32());
    let body = blk(
        vec![
            let_(a, ex(at, ExprKind::ArrayFill(Box::new(int(0, Ty::u32()))))),
            let_(i, usz(0)),
            stmt(loop_(
                0,
                vec![CondPart::Bool(lt(local(i, us), usz(5)))],
                vec![assign(index(local(a, at), local(i, us)), sq)],
                Some(block_unit(vec![assign(local(i, us), add(local(i, us), usz(1)))])),
            )),
        ],
        Some(local(a, at)),
        at,
    );
    let id = tb.add(f.build(body));
    let v = tb.eval(&call(id, vec![], at)).unwrap();
    let vals: Vec<u32> = v.bytes.chunks(4).map(|c| u32::from_le_bytes(c.try_into().unwrap())).collect();
    assert_eq!(vals, vec![0, 1, 4, 9, 16]);
}

#[test]
fn big_endian_32_bit_target() {
    // Layout and arithmetic follow the target.
    let target =
        crate::layout::Target { ptr_bytes: 4, little_endian: false, int128_align: 8, os: "Linux", arch: "Other" };
    let c = RunConfig { target, mode: Mode::Checked, step_budget: None };
    let prog = Program::default();
    let v = eval_const(&prog, &c, &add(int(0x0102, Ty::int(false, 16)), int(1, Ty::int(false, 16)))).unwrap();
    assert_eq!(v.bytes, vec![0x01, 0x03]);
    let v = eval_const(&prog, &c, &string("ab")).unwrap();
    assert_eq!(v.bytes.len(), 8);
    assert_eq!(&v.bytes[4..8], &[0, 0, 0, 2]);
    let v = eval_const(&prog, &c, &usz(5)).unwrap();
    assert_eq!(v.bytes, vec![0, 0, 0, 5]);
}
