//! Interpreter unit tests, building TIR by hand.

use super::testkit::*;
use super::*;
use crate::symbol::Symbol;
use num_bigint::BigInt;
use num_traits::One;

fn u64t() -> Ty {
    Ty::int(false, 64)
}

fn bin_eval(op: BinOp, a: i128, b: i128, ty: Ty) -> Result<ConstValue, Trap> {
    Tb::new().eval(&bin(op, int(a, ty), int(b, ty)))
}

fn bin_i(op: BinOp, a: i128, b: i128, ty: Ty) -> i128 {
    int_of(&bin_eval(op, a, b, ty).unwrap_or_else(|t| panic!("trap: {}", t.message)))
}

fn bin_trap(op: BinOp, a: i128, b: i128, ty: Ty) -> String {
    trap_msg(bin_eval(op, a, b, ty))
}

const ADD: BinOp = BinOp::Add(Flavor::Checked);
const ADDW: BinOp = BinOp::Add(Flavor::Wrap);
const ADDS: BinOp = BinOp::Add(Flavor::Sat);
const ADDO: BinOp = BinOp::Add(Flavor::Opt);

/// The value of an optional integer result: `None` for null.
fn opt_int(v: &ConstValue, inner: Ty) -> Option<i128> {
    let t = Tb::new();
    let lookup = |id| t.prog.adts.get(&id);
    let m = crate::layout::Layouts::new(target64(), &lookup);
    let l = m.layout(v.ty);
    if v.bytes[l.offsets[1] as usize] == 0 {
        return None;
    }
    let (signed, bits) = crate::layout::int_bits(inner, &target64()).unwrap();
    let sz = m.size_of(inner) as usize;
    Some(num::decode_int(&v.bytes[..sz], signed, bits, true).to_i128().unwrap())
}

// ---------------------------------------------------------------------------
// Integer arithmetic
// ---------------------------------------------------------------------------

#[test]
fn u8_flavors() {
    let t = Ty::u8();
    assert_eq!(bin_i(ADD, 100, 100, t), 200);
    assert!(bin_trap(ADD, 200, 100, t).contains("integer overflow in `+`"));
    assert_eq!(bin_i(ADDW, 250, 10, t), 4);
    assert_eq!(bin_i(ADDS, 250, 10, t), 255);
    let v = bin_eval(ADDO, 250, 10, t).unwrap();
    assert_eq!(opt_int(&v, t), None);
    let v = bin_eval(ADDO, 1, 2, t).unwrap();
    assert_eq!(opt_int(&v, t), Some(3));
    assert!(bin_trap(BinOp::Sub(Flavor::Checked), 1, 2, t).contains("integer overflow in `-`"));
    assert_eq!(bin_i(BinOp::Sub(Flavor::Wrap), 1, 2, t), 255);
    assert_eq!(bin_i(BinOp::Sub(Flavor::Sat), 1, 2, t), 0);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Wrap), 16, 17, t), 16);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Sat), 16, 17, t), 255);
    assert!(bin_trap(BinOp::Mul(Flavor::Checked), 16, 16, t).contains("`*`"));
}

#[test]
fn i8_flavors() {
    let t = Ty::int(true, 8);
    assert!(bin_trap(ADD, 127, 1, t).contains("overflow"));
    assert_eq!(bin_i(ADDW, 127, 1, t), -128);
    assert_eq!(bin_i(ADDS, 127, 1, t), 127);
    assert_eq!(bin_i(ADDS, -100, -100, t), -128);
    assert_eq!(bin_i(BinOp::Sub(Flavor::Wrap), -128, 1, t), 127);
    assert!(bin_trap(BinOp::Sub(Flavor::Checked), -128, 1, t).contains("overflow"));
    assert_eq!(bin_i(BinOp::Mul(Flavor::Sat), -100, 2, t), -128);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Checked), -64, 2, t), -128);
    // negation
    assert_eq!(eval_int(&un(UnOp::Neg, int(5, t))), -5);
    assert!(eval_trap(&un(UnOp::Neg, int(-128, t))).contains("integer overflow in `-`"));
}

