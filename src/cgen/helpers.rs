//! Per-type C functions for structural equality (§7.6), ordering (§7.5) and
//! hashing (§16.8), generated on demand. They take the addresses of the values
//! and agree exactly with the interpreter (the hash too, so that hash maps
//! iterate in the same order).

use super::types::{Repr, Types};
use crate::layout::is_nullable_pointer;
use crate::tir::AdtKind;
use crate::types::{Ty, TyKind};
use std::collections::HashMap;
use std::fmt::Write;

#[derive(Default)]
pub struct Helpers {
    pub protos: String,
    pub bodies: String,
    eq: HashMap<Ty, String>,
    less: HashMap<Ty, String>,
    hash: HashMap<Ty, String>,
    n: usize,
}

impl Helpers {
    fn name(&mut self, kind: &str) -> String {
        self.n += 1;
        format!("gm_{}{}", kind, self.n)
    }

    /// `bool f(const uint8_t *a, const uint8_t *b)`: `is` on values of type `t`.
    pub fn eq_fn(&mut self, ty: &mut Types, t: Ty) -> String {
        if let Some(n) = self.eq.get(&t) {
            return n.clone();
        }
        let name = self.name("eq");
        self.eq.insert(t, name.clone());
        let _ = writeln!(self.protos, "static bool {}(const uint8_t *a, const uint8_t *b);", name);
        let mut b = String::new();
        let p = ty.lay.target.ptr_bytes;
        match ty.repr(t) {
            _ if ty.wide(t).is_some() => {
                let w = ty.wide(t).unwrap();
                let _ = writeln!(b, "    return gm_wcmp(a, b, {}, {}) == 0;", w.bits, w.signed as u8);
            }
            Repr::Int { .. } | Repr::Float(_) | Repr::Ptr => {
                let c = ty.cty(t);
                let _ = writeln!(b, "    return GM_LD({c}, a) == GM_LD({c}, b);");
            }
            Repr::Bool => b.push_str("    return (a[0] != 0) == (b[0] != 0);\n"),
            _ => match t.kind() {
                TyKind::Tuple { elems, .. } => {
                    let offs = ty.offsets(t);
                    for (i, et) in elems.iter().enumerate() {
                        let f = self.eq_fn(ty, *et);
                        let _ = writeln!(b, "    if (!{f}(a + {o}, b + {o})) return false;", o = offs[i]);
                    }
                    b.push_str("    return true;\n");
                }
                TyKind::Array { len, elem, .. } => {
                    let f = self.eq_fn(ty, *elem);
                    let esz = ty.size(*elem);
                    let _ = writeln!(
                        b,
                        "    for (uint64_t i = 0; i < {len}; i++) if (!{f}(a + i * {esz}, b + i * {esz})) return false;"
                    );
                    b.push_str("    return true;\n");
                }
                TyKind::Slice { elem, .. } => {
                    let f = self.eq_fn(ty, *elem);
                    let esz = ty.size(*elem);
                    let _ = writeln!(b, "    uintptr_t n = GM_LD(uintptr_t, a + {p});");
                    let _ = writeln!(b, "    if (n != GM_LD(uintptr_t, b + {p})) return false;");
                    b.push_str("    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);\n");
                    let _ = writeln!(
                        b,
                        "    for (uintptr_t i = 0; i < n; i++) if (!{f}(pa + i * {esz}, pb + i * {esz})) return false;"
                    );
                    b.push_str("    return true;\n");
                }
                TyKind::Optional(inner) => {
                    let f = self.eq_fn(ty, *inner);
                    let flag = ty.offsets(t)[1];
                    let _ = writeln!(b, "    if (a[{flag}] != b[{flag}]) return false;");
                    let _ = writeln!(b, "    return !a[{flag}] || {f}(a, b);");
                }
                TyKind::Result { err, ok } => {
                    let (fe, fo) = (self.eq_fn(ty, *err), self.eq_fn(ty, *ok));
                    let offs = ty.offsets(t);
                    let (tg, pl) = (offs[0], offs[1]);
                    let _ = writeln!(b, "    if ((a[{tg}] != 0) != (b[{tg}] != 0)) return false;");
                    let _ = writeln!(b, "    return a[{tg}] ? {fe}(a + {pl}, b + {pl}) : {fo}(a + {pl}, b + {pl});");
                }
                TyKind::Adt(id, _) => match &ty.prog.adts[id].kind {
                    AdtKind::Struct { fields, bits: None, .. } => {
                        let offs = ty.offsets(t);
                        let fields = fields.clone();
                        for (i, fd) in fields.iter().enumerate() {
                            let f = self.eq_fn(ty, fd.ty);
                            let _ = writeln!(b, "    if (!{f}(a + {o}, b + {o})) return false;", o = offs[i]);
                        }
                        b.push_str("    return true;\n");
                    }
                    AdtKind::Struct { bits: Some(u), .. } => {
                        let f = self.eq_fn(ty, *u);
                        let _ = writeln!(b, "    return {f}(a, b);");
                    }
                    AdtKind::Union { tag, variants } => {
                        let (tag, variants) = (*tag, variants.clone());
                        let offs = ty.offsets(t);
                        let tc = ty.cty(tag);
                        let _ =
                            writeln!(b, "    {tc} ta = GM_LD({tc}, a + {o}), tb = GM_LD({tc}, b + {o});", o = offs[0]);
                        b.push_str("    if (ta != tb) return false;\n");
                        for v in &variants {
                            if let Some(pt) = v.payload {
                                let f = self.eq_fn(ty, pt);
                                let _ = writeln!(
                                    b,
                                    "    if (ta == ({tc}){}) return {f}(a + {o}, b + {o});",
                                    v.tag,
                                    o = offs[1]
                                );
                            }
                        }
                        b.push_str("    return true;\n");
                    }
                    AdtKind::CUnion { .. } => b.push_str("    return false;\n"),
                },
                _ => b.push_str("    return true;\n"),
            },
        }
        let _ = writeln!(self.bodies, "static bool {}(const uint8_t *a, const uint8_t *b) {{\n{}}}", name, b);
        name
    }

