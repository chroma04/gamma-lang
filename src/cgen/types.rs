//! C representations of Gamma types, and the type descriptors that the
//! runtime's formatter reads.
//!
//! Scalars (integers up to 128 bits, floats, `bool`, pointers) are native C
//! types. Every other first-class type is a *blob*: a struct holding its bytes
//! with the size and alignment of the Gamma layout, so values have exactly the
//! bytes the interpreter gives them. Fields are read and written with `memcpy`
//! at the layout's offsets.

use crate::layout::{Layouts, is_nullable_pointer};
use crate::tir::{AdtKind, Program};
use crate::types::{FloatTy, IntTy, Ty, TyKind};
use std::collections::{BTreeSet, HashMap};
use std::fmt::Write;

pub struct Types<'a> {
    pub prog: &'a Program,
    pub lay: &'a Layouts<'a>,
    /// Blob typedefs needed: (size, align).
    blobs: BTreeSet<(u64, u64)>,
    desc_ids: HashMap<Ty, usize>,
    desc_fwd: String,
    desc_parts: String,
    desc_defs: String,
}

/// How a type is represented in C.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Repr {
    /// A native integer of `bits` bits stored in a C type of `storage` bytes.
    Int {
        signed: bool,
        bits: u32,
        storage: u64,
    },
    Float(FloatTy),
    Bool,
    /// Any pointer, function pointer, or optional pointer (null is 0): `void *`.
    Ptr,
    /// A second-class function value (`gm_closure`).
    Closure,
    /// A handler value (`gm_hval`).
    Handler,
    /// Bytes in a blob struct.
    Blob,
}

