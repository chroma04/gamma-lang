//! The interpreter state and the typed helpers shared by all evaluation code.

use super::bytes::Bytes;
use super::memory::{AllocKind, MemError, Memory};
use super::num::{self, Int};
use super::value::*;
use super::{Mode, RunConfig};
use crate::layout::{self, Layouts, Target};
use crate::source::Span;
use crate::tir::*;
use crate::types::*;
use num_bigint::BigInt;
use std::cell::{RefCell, UnsafeCell};
use std::collections::{HashMap, HashSet};
use std::io::Write;
use std::rc::Rc;

/// Fake code addresses for function pointers: `CODE_BASE + 16 * fn id`.
pub const CODE_BASE: u64 = 0x1000;

/// Cached facts about a type.
pub struct TyInfo {
    pub kind: &'static TyKind,
    pub size: usize,
    pub align: u64,
    pub offsets: Vec<u64>,
    /// `(signed, bits)` for integer types.
    pub int: Option<(bool, u32)>,
    /// Integer types of at most 64 bits, which the evaluator handles without arbitrary-precision numbers.
    pub small: Option<SmallInt>,
}

/// An integer type of at most 64 bits.
#[derive(Clone, Copy, Debug)]
pub struct SmallInt {
    pub signed: bool,
    pub bits: u32,
    /// Size in bytes (at most 8).
    pub size: usize,
    pub align: u64,
}

/// The per-type facts, filled in on first use. Entries are never replaced or
/// removed, so a reference handed out stays valid as long as the table lives.
#[derive(Default)]
struct TyTable {
    slots: UnsafeCell<Vec<*mut TyInfo>>,
}

impl TyTable {
    #[inline(always)]
    fn get(&self, idx: usize) -> Option<&TyInfo> {
        // SAFETY: the table is only touched by the one thread running the machine; a slot
        // is written once, with a leaked box that lives until the table is dropped.
        unsafe {
            let slots = &*self.slots.get();
            match slots.get(idx) {
                Some(&p) if !p.is_null() => Some(&*p),
                _ => None,
            }
        }
    }

    fn insert(&self, idx: usize, info: TyInfo) -> &TyInfo {
        let p = Box::into_raw(Box::new(info));
        // SAFETY: as in `get`; growing the vector moves the pointers, not the infos they point to.
        unsafe {
            let slots = &mut *self.slots.get();
            if slots.len() <= idx {
                slots.resize(idx + 1, std::ptr::null_mut());
            }
            slots[idx] = p;
            &*p
        }
    }
}

impl Drop for TyTable {
    fn drop(&mut self) {
        for &p in self.slots.get_mut().iter() {
            if !p.is_null() {
                // SAFETY: allocated by `insert` with `Box::into_raw`, and freed only here.
                drop(unsafe { Box::from_raw(p) });
            }
        }
    }
}

pub struct ConstEntry {
    pub addr: u64,
}

/// A storage location.
#[derive(Clone, Copy, Debug)]
pub enum Place {
    Mem(u64),
    /// Memory inside a field of a `#packed` struct: accessed with alignment 1.
    Packed(u64),
    /// A field of type `fty` at bit `off` of the `#bits` struct of type `whole` stored
    /// at `addr` (a field of a nested `#bits` field adds the offsets).
    Bits {
        addr: u64,
        whole: Ty,
        off: u32,
        fty: Ty,
    },
}

/// The pair of frame and handler context an expression is evaluated in.
#[derive(Clone, Copy)]
pub struct Act<'f> {
    pub fr: &'f Rc<Frame>,
    pub cx: &'f Ctx,
}

pub type Res<T> = Result<T, String>;

