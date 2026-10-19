//! Large arguments that are compile-time constants (Appendix D.5).
//!
//! A large argument (`Types::by_ref`) crosses a call as an address. When it is
//! built from literals only, such as the `SourceLocation` that `@caller_location()`
//! fills in, the caller would otherwise build it in a temporary at every call and
//! pass that temporary's address: under Fil-C a local whose address escapes is
//! allocated on the garbage-collected heap, once per call. Such an argument lives
//! in static storage instead, initialized at startup like a top-level constant,
//! and every call passes its address. The callee never writes it: it copies a
//! large parameter before anything else runs, or reads in place one it never
//! writes.

use super::Gen;
use crate::tir::{ConstValue, Expr, ExprKind, Reloc, StaticData};
use crate::types::{FloatTy, TyKind};

impl Gen<'_> {
    /// The C address (`uint8_t *`) of static storage holding the value of `e`, if
    /// `e` is built from literals only.
    pub fn const_arg(&mut self, e: &Expr) -> Option<String> {
        let size = self.ty.size(e.ty) as usize;
        let mut v = ConstValue { ty: e.ty, bytes: vec![0; size], relocs: vec![] };
        if !self.const_into(e, 0, &mut v) {
            return None;
        }
        let i = match self.site_consts.iter().position(|c| *c == v) {
            Some(i) => i,
            None => {
                self.site_consts.push(v);
                self.site_consts.len() - 1
            }
        };
        Some(format!("((uint8_t *)gm_k{})", i))
    }

    /// Whether `e` is built from literals only, so that `const_arg` passes it from static
    /// storage.
    pub fn is_const_arg(&mut self, e: &Expr) -> bool {
        let size = self.ty.size(e.ty) as usize;
        let mut v = ConstValue { ty: e.ty, bytes: vec![0; size], relocs: vec![] };
        self.const_into(e, 0, &mut v)
    }

    /// Writes the value of `e` at `off` in `v`; false if `e` is not built from literals.
    fn const_into(&mut self, e: &Expr, off: usize, v: &mut ConstValue) -> bool {
        let size = self.ty.size(e.ty) as usize;
        let little = self.ty.lay.target.little_endian;
        match &e.kind {
            ExprKind::Int(n) => {
                let fill = if n.sign() == num_bigint::Sign::Minus { 0xFF } else { 0 };
                let mut b = n.to_signed_bytes_le();
                b.resize(size.max(b.len()), fill);
                v.bytes[off..off + size].copy_from_slice(&crate::layout::int_bytes_swap_order(&b[..size], little));
            }
            ExprKind::Bool(b) => v.bytes[off] = *b as u8,
            ExprKind::Float(x) => match e.ty.kind() {
                TyKind::Float(FloatTy::F32) => {
                    let b = if little { (*x as f32).to_le_bytes() } else { (*x as f32).to_be_bytes() };
                    v.bytes[off..off + 4].copy_from_slice(&b)
                }
                TyKind::Float(FloatTy::F64) => {
                    let b = if little { x.to_le_bytes() } else { x.to_be_bytes() };
                    v.bytes[off..off + 8].copy_from_slice(&b)
                }
                _ => return false,
            },
            ExprKind::ConstBytes(b) if b.len() == size => v.bytes[off..off + size].copy_from_slice(b),
            ExprKind::Str(s) => {
                let p = self.ty.lay.target.ptr_bytes as usize;
                v.relocs.push(Reloc { offset: off as u64, target: StaticData::Bytes(s.clone()), addend: 0 });
                let len = crate::layout::int_bytes_swap_order(&(s.len() as u64).to_le_bytes()[..p], little);
                v.bytes[off + p..off + 2 * p].copy_from_slice(&len);
            }
            ExprKind::CStr(s) => {
                v.relocs.push(Reloc { offset: off as u64, target: StaticData::Bytes(s.clone()), addend: 0 });
            }
            ExprKind::Aggregate(elems) if self.ty.bits_layout(e.ty).is_none() => {
                let offs = self.ty.offsets(e.ty);
                for (x, o) in elems.iter().zip(offs) {
                    if !self.const_into(x, off + o as usize, v) {
                        return false;
                    }
                }
            }
            ExprKind::Array(elems) => {
                for (i, x) in elems.iter().enumerate() {
                    let esz = self.ty.size(x.ty) as usize;
                    if !self.const_into(x, off + i * esz, v) {
                        return false;
                    }
                }
            }
            _ => return false,
        }
        true
    }
}
