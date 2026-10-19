//! How values cross the C calling convention: scalars in one parameter each, and structs,
//! tuples, arrays and C unions by value, classified as the System V x86-64 ABI and the
//! AArch64 procedure call standard (AAPCS64, and Apple's variant of it) do.
//!
//! An aggregate of up to 16 bytes travels in registers, as the integer or floating-point
//! parts of its eightbytes (x86-64), or as up to four floats of one type (an AArch64
//! homogeneous floating-point aggregate) or one or two integer registers (AArch64). A
//! larger one is copied to the stack (x86-64) or passed as the address of a copy
//! (AArch64), and a larger result is written through a hidden pointer.

use super::{CResult, Gen, Rep, unsupported};
use crate::source::Span;
use crate::tir::{AdtKind, FnId};
use crate::types::{ArrayKind, Ty, TyKind};
use cranelift_codegen::ir::{AbiParam, ArgumentPurpose, Signature, Type, types};

/// How one argument crosses the C ABI.
#[derive(Clone, Debug, PartialEq)]
pub enum CArg {
    /// Nothing: a value without bytes.
    None,
    /// A scalar, in one parameter.
    Scalar,
    /// An aggregate in registers: `pad` unused parameters first (to skip registers the
    /// standard leaves unused), then each part loaded from its offset in the value.
    Parts { pad: Vec<Type>, parts: Vec<(Type, u32)> },
    /// An aggregate copied to the stack (x86-64): the parameter is the address of `size`
    /// bytes (a multiple of 8), which the call copies.
    Stack(u32),
    /// An aggregate passed as the address of a copy (AArch64, larger than 16 bytes).
    Indirect,
}

/// How a result crosses the C ABI.
#[derive(Clone, Debug, PartialEq)]
pub enum CRet {
    None,
    Scalar,
    /// An aggregate in registers, each part stored at its offset in the value.
    Parts(Vec<(Type, u32)>),
    /// An aggregate written through a hidden pointer, the signature's first parameter.
    Sret,
}

/// The C signature of a function, and how each value maps to its parameters.
pub struct CSig {
    pub sig: Signature,
    pub args: Vec<CArg>,
    pub ret: CRet,
}

impl CSig {
    /// True if every value is a scalar (or nothing), so the signature is also the plain
    /// convention's.
    pub fn is_scalar(&self) -> bool {
        self.args.iter().all(|a| matches!(a, CArg::Scalar | CArg::None))
            && matches!(self.ret, CRet::Scalar | CRet::None)
    }
}

/// The class of a scalar piece of an aggregate.
#[derive(Clone, Copy, PartialEq, Debug)]
enum Class {
    Int,
    Float(Type),
}

/// A scalar piece of an aggregate: its offset, size and class.
type Piece = (u64, u64, Class);

/// Registers left for arguments, counted as the standards assign them.
struct Regs {
    int: u32,
    float: u32,
}

impl<'a> Gen<'a> {
    /// The C signature of foreign or exported function `f`.
    pub fn c_sig_of(&self, f: FnId) -> CResult<CSig> {
        let def = &self.prog.fns[f.index()];
        let tys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
        self.c_sig(&tys, def.ret, def.callconv, def.span)
    }

