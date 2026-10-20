//! Constants: memoized evaluation, materialization in static memory, and
//! conversion of evaluated values into relocatable [`ConstValue`]s.

use super::machine::*;
use super::memory::AllocKind;
use super::value::*;
use crate::layout;
use crate::source::Span;
use crate::tir::*;
use crate::types::*;

const ESCAPE_MSG: &str = "constant contains a pointer to memory that does not outlive compile-time evaluation";

impl<'a> Machine<'a> {
    /// The address of the (immutable) storage of constant `id`, evaluating it on first use.
    pub fn const_addr(&mut self, span: Span, id: ConstId) -> R<u64> {
        if let Some(c) = self.consts.get(&id) {
            return Ok(c.addr);
        }
        let prog = self.prog;
        let def = &prog.consts[id.index()];
        if !self.consts_busy.insert(id) {
            return self.trap(span, format!("constant `{}` depends on itself", def.name));
        }
        let r = self.const_addr_inner(span, id, def);
        self.consts_busy.remove(&id);
        r
    }

    fn const_addr_inner(&mut self, span: Span, id: ConstId, def: &'a ConstDef) -> R<u64> {
        let value = match &def.value {
            Some(v) => v.clone(),
            None => {
                let saved = self.const_mode;
                self.const_mode = true;
                let r = self.eval_closed(&def.init).and_then(|b| self.to_const_value(def.init.span, def.ty, &b));
                self.const_mode = saved;
                r?
            }
        };
        let ti = self.ti(def.ty);
        let addr = self.alloc_mem(span, ti.size, ti.align, AllocKind::Const(id.0), false)?;
        self.consts.insert(id, ConstEntry { addr });
        let mut bytes = value.bytes.clone();
        for r in &value.relocs {
            let target = match &r.target {
                StaticData::Bytes(rc) => self.string_literal(span, rc, false)?,
                StaticData::Const(c) => self.const_addr(span, *c)?,
                StaticData::Fn(f) => self.fn_addr(*f),
            };
            let p = self.ptr_bytes(target.wrapping_add(r.addend));
            let off = r.offset as usize;
            bytes[off..off + p.len()].copy_from_slice(&p);
        }
        let res = self.mem.init(addr, &bytes);
        self.mem_err(span, res)?;
        Ok(addr)
    }

    /// Converts evaluated bytes to a [`ConstValue`]: pointers into static data become
    /// relocations (their bytes are zeroed); any other pointer is an error.
    pub fn to_const_value(&self, span: Span, ty: Ty, b: &[u8]) -> R<ConstValue> {
        let mut bytes = b.to_vec();
        let mut relocs = Vec::new();
        self.scan_ptrs(span, ty, b, 0, &mut bytes, &mut relocs)?;
        Ok(ConstValue { ty, bytes, relocs })
    }

    /// Whether `addr` is in live static data or a constant, which a constant may refer to.
    fn points_to_static(&self, addr: u64) -> bool {
        matches!(self.mem.describe(addr), Some((_, _, AllocKind::Static(_) | AllocKind::Const(_), true)))
    }

    fn reloc_ptr(&self, span: Span, off: usize, b: &[u8], bytes: &mut [u8], relocs: &mut Vec<Reloc>) -> R<()> {
        let psz = self.ptr_size();
        let addr = self.ptr_of(&b[off..off + psz]);
        if addr == 0 {
            return Ok(());
        }
        let Some((base, _, kind, live)) = self.mem.describe(addr) else {
            return self.trap(span, ESCAPE_MSG);
        };
        let target = match kind {
            AllocKind::Static(i) if live => StaticData::Bytes(self.mem.statics[i as usize].clone()),
            AllocKind::Const(i) if live => StaticData::Const(ConstId(i)),
            _ => return self.trap(span, ESCAPE_MSG),
        };
        relocs.push(Reloc { offset: off as u64, target, addend: addr - base });
        bytes[off..off + psz].fill(0);
        Ok(())
    }

    fn scan_ptrs(&self, span: Span, ty: Ty, b: &[u8], off: usize, bytes: &mut [u8], relocs: &mut Vec<Reloc>) -> R<()> {
        match ty.kind() {
            // An empty slice's pointer is never dereferenced: one that points to no static
            // data (`empty_slice` makes it from the element alignment) is kept as it is.
            TyKind::Slice { .. }
                if self.ptr_of(&b[off + self.ptr_size()..off + 2 * self.ptr_size()]) == 0
                    && !self.points_to_static(self.ptr_of(&b[off..off + self.ptr_size()])) =>
            {
                Ok(())
            }
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::Slice { .. } => {
                self.reloc_ptr(span, off, b, bytes, relocs)
            }
            TyKind::FnPtr(_) => {
                let psz = self.ptr_size();
                let addr = self.ptr_of(&b[off..off + psz]);
                if addr == 0 {
                    return Ok(());
                }
                match self.fn_from_addr(addr) {
                    Some(f) => {
                        relocs.push(Reloc { offset: off as u64, target: StaticData::Fn(f), addend: 0 });
                        bytes[off..off + psz].fill(0);
                        Ok(())
                    }
                    None => self.trap(span, "constant contains an invalid function pointer"),
                }
            }
            TyKind::Array { len, elem, .. } => {
                let esz = self.size_of(*elem);
                for i in 0..*len as usize {
                    self.scan_ptrs(span, *elem, b, off + i * esz, bytes, relocs)?;
                }
                Ok(())
            }
            TyKind::Tuple { elems, .. } => {
                let ti = self.ti(ty);
                for (i, t) in elems.iter().enumerate() {
                    self.scan_ptrs(span, *t, b, off + ti.offsets[i] as usize, bytes, relocs)?;
                }
                Ok(())
            }
            TyKind::Optional(inner) => {
                if layout::is_nullable_pointer(*inner) {
                    return self.scan_ptrs(span, *inner, b, off, bytes, relocs);
                }
                let sz = self.size_of(ty);
                if self.opt_present(ty, &b[off..off + sz]) {
                    let po = self.ti(ty).offsets[0] as usize;
                    self.scan_ptrs(span, *inner, b, off + po, bytes, relocs)?;
                }
                Ok(())
            }
            TyKind::Result { err, ok } => {
                let sz = self.size_of(ty);
                let is_err = self.res_is_err(ty, &b[off..off + sz]);
                let po = self.ti(ty).offsets[1] as usize;
                self.scan_ptrs(span, if is_err { *err } else { *ok }, b, off + po, bytes, relocs)
            }
            TyKind::Adt(..) => {
                let Some(def) = self.adt(ty) else { return Ok(()) };
                match &def.kind {
                    AdtKind::Struct { bits: Some(_), .. } => Ok(()),
                    AdtKind::Struct { fields, .. } => {
                        let ti = self.ti(ty);
                        for (i, f) in fields.iter().enumerate() {
                            self.scan_ptrs(span, f.ty, b, off + ti.offsets[i] as usize, bytes, relocs)?;
                        }
                        Ok(())
                    }
                    AdtKind::Union { .. } => {
                        let sz = self.size_of(ty);
                        let v = self.tr(span, self.variant_index(ty, &b[off..off + sz]))?;
                        if let Some(pt) = self.variant_payload_ty(ty, v) {
                            self.scan_ptrs(span, pt, b, off + self.payload_offset(ty), bytes, relocs)?;
                        }
                        Ok(())
                    }
                    AdtKind::CUnion { .. } => Ok(()),
                }
            }
            _ => Ok(()),
        }
    }
}
