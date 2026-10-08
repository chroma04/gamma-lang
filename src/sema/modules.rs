//! Loading modules and collecting their top-level declarations (§3.1, §3.4).

use super::{Decl, DeclId, DeclState, Module, ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ItemKind};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::rc::Rc;

const PRELUDE_SRC: &str = include_str!("../../lib/core/prelude.gm");

impl<'s> Sema<'s> {
    pub(super) fn load_prelude(&mut self) {
        let file = self.sess.sources.add("<core>/prelude.gm", PRELUDE_SRC.to_string());
        let ast = self.sess.parse(file);
        let id = self.add_module(Symbol::intern("core"), vec![ast], true);
        self.prelude = Some(id);
        // Resolve the ambient effects eagerly so their ids are known.
        for (name, slot) in [("Out", 0), ("Err", 1)] {
            if let Some(&d) = self.modules[id.0 as usize].scope.get(&Symbol::intern(name))
                && let super::DeclValue::Effect(e) = self.resolve_decl(d)
            {
                match slot {
                    0 => self.prog.ambient.out = Some(e),
                    _ => self.prog.ambient.err = Some(e),
                }
            }
        }
    }

    /// Loads the root module: a single `.gm` file, or every `.gm` file directly
    /// inside a directory.
    pub(super) fn load_root(&mut self, root: &Path) -> Option<ModuleId> {
        let paths: Vec<PathBuf> = if root.is_dir() {
            let mut v: Vec<PathBuf> = match std::fs::read_dir(root) {
                Ok(rd) => rd
                    .flatten()
                    .map(|e| e.path())
                    .filter(|p| p.is_file() && p.extension().is_some_and(|e| e == "gm"))
                    .collect(),
                Err(e) => {
                    self.error(Span::DUMMY, format!("cannot read directory {}: {}", root.display(), e));
                    return None;
                }
            };
            v.sort();
            if v.is_empty() {
                self.error(Span::DUMMY, format!("no `.gm` files in {}", root.display()));
                return None;
            }
            v
        } else {
            vec![root.to_path_buf()]
        };
        let mut files = vec![];
        for p in &paths {
            let file = self.sess.load(p)?;
            files.push(self.sess.parse(file));
        }
        if self.sess.diags.has_errors() {
            return None;
        }
        let name = root.file_stem().map_or("main".to_string(), |s| s.to_string_lossy().into_owned());
        Some(self.add_module(Symbol::intern(&name), files, false))
    }

    fn add_module(&mut self, name: Symbol, files: Vec<ast::File>, is_prelude: bool) -> ModuleId {
        let id = ModuleId(self.modules.len() as u32);
        self.modules.push(Module { name, scope: HashMap::new(), is_prelude });
        let mut scope: HashMap<Symbol, DeclId> = HashMap::new();
        for file in files {
            for item in file.items {
                self.collect_item(id, Rc::new(item), &mut scope);
            }
        }
        self.modules[id.0 as usize].scope = scope;
        id
    }

    fn collect_item(&mut self, module: ModuleId, item: Rc<ast::Item>, scope: &mut HashMap<Symbol, DeclId>) {
        match &item.kind {
            ItemKind::Binding(b) => {
                if b.names.len() != 1 {
                    self.error(b.span, "destructuring is not allowed at top level; bind each name separately");
                    return;
                }
                let name = b.names[0];
                if name.name.as_str() == "_" {
                    self.error(name.span, "`_` cannot be declared at top level");
                    return;
                }
                if super::builtins::is_reserved_type_name(name.name.as_str()) {
                    self.error(name.span, format!("`{}` is a reserved type name and cannot be redeclared", name.name));
                    return;
                }
                if self.check_duplicate(scope, name) {
                    return;
                }
                let id = DeclId(self.decls.len() as u32);
                self.decls.push(Decl { name, module, item: item.clone(), state: DeclState::Unresolved, op_of: None });
                scope.insert(name.name, id);
                // An effect's operations are also top-level functions of the module (§12.1).
                if let ast::ExprKind::Effect(lit) = &b.init.kind {
                    for (i, op) in lit.ops.iter().enumerate() {
                        let op_decl = Decl {
                            name: op.name,
                            module,
                            item: item.clone(),
                            state: DeclState::Unresolved,
                            op_of: Some((id, i as u32)),
                        };
                        self.declare_name(scope, op_decl);
                    }
                }
            }
            ItemKind::When(w) => self.error(w.span, "top-level `when` is not supported yet"),
            ItemKind::Handler(h) => self.error(h.span, "handler declarations are not supported yet"),
            ItemKind::Test { name_span, .. } => self.error(*name_span, "tests are not supported yet"),
            ItemKind::Intrinsic(e) => self.error(e.span, "top-level intrinsic calls are not supported yet"),
        }
    }

    /// Reports a duplicate top-level name; returns true if `name` is already declared.
    fn check_duplicate(&mut self, scope: &HashMap<Symbol, DeclId>, name: ast::Ident) -> bool {
        let Some(&prev) = scope.get(&name.name) else { return false };
        let prev_span = self.decls[prev.0 as usize].name.span;
        let d = crate::diag::Diagnostic::error(name.span, format!("`{}` is declared twice", name.name))
            .with_label(prev_span, "first declared here");
        self.emit(d);
        true
    }

    fn declare_name(&mut self, scope: &mut HashMap<Symbol, DeclId>, decl: Decl) {
        if self.check_duplicate(scope, decl.name) {
            return;
        }
        let id = DeclId(self.decls.len() as u32);
        scope.insert(decl.name.name, id);
        self.decls.push(decl);
    }

    /// Looks up a name at module level: the module's own declarations, then the
    /// prelude's public declarations.
    pub fn lookup_module_name(&mut self, module: ModuleId, name: Symbol) -> Option<DeclId> {
        if let Some(&d) = self.modules[module.0 as usize].scope.get(&name) {
            return Some(d);
        }
        let prelude = self.prelude?;
        let &d = self.modules[prelude.0 as usize].scope.get(&name)?;
        if self.decls[d.0 as usize].item.is_pub { Some(d) } else { None }
    }
}
