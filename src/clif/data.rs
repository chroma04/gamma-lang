//! Static data: strings, the type descriptors the runtime formats and compares values with,
//! format specs, constants, globals and the clause tables of handlers.

use super::{CResult, Gen, Work, module_err, unsupported};
use crate::source::Span;
use crate::tir::{AdtKind, ConstValue, FmtAlign, FmtKind, FmtSpec, StaticData};
use crate::types::{Ty, TyKind};
use cranelift_module::{DataDescription, DataId, FuncId, Linkage, Module};

/// A `#bits` struct's integer type and its fields as (bit offset, width, type).
type BitsLayout = (Ty, Vec<(u32, u32, Ty)>);

/// A pointer stored in static data.
enum Reloc {
    /// The address of data, plus an addend.
    Data(DataId, i64),
    /// The address of a function.
    Fn(FuncId),
}

/// The kinds of type descriptors (`gm_kind` in the runtime).
const K_INT: u32 = 0;
const K_FLOAT: u32 = 1;
const K_BOOL: u32 = 2;
const K_PTR: u32 = 3;
const K_SLICE: u32 = 4;
const K_ARRAY: u32 = 5;
const K_TUPLE: u32 = 6;
const K_OPTIONAL: u32 = 7;
const K_RESULT: u32 = 8;
const K_STRUCT: u32 = 9;
const K_UNION: u32 = 10;
const K_OPAQUE: u32 = 11;
const K_BITS: u32 = 12;

fn put32(b: &mut [u8], off: usize, v: u64) {
    b[off..off + 4].copy_from_slice(&(v as u32).to_le_bytes());
}

fn put64(b: &mut [u8], off: usize, v: u64) {
    b[off..off + 8].copy_from_slice(&v.to_le_bytes());
}