    /// `bool f(const uint8_t *a, const uint8_t *b)`: `a < b` for values of type `t`.
    pub fn less_fn(&mut self, ty: &mut Types, t: Ty) -> String {
        if let Some(n) = self.less.get(&t) {
            return n.clone();
        }
        let name = self.name("lt");
        self.less.insert(t, name.clone());
        let _ = writeln!(self.protos, "static bool {}(const uint8_t *a, const uint8_t *b);", name);
        let mut b = String::new();
        let p = ty.lay.target.ptr_bytes;
        match ty.repr(t) {
            _ if ty.wide(t).is_some() => {
                let w = ty.wide(t).unwrap();
                let _ = writeln!(b, "    return gm_wcmp(a, b, {}, {}) < 0;", w.bits, w.signed as u8);
            }
            Repr::Int { .. } | Repr::Float(_) | Repr::Ptr | Repr::Bool => {
                let c = ty.cty(t);
                let _ = writeln!(b, "    return GM_LD({c}, a) < GM_LD({c}, b);");
            }
            _ => match t.kind() {
                TyKind::Tuple { elems, .. } => {
                    let offs = ty.offsets(t);
                    for (i, et) in elems.iter().enumerate() {
                        let (fe, fl) = (self.eq_fn(ty, *et), self.less_fn(ty, *et));
                        let o = offs[i];
                        let _ = writeln!(b, "    if (!{fe}(a + {o}, b + {o})) return {fl}(a + {o}, b + {o});");
                    }
                    b.push_str("    return false;\n");
                }
                TyKind::Array { .. } | TyKind::Slice { .. } => {
                    let (elem, array_len) = match t.kind() {
                        TyKind::Array { len, elem, .. } => (*elem, Some(*len)),
                        TyKind::Slice { elem, .. } => (*elem, None),
                        _ => unreachable!(),
                    };
                    let (fe, fl) = (self.eq_fn(ty, elem), self.less_fn(ty, elem));
                    let esz = ty.size(elem);
                    match array_len {
                        Some(n) => {
                            let _ =
                                writeln!(b, "    uintptr_t na = {n}, nb = {n};\n    const uint8_t *pa = a, *pb = b;");
                        }
                        None => {
                            let _ = writeln!(
                                b,
                                "    uintptr_t na = GM_LD(uintptr_t, a + {p}), nb = GM_LD(uintptr_t, b + {p});"
                            );
                            b.push_str("    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);\n");
                        }
                    }
                    let _ = writeln!(
                        b,
                        "    for (uintptr_t i = 0; i < na && i < nb; i++) {{ if (!{fe}(pa + i * {esz}, pb + i * {esz})) return {fl}(pa + i * {esz}, pb + i * {esz}); }}"
                    );
                    b.push_str("    return na < nb;\n");
                }
                TyKind::Adt(id, _) => match &ty.prog.adts[id].kind {
                    AdtKind::Union { tag, .. } => {
                        let tag = *tag;
                        let tc = ty.cty(tag);
                        let o = ty.offsets(t)[0];
                        let _ = writeln!(b, "    return GM_LD({tc}, a + {o}) < GM_LD({tc}, b + {o});");
                    }
                    AdtKind::Struct { bits: Some(u), .. } => {
                        let f = self.less_fn(ty, *u);
                        let _ = writeln!(b, "    return {f}(a, b);");
                    }
                    _ => b.push_str("    return false;\n"),
                },
                _ => b.push_str("    return false;\n"),
            },
        }
        let _ = writeln!(self.bodies, "static bool {}(const uint8_t *a, const uint8_t *b) {{\n{}}}", name, b);
        name
    }

