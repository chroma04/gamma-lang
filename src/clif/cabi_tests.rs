//! Classification of values for each target's C calling convention, checked against the
//! System V x86-64 psABI (3.2.3), AAPCS64 (6.8.2, "Parameter Passing") and the Microsoft
//! x64 calling convention. The classifier runs on layouts alone, so every target is
//! exercised on every host.

use super::*;
use crate::layout::Target;
use crate::symbol::Symbol;
use crate::tir::FieldDef;
use crate::types::FloatTy;
use cranelift_codegen::ir::types::{F32, F64, I8, I16, I32, I64};

type Os = (&'static str, &'static str);

const SYSV: Os = ("Linux", "X86_64");
const A64: Os = ("Linux", "Aarch64");
const APPLE: Os = ("MacOS", "Aarch64");
const WIN: Os = ("Windows", "X86_64");

fn target((os, arch): Os) -> Target {
    Target { ptr_bytes: 8, little_endian: true, int128_align: 16, os, arch, filc: false, f16: true }
}

fn f32t() -> Ty {
    Ty::intern(TyKind::Float(FloatTy::F32))
}

fn i64t() -> Ty {
    Ty::int(true, 64)
}

fn i128t() -> Ty {
    Ty::int(true, 128)
}

/// The nominal types of a test.
#[derive(Default)]
struct Types {
    adts: HashMap<AdtId, AdtDef>,
}

impl Types {
    fn adt(&mut self, kind: AdtKind) -> Ty {
        let id = AdtId::fresh(Symbol::intern("T"));
        self.adts.insert(id, AdtDef { name: Symbol::intern("T"), kind });
        Ty::intern(TyKind::Adt(id, Box::new([])))
    }

    fn fields(fields: &[Ty]) -> Vec<FieldDef> {
        fields
            .iter()
            .enumerate()
            .map(|(i, &ty)| FieldDef { name: Symbol::intern(&format!("f{i}")), ty, is_pub: true, align: None })
            .collect()
    }

    fn strukt(&mut self, fields: &[Ty]) -> Ty {
        self.adt(AdtKind::Struct { fields: Self::fields(fields), packed: false, align: None, bits: None })
    }

    fn packed(&mut self, fields: &[Ty]) -> Ty {
        self.adt(AdtKind::Struct { fields: Self::fields(fields), packed: true, align: None, bits: None })
    }

    fn cunion(&mut self, fields: &[Ty]) -> Ty {
        self.adt(AdtKind::CUnion { fields: Self::fields(fields) })
    }

    /// The C signature of `tys -> ret` on `t`.
    fn sig(&self, t: Os, tys: &[Ty], ret: Ty) -> CResult<CSig> {
        let lookup = |id: AdtId| self.adts.get(&id);
        let lay = Layouts::new(target(t), &lookup);
        let (cc, conv) = if t == WIN {
            (crate::types::CallConv::Win64, CallConv::WindowsFastcall)
        } else if t.1 == "X86_64" {
            (crate::types::CallConv::C, CallConv::SystemV)
        } else {
            (crate::types::CallConv::C, CallConv::AppleAarch64)
        };
        CAbi { lay: &lay, adts: &self.adts }.c_sig(tys, ret, cc, conv, Span::DUMMY)
    }

    fn args(&self, t: Os, tys: &[Ty]) -> Vec<CArg> {
        self.sig(t, tys, Ty::unit()).unwrap().args
    }

    fn ret(&self, t: Os, ret: Ty) -> CRet {
        self.sig(t, &[], ret).unwrap().ret
    }

    /// How `last` is passed after the arguments `prefix`.
    fn after(&self, t: Os, prefix: &[Ty], last: Ty) -> CArg {
        let mut tys = prefix.to_vec();
        tys.push(last);
        self.args(t, &tys).pop().unwrap()
    }
}

fn parts(p: &[(Type, u32)]) -> CArg {
    CArg::Parts { pad: vec![], parts: p.to_vec() }
}

