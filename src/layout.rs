//! Memory layout of types for a target (spec §5, §19.7).
//!
//! Layouts the specification leaves unspecified are fixed here:
//!
//! - `?T` for a non-pointer `T` is laid out as `T` followed by a one-byte
//!   presence flag (1 = present), rounded up to `T`'s alignment.
//! - `E!T` is laid out like a tagged union `{ tag: u8, payload }` where tag 0
//!   is success (payload `T`) and tag 1 is error (payload `E`).
//! - `cont(T)` is two pointers (context address and generation counter).
//! - A second-class function value is three pointers (code, environment,
//!   handler context); a handler value is two pointers.

use crate::tir::{AdtDef, AdtKind};
use crate::types::{AdtId, FloatTy, IntTy, Ty, TyKind};
use std::cell::RefCell;
use std::collections::HashMap;

/// Properties of the compilation target that affect layout and arithmetic.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Target {
    /// Pointer width in bytes (4 or 8).
    pub ptr_bytes: u64,
    pub little_endian: bool,
    /// Alignment the C ABI gives `__int128` (16 on x86-64 and AArch64).
    pub int128_align: u64,
    /// The operating system, as a variant name of `target.Os` (§3.6).
    pub os: &'static str,
    /// The architecture, as a variant name of `target.Arch`.
    pub arch: &'static str,
    /// The C compiler is Fil-C, whose pointers carry capabilities that are lost when a
    /// pointer travels as an integer: the C back end keeps pointers as C pointers, and
    /// Safe builds (§18.7) require it.
    pub filc: bool,
    /// The C compiler has the `_Float16` type, which the C back end uses for `f16`
    /// (GCC lacks it on s390x and some other targets).
    pub f16: bool,
}

/// Converts the bytes of an integer between the target's byte order and little-endian
/// (the conversion is its own inverse). An integer of up to 128 bits is stored as one
/// number in the target's byte order; a wider one as 64-bit words, least significant
/// first, each in the target's byte order (§5.1).
pub fn int_bytes_swap_order(bytes: &[u8], little: bool) -> Vec<u8> {
    if little {
        bytes.to_vec()
    } else if bytes.len() <= 16 {
        bytes.iter().rev().copied().collect()
    } else {
        bytes.chunks(8).flat_map(|w| w.iter().rev().copied().collect::<Vec<u8>>()).collect()
    }
}

impl Default for Target {
    fn default() -> Self {
        Target::host()
    }
}

impl Target {
    /// The target the compiler itself runs on.
    pub fn host() -> Target {
        Target {
            ptr_bytes: std::mem::size_of::<usize>() as u64,
            little_endian: cfg!(target_endian = "little"),
            int128_align: 16,
            os: if cfg!(target_os = "linux") {
                "Linux"
            } else if cfg!(target_os = "macos") {
                "MacOS"
            } else if cfg!(target_os = "windows") {
                "Windows"
            } else if cfg!(target_os = "freebsd") {
                "FreeBSD"
            } else {
                "Freestanding"
            },
            arch: if cfg!(target_arch = "x86_64") {
                "X86_64"
            } else if cfg!(target_arch = "aarch64") {
                "Aarch64"
            } else if cfg!(target_arch = "riscv64") {
                "Riscv64"
            } else if cfg!(target_arch = "s390x") {
                "S390x"
            } else if cfg!(target_arch = "wasm32") {
                "Wasm32"
            } else {
                "Other"
            },
            filc: false,
            f16: true,
        }
    }