    /// `uint64_t f(const uint8_t *v, uint64_t h)`: mixes the value into the FNV-1a state `h`.
    pub fn hash_fn(&mut self, ty: &mut Types, t: Ty) -> String {
        if let Some(n) = self.hash.get(&t) {
            return n.clone();
        }
        let name = self.name("hash");
        self.hash.insert(t, name.clone());
        let _ = writeln!(self.protos, "static uint64_t {}(const uint8_t *v, uint64_t h);", name);
        let mut b = String::new();
        let p = ty.lay.target.ptr_bytes;
        match ty.repr(t) {
            _ if ty.wide(t).is_some() => {
                let w = ty.wide(t).unwrap();
                let _ = writeln!(b, "    return gm_whash(h, v, {}, {});", w.bits, w.signed as u8);
            }
            Repr::Int { signed, .. } => {
                let c = ty.cty(t);
                let f = if signed { "gm_hash_int" } else { "gm_hash_uint" };
                let _ = writeln!(b, "    return {f}(h, GM_LD({c}, v));");
            }
            Repr::Float(_) => {
                let c = ty.cty(t);
                let _ = writeln!(
                    b,
                    "    double x = (double)GM_LD({c}, v); if (x == 0) x = 0; uint64_t bits; memcpy(&bits, &x, 8);"
                );
                b.push_str("    return gm_mix64(h, bits);\n");
            }
            Repr::Bool => b.push_str("    uint8_t x = v[0] != 0; return gm_mix(h, &x, 1);\n"),
            Repr::Ptr => {
                b.push_str("    uint64_t x = (uint64_t)(uintptr_t)GM_LD(void *, v); return gm_mix64(h, x);\n");
            }
            _ => match t.kind() {
                TyKind::Tuple { elems, .. } => {
                    let offs = ty.offsets(t);
                    for (i, et) in elems.iter().enumerate() {
                        let f = self.hash_fn(ty, *et);
                        let _ = writeln!(b, "    h = {f}(v + {}, h);", offs[i]);
                    }
                    b.push_str("    return h;\n");
                }
                TyKind::Array { len, elem, .. } => {
                    let f = self.hash_fn(ty, *elem);
                    let esz = ty.size(*elem);
                    let _ = writeln!(
                        b,
                        "    for (uint64_t i = 0; i < {len}; i++) h = {f}(v + i * {esz}, h);\n    return h;"
                    );
                }
                TyKind::Slice { elem, .. } => {
                    let f = self.hash_fn(ty, *elem);
                    let esz = ty.size(*elem);
                    let _ =
                        writeln!(b, "    uint64_t n = GM_LD(uintptr_t, v + {p}); const uint8_t *d = GM_LD(void *, v);");
                    b.push_str("    h = gm_mix64(h, n);\n");
                    let _ =
                        writeln!(b, "    for (uint64_t i = 0; i < n; i++) h = {f}(d + i * {esz}, h);\n    return h;");
                }
                TyKind::Optional(inner) if !is_nullable_pointer(*inner) => {
                    let f = self.hash_fn(ty, *inner);
                    let flag = ty.offsets(t)[1];
                    let _ = writeln!(b, "    uint8_t s = v[{flag}] != 0; h = gm_mix(h, &s, 1);");
                    let _ = writeln!(b, "    return s ? {f}(v, h) : h;");
                }
                TyKind::Result { err, ok } => {
                    let (fe, fo) = (self.hash_fn(ty, *err), self.hash_fn(ty, *ok));
                    let offs = ty.offsets(t);
                    let _ = writeln!(b, "    uint8_t e = v[{}] != 0; h = gm_mix(h, &e, 1);", offs[0]);
                    let _ = writeln!(b, "    return e ? {fe}(v + {pl}, h) : {fo}(v + {pl}, h);", pl = offs[1]);
                }
                TyKind::Adt(id, _) => match &ty.prog.adts[id].kind {
                    AdtKind::Struct { fields, bits: None, .. } => {
                        let offs = ty.offsets(t);
                        let fields = fields.clone();
                        for (i, fd) in fields.iter().enumerate() {
                            let f = self.hash_fn(ty, fd.ty);
                            let _ = writeln!(b, "    h = {f}(v + {}, h);", offs[i]);
                        }
                        b.push_str("    return h;\n");
                    }
                    AdtKind::Struct { bits: Some(u), .. } => {
                        let f = self.hash_fn(ty, *u);
                        let _ = writeln!(b, "    return {f}(v, h);");
                    }
                    AdtKind::Union { tag, variants } => {
                        let (tag, variants) = (*tag, variants.clone());
                        let offs = ty.offsets(t);
                        let tc = ty.cty(tag);
                        let _ = writeln!(b, "    {tc} tg = GM_LD({tc}, v + {});", offs[0]);
                        for (i, var) in variants.iter().enumerate() {
                            let _ = writeln!(b, "    if (tg == ({tc}){}) {{", var.tag);
                            let _ = writeln!(b, "        uint64_t k = {i}; h = gm_mix64(h, k);");
                            if let Some(pt) = var.payload {
                                let f = self.hash_fn(ty, pt);
                                let _ = writeln!(b, "        h = {f}(v + {}, h);", offs[1]);
                            }
                            b.push_str("        return h;\n    }\n");
                        }
                        b.push_str("    return h;\n");
                    }
                    AdtKind::CUnion { .. } => b.push_str("    return h;\n"),
                },
                _ => b.push_str("    return h;\n"),
            },
        }
        let _ = writeln!(self.bodies, "static uint64_t {}(const uint8_t *v, uint64_t h) {{\n{}}}", name, b);
        name
    }
}