fn padded(pad: usize, p: &[(Type, u32)]) -> CArg {
    CArg::Parts { pad: vec![I64; pad], parts: p.to_vec() }
}

fn ints(n: usize) -> Vec<Ty> {
    vec![i64t(); n]
}

fn doubles(n: usize) -> Vec<Ty> {
    vec![Ty::f64(); n]
}

// --- System V x86-64 -------------------------------------------------------------------

#[test]
fn sysv_eightbyte_classes() {
    let mut ty = Types::default();
    let (i32t, f32t, f64t) = (Ty::i32(), f32t(), Ty::f64());
    // An eightbyte is INTEGER if any field in it is, else SSE.
    let int_float = ty.strukt(&[i32t, f32t]);
    let two_floats = ty.strukt(&[f32t, f32t]);
    let three_floats = ty.strukt(&[f32t, f32t, f32t]);
    let double_int = ty.strukt(&[f64t, i64t()]);
    let int_double = ty.strukt(&[i64t(), f64t]);
    let two_doubles = ty.strukt(&[f64t, f64t]);
    let mixed = ty.strukt(&[f32t, i32t, f64t]);
    let single = ty.strukt(&[f64t]);
    let bytes = ty.strukt(&[Ty::u8(), Ty::u8(), Ty::u8()]);
    let float_array = ty.strukt(&[Ty::array(4, f32t)]);
    let union = ty.cunion(&[f64t, i64t()]);
    let tuple = Ty::tuple(vec![Ty::u8(), f32t]);
    let tys = [
        int_float,
        two_floats,
        three_floats,
        double_int,
        int_double,
        two_doubles,
        mixed,
        single,
        bytes,
        float_array,
        union,
        tuple,
    ];
    let want = [
        parts(&[(I64, 0)]),
        parts(&[(F64, 0)]),
        parts(&[(F64, 0), (F64, 8)]),
        parts(&[(F64, 0), (I64, 8)]),
        parts(&[(I64, 0), (F64, 8)]),
        parts(&[(F64, 0), (F64, 8)]),
        parts(&[(I64, 0), (F64, 8)]),
        parts(&[(F64, 0)]),
        parts(&[(I64, 0)]),
        parts(&[(F64, 0), (F64, 8)]),
        // A union of an integer and a double is INTEGER.
        parts(&[(I64, 0)]),
        parts(&[(I64, 0)]),
    ];
    // Each alone, so that no register runs out.
    for (t, want) in tys.iter().zip(want) {
        assert_eq!(ty.args(SYSV, &[*t]), vec![want], "{t}");
    }
}

#[test]
fn sysv_memory_class() {
    let mut ty = Types::default();
    let big = ty.strukt(&ints(3));
    let seventeen = ty.strukt(&[i64t(), i64t(), Ty::u8()]);
    let floats = ty.strukt(&[Ty::array(5, f32t())]);
    // An unaligned field makes the object MEMORY.
    let unaligned = ty.packed(&[Ty::u8(), Ty::u32()]);
    // On the stack, an argument takes a multiple of 8 bytes.
    assert_eq!(
        ty.args(SYSV, &[big, seventeen, floats, unaligned]),
        vec![CArg::Stack(24), CArg::Stack(24), CArg::Stack(24), CArg::Stack(8)]
    );
    let sig = ty.sig(SYSV, &[big], Ty::unit()).unwrap();
    assert_eq!(sig.sig.params.len(), 1);
    assert_eq!(sig.sig.params[0].purpose, ArgumentPurpose::StructArgument(24));
}