    /// The C signature of a function with parameters `tys` and result `ret`.
    pub fn c_sig(&self, tys: &[Ty], ret: Ty, cc: crate::types::CallConv, span: Span) -> CResult<CSig> {
        let mut sig = Signature::new(self.callconv(cc));
        let win64 = cc == crate::types::CallConv::Win64;
        let x64 = self.prog.target.arch == "X86_64";
        let mut regs = if x64 { Regs { int: 6, float: 8 } } else { Regs { int: 8, float: 8 } };
        let cret = match self.rep(ret) {
            Rep::Zero => CRet::None,
            Rep::F16 => return unsupported(span, format!("passing `{}` to or from C", ret)),
            Rep::Mem { size, .. } => {
                let parts = if win64 { win64_parts(size) } else { self.classify(ret, span)? };
                match parts {
                    Some(parts) => {
                        sig.returns.extend(parts.iter().map(|(t, _)| AbiParam::new(*t)));
                        CRet::Parts(parts)
                    }
                    _ => {
                        sig.params.push(AbiParam::special(types::I64, ArgumentPurpose::StructReturn));
                        if x64 && !win64 {
                            // The hidden pointer takes the first integer register (AArch64
                            // passes it in x8).
                            regs.int -= 1;
                        }
                        CRet::Sret
                    }
                }
            }
            r => {
                sig.returns.push(r.abi().unwrap());
                CRet::Scalar
            }
        };
        let mut args = vec![];
        for &t in tys {
            let r = self.rep(t);
            let a = match r {
                Rep::Zero => CArg::None,
                Rep::F16 => return unsupported(span, format!("passing `{}` to or from C", t)),
                Rep::Mem { size, .. } => {
                    let a = if win64 {
                        // Windows x64 passes an aggregate of 1, 2, 4 or 8 bytes as an
                        // integer, any other as the address of a copy.
                        match win64_parts(size) {
                            Some(parts) => CArg::Parts { pad: vec![], parts },
                            None => CArg::Indirect,
                        }
                    } else if x64 {
                        self.x64_arg(t, size, &mut regs, span)?
                    } else {
                        self.a64_arg(t, &mut regs, span)?
                    };
                    match &a {
                        CArg::Parts { pad, parts } => {
                            sig.params.extend(pad.iter().chain(parts.iter().map(|(t, _)| t)).map(|t| AbiParam::new(*t)))
                        }
                        CArg::Stack(size) => {
                            sig.params.push(AbiParam::special(types::I64, ArgumentPurpose::StructArgument(*size)))
                        }
                        _ => sig.params.push(AbiParam::new(types::I64)),
                    }
                    a
                }
                r => {
                    let p = r.abi().unwrap();
                    match p.value_type {
                        t if t.is_float() => regs.float = regs.float.saturating_sub(1),
                        types::I128 => {
                            if !x64 && regs.int % 2 == 1 {
                                regs.int -= 1;
                            }
                            regs.int = regs.int.saturating_sub(2)
                        }
                        _ => regs.int = regs.int.saturating_sub(1),
                    }
                    sig.params.push(p);
                    CArg::Scalar
                }
            };
            args.push(a);
        }
        Ok(CSig { sig, args, ret: cret })
    }

    /// An aggregate argument on x86-64: its eightbytes in registers if they all fit, else
    /// on the stack.
    fn x64_arg(&self, t: Ty, size: u64, regs: &mut Regs, span: Span) -> CResult<CArg> {
        if let Some(parts) = self.classify(t, span)? {
            let ni = parts.iter().filter(|(t, _)| t.is_int()).count() as u32;
            let nf = parts.len() as u32 - ni;
            if ni <= regs.int && nf <= regs.float {
                regs.int -= ni;
                regs.float -= nf;
                return Ok(CArg::Parts { pad: vec![], parts });
            }
        }
        Ok(CArg::Stack(size.next_multiple_of(8) as u32))
    }

    /// An aggregate argument on AArch64.
    fn a64_arg(&self, t: Ty, regs: &mut Regs, span: Span) -> CResult<CArg> {
        let Some(parts) = self.classify(t, span)? else {
            // Larger than 16 bytes: the address of a copy, in an integer register.
            regs.int = regs.int.saturating_sub(1);
            return Ok(CArg::Indirect);
        };
        if parts[0].0.is_float() {
            let n = parts.len() as u32;
            if n > regs.float {
                return unsupported(span, format!("passing `{}` to C after the floating-point registers ran out", t));
            }
            regs.float -= n;
            return Ok(CArg::Parts { pad: vec![], parts });
        }
        let mut pad = vec![];
        let n = parts.len() as u32;
        if self.align(t) >= 16 && regs.int % 2 == 1 {
            // A 16-byte aligned aggregate starts at an even register.
            pad.push(types::I64);
            regs.int -= 1;
        }
        if n > regs.int {
            if self.align(t) >= 16 {
                return unsupported(span, format!("passing `{}` to C on the stack", t));
            }
            // An aggregate that does not fit the registers left goes to the stack whole,
            // and no later argument uses an integer register.
            pad.extend((0..regs.int).map(|_| types::I64));
            regs.int = 0;
        } else {
            regs.int -= n;
        }
        Ok(CArg::Parts { pad, parts })
    }

