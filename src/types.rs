//! Semantic types (spec §5).
//!
//! Types are hash-consed: a [`Ty`] is a small copyable handle, and two
//! structurally identical types (spec §5.18) have the same handle, so type
//! identity is `==`. Nominal types (`struct`, `union`, `effect` literals) are
//! identified by an id allocated when the literal is evaluated, so two
//! identically written literals are still distinct types.
//!
//! The interner is global and thread-safe; interned kinds are never freed.

use crate::symbol::Symbol;
use std::collections::HashMap;
use std::fmt;
use std::sync::{OnceLock, RwLock};

#[derive(Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Ty(u32);

/// A fixed-width integer type `uN` / `iN` (1 ≤ N ≤ 65535).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct IntTy {
    pub signed: bool,
    pub bits: u16,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum FloatTy {
    F16,
    F32,
    F64,
}

impl FloatTy {
    pub fn bits(self) -> u32 {
        match self {
            FloatTy::F16 => 16,
            FloatTy::F32 => 32,
            FloatTy::F64 => 64,
        }
    }

    /// Bits in the significand including the implicit bit.
    pub fn significand_bits(self) -> u32 {
        match self {
            FloatTy::F16 => 11,
            FloatTy::F32 => 24,
            FloatTy::F64 => 53,
        }
    }
}

/// Identifies a nominal `struct` or `union` type definition (one per literal).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct AdtId(pub u32);

/// Identifies a nominal `effect` definition (one per literal).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct EffectId(pub u32);

/// Identifies a generic type parameter while a generic body is checked (§16.1).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ParamId(pub u32);

/// A type variable created for an untyped literal during literal inference (§6.1).
/// Only meaningful inside the function body being checked.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct InferVar(pub u32);

/// A compile-time argument of a generic type or effect instantiation.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum GenericArg {
    Type(Ty),
    /// A compile-time value, identified by a canonical encoding of the value
    /// (its bytes for plain data, or a function id). Compared with `is` (§5.18).
    Value(ConstKey),
}

/// A canonical, hashable encoding of a compile-time value used as a generic argument.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum ConstKey {
    /// The value's type and its bytes in target layout.
    Bytes(Ty, Box<[u8]>),
    /// A named function, by declaration identity (with its name, for display).
    Fn(u32, Symbol),
    /// The null value of an optional function pointer.
    Null,
    /// A string (`[]const u8`), by its contents.
    Str(Box<[u8]>),
}

/// An instantiated effect, such as `Yield(i32)`.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct EffectRef {
    pub effect: EffectId,
    pub args: Box<[GenericArg]>,
}

/// A set of effects, kept sorted and deduplicated so equal sets compare equal.
#[derive(Clone, Debug, PartialEq, Eq, Hash, Default)]
pub struct EffectSet {
    pub effects: Box<[EffectRef]>,
}

impl EffectSet {
    pub fn new(mut effects: Vec<EffectRef>) -> EffectSet {
        effects.sort();
        effects.dedup();
        EffectSet { effects: effects.into_boxed_slice() }
    }

    pub fn empty() -> EffectSet {
        EffectSet::default()
    }

    pub fn is_empty(&self) -> bool {
        self.effects.is_empty()
    }

    pub fn contains(&self, e: &EffectRef) -> bool {
        self.effects.iter().any(|x| x == e)
    }

    pub fn is_subset_of(&self, other: &EffectSet) -> bool {
        self.effects.iter().all(|e| other.contains(e))
    }
}

/// A calling convention (§19.2). `#callconv(.SysV)` is the same as `.C` on every
/// target that has it, so only the conventions that differ from the target's C ABI
/// are distinguished.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum CallConv {
    /// The target's C ABI.
    #[default]
    C,
    /// The Microsoft x64 convention (`.Win64`, x86-64 only).
    Win64,
}