#[test]
fn sysv_results() {
    let mut ty = Types::default();
    let big = ty.strukt(&ints(3));
    let pair = ty.strukt(&[i64t(), Ty::f64()]);
    let float_pair = ty.strukt(&[f32t(), f32t()]);
    assert_eq!(ty.ret(SYSV, big), CRet::Sret);
    assert_eq!(ty.ret(SYSV, pair), CRet::Parts(vec![(I64, 0), (F64, 8)]));
    assert_eq!(ty.ret(SYSV, float_pair), CRet::Parts(vec![(F64, 0)]));
    assert_eq!(ty.ret(SYSV, Ty::i32()), CRet::Scalar);
    assert_eq!(ty.ret(SYSV, Ty::unit()), CRet::None);
    let sig = ty.sig(SYSV, &[Ty::i32()], big).unwrap();
    assert_eq!(sig.sig.params[0].purpose, ArgumentPurpose::StructReturn);
    assert_eq!(sig.sig.params.len(), 2);
    assert!(sig.sig.returns.is_empty());
    let sig = ty.sig(SYSV, &[], pair).unwrap();
    assert_eq!(sig.sig.returns.iter().map(|r| r.value_type).collect::<Vec<_>>(), vec![I64, F64]);
}

#[test]
fn sysv_register_exhaustion() {
    let mut ty = Types::default();
    let two_ints = ty.strukt(&[i64t(), i64t()]);
    let one_int = ty.strukt(&[i64t()]);
    let one_double = ty.strukt(&[Ty::f64()]);
    let two_doubles = ty.strukt(&[Ty::f64(), Ty::f64()]);
    let both = ty.strukt(&[i64t(), Ty::f64()]);
    // Six integer registers: two eightbytes fit after four integers, not after five.
    assert_eq!(ty.after(SYSV, &ints(4), two_ints), parts(&[(I64, 0), (I64, 8)]));
    assert_eq!(ty.after(SYSV, &ints(5), two_ints), CArg::Stack(16));
    assert_eq!(ty.after(SYSV, &ints(5), one_int), parts(&[(I64, 0)]));
    assert_eq!(ty.after(SYSV, &ints(6), one_int), CArg::Stack(8));
    // An argument that goes to the stack leaves the registers left to later ones.
    let mut p = ints(5);
    p.extend([two_ints, one_int]);
    assert_eq!(ty.args(SYSV, &p)[5..], [CArg::Stack(16), parts(&[(I64, 0)])]);
    // A hidden result pointer takes the first integer register.
    let big = ty.strukt(&ints(3));
    let sig = ty.sig(SYSV, &[ints(3), vec![two_ints]].concat(), big).unwrap();
    assert_eq!(sig.args[3], parts(&[(I64, 0), (I64, 8)]));
    let sig = ty.sig(SYSV, &[ints(4), vec![two_ints]].concat(), big).unwrap();
    assert_eq!(sig.args[4], CArg::Stack(16));
    // Eight SSE registers.
    assert_eq!(ty.after(SYSV, &doubles(8), one_double), CArg::Stack(8));
    assert_eq!(ty.after(SYSV, &doubles(7), one_double), parts(&[(F64, 0)]));
    assert_eq!(ty.after(SYSV, &doubles(7), two_doubles), CArg::Stack(16));
    // Integer and SSE registers are counted apart; a mixed struct needs one of each.
    assert_eq!(ty.after(SYSV, &ints(5), both), parts(&[(I64, 0), (F64, 8)]));
    assert_eq!(ty.after(SYSV, &ints(6), both), CArg::Stack(16));
    assert_eq!(ty.after(SYSV, &doubles(8), both), CArg::Stack(16));
    assert_eq!(ty.after(SYSV, &ints(6), one_double), parts(&[(F64, 0)]));
    assert_eq!(ty.after(SYSV, &[doubles(8), ints(5)].concat(), one_int), parts(&[(I64, 0)]));
}

