//! Requests about all uses of a declaration, across the modules of a package: find
//! references and rename.

use super::analysis::{Analysis, canon, range_json};
use super::position::{Encoding, LineIndex};
use super::workspace::{Found, Loc, Workspace, package_roots};
use crate::source::{FileId, Span};
use crate::syntax::token::Keyword;
use serde_json::{Value, json};
use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};

/// What a request needs: the analysis of the module with the document, the cache of the
/// other modules, and the open buffers.
pub struct Request<'a> {
    pub a: &'a Analysis,
    pub ws: &'a mut Workspace,
    pub dir: &'a Path,
    pub overlays: &'a HashMap<PathBuf, String>,
    pub recycle: bool,
    pub enc: Encoding,
    pub uri_of: &'a dyn Fn(&Path) -> String,
}

/// The declaration a request is about.
struct Target {
    span: Span,
    loc: Loc,
    name: String,
    /// A local or parameter: its uses are in its own module.
    local: bool,
}

impl Request<'_> {
    fn is_local(&self, def: Span) -> bool {
        self.a.index.scopes.iter().any(|s| s.def == def)
    }

    /// The declaration that the name at `off` refers to, if it is in a file that can be
    /// edited.
    fn declaration_at(&self, file: FileId, off: u32) -> Result<Target, String> {
        let span = self.a.index.definition_at(file, off).ok_or("there is nothing to rename here")?;
        if span.hi <= span.lo {
            return Err("this name has no declaration in the source that can be renamed".into());
        }
        let name = self.a.text(span.file).get(span.lo as usize..span.hi as usize).unwrap_or_default().to_string();
        let Some(path) = self.a.real_path(span.file) else {
            return Err(format!("cannot rename `{}`: it is part of the core library", name));
        };
        let loc = Loc { path, lo: span.lo, hi: span.hi };
        Ok(Target { span, loc, name, local: self.is_local(span) })
    }

    /// Refuses declarations outside the package being edited.
    fn check_editable(&self, t: &Target) -> Result<(), String> {
        if t.loc.path.starts_with(canon(&crate::sema::std_root())) {
            return Err(format!("cannot rename `{}`: it is part of the standard library", t.name));
        }
        let roots = package_roots(self.dir);
        if !t.loc.path.starts_with(&roots[0]) {
            return Err(format!("cannot rename `{}`: it is declared in a dependency, outside this package", t.name));
        }
        Ok(())
    }

    /// The uses of the declaration in this module and, unless it is a local, in the other
    /// modules of the package and of its path dependencies.
    fn find(&mut self, t: &Target) -> Found {
        let mut modules = vec![self.ws.store(self.dir, self.a, self.overlays)];
        if !t.local {
            let roots = package_roots(self.dir);
            if roots.iter().any(|r| t.loc.path.starts_with(r)) {
                for cand in Workspace::module_dirs(&roots, self.overlays) {
                    if cand != self.dir && Workspace::mentions(&cand, &t.name, self.overlays) {
                        modules.push(self.ws.facts(&cand, self.overlays, self.recycle));
                    }
                }
            }
        }
        let mut uses: Vec<Loc> = modules
            .iter()
            .flat_map(|m| m.refs.iter().filter(|(_, to)| *to == t.loc).map(|(from, _)| from.clone()))
            .collect();
        uses.sort();
        uses.dedup();
        Found { uses, modules }
    }

    /// The text of a file: from the open documents, an analysis, or the disk.
    fn text_of(&self, found: &Found, path: &Path) -> Option<String> {
        if let Some(t) = found.text(path) {
            return Some(t.to_string());
        }
        if let Some(t) = self.overlays.get(path) {
            return Some(t.clone());
        }
        std::fs::read_to_string(path).ok()
    }

    fn location(&self, found: &Found, loc: &Loc) -> Option<Value> {
        let text = self.text_of(found, &loc.path)?;
        let ix = LineIndex::new(&text);
        Some(
            json!({"uri": (self.uri_of)(&loc.path), "range": range_json(&ix, loc.lo as usize, loc.hi as usize, self.enc)}),
        )
    }
}

/// All references to the declaration of the name at `off`, in every module of the package
/// that uses it.
pub fn references(rq: &mut Request, file: FileId, off: u32, include_declaration: bool) -> Value {
    let Ok(t) = rq.declaration_at(file, off) else {
        // A declaration in the embedded core library, or none: the uses in this module.
        return super::features::references(
            &super::features::Ctx { a: rq.a, enc: rq.enc, uri_of: rq.uri_of },
            file,
            off,
            include_declaration,
        );
    };
    let found = rq.find(&t);
    let mut locs = vec![];
    if include_declaration {
        locs.push(t.loc.clone());
    }
    locs.extend(found.uses.iter().cloned());
    Value::Array(locs.iter().filter_map(|l| rq.location(&found, l)).collect())
}

/// The range of `name` in `text[lo..hi]` (the whole of it, or the last whole word, as in
/// the span of `a.name`).
fn narrow(text: &str, lo: u32, hi: u32, name: &str) -> Option<(u32, u32)> {
    let slice = text.get(lo as usize..hi as usize)?;
    if slice == name {
        return Some((lo, hi));
    }
    let is_word = |c: char| c.is_alphanumeric() || c == '_';
    let (mut best, mut from) = (None, 0);
    while let Some(i) = slice[from..].find(name) {
        let at = from + i;
        let end = at + name.len();
        let before_ok = !slice[..at].chars().next_back().is_some_and(is_word);
        let after_ok = !slice[end..].chars().next().is_some_and(is_word);
        if before_ok && after_ok {
            best = Some(at);
        }
        from = at + 1;
    }
    best.map(|at| (lo + at as u32, lo + (at + name.len()) as u32))
}