/// The signature part of a function or function pointer type (§5.14).
/// Parameter names are not part of the type.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct FnTy {
    pub params: Box<[Ty]>,
    pub ret: Ty,
    pub effects: EffectSet,
    /// Only foreign function types may be variadic.
    pub variadic: bool,
    /// Other than `C` only for plain function pointers (§19.2).
    pub callconv: CallConv,
}

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum TyKind {
    Int(IntTy),
    Usize,
    Isize,
    Float(FloatTy),
    Bool,
    /// The type with no values (§5.17).
    Never,
    /// The type of types (compile time only).
    Type,
    /// `rawptr`
    Rawptr,
    /// `^T` / `^const T`
    Ptr {
        is_const: bool,
        pointee: Ty,
    },
    /// `[^]T` / `[^]const T`
    ManyPtr {
        is_const: bool,
        elem: Ty,
    },
    /// `[]T` / `[]const T`
    Slice {
        is_const: bool,
        elem: Ty,
    },
    /// `[N]T`
    Array {
        len: u64,
        elem: Ty,
    },
    /// `(T1, T2)` or `(a: T1, b: T2)`; `()` is the unit type.
    Tuple {
        elems: Box<[Ty]>,
        names: Option<Box<[Symbol]>>,
    },
    /// `?T`
    Optional(Ty),
    /// `E!T`
    Result {
        err: Ty,
        ok: Ty,
    },
    /// `fn(…) -> R \ {E}` — second-class function value (§11.6).
    Fn(Box<FnTy>),
    /// `^fn(…) -> R \ {E}` — function pointer.
    FnPtr(Box<FnTy>),
    /// A struct or union type, with its generic arguments (empty if not generic).
    Adt(AdtId, Box<[GenericArg]>),
    /// `cont(T)`
    Cont(Ty),
    /// A generic parameter, opaque inside the generic body: a type parameter, or a
    /// compile-time value parameter where a generic argument is expected (§16.5).
    Param(ParamId),
    /// `[N]T` inside a generic body, where `N` is a compile-time value parameter
    /// (`len` is its `Param`). Instantiation turns it into an `Array`.
    ParamArray {
        len: Ty,
        elem: Ty,
    },
    /// A compile-time value used as a generic argument (§16.4, §16.5), such as
    /// the `16` of `Ring(u8, 16)`. Never the type of an expression.
    Value(ConstKey),
    /// An unresolved literal type variable.
    Infer(InferVar),
    /// The anonymous second-class type of a handler literal (§5.15), identified
    /// by the literal.
    Handler(u32),
    /// The type of an erroneous expression; suppresses follow-on errors.
    Error,
}

struct Interner {
    kinds: Vec<&'static TyKind>,
    map: HashMap<&'static TyKind, Ty>,
    adt_names: Vec<Symbol>,
    effect_names: Vec<Symbol>,
    param_names: Vec<Symbol>,
    /// For an instantiation of a generic struct or union: its template.
    adt_templates: HashMap<AdtId, AdtId>,
}

fn interner() -> &'static RwLock<Interner> {
    static I: OnceLock<RwLock<Interner>> = OnceLock::new();
    I.get_or_init(|| {
        RwLock::new(Interner {
            kinds: vec![],
            map: HashMap::new(),
            adt_names: vec![],
            effect_names: vec![],
            param_names: vec![],
            adt_templates: HashMap::new(),
        })
    })
}

impl Ty {
    pub fn intern(kind: TyKind) -> Ty {
        if let Some(&t) = interner().read().unwrap().map.get(&kind) {
            return t;
        }
        let mut i = interner().write().unwrap();
        if let Some(&t) = i.map.get(&kind) {
            return t;
        }
        let leaked: &'static TyKind = Box::leak(Box::new(kind));
        let t = Ty(i.kinds.len() as u32);
        i.kinds.push(leaked);
        i.map.insert(leaked, t);
        t
    }

    /// A dense index, unique per distinct type, usable to key side tables.
    pub fn index(self) -> usize {
        self.0 as usize
    }

