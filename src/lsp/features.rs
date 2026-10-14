//! The answers to language requests, as LSP JSON, computed from an [`Analysis`].

use super::analysis::{Analysis, range_json};
use super::position::{Encoding, LineIndex};
use crate::ide::{DocSymbol, Item, Kind};
use crate::source::{FileId, Span};
use serde_json::{Value, json};
use std::path::Path;

/// What a request needs to build its answer.
pub struct Ctx<'a> {
    pub a: &'a Analysis,
    pub enc: Encoding,
    pub uri_of: &'a dyn Fn(&Path) -> String,
}

impl Ctx<'_> {
    pub fn offset(&self, file: FileId, line: u32, character: u32) -> u32 {
        LineIndex::new(self.a.text(file)).offset(line, character, self.enc) as u32
    }

    fn location(&self, span: Span) -> Option<Value> {
        let path = self.a.real_path(span.file)?;
        Some(json!({"uri": (self.uri_of)(&path), "range": self.a.range(span, self.enc)}))
    }
}

fn markdown(code: &str, doc: Option<&str>) -> String {
    let mut s = format!("```gamma\n{}\n```", code);
    if let Some(d) = doc {
        s.push_str("\n\n");
        s.push_str(d);
    }
    s
}

pub fn hover(cx: &Ctx, file: FileId, off: u32) -> Value {
    let Some(h) = cx.a.index.hover_at(file, off) else { return Value::Null };
    json!({
        "contents": {"kind": "markdown", "value": markdown(&h.hover.code, h.hover.doc.as_deref())},
        "range": cx.a.range(h.range, cx.enc),
    })
}

pub fn definition(cx: &Ctx, file: FileId, off: u32) -> Value {
    cx.a.index.definition_at(file, off).and_then(|s| cx.location(s)).unwrap_or(Value::Null)
}

pub fn references(cx: &Ctx, file: FileId, off: u32, include_declaration: bool) -> Value {
    let Some(def) = cx.a.index.definition_at(file, off) else { return json!([]) };
    let mut spans = cx.a.index.uses_of(def);
    if include_declaration && def.hi > def.lo {
        spans.insert(0, def);
    }
    Value::Array(spans.into_iter().filter_map(|s| cx.location(s)).collect())
}

fn symbol_kind(k: Kind) -> u32 {
    match k {
        Kind::Module => 2,
        Kind::Function | Kind::Test => 12,
        Kind::Method | Kind::Operation => 6,
        Kind::Struct => 23,
        Kind::Union => 10,
        Kind::Effect => 11,
        Kind::Handler => 19,
        Kind::Const => 14,
        Kind::Variable | Kind::Parameter => 13,
        Kind::Field => 8,
        Kind::Variant => 22,
        Kind::TypeAlias => 5,
        Kind::TypeParam => 26,
        Kind::Keyword => 13,
    }
}

fn completion_kind(k: Kind) -> u32 {
    match k {
        Kind::Module => 9,
        Kind::Function | Kind::Test => 3,
        Kind::Method | Kind::Operation => 2,
        Kind::Struct => 22,
        Kind::Union => 13,
        Kind::Effect => 8,
        Kind::Handler => 7,
        Kind::Const => 21,
        Kind::Variable | Kind::Parameter => 6,
        Kind::Field => 5,
        Kind::Variant => 20,
        Kind::TypeAlias => 7,
        Kind::TypeParam => 25,
        Kind::Keyword => 14,
    }
}

fn symbol_json(cx: &Ctx, s: &DocSymbol) -> Value {
    json!({
        "name": s.name,
        "detail": s.detail,
        "kind": symbol_kind(s.kind),
        "range": cx.a.range(s.range, cx.enc),
        "selectionRange": cx.a.range(s.name_span, cx.enc),
        "children": s.children.iter().map(|c| symbol_json(cx, c)).collect::<Vec<_>>(),
    })
}

pub fn document_symbols(cx: &Ctx, file: FileId) -> Value {
    let syms = cx.a.index.outlines.get(&file).map(|v| v.as_slice()).unwrap_or(&[]);
    Value::Array(syms.iter().map(|s| symbol_json(cx, s)).collect())
}

fn item_json(i: &Item, sort: Option<&str>) -> Value {
    let mut v = json!({"label": i.label, "kind": completion_kind(i.kind)});
    if !i.detail.is_empty() {
        v["detail"] = json!(i.detail);
    }
    if let Some(d) = &i.doc {
        v["documentation"] = json!({"kind": "markdown", "value": d});
    }
    if let Some(s) = sort {
        v["sortText"] = json!(format!("{}{}", s, i.label));
    }
    v
}

