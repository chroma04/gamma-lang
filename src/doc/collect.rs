//! Reading a module: its public declarations, signatures and documentation.

use crate::driver::Session;
use crate::source::Span;
use crate::syntax::ast::*;
use std::path::PathBuf;

/// What a public declaration is. The order is the order of the sections in the output.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Kind {
    Struct,
    Union,
    TypeAlias,
    Effect,
    Handler,
    Const,
    Var,
    Function,
    Foreign,
    Import,
}

impl Kind {
    pub const ALL: [Kind; 10] = [
        Kind::Struct,
        Kind::Union,
        Kind::TypeAlias,
        Kind::Effect,
        Kind::Handler,
        Kind::Const,
        Kind::Var,
        Kind::Function,
        Kind::Foreign,
        Kind::Import,
    ];

    /// The title of the section listing declarations of this kind.
    pub fn section(self) -> &'static str {
        match self {
            Kind::Struct => "Structs",
            Kind::Union => "Unions",
            Kind::TypeAlias => "Type aliases",
            Kind::Effect => "Effects",
            Kind::Handler => "Handlers",
            Kind::Const => "Constants",
            Kind::Var => "Variables",
            Kind::Function => "Functions",
            Kind::Foreign => "Foreign functions",
            Kind::Import => "Imports",
        }
    }

    /// What to call the declaration in a warning.
    fn noun(self) -> &'static str {
        match self {
            Kind::Struct => "struct",
            Kind::Union => "union",
            Kind::TypeAlias => "type alias",
            Kind::Effect => "effect",
            Kind::Handler => "handler",
            Kind::Const => "constant",
            Kind::Var => "variable",
            Kind::Function => "function",
            Kind::Foreign => "foreign block",
            Kind::Import => "import",
        }
    }
}

/// One public declaration.
#[derive(Clone, Debug)]
pub struct Decl {
    pub kind: Kind,
    /// The declared name; for a handler, `handler E for T`.
    pub name: String,
    /// The signature as written, in the order and layout of the source.
    pub sig: String,
    pub doc: Option<String>,
    /// What the fields, variants or operations of the declaration are called, and the
    /// documented ones among them with their doc comments.
    pub member_label: &'static str,
    pub members: Vec<(String, String)>,
    /// Where the declaration is, for warnings: `file:line`.
    pub location: String,
    /// For a handler declaration that is not `pub`: the name of the type it handles. It is
    /// listed only if that type is a public declaration of the module, since a handler
    /// declaration applies wherever its type is used (spec section 12.6).
    handler_target: Option<String>,
}

/// A documented module.
#[derive(Clone, Debug)]
pub struct Module {
    /// The name shown in the documentation: the import path for the standard library
    /// (`std/io`), else the directory name.
    pub name: String,
    /// The output file of the module below the output directory, without extension.
    pub path: String,
    /// The module's own documentation (`//!` lines).
    pub doc: String,
    pub decls: Vec<Decl>,
    /// The first file of the module, for warnings.
    pub location: String,
}

impl Module {
    /// Warnings for the public declarations (and the module itself) without documentation.
    pub fn missing(&self) -> Vec<String> {
        let mut out = Vec::new();
        if self.doc.trim().is_empty() {
            out.push(format!("{}: warning: module `{}` has no `//!` documentation", self.location, self.name));
        }
        for d in &self.decls {
            if d.doc.is_none() && d.handler_target.is_none() {
                out.push(format!("{}: warning: public {} `{}` has no doc comment", d.location, d.kind.noun(), d.name));
            }
        }
        out
    }
}

/// Reads the module made of `files` (parsed in the given order).
///
/// `name` is the module's name and `path` its output path. A file that does not parse is
/// an error.
pub fn load_module(files: &[PathBuf], name: &str, path: &str) -> Result<Module, String> {
    let mut sess = Session::new();
    let mut module = Module {
        name: name.to_string(),
        path: path.to_string(),
        doc: String::new(),
        decls: Vec::new(),
        location: files.first().map(|f| f.display().to_string()).unwrap_or_default(),
    };
    let mut docs = Vec::new();
    for f in files {
        let Some(id) = sess.load(f) else {
            return Err(sess.render_diagnostics(crate::diag::DiagFormat::Human));
        };
        let ast = sess.parse(id);
        if sess.diags.has_errors() {
            return Err(sess.render_diagnostics(crate::diag::DiagFormat::Human));
        }
        let file = sess.sources.file(id);
        let src = file.src.as_str();
        let cx = Cx { src, path: f.display().to_string(), file };
        let first = ast.items.first().map_or(src.len(), |i| i.span.lo as usize);
        let doc = module_doc(&src[..first]);
        if !doc.is_empty() {
            docs.push(doc);
        }
        cx.items(&ast.items, None, &mut module.decls);
    }
    module.doc = docs.join("\n\n");
    let public: Vec<String> = module.decls.iter().filter(|d| d.kind != Kind::Handler).map(|d| d.name.clone()).collect();
    module.decls.retain(|d| d.handler_target.as_ref().is_none_or(|t| public.contains(t)));
    Ok(module)
}