    pub fn kind(self) -> &'static TyKind {
        interner().read().unwrap().kinds[self.0 as usize]
    }

    // --- constructors for common types ---

    pub fn int(signed: bool, bits: u16) -> Ty {
        Ty::intern(TyKind::Int(IntTy { signed, bits }))
    }
    pub fn u8() -> Ty {
        Ty::int(false, 8)
    }
    pub fn u32() -> Ty {
        Ty::int(false, 32)
    }
    pub fn u64() -> Ty {
        Ty::int(false, 64)
    }
    pub fn i32() -> Ty {
        Ty::int(true, 32)
    }
    pub fn usize() -> Ty {
        Ty::intern(TyKind::Usize)
    }
    pub fn isize() -> Ty {
        Ty::intern(TyKind::Isize)
    }
    pub fn f64() -> Ty {
        Ty::intern(TyKind::Float(FloatTy::F64))
    }
    pub fn bool() -> Ty {
        Ty::intern(TyKind::Bool)
    }
    pub fn unit() -> Ty {
        Ty::intern(TyKind::Tuple { elems: Box::new([]), names: None })
    }
    pub fn never() -> Ty {
        Ty::intern(TyKind::Never)
    }
    pub fn type_() -> Ty {
        Ty::intern(TyKind::Type)
    }
    pub fn error() -> Ty {
        Ty::intern(TyKind::Error)
    }
    pub fn slice(is_const: bool, elem: Ty) -> Ty {
        Ty::intern(TyKind::Slice { is_const, elem })
    }
    /// `string`, i.e. `[]const u8`.
    pub fn string() -> Ty {
        Ty::slice(true, Ty::u8())
    }
    pub fn ptr(is_const: bool, pointee: Ty) -> Ty {
        Ty::intern(TyKind::Ptr { is_const, pointee })
    }
    pub fn many_ptr(is_const: bool, elem: Ty) -> Ty {
        Ty::intern(TyKind::ManyPtr { is_const, elem })
    }
    pub fn array(len: u64, elem: Ty) -> Ty {
        Ty::intern(TyKind::Array { len, elem })
    }
    pub fn optional(inner: Ty) -> Ty {
        Ty::intern(TyKind::Optional(inner))
    }
    pub fn result(err: Ty, ok: Ty) -> Ty {
        Ty::intern(TyKind::Result { err, ok })
    }
    pub fn tuple(elems: Vec<Ty>) -> Ty {
        Ty::intern(TyKind::Tuple { elems: elems.into_boxed_slice(), names: None })
    }
    pub fn infer(v: InferVar) -> Ty {
        Ty::intern(TyKind::Infer(v))
    }

    // --- predicates ---

    pub fn is_error(self) -> bool {
        matches!(self.kind(), TyKind::Error)
    }
    pub fn is_never(self) -> bool {
        matches!(self.kind(), TyKind::Never)
    }
    pub fn is_unit(self) -> bool {
        matches!(self.kind(), TyKind::Tuple { elems, .. } if elems.is_empty())
    }
    /// `T` if this is `?T`.
    pub fn optional_inner(self) -> Option<Ty> {
        match self.kind() {
            TyKind::Optional(t) => Some(*t),
            _ => None,
        }
    }

    pub fn is_bool(self) -> bool {
        matches!(self.kind(), TyKind::Bool)
    }
    /// Any integer type, including `usize`/`isize` (not literal variables).
    pub fn is_integer(self) -> bool {
        matches!(self.kind(), TyKind::Int(_) | TyKind::Usize | TyKind::Isize)
    }
    pub fn is_signed_integer(self) -> bool {
        matches!(self.kind(), TyKind::Int(IntTy { signed: true, .. }) | TyKind::Isize)
    }
    pub fn is_unsigned_integer(self) -> bool {
        matches!(self.kind(), TyKind::Int(IntTy { signed: false, .. }) | TyKind::Usize)
    }
    /// An array type, `[N]T` (including one sized by a compile-time parameter).
    pub fn is_array(self) -> bool {
        matches!(self.kind(), TyKind::Array { .. } | TyKind::ParamArray { .. })
    }
    pub fn is_float(self) -> bool {
        matches!(self.kind(), TyKind::Float(_))
    }
    pub fn is_infer(self) -> bool {
        matches!(self.kind(), TyKind::Infer(_))
    }

    /// True if any part of the type is an inference variable.
    pub fn has_infer(self) -> bool {
        let mut found = false;
        self.walk(&mut |t| found |= t.is_infer());
        found
    }

    /// Calls `f` on this type and every type it is built from (not through ADT definitions).
    pub fn walk(self, f: &mut impl FnMut(Ty)) {
        f(self);
        match self.kind() {
            TyKind::Ptr { pointee: t, .. }
            | TyKind::ManyPtr { elem: t, .. }
            | TyKind::Slice { elem: t, .. }
            | TyKind::Array { elem: t, .. }
            | TyKind::Optional(t)
            | TyKind::Cont(t) => t.walk(f),
            TyKind::ParamArray { len, elem } => {
                len.walk(f);
                elem.walk(f);
            }
            TyKind::Result { err, ok } => {
                err.walk(f);
                ok.walk(f);
            }
            TyKind::Tuple { elems, .. } => elems.iter().for_each(|t| t.walk(f)),
            TyKind::Fn(sig) | TyKind::FnPtr(sig) => {
                sig.params.iter().for_each(|t| t.walk(f));
                sig.ret.walk(f);
                for e in sig.effects.effects.iter() {
                    for a in e.args.iter() {
                        if let GenericArg::Type(t) = a {
                            t.walk(f);
                        }
                    }
                }
            }
            TyKind::Adt(_, args) => {
                for a in args.iter() {
                    if let GenericArg::Type(t) = a {
                        t.walk(f);
                    }
                }
            }
            _ => {}
        }
    }

    /// Rebuilds the type, replacing each component type `t` for which `f(t)` is `Some`.
    pub fn map(self, f: &mut impl FnMut(Ty) -> Option<Ty>) -> Ty {
        if let Some(t) = f(self) {
            return t;
        }
        let k = match self.kind() {
            TyKind::Ptr { is_const, pointee } => TyKind::Ptr { is_const: *is_const, pointee: pointee.map(f) },
            TyKind::ManyPtr { is_const, elem } => TyKind::ManyPtr { is_const: *is_const, elem: elem.map(f) },
            TyKind::Slice { is_const, elem } => TyKind::Slice { is_const: *is_const, elem: elem.map(f) },
            TyKind::Array { len, elem } => TyKind::Array { len: *len, elem: elem.map(f) },
            TyKind::ParamArray { len, elem } => {
                let (len, elem) = (len.map(f), elem.map(f));
                match len.kind() {
                    TyKind::Value(k) => match k.as_u64() {
                        Some(n) => TyKind::Array { len: n, elem },
                        None => TyKind::Error,
                    },
                    _ => TyKind::ParamArray { len, elem },
                }
            }
            TyKind::Optional(t) => TyKind::Optional(t.map(f)),
            TyKind::Cont(t) => TyKind::Cont(t.map(f)),
            TyKind::Result { err, ok } => TyKind::Result { err: err.map(f), ok: ok.map(f) },
            TyKind::Tuple { elems, names } => {
                TyKind::Tuple { elems: elems.iter().map(|t| t.map(f)).collect(), names: names.clone() }
            }
            TyKind::Fn(sig) => TyKind::Fn(Box::new(map_sig(sig, f))),
            TyKind::FnPtr(sig) => TyKind::FnPtr(Box::new(map_sig(sig, f))),
            TyKind::Adt(id, args) => TyKind::Adt(
                *id,
                args.iter()
                    .map(|a| match a {
                        GenericArg::Type(t) => GenericArg::Type(t.map(f)),
                        v => v.clone(),
                    })
                    .collect(),
            ),
            _ => return self,
        };
        Ty::intern(k)
    }
}

