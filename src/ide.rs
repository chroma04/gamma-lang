//! Facts about a checked program for editors: what a name refers to, what to show on
//! hover, which names are in scope, and the outline of each file.
//!
//! The index is built by [`crate::sema::analyze_for_ide`] and holds no references
//! into the compiler: positions are [`Span`]s (byte offsets), types are interned
//! [`Ty`] handles and everything else is plain text. Queries are linear scans, which
//! is fast enough for the size of one module.

use crate::source::{FileId, Span};
use crate::types::Ty;
use std::collections::HashMap;
use std::rc::Rc;

/// What a declaration, member or completion item is.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Kind {
    Module,
    Function,
    Method,
    Struct,
    Union,
    Effect,
    Handler,
    Const,
    Variable,
    Parameter,
    Field,
    Variant,
    Operation,
    Test,
    TypeAlias,
    TypeParam,
    Keyword,
}

/// A named thing: a completion item, and the data of an outline entry.
#[derive(Clone, Debug)]
pub struct Item {
    pub label: String,
    pub kind: Kind,
    /// The type or signature, shown next to the label.
    pub detail: String,
    pub doc: Option<String>,
}

/// What to show for a declared name: the declaration written as source code, and the
/// doc comment attached to it.
#[derive(Clone, Debug)]
pub struct Hover {
    pub code: String,
    pub doc: Option<String>,
}

/// The type of an expression.
#[derive(Clone, Copy, Debug)]
pub struct TypeInfo {
    pub ty: Ty,
    /// Worth showing on hover (not a block or a control-flow construct).
    pub hover: bool,
}

/// What a hover request finds: the text for the range `range`.
#[derive(Clone, Debug)]
pub struct HoverResult {
    pub range: Span,
    pub hover: Hover,
}

/// The name at `from` refers to the declaration whose name is at `to`.
#[derive(Clone, Copy, Debug)]
pub struct Ref {
    pub from: Span,
    pub to: Span,
}

/// An entry of a file's outline.
#[derive(Clone, Debug)]
pub struct DocSymbol {
    pub name: String,
    pub kind: Kind,
    pub detail: String,
    /// The whole declaration.
    pub range: Span,
    /// The declared name.
    pub name_span: Span,
    pub children: Vec<DocSymbol>,
}

/// A local name and the bytes where it is visible.
#[derive(Clone, Debug)]
pub struct LocalScope {
    pub item: Item,
    pub def: Span,
    /// Visible at offsets `from..to` of the file of `def`.
    pub from: u32,
    pub to: u32,
}

#[derive(Default)]
pub struct IdeIndex {
    pub refs: Vec<Ref>,
    /// Hover text by the span of a declared name. A use of the name shows the text of
    /// the declaration it refers to.
    pub defs: HashMap<Span, Hover>,
    /// The outline of each file of the analyzed module.
    pub outlines: HashMap<FileId, Vec<DocSymbol>>,
    pub scopes: Vec<LocalScope>,
    /// The type of expressions, by span (the innermost expression for equal spans).
    pub types: HashMap<Span, TypeInfo>,
    /// Members of nominal types that occur in `types`, by type.
    pub type_members: HashMap<Ty, Rc<Vec<Item>>>,
    /// Members of the module or type that the name at a span denotes.
    pub receivers: HashMap<Span, Rc<Vec<Item>>>,
    /// Top-level names usable in each file: its module's declarations.
    pub module_items: HashMap<FileId, Rc<Vec<Item>>>,
    /// The names of the universe scope.
    pub universe: Rc<Vec<Item>>,
}

fn contains(s: Span, file: FileId, off: u32) -> bool {
    s.file == file && s.lo <= off && off <= s.hi
}