    /// The registers of an aggregate of type `t` passed by value, or `None` if it is passed
    /// in memory. On x86-64: one per eightbyte, `I64` if it holds an integer, else `F64`
    /// (none if larger than 16 bytes or a field is misaligned). On AArch64: up to four
    /// floats of one type (a homogeneous floating-point aggregate), else one or two `I64`
    /// (none if larger than 16 bytes).
    fn classify(&self, t: Ty, span: Span) -> CResult<Option<Vec<(Type, u32)>>> {
        let size = self.size(t);
        let x64 = self.prog.target.arch == "X86_64";
        if size > 32 || (x64 && size > 16) {
            return Ok(None);
        }
        let mut pieces = vec![];
        if let Err(what) = self.pieces(t, 0, &mut pieces) {
            return unsupported(span, format!("passing `{}` by value to or from C (it holds {})", t, what));
        }
        pieces.sort_by_key(|p| p.0);
        pieces.dedup();
        if x64 {
            if pieces.iter().any(|&(off, sz, _)| off % sz != 0) {
                return Ok(None);
            }
            let parts = (0..size.div_ceil(8))
                .map(|i| {
                    let int = pieces.iter().any(|&(off, sz, c)| off < 8 * i + 8 && off + sz > 8 * i && c == Class::Int);
                    (if int { types::I64 } else { types::F64 }, 8 * i as u32)
                })
                .collect();
            return Ok(Some(parts));
        }
        // A homogeneous floating-point aggregate: one to four floats of one type, each in a
        // register of its own.
        if let Some(&(_, fsz, Class::Float(ft))) = pieces.first()
            && pieces.len() <= 4
            && pieces.iter().enumerate().all(|(i, &(off, _, c))| c == Class::Float(ft) && off == i as u64 * fsz)
            && size == pieces.len() as u64 * fsz
        {
            return Ok(Some(pieces.iter().map(|&(off, _, _)| (ft, off as u32)).collect()));
        }
        if size > 16 {
            return Ok(None);
        }
        Ok(Some((0..size.div_ceil(8)).map(|i| (types::I64, 8 * i as u32)).collect()))
    }

    /// The scalar pieces of a value of type `t` at offset `base`, or what makes it unfit for
    /// C.
    fn pieces(&self, t: Ty, base: u64, out: &mut Vec<Piece>) -> Result<(), String> {
        let size = self.size(t);
        match self.rep(t) {
            Rep::Zero => return Ok(()),
            Rep::F16 => return Err("an `f16`".into()),
            Rep::F32 => out.push((base, 4, Class::Float(types::F32))),
            Rep::F64 => out.push((base, 8, Class::Float(types::F64))),
            Rep::Int { .. } | Rep::Bool | Rep::Ptr => out.push((base, size, Class::Int)),
            Rep::Mem { .. } => match t.kind() {
                TyKind::Tuple { elems, .. } => {
                    for (e, off) in elems.iter().zip(self.offsets(t)) {
                        self.pieces(*e, base + off, out)?;
                    }
                }
                TyKind::Array { len, elem, kind: ArrayKind::Plain } => {
                    let step = self.size(*elem);
                    for i in 0..*len {
                        self.pieces(*elem, base + i * step, out)?;
                    }
                }
                TyKind::Adt(id, _) => match &self.prog.adts[id].kind {
                    AdtKind::Struct { bits: Some(_), .. } => out.push((base, size, Class::Int)),
                    AdtKind::Struct { fields, .. } => {
                        for (f, off) in fields.iter().zip(self.offsets(t)) {
                            self.pieces(f.ty, base + off, out)?;
                        }
                    }
                    AdtKind::CUnion { fields } => {
                        for f in fields {
                            self.pieces(f.ty, base, out)?;
                        }
                    }
                    AdtKind::Union { tag, variants } => {
                        let offs = self.offsets(t);
                        self.pieces(*tag, base + offs[0], out)?;
                        for p in variants.iter().filter_map(|v| v.payload) {
                            self.pieces(p, base + offs[1], out)?;
                        }
                    }
                },
                _ => return Err(format!("a `{}`", t)),
            },
        }
        Ok(())
    }
}

/// How Windows x64 passes an aggregate of `size` bytes: as one integer if it has 1, 2, 4
/// or 8 bytes, else in memory.
fn win64_parts(size: u64) -> Option<Vec<(Type, u32)>> {
    matches!(size, 1 | 2 | 4 | 8).then(|| vec![(super::int_type(size), 0)])
}

/// The bytes a temporary needs to hold `parts` at their offsets.
pub fn parts_size(parts: &[(Type, u32)]) -> u64 {
    parts.iter().map(|(t, off)| *off as u64 + t.bytes() as u64).max().unwrap_or(0)
}