fn map_sig(sig: &FnTy, f: &mut impl FnMut(Ty) -> Option<Ty>) -> FnTy {
    FnTy {
        params: sig.params.iter().map(|t| t.map(f)).collect(),
        ret: sig.ret.map(f),
        effects: sig.effects.clone(),
        variadic: sig.variadic,
        callconv: sig.callconv,
    }
}

impl AdtId {
    /// Allocates a new nominal type id with the given display name.
    pub fn fresh(name: Symbol) -> AdtId {
        let mut i = interner().write().unwrap();
        i.adt_names.push(name);
        AdtId(i.adt_names.len() as u32 - 1)
    }

    pub fn name(self) -> Symbol {
        interner().read().unwrap().adt_names[self.0 as usize]
    }

    /// Records that `self` is an instantiation of the generic type `template`.
    pub fn set_template(self, template: AdtId) {
        interner().write().unwrap().adt_templates.insert(self, template);
    }

    /// The generic type this is an instantiation of, or `self`.
    pub fn template(self) -> AdtId {
        interner().read().unwrap().adt_templates.get(&self).copied().unwrap_or(self)
    }
}

impl EffectId {
    pub fn fresh(name: Symbol) -> EffectId {
        let mut i = interner().write().unwrap();
        i.effect_names.push(name);
        EffectId(i.effect_names.len() as u32 - 1)
    }

