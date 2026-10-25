//! Building the editor index ([`crate::ide::IdeIndex`]) for a module.
//!
//! The checker runs as it does for `gamma test`; afterwards its tables and typed IR are
//! read once, without changing them, and the module's syntax is walked to resolve the
//! names the typed IR no longer carries. Nothing here runs in ordinary compilation.

mod facts;
mod info;
mod items;
#[cfg(test)]
mod tests;
mod walk;

use super::Sema;
use crate::diag::Diagnostics;
use crate::driver::Session;
use crate::ide::{IdeIndex, TypeInfo};
use crate::source::FileId;
use crate::syntax::ast::NodeIdGen;
use crate::syntax::{lexer, parser};
use crate::types::{Ty, TyKind};
use std::collections::HashMap;
use std::path::Path;
use std::rc::Rc;

/// Checks the module at `root` (a directory, or a single `.gm` file) like `gamma test`
/// does, but keeps going after syntax errors, and returns what editors need to know
/// about it. The diagnostics are left in `sess`.
pub fn analyze_for_ide(sess: &mut Session, root: &Path) -> IdeIndex {
    sess.recover_layout = true;
    let mut sema = Sema::new(sess);
    sema.tests_enabled = true;
    sema.keep_going = true;
    sema.load_prelude();
    let root_mod = sema.load_root(root);
    if let Some(m) = root_mod {
        sema.check_all(m);
        sema.check_inline();
        sema.check_escapes();
        sema.check_dangling_addresses();
        sema.check_continuations();
    }
    build(&mut sema, root, root_mod)
}

fn build(sema: &mut Sema, root: &Path, root_mod: Option<super::ModuleId>) -> IdeIndex {
    let facts = sema.collect_facts();
    let mut idx = IdeIndex::default();
    idx.refs.extend(facts.refs.iter().copied());
    let mut texts: HashMap<String, u32> = HashMap::new();
    for (span, t) in &facts.types {
        let text = t.ty.to_string();
        let next = texts.len() as u32;
        let id = *texts.entry(text).or_insert(next);
        idx.types.insert(*span, TypeInfo { text: id, hover: t.hover });
    }
    let mut type_texts = vec![String::new(); texts.len()];
    for (text, id) in texts {
        type_texts[id as usize] = text;
    }
    idx.type_texts = type_texts;
    idx.universe = Rc::new(sema.universe_items());
    let Some(module) = root_mod else {
        idx.finish();
        return idx;
    };
    // The files of the analyzed module.
    let files: Vec<(FileId, String)> = sema
        .sess
        .sources
        .files()
        .filter(|(_, f)| {
            let p = f.path.as_path();
            p == root || (p.parent() == Some(root) && p.extension().is_some_and(|e| e == "gm"))
        })
        .map(|(id, f)| (id, f.src.clone()))
        .collect();
    let scope_items = Rc::new(sema.module_scope_items(module));
    let mut ids = NodeIdGen::new();
    let mut outlines = HashMap::new();
    {
        let mut w = walk::Walker::new(sema, &facts, &mut idx, module);
        for (id, src) in &files {
            let mut diags = Diagnostics::new();
            let lexed = lexer::lex_recovering(*id, src, &mut diags);
            let ast = parser::parse_file(*id, &lexed, &mut ids, &mut diags);
            let mut out = vec![];
            for item in &ast.items {
                w.item(item, &mut out);
            }
            outlines.insert(*id, out);
        }
    }
    for (id, _) in &files {
        idx.module_items.insert(*id, scope_items.clone());
    }
    idx.outlines = outlines;
    // What `.` offers after each expression whose type has members.
    let mut by_type: HashMap<Ty, Rc<Vec<crate::ide::Item>>> = HashMap::new();
    for (span, t) in &facts.types {
        let mut ty = t.ty;
        while let TyKind::Ptr { pointee, .. } | TyKind::Optional(pointee) = ty.kind() {
            ty = *pointee;
        }
        let members = by_type.entry(ty).or_insert_with(|| Rc::new(sema.value_members(ty)));
        if !members.is_empty() {
            idx.receivers.entry(*span).or_insert_with(|| members.clone());
        }
    }
    for scope in &mut idx.scopes {
        if scope.item.detail.is_empty()
            && let Some((_, ty, _)) = facts.locals.get(&scope.def)
        {
            scope.item.detail = ty.to_string();
        }
    }
    idx.finish();
    idx
}
