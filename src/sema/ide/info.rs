//! Describing declarations for editors: hover text, outline kinds and completion items.

use super::super::{DeclId, DeclState, DeclValue, ModuleId, Sema};
use crate::ide::{Hover, Item, Kind};
use crate::source::Span;
use crate::syntax::ast::{self, ExprKind, ItemKind};
use crate::tir::FnId;
use crate::types::{Ty, TyKind};

/// Source text longer than this many lines is cut in a hover.
const HOVER_LINES: usize = 24;

/// The builtin methods of numbers (§6.6, §21.3).
const NUMBER_METHODS: &[&str] =
    &["abs_diff", "bits", "ceil", "checked", "floor", "max", "min", "round", "sat", "to", "trunc", "unsigned_abs"];
/// The builtin methods of arrays and slices (§21.3).
const SEQUENCE_METHODS: &[&str] = &["first", "get", "get_ptr", "last", "len", "ptr"];

pub(super) const TYPE_NAMES: &[&str] = &[
    "bool", "cstring", "f16", "f32", "f64", "i16", "i32", "i64", "i8", "isize", "never", "rawptr", "rune", "string",
    "type", "u16", "u32", "u64", "u8", "usize",
];

pub(super) const BUILTIN_FNS: &[&str] = &["assert", "eprint", "expect_eq", "print", "trap"];

impl<'s> Sema<'s> {
    pub(super) fn src(&self, span: Span) -> String {
        if span == Span::DUMMY || span.file.0 as usize >= self.sess.sources.files().count() {
            return String::new();
        }
        self.sess.sources.text(span).to_string()
    }

    /// The value of an already resolved declaration.
    pub(super) fn decl_done(&self, d: DeclId) -> Option<DeclValue> {
        match &self.decls[d.0 as usize].state {
            DeclState::Done(v) => Some(v.clone()),
            _ => None,
        }
    }

    /// `fn(a: i32, b: string) -> bool needs Out`: the signature of function `f`.
    pub(super) fn fn_signature(&self, f: FnId, skip_receiver: bool) -> String {
        let Some(sig) = self.sigs.get(&f) else { return "fn()".into() };
        let mut params: Vec<String> = vec![];
        let mut skipped = !skip_receiver;
        for p in &sig.params {
            if !skipped && p.generic.is_none() {
                skipped = true;
                continue;
            }
            let value_param = p.generic.is_some_and(|g| self.value_params.contains_key(&g));
            let mut s = if value_param { format!("{} :: {}", p.name, p.ty) } else { format!("{}: {}", p.name, p.ty) };
            if let Some(d) = &p.default {
                s.push_str(" = ");
                s.push_str(&self.src(d.span));
            }
            params.push(s);
        }
        let mut out = format!("fn({})", params.join(", "));
        if !sig.ret.is_unit() {
            out.push_str(&format!(" -> {}", sig.ret));
        }
        if !sig.effects.effects.is_empty() {
            let es: Vec<String> = sig.effects.effects.iter().map(|e| e.to_string()).collect();
            out.push_str(&format!(" needs {}", es.join(" + ")));
        }
        out
    }

    /// The text of a declaration's source, cut to a reasonable length.
    fn item_text(&self, span: Span) -> String {
        let text = self.src(span);
        let lines: Vec<&str> = text.lines().collect();
        if lines.len() <= HOVER_LINES {
            return text.trim().to_string();
        }
        let mut out = lines[..HOVER_LINES].join("\n");
        out.push_str("\n    …\n}");
        out
    }

    /// The first line of a declaration's source.
    fn first_line(&self, span: Span) -> String {
        self.src(span).lines().next().unwrap_or("").trim_end().to_string()
    }