impl Gen<'_> {
    fn define_bytes(&mut self, id: DataId, bytes: Vec<u8>, align: u64, relocs: Vec<(u32, Reloc)>) -> CResult<()> {
        let mut d = DataDescription::new();
        d.define(bytes.into_boxed_slice());
        d.set_align(align.max(1));
        for (off, r) in relocs {
            match r {
                Reloc::Data(t, add) => {
                    let gv = self.module.declare_data_in_data(t, &mut d);
                    d.write_data_addr(off, gv, add);
                }
                Reloc::Fn(f) => {
                    let fr = self.module.declare_func_in_data(f, &mut d);
                    d.write_function_addr(off, fr);
                }
            }
        }
        self.module.define_data(id, &d).map_err(module_err)
    }

    /// Read-only static bytes followed by a NUL byte (string contents, C strings).
    pub fn cstring(&mut self, bytes: &[u8]) -> CResult<DataId> {
        if let Some(&id) = self.bytes.get(bytes) {
            return Ok(id);
        }
        let id = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        let mut b = bytes.to_vec();
        b.push(0);
        self.define_bytes(id, b, 1, vec![])?;
        self.bytes.insert(bytes.to_vec(), id);
        Ok(id)
    }

    /// The path of source file `file`, as trap locations name it.
    pub fn src_file(&mut self, file: u32) -> CResult<DataId> {
        if let Some(&id) = self.srcs.get(&file) {
            return Ok(id);
        }
        let path = self.sources.file(crate::source::FileId(file)).path.display().to_string();
        let id = self.cstring(path.as_bytes())?;
        self.srcs.insert(file, id);
        Ok(id)
    }

    /// A format spec (`gm_spec`).
    pub fn spec_data(&mut self, s: &FmtSpec) -> CResult<DataId> {
        let mut b = [0u8; 16];
        b[0] = s.fill;
        b[1] = match s.align {
            FmtAlign::Auto => 0,
            FmtAlign::Left => 1,
            FmtAlign::Right => 2,
            FmtAlign::Center => 3,
        };
        b[2] = s.plus as u8;
        b[3] = s.alt as u8;
        b[4] = s.zero as u8;
        b[5] = match s.kind {
            FmtKind::Display => 0,
            FmtKind::Hex => 1,
            FmtKind::HexUpper => 2,
            FmtKind::Oct => 3,
            FmtKind::Bin => 4,
            FmtKind::Exp => 5,
        };
        put32(&mut b, 8, s.width as u64);
        let p = s.precision.map_or(-1i32, |p| p as i32);
        b[12..16].copy_from_slice(&p.to_le_bytes());
        if let Some(&id) = self.specs.get(&b) {
            return Ok(id);
        }
        let id = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        self.define_bytes(id, b.to_vec(), 8, vec![])?;
        self.specs.insert(b, id);
        Ok(id)
    }

    /// The width in bits of a `#bits` field of type `t` (§5.10.5).
    pub fn bit_width(&self, t: Ty) -> u32 {
        match t.kind() {
            TyKind::Bool => 1,
            TyKind::Int(i) => i.bits as u32,
            TyKind::Adt(id, _) => match &self.prog.adts[id].kind {
                AdtKind::Union { tag, .. } => self.bit_width(*tag),
                AdtKind::Struct { bits: Some(u), .. } => self.bit_width(*u),
                _ => 8 * self.size(t) as u32,
            },
            _ => 8 * self.size(t) as u32,
        }
    }

    /// For a `#bits` struct: its underlying integer type and each field's bit offset,
    /// width and type, from the least significant bit.
    pub fn bits_layout(&self, t: Ty) -> Option<BitsLayout> {
        let TyKind::Adt(id, _) = t.kind() else { return None };
        let AdtKind::Struct { fields, bits: Some(u), .. } = &self.prog.adts[id].kind else { return None };
        let mut off = 0;
        let mut out = vec![];
        for fd in fields {
            let w = self.bit_width(fd.ty);
            out.push((off, w, fd.ty));
            off += w;
        }
        Some((*u, out))
    }

    /// The type descriptor of `t` (`gm_type`), as the C back end describes it.
    pub fn desc(&mut self, t: Ty) -> CResult<DataId> {
        if let Some(&id) = self.descs.get(&t) {
            return Ok(id);
        }
        let id = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        self.descs.insert(t, id);
        let mut b = vec![0u8; 104];
        let mut relocs: Vec<(u32, Reloc)> = vec![];
        put32(&mut b, 4, self.size(t));
        put32(&mut b, 8, self.align(t));
        let mut kind = K_OPAQUE;
        let mut name: Option<String> = None;
        match t.kind() {
            TyKind::Int(i) => {
                kind = K_INT;
                b[24..26].copy_from_slice(&i.bits.to_le_bytes());
                b[26] = i.signed as u8;
            }
            TyKind::Usize | TyKind::Isize => {
                kind = K_INT;
                b[24..26].copy_from_slice(&(self.prog.target.ptr_bits() as u16).to_le_bytes());
                b[26] = matches!(t.kind(), TyKind::Isize) as u8;
            }
            TyKind::Float(f) => {
                kind = K_FLOAT;
                b[24..26].copy_from_slice(&(f.bits() as u16).to_le_bytes());
            }
            TyKind::Bool => kind = K_BOOL,
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => kind = K_PTR,
            TyKind::Slice { elem, .. } => {
                kind = K_SLICE;
                let e = self.desc(*elem)?;
                relocs.push((32, Reloc::Data(e, 0)));
            }
            TyKind::Array { len, elem, .. } => {
                kind = K_ARRAY;
                let e = self.desc(*elem)?;
                relocs.push((32, Reloc::Data(e, 0)));
                put64(&mut b, 48, *len);
            }
            TyKind::Tuple { elems, names } => {
                kind = K_TUPLE;
                let offs = self.offsets(t);
                let mut fields = vec![];
                for (k, et) in elems.iter().enumerate() {
                    let n = names.as_ref().map(|n| n[k].as_str().to_string());
                    fields.push((n, offs[k], *et));
                }
                b[72] = names.is_some() as u8;
                self.fields_array(&mut b, &mut relocs, &fields)?;
            }
            TyKind::Optional(inner) => {
                kind = K_OPTIONAL;
                let e = self.desc(*inner)?;
                relocs.push((32, Reloc::Data(e, 0)));
                let flag = if crate::layout::is_nullable_pointer(*inner) { 0xffff_ffff } else { self.offsets(t)[1] };
                put32(&mut b, 76, flag);
            }
            TyKind::Result { err, ok } => {
                kind = K_RESULT;
                let (e, o) = (self.desc(*err)?, self.desc(*ok)?);
                relocs.push((32, Reloc::Data(o, 0)));
                relocs.push((40, Reloc::Data(e, 0)));
                let offs = self.offsets(t);
                put32(&mut b, 80, offs[0]);
                put32(&mut b, 84, offs[1]);
            }
            TyKind::Adt(aid, _) => {
                let def = &self.prog.adts[aid];
                name = Some(def.name.as_str().to_string());
                match &def.kind {
                    AdtKind::Struct { fields, bits: None, .. } => {
                        kind = K_STRUCT;
                        let offs = self.offsets(t);
                        let fs: Vec<(Option<String>, u64, Ty)> = fields
                            .iter()
                            .enumerate()
                            .map(|(k, fd)| (Some(fd.name.as_str().to_string()), offs[k], fd.ty))
                            .collect();
                        self.fields_array(&mut b, &mut relocs, &fs)?;
                    }
                    AdtKind::Struct { fields, bits: Some(_), .. } => {
                        kind = K_BITS;
                        let layout = self.bits_layout(t).unwrap();
                        let fs: Vec<(Option<String>, u64, Ty)> = fields
                            .iter()
                            .zip(&layout.1)
                            .map(|(fd, (off, _, fty))| (Some(fd.name.as_str().to_string()), *off as u64, *fty))
                            .collect();
                        let w = self.bit_width(t);
                        b[24..26].copy_from_slice(&(w as u16).to_le_bytes());
                        self.fields_array(&mut b, &mut relocs, &fs)?;
                    }
                    AdtKind::Union { tag, variants } => {
                        kind = K_UNION;
                        let offs = self.offsets(t);
                        let mut vb = vec![0u8; 24 * variants.len()];
                        let mut vr = vec![];
                        for (k, v) in variants.iter().enumerate() {
                            let n = self.cstring(v.name.as_str().as_bytes())?;
                            vr.push((24 * k as u32, Reloc::Data(n, 0)));
                            let tagv: i64 = i64::try_from(&v.tag).unwrap_or(0);
                            vb[24 * k + 8..24 * k + 16].copy_from_slice(&tagv.to_le_bytes());
                            if let Some(p) = v.payload {
                                let pd = self.desc(p)?;
                                vr.push((24 * k as u32 + 16, Reloc::Data(pd, 0)));
                            }
                        }
                        if !variants.is_empty() {
                            let va = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
                            self.define_bytes(va, vb, 8, vr)?;
                            relocs.push((96, Reloc::Data(va, 0)));
                        }
                        put32(&mut b, 92, variants.len() as u64);
                        put32(&mut b, 80, offs[0]);
                        put32(&mut b, 84, offs.get(1).copied().unwrap_or(0));
                        b[88] = self.size(*tag) as u8;
                        let w = self.bit_width(*tag);
                        b[24..26].copy_from_slice(&(w as u16).to_le_bytes());
                        b[89] = tag.is_signed_integer() as u8;
                    }
                    AdtKind::CUnion { .. } => {
                        // Values of a `#c_union` are never equal (§7.6).
                        name = Some(format!("<{}>", def.name));
                        b[24] = 1;
                    }
                }
            }
            _ => name = Some(format!("<{}>", t)),
        }
        put32(&mut b, 0, kind as u64);
        if let Some(n) = name {
            let s = self.cstring(n.as_bytes())?;
            relocs.push((16, Reloc::Data(s, 0)));
        }
        self.define_bytes(id, b, 8, relocs)?;
        Ok(id)
    }

    /// The fields of a tuple or struct descriptor: a `gm_field` array.
    fn fields_array(
        &mut self,
        b: &mut [u8],
        relocs: &mut Vec<(u32, Reloc)>,
        fields: &[(Option<String>, u64, Ty)],
    ) -> CResult<()> {
        put32(b, 56, fields.len() as u64);
        if fields.is_empty() {
            return Ok(());
        }
        let mut fb = vec![0u8; 24 * fields.len()];
        let mut fr = vec![];
        for (k, (name, off, ty)) in fields.iter().enumerate() {
            if let Some(n) = name {
                let s = self.cstring(n.as_bytes())?;
                fr.push((24 * k as u32, Reloc::Data(s, 0)));
            }
            put32(&mut fb, 24 * k + 8, *off);
            let d = self.desc(*ty)?;
            fr.push((24 * k as u32 + 16, Reloc::Data(d, 0)));
        }
        let fa = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        self.define_bytes(fa, fb, 8, fr)?;
        relocs.push((64, Reloc::Data(fa, 0)));
        Ok(())
    }

    /// The relocations of a compile-time value.
    fn value_relocs(&mut self, v: &ConstValue, span: Span) -> CResult<Vec<(u32, Reloc)>> {
        let mut out = vec![];
        for r in &v.relocs {
            let reloc = match &r.target {
                StaticData::Bytes(b) => Reloc::Data(self.cstring(b)?, r.addend as i64),
                StaticData::Const(k) => Reloc::Data(self.const_data(k.0)?, r.addend as i64),
                StaticData::Fn(f) => {
                    if r.addend != 0 {
                        return unsupported(span, "an offset function address in a constant");
                    }
                    Reloc::Fn(self.func(*f)?)
                }
            };
            out.push((r.offset as u32, reloc));
        }
        Ok(out)
    }

    /// The storage of top-level constant `k`, holding its value.
    pub fn const_data(&mut self, k: u32) -> CResult<DataId> {
        if let Some(&id) = self.consts.get(&k) {
            return Ok(id);
        }
        let c = &self.prog.consts[k as usize];
        let Some(v) = &c.value else {
            return unsupported(c.span, "a constant without a compile-time value");
        };
        let (span, ty) = (c.span, c.ty);
        let v = v.clone();
        let id = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        self.consts.insert(k, id);
        let relocs = self.value_relocs(&v, span)?;
        let mut bytes = v.bytes.clone();
        bytes.resize((self.size(ty) as usize).max(bytes.len()).max(1), 0);
        let align = self.align(ty);
        self.define_bytes(id, bytes, align, relocs)?;
        Ok(id)
    }

    /// The storage of `#global` `k`, and whether it is thread-local.
    pub fn global_data(&mut self, k: u32) -> CResult<(DataId, bool)> {
        let gl = &self.prog.globals[k as usize];
        let tls = gl.thread_local && gl.link_name.is_none();
        if let Some(&id) = self.globals.get(&k) {
            return Ok((id, tls));
        }
        if let Some(link) = gl.link_name {
            let id = self.module.declare_data(link.as_str(), Linkage::Import, true, false).map_err(module_err)?;
            self.globals.insert(k, id);
            return Ok((id, false));
        }
        let (span, ty) = (gl.span, gl.ty);
        let align = self.align(ty).max(gl.align.unwrap_or(1));
        let value = gl.value.clone();
        let name = self.anon_name("gm_g");
        let id = self.module.declare_data(&name, Linkage::Local, true, tls).map_err(module_err)?;
        self.globals.insert(k, id);
        let (bytes, relocs) = match &value {
            Some(v) => (v.bytes.clone(), self.value_relocs(v, span)?),
            None => (vec![], vec![]),
        };
        let mut bytes = bytes;
        bytes.resize((self.size(ty) as usize).max(bytes.len()).max(1), 0);
        self.define_bytes(id, bytes, align, relocs)?;
        Ok((id, tls))
    }

    /// The table of the clause functions of handler literal `h`, by operation.
    pub fn handler_table(&mut self, h: usize) -> CResult<DataId> {
        if let Some(&id) = self.handler_tables.get(&h) {
            return Ok(id);
        }
        let clauses = self.prog.handlers[h].clauses.clone();
        let id = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        self.handler_tables.insert(h, id);
        let mut relocs = vec![];
        for (k, c) in clauses.iter().enumerate() {
            relocs.push((8 * k as u32, Reloc::Fn(self.func(*c)?)));
        }
        self.define_bytes(id, vec![0; 8 * clauses.len().max(1)], 8, relocs)?;
        Ok(id)
    }

    /// The table of clause thunks of handler declaration `d` (see `gen_decl_thunk`).
    pub fn decl_table(&mut self, d: usize) -> CResult<DataId> {
        if let Some(&id) = self.decl_tables.get(&d) {
            return Ok(id);
        }
        let clauses = self.prog.handler_decls[d].clauses.clone();
        let id = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        self.decl_tables.insert(d, id);
        let mut relocs = vec![];
        for (k, c) in clauses.iter().enumerate() {
            let def = &self.prog.fns[c.index()];
            let ptys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
            let sig = self.sig_of(&ptys[1..], def.ret, true, self.cc);
            let func = self.local_fn("gm_dt", &sig)?;
            self.queue.push_back(Work::DeclThunk { func, clause: *c });
            relocs.push((8 * k as u32, Reloc::Fn(func)));
        }
        self.define_bytes(id, vec![0; 8 * clauses.len().max(1)], 8, relocs)?;
        Ok(id)
    }
}
