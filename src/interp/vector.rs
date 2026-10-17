//! The builtins of vectors `#simd [N]T` (§5.6.2), lane by lane with the scalar rules.

use super::bytes::Bytes;
use super::machine::*;
use super::value::*;
use crate::source::Span;
use crate::tir::{BinOp, Builtin, Flavor};
use crate::types::*;

impl<'a> Machine<'a> {
    /// The lane count, lane type and lane size of a vector type.
    fn lanes_of(&self, t: Ty) -> (usize, Ty, usize) {
        match t.kind() {
            TyKind::Array { len, elem, .. } => (*len as usize, *elem, self.size_of(*elem)),
            _ => (0, t, 0),
        }
    }

    /// A vector builtin applied to the argument values `v` of types `tys`, or `None` if
    /// `op` is not one. `VecLoad` and `VecStore` reach memory.
    pub fn vector_builtin(&mut self, span: Span, op: Builtin, res_ty: Ty, tys: &[Ty], v: &[Bytes]) -> Option<R<Bytes>> {
        Some(match op {
            Builtin::LaneEq
            | Builtin::LaneNe
            | Builtin::LaneLt
            | Builtin::LaneLe
            | Builtin::LaneGt
            | Builtin::LaneGe => self.lane_compare(span, op, tys[0], &v[0], &v[1]),
            Builtin::ReduceSum | Builtin::ReduceMin | Builtin::ReduceMax => {
                let (n, lane, sz) = self.lanes_of(tys[0]);
                let mut acc = Bytes::from_slice(&v[0][..sz]);
                for i in 1..n {
                    let x = Bytes::from_slice(&v[0][i * sz..(i + 1) * sz]);
                    acc = match op {
                        Builtin::ReduceSum => {
                            match self.binop(span, BinOp::Add(Flavor::Checked), lane, lane, lane, &acc, &x) {
                                Ok(b) => b,
                                Err(e) => return Some(Err(e)),
                            }
                        }
                        _ => {
                            let m = if op == Builtin::ReduceMin { Builtin::Min } else { Builtin::Max };
                            match self.builtin(span, m, lane, &[lane, lane], &[acc, x]) {
                                Ok(b) => b,
                                Err(e) => return Some(Err(e)),
                            }
                        }
                    };
                }
                Ok(acc)
            }
            Builtin::MaskAll => Ok(self.bool_bytes(v[0].iter().all(|b| *b != 0))),
            Builtin::MaskAny => Ok(self.bool_bytes(v[0].iter().any(|b| *b != 0))),
            Builtin::MaskCount => Ok(self.usize_bytes(v[0].iter().filter(|b| **b != 0).count() as u64)),
            Builtin::Select => {
                let (n, _, sz) = self.lanes_of(tys[1]);
                let mut out = v[2].clone();
                for i in 0..n {
                    if v[0][i] != 0 {
                        out[i * sz..(i + 1) * sz].copy_from_slice(&v[1][i * sz..(i + 1) * sz]);
                    }
                }
                Ok(out)
            }
            Builtin::LaneMin | Builtin::LaneMax | Builtin::LaneAbs => {
                let (n, lane, sz) = self.lanes_of(tys[0]);
                let scalar = match op {
                    Builtin::LaneMin => Builtin::Min,
                    Builtin::LaneMax => Builtin::Max,
                    _ => Builtin::Abs,
                };
                let mut out = Bytes::zeroed(n * sz);
                for i in 0..n {
                    let lanes: Vec<Bytes> = v.iter().map(|x| Bytes::from_slice(&x[i * sz..(i + 1) * sz])).collect();
                    let tys = vec![lane; lanes.len()];
                    match self.builtin(span, scalar, lane, &tys, &lanes) {
                        Ok(b) => out[i * sz..(i + 1) * sz].copy_from_slice(&b),
                        Err(e) => return Some(Err(e)),
                    }
                }
                Ok(out)
            }
            Builtin::LaneConvert => {
                let (n, from, fsz) = self.lanes_of(tys[0]);
                let (_, to, tsz) = self.lanes_of(res_ty);
                let how = if to.is_float() { Builtin::Round } else { Builtin::Convert };
                let mut out = Bytes::zeroed(n * tsz);
                for i in 0..n {
                    match self.convert(span, how, from, to, &v[0][i * fsz..(i + 1) * fsz]) {
                        Ok(b) => out[i * tsz..(i + 1) * tsz].copy_from_slice(&b),
                        Err(e) => return Some(Err(e)),
                    }
                }
                Ok(out)
            }
            Builtin::Shuffle => {
                let (_, _, sz) = self.lanes_of(tys[0]);
                let mut out = Bytes::zeroed(sz * (v.len() - 1));
                for (j, k) in v[1..].iter().enumerate() {
                    let k = self.int_of(Ty::usize(), k).to_u64_sat() as usize;
                    out[j * sz..(j + 1) * sz].copy_from_slice(&v[0][k * sz..(k + 1) * sz]);
                }
                Ok(out)
            }
            Builtin::VecLoad | Builtin::VecStore => {
                let (vt, s) = if op == Builtin::VecLoad { (res_ty, &v[0]) } else { (tys[0], &v[1]) };
                let (n, lane, sz) = self.lanes_of(vt);
                let (p, len) = self.slice_parts(s);
                if len < n as u64 && !self.fast() {
                    let what = if op == Builtin::VecLoad { "load" } else { "store" };
                    return Some(
                        self.trap(span, format!("vector {} of {} lanes with a slice of {} elements", what, n, len)),
                    );
                }
                let align = self.ti(lane).align;
                if op == Builtin::VecLoad {
                    self.load(span, p, n * sz, align)
                } else {
                    self.store(span, p, &v[0], align).map(|()| Bytes::from_slice(&[]))
                }
            }
            _ => return None,
        })
    }

    fn lane_compare(&self, span: Span, op: Builtin, t: Ty, a: &[u8], b: &[u8]) -> R<Bytes> {
        let (n, lane, sz) = self.lanes_of(t);
        let mut out = Bytes::zeroed(n);
        for i in 0..n {
            let (x, y) = (&a[i * sz..(i + 1) * sz], &b[i * sz..(i + 1) * sz]);
            let r = match op {
                Builtin::LaneEq => self.eq(lane, x, y),
                Builtin::LaneNe => self.eq(lane, x, y).map(|e| !e),
                Builtin::LaneLt => self.cmp_op(BinOp::Lt, lane, x, y),
                Builtin::LaneLe => self.cmp_op(BinOp::Le, lane, x, y),
                Builtin::LaneGt => self.cmp_op(BinOp::Gt, lane, x, y),
                _ => self.cmp_op(BinOp::Ge, lane, x, y),
            };
            out[i] = self.tr(span, r)? as u8;
        }
        Ok(out)
    }
}