#[test]
fn sysv_int128() {
    let mut ty = Types::default();
    let one_int = ty.strukt(&[i64t()]);
    let two_ints = ty.strukt(&[i64t(), i64t()]);
    let wide = ty.strukt(&[i128t()]);
    // A struct holding an `__int128` is two INTEGER eightbytes, on the stack when they do
    // not fit.
    assert_eq!(ty.args(SYSV, &[wide]), vec![parts(&[(I64, 0), (I64, 8)])]);
    assert_eq!(ty.after(SYSV, &ints(5), wide), CArg::Stack(16));
    // An `__int128` takes two registers...
    assert_eq!(ty.after(SYSV, &[i128t(); 2], two_ints), parts(&[(I64, 0), (I64, 8)]));
    assert_eq!(ty.after(SYSV, &[i128t(); 3], one_int), CArg::Stack(8));
    // ... and, unlike on AArch64, may start at an odd one.
    assert_eq!(ty.after(SYSV, &[ints(1), vec![i128t()]].concat(), two_ints), parts(&[(I64, 0), (I64, 8)]));
    assert_eq!(ty.after(SYSV, &[ints(4), vec![i128t()]].concat(), one_int), CArg::Stack(8));
}

/// An `__int128` that finds one register left goes to the stack whole and leaves that
/// register to the next argument (psABI 3.2.3). The classification counts it as used, and
/// Cranelift itself splits such a value between the register and the stack, so a call
/// disagrees with C there.
#[test]
#[ignore = "an __int128 with one register left is split by Cranelift"]
fn sysv_int128_with_one_register_left() {
    let mut ty = Types::default();
    let one_int = ty.strukt(&[i64t()]);
    assert_eq!(ty.after(SYSV, &[ints(5), vec![i128t()]].concat(), one_int), parts(&[(I64, 0)]));
}

// --- AArch64 ---------------------------------------------------------------------------

/// Apple's variant differs from AAPCS64 in variadic calls and stack packing, which are not
/// part of classification, so each case runs on both.
fn both(f: impl Fn(Os)) {
    f(A64);
    f(APPLE);
}

#[test]
fn a64_homogeneous_float_aggregates() {
    both(|t| {
        let mut ty = Types::default();
        let (f32t, f64t) = (f32t(), Ty::f64());
        let one = ty.strukt(&[f32t]);
        let four = ty.strukt(&[f32t; 4]);
        let doubles = ty.strukt(&[f64t; 4]);
        let two_doubles = ty.strukt(&[f64t, f64t]);
        let array = ty.strukt(&[Ty::array(3, f64t)]);
        let inner = ty.strukt(&[f32t, f32t]);
        let nested = ty.strukt(&[inner, inner]);
        let tuple = Ty::tuple(vec![f64t, f64t]);
        let cunion = ty.cunion(&[f32t, f32t]);
        let tys = [one, four, doubles, two_doubles, array, nested, tuple, cunion];
        let want = [
            parts(&[(F32, 0)]),
            parts(&[(F32, 0), (F32, 4), (F32, 8), (F32, 12)]),
            parts(&[(F64, 0), (F64, 8), (F64, 16), (F64, 24)]),
            parts(&[(F64, 0), (F64, 8)]),
            parts(&[(F64, 0), (F64, 8), (F64, 16)]),
            parts(&[(F32, 0), (F32, 4), (F32, 8), (F32, 12)]),
            parts(&[(F64, 0), (F64, 8)]),
            parts(&[(F32, 0)]),
        ];
        // Each alone, so that no register runs out.
        for (t1, want) in tys.iter().zip(want) {
            assert_eq!(ty.args(t, &[*t1]), vec![want], "{t1}");
        }
        assert_eq!(ty.ret(t, four), CRet::Parts(vec![(F32, 0), (F32, 4), (F32, 8), (F32, 12)]));
        assert_eq!(ty.ret(t, doubles), CRet::Parts(vec![(F64, 0), (F64, 8), (F64, 16), (F64, 24)]));
    });
}