impl<'a> Types<'a> {
    pub fn new(prog: &'a Program, lay: &'a Layouts<'a>) -> Types<'a> {
        Types {
            prog,
            lay,
            blobs: BTreeSet::new(),
            desc_ids: HashMap::new(),
            desc_fwd: String::new(),
            desc_parts: String::new(),
            desc_defs: String::new(),
        }
    }

    pub fn size(&self, t: Ty) -> u64 {
        self.lay.size_of(t)
    }

    pub fn align(&self, t: Ty) -> u64 {
        self.lay.align_of(t)
    }

    pub fn offsets(&self, t: Ty) -> Vec<u64> {
        self.lay.layout(t).offsets
    }

    pub fn repr(&self, t: Ty) -> Repr {
        match t.kind() {
            TyKind::Int(IntTy { signed, bits }) if *bits <= 128 => {
                Repr::Int { signed: *signed, bits: *bits as u32, storage: self.size(t) }
            }
            TyKind::Usize | TyKind::Isize => Repr::Int {
                signed: matches!(t.kind(), TyKind::Isize),
                bits: self.lay.target.ptr_bits(),
                storage: self.lay.target.ptr_bytes,
            },
            TyKind::Float(f) => Repr::Float(*f),
            TyKind::Bool => Repr::Bool,
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => Repr::Ptr,
            TyKind::Optional(inner) if is_nullable_pointer(*inner) => Repr::Ptr,
            TyKind::Fn(_) => Repr::Closure,
            TyKind::Handler(_) => Repr::Handler,
            _ => Repr::Blob,
        }
    }

    /// The C type of values of `t`.
    pub fn cty(&mut self, t: Ty) -> String {
        match self.repr(t) {
            Repr::Int { signed, storage, .. } => int_cty(signed, storage).to_string(),
            Repr::Float(FloatTy::F16) => "_Float16".into(),
            Repr::Float(FloatTy::F32) => "float".into(),
            Repr::Float(FloatTy::F64) => "double".into(),
            Repr::Bool => "uint8_t".into(),
            Repr::Ptr => "void *".into(),
            Repr::Closure => "gm_closure".into(),
            Repr::Handler => "gm_hval".into(),
            Repr::Blob => {
                let (s, a) = (self.size(t), self.align(t).max(1));
                self.blobs.insert((s, a));
                blob_name(s, a)
            }
        }
    }

    /// Typedefs of the blob types used.
    pub fn blob_typedefs(&self) -> String {
        let mut out = String::new();
        for (s, a) in &self.blobs {
            let _ = writeln!(out, "typedef struct {{ _Alignas({}) uint8_t b[{}]; }} {};", a, s, blob_name(*s, *a));
        }
        out
    }

    /// The C expression `&gm_tyN` for the descriptor of `t` (§21.6.1 formatting).
    pub fn desc(&mut self, t: Ty) -> String {
        if let Some(&i) = self.desc_ids.get(&t) {
            return format!("&gm_ty{}", i);
        }
        let i = self.desc_ids.len();
        self.desc_ids.insert(t, i);
        let _ = writeln!(self.desc_fwd, "static const gm_type gm_ty{};", i);
        let (size, align) = (self.size(t), self.align(t));
        let mut f: Vec<String> = vec![format!(".size = {}", size), format!(".align = {}", align)];
        match t.kind() {
            TyKind::Int(IntTy { signed, bits }) => {
                f.push("kind = GM_K_INT".into());
                f.push(format!("bits = {}", bits));
                f.push(format!("is_signed = {}", signed));
            }
            TyKind::Usize | TyKind::Isize => {
                f.push("kind = GM_K_INT".into());
                f.push(format!("bits = {}", self.lay.target.ptr_bits()));
                f.push(format!("is_signed = {}", matches!(t.kind(), TyKind::Isize)));
            }
            TyKind::Float(ft) => {
                f.push("kind = GM_K_FLOAT".into());
                f.push(format!("bits = {}", ft.bits()));
            }
            TyKind::Bool => f.push("kind = GM_K_BOOL".into()),
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => {
                f.push("kind = GM_K_PTR".into())
            }
            TyKind::Slice { elem, .. } => {
                let e = self.desc(*elem);
                f.push("kind = GM_K_SLICE".into());
                f.push(format!("elem = {}", e));
            }
            TyKind::Array { len, elem } => {
                let e = self.desc(*elem);
                f.push("kind = GM_K_ARRAY".into());
                f.push(format!("elem = {}", e));
                f.push(format!("len = {}", len));
            }
            TyKind::Tuple { elems, names } => {
                let offs = self.offsets(t);
                let mut parts = vec![];
                for (k, et) in elems.iter().enumerate() {
                    let d = self.desc(*et);
                    let name = names.as_ref().map_or("NULL".to_string(), |n| c_string(n[k].as_str().as_bytes()));
                    parts.push(format!("{{{}, {}, {}}}", name, offs[k], d));
                }
                f.push("kind = GM_K_TUPLE".into());
                f.push(format!("named = {}", names.is_some()));
                self.fields_array(i, &parts, &mut f);
            }
            TyKind::Optional(inner) => {
                let e = self.desc(*inner);
                f.push("kind = GM_K_OPTIONAL".into());
                f.push(format!("elem = {}", e));
                if is_nullable_pointer(*inner) {
                    f.push("flag_offset = GM_NICHE".into());
                } else {
                    f.push(format!("flag_offset = {}", self.offsets(t)[1]));
                }
            }
            TyKind::Result { err, ok } => {
                let (e, o) = (self.desc(*err), self.desc(*ok));
                let offs = self.offsets(t);
                f.push("kind = GM_K_RESULT".into());
                f.push(format!("elem = {}", o));
                f.push(format!("err = {}", e));
                f.push(format!("tag_offset = {}", offs[0]));
                f.push(format!("payload_offset = {}", offs[1]));
            }
            TyKind::Adt(id, _) => {
                let def = &self.prog.adts[id];
                f.push(format!("name = {}", c_string(def.name.as_str().as_bytes())));
                match &def.kind {
                    AdtKind::Struct { fields, bits: None, .. } => {
                        let offs = self.offsets(t);
                        let mut parts = vec![];
                        for (k, fd) in fields.iter().enumerate() {
                            let d = self.desc(fd.ty);
                            parts.push(format!("{{{}, {}, {}}}", c_string(fd.name.as_str().as_bytes()), offs[k], d));
                        }
                        f.push("kind = GM_K_STRUCT".into());
                        self.fields_array(i, &parts, &mut f);
                    }
                    AdtKind::Union { tag, variants } => {
                        let offs = self.offsets(t);
                        let mut parts = vec![];
                        for v in variants {
                            let p = match v.payload {
                                Some(p) => self.desc(p),
                                None => "NULL".into(),
                            };
                            parts.push(format!("{{{}, {}, {}}}", c_string(v.name.as_str().as_bytes()), v.tag, p));
                        }
                        let _ = writeln!(
                            self.desc_parts,
                            "static const gm_variant gm_ty{}_v[] = {{{}}};",
                            i,
                            parts.join(", ")
                        );
                        f.push("kind = GM_K_UNION".into());
                        f.push(format!("nvariants = {}", variants.len()));
                        f.push(format!("variants = gm_ty{}_v", i));
                        f.push(format!("tag_offset = {}", offs[0]));
                        f.push(format!("payload_offset = {}", offs.get(1).copied().unwrap_or(0)));
                        f.push(format!("tag_size = {}", self.size(*tag)));
                        f.push(format!("tag_signed = {}", tag.is_signed_integer()));
                    }
                    _ => {
                        f.push("kind = GM_K_OPAQUE".into());
                        f.push(format!("name = {}", c_string(format!("<{}>", def.name).as_bytes())));
                    }
                }
            }
            _ => {
                f.push("kind = GM_K_OPAQUE".into());
                f.push(format!("name = {}", c_string(format!("<{}>", t).as_bytes())));
            }
        }
        let fields: Vec<String> =
            f.iter().map(|x| if x.starts_with('.') { x.clone() } else { format!(".{}", x) }).collect();
        let _ = writeln!(self.desc_defs, "static const gm_type gm_ty{} = {{{}}};", i, fields.join(", "));
        format!("&gm_ty{}", i)
    }

    fn fields_array(&mut self, i: usize, parts: &[String], f: &mut Vec<String>) {
        if parts.is_empty() {
            f.push("nfields = 0".into());
            return;
        }
        let _ = writeln!(self.desc_parts, "static const gm_field gm_ty{}_f[] = {{{}}};", i, parts.join(", "));
        f.push(format!("nfields = {}", parts.len()));
        f.push(format!("fields = gm_ty{}_f", i));
    }

    /// The descriptor definitions, in dependency-safe order.
    pub fn descriptors(&self) -> String {
        format!("{}{}{}", self.desc_fwd, self.desc_parts, self.desc_defs)
    }
}

pub fn int_cty(signed: bool, storage: u64) -> &'static str {
    match (signed, storage) {
        (false, 1) => "uint8_t",
        (true, 1) => "int8_t",
        (false, 2) => "uint16_t",
        (true, 2) => "int16_t",
        (false, 4) => "uint32_t",
        (true, 4) => "int32_t",
        (false, 8) => "uint64_t",
        (true, 8) => "int64_t",
        (false, _) => "unsigned __int128",
        (true, _) => "__int128",
    }
}

pub fn blob_name(size: u64, align: u64) -> String {
    format!("gm_b{}_{}", size, align)
}

/// A C string literal holding `bytes` (escaped so any byte is safe).
pub fn c_string(bytes: &[u8]) -> String {
    let mut s = String::from("\"");
    for &b in bytes {
        match b {
            b'"' => s.push_str("\\\""),
            b'\\' => s.push_str("\\\\"),
            0x20..=0x7e if b != b'?' => s.push(b as char),
            _ => {
                let _ = write!(s, "\\{:03o}", b);
            }
        }
    }
    s.push('"');
    s
}
