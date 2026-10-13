//! Differential tests: the small-integer paths must agree with the general byte-string paths.

use super::machine::*;
use super::testkit::*;
use super::*;
use crate::layout::Target;

fn targets() -> Vec<Target> {
    vec![
        target64(),
        Target { ptr_bytes: 4, little_endian: true, int128_align: 16, os: "Linux", arch: "X86_64", filc: false },
        Target { ptr_bytes: 8, little_endian: false, int128_align: 16, os: "Linux", arch: "X86_64", filc: false },
        Target { ptr_bytes: 4, little_endian: false, int128_align: 16, os: "Linux", arch: "X86_64", filc: false },
    ]
}

fn int_tys() -> Vec<Ty> {
    let mut v = vec![Ty::usize(), Ty::isize()];
    for bits in [1u16, 2, 3, 5, 7, 8, 9, 13, 16, 17, 24, 31, 32, 33, 48, 63, 64] {
        v.push(Ty::int(false, bits));
        v.push(Ty::int(true, bits));
    }
    v
}

/// Values worth trying for a type: the extremes and their neighbours, and everything for narrow types.
fn samples(si: SmallInt) -> Vec<i128> {
    let (lo, hi) = (si.min(), si.max());
    let mut v = vec![];
    if si.bits <= 8 {
        let mut x = lo;
        while x <= hi {
            v.push(x);
            x += 1;
        }
        return v;
    }
    for d in 0..4 {
        v.push(lo + d);
        v.push(hi - d);
    }
    for x in [0, 1, 2, 3, 7, 10, 255, 256, 1000, 65535, 65536, 0x7fff_ffff, 0x8000_0000, 0xdead_beef, -1, -2, -3, -10] {
        if si.fits(x) {
            v.push(x);
        }
    }
    let mid = hi / 2;
    v.extend([mid - 1, mid, mid + 1]);
    let mut s = 0x9e37_79b9_7f4a_7c15u64;
    for _ in 0..12 {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        v.push(si.value(s));
    }
    v.retain(|x| si.fits(*x));
    v.sort();
    v.dedup();
    v
}

#[allow(clippy::boxed_local)]
fn trap_text(f: Box<Flow>) -> String {
    match *f {
        Flow::Trap(t) => t.message,
        other => panic!("unexpected flow {:?}", other),
    }
}