#[test]
fn a64_not_homogeneous() {
    both(|t| {
        let mut ty = Types::default();
        let (f32t, f64t) = (f32t(), Ty::f64());
        let five = ty.strukt(&[f32t; 5]);
        let five_array = ty.strukt(&[Ty::array(5, f32t)]);
        let mixed_floats = ty.strukt(&[f32t, f64t]);
        let float_int = ty.strukt(&[f32t, Ty::i32()]);
        let doubles_and_float = ty.strukt(&[f64t, f64t, f64t, f32t]);
        assert_eq!(
            ty.args(t, &[five, five_array, mixed_floats, float_int, doubles_and_float]),
            vec![
                // Over 16 bytes and not an HFA: the address of a copy.
                CArg::Indirect,
                CArg::Indirect,
                // Not of one type: general registers.
                parts(&[(I64, 0), (I64, 8)]),
                parts(&[(I64, 0)]),
                CArg::Indirect,
            ]
        );
        assert_eq!(ty.ret(t, five), CRet::Sret);
        assert_eq!(ty.ret(t, mixed_floats), CRet::Parts(vec![(I64, 0), (I64, 8)]));
    });
}

#[test]
fn a64_small_aggregates_in_integer_registers() {
    both(|t| {
        let mut ty = Types::default();
        let byte = ty.strukt(&[Ty::u8()]);
        let word = ty.strukt(&[Ty::u8(), Ty::u8(), Ty::u8()]);
        let pair = ty.strukt(&[i64t(), i64t()]);
        let twelve = ty.strukt(&[Ty::i32(), Ty::i32(), Ty::i32()]);
        let sixteen = ty.strukt(&[i64t(), Ty::u8()]);
        let big = ty.strukt(&ints(3));
        let packed = ty.packed(&[Ty::u8(), Ty::u32()]);
        assert_eq!(
            ty.args(t, &[byte, word, pair, twelve, sixteen, big, packed]),
            vec![
                parts(&[(I64, 0)]),
                parts(&[(I64, 0)]),
                parts(&[(I64, 0), (I64, 8)]),
                parts(&[(I64, 0), (I64, 8)]),
                parts(&[(I64, 0), (I64, 8)]),
                CArg::Indirect,
                parts(&[(I64, 0)]),
            ]
        );
        assert_eq!(ty.ret(t, pair), CRet::Parts(vec![(I64, 0), (I64, 8)]));
        assert_eq!(ty.ret(t, big), CRet::Sret);
        let sig = ty.sig(t, &[], big).unwrap();
        assert_eq!(sig.sig.params.len(), 1);
        assert_eq!(sig.sig.params[0].purpose, ArgumentPurpose::StructReturn);
    });
}

#[test]
fn a64_register_exhaustion() {
    both(|t| {
        let mut ty = Types::default();
        let one = ty.strukt(&[i64t()]);
        let pair = ty.strukt(&[i64t(), i64t()]);
        let big = ty.strukt(&ints(3));
        let float2 = ty.strukt(&[f32t(), f32t()]);
        let float1 = ty.strukt(&[Ty::f64()]);
        let pair_parts = [(I64, 0), (I64, 8)];
        // Eight general registers: a pair fits after six; after seven, the register left is
        // skipped and the pair goes to the stack whole (AAPCS64 C.14, C.15).
        assert_eq!(ty.after(t, &ints(6), pair), parts(&pair_parts));
        assert_eq!(ty.after(t, &ints(7), pair), padded(1, &pair_parts));
        assert_eq!(ty.after(t, &ints(7), one), parts(&[(I64, 0)]));
        assert_eq!(ty.after(t, &ints(8), one), parts(&[(I64, 0)]));
        // Once an aggregate went to the stack, no later argument uses a register.
        let mut p = ints(7);
        p.extend([pair, one]);
        assert_eq!(ty.args(t, &p)[7..], [padded(1, &pair_parts), parts(&[(I64, 0)])]);
        // The address of a large aggregate takes one register.
        assert_eq!(ty.after(t, &[ints(5), vec![big]].concat(), pair), parts(&pair_parts));
        assert_eq!(ty.after(t, &[ints(6), vec![big]].concat(), pair), padded(1, &pair_parts));
        assert_eq!(ty.after(t, &ints(8), big), CArg::Indirect);
        // A hidden result pointer is in x8, not an argument register: four pairs fill x0-x7.
        let sig = ty.sig(t, &[pair; 4], big).unwrap();
        assert!(sig.args.iter().all(|a| *a == parts(&pair_parts)));
        // General and floating-point registers are counted apart.
        assert_eq!(ty.after(t, &doubles(8), one), parts(&[(I64, 0)]));
        assert_eq!(ty.after(t, &ints(8), float1), parts(&[(F64, 0)]));
        assert_eq!(ty.after(t, &doubles(7), float1), parts(&[(F64, 0)]));
        assert_eq!(ty.after(t, &doubles(6), float2), parts(&[(F32, 0), (F32, 4)]));
        // A homogeneous float aggregate that does not fit the registers left is not
        // supported (AAPCS64 would pass it on the stack).
        assert!(ty.sig(t, &[doubles(8), vec![float1]].concat(), Ty::unit()).is_err());
        assert!(ty.sig(t, &[doubles(7), vec![float2]].concat(), Ty::unit()).is_err());
    });
}