#[test]
fn u64_and_i64_edges() {
    let t = u64t();
    let max = u64::MAX as i128;
    assert_eq!(bin_i(ADDW, max, 1, t), 0);
    assert!(bin_trap(ADD, max, 1, t).contains("overflow"));
    assert!(bin_trap(BinOp::Mul(Flavor::Checked), max, max, t).contains("overflow"));
    assert_eq!(bin_i(BinOp::Mul(Flavor::Wrap), max, max, t), 1);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Sat), max, max, t), max);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Checked), 1 << 32, 1 << 31, t), 1 << 63);
    let s = Ty::int(true, 64);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Wrap), i64::MIN as i128, -1, s), i64::MIN as i128);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Sat), i64::MIN as i128, -1, s), i64::MAX as i128);
}

#[test]
fn i128_arithmetic() {
    let t = Ty::int(true, 128);
    assert_eq!(bin_i(ADD, i128::MAX - 1, 1, t), i128::MAX);
    assert!(bin_trap(ADD, i128::MAX, 1, t).contains("overflow"));
    assert_eq!(bin_i(ADDW, i128::MAX, 1, t), i128::MIN);
    assert_eq!(bin_i(ADDS, i128::MAX, 1, t), i128::MAX);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Checked), 1 << 60, 1 << 60, t), 1 << 120);
    assert!(bin_trap(BinOp::Mul(Flavor::Checked), 1 << 64, 1 << 64, t).contains("overflow"));
    let u = Ty::int(false, 128);
    let v = Tb::new()
        .eval(&bin(BinOp::Add(Flavor::Checked), bigint(BigInt::from(u128::MAX - 5), u), bigint(BigInt::from(5), u)))
        .unwrap();
    assert_eq!(big_of(&v), BigInt::from(u128::MAX));
}

#[test]
fn u200_arithmetic() {
    let t = Ty::int(false, 200);
    let two199 = BigInt::one() << 199usize;
    let max = (BigInt::one() << 200usize) - BigInt::one();
    let e = |op, a: &BigInt, b: &BigInt| Tb::new().eval(&bin(op, bigint(a.clone(), t), bigint(b.clone(), t)));
    assert_eq!(big_of(&e(ADD, &two199, &(&two199 - 1)).unwrap()), max);
    assert!(trap_msg(e(ADD, &two199, &two199)).contains("overflow"));
    assert_eq!(big_of(&e(ADDW, &max, &BigInt::one()).unwrap()), BigInt::from(0));
    assert_eq!(big_of(&e(ADDS, &max, &BigInt::one()).unwrap()), max);
    assert_eq!(big_of(&e(BinOp::Mul(Flavor::Checked), &two199, &BigInt::from(1)).unwrap()), two199);
    assert!(trap_msg(e(BinOp::Mul(Flavor::Checked), &two199, &BigInt::from(2))).contains("overflow"));
    assert_eq!(big_of(&e(BinOp::Div, &max, &BigInt::from(3)).unwrap()), &max / 3);
    // bitwise / shifts on wide values
    let sh = Tb::new().eval(&bin(BinOp::Shl(Flavor::Checked), bigint(BigInt::one(), t), int(199, Ty::u32()))).unwrap();
    assert_eq!(big_of(&sh), two199);
    assert!(
        trap_msg(Tb::new().eval(&bin(BinOp::Shl(Flavor::Checked), bigint(BigInt::one(), t), int(200, Ty::u32()))))
            .contains("shift amount 200 too large for u200")
    );
}

#[test]
fn signed_wide() {
    let t = Ty::int(true, 200);
    let min = -(BigInt::one() << 199usize);
    let r = Tb::new()
        .eval(&bin(BinOp::Sub(Flavor::Checked), bigint(min.clone(), t), bigint(BigInt::one(), t)))
        .map(|v| big_of(&v));
    assert!(r.is_err());
    let r = Tb::new().eval(&bin(BinOp::Sub(Flavor::Sat), bigint(min.clone(), t), bigint(BigInt::one(), t))).unwrap();
    assert_eq!(big_of(&r), min);
}

