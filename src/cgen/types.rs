//! C representations of Gamma types, and the type descriptors that the
//! runtime's formatter reads.
//!
//! Scalars (integers up to 128 bits, floats, `bool`, pointers) are native C
//! types. Every other first-class type, including integers wider than 128 bits,
//! is a *blob*: a struct holding its bytes
//! with the size and alignment of the Gamma layout, so values have exactly the
//! bytes the interpreter gives them. Fields are read and written with `memcpy`
//! at the layout's offsets.
//!
//! Blobs do not cross the C ABI the way C structs do (a struct of two floats is
//! passed in floating-point registers, a blob of 8 bytes in an integer register), so
//! a struct or `#c_union` whose fields are all C-compatible (§19.1) also gets a
//! *mirror*: a C type with the same fields and layout. Functions with the plain C
//! calling convention take and return mirrors, and values are copied between a
//! blob and its mirror at those calls.

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
    /// Mirror C types by Gamma type (`None`: the type has none).
    mirrors: HashMap<Ty, Option<String>>,
    mirror_defs: String,
    /// `-g`: mirror fields and frame fields are also named after the Gamma ones
    /// (see `super::debug`).
    pub debug_names: bool,
    /// The Gamma names given to members so far.
    member_names: BTreeSet<String>,
    /// `-g`: the debugger views built so far (see `super::views`).
    pub(super) views: super::views::Views,
}