    pub fn ptr_bits(&self) -> u32 {
        (self.ptr_bytes * 8) as u32
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Layout {
    pub size: u64,
    pub align: u64,
    /// Byte offsets of fields: struct fields / tuple elements in order; for a
    /// tagged union or result, `[tag offset, payload offset]`; for an optional
    /// with a flag, `[payload offset, flag offset]`. Empty otherwise.
    pub offsets: Vec<u64>,
}

impl Layout {
    fn scalar(size: u64) -> Layout {
        Layout { size, align: size.max(1), offsets: vec![] }
    }
}

/// Size in bytes of the storage of an integer of `bits` bits (§5.2).
pub fn int_storage_bytes(bits: u32) -> u64 {
    match bits {
        0..=8 => 1,
        9..=16 => 2,
        17..=32 => 4,
        33..=64 => 8,
        65..=128 => 16,
        _ => (bits as u64).div_ceil(64) * 8,
    }
}

/// Bit width of an integer type (resolving `usize`/`isize` for the target).
pub fn int_bits(ty: Ty, target: &Target) -> Option<(bool, u32)> {
    match ty.kind() {
        TyKind::Int(IntTy { signed, bits }) => Some((*signed, *bits as u32)),
        TyKind::Usize => Some((false, target.ptr_bits())),
        TyKind::Isize => Some((true, target.ptr_bits())),
        _ => None,
    }
}

fn round_up(x: u64, align: u64) -> u64 {
    x.div_ceil(align) * align
}

/// Lays out fields in order with C rules (§5.10.4).
fn struct_layout(fields: &[(Layout, Option<u64>)], packed: bool, min_align: Option<u64>) -> Layout {
    let mut offset = 0;
    let mut align = 1;
    let mut offsets = Vec::with_capacity(fields.len());
    for (l, field_align) in fields {
        let a = if packed { 1 } else { field_align.map_or(l.align, |fa| fa.max(l.align)) };
        offset = round_up(offset, a);
        offsets.push(offset);
        offset += l.size;
        align = align.max(a);
    }
    if let Some(m) = min_align {
        align = align.max(m);
    }
    Layout { size: round_up(offset, align), align, offsets }
}

/// True for the pointer kinds whose optional uses address 0 as null (§5.12).
pub fn is_nullable_pointer(ty: Ty) -> bool {
    matches!(ty.kind(), TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_))
}

/// Computes and caches layouts. `adts` gives the (instantiated) definitions of
/// nominal types.
pub struct Layouts<'a> {
    pub target: Target,
    adts: &'a dyn Fn(AdtId) -> Option<&'a AdtDef>,
    cache: RefCell<HashMap<Ty, Layout>>,
}