#[test]
fn odd_width_integers() {
    let t = Ty::int(false, 5);
    assert_eq!(bin_i(ADD, 15, 16, t), 31);
    assert!(bin_trap(ADD, 16, 16, t).contains("overflow"));
    assert_eq!(bin_i(ADDW, 31, 1, t), 0);
    let s = Ty::int(true, 5);
    assert_eq!(bin_i(ADDW, 15, 1, s), -16);
    assert_eq!(bin_i(ADDS, 15, 1, s), 15);
    let u24 = Ty::int(false, 24);
    assert_eq!(bin_i(BinOp::Mul(Flavor::Wrap), 0xFFFFFF, 0xFFFFFF, u24), 1);
}

#[test]
fn division_and_remainder() {
    let t = Ty::i32();
    assert_eq!(bin_i(BinOp::Div, -7, 2, t), -3);
    assert_eq!(bin_i(BinOp::Rem, -7, 2, t), -1);
    assert_eq!(bin_i(BinOp::Rem, 7, -2, t), 1);
    assert_eq!(bin_i(BinOp::Div, 7, -2, t), -3);
    assert_eq!(bin_i(BinOp::Div, 0, 5, t), 0);
    assert!(bin_trap(BinOp::Div, 1, 0, t).contains("division by zero"));
    assert!(bin_trap(BinOp::Rem, 1, 0, t).contains("division by zero"));
    assert!(bin_trap(BinOp::Div, i32::MIN as i128, -1, t).contains("overflow"));
    assert!(bin_trap(BinOp::Rem, i32::MIN as i128, -1, t).contains("overflow"));
    let u = Ty::u8();
    assert_eq!(bin_i(BinOp::Div, 200, 7, u), 28);
    assert_eq!(bin_i(BinOp::Rem, 200, 7, u), 4);
}

#[test]
fn division_traps_in_fast_mode() {
    let t = Ty::i32();
    let e = bin(BinOp::Div, int(1, t), int(0, t));
    assert!(Tb::new().eval_mode(&e, Mode::Fast).is_err());
    let e = bin(BinOp::Div, int(i32::MIN as i128, t), int(-1, t));
    assert!(Tb::new().eval_mode(&e, Mode::Fast).is_err());
}

#[test]
fn fast_mode_skips_overflow_checks() {
    // `eval_const` always checks; run_main honours the mode.
    let mut tb = Tb::new();
    let mut f = FnB::new("main", Ty::u8());
    let body = bin(ADD, u8e(200), u8e(100));
    f = f.kind(FnKind::Named);
    let main = tb.add(f.build(body));
    tb.prog.main = Some(main);
    let mut c = cfg();
    c.mode = Mode::Fast;
    let (mut o, mut e) = (Vec::new(), Vec::new());
    assert_eq!(run_main(&tb.prog, &c, &[], &mut o, &mut e), Outcome::Exit(44));
    c.mode = Mode::Checked;
    match run_main(&tb.prog, &c, &[], &mut o, &mut e) {
        Outcome::Trap(t) => assert!(t.message.contains("overflow")),
        other => panic!("{:?}", other),
    }
}

