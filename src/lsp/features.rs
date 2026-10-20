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
        self.a.line_index(file).offset(line, character, self.enc) as u32
    }

    fn location(&self, span: Span) -> Option<Value> {
        let path = self.a.open_path(span.file)?;
        Some(json!({"uri": (self.uri_of)(&path), "range": self.a.range(span, self.enc)}))
    }
}

fn markdown(code: &str, doc: Option<&str>) -> String {
    let fence = crate::doc::md_html::fence(code);
    let mut s = format!("{fence}gamma\n{}\n{fence}", code);
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

/// Where the cursor is in a call: the offset just after the callee's name, the index of
/// the argument, and the name of the parameter if the argument is written `name: value`.
pub fn call_context(text: &str, off: usize) -> Option<(usize, usize, Option<String>)> {
    let b = text.as_bytes();
    let off = off.min(b.len());
    let (mut depth, mut commas, mut i) = (0usize, 0usize, off);
    let mut last_comma = None;
    let open = loop {
        if i == 0 {
            return None;
        }
        i -= 1;
        match b[i] {
            b')' | b']' | b'}' => depth += 1,
            b'(' if depth == 0 => break i,
            b'(' => depth -= 1,
            b'[' | b'{' if depth == 0 => return None,
            b'[' | b'{' => depth -= 1,
            b',' if depth == 0 => {
                commas += 1;
                last_comma.get_or_insert(i);
            }
            // A string literal ends the search on its line: skip it whole.
            b'"' => {
                let mut j = i;
                while j > 0 && b[j - 1] != b'\n' {
                    j -= 1;
                    if b[j] == b'"' && (j == 0 || b[j - 1] != b'\\') {
                        break;
                    }
                }
                i = j;
            }
            _ => {}
        }
    };
    let mut end = open;
    while end > 0 && b[end - 1].is_ascii_whitespace() && b[end - 1] != b'\n' {
        end -= 1;
    }
    if end == 0 || !(b[end - 1].is_ascii_alphanumeric() || b[end - 1] == b'_') {
        return None;
    }
    // `name: value`: the words before a colon at the start of the argument.
    let start = last_comma.map_or(open + 1, |c| c + 1);
    let seg = text.get(start..off).unwrap_or("").trim_start();
    let named = seg.split_once(':').and_then(|(n, _)| {
        let n = n.trim();
        (!n.is_empty() && n.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_')).then(|| n.to_string())
    });
    Some((end, commas, named))
}

/// Splits the parameter list of the signature text `sig` (`fn(a: T, b: U) -> R`) into
/// its parameters and the text after the closing parenthesis.
fn split_signature(sig: &str) -> Option<(Vec<String>, String)> {
    let start = sig.find("fn(")? + 3;
    let (mut depth, mut cur, mut params) = (0, String::new(), vec![]);
    for (i, c) in sig[start..].char_indices() {
        match c {
            '(' | '[' | '{' => depth += 1,
            ')' | ']' | '}' if depth == 0 => {
                if !cur.trim().is_empty() {
                    params.push(cur.trim().to_string());
                }
                return Some((params, sig[start + i + 1..].to_string()));
            }
            ')' | ']' | '}' => depth -= 1,
            ',' if depth == 0 => {
                params.push(cur.trim().to_string());
                cur.clear();
                continue;
            }
            _ => {}
        }
        cur.push(c);
    }
    None
}

/// Help with the arguments of the call around `off`: the signature of the function
/// called, with the parameter being written marked.
pub fn signature_help(cx: &Ctx, file: FileId, off: u32) -> Value {
    let text = cx.a.text(file);
    let Some((callee_end, mut active, named)) = call_context(text, off as usize) else { return Value::Null };
    let Some(h) = cx.a.index.hover_at(file, callee_end as u32 - 1) else { return Value::Null };
    let code = &h.hover.code;
    let Some((mut params, tail)) = split_signature(code) else { return Value::Null };
    let name = code.split(" ::").next().filter(|_| code.contains(" :: ")).unwrap_or_else(|| {
        let word = |c: char| c.is_alphanumeric() || c == '_';
        let ident_start = text[..callee_end]
            .rfind(|c: char| !word(c))
            .map_or(0, |i| i + text[i..].chars().next().map_or(1, char::len_utf8));
        &text[ident_start..callee_end]
    });
    // A method call supplies the receiver and the type parameters itself.
    let before = text[..callee_end].trim_end_matches(|c: char| c.is_alphanumeric() || c == '_');
    if before.ends_with('.') {
        params.retain(|p| !(p.ends_with(": type") || p.contains(" :: ")));
        if !params.is_empty() {
            params.remove(0);
        }
    }
    if let Some(n) = named
        && let Some(i) = params.iter().position(|p| p.split(':').next().is_some_and(|x| x.trim() == n))
    {
        active = i;
    }
    let label = format!("{}({}){}", name, params.join(", "), tail);
    let mut sig = json!({
        "label": label,
        "parameters": params.iter().map(|p| json!({"label": p})).collect::<Vec<_>>(),
    });
    if let Some(d) = &h.hover.doc {
        sig["documentation"] = json!({"kind": "markdown", "value": d});
    }
    json!({"signatures": [sig], "activeSignature": 0, "activeParameter": active})
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
    fn finds_the_call_around_the_cursor() {
        let at = |t: &str| {
            let off = t.find('|').unwrap();
            call_context(&t.replace('|', ""), off)
        };
        assert_eq!(at("f(|)"), Some((1, 0, None)));
        assert_eq!(at("f(a, |"), Some((1, 1, None)));
        assert_eq!(at("x := foo (a, g(b, c), |)"), Some((8, 2, None)));
        assert_eq!(at("f(a, \"x,y\", |"), Some((1, 2, None)));
        assert_eq!(at("f(a, name: |"), Some((1, 1, Some("name".to_string()))));
        // Not calls: grouping, indexing, nothing.
        assert_eq!(at("x := (a + |"), None);
        assert_eq!(at("a[f(1), |"), None);
        assert_eq!(at("plain |"), None);
        // The inner call wins; after it closes the outer one does.
        assert_eq!(at("f(g(1), |"), Some((1, 1, None)));
        assert_eq!(at("f(g(1, |"), Some((3, 1, None)));
    }

    #[test]
    fn splits_signatures() {
        let (ps, tail) = split_signature("sum :: fn(p: Point, extra: (i32, u8) = (1, 2)) -> i32 needs Out").unwrap();
        assert_eq!(ps, vec!["p: Point", "extra: (i32, u8) = (1, 2)"]);
        assert_eq!(tail, " -> i32 needs Out");
        assert_eq!(split_signature("greet :: fn()").unwrap().0, Vec::<String>::new());
        assert!(split_signature("x: i32").is_none());
    }

    #[test]
    fn full_edit_covers_the_text() {
        let e = full_edit("a\nbc", "x", Encoding::Utf16);
        assert_eq!(e[0]["range"]["end"], json!({"line": 1, "character": 2}));
    }
}