/// The `//!` lines of `head` (the text before the first item), as Markdown.
fn module_doc(head: &str) -> String {
    let lines: Vec<&str> = head
        .lines()
        .filter_map(|l| l.trim_start().strip_prefix("//!"))
        .map(|l| l.strip_prefix(' ').unwrap_or(l))
        .collect();
    lines.join("\n").trim_matches('\n').to_string()
}

/// Names of the builtin types, for telling `c_int :: i32` (a type alias) from a constant.
const PRIMITIVES: [&str; 17] = [
    "i8", "i16", "i32", "i64", "isize", "u8", "u16", "u32", "u64", "usize", "f32", "f64", "bool", "string", "i128",
    "u128", "type",
];

struct Cx<'a> {
    src: &'a str,
    path: String,
    file: &'a crate::source::SourceFile,
}

impl Cx<'_> {
    fn text(&self, s: Span) -> &str {
        &self.src[s.lo as usize..s.hi as usize]
    }

    fn location(&self, lo: u32) -> String {
        format!("{}:{}", self.path, self.file.line_col(lo).0)
    }

    fn items(&self, items: &[Item], cond: Option<&str>, out: &mut Vec<Decl>) {
        for item in items {
            match &item.kind {
                ItemKind::When(w) => {
                    let c = format!("when {}", squash(self.text(w.cond.span)));
                    let c = match cond {
                        Some(outer) => format!("{}; {}", outer, c),
                        None => c,
                    };
                    self.items(&w.then, Some(&c), out);
                    if let Some(e) = &w.else_ {
                        let c = match cond {
                            Some(outer) => format!("{}; otherwise", outer),
                            None => "otherwise".to_string(),
                        };
                        self.items(e, Some(&c), out);
                    }
                }
                ItemKind::Binding(b) if item.vis == Visibility::Public => self.binding(item, b, cond, out),
                ItemKind::Handler(h) => self.handler(item, h, cond, out),
                _ => {}
            }
        }
    }

    fn handler(&self, item: &Item, h: &HandlerDecl, cond: Option<&str>, out: &mut Vec<Decl>) {
        let from = h.effects.as_ref().and_then(|e| e.last()).map_or(h.for_ty.span.hi, |e| e.span.hi);
        let open = from as usize;
        let open = open + self.src[open..].find('{').unwrap_or(0);
        let head = self.src[h.span.lo as usize..open].trim_end();
        let name = squash(&self.src[h.span.lo as usize..h.for_ty.span.hi as usize]);
        let sig = self.with_attrs(item, cond, head.to_string());
        out.push(Decl {
            kind: Kind::Handler,
            name,
            sig,
            doc: item.doc.clone(),
            member_label: "",
            members: Vec::new(),
            location: self.location(item.span.lo),
            handler_target: (item.vis != Visibility::Public).then(|| self.base_name(&h.for_ty)),
        });
    }

    /// The name of the type that `e` denotes, without pointers and generic arguments.
    fn base_name(&self, e: &Expr) -> String {
        match &e.kind {
            ExprKind::PtrType { inner, .. } | ExprKind::Paren(inner) => self.base_name(inner),
            ExprKind::Call { callee, .. } => self.base_name(callee),
            _ => self.text(e.span).to_string(),
        }
    }

    fn binding(&self, item: &Item, b: &Binding, cond: Option<&str>, out: &mut Vec<Decl>) {
        let name_lo = b.names[0].span.lo;
        let name = b.names.iter().map(|n| self.text(n.span)).collect::<Vec<_>>().join(", ");
        let init = strip_attrs(&b.init);
        let mut members = Vec::new();
        let mut label = "";
        let (kind, sig) = match &init.kind {
            ExprKind::Fn(f) => {
                let end = match &f.body {
                    FnBody::Block(blk) => blk.span.lo,
                    FnBody::Expr(e) => e.span.lo,
                } as usize;
                let text = self.src[name_lo as usize..end].trim_end();
                let text = text.strip_suffix("=>").unwrap_or(text).trim_end();
                (Kind::Function, text.to_string())
            }
            ExprKind::Struct(s) => {
                label = "Fields";
                let mut body = Vec::new();
                let mut hidden = false;
                self.fields(&s.members, &mut body, &mut members, &mut hidden);
                (Kind::Struct, self.braced(name_lo, s.span.lo, &body, hidden))
            }
            ExprKind::Union(u) => {
                label = "Variants";
                let mut body = Vec::new();
                self.variants(&u.members, &mut body, &mut members);
                (Kind::Union, self.braced(name_lo, u.span.lo, &body, false))
            }
            ExprKind::Effect(e) => {
                label = "Operations";
                let mut body = Vec::new();
                for op in &e.ops {
                    body.push(self.text(op.span).to_string());
                    self.member_doc(&mut members, self.text(op.name.span), op.span.lo);
                }
                (Kind::Effect, self.braced(name_lo, e.span.lo, &body, false))
            }
            ExprKind::Foreign(f) => {
                let body: Vec<String> = f.members.iter().map(|m| self.text(m.span).to_string()).collect();
                let open = find_open_brace(self.src, init.span.lo as usize);
                let head = self.src[name_lo as usize..open].trim_end();
                (Kind::Foreign, join_braced(head, &body, false))
            }
            ExprKind::Import(_) => (Kind::Import, self.src[name_lo as usize..b.init.span.hi as usize].to_string()),
            k => {
                let typed_value = b.ty.as_ref().is_some_and(|t| self.text(t.span) != "type");
                let kind = match (b.kind, k) {
                    (BindingKind::Var, _) => Kind::Var,
                    _ if typed_value => Kind::Const,
                    (_, ExprKind::OptionalType(_))
                    | (_, ExprKind::PtrType { .. })
                    | (_, ExprKind::SliceType { .. })
                    | (_, ExprKind::ManyPtrType { .. })
                    | (_, ExprKind::ArrayType { .. })
                    | (_, ExprKind::ErrorUnionType { .. })
                    | (_, ExprKind::FnType(_))
                    | (_, ExprKind::ContType(_)) => Kind::TypeAlias,
                    (_, ExprKind::Ident(_) | ExprKind::Field { .. } | ExprKind::Call { .. })
                        if b.ty.is_none()
                            && (name.chars().next().is_some_and(|c| c.is_uppercase())
                                || PRIMITIVES.contains(&self.text(init.span))) =>
                    {
                        Kind::TypeAlias
                    }
                    _ => Kind::Const,
                };
                let full = self.src[name_lo as usize..b.init.span.hi as usize].to_string();
                let sig = if full.contains('\n') || full.len() > 100 {
                    // A long initializer is an implementation detail; show the declaration.
                    let ty = b.ty.as_ref().map(|t| format!(": {}", self.text(t.span)));
                    match (kind, ty) {
                        (Kind::TypeAlias, _) => format!("{} :: ...", name),
                        (_, Some(ty)) => format!("{}{}", name, ty),
                        (_, None) => format!("{} :: ...", name),
                    }
                } else {
                    full
                };
                (kind, sig)
            }
        };
        out.push(Decl {
            kind,
            name,
            sig: self.with_attrs(item, cond, sig),
            doc: item.doc.clone(),
            member_label: label,
            members,
            location: self.location(item.span.lo),
            handler_target: None,
        });
    }

    /// `head {` + the body lines + `}`, where the head is the source from `name_lo` to the
    /// opening brace of the literal that starts at `lit_lo`.
    fn braced(&self, name_lo: u32, lit_lo: u32, body: &[String], hidden: bool) -> String {
        let open = find_open_brace(self.src, lit_lo as usize);
        let head = self.src[name_lo as usize..open].trim_end();
        join_braced(head, body, hidden)
    }

    fn fields(&self, ms: &[StructMember], body: &mut Vec<String>, docs: &mut Vec<(String, String)>, hidden: &mut bool) {
        for m in ms {
            match m {
                StructMember::Field(f) if f.vis == Visibility::Public => {
                    body.push(self.text(f.span).to_string());
                    let names = f.names.iter().map(|n| self.text(n.span)).collect::<Vec<_>>().join(", ");
                    self.member_doc(docs, &names, f.span.lo);
                }
                StructMember::Field(_) => *hidden = true,
                StructMember::When { then, else_, .. } => {
                    self.fields(then, body, docs, hidden);
                    if let Some(e) = else_ {
                        self.fields(e, body, docs, hidden);
                    }
                }
            }
        }
    }

    fn variants(&self, ms: &[UnionMember], body: &mut Vec<String>, docs: &mut Vec<(String, String)>) {
        for m in ms {
            match m {
                UnionMember::Variant(v) => {
                    body.push(self.text(v.span).to_string());
                    self.member_doc(docs, self.text(v.name.span), v.span.lo);
                }
                UnionMember::When { then, else_, .. } => {
                    self.variants(then, body, docs);
                    if let Some(e) = else_ {
                        self.variants(e, body, docs);
                    }
                }
            }
        }
    }

    /// Records the doc comment above the member that starts at `lo`, if it has one.
    fn member_doc(&self, docs: &mut Vec<(String, String)>, name: &str, lo: u32) {
        let lo = lo as usize;
        let line_start = self.src[..lo].rfind('\n').map_or(0, |i| i + 1);
        if !self.src[line_start..lo].trim().is_empty() {
            return; // Not the first on its line: a comment above belongs to something else.
        }
        let mut lines = Vec::new();
        let mut end = line_start;
        while end > 0 {
            let start = self.src[..end - 1].rfind('\n').map_or(0, |i| i + 1);
            let line = self.src[start..end].trim();
            match line.strip_prefix("///") {
                Some(text) if !text.starts_with('/') => lines.push(text.strip_prefix(' ').unwrap_or(text)),
                _ => break,
            }
            end = start;
        }
        if !lines.is_empty() {
            lines.reverse();
            docs.push((name.to_string(), lines.join("\n")));
        }
    }

    /// The signature preceded by the attributes of the item and the `when` condition that
    /// makes it exist.
    fn with_attrs(&self, item: &Item, cond: Option<&str>, sig: String) -> String {
        let mut out = String::new();
        if let Some(c) = cond {
            out.push_str(&format!("// {}\n", c));
        }
        for a in &item.attrs {
            out.push_str(self.text(a.span));
            out.push('\n');
        }
        out.push_str(&sig);
        out
    }
}