#[test]
fn shifts() {
    let u = Ty::u8();
    let amt = |op: BinOp, a: i128, n: i128, nty: Ty| Tb::new().eval(&bin_r(op, int(a, u), int(n, nty)));
    assert_eq!(int_of(&amt(BinOp::Shl(Flavor::Checked), 1, 7, Ty::u32()).unwrap()), 128);
    assert!(trap_msg(amt(BinOp::Shl(Flavor::Checked), 1, 8, Ty::u32())).contains("shift amount 8 too large for u8"));
    assert!(trap_msg(amt(BinOp::Shr, 1, 70, Ty::u64_())).contains("shift amount 70 too large for u8"));
    assert!(trap_msg(amt(BinOp::Shl(Flavor::Wrap), 1, 9, Ty::u8())).contains("too large"));
    // shift overflow by flavor
    assert!(trap_msg(amt(BinOp::Shl(Flavor::Checked), 0x80, 1, Ty::u8())).contains("overflow in `<<`"));
    assert_eq!(int_of(&amt(BinOp::Shl(Flavor::Wrap), 0x81, 1, Ty::u8()).unwrap()), 2);
    assert_eq!(int_of(&amt(BinOp::Shl(Flavor::Sat), 0x81, 1, Ty::u8()).unwrap()), 255);
    let v = amt(BinOp::Shl(Flavor::Opt), 0x81, 1, Ty::u8()).unwrap();
    assert_eq!(opt_int(&v, u), None);
    let v = amt(BinOp::Shl(Flavor::Opt), 0x41, 1, Ty::u8()).unwrap();
    assert_eq!(opt_int(&v, u), Some(0x82));
    // logical right shift for unsigned
    assert_eq!(int_of(&amt(BinOp::Shr, 0x80, 7, Ty::u8()).unwrap()), 1);
    // signed
    let s = Ty::int(true, 8);
    let sa = |op: BinOp, a: i128, n: i128| Tb::new().eval(&bin_r(op, int(a, s), int(n, Ty::u8())));
    assert_eq!(int_of(&sa(BinOp::Shr, -8, 1).unwrap()), -4);
    assert_eq!(int_of(&sa(BinOp::Shr, -1, 7).unwrap()), -1);
    assert_eq!(int_of(&sa(BinOp::Shl(Flavor::Checked), -1, 7).unwrap()), -128);
    assert!(trap_msg(sa(BinOp::Shl(Flavor::Checked), 1, 7)).contains("overflow"));
    assert_eq!(int_of(&sa(BinOp::Shl(Flavor::Sat), 1, 7).unwrap()), 127);
    assert_eq!(int_of(&sa(BinOp::Shl(Flavor::Sat), -2, 7).unwrap()), -128);
    assert_eq!(int_of(&sa(BinOp::Shl(Flavor::Wrap), 1, 7).unwrap()), -128);
}

/// Binary op whose result type is the left operand's (shifts).
fn bin_r(op: BinOp, l: Expr, r: Expr) -> Expr {
    bin(op, l, r)
}

trait U64Alias {
    fn u64_() -> Ty;
}
impl U64Alias for Ty {
    fn u64_() -> Ty {
        Ty::int(false, 64)
    }
}

#[test]
fn bitwise_ops() {
    let t = Ty::u8();
    assert_eq!(bin_i(BinOp::BitAnd, 0b1100, 0b1010, t), 0b1000);
    assert_eq!(bin_i(BinOp::BitOr, 0b1100, 0b1010, t), 0b1110);
    assert_eq!(bin_i(BinOp::BitXor, 0b1100, 0b1010, t), 0b0110);
    assert_eq!(eval_int(&un(UnOp::BitNot, int(5, t))), 250);
    let s = Ty::int(true, 8);
    assert_eq!(eval_int(&un(UnOp::BitNot, int(5, s))), -6);
    assert_eq!(bin_i(BinOp::BitAnd, -1, 0x7f, s), 0x7f);
    assert_eq!(bin_i(BinOp::BitXor, -1, -1, s), 0);
    let w = Ty::int(false, 130);
    let v = Tb::new().eval(&un(UnOp::BitNot, bigint(BigInt::from(0), w))).unwrap();
    assert_eq!(big_of(&v), (BigInt::one() << 130usize) - 1);
}

#[test]
fn array_elementwise() {
    let t = Ty::array(3, Ty::i32());
    let a = array(vec![i32e(1), i32e(2), i32e(3)]);
    let b = array(vec![i32e(10), i32e(20), i32e(30)]);
    let sum = Tb::new().eval(&add(a.clone(), b.clone())).unwrap();
    assert_eq!(sum.ty, t);
    let vals: Vec<i32> = sum.bytes.chunks(4).map(|c| i32::from_le_bytes(c.try_into().unwrap())).collect();
    assert_eq!(vals, vec![11, 22, 33]);
    let neg = Tb::new().eval(&un(UnOp::Neg, a.clone())).unwrap();
    let vals: Vec<i32> = neg.bytes.chunks(4).map(|c| i32::from_le_bytes(c.try_into().unwrap())).collect();
    assert_eq!(vals, vec![-1, -2, -3]);
    let big = array(vec![u8e(200), u8e(1)]);
    let big2 = array(vec![u8e(100), u8e(1)]);
    assert!(trap_msg(Tb::new().eval(&add(big.clone(), big2.clone()))).contains("overflow"));
    let w = Tb::new().eval(&bin(ADDW, big, big2)).unwrap();
    assert_eq!(w.bytes.to_vec(), vec![44, 2]);
}