pub struct Machine<'a> {
    pub prog: &'a Program,
    pub target: Target,
    pub mode: Mode,
    pub budget: Option<u64>,
    layouts: Layouts<'a>,
    tyinfo: TyTable,
    pub mem: Memory,
    pub steps: u64,
    pub depth: u32,
    pub next_frame: u64,
    pub stack_floor: usize,
    pub temps: Vec<u64>,
    pub str_cache: HashMap<(usize, usize), u64>,
    pub str_keep: Vec<Rc<[u8]>>,
    pub cstr_cache: HashMap<(usize, usize), u64>,
    pub consts: HashMap<ConstId, ConstEntry>,
    pub consts_busy: HashSet<ConstId>,
    /// Storage of `#global`s, by global and thread (0 except for `#thread_local` ones).
    pub globals: HashMap<(GlobalId, u64), u64>,
    /// The running thread (0: the main thread).
    pub thread_id: u64,
    pub out: Option<&'a mut dyn Write>,
    pub errw: Option<&'a mut dyn Write>,
    pub args: Vec<String>,
    /// True during compile-time evaluation (no `#global` access).
    pub const_mode: bool,
    pub code_end: u64,
    /// Fibers and active `with … on`s (§14).
    pub fib: super::fiber::Fibers,
    /// The bytes left on the running fiber's stack, as estimated by [`Machine::frame_cost`]
    /// (`u64::MAX` off fibers).
    pub fiber_stack: u64,
    /// Frame costs by function (`u64::MAX`: not yet computed).
    frame_costs: Vec<u64>,
    /// Finished frames nobody else refers to, kept to reuse their buffers.
    frame_pool: Vec<Rc<Frame>>,
    /// Emptied argument vectors, kept to reuse their buffers.
    pub arg_pool: Vec<Vec<Value>>,
    /// Debug builds: the allocator `(owner, vt)` and site of each block from `new`/`make`.
    pub alloc_tags: HashMap<u64, (u64, u64, String)>,
    /// Open files and directories of the file-system services.
    pub sys: super::sys::SysState,
    /// The program's threads (§19.6).
    pub threads: super::threads::Threads,
}

pub const MAX_DEPTH: u32 = 200_000;