/// Completions for a name written at `off`: the locals in scope, the module's
/// declarations, then the universe scope (the core library, builtin types and keywords).
pub fn names(cx: &Ctx, file: FileId, off: u32) -> Vec<Value> {
    let ix = &cx.a.index;
    let mut seen = std::collections::HashSet::new();
    let mut out = vec![];
    // Later locals shadow nothing (shadowing is an error), so order only matters for display.
    let mut locals: Vec<_> = ix.locals_at(file, off).collect();
    locals.sort_by_key(|s| std::cmp::Reverse(s.def.lo));
    for l in locals {
        if seen.insert(l.item.label.clone()) {
            out.push(item_json(&l.item, Some("0")));
        }
    }
    let module = ix.module_items.get(&file).map(|m| m.as_slice()).unwrap_or(&[]);
    for i in module {
        if seen.insert(i.label.clone()) {
            out.push(item_json(i, Some("1")));
        }
    }
    for i in ix.universe.iter() {
        if seen.insert(i.label.clone()) {
            out.push(item_json(i, Some("2")));
        }
    }
    out
}

/// Completions for the members of the expression written at `lo..hi`.
pub fn members(cx: &Ctx, file: FileId, lo: u32, hi: u32) -> Option<Vec<Value>> {
    let ix = &cx.a.index;
    let found = ix.members_at(file, lo, hi).or_else(|| {
        // `(a + b).`: the checker knows the expression inside the parentheses.
        let text = cx.a.text(file);
        let inner = text.get(lo as usize..hi as usize)?;
        (inner.starts_with('(') && inner.ends_with(')')).then(|| ix.members_at(file, lo + 1, hi - 1))?
    })?;
    Some(found.iter().map(|i| item_json(i, None)).collect())
}

/// For a completion request after `receiver.prefix`, the offsets where the receiver
/// starts and the dot is.
pub fn member_context(text: &str, off: usize) -> Option<(usize, usize)> {
    let b = text.as_bytes();
    let is_ident = |c: u8| c.is_ascii_alphanumeric() || c == b'_';
    let mut p = off.min(b.len());
    while p > 0 && is_ident(b[p - 1]) {
        p -= 1;
    }
    if p == 0 || b[p - 1] != b'.' || (p >= 2 && b[p - 2] == b'.') {
        return None;
    }
    let dot = p - 1;
    let mut i = dot;
    while i > 0 {
        let c = b[i - 1];
        if is_ident(c) || c == b'.' || c == b'^' {
            i -= 1;
        } else if c == b')' || c == b']' {
            let (open, close) = if c == b')' { (b'(', b')') } else { (b'[', b']') };
            let mut depth = 0;
            let mut j = i;
            while j > 0 {
                j -= 1;
                if b[j] == close {
                    depth += 1;
                } else if b[j] == open {
                    depth -= 1;
                    if depth == 0 {
                        break;
                    }
                }
            }
            if depth != 0 {
                return None;
            }
            i = j;
        } else {
            break;
        }
    }
    // A leading `.` belongs to something else (`..`, `.Variant`).
    while i < dot && b[i] == b'.' {
        i += 1;
    }
    (i < dot).then_some((i, dot))
}

/// The edit that replaces the whole of `old` by `new`.
pub fn full_edit(old: &str, new: &str, enc: Encoding) -> Value {
    let ix = LineIndex::new(old);
    json!([{"range": range_json(&ix, 0, old.len(), enc), "newText": new}])
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ctx_of(text: &str, at: &str) -> Option<(usize, usize)> {
        let off = text.find(at).unwrap() + at.len();
        member_context(text, off)
    }

    #[test]
    fn finds_the_receiver_before_a_dot() {
        let t = "x := foo.bar.ba";
        let (lo, dot) = ctx_of(t, "foo.bar.ba").unwrap();
        assert_eq!(&t[lo..dot], "foo.bar");
        let t = "if pt.";
        let (lo, dot) = ctx_of(t, "pt.").unwrap();
        assert_eq!(&t[lo..dot], "pt");
        let t = "make(1, 2).x";
        let (lo, dot) = ctx_of(t, "make(1, 2).x").unwrap();
        assert_eq!(&t[lo..dot], "make(1, 2)");
        let t = "a[i].";
        let (lo, dot) = ctx_of(t, "a[i].").unwrap();
        assert_eq!(&t[lo..dot], "a[i]");
    }

    #[test]
    fn ranges_and_variants_are_not_member_accesses() {
        assert!(ctx_of("for i in 0..n", "0..").is_none());
        assert!(ctx_of("x := .Red", ".").is_none());
        assert!(ctx_of("plain", "pl").is_none());
    }

    #[test]
    fn full_edit_covers_the_text() {
        let e = full_edit("a\nbc", "x", Encoding::Utf16);
        assert_eq!(e[0]["range"]["end"], json!({"line": 1, "character": 2}));
    }
}