// ---------------------------------------------------------------------------
// Floats
// ---------------------------------------------------------------------------

#[test]
fn float_arithmetic() {
    let f = Ty::f64();
    let v = Tb::new().eval(&add(flt(0.1, f), flt(0.2, f))).unwrap();
    assert_eq!(f64_of(&v), 0.1 + 0.2);
    let v = Tb::new().eval(&bin(BinOp::Div, flt(1.0, f), flt(0.0, f))).unwrap();
    assert_eq!(f64_of(&v), f64::INFINITY);
    let v = Tb::new().eval(&bin(BinOp::Div, flt(0.0, f), flt(0.0, f))).unwrap();
    assert!(f64_of(&v).is_nan());
    let v = Tb::new().eval(&un(UnOp::Neg, flt(0.0, f))).unwrap();
    assert!(f64_of(&v).is_sign_negative());
    let f32t = Ty::intern(TyKind::Float(FloatTy::F32));
    let v = Tb::new().eval(&add(flt(0.1f32 as f64, f32t), flt(0.2f32 as f64, f32t))).unwrap();
    assert_eq!(f64_of(&v), (0.1f32 + 0.2f32) as f64);
    // f32 overflow rounds to infinity
    let v = Tb::new().eval(&bin(BinOp::Mul(Flavor::Checked), flt(3e38, f32t), flt(10.0, f32t))).unwrap();
    assert_eq!(f64_of(&v), f64::INFINITY);
}

#[test]
fn f16_rounding() {
    let h = Ty::intern(TyKind::Float(FloatTy::F16));
    // 1.0 + 2^-11 is a tie between 1.0 and 1 + 2^-10; round to even gives 1.0.
    let one = flt(1.0, h);
    let tiny = flt(2f64.powi(-11), h);
    let v = Tb::new().eval(&add(one.clone(), tiny)).unwrap();
    assert_eq!(f64_of(&v), 1.0);
    // 1.0 + 3*2^-11 ties between 1+2^-10 and 1+2^-9 -> even mantissa (1 + 2^-9)
    let three = flt(3.0 * 2f64.powi(-11), h);
    let v = Tb::new().eval(&add(one.clone(), three)).unwrap();
    assert_eq!(f64_of(&v), 1.0 + 2f64.powi(-9));
    // overflow to infinity
    let v = Tb::new().eval(&bin(BinOp::Mul(Flavor::Checked), flt(65504.0, h), flt(2.0, h))).unwrap();
    assert_eq!(f64_of(&v), f64::INFINITY);
    // 1/3 rounds to the nearest f16
    let v = Tb::new().eval(&bin(BinOp::Div, one, flt(3.0, h))).unwrap();
    assert_eq!(f64_of(&v), num::f16_to_f64(num::f64_to_f16(1.0 / 3.0)));
    // size of storage is two bytes
    assert_eq!(v.bytes.len(), 2);
}

#[test]
fn float_comparisons_and_nan() {
    let f = Ty::f64();
    let nan = bin(BinOp::Div, flt(0.0, f), flt(0.0, f));
    assert!(!bool_of(&Tb::new().eval(&lt(nan.clone(), flt(1.0, f))).unwrap()));
    assert!(!bool_of(&Tb::new().eval(&bin(BinOp::Ge, nan.clone(), flt(1.0, f))).unwrap()));
    assert!(!bool_of(&Tb::new().eval(&eq(nan.clone(), nan.clone())).unwrap()));
    assert!(bool_of(&Tb::new().eval(&eq(flt(0.0, f), un(UnOp::Neg, flt(0.0, f)))).unwrap()));
    assert!(bool_of(&Tb::new().eval(&lt(flt(1.0, f), flt(2.0, f))).unwrap()));
}