impl<'a> Layouts<'a> {
    pub fn new(target: Target, adts: &'a dyn Fn(AdtId) -> Option<&'a AdtDef>) -> Layouts<'a> {
        Layouts { target, adts, cache: RefCell::new(HashMap::new()) }
    }

    pub fn layout(&self, ty: Ty) -> Layout {
        if let Some(l) = self.cache.borrow().get(&ty) {
            return l.clone();
        }
        let l = self.compute(ty);
        self.cache.borrow_mut().insert(ty, l.clone());
        l
    }

    pub fn size_of(&self, ty: Ty) -> u64 {
        self.layout(ty).size
    }

    pub fn align_of(&self, ty: Ty) -> u64 {
        self.layout(ty).align
    }

    fn compute(&self, ty: Ty) -> Layout {
        let p = self.target.ptr_bytes;
        match ty.kind() {
            TyKind::Int(IntTy { bits, .. }) => {
                let size = int_storage_bytes(*bits as u32);
                let align = match size {
                    16 => self.target.int128_align,
                    s if s > 16 => 8,
                    s => s,
                };
                Layout { size, align, offsets: vec![] }
            }
            TyKind::Usize | TyKind::Isize => Layout::scalar(p),
            TyKind::Float(f) => Layout::scalar(match f {
                FloatTy::F16 => 2,
                FloatTy::F32 => 4,
                FloatTy::F64 => 8,
            }),
            TyKind::Bool => Layout::scalar(1),
            TyKind::Never
            | TyKind::Type
            | TyKind::Error
            | TyKind::Param(_)
            | TyKind::ParamArray { .. }
            | TyKind::LenExpr { .. }
            | TyKind::Value(_)
            | TyKind::Effects(_)
            | TyKind::Infer(_) => Layout { size: 0, align: 1, offsets: vec![] },
            TyKind::Rawptr | TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::FnPtr(_) => Layout::scalar(p),
            TyKind::Slice { .. } => Layout { size: 2 * p, align: p, offsets: vec![0, p] },
            TyKind::Array { len, elem, .. } => {
                let e = self.layout(*elem);
                Layout { size: e.size * len, align: e.align, offsets: vec![] }
            }
            TyKind::Tuple { elems, .. } => {
                let fields: Vec<_> = elems.iter().map(|t| (self.layout(*t), None)).collect();
                struct_layout(&fields, false, None)
            }
            TyKind::Optional(inner) => {
                if is_nullable_pointer(*inner) {
                    return self.layout(*inner);
                }
                let l = self.layout(*inner);
                struct_layout(&[(l, None), (Layout::scalar(1), None)], false, None)
            }
            TyKind::Result { err, ok } => {
                let (e, o) = (self.layout(*err), self.layout(*ok));
                let payload = Layout { size: e.size.max(o.size), align: e.align.max(o.align), offsets: vec![] };
                struct_layout(&[(Layout::scalar(1), None), (payload, None)], false, None)
            }
            TyKind::Fn(_) => Layout { size: 3 * p, align: p, offsets: vec![] },
            TyKind::Handler(_) => Layout { size: 2 * p, align: p, offsets: vec![] },
            TyKind::Cont(_) => Layout { size: 2 * p, align: p, offsets: vec![] },
            TyKind::Adt(id, _) => match (self.adts)(*id) {
                Some(def) => self.adt_layout(def),
                None => Layout { size: 0, align: 1, offsets: vec![] },
            },
        }
    }

    fn adt_layout(&self, def: &AdtDef) -> Layout {
        match &def.kind {
            AdtKind::Struct { bits: Some(u), .. } => {
                let l = self.layout(*u);
                Layout { size: l.size, align: l.align, offsets: vec![] }
            }
            AdtKind::Struct { fields, packed, align, bits: None } => {
                let fs: Vec<_> = fields.iter().map(|f| (self.layout(f.ty), f.align)).collect();
                struct_layout(&fs, *packed, *align)
            }
            AdtKind::Union { tag, variants } => {
                let tag_l = self.layout(*tag);
                let payloads: Vec<Layout> = variants.iter().filter_map(|v| v.payload.map(|t| self.layout(t))).collect();
                if payloads.is_empty() {
                    return Layout { size: tag_l.size, align: tag_l.align, offsets: vec![0] };
                }
                let payload = Layout {
                    size: payloads.iter().map(|l| l.size).max().unwrap_or(0),
                    align: payloads.iter().map(|l| l.align).max().unwrap_or(1),
                    offsets: vec![],
                };
                struct_layout(&[(tag_l, None), (payload, None)], false, None)
            }
            AdtKind::CUnion { fields } => {
                let ls: Vec<Layout> = fields.iter().map(|f| self.layout(f.ty)).collect();
                let align = ls.iter().map(|l| l.align).max().unwrap_or(1);
                let size = round_up(ls.iter().map(|l| l.size).max().unwrap_or(0), align);
                Layout { size, align, offsets: vec![0; ls.len()] }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::symbol::Symbol;
    use crate::tir::FieldDef;

    fn no_adts(_: AdtId) -> Option<&'static AdtDef> {
        None
    }

    #[test]
    fn integer_storage() {
        let l = Layouts::new(Target::host(), &no_adts);
        assert_eq!(l.size_of(Ty::int(false, 1)), 1);
        assert_eq!(l.size_of(Ty::int(false, 9)), 2);
        assert_eq!(l.size_of(Ty::int(true, 24)), 4);
        assert_eq!(l.size_of(Ty::int(false, 65)), 16);
        assert_eq!(l.align_of(Ty::int(false, 65)), 16);
        assert_eq!(l.size_of(Ty::int(false, 129)), 24);
        assert_eq!(l.align_of(Ty::int(false, 129)), 8);
    }

    #[test]
    fn aggregates() {
        let l = Layouts::new(Target::host(), &no_adts);
        let t = Ty::tuple(vec![Ty::u8(), Ty::u32(), Ty::u8()]);
        assert_eq!(l.layout(t), Layout { size: 12, align: 4, offsets: vec![0, 4, 8] });
        assert_eq!(l.size_of(Ty::string()), 16);
        assert_eq!(l.size_of(Ty::optional(Ty::ptr(false, Ty::u8()))), 8);
        assert_eq!(l.layout(Ty::optional(Ty::u32())), Layout { size: 8, align: 4, offsets: vec![0, 4] });
        assert_eq!(l.size_of(Ty::unit()), 0);
        assert_eq!(l.size_of(Ty::array(3, Ty::u32())), 12);
    }

    #[test]
    fn structs() {
        let id = AdtId::fresh(Symbol::intern("S"));
        let def = AdtDef {
            name: Symbol::intern("S"),
            kind: AdtKind::Struct {
                fields: vec![
                    FieldDef { name: Symbol::intern("a"), ty: Ty::u8(), is_pub: false, align: None },
                    FieldDef { name: Symbol::intern("b"), ty: Ty::int(false, 64), is_pub: false, align: None },
                ],
                packed: false,
                align: None,
                bits: None,
            },
        };
        let lookup = |i: AdtId| if i == id { Some(&def) } else { None };
        let l = Layouts::new(Target::host(), &lookup);
        let ty = Ty::intern(TyKind::Adt(id, Box::new([])));
        assert_eq!(l.layout(ty), Layout { size: 16, align: 8, offsets: vec![0, 8] });
    }
}