/// An integer wider than 128 bits: `bits` bits, little-endian two's complement in
/// `size` bytes (a multiple of 8). The runtime's `gm_w*` routines operate on these.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct WideInfo {
    pub signed: bool,
    pub bits: u32,
    pub size: u64,
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
            mirrors: HashMap::new(),
            mirror_defs: String::new(),
            debug_names: false,
            member_names: BTreeSet::new(),
            views: Default::default(),
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

    /// Facts about `t` if it is an integer wider than 128 bits, which is held as a blob.
    pub fn wide(&self, t: Ty) -> Option<WideInfo> {
        match t.kind() {
            TyKind::Int(IntTy { signed, bits }) if *bits > 128 => {
                Some(WideInfo { signed: *signed, bits: *bits as u32, size: self.size(t) })
            }
            _ => None,
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

    /// The mirror of `t`: a C type with its fields, for a C-compatible struct,
    /// `#c_union` or `#bits` struct (the latter is its integer). `None` for other types.
    pub fn mirror(&mut self, t: Ty) -> Option<String> {
        if let Some(m) = self.mirrors.get(&t) {
            return m.clone();
        }
        let m = self.make_mirror(t);
        self.mirrors.insert(t, m.clone());
        m
    }

    fn make_mirror(&mut self, t: Ty) -> Option<String> {
        let TyKind::Adt(id, _) = t.kind() else { return None };
        let def = self.prog.adts.get(id)?;
        let (keyword, fields, packed, align) = match &def.kind {
            AdtKind::Struct { bits: Some(u), .. } => return Some(self.cty(*u)),
            AdtKind::Struct { fields, packed, align, bits: None } => ("struct", fields, *packed, *align),
            AdtKind::CUnion { fields } => ("union", fields, false, None),
            AdtKind::Union { .. } => return None,
        };
        if fields.is_empty() || self.size(t) == 0 {
            return None;
        }
        let names: Vec<String> = if self.debug_names {
            let gamma = super::debug::unique_members(fields.iter().map(|f| f.name.as_str()), &mut BTreeSet::new());
            gamma.into_iter().enumerate().map(|(i, n)| n.unwrap_or_else(|| format!("f{}", i))).collect()
        } else {
            (0..fields.len()).map(|i| format!("f{}", i)).collect()
        };
        let mut members = vec![];
        for (i, f) in fields.iter().enumerate() {
            let (ty, dims) = self.field_cty(f.ty)?;
            let al = f.align.map_or(String::new(), |a| format!("_Alignas({}) ", a));
            members.push(format!("{}{} {}{};", al, ty, names[i], dims));
        }
        if self.debug_names {
            self.member_names.extend(names.iter().filter(|n| super::debug::member_name(n).is_some()).cloned());
        }
        let name = format!("gm_cs{}", self.mirrors.len());
        let mut attrs = vec![];
        if packed {
            attrs.push("packed".to_string());
        }
        if let Some(a) = align {
            attrs.push(format!("aligned({})", a));
        }
        let attrs = if attrs.is_empty() { String::new() } else { format!(" __attribute__(({}))", attrs.join(", ")) };
        let _ =
            writeln!(self.mirror_defs, "typedef {} {}{} {{ {} }} {};", keyword, attrs, name, members.join(" "), name);
        // The C compiler must agree with the Gamma layout.
        let _ = write!(
            self.mirror_defs,
            "_Static_assert(sizeof({n}) == {} && _Alignof({n}) == {}",
            self.size(t),
            self.align(t),
            n = name
        );
        for (i, off) in self.offsets(t).iter().enumerate().take(fields.len()) {
            let _ = write!(self.mirror_defs, " && offsetof({}, {}) == {}", name, names[i], off);
        }
        let _ = writeln!(self.mirror_defs, ", \"layout of `{}`\");", t);
        Some(name)
    }

    /// Records `name` as a Gamma name given to a member (see `super::debug::guard`).
    pub fn add_member_name(&mut self, name: &str) {
        self.member_names.insert(name.to_string());
    }

    /// The Gamma names given to members.
    pub fn member_names(&self) -> BTreeSet<String> {
        self.member_names.clone()
    }

    /// The C type of a mirror's field of type `t`, and the array dimensions that follow
    /// the field's name.
    fn field_cty(&mut self, t: Ty) -> Option<(String, String)> {
        match self.repr(t) {
            Repr::Bool => return Some(("_Bool".into(), String::new())),
            Repr::Int { .. } | Repr::Float(_) | Repr::Ptr => return Some((self.cty(t), String::new())),
            Repr::Closure | Repr::Handler => return None,
            Repr::Blob => {}
        }
        match t.kind() {
            TyKind::Array { len, elem } if *len > 0 => {
                let (ty, dims) = self.field_cty(*elem)?;
                Some((ty, format!("[{}]{}", len, dims)))
            }
            TyKind::Adt(..) => Some((self.mirror(t)?, String::new())),
            _ => None,
        }
    }

    /// Whether values of `t` cross calls between Gamma functions by address: blobs larger
    /// than two words, which the C ABI would copy through memory anyway. The callee copies
    /// the value into its frame before anything else runs, so the address may be any
    /// place holding it.
    pub fn by_ref(&self, t: Ty) -> bool {
        matches!(self.repr(t), Repr::Blob) && self.size(t) > 16
    }

    /// The C type of a parameter of type `t` of a Gamma function (not of a plain C
    /// signature): an address if `by_ref`.
    pub fn param_cty(&mut self, t: Ty) -> String {
        if self.by_ref(t) { "const void *".into() } else { self.cty(t) }
    }

    /// The C type of `t` in the plain C calling convention: its mirror if it has one.
    pub fn plain_cty(&mut self, t: Ty) -> String {
        match self.mirror(t) {
            Some(m) => m,
            None => self.cty(t),
        }
    }

    /// The definitions of the mirrors used.
    pub fn mirror_defs(&self) -> &str {
        &self.mirror_defs
    }

    /// Typedefs of the blob types used.
    ///
    /// Under Fil-C (`Target::filc`), a blob that can hold a pointer is a union with
    /// pointer-typed lanes: a pointer keeps its capability only while it is stored as a
    /// pointer, and a struct of bytes passed or returned by value travels in integer
    /// registers, which would drop the capabilities of the pointers in it.
    pub fn blob_typedefs(&self) -> String {
        let mut out = String::new();
        let pb = self.lay.target.ptr_bytes;
        for (s, a) in &self.blobs {
            let name = blob_name(*s, *a);
            if self.lay.target.filc && *a >= pb && *s >= pb && s % pb == 0 {
                let _ = writeln!(
                    out,
                    "typedef union {{ _Alignas({}) uint8_t b[{}]; void *gm_lanes[{}]; }} {};",
                    a,
                    s,
                    s / pb,
                    name
                );
            } else {
                let _ = writeln!(out, "typedef struct {{ _Alignas({}) uint8_t b[{}]; }} {};", a, s, name);
            }
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
                    AdtKind::Struct { fields, bits: Some(_), .. } => {
                        let layout = self.bits_layout(t).unwrap();
                        let mut parts = vec![];
                        for (fd, (off, _, fty)) in fields.iter().zip(&layout.1) {
                            let d = self.desc(*fty);
                            parts.push(format!("{{{}, {}, {}}}", c_string(fd.name.as_str().as_bytes()), off, d));
                        }
                        f.push("kind = GM_K_BITS".into());
                        f.push(format!("bits = {}", self.bit_width(t)));
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
                        f.push(format!("bits = {}", self.bit_width(*tag)));
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
    /// width and type, in declaration order from the least significant bit.
    pub fn bits_layout(&self, t: Ty) -> Option<(Ty, Vec<BitField>)> {
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

    /// The descriptor definitions, in dependency-safe order.
    pub fn descriptors(&self) -> String {
        format!("{}{}{}", self.desc_fwd, self.desc_parts, self.desc_defs)
    }
}

/// A field of a `#bits` struct: bit offset, width and type.
pub type BitField = (u32, u32, Ty);

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
