//! Debugger views of values (`gamma build -g`).
//!
//! Values that are blobs in the generated C (see `super::types`) would show as raw
//! bytes in a debugger. With debug information on, each frame field instead also has a
//! *view*: a C type laid out exactly like the Gamma value, whose members a debugger can
//! name and print:
//!
//! | Gamma type | View |
//! |---|---|
//! | struct, `#c_union` | its fields by name |
//! | tuple | `_0`, `_1`… or the element names |
//! | tagged union | `tag` (a C enum naming the variant, `Shape_Circle`) and a member per payload, named after its variant |
//! | enum (payload-less union) | the C enum alone |
//! | `?T` | `value`, `present` (a pointer optional is a pointer) |
//! | `E!T` | `is_err`, `ok`, `err` |
//! | `[]T` | `ptr` (a typed pointer), `len` |
//! | `^T`, `[^]T` | a pointer to the view of `T` |
//! | `bool` | `_Bool` |
//!
//! Views are only declared for the debugger: generated code never refers to them. Each
//! view is a packed struct whose members sit at the offsets of the Gamma layout, with
//! unnamed bit-fields (which debuggers do not print) as padding, so it has exactly the
//! size of the value. Its alignment is 1, so a frame field holding a view next to the
//! blob it aliases keeps the blob's layout.

use super::debug;
use super::types::{Repr, Types};
use crate::tir::AdtKind;
use crate::types::{Ty, TyKind};
use std::collections::{BTreeSet, HashMap};
use std::fmt::Write;

/// The C type of a view and the array dimensions that follow a member's name.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct View {
    pub cty: String,
    pub dims: String,
}

impl View {
    fn scalar(cty: impl Into<String>) -> Option<View> {
        Some(View { cty: cty.into(), dims: String::new() })
    }
}

#[derive(Default)]
pub struct Views {
    /// Views by type (`None`: the type has no bytes to show).
    cache: HashMap<Ty, Option<View>>,
    /// Views whose names are given out but whose definitions are still to be written:
    /// the targets of pointers, defined once the view that points at them is complete.
    pending: Vec<Ty>,
    building: bool,
    defs: String,
    count: usize,
    /// The C enumerators declared, which share one namespace.
    enumerators: BTreeSet<String>,
}

/// A member of a view at an offset of the Gamma layout.
struct Member {
    offset: u64,
    size: u64,
    decl: String,
}