#[test]
fn a64_int128() {
    both(|t| {
        let mut ty = Types::default();
        let wide = ty.strukt(&[i128t()]);
        let pair = ty.strukt(&[i64t(), i64t()]);
        let one = ty.strukt(&[i64t()]);
        let pair_parts = [(I64, 0), (I64, 8)];
        // A 16-byte aligned aggregate starts at an even register (C.8).
        assert_eq!(ty.args(t, &[wide]), vec![parts(&pair_parts)]);
        assert_eq!(ty.after(t, &ints(1), wide), padded(1, &pair_parts));
        assert_eq!(ty.after(t, &ints(2), wide), parts(&pair_parts));
        // So does an `__int128`: after one register it skips one.
        assert_eq!(ty.after(t, &[ints(1), vec![i128t()]].concat(), pair), parts(&pair_parts));
        // x0, a skipped x1 and x2-x3 are used, and an odd number of registers is left.
        let odd = [ints(1), vec![i128t()], ints(3)].concat();
        assert_eq!(ty.after(t, &odd, pair), padded(1, &pair_parts));
        assert_eq!(ty.after(t, &odd, one), parts(&[(I64, 0)]));
        assert_eq!(ty.after(t, &[ints(1), vec![i128t(); 2]].concat(), pair), parts(&pair_parts));
        // x6 and x7 remain after three `__int128` values, and none after four.
        assert_eq!(ty.after(t, &[i128t(); 3], pair), parts(&pair_parts));
        assert_eq!(ty.after(t, &[i128t(); 4], one), parts(&[(I64, 0)]));
        assert_eq!(ty.after(t, &[i128t(); 4], pair), padded(0, &pair_parts));
        // No room for an aligned aggregate: not supported.
        assert!(ty.sig(t, &[vec![i128t(); 4], vec![wide]].concat(), Ty::unit()).is_err());
    });
}

#[test]
fn apple_classifies_like_aapcs64() {
    let mut ty = Types::default();
    let five = ty.strukt(&[f32t(); 5]);
    let hfa = ty.strukt(&[Ty::f64(); 3]);
    let pair = ty.strukt(&[Ty::i32(), Ty::u8()]);
    let wide = ty.strukt(&[i128t()]);
    let tys = [hfa, five, pair, i64t(), wide, Ty::f64()];
    assert_eq!(ty.args(A64, &tys), ty.args(APPLE, &tys));
    for r in [hfa, five, pair, wide] {
        assert_eq!(ty.ret(A64, r), ty.ret(APPLE, r));
    }
}

// --- Windows x64 -----------------------------------------------------------------------