impl<'a> Machine<'a> {
    pub fn new(
        prog: &'a Program,
        cfg: &RunConfig,
        adts: &'a dyn Fn(AdtId) -> Option<&'a AdtDef>,
        stack_floor: usize,
    ) -> Machine<'a> {
        let code_end = CODE_BASE + 16 * (prog.fns.len() as u64 + 1);
        let data_base = (code_end + 0x10000).div_ceil(0x10000) * 0x10000;
        Machine {
            prog,
            target: cfg.target.clone(),
            mode: cfg.mode,
            budget: cfg.step_budget,
            layouts: Layouts::new(cfg.target.clone(), adts),
            tyinfo: TyTable::default(),
            mem: Memory::new(data_base, cfg.target.ptr_bytes),
            steps: 0,
            depth: 0,
            next_frame: 1,
            stack_floor,
            temps: Vec::new(),
            str_cache: HashMap::new(),
            str_keep: Vec::new(),
            cstr_cache: HashMap::new(),
            consts: HashMap::new(),
            consts_busy: HashSet::new(),
            globals: HashMap::new(),
            thread_id: 0,
            out: None,
            errw: None,
            args: Vec::new(),
            const_mode: false,
            fib: Default::default(),
            fiber_stack: u64::MAX,
            frame_costs: vec![u64::MAX; prog.fns.len()],
            frame_pool: Vec::new(),
            arg_pool: Vec::new(),
            alloc_tags: HashMap::new(),
            sys: Default::default(),
            threads: Default::default(),
            code_end,
        }
    }

    pub fn little(&self) -> bool {
        self.target.little_endian
    }

    pub fn ptr_size(&self) -> usize {
        self.target.ptr_bytes as usize
    }

    // --- types -------------------------------------------------------------

    #[inline(always)]
    pub fn ti(&self, ty: Ty) -> &TyInfo {
        match self.tyinfo.get(ty.index()) {
            Some(i) => i,
            None => self.ti_slow(ty),
        }
    }

    #[inline(never)]
    fn ti_slow(&self, ty: Ty) -> &TyInfo {
        let l = self.layouts.layout(ty);
        let int = layout::int_bits(ty, &self.target);
        let size = l.size as usize;
        let small = match int {
            Some((signed, bits)) if bits <= 64 && size <= 8 => Some(SmallInt { signed, bits, size, align: l.align }),
            _ => None,
        };
        self.tyinfo.insert(ty.index(), TyInfo { kind: ty.kind(), size, align: l.align, offsets: l.offsets, int, small })
    }

    pub fn size_of(&self, ty: Ty) -> usize {
        self.ti(ty).size
    }

    pub fn adt(&self, ty: Ty) -> Option<&'a AdtDef> {
        let prog = self.prog;
        match ty.kind() {
            TyKind::Adt(id, _) => prog.adts.get(id),
            _ => None,
        }
    }

    /// True if `ty` is a `#packed` struct.
    pub fn is_packed(&self, ty: Ty) -> bool {
        matches!(self.adt(ty).map(|d| &d.kind), Some(AdtKind::Struct { packed: true, .. }))
    }

    pub fn bits_struct_ty(&self, ty: Ty) -> Option<Ty> {
        match &self.adt(ty)?.kind {
            AdtKind::Struct { bits: Some(u), .. } => Some(*u),
            _ => None,
        }
    }

    // --- traps ---------------------------------------------------------------

    pub fn trap<T>(&self, span: Span, msg: impl Into<String>) -> R<T> {
        Err(trap_flow(span, msg))
    }

    pub fn mem_err<T>(&self, span: Span, r: Result<T, MemError>) -> R<T> {
        r.map_err(|e| trap_flow(span, e.message()))
    }

    pub fn tr<T>(&self, span: Span, r: Res<T>) -> R<T> {
        r.map_err(|m| trap_flow(span, m))
    }

    // --- integers and pointers -------------------------------------------------

    pub fn decode_int_ty(&self, ti: &TyInfo, b: &[u8]) -> Int {
        let (signed, bits) = ti.int.expect("integer type");
        num::decode_int(&b[..ti.size.min(b.len())], signed, bits, self.little())
    }

    pub fn int_of(&self, ty: Ty, b: &[u8]) -> Int {
        let ti = self.ti(ty);
        self.decode_int_ty(ti, b)
    }

    pub fn int_bytes(&self, ty: Ty, v: &Int) -> Bytes {
        let ti = self.ti(ty);
        num::encode_int(v, ti.size, self.little())
    }

    #[inline]
    pub fn ptr_of(&self, b: &[u8]) -> u64 {
        // A pointer is an unsigned integer of 32 or 64 bits.
        let n = self.ptr_size();
        super::fast::word_of(&b[..n], n, self.little())
    }

    #[inline]
    pub fn ptr_bytes(&self, addr: u64) -> Bytes {
        let n = self.ptr_size();
        let w = if self.little() { addr } else { (addr << (64 - 8 * n)).swap_bytes() };
        Bytes::from_word(w, n)
    }

    pub fn usize_bytes(&self, n: u64) -> Bytes {
        self.ptr_bytes(n)
    }

    pub fn slice_parts(&self, b: &[u8]) -> (u64, u64) {
        let p = self.ptr_size();
        (self.ptr_of(&b[..p]), self.ptr_of(&b[p..2 * p]))
    }

    pub fn make_slice(&self, ptr: u64, len: u64) -> Bytes {
        let p = self.ptr_size();
        let mut out = Bytes::zeroed(2 * p);
        out[..p].copy_from_slice(&self.ptr_bytes(ptr));
        out[p..].copy_from_slice(&self.ptr_bytes(len));
        out
    }

    pub fn bool_bytes(&self, b: bool) -> Bytes {
        Bytes::from_slice(&[b as u8])
    }

    // --- floats --------------------------------------------------------------------

    pub fn float_of(&self, fty: FloatTy, b: &[u8]) -> f64 {
        num::decode_float(b, fty, self.little())
    }

    pub fn float_bytes(&self, fty: FloatTy, x: f64) -> Bytes {
        num::encode_float(x, fty, self.little())
    }

    // --- optionals and results ---------------------------------------------------------

    pub fn opt_inner(&self, ty: Ty) -> Ty {
        match ty.kind() {
            TyKind::Optional(t) => *t,
            _ => panic!("not an optional: {}", ty),
        }
    }

    pub fn opt_present(&self, ty: Ty, b: &[u8]) -> bool {
        let inner = self.opt_inner(ty);
        if layout::is_nullable_pointer(inner) {
            self.ptr_of(b) != 0
        } else {
            let ti = self.ti(ty);
            b[ti.offsets[1] as usize] != 0
        }
    }

    pub fn opt_payload(&self, ty: Ty, b: &[u8]) -> Bytes {
        let inner = self.opt_inner(ty);
        let isz = self.size_of(inner);
        if layout::is_nullable_pointer(inner) {
            Bytes::from_slice(&b[..isz])
        } else {
            let off = self.ti(ty).offsets[0] as usize;
            Bytes::from_slice(&b[off..off + isz])
        }
    }

    pub fn make_some(&self, ty: Ty, payload: &[u8]) -> Bytes {
        let inner = self.opt_inner(ty);
        let ti = self.ti(ty);
        let mut out = Bytes::zeroed(ti.size);
        if layout::is_nullable_pointer(inner) {
            let n = payload.len().min(ti.size);
            out[..n].copy_from_slice(&payload[..n]);
        } else {
            let off = ti.offsets[0] as usize;
            let n = payload.len().min(ti.size - off);
            out[off..off + n].copy_from_slice(&payload[..n]);
            out[ti.offsets[1] as usize] = 1;
        }
        out
    }

    pub fn make_none(&self, ty: Ty) -> Bytes {
        Bytes::zeroed(self.size_of(ty))
    }

    pub fn res_types(&self, ty: Ty) -> (Ty, Ty) {
        match ty.kind() {
            TyKind::Result { err, ok } => (*err, *ok),
            _ => panic!("not a result: {}", ty),
        }
    }

    pub fn res_is_err(&self, ty: Ty, b: &[u8]) -> bool {
        let ti = self.ti(ty);
        b[ti.offsets[0] as usize] != 0
    }

    pub fn res_payload(&self, ty: Ty, b: &[u8]) -> Bytes {
        let (err, ok) = self.res_types(ty);
        let is_err = self.res_is_err(ty, b);
        let sz = self.size_of(if is_err { err } else { ok });
        let off = self.ti(ty).offsets[1] as usize;
        Bytes::from_slice(&b[off..off + sz])
    }

    pub fn make_res(&self, ty: Ty, is_err: bool, payload: &[u8]) -> Bytes {
        let ti = self.ti(ty);
        let mut out = Bytes::zeroed(ti.size);
        out[ti.offsets[0] as usize] = is_err as u8;
        let off = ti.offsets[1] as usize;
        let n = payload.len().min(ti.size - off);
        out[off..off + n].copy_from_slice(&payload[..n]);
        out
    }

    // --- unions -------------------------------------------------------------------------

    pub fn union_def(&self, ty: Ty) -> Option<(Ty, &'a [VariantDef])> {
        match &self.adt(ty)?.kind {
            AdtKind::Union { tag, variants } => Some((*tag, variants)),
            _ => None,
        }
    }

    pub fn tag_int(&self, ty: Ty, b: &[u8]) -> Res<Int> {
        let (tag, _) = self.union_def(ty).ok_or_else(|| format!("not a union: {}", ty))?;
        let ti = self.ti(ty);
        let off = ti.offsets.first().copied().unwrap_or(0) as usize;
        let tsz = self.size_of(tag);
        Ok(self.int_of(tag, &b[off..off + tsz]))
    }

    pub fn variant_index(&self, ty: Ty, b: &[u8]) -> Res<usize> {
        let (_, variants) = self.union_def(ty).ok_or_else(|| format!("not a union: {}", ty))?;
        let t = self.tag_int(ty, b)?;
        variants
            .iter()
            .position(|v| tag_eq(&t, &v.tag))
            .ok_or_else(|| format!("invalid tag value {:?} for union {}", t, ty))
    }

    pub fn variant_payload_ty(&self, ty: Ty, variant: usize) -> Option<Ty> {
        self.union_def(ty).and_then(|(_, vs)| vs.get(variant)).and_then(|v| v.payload)
    }

    pub fn payload_offset(&self, ty: Ty) -> usize {
        self.ti(ty).offsets.get(1).copied().unwrap_or(0) as usize
    }

    pub fn make_variant(&self, ty: Ty, variant: usize, payload: &[u8]) -> Bytes {
        let (tag, variants) = self.union_def(ty).expect("union");
        let ti = self.ti(ty);
        let mut out = Bytes::zeroed(ti.size);
        let tag_bytes = self.int_bytes(tag, &Int::from_big(variants[variant].tag.clone()));
        out[..tag_bytes.len()].copy_from_slice(&tag_bytes);
        if !payload.is_empty() {
            let off = self.payload_offset(ty);
            let n = payload.len().min(ti.size.saturating_sub(off));
            out[off..off + n].copy_from_slice(&payload[..n]);
        }
        out
    }

    // --- struct / tuple fields ------------------------------------------------------------

    /// Type and byte offset of field `idx` of a struct, tuple or `#c_union` (not `#bits`).
    pub fn field_layout(&self, ty: Ty, idx: usize) -> Res<(Ty, usize)> {
        match ty.kind() {
            TyKind::Tuple { elems, .. } => {
                let ti = self.ti(ty);
                Ok((elems[idx], ti.offsets[idx] as usize))
            }
            TyKind::Adt(..) => match &self.adt(ty).ok_or("unknown struct type")?.kind {
                AdtKind::Struct { fields, bits: None, .. } => {
                    let ti = self.ti(ty);
                    Ok((fields[idx].ty, ti.offsets[idx] as usize))
                }
                AdtKind::CUnion { fields } => Ok((fields[idx].ty, 0)),
                _ => Err(format!("type {} has no addressable fields", ty)),
            },
            _ => Err(format!("type {} has no fields", ty)),
        }
    }

    /// Width in bits of a `#bits` field of type `ty`.
    pub fn bit_width(&self, ty: Ty) -> u32 {
        match ty.kind() {
            TyKind::Bool => 1,
            TyKind::Int(i) => i.bits as u32,
            _ => match self.adt(ty).map(|d| &d.kind) {
                Some(AdtKind::Union { tag, .. }) => self.bit_width(*tag),
                Some(AdtKind::Struct { bits: Some(u), .. }) => self.bit_width(*u),
                _ => 8 * self.size_of(ty) as u32,
            },
        }
    }

    /// Bit offset and type of field `idx` of the `#bits` struct `ty`.
    pub fn bits_field(&self, ty: Ty, idx: usize) -> Res<(u32, Ty)> {
        match self.adt(ty).map(|d| &d.kind) {
            Some(AdtKind::Struct { fields, bits: Some(_), .. }) => {
                let off: u32 = fields[..idx].iter().map(|f| self.bit_width(f.ty)).sum();
                Ok((off, fields[idx].ty))
            }
            _ => Err(format!("{} is not a #bits struct", ty)),
        }
    }

    /// The raw (zero-extended) bits of a value of a `#bits` field type.
    pub fn bits_raw(&self, fty: Ty, b: &[u8]) -> Int {
        let w = self.bit_width(fty);
        match fty.kind() {
            TyKind::Bool => Int::S((b[0] & 1) as i128),
            _ => {
                let (signed, bits, size) = match self.ti(fty).int {
                    Some((s, bits)) => (s, bits, self.size_of(fty)),
                    None => {
                        // Enums and nested #bits structs: raw tag / underlying integer.
                        let under = match self.adt(fty).map(|d| &d.kind) {
                            Some(AdtKind::Union { tag, .. }) => *tag,
                            Some(AdtKind::Struct { bits: Some(u), .. }) => *u,
                            _ => Ty::u8(),
                        };
                        let ui = self.ti(under).int.unwrap_or((false, 8));
                        (ui.0, ui.1, self.size_of(under))
                    }
                };
                let v = num::decode_int(&b[..size.min(b.len())], signed, bits, self.little());
                num::wrap(&v, false, w)
            }
        }
    }

    /// Converts raw bits to the bytes of a `#bits` field of type `fty`.
    pub fn bits_from_raw(&self, fty: Ty, raw: &Int) -> Bytes {
        let w = self.bit_width(fty);
        match fty.kind() {
            TyKind::Bool => Bytes::from_slice(&[(!raw.is_zero()) as u8]),
            _ => {
                let (signed, size) = match self.ti(fty).int {
                    Some((s, _)) => (s, self.size_of(fty)),
                    None => {
                        let under = match self.adt(fty).map(|d| &d.kind) {
                            Some(AdtKind::Union { tag, .. }) => *tag,
                            Some(AdtKind::Struct { bits: Some(u), .. }) => *u,
                            _ => Ty::u8(),
                        };
                        (self.ti(under).int.map(|i| i.0).unwrap_or(false), self.size_of(under))
                    }
                };
                let v = if signed { num::wrap(raw, true, w) } else { raw.clone() };
                num::encode_int(&v, size, self.little())
            }
        }
    }

    /// Reads field `idx` out of the bytes of a struct, tuple, `#bits` struct or `#c_union`.
    pub fn field_bytes(&self, ty: Ty, b: &[u8], idx: usize) -> Res<(Ty, Bytes)> {
        if let Some(u) = self.bits_struct_ty(ty) {
            let (off, fty) = self.bits_field(ty, idx)?;
            let whole = self.int_of_unsigned(u, b);
            let w = self.bit_width(fty);
            let raw = num::wrap(&whole.shr(off as u64), false, w);
            return Ok((fty, self.bits_from_raw(fty, &raw)));
        }
        let (fty, off) = self.field_layout(ty, idx)?;
        let sz = self.size_of(fty);
        if off + sz > b.len() {
            return Err(format!("field {} out of range of {}", idx, ty));
        }
        Ok((fty, Bytes::from_slice(&b[off..off + sz])))
    }

    /// The bits of an integer of type `u` as a non-negative number.
    pub fn int_of_unsigned(&self, u: Ty, b: &[u8]) -> Int {
        let ti = self.ti(u);
        let (_, bits) = ti.int.unwrap_or((false, 8));
        num::decode_int(&b[..ti.size.min(b.len())], false, bits, self.little())
    }

    // --- memory ------------------------------------------------------------------------------

    pub fn load(&self, span: Span, addr: u64, size: usize, align: u64) -> R<Bytes> {
        if size == 0 {
            return Ok(Bytes::empty());
        }
        if size <= 8 {
            return match self.mem.read_word(addr, size, align) {
                Ok(w) => Ok(Bytes::from_word(w, size)),
                Err(e) => Err(trap_flow(span, e.message())),
            };
        }
        let r = self.mem.read(addr, size as u64, align);
        match r {
            Ok(s) => Ok(Bytes::from_slice(s)),
            Err(e) => Err(trap_flow(span, e.message())),
        }
    }

    pub fn load_ty(&self, span: Span, addr: u64, ty: Ty) -> R<Bytes> {
        let ti = self.ti(ty);
        self.load(span, addr, ti.size, ti.align)
    }

    pub fn store(&mut self, span: Span, addr: u64, data: &[u8], align: u64) -> R<()> {
        if data.is_empty() {
            return Ok(());
        }
        if data.len() <= 8 {
            return match self.mem.write_word(addr, super::fast::word_of(data, data.len(), true), data.len(), align) {
                Ok(()) => Ok(()),
                Err(e) => Err(trap_flow(span, e.message())),
            };
        }
        let r = self.mem.write(addr, data, align);
        self.mem_err(span, r)
    }

    pub fn alloc_mem(&mut self, span: Span, size: usize, align: u64, kind: AllocKind, mutable: bool) -> R<u64> {
        let r = self.mem.alloc(size as u64, align, kind, mutable);
        self.mem_err(span, r)
    }

    /// Allocates a static immutable allocation holding `bytes`.
    pub fn static_bytes(&mut self, span: Span, bytes: &Rc<[u8]>) -> R<u64> {
        let idx = self.mem.statics.len() as u32;
        let addr = self.alloc_mem(span, bytes.len(), 1, AllocKind::Static(idx), false)?;
        self.mem.statics.push(bytes.clone());
        let r = self.mem.init(addr, bytes);
        self.mem_err(span, r)?;
        Ok(addr)
    }

    /// The interned static allocation for a string literal.
    pub fn string_literal(&mut self, span: Span, bytes: &Rc<[u8]>, nul: bool) -> R<u64> {
        let key = (Rc::as_ptr(bytes) as *const u8 as usize, bytes.len());
        let cache = if nul { &self.cstr_cache } else { &self.str_cache };
        if let Some(&a) = cache.get(&key) {
            return Ok(a);
        }
        let data: Rc<[u8]> = if nul {
            let mut v = bytes.to_vec();
            v.push(0);
            v.into()
        } else {
            bytes.clone()
        };
        let addr = self.static_bytes(span, &data)?;
        self.str_keep.push(bytes.clone());
        if nul {
            self.cstr_cache.insert(key, addr);
        } else {
            self.str_cache.insert(key, addr);
        }
        Ok(addr)
    }

    /// Allocates a temporary that dies at the end of the enclosing statement.
    pub fn alloc_temp(&mut self, span: Span, data: &[u8], align: u64) -> R<u64> {
        let addr = self.alloc_mem(span, data.len(), align, AllocKind::Temp, true)?;
        self.temps.push(addr);
        self.store(span, addr, data, 1)?;
        Ok(addr)
    }

    pub fn kill_temps(&mut self, mark: usize) {
        while self.temps.len() > mark {
            let a = self.temps.pop().unwrap();
            self.mem.kill(a);
        }
    }

    /// Reads the bytes a slice value refers to.
    pub fn slice_bytes(&self, span: Span, slice: &[u8], elem_size: usize) -> R<Bytes> {
        let (p, n) = self.slice_parts(slice);
        let total = (n as usize).saturating_mul(elem_size);
        self.load(span, p, total, 1)
    }

    /// Reads a `[]const u8` slice value as a Rust byte vector.
    pub fn read_str(&self, span: Span, slice: &[u8]) -> R<Vec<u8>> {
        Ok(self.slice_bytes(span, slice, 1)?.into_vec())
    }

    // --- frames ------------------------------------------------------------------------------

    pub fn new_frame(
        &mut self,
        ret_ty: Ty,
        func: Option<FnId>,
        parent_env: Option<Rc<Frame>>,
        nlocals: usize,
        installing: Option<(u64, LabelId)>,
        is_clause_lit: bool,
    ) -> Rc<Frame> {
        let id = self.next_frame;
        self.next_frame += 1;
        if let Some(mut rc) = self.frame_pool.pop() {
            let f = Rc::get_mut(&mut rc).expect("pooled frames are unshared");
            f.id = id;
            f.ret_ty = ret_ty;
            f.func = func;
            f.parent_env = parent_env;
            f.installing = installing;
            f.is_clause_lit = is_clause_lit;
            f.locals.get_mut().resize(nlocals, Slot::Empty);
            return rc;
        }
        Rc::new(Frame {
            id,
            ret_ty,
            func,
            parent_env,
            locals: RefCell::new(vec![Slot::Empty; nlocals]),
            allocs: RefCell::new(Vec::new()),
            installing,
            is_clause_lit,
        })
    }

    pub fn kill_frame(&mut self, fr: &Frame) {
        for a in fr.allocs.borrow().iter() {
            self.mem.kill(*a);
        }
    }

    /// Takes back a finished frame: if nothing else refers to it, its buffers are kept for the next call.
    pub fn recycle_frame(&mut self, mut fr: Rc<Frame>) {
        if self.frame_pool.len() < 256
            && let Some(f) = Rc::get_mut(&mut fr)
        {
            f.locals.get_mut().clear();
            f.allocs.get_mut().clear();
            f.parent_env = None;
            self.frame_pool.push(fr);
        }
    }

    /// Returns the frame `levels` capture links up.
    pub fn up_frame(&self, fr: &Rc<Frame>, levels: u32) -> Option<Rc<Frame>> {
        let mut cur = fr.clone();
        for _ in 0..levels {
            let next = cur.parent_env.clone()?;
            cur = next;
        }
        Some(cur)
    }

    /// The address of a first-class local's storage, allocating it on first use.
    pub fn local_addr(&mut self, span: Span, fr: &Rc<Frame>, id: LocalId, size: usize, align: u64) -> R<u64> {
        let i = id.index();
        {
            let locals = fr.locals.borrow();
            if let Some(Slot::Mem(a)) = locals.get(i) {
                return Ok(*a);
            }
        }
        let addr = self.alloc_mem(span, size, align, AllocKind::Stack, true)?;
        fr.allocs.borrow_mut().push(addr);
        let mut locals = fr.locals.borrow_mut();
        if locals.len() <= i {
            locals.resize(i + 1, Slot::Empty);
        }
        locals[i] = Slot::Mem(addr);
        Ok(addr)
    }

    /// Binds a value to a local: first-class data is stored in the local's own allocation.
    pub fn bind_local(&mut self, span: Span, fr: &Rc<Frame>, id: LocalId, ty: Option<Ty>, v: Value) -> R<()> {
        match v {
            Value::Bytes(b) => {
                let (size, align) = match ty {
                    Some(t) => {
                        let ti = self.ti(t);
                        (ti.size.max(b.len()), ti.align)
                    }
                    None => (b.len(), 16),
                };
                let addr = self.local_addr(span, fr, id, size, align)?;
                self.store(span, addr, &b, 1)
            }
            other => {
                let i = id.index();
                let mut locals = fr.locals.borrow_mut();
                if locals.len() <= i {
                    locals.resize(i + 1, Slot::Empty);
                }
                locals[i] = Slot::Val(Box::new(other));
                Ok(())
            }
        }
    }

    // --- function pointers -----------------------------------------------------------------------

    pub fn fn_addr(&self, f: FnId) -> u64 {
        CODE_BASE + 16 * f.0 as u64
    }

    pub fn fn_from_addr(&self, addr: u64) -> Option<FnId> {
        if addr < CODE_BASE || addr >= self.code_end || !(addr - CODE_BASE).is_multiple_of(16) {
            return None;
        }
        let id = ((addr - CODE_BASE) / 16) as usize;
        if id < self.prog.fns.len() { Some(FnId(id as u32)) } else { None }
    }

    /// The stack bytes a call of `f` is charged on a fiber stack ([`crate::tir::frame_cost`]).
    pub fn frame_cost(&mut self, f: FnId) -> u64 {
        let known = self.frame_costs[f.index()];
        if known != u64::MAX {
            return known;
        }
        let prog = self.prog;
        let c = crate::tir::frame_cost(&prog.fns[f.index()], &mut |t| self.size_of(t) as u64);
        self.frame_costs[f.index()] = c;
        c
    }

    pub fn fast(&self) -> bool {
        self.mode == Mode::Fast && !self.const_mode
    }
}

pub fn tag_eq(t: &Int, tag: &BigInt) -> bool {
    match t {
        Int::S(a) => tag.to_i128_checked() == Some(*a),
        Int::B(b) => b == tag,
    }
}

trait ToI128Checked {
    fn to_i128_checked(&self) -> Option<i128>;
}

impl ToI128Checked for BigInt {
    fn to_i128_checked(&self) -> Option<i128> {
        num_traits::ToPrimitive::to_i128(self)
    }
}