// ---------------------------------------------------------------------------
// Comparison and equality
// ---------------------------------------------------------------------------

#[test]
fn lexicographic_comparison() {
    let t1 = tuple(vec![i32e(1), i32e(5)]);
    let t2 = tuple(vec![i32e(1), i32e(7)]);
    assert!(bool_of(&Tb::new().eval(&lt(t1.clone(), t2.clone())).unwrap()));
    assert!(!bool_of(&Tb::new().eval(&lt(t2.clone(), t1.clone())).unwrap()));
    assert!(bool_of(&Tb::new().eval(&bin(BinOp::Le, t1.clone(), t1.clone())).unwrap()));
    assert!(bool_of(&Tb::new().eval(&bin(BinOp::Ge, t2.clone(), t1.clone())).unwrap()));
    let a1 = array(vec![u8e(1), u8e(2), u8e(3)]);
    let a2 = array(vec![u8e(1), u8e(3), u8e(0)]);
    assert!(bool_of(&Tb::new().eval(&lt(a1, a2)).unwrap()));
    // strings: bytewise, shorter prefix is less
    assert!(bool_of(&Tb::new().eval(&lt(string("abc"), string("abd"))).unwrap()));
    assert!(bool_of(&Tb::new().eval(&lt(string("ab"), string("abc"))).unwrap()));
    assert!(!bool_of(&Tb::new().eval(&lt(string("abc"), string("abc"))).unwrap()));
    assert!(bool_of(&Tb::new().eval(&bin(BinOp::Gt, string("b"), string("abc"))).unwrap()));
    assert!(bool_of(&Tb::new().eval(&bin(BinOp::Ge, string("abc"), string("abc"))).unwrap()));
}

#[test]
fn enum_comparison_by_tag() {
    let mut tb = Tb::new();
    let u = tb.add_union("Dir", &[("N", None), ("E", None), ("S", None)]);
    let v = |i| ex(u, ExprKind::Variant { variant: i, payload: None });
    assert!(bool_of(&tb.eval(&lt(v(0), v(2))).unwrap()));
    assert!(!bool_of(&tb.eval(&lt(v(2), v(1))).unwrap()));
    assert!(bool_of(&tb.eval(&bin(BinOp::Ge, v(2), v(2))).unwrap()));
}

#[test]
fn is_equality() {
    let mut tb = Tb::new();
    // aggregates
    let s = tb.add_struct("P", &[("x", Ty::i32()), ("y", Ty::u8())]);
    let p = |x, y| ex(s, ExprKind::Aggregate(vec![i32e(x), u8e(y)]));
    assert!(bool_of(&tb.eval(&eq(p(1, 2), p(1, 2))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(p(1, 2), p(1, 3))).unwrap()));
    // slices compare by content, not address
    let a = array(vec![u8e(1), u8e(2)]);
    let b = array(vec![u8e(1), u8e(2)]);
    let sl = |arr: Expr| {
        let local_ty = arr.ty;
        let mut f = FnB::new("t", Ty::bool());
        let l = f.local("a", local_ty);
        let _ = l;
        slice(arr, None, None, false)
    };
    let e = eq(sl(a), sl(b));
    assert!(bool_of(&tb.eval(&e).unwrap()));
    assert!(bool_of(&tb.eval(&eq(string("hello"), string("hello"))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(string("hello"), string("hellp"))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(string("hello"), string("hell"))).unwrap()));
    // optionals
    let oi = Ty::optional(Ty::i32());
    assert!(bool_of(&tb.eval(&eq(null(oi), null(oi))).unwrap()));
    assert!(bool_of(&tb.eval(&eq(some(i32e(3)), some(i32e(3)))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(some(i32e(3)), null(oi))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(some(i32e(3)), some(i32e(4)))).unwrap()));
    // results
    let r = Ty::result(Ty::u8(), Ty::i32());
    assert!(bool_of(&tb.eval(&eq(err(u8e(1), r), err(u8e(1), r))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(err(u8e(1), r), err(u8e(2), r))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(err(u8e(1), r), ok(i32e(1), r))).unwrap()));
    assert!(bool_of(&tb.eval(&eq(ok(i32e(1), r), ok(i32e(1), r))).unwrap()));
    // unions: tag and payload
    let u = tb.add_union(
        "Shape",
        &[("Circle", Some(Ty::f64())), ("Rect", Some(Ty::tuple(vec![Ty::f64(), Ty::f64()]))), ("Empty", None)],
    );
    let circle = |x| ex(u, ExprKind::Variant { variant: 0, payload: Some(Box::new(flt(x, Ty::f64()))) });
    let empty = ex(u, ExprKind::Variant { variant: 2, payload: None });
    assert!(bool_of(&tb.eval(&eq(circle(1.0), circle(1.0))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(circle(1.0), circle(2.0))).unwrap()));
    assert!(!bool_of(&tb.eval(&eq(circle(1.0), empty.clone())).unwrap()));
    assert!(bool_of(&tb.eval(&eq(empty.clone(), empty)).unwrap()));
    // is not
    let ne = ex(Ty::bool(), ExprKind::Eq { lhs: Box::new(i32e(1)), rhs: Box::new(i32e(2)), negated: true });
    assert!(bool_of(&tb.eval(&ne).unwrap()));
}