impl IdeIndex {
    /// What to show for the name or expression around `off`.
    pub fn hover_at(&self, file: FileId, off: u32) -> Option<HoverResult> {
        let by_use = self
            .refs
            .iter()
            .filter(|r| contains(r.from, file, off))
            .min_by_key(|r| r.from.hi - r.from.lo)
            .and_then(|r| self.defs.get(&r.to).map(|h| (r.from, h)));
        let by_def = self
            .defs
            .iter()
            .filter(|(s, _)| contains(**s, file, off))
            .min_by_key(|(s, _)| s.hi - s.lo)
            .map(|(s, h)| (*s, h));
        let named = match (by_use, by_def) {
            (Some(a), Some(b)) => Some(if b.0.hi - b.0.lo < a.0.hi - a.0.lo { b } else { a }),
            (a, b) => a.or(b),
        };
        let typed = self
            .types
            .iter()
            .filter(|(s, t)| t.hover && contains(**s, file, off) && s.hi > s.lo)
            .min_by_key(|(s, _)| s.hi - s.lo);
        match (named, typed) {
            (Some((range, h)), t) if t.is_none_or(|(s, _)| s.hi - s.lo >= range.hi - range.lo) => {
                Some(HoverResult { range, hover: h.clone() })
            }
            (_, Some((s, t))) => Some(HoverResult { range: *s, hover: Hover { code: t.ty.to_string(), doc: None } }),
            _ => None,
        }
    }

    /// The declaration that the name at `off` refers to (or, on a declared name,
    /// the declaration itself).
    pub fn definition_at(&self, file: FileId, off: u32) -> Option<Span> {
        let r = self
            .refs
            .iter()
            .filter(|r| contains(r.from, file, off))
            .min_by_key(|r| r.from.hi - r.from.lo)
            .map(|r| r.to);
        r.or_else(|| self.declared_at(file, off))
    }

    /// The span of a declared name that covers `off`.
    pub fn declared_at(&self, file: FileId, off: u32) -> Option<Span> {
        self.refs.iter().map(|r| r.to).filter(|s| contains(*s, file, off)).min_by_key(|s| s.hi - s.lo)
    }

    /// All uses of the declaration whose name is at `def`.
    pub fn uses_of(&self, def: Span) -> Vec<Span> {
        let mut v: Vec<Span> = self.refs.iter().filter(|r| r.to == def).map(|r| r.from).collect();
        v.sort_by_key(|s| (s.file, s.lo, s.hi));
        v.dedup();
        v
    }

    /// The locals visible at `off`.
    pub fn locals_at(&self, file: FileId, off: u32) -> impl Iterator<Item = &LocalScope> {
        self.scopes.iter().filter(move |s| s.def.file == file && s.from <= off && off <= s.to)
    }

    /// The members of the value, type or module written at `lo..hi`.
    pub fn members_at(&self, file: FileId, lo: u32, hi: u32) -> Option<Rc<Vec<Item>>> {
        let span = Span { file, lo, hi };
        if let Some(m) = self.receivers.get(&span) {
            return Some(m.clone());
        }
        let mut ty = self.types.get(&span)?.ty;
        // Members are reached through pointers and optionals alike.
        loop {
            if let Some(m) = self.type_members.get(&ty) {
                return Some(m.clone());
            }
            match ty.kind() {
                crate::types::TyKind::Ptr { pointee, .. } => ty = *pointee,
                crate::types::TyKind::Optional(inner) => ty = *inner,
                _ => return None,
            }
        }
    }

    /// Sorts the tables so that lookups and results are deterministic.
    pub fn finish(&mut self) {
        let key = |s: &Span| (s.file, s.lo, s.hi);
        self.refs.sort_by_key(|r| (key(&r.from), key(&r.to)));
        self.refs.dedup_by_key(|r| (key(&r.from), key(&r.to)));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hover_prefers_the_narrowest_name() {
        let f = FileId(0);
        let sp = |lo, hi| Span { file: f, lo, hi };
        let mut ix = IdeIndex::default();
        ix.defs.insert(sp(0, 3), Hover { code: "decl".into(), doc: None });
        ix.refs.push(Ref { from: sp(10, 12), to: sp(0, 3) });
        assert_eq!(ix.hover_at(f, 11).unwrap().hover.code, "decl");
        assert_eq!(ix.hover_at(f, 1).unwrap().range, sp(0, 3));
        assert!(ix.hover_at(f, 30).is_none());
    }
}