    pub fn name(self) -> Symbol {
        interner().read().unwrap().effect_names[self.0 as usize]
    }
}

impl ParamId {
    /// Allocates a new type parameter with the given display name.
    pub fn fresh(name: Symbol) -> ParamId {
        let mut i = interner().write().unwrap();
        i.param_names.push(name);
        ParamId(i.param_names.len() as u32 - 1)
    }

    pub fn name(self) -> Symbol {
        interner().read().unwrap().param_names[self.0 as usize]
    }
}

impl fmt::Display for Ty {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self.kind() {
            TyKind::Int(i) => write!(f, "{}{}", if i.signed { 'i' } else { 'u' }, i.bits),
            TyKind::Usize => f.write_str("usize"),
            TyKind::Isize => f.write_str("isize"),
            TyKind::Float(x) => write!(f, "f{}", x.bits()),
            TyKind::Bool => f.write_str("bool"),
            TyKind::Never => f.write_str("never"),
            TyKind::Type => f.write_str("type"),
            TyKind::Rawptr => f.write_str("rawptr"),
            TyKind::Ptr { is_const, pointee } => write!(f, "^{}{}", if *is_const { "const " } else { "" }, pointee),
            TyKind::ManyPtr { is_const, elem } => write!(f, "[^]{}{}", if *is_const { "const " } else { "" }, elem),
            TyKind::Slice { is_const, elem } => {
                if *is_const && *elem == Ty::u8() {
                    f.write_str("string")
                } else {
                    write!(f, "[]{}{}", if *is_const { "const " } else { "" }, elem)
                }
            }
            TyKind::Array { len, elem } => write!(f, "[{}]{}", len, elem),
            TyKind::Tuple { elems, names } => {
                f.write_str("(")?;
                for (i, t) in elems.iter().enumerate() {
                    if i > 0 {
                        f.write_str(", ")?;
                    }
                    if let Some(names) = names {
                        write!(f, "{}: ", names[i])?;
                    }
                    write!(f, "{}", t)?;
                }
                if elems.len() == 1 && names.is_none() {
                    f.write_str(",")?;
                }
                f.write_str(")")
            }
            TyKind::Optional(t) => write!(f, "?{}", t),
            TyKind::Result { err, ok } => write!(f, "{}!{}", err, ok),
            TyKind::Fn(sig) => fmt_sig(f, "fn", sig),
            TyKind::FnPtr(sig) => fmt_sig(f, "^fn", sig),
            TyKind::Adt(id, args) => {
                write!(f, "{}", id.name())?;
                if !args.is_empty() {
                    f.write_str("(")?;
                    for (i, a) in args.iter().enumerate() {
                        if i > 0 {
                            f.write_str(", ")?;
                        }
                        match a {
                            GenericArg::Type(t) => write!(f, "{}", t)?,
                            GenericArg::Value(k) => write!(f, "{}", k)?,
                        }
                    }
                    f.write_str(")")?;
                }
                Ok(())
            }
            TyKind::Cont(t) => write!(f, "cont({})", t),
            TyKind::Param(p) => write!(f, "{}", p.name()),
            TyKind::ParamArray { len, elem } => write!(f, "[{}]{}", len, elem),
            TyKind::Value(k) => write!(f, "{}", k),
            TyKind::Handler(_) => f.write_str("handler"),
            TyKind::Infer(_) => f.write_str("{integer}"),
            TyKind::Error => f.write_str("{error}"),
        }
    }
}