/// C support for the helpers and for bit builtins.
pub const SUPPORT: &str = r#"
#define GM_NOINLINE __attribute__((noinline))
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
    }
    return h;
}
static double gm_fmin(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? x : y;
    return x < y ? x : y;
}
static double gm_fmax(double x, double y) {
    if (x != x) return y;
    if (y != y) return x;
    if (x == 0 && y == 0) return __builtin_signbit(x) ? y : x;
    return x > y ? x : y;
}
static unsigned __int128 gm_clz(unsigned __int128 x, int bits) {
    int n = 0;
    for (int i = bits - 1; i >= 0 && !((x >> i) & 1); i--) n++;
    return n;
}
static unsigned __int128 gm_ctz(unsigned __int128 x, int bits) {
    int n = 0;
    while (n < bits && !((x >> n) & 1)) n++;
    return n;
}
static unsigned __int128 gm_popcount(unsigned __int128 x) {
    int n = 0;
    while (x) { n += (int)(x & 1); x >>= 1; }
    return n;
}
static unsigned __int128 gm_bswap(unsigned __int128 x, int bits) {
    unsigned __int128 r = 0;
    for (int i = 0; i < bits / 8; i++) { r = (r << 8) | (x & 0xff); x >>= 8; }
    return r;
}
static unsigned __int128 gm_bitreverse(unsigned __int128 x, int bits) {
    unsigned __int128 r = 0;
    for (int i = 0; i < bits; i++) { r = (r << 1) | (x & 1); x >>= 1; }
    return r;
}
/* Rotates the low `bits` bits of x left by k (taken modulo bits). */
static unsigned __int128 gm_rotl(unsigned __int128 x, uint64_t k, int bits) {
    unsigned __int128 mask = bits == 128 ? ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);
    x &= mask;
    k %= (uint64_t)bits;
    if (k == 0) return x;
    return ((x << k) | (x >> ((uint64_t)bits - k))) & mask;
}
"#;