    /// The kind and hover text of declaration `d`.
    pub(super) fn decl_info(&self, d: DeclId) -> (Kind, Hover) {
        let decl = &self.decls[d.0 as usize];
        let name = decl.name.name;
        let doc = decl.item.doc.clone();
        let item = &decl.item;
        let init_kind = match &item.kind {
            ItemKind::Binding(b) => Some(&b.init.kind),
            _ => None,
        };
        if let Some((ed, i)) = decl.op_of {
            let eff = match self.decl_done(ed) {
                Some(DeclValue::Effect(e)) => self.effects.get(&e),
                _ => None,
            };
            let code = match eff.and_then(|info| info.ops.get(i as usize).map(|op| (info, op))) {
                Some((info, op)) => {
                    let ps: Vec<String> = op.params.iter().map(|p| format!("{}: {}", p.name, p.ty)).collect();
                    let ret = if op.ret.is_unit() { String::new() } else { format!(" -> {}", op.ret) };
                    let kw = match op.kind {
                        ast::OpKind::Fn => "fn",
                        ast::OpKind::Esc => "esc",
                        ast::OpKind::Ctl => "ctl",
                    };
                    format!("{}.{} :: {}({}){}", info.name, op.name, kw, ps.join(", "), ret)
                }
                None => name.to_string(),
            };
            return (Kind::Operation, Hover { code, doc });
        }
        let (kind, code) = match (self.decl_done(d), ()) {
            (Some(DeclValue::Fn(f)), _) => (Kind::Function, format!("{} :: {}", name, self.fn_signature(f, false))),
            (Some(DeclValue::Const { ty, .. }), _) => (Kind::Const, format!("{}: {}", name, ty)),
            (Some(DeclValue::Global { ty, .. }), _) => (Kind::Variable, format!("#global {}: {}", name, ty)),
            (Some(DeclValue::Module(_)), _) => (Kind::Module, self.first_line(item.span)),
            (Some(DeclValue::Effect(_)), _) => (Kind::Effect, self.item_text(item.span)),
            (Some(DeclValue::Untyped(_)), _) => (Kind::Const, self.first_line(item.span)),
            (Some(DeclValue::Type(_) | DeclValue::GenericType(_)) | None, _) | (Some(DeclValue::Error), _) => {
                let kind = match init_kind {
                    Some(ExprKind::Struct(_)) => Kind::Struct,
                    Some(ExprKind::Union(_)) => Kind::Union,
                    Some(ExprKind::Effect(_)) => Kind::Effect,
                    Some(ExprKind::Import(_) | ExprKind::Foreign(_)) => Kind::Module,
                    Some(ExprKind::Fn(_)) => Kind::Function,
                    _ => Kind::TypeAlias,
                };
                let text = match kind {
                    Kind::Struct | Kind::Union | Kind::Effect => self.item_text(item.span),
                    _ => self.first_line(item.span),
                };
                (kind, text)
            }
            _ => (Kind::Const, name.to_string()),
        };
        (kind, Hover { code, doc })
    }

    /// A completion item for declaration `d`.
    pub(super) fn decl_item(&self, d: DeclId) -> Item {
        let decl = &self.decls[d.0 as usize];
        let (kind, hover) = self.decl_info(d);
        let detail = match self.decl_done(d) {
            Some(DeclValue::Fn(f)) => self.fn_signature(f, false),
            Some(DeclValue::Const { ty, .. } | DeclValue::Global { ty, .. }) => ty.to_string(),
            _ => hover.code.lines().next().unwrap_or("").to_string(),
        };
        Item { label: decl.name.name.to_string(), kind, detail, doc: hover.doc }
    }

    /// The public declarations of module `m`, as the members of `m.`.
    pub(super) fn module_members(&self, m: ModuleId) -> Vec<Item> {
        let mut ds: Vec<DeclId> = self.modules[m.0 as usize]
            .scope
            .values()
            .copied()
            .filter(|d| self.decls[d.0 as usize].item.is_pub)
            .collect();
        ds.sort();
        ds.into_iter().map(|d| self.decl_item(d)).collect()
    }

    /// The names of the universe scope: the prelude's public declarations and the builtins.
    pub(super) fn universe_items(&self) -> Vec<Item> {
        let mut items = vec![];
        if let Some(p) = self.prelude {
            let mut ds: Vec<DeclId> = self.modules[p.0 as usize]
                .scope
                .values()
                .copied()
                .filter(|d| {
                    let decl = &self.decls[d.0 as usize];
                    decl.item.is_pub && !self.universe_hidden(*d) && decl.name.name.as_str() != "core"
                })
                .collect();
            ds.sort();
            items.extend(ds.into_iter().map(|d| self.decl_item(d)));
        }
        let simple = |label: &str, kind, detail: &str| Item {
            label: label.to_string(),
            kind,
            detail: detail.to_string(),
            doc: None,
        };
        items.extend(TYPE_NAMES.iter().map(|n| simple(n, Kind::TypeAlias, "builtin type")));
        items.extend(BUILTIN_FNS.iter().map(|n| simple(n, Kind::Function, "builtin function")));
        items.extend(crate::syntax::token::Keyword::ALL.iter().map(|k| simple(k.as_str(), Kind::Keyword, "")));
        items
    }

    /// The module-level names of module `m`: its own declarations.
    pub(super) fn module_scope_items(&self, m: ModuleId) -> Vec<Item> {
        let mut ds: Vec<DeclId> = self.modules[m.0 as usize].scope.values().copied().collect();
        ds.sort();
        ds.into_iter().map(|d| self.decl_item(d)).collect()
    }