fn fmt_sig(f: &mut fmt::Formatter<'_>, head: &str, sig: &FnTy) -> fmt::Result {
    if sig.callconv == CallConv::Win64 {
        f.write_str("#callconv(.Win64) ")?;
    }
    write!(f, "{}(", head)?;
    for (i, p) in sig.params.iter().enumerate() {
        if i > 0 {
            f.write_str(", ")?;
        }
        write!(f, "{}", p)?;
    }
    if sig.variadic {
        f.write_str(if sig.params.is_empty() { "..." } else { ", ..." })?;
    }
    f.write_str(")")?;
    if !sig.ret.is_unit() {
        write!(f, " -> {}", sig.ret)?;
    }
    if !sig.effects.is_empty() {
        f.write_str(" \\ {")?;
        for (i, e) in sig.effects.effects.iter().enumerate() {
            if i > 0 {
                f.write_str(", ")?;
            }
            write!(f, "{}", e)?;
        }
        f.write_str("}")?;
    }
    Ok(())
}

impl fmt::Display for EffectRef {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.effect.name().as_str())?;
        if !self.args.is_empty() {
            f.write_str("(")?;
            for (i, a) in self.args.iter().enumerate() {
                if i > 0 {
                    f.write_str(", ")?;
                }
                match a {
                    GenericArg::Type(t) => write!(f, "{}", t)?,
                    GenericArg::Value(v) => write!(f, "{}", v)?,
                }
            }
            f.write_str(")")?;
        }
        Ok(())
    }
}

impl fmt::Debug for Ty {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Ty({})", self)
    }
}

impl ConstKey {
    /// The key of an integer value of type `ty` (which must be an integer type).
    pub fn int(ty: Ty, v: &num_bigint::BigInt, size: u64) -> ConstKey {
        let mut bytes = v.to_signed_bytes_le();
        let fill = if v.sign() == num_bigint::Sign::Minus { 0xff } else { 0 };
        bytes.resize(size as usize, fill);
        ConstKey::Bytes(ty, bytes.into())
    }

    /// The value of an integer key, if it is an integer that fits in `u64`.
    pub fn as_int(&self) -> Option<num_bigint::BigInt> {
        match self {
            ConstKey::Bytes(t, b) if t.is_signed_integer() => Some(num_bigint::BigInt::from_signed_bytes_le(b)),
            ConstKey::Bytes(t, b) if t.is_integer() => {
                Some(num_bigint::BigInt::from_bytes_le(num_bigint::Sign::Plus, b))
            }
            _ => None,
        }
    }

    pub fn as_u64(&self) -> Option<u64> {
        use num_traits::ToPrimitive;
        self.as_int().and_then(|i| i.to_u64())
    }
}

impl fmt::Display for ConstKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ConstKey::Fn(_, name) => write!(f, "{}", name),
            ConstKey::Null => f.write_str("null"),
            ConstKey::Str(s) => write!(f, "{:?}", String::from_utf8_lossy(s)),
            ConstKey::Bytes(t, b) => {
                if let Some(i) = self.as_int() {
                    write!(f, "{}", i)
                } else if t.is_bool() {
                    write!(f, "{}", b.first() == Some(&1))
                } else {
                    write!(f, "{}(…)", t)
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn structural_identity() {
        assert_eq!(Ty::slice(true, Ty::u8()), Ty::string());
        assert_ne!(Ty::slice(false, Ty::u8()), Ty::string());
        assert_eq!(Ty::optional(Ty::ptr(false, Ty::i32())), Ty::optional(Ty::ptr(false, Ty::i32())));
        assert_ne!(Ty::usize(), Ty::int(false, 64));
    }

    #[test]
    fn display() {
        assert_eq!(Ty::string().to_string(), "string");
        assert_eq!(Ty::optional(Ty::ptr(true, Ty::u8())).to_string(), "?^const u8");
        assert_eq!(Ty::result(Ty::i32(), Ty::unit()).to_string(), "i32!()");
        assert_eq!(Ty::tuple(vec![Ty::u8()]).to_string(), "(u8,)");
        assert_eq!(Ty::array(4, Ty::f64()).to_string(), "[4]f64");
    }

    #[test]
    fn nominal_types_are_distinct() {
        let a = AdtId::fresh(Symbol::intern("S"));
        let b = AdtId::fresh(Symbol::intern("S"));
        assert_ne!(Ty::intern(TyKind::Adt(a, Box::new([]))), Ty::intern(TyKind::Adt(b, Box::new([]))));
    }
}
