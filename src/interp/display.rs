//! `@write` display rules (spec §21.6.1), by static type.

use super::machine::*;
use super::num;
use crate::tir::*;
use crate::types::*;

/// Appends `bytes` quoted with escapes.
pub fn quote_bytes(out: &mut Vec<u8>, bytes: &[u8]) {
    out.push(b'"');
    let mut i = 0;
    while i < bytes.len() {
        let b = bytes[i];
        match b {
            b'"' => out.extend_from_slice(b"\\\""),
            b'\\' => out.extend_from_slice(b"\\\\"),
            b'\n' => out.extend_from_slice(b"\\n"),
            b'\r' => out.extend_from_slice(b"\\r"),
            b'\t' => out.extend_from_slice(b"\\t"),
            0 => out.extend_from_slice(b"\\0"),
            0x20..=0x7e => out.push(b),
            0x80..=0xff => {
                // Keep valid UTF-8 sequences; escape stray bytes.
                let len = match b {
                    0xc2..=0xdf => 2,
                    0xe0..=0xef => 3,
                    0xf0..=0xf4 => 4,
                    _ => 0,
                };
                if len > 0 && i + len <= bytes.len() && std::str::from_utf8(&bytes[i..i + len]).is_ok() {
                    out.extend_from_slice(&bytes[i..i + len]);
                    i += len;
                    continue;
                }
                out.extend_from_slice(format!("\\x{:02x}", b).as_bytes());
            }
            _ => out.extend_from_slice(format!("\\x{:02x}", b).as_bytes()),
        }
        i += 1;
    }
    out.push(b'"');
}

impl<'a> Machine<'a> {
    /// Writes `b`, a value of type `ty`, to `out`. `top` is true when the value is the
    /// argument itself (strings are then written raw instead of quoted).
    pub fn format_value(&self, out: &mut Vec<u8>, ty: Ty, b: &[u8], top: bool) -> Res<()> {
        match ty.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize => {
                out.extend_from_slice(super::ops::fmt_int(&self.int_of(ty, b)).as_bytes());
            }
            TyKind::Float(f) => out.extend_from_slice(num::format_float(self.float_of(*f, b), *f).as_bytes()),
            TyKind::Bool => out.extend_from_slice(if b[0] != 0 { b"true" } else { b"false" }),
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => {
                out.extend_from_slice(format!("{:#x}", self.ptr_of(b)).as_bytes());
            }
            TyKind::Slice { elem, .. } => {
                let (_, esz, data) = self.seq_elems(ty, b)?;
                if matches!(elem.kind(), TyKind::Int(IntTy { signed: false, bits: 8 })) {
                    if top {
                        out.extend_from_slice(&data);
                    } else {
                        quote_bytes(out, &data);
                    }
                } else {
                    let n = self.slice_parts(b).1 as usize;
                    self.format_seq(out, *elem, esz, &data, n)?;
                }
            }
            TyKind::Array { len, elem } => {
                let esz = self.size_of(*elem);
                self.format_seq(out, *elem, esz, b, *len as usize)?;
            }
            TyKind::Tuple { elems, names } => {
                out.push(b'(');
                for i in 0..elems.len() {
                    if i > 0 {
                        out.extend_from_slice(b", ");
                    }
                    if let Some(names) = names {
                        out.extend_from_slice(names[i].as_str().as_bytes());
                        out.extend_from_slice(b": ");
                    }
                    let (fty, fb) = self.field_bytes(ty, b, i)?;
                    self.format_value(out, fty, &fb, false)?;
                }
                if elems.len() == 1 && names.is_none() {
                    out.push(b',');
                }
                out.push(b')');
            }
            TyKind::Optional(inner) => {
                if self.opt_present(ty, b) {
                    self.format_value(out, *inner, &self.opt_payload(ty, b), false)?;
                } else {
                    out.extend_from_slice(b"null");
                }
            }
            TyKind::Result { err, ok } => {
                let payload = self.res_payload(ty, b);
                if self.res_is_err(ty, b) {
                    out.push(b'!');
                    self.format_value(out, *err, &payload, false)?;
                } else {
                    self.format_value(out, *ok, &payload, false)?;
                }
            }
            TyKind::Adt(..) => {
                let def = self.adt(ty).ok_or_else(|| format!("unknown type {}", ty))?;
                match &def.kind {
                    AdtKind::Struct { fields, .. } => {
                        out.extend_from_slice(def.name.as_str().as_bytes());
                        out.push(b'(');
                        let mut first = true;
                        for (i, f) in fields.iter().enumerate() {
                            if f.name.as_str() == "_" {
                                continue;
                            }
                            if !first {
                                out.extend_from_slice(b", ");
                            }
                            first = false;
                            out.extend_from_slice(f.name.as_str().as_bytes());
                            out.extend_from_slice(b": ");
                            let (fty, fb) = self.field_bytes(ty, b, i)?;
                            self.format_value(out, fty, &fb, false)?;
                        }
                        out.push(b')');
                    }
                    AdtKind::Union { variants, .. } => {
                        let v = self.variant_index(ty, b)?;
                        out.push(b'.');
                        out.extend_from_slice(variants[v].name.as_str().as_bytes());
                        if let Some(pt) = variants[v].payload {
                            let off = self.payload_offset(ty);
                            let pb = &b[off..off + self.size_of(pt)];
                            out.push(b'(');
                            if let TyKind::Tuple { elems, names } = pt.kind() {
                                for i in 0..elems.len() {
                                    if i > 0 {
                                        out.extend_from_slice(b", ");
                                    }
                                    if let Some(names) = names {
                                        out.extend_from_slice(names[i].as_str().as_bytes());
                                        out.extend_from_slice(b": ");
                                    }
                                    let (fty, fb) = self.field_bytes(pt, pb, i)?;
                                    self.format_value(out, fty, &fb, false)?;
                                }
                            } else {
                                self.format_value(out, pt, pb, false)?;
                            }
                            out.push(b')');
                        }
                    }
                    AdtKind::CUnion { .. } => out.extend_from_slice(format!("<{}>", def.name).as_bytes()),
                }
            }
            _ => out.extend_from_slice(format!("<{}>", ty).as_bytes()),
        }
        Ok(())
    }

    fn format_seq(&self, out: &mut Vec<u8>, elem: Ty, esz: usize, data: &[u8], n: usize) -> Res<()> {
        out.push(b'[');
        for i in 0..n {
            if i > 0 {
                out.extend_from_slice(b", ");
            }
            self.format_value(out, elem, &data[i * esz..(i + 1) * esz], false)?;
        }
        out.push(b']');
        Ok(())
    }
}