    /// The members reached with `.` on a value of the nominal or builtin type `ty`:
    /// fields, then the functions of the type's module that take it as a receiver.
    pub(super) fn value_members(&self, ty: Ty) -> Vec<Item> {
        let mut items = vec![];
        match ty.kind() {
            TyKind::Adt(id, _) => {
                self.field_items(ty, &mut items, 0);
                if let Some(info) = self.adts.get(id) {
                    let template = id.template();
                    let mut ds: Vec<DeclId> = self.modules[info.module.0 as usize].scope.values().copied().collect();
                    ds.sort();
                    for d in ds {
                        let Some(DeclValue::Fn(f)) = self.decl_done(d) else { continue };
                        let Some(sig) = self.sigs.get(&f) else { continue };
                        let Some(first) = sig.params.iter().find(|p| p.generic.is_none()) else { continue };
                        let mut recv = first.ty;
                        if let TyKind::Ptr { pointee, .. } = recv.kind() {
                            recv = *pointee;
                        }
                        let fits = matches!(recv.kind(), TyKind::Adt(x, _) if x.template() == template);
                        if fits {
                            items.push(Item {
                                label: self.decls[d.0 as usize].name.name.to_string(),
                                kind: Kind::Method,
                                detail: self.fn_signature(f, true),
                                doc: self.decls[d.0 as usize].item.doc.clone(),
                            });
                        }
                    }
                }
            }
            _ if ty.is_integer() || ty.is_float() => items.extend(builtin_methods(NUMBER_METHODS)),
            TyKind::Slice { .. } | TyKind::Array { .. } => items.extend(builtin_methods(SEQUENCE_METHODS)),
            _ => {}
        }
        items
    }

    /// Fields of the struct `ty`, including those it embeds with `using` (§5.10.3).
    fn field_items(&self, ty: Ty, items: &mut Vec<Item>, depth: u32) {
        let TyKind::Adt(id, _) = ty.kind() else { return };
        let Some(def) = self.prog.adts.get(id) else { return };
        let fields = match &def.kind {
            crate::tir::AdtKind::Struct { fields, .. } | crate::tir::AdtKind::CUnion { fields } => fields,
            crate::tir::AdtKind::Union { .. } => return,
        };
        for (i, f) in fields.iter().enumerate() {
            items.push(Item { label: f.name.to_string(), kind: Kind::Field, detail: f.ty.to_string(), doc: None });
            if i == 0 && depth < 4 && self.adts.get(id).is_some_and(|a| a.has_using) {
                self.field_items(f.ty, items, depth + 1);
            }
        }
    }

    /// The variants of the union `ty`, as the members of the type's name.
    pub(super) fn variant_members(&self, ty: Ty) -> Vec<Item> {
        let TyKind::Adt(id, _) = ty.kind() else { return vec![] };
        let Some(def) = self.prog.adts.get(id) else { return vec![] };
        match &def.kind {
            crate::tir::AdtKind::Union { variants, .. } => variants
                .iter()
                .map(|v| Item {
                    label: v.name.to_string(),
                    kind: Kind::Variant,
                    detail: v.payload.map_or(String::new(), |p| format!(": {}", p)),
                    doc: None,
                })
                .collect(),
            _ => vec![],
        }
    }

    /// The operations of effect declaration `d`, as the members of the effect's name.
    pub(super) fn operation_members(&self, d: DeclId) -> Vec<Item> {
        let Some(DeclValue::Effect(e)) = self.decl_done(d) else { return vec![] };
        let Some(info) = self.effects.get(&e) else { return vec![] };
        info.ops
            .iter()
            .map(|op| {
                let ps: Vec<String> = op.params.iter().map(|p| format!("{}: {}", p.name, p.ty)).collect();
                let ret = if op.ret.is_unit() { String::new() } else { format!(" -> {}", op.ret) };
                Item {
                    label: op.name.to_string(),
                    kind: Kind::Operation,
                    detail: format!("({}){}", ps.join(", "), ret),
                    doc: None,
                }
            })
            .collect()
    }

    /// The declaration of the struct or union that type `ty` (or the type it points to)
    /// instantiates.
    pub(super) fn adt_decl_of(&self, mut ty: Ty) -> Option<DeclId> {
        while let TyKind::Ptr { pointee, .. } = ty.kind() {
            ty = *pointee;
        }
        let TyKind::Adt(id, _) = ty.kind() else { return None };
        self.adt_decls.get(&id.template()).copied()
    }
}

fn builtin_methods(names: &[&str]) -> impl Iterator<Item = Item> {
    names
        .iter()
        .map(|n| Item { label: (*n).to_string(), kind: Kind::Method, detail: "builtin method".to_string(), doc: None })
        .collect::<Vec<_>>()
        .into_iter()
}