/// The identifier at `off` that a rename would start from: the use or declaration of the
/// name there.
pub fn prepare_rename(rq: &mut Request, file: FileId, off: u32) -> Result<Value, String> {
    let t = rq.declaration_at(file, off)?;
    rq.check_editable(&t)?;
    let text = rq.a.text(file);
    let at = |s: Span| s.file == file && s.lo <= off && off <= s.hi;
    let span =
        rq.a.index
            .refs
            .iter()
            .filter(|r| at(r.from) && r.to == t.span)
            .map(|r| r.from)
            .chain(at(t.span).then_some(t.span))
            .min_by_key(|s| s.hi - s.lo)
            .ok_or("there is nothing to rename here")?;
    let (lo, hi) = narrow(text, span.lo, span.hi, &t.name).ok_or("this use cannot be renamed on its own")?;
    let ix = LineIndex::new(text);
    Ok(json!({"range": range_json(&ix, lo as usize, hi as usize, rq.enc), "placeholder": t.name}))
}

/// Whether `name` can be the name of a declaration.
pub fn check_new_name(name: &str) -> Result<(), String> {
    if !crate::pkg::manifest::is_identifier(name) {
        return Err(format!("`{}` is not an identifier", name));
    }
    if name == "_" {
        return Err("`_` cannot be the name of a declaration".into());
    }
    if Keyword::from_str(name).is_some() {
        return Err(format!("`{}` is a keyword", name));
    }
    if crate::sema::is_reserved_type_name(name) {
        return Err(format!("`{}` is the name of a builtin type", name));
    }
    Ok(())
}

fn line_of(text: &str, off: u32) -> usize {
    text.get(..off as usize).map_or(0, |t| t.matches('\n').count()) + 1
}

/// The edits that rename the declaration at `off` and all its uses to `new_name`.
pub fn rename(rq: &mut Request, file: FileId, off: u32, new_name: &str) -> Result<Value, String> {
    check_new_name(new_name)?;
    let t = rq.declaration_at(file, off)?;
    rq.check_editable(&t)?;
    if t.name == new_name {
        return Ok(json!({"changes": {}}));
    }
    let found = rq.find(&t);
    check_clashes(rq, &t, &found, new_name)?;
    let mut by_file: BTreeMap<PathBuf, Vec<(u32, u32)>> = BTreeMap::new();
    let mut texts: HashMap<PathBuf, String> = HashMap::new();
    for loc in std::iter::once(&t.loc).chain(found.uses.iter()) {
        if !texts.contains_key(&loc.path) {
            let text = rq.text_of(&found, &loc.path).ok_or_else(|| format!("cannot read {}", loc.path.display()))?;
            texts.insert(loc.path.clone(), text);
        }
        let text = &texts[&loc.path];
        let (lo, hi) = narrow(text, loc.lo, loc.hi, &t.name).ok_or_else(|| {
            format!(
                "cannot rename `{}`: a use at {}:{} is not a plain name",
                t.name,
                loc.path.display(),
                line_of(text, loc.lo)
            )
        })?;
        by_file.entry(loc.path.clone()).or_default().push((lo, hi));
    }
    let mut changes = serde_json::Map::new();
    for (path, mut spans) in by_file {
        spans.sort();
        spans.dedup();
        let ix = LineIndex::new(&texts[&path]);
        let edits: Vec<Value> = spans
            .iter()
            .map(|&(lo, hi)| json!({"range": range_json(&ix, lo as usize, hi as usize, rq.enc), "newText": new_name}))
            .collect();
        changes.insert((rq.uri_of)(&path), Value::Array(edits));
    }
    Ok(json!({"changes": changes}))
}

/// Refuses a new name that is already taken where the declaration or one of its uses is.
fn check_clashes(rq: &mut Request, t: &Target, found: &Found, new_name: &str) -> Result<(), String> {
    let old = &t.name;
    let home = t.loc.path.parent().map(Path::to_path_buf).unwrap_or_default();
    let module = match found.modules.iter().find(|m| m.dir == home) {
        Some(m) => m.clone(),
        None => rq.ws.facts(&home, rq.overlays, rq.recycle),
    };
    if module.siblings.get(&t.loc).is_some_and(|names| names.iter().any(|n| n == new_name)) {
        return Err(format!(
            "cannot rename `{}` to `{}`: `{}` is already declared in the same scope",
            old, new_name, new_name
        ));
    }
    if let Some(me) = module.locals.iter().find(|l| l.def == t.loc) {
        if module.top_level.contains(new_name) {
            return Err(format!(
                "cannot rename `{}` to `{}`: it would hide the declaration `{}` of the module",
                old, new_name, new_name
            ));
        }
        let clash = module.locals.iter().any(|l| {
            l.label == new_name && l.def != me.def && l.def.path == me.def.path && l.from <= me.to && me.from <= l.to
        });
        if clash {
            return Err(format!(
                "cannot rename `{}` to `{}`: `{}` is already declared in a scope that overlaps",
                old, new_name, new_name
            ));
        }
    }
    for u in &found.uses {
        for m in &found.modules {
            let hit = m.locals.iter().find(|l| {
                l.label == new_name && l.def.path == u.path && l.def != t.loc && l.from <= u.lo && u.lo <= l.to
            });
            if hit.is_some() {
                let line = m.texts.get(&u.path).map_or(0, |x| line_of(x, u.lo));
                return Err(format!(
                    "cannot rename `{}` to `{}`: `{}` is a local name where `{}` is used ({}:{})",
                    old,
                    new_name,
                    new_name,
                    old,
                    u.path.display(),
                    line
                ));
            }
        }
    }
    Ok(())
}