/// `head {`, the lines of `body` indented, and `}`. `hidden` adds a line saying that
/// private members were left out.
fn join_braced(head: &str, body: &[String], hidden: bool) -> String {
    if body.is_empty() && !hidden {
        return format!("{} {{}}", head);
    }
    let mut out = format!("{} {{\n", head);
    for b in body {
        for (i, line) in b.lines().enumerate() {
            // Continuation lines keep their indentation relative to the declaration.
            let line = if i == 0 { line.trim_start() } else { line };
            if line.trim().is_empty() {
                out.push('\n');
            } else {
                out.push_str(&format!("    {}\n", line.trim_end()));
            }
        }
    }
    if hidden {
        out.push_str("    // private fields omitted\n");
    }
    out.push('}');
    out
}

fn strip_attrs(e: &Expr) -> &Expr {
    match &e.kind {
        ExprKind::Attributed { inner, .. } => strip_attrs(inner),
        _ => e,
    }
}

/// Collapses runs of white space (including line breaks) to single spaces.
fn squash(s: &str) -> String {
    s.split_whitespace().collect::<Vec<_>>().join(" ")
}

/// The offset of the `{` that opens the body of the literal starting at `from`, skipping
/// brackets (generic parameters, `where` clauses), strings and comments.
fn find_open_brace(src: &str, from: usize) -> usize {
    let b = src.as_bytes();
    let (mut i, mut depth) = (from, 0i32);
    while i < b.len() {
        match b[i] {
            b'(' | b'[' => depth += 1,
            b')' | b']' => depth -= 1,
            b'{' if depth <= 0 => return i,
            b'"' => {
                i += 1;
                while i < b.len() && b[i] != b'"' {
                    if b[i] == b'\\' {
                        i += 1;
                    }
                    i += 1;
                }
            }
            b'/' if b.get(i + 1) == Some(&b'/') => {
                while i < b.len() && b[i] != b'\n' {
                    i += 1;
                }
            }
            _ => {}
        }
        i += 1;
    }
    from
}