#[test]
fn in_operator() {
    let mut tb = Tb::new();
    let range = tb.add_struct("Range", &[("lo", Ty::i32()), ("hi", Ty::i32())]);
    let rangei = tb.add_struct("RangeIncl", &[("lo", Ty::i32()), ("hi", Ty::i32())]);
    let inn = |x: Expr, c: Expr, neg| {
        ex(Ty::bool(), ExprKind::In { elem: Box::new(x), container: Box::new(c), negated: neg })
    };
    let r = |lo, hi| ex(range, ExprKind::Aggregate(vec![i32e(lo), i32e(hi)]));
    let ri = |lo, hi| ex(rangei, ExprKind::Aggregate(vec![i32e(lo), i32e(hi)]));
    assert!(bool_of(&tb.eval(&inn(i32e(3), r(1, 5), false)).unwrap()));
    assert!(!bool_of(&tb.eval(&inn(i32e(5), r(1, 5), false)).unwrap()));
    assert!(bool_of(&tb.eval(&inn(i32e(5), ri(1, 5), false)).unwrap()));
    assert!(!bool_of(&tb.eval(&inn(i32e(0), ri(1, 5), false)).unwrap()));
    assert!(bool_of(&tb.eval(&inn(i32e(0), ri(1, 5), true)).unwrap()));
    // arrays and tuples
    assert!(bool_of(&tb.eval(&inn(u8e(2), array(vec![u8e(1), u8e(2)]), false)).unwrap()));
    assert!(!bool_of(&tb.eval(&inn(u8e(3), array(vec![u8e(1), u8e(2)]), false)).unwrap()));
    assert!(bool_of(&tb.eval(&inn(u8e(10), tuple(vec![u8e(10), u8e(13)]), false)).unwrap()));
    assert!(bool_of(&tb.eval(&inn(u8e(11), tuple(vec![u8e(10), u8e(13)]), true)).unwrap()));
    let sl = slice(array(vec![i32e(4), i32e(5)]), None, None, false);
    assert!(bool_of(&tb.eval(&inn(i32e(5), sl, false)).unwrap()));
}

#[test]
fn logic_short_circuits() {
    let boom = bin(BinOp::Div, i32e(1), i32e(0));
    let boom_b = eq(boom, i32e(0));
    assert!(!bool_of(&Tb::new().eval(&logic(true, boolean(false), boom_b.clone())).unwrap()));
    assert!(bool_of(&Tb::new().eval(&logic(false, boolean(true), boom_b.clone())).unwrap()));
    assert!(Tb::new().eval(&logic(true, boolean(true), boom_b)).is_err());
    assert!(bool_of(&Tb::new().eval(&un(UnOp::Not, boolean(false))).unwrap()));
}

#[allow(dead_code)]
fn _unused() {
    let _ = Symbol::intern("x");
}