#[test]
fn small_binop_matches_general() {
    let flavors = [Flavor::Wrap, Flavor::Sat, Flavor::Checked];
    let mut ops = vec![BinOp::Div, BinOp::Rem, BinOp::Shr, BinOp::BitAnd, BinOp::BitOr, BinOp::BitXor];
    ops.extend([BinOp::Lt, BinOp::Le, BinOp::Gt, BinOp::Ge]);
    for fl in flavors {
        ops.extend([BinOp::Add(fl), BinOp::Sub(fl), BinOp::Mul(fl), BinOp::Shl(fl)]);
    }
    let prog = Program::default();
    let bool_ty = Ty::bool();
    for (ti, target) in targets().into_iter().enumerate() {
        // Debug builds check arithmetic exactly as Checked builds do.
        for mode in [Mode::Checked, Mode::Fast] {
            let cfg = RunConfig { target: target.clone(), mode, step_budget: None };
            let lookup = |id: AdtId| prog.adts.get(&id);
            let m = Machine::new(&prog, &cfg, &lookup, 0);
            for ty in int_tys() {
                let si = m.small_of(ty).unwrap();
                // One-byte types, tried exhaustively, do not depend on the pointer size or
                // the byte order: the first target covers them.
                if ti > 0 && si.size == 1 {
                    continue;
                }
                let vals = samples(si);
                // The shift amount has its own type.
                let rtys = [ty, Ty::u32(), Ty::i32(), Ty::u8()];
                for &op in &ops {
                    let shift = matches!(op, BinOp::Shl(_) | BinOp::Shr);
                    for &rty in if shift { &rtys[..] } else { &rtys[..1] } {
                        let ri = m.small_of(rty).unwrap();
                        let rvals = if shift {
                            let mut r = samples(ri);
                            r.extend([0, 1, si.bits as i128 - 1, si.bits as i128, si.bits as i128 + 1, 200]);
                            r.retain(|x| ri.fits(*x));
                            r
                        } else {
                            vals.clone()
                        };
                        for &a in &vals {
                            for &b in &rvals {
                                let (ab, bb) = (m.encode_small(si, a), m.encode_small(ri, b));
                                let res_ty = if matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge) {
                                    bool_ty
                                } else {
                                    ty
                                };
                                let general = m.binop(Span::DUMMY, op, ty, rty, res_ty, &ab, &bb);
                                let fast = m.small_binop(Span::DUMMY, op, si, ty, a, b);
                                let ctx = || {
                                    format!("{:?} {} {} {} {}-byte pointers {:?}", op, a, b, ty, target.ptr_bytes, mode)
                                };
                                match (general, fast) {
                                    (Ok(g), Ok(f)) => {
                                        let enc = if res_ty == bool_ty {
                                            m.bool_bytes(f != 0)
                                        } else {
                                            m.encode_small(si, f)
                                        };
                                        assert!(g == enc, "{}", ctx());
                                    }
                                    (Err(g), Err(f)) => assert_eq!(trap_text(g), trap_text(f), "{}", ctx()),
                                    (g, f) => panic!("{}: general ok={}, small ok={}", ctx(), g.is_ok(), f.is_ok()),
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

#[test]
fn small_unops_and_codecs_match_general() {
    let prog = Program::default();
    for target in targets() {
        for mode in [Mode::Checked, Mode::Fast] {
            let cfg = RunConfig { target: target.clone(), mode, step_budget: None };
            let lookup = |id: AdtId| prog.adts.get(&id);
            let m = Machine::new(&prog, &cfg, &lookup, 0);
            for ty in int_tys() {
                let si = m.small_of(ty).unwrap();
                for a in samples(si) {
                    let ab = m.encode_small(si, a);
                    assert_eq!(ab, m.int_bytes(ty, &num::Int::S(a)), "encode {} {}", a, ty);
                    assert_eq!(m.decode_small(si, &ab), m.int_of(ty, &ab).to_i128().unwrap(), "decode {} {}", a, ty);
                    // Negation and complement.
                    let neg = m.unop(Span::DUMMY, UnOp::Neg, ty, &ab);
                    let v = -a;
                    let fneg = if si.fits(v) {
                        Ok(v)
                    } else if m.fast() {
                        Ok(si.value(v as u64))
                    } else {
                        Err("integer overflow in `-`".to_string())
                    };
                    match (neg, fneg) {
                        (Ok(g), Ok(f)) => assert_eq!(g, m.encode_small(si, f), "neg {} {}", a, ty),
                        (Err(g), Err(f)) => assert_eq!(trap_text(g), f),
                        _ => panic!("neg {} {}", a, ty),
                    }
                    let not = m.unop(Span::DUMMY, UnOp::BitNot, ty, &ab).unwrap();
                    assert_eq!(not, m.encode_small(si, si.value(!a as u64)), "not {} {}", a, ty);
                }
            }
        }
    }
}

/// Words written through the small-integer path read back through the general path, and vice versa.
#[test]
fn small_memory_access_matches_general() {
    let prog = Program::default();
    for target in targets() {
        let cfg = RunConfig { target: target.clone(), mode: Mode::Checked, step_budget: None };
        let lookup = |id: AdtId| prog.adts.get(&id);
        let mut m = Machine::new(&prog, &cfg, &lookup, 0);
        for ty in int_tys() {
            let si = m.small_of(ty).unwrap();
            let addr = m.alloc_mem(Span::DUMMY, 16, 16, memory::AllocKind::Stack, true).unwrap();
            for a in samples(si) {
                m.store_small(Span::DUMMY, addr, a, si, si.align).unwrap();
                let g = m.load_ty(Span::DUMMY, addr, ty).unwrap();
                assert_eq!(g, m.encode_small(si, a), "{} {}", a, ty);
                assert_eq!(m.load_small(Span::DUMMY, addr, si, si.align).unwrap(), a);
                m.store(Span::DUMMY, addr + 8, &m.encode_small(si, a), si.align).unwrap_or(());
            }
            // Misalignment and bounds are reported as the general path reports them.
            if si.size > 1 {
                let g = m.load(Span::DUMMY, addr + 1, si.size, si.align).unwrap_err();
                let f = m.load_small(Span::DUMMY, addr + 1, si, si.align).unwrap_err();
                assert_eq!(trap_text(g), trap_text(f));
            }
            let g = m.load(Span::DUMMY, addr + 16, si.size, 1).unwrap_err();
            let f = m.load_small(Span::DUMMY, addr + 16, si, 1).unwrap_err();
            assert_eq!(trap_text(g), trap_text(f));
            m.mem.kill(addr);
            let g = m.load(Span::DUMMY, addr, si.size, 1).unwrap_err();
            let f = m.load_small(Span::DUMMY, addr, si, 1).unwrap_err();
            assert_eq!(trap_text(g), trap_text(f));
        }
    }
}