impl Types<'_> {
    /// The debugger view of values of `t` (see the module documentation), or `None` if
    /// `t` has no bytes.
    pub fn debug_view(&mut self, t: Ty) -> Option<View> {
        if self.views.building {
            return self.view(t);
        }
        self.views.building = true;
        let v = self.view(t);
        while let Some(p) = self.views.pending.pop() {
            self.define_view(p);
        }
        self.views.building = false;
        v
    }

    /// The definitions of the views used.
    pub fn view_defs(&self) -> &str {
        &self.views.defs
    }

    fn view(&mut self, t: Ty) -> Option<View> {
        if let Some(v) = self.views.cache.get(&t).cloned() {
            // A pointer's target, now needed by value: it is defined first.
            if let Some(k) = self.views.pending.iter().position(|p| *p == t) {
                self.views.pending.remove(k);
                self.define_view(t);
            }
            return v;
        }
        if self.size(t) == 0 {
            self.views.cache.insert(t, None);
            return None;
        }
        let v = match self.repr(t) {
            Repr::Bool => View::scalar("_Bool"),
            Repr::Ptr => View::scalar(self.pointer_view(t)),
            Repr::Blob if self.is_aggregate(t) => {
                let v = self.reserve(t);
                self.define_view(t);
                v
            }
            Repr::Blob => match t.kind() {
                TyKind::Array { len, elem, .. } => match self.view(*elem) {
                    Some(e) => Some(View { cty: e.cty, dims: format!("[{}]{}", len, e.dims) }),
                    None => View::scalar(self.cty(t)),
                },
                TyKind::Adt(..) if self.enum_info(t).is_some() => {
                    let v = self.reserve(t);
                    self.define_view(t);
                    v
                }
                // Closures, handlers, continuations.
                _ => View::scalar(self.cty(t)),
            },
            _ => View::scalar(self.cty(t)),
        };
        self.views.cache.insert(t, v.clone());
        v
    }

    /// Whether the view of `t` is a struct (or union) of its own.
    fn is_aggregate(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Tuple { .. } | TyKind::Slice { .. } | TyKind::Optional(_) | TyKind::Result { .. } => true,
            TyKind::Adt(id, _) => match &self.prog.adts[id].kind {
                AdtKind::Struct { bits, .. } => bits.is_none(),
                AdtKind::CUnion { .. } => true,
                AdtKind::Union { variants, .. } => variants.iter().any(|v| v.payload.is_some_and(|p| self.size(p) > 0)),
            },
            _ => false,
        }
    }

    /// The suffix of the name of `t`'s view by which the gdb pretty printers
    /// (`debug::gdb_script_section`) recognize views they show differently.
    fn view_kind(&self, t: Ty) -> &'static str {
        match t.kind() {
            TyKind::Slice { elem, .. } if matches!(elem.kind(), TyKind::Int(i) if i.bits == 8) => "_string",
            TyKind::Slice { .. } => "_slice",
            TyKind::Optional(_) => "_optional",
            TyKind::Result { .. } => "_result",
            TyKind::Adt(..) if self.enum_info(t).is_some() => "_union",
            _ => "",
        }
    }

    /// Gives `t`'s view its name before its definition, so pointers can refer to it.
    fn reserve(&mut self, t: Ty) -> Option<View> {
        let n = self.views.count;
        self.views.count += 1;
        let cty = if self.is_aggregate(t) {
            format!("struct gm_dv{}{}", n, self.view_kind(t))
        } else {
            format!("enum gm_dv{}", n)
        };
        let v = View::scalar(cty);
        self.views.cache.insert(t, v.clone());
        v
    }

    /// The C type of a pointer of type `t` to a value: a pointer to the target's view.
    fn pointer_view(&mut self, t: Ty) -> String {
        let target = match t.kind() {
            TyKind::Ptr { pointee, .. } => *pointee,
            TyKind::ManyPtr { elem, .. } => *elem,
            TyKind::Optional(inner) => return self.pointer_view(*inner),
            _ => return "void *".into(),
        };
        format!("{} *", self.target_view(target))
    }

    /// The C type a pointer to a `t` points to.
    fn target_view(&mut self, t: Ty) -> String {
        if let Some(v) = self.views.cache.get(&t) {
            return match v {
                Some(v) if v.dims.is_empty() && !v.cty.starts_with("enum ") => v.cty.clone(),
                _ => "void".into(),
            };
        }
        if self.size(t) == 0 {
            return "void".into();
        }
        if self.is_aggregate(t) {
            // Defined later: the target may contain the view being built.
            let v = self.reserve(t).unwrap();
            self.views.pending.push(t);
            return v.cty;
        }
        match self.view(t) {
            Some(v) if v.dims.is_empty() && !v.cty.starts_with("enum ") => v.cty,
            // An array, or an enum (which C cannot point at before its definition).
            _ => "void".into(),
        }
    }

    /// Writes the definition of the view of `t`, whose name is reserved.
    fn define_view(&mut self, t: Ty) {
        let Some(Some(View { cty, .. })) = self.views.cache.get(&t).cloned() else { return };
        let size = self.size(t);
        let offs = self.offsets(t);
        let mut members = vec![];
        let mut taken = BTreeSet::new();
        match t.kind().clone() {
            TyKind::Tuple { elems, names } => {
                let names: Vec<String> = match &names {
                    Some(n) => debug::unique_view_members(n.iter().map(|s| s.as_str()), &mut taken)
                        .into_iter()
                        .enumerate()
                        .map(|(k, n)| n.unwrap_or_else(|| format!("_{}", k)))
                        .collect(),
                    None => (0..elems.len()).map(|k| format!("_{}", k)).collect(),
                };
                for (k, e) in elems.iter().enumerate() {
                    self.push_member(&mut members, offs[k], *e, &names[k]);
                }
            }
            TyKind::Slice { elem, .. } => {
                let target = self.target_view(elem);
                let len = self.cty(Ty::usize());
                members.push(Member { offset: offs[0], size: offs[1], decl: format!("{} *ptr;", target) });
                members.push(Member { offset: offs[1], size: size - offs[1], decl: format!("{} len;", len) });
            }
            TyKind::Optional(inner) => {
                self.push_member(&mut members, offs[0], inner, "value");
                members.push(Member { offset: offs[1], size: 1, decl: "_Bool present;".into() });
            }
            TyKind::Result { err, ok } => {
                members.push(Member { offset: offs[0], size: 1, decl: "_Bool is_err;".into() });
                let arms = [(ok, "ok".to_string()), (err, "err".to_string())];
                self.push_union(&mut members, offs[1], &arms);
            }
            TyKind::Adt(id, _) => match self.prog.adts[&id].kind.clone() {
                AdtKind::Struct { fields, .. } => {
                    let names = debug::unique_view_members(fields.iter().map(|f| f.name.as_str()), &mut taken);
                    for (k, f) in fields.iter().enumerate() {
                        let name = names[k].clone().unwrap_or_else(|| format!("f{}", k));
                        self.push_member(&mut members, offs[k], f.ty, &name);
                    }
                }
                AdtKind::CUnion { fields } => {
                    let names = debug::unique_view_members(fields.iter().map(|f| f.name.as_str()), &mut taken);
                    let arms: Vec<(Ty, String)> = fields
                        .iter()
                        .enumerate()
                        .map(|(k, f)| (f.ty, names[k].clone().unwrap_or_else(|| format!("f{}", k))))
                        .collect();
                    self.push_union(&mut members, 0, &arms);
                }
                AdtKind::Union { tag, variants } => {
                    let tag_cty = self.define_enum(t, &cty);
                    if !self.is_aggregate(t) {
                        return;
                    }
                    members.push(Member { offset: offs[0], size: self.size(tag), decl: format!("{} tag;", tag_cty) });
                    taken.insert("tag".to_string());
                    let names = debug::unique_view_members(variants.iter().map(|v| v.name.as_str()), &mut taken);
                    let arms: Vec<(Ty, String)> = variants
                        .iter()
                        .enumerate()
                        .filter_map(|(k, v)| Some((v.payload?, names[k].clone().unwrap_or_else(|| format!("v{}", k)))))
                        .collect();
                    self.push_union(&mut members, offs[1], &arms);
                }
            },
            _ => return,
        }
        let mut body = String::new();
        let mut at = 0;
        for m in &members {
            pad(&mut body, m.offset - at);
            let _ = write!(body, " {}", m.decl);
            at = m.offset + m.size;
        }
        pad(&mut body, size - at);
        let _ = writeln!(self.views.defs, "{} {{{} }};", packed(&cty), body);
        self.static_assert(&cty, size);
    }

    /// Adds a member `name` of type `t` at `offset` (none if `t` has no bytes).
    fn push_member(&mut self, members: &mut Vec<Member>, offset: u64, t: Ty, name: &str) {
        if let Some(v) = self.view(t) {
            self.add_member_name(name);
            members.push(Member { offset, size: self.size(t), decl: format!("{} {}{};", v.cty, name, v.dims) });
        }
    }

    /// Adds an anonymous union of `arms` at `offset` (none if no arm has bytes).
    fn push_union(&mut self, members: &mut Vec<Member>, offset: u64, arms: &[(Ty, String)]) {
        let mut decls = String::new();
        let mut size = 0;
        for (t, name) in arms {
            if let Some(v) = self.view(*t) {
                self.add_member_name(name);
                let _ = write!(decls, " {} {}{};", v.cty, name, v.dims);
                size = size.max(self.size(*t));
            }
        }
        if size > 0 {
            members.push(Member { offset, size, decl: format!("union __attribute__((packed)) {{{} }};", decls) });
        }
    }

    fn static_assert(&mut self, cty: &str, size: u64) {
        let _ = writeln!(self.views.defs, "_Static_assert(sizeof({}) == {}, \"debugger view\");", cty, size);
    }

    /// For a tagged union whose tag a C enum can name: the enumerator prefix's base (the
    /// union's name) and the tag's size and signedness.
    fn enum_info(&self, t: Ty) -> Option<(String, u64, bool)> {
        let TyKind::Adt(id, _) = t.kind() else { return None };
        let def = &self.prog.adts[id];
        let AdtKind::Union { tag, .. } = &def.kind else { return None };
        let size = self.size(*tag);
        // Enumerators are `int`s: an 8-byte tag stays an integer.
        if size > 4 {
            return None;
        }
        let base: String = def.name.as_str().chars().take_while(|c| c.is_ascii_alphanumeric() || *c == '_').collect();
        debug::member_name(&base)?;
        Some((base, size, tag.is_signed_integer()))
    }

    /// Declares the C enum of a union's tag (if it has one) and returns the C type of
    /// the view's `tag` member. An enum view is named `view`; the tag of a union with
    /// payloads gets an enum of its own.
    fn define_enum(&mut self, t: Ty, view: &str) -> String {
        let TyKind::Adt(id, _) = t.kind() else { unreachable!() };
        let AdtKind::Union { tag, variants } = &self.prog.adts[id].kind else { unreachable!() };
        let Some((base, size, signed)) = self.enum_info(t) else {
            return self.cty(*tag);
        };
        let name = if view.starts_with("enum ") {
            view.to_string()
        } else {
            let n = self.views.count;
            self.views.count += 1;
            format!("enum gm_dv{}", n)
        };
        // Generic instances share a name: the prefix gets a number.
        let mut prefix = base.clone();
        let mut k = 1;
        while variants.iter().any(|v| self.views.enumerators.contains(&format!("{}_{}", prefix, v.name))) {
            k += 1;
            prefix = format!("{}{}", base, k);
        }
        let mut items = vec![];
        let (mut lo, mut hi) = (0i64, 0i64);
        for v in variants {
            let e = format!("{}_{}", prefix, v.name);
            let value: i64 = v.tag.to_string().parse().unwrap_or(0);
            lo = lo.min(value);
            hi = hi.max(value);
            self.views.enumerators.insert(e.clone());
            self.add_member_name(&e);
            items.push(format!("{} = {}", e, value));
        }
        // A packed enum takes the smallest size that holds its values; a wider tag gets
        // an extra enumerator that needs the tag's size.
        let natural = [1u64, 2, 4]
            .into_iter()
            .find(|&s| {
                let bits = 8 * s as u32;
                if lo < 0 { lo >= -(1i64 << (bits - 1)) && hi < (1i64 << (bits - 1)) } else { hi < (1i64 << bits) }
            })
            .unwrap_or(4);
        if natural < size {
            let bits = 8 * (size / 2) as u32;
            let wide = if signed { -(1i64 << (bits - 1)) - 1 } else { 1i64 << bits };
            let e = format!("{}__size", prefix);
            self.views.enumerators.insert(e.clone());
            self.add_member_name(&e);
            items.push(format!("{} = {}", e, wide));
        }
        let _ = writeln!(self.views.defs, "{} {{ {} }};", packed(&name), items.join(", "));
        self.static_assert(&name, size);
        name
    }
}

/// `struct x` or `enum x` as the head of a packed definition.
fn packed(cty: &str) -> String {
    let (keyword, name) = cty.split_once(' ').unwrap_or(("struct", cty));
    format!("{} __attribute__((packed)) {}", keyword, name)
}

/// Padding of `bytes` bytes: unnamed bit-fields, which debuggers do not show.
fn pad(body: &mut String, bytes: u64) {
    for _ in 0..bytes / 8 {
        body.push_str(" uint64_t : 64;");
    }
    for _ in 0..bytes % 8 {
        body.push_str(" uint8_t : 8;");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn padding_is_unnamed_bit_fields() {
        let mut s = String::new();
        pad(&mut s, 10);
        assert_eq!(s, " uint64_t : 64; uint8_t : 8; uint8_t : 8;");
    }

    #[test]
    fn packed_heads() {
        assert_eq!(packed("struct gm_dv1_slice"), "struct __attribute__((packed)) gm_dv1_slice");
        assert_eq!(packed("enum gm_dv2"), "enum __attribute__((packed)) gm_dv2");
    }
}