#[test]
fn win64_aggregates_by_size() {
    let mut ty = Types::default();
    let u8t = Ty::u8();
    let cases: Vec<(Ty, Option<Type>)> = vec![
        (ty.strukt(&[u8t]), Some(I8)),
        (ty.strukt(&[u8t, u8t]), Some(I16)),
        (ty.strukt(&[u8t, u8t, u8t]), None),
        (ty.strukt(&[Ty::u32()]), Some(I32)),
        (ty.strukt(&[u8t; 4]), Some(I32)),
        // Padding counts: 8 bytes.
        (ty.strukt(&[Ty::u32(), u8t]), Some(I64)),
        (ty.strukt(&[u8t; 6]), None),
        (ty.strukt(&[u8t; 7]), None),
        (ty.strukt(&[Ty::u64()]), Some(I64)),
        // Floating-point members make no difference: one integer register.
        (ty.strukt(&[f32t(), f32t()]), Some(I64)),
        (ty.strukt(&[Ty::f64()]), Some(I64)),
        (ty.strukt(&[f32t()]), Some(I32)),
        (Ty::tuple(vec![Ty::u32(), Ty::u32()]), Some(I64)),
        (ty.strukt(&[Ty::u64(), u8t]), None),
        (ty.strukt(&[Ty::u64(), Ty::u64()]), None),
        (ty.strukt(&[Ty::f64(), Ty::f64()]), None),
        (ty.strukt(&[Ty::u64(); 3]), None),
        (ty.strukt(&[Ty::u64(); 5]), None),
    ];
    for (t, reg) in cases {
        let (want_arg, want_ret) = match reg {
            Some(r) => (parts(&[(r, 0)]), CRet::Parts(vec![(r, 0)])),
            None => (CArg::Indirect, CRet::Sret),
        };
        let sig = ty.sig(WIN, &[t], t).unwrap();
        assert_eq!(sig.args, vec![want_arg], "{t}");
        assert_eq!(sig.ret, want_ret, "{t}");
        assert_eq!(sig.sig.call_conv, CallConv::WindowsFastcall);
        match reg {
            Some(r) => {
                assert_eq!(sig.sig.params.len(), 1, "{t}");
                assert_eq!(sig.sig.params[0].value_type, r);
                assert_eq!(sig.sig.returns[0].value_type, r);
            }
            None => {
                // The result pointer, then the address of the copy.
                assert_eq!(sig.sig.params.len(), 2, "{t}");
                assert_eq!(sig.sig.params[0].purpose, ArgumentPurpose::StructReturn);
                assert_eq!(sig.sig.params[1].value_type, I64);
            }
        }
    }
}

#[test]
fn win64_scalars_and_many_arguments() {
    let mut ty = Types::default();
    let three = ty.strukt(&[Ty::u8(); 3]);
    let eight = ty.strukt(&[Ty::u64()]);
    let sig = ty.sig(WIN, &[Ty::i32(), Ty::f64(), f32t(), i128t(), Ty::bool()], Ty::f64()).unwrap();
    assert!(sig.args.iter().all(|a| *a == CArg::Scalar));
    assert_eq!(sig.ret, CRet::Scalar);
    // The classification does not depend on the registers earlier arguments took (the
    // four-register budget is the code generator's): each argument is one slot, the value
    // or the address of its copy.
    let tys = [i64t(), three, Ty::f64(), eight, three, i64t(), eight, three];
    let sig = ty.sig(WIN, &tys, Ty::unit()).unwrap();
    assert_eq!(
        sig.args,
        vec![
            CArg::Scalar,
            CArg::Indirect,
            CArg::Scalar,
            parts(&[(I64, 0)]),
            CArg::Indirect,
            CArg::Scalar,
            parts(&[(I64, 0)]),
            CArg::Indirect
        ]
    );
    assert_eq!(sig.sig.params.len(), tys.len());
}
