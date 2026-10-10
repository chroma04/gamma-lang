//! Loading modules and collecting their top-level declarations (§3.1, §3.4).

use super::{Decl, DeclId, DeclState, Module, ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ItemKind};
use std::collections::HashMap;
use std::path::Path;
use std::rc::Rc;

const PRELUDE_SRC: &str = include_str!("../../lib/core/prelude.gm");

/// The other core modules (§21.5): name, source, and the names re-exported to
/// the universe scope. Their remaining functions are reached as methods.
const CORE_MODULES: &[(&str, &str, &[&str])] = &[
    ("list", include_str!("../../lib/core/list.gm"), &["List", "list"]),
    ("map", include_str!("../../lib/core/map.gm"), &["HashMap", "map", "map_with"]),
    ("slice", include_str!("../../lib/core/slice.gm"), &[]),
];

impl<'s> Sema<'s> {
    pub(super) fn load_prelude(&mut self) {
        let file = self.sess.sources.add("<core>/prelude.gm", PRELUDE_SRC.to_string());
        let ast = self.sess.parse(file);
        let id = self.add_module(Symbol::intern("core"), vec![ast], true);
        self.collect_whens(id);
        self.prelude = Some(id);
        self.load_target_module(id);
        for (name, src, exports) in CORE_MODULES {
            let file = self.sess.sources.add(format!("<core>/{}.gm", name), src.to_string());
            let ast = self.sess.parse(file);
            let m = self.add_module(Symbol::intern(&format!("core/{}", name)), vec![ast], true);
            self.core_modules.push(m);
            for e in exports.iter() {
                let e = Symbol::intern(e);
                let d = self.modules[m.0 as usize].scope.get(&e).copied();
                match d {
                    Some(d) => {
                        self.modules[id.0 as usize].scope.insert(e, d);
                    }
                    None => self.error(Span::DUMMY, format!("core module `{}` does not declare `{}`", name, e)),
                }
            }
        }
        // Resolve the ambient effects eagerly so their ids are known.
        if let Some(&d) = self.modules[id.0 as usize].scope.get(&Symbol::intern("Yield"))
            && let super::DeclValue::Effect(e) = self.resolve_decl(d)
        {
            self.yield_effect = Some(e);
        }
        for (name, slot) in [
            ("Out", 0),
            ("Err", 1),
            ("Env", 2),
            ("Alloc", 3),
            ("Sys", 4),
            ("In", 5),
            ("Clock", 6),
            ("Rand", 7),
            ("Fs", 8),
            ("Net", 9),
        ] {
            if let Some(&d) = self.modules[id.0 as usize].scope.get(&Symbol::intern(name))
                && let super::DeclValue::Effect(e) = self.resolve_decl(d)
            {
                let a = &mut self.prog.ambient;
                *match slot {
                    0 => &mut a.out,
                    1 => &mut a.err,
                    2 => &mut a.env,
                    3 => &mut a.alloc,
                    4 => &mut a.sys,
                    5 => &mut a.input,
                    6 => &mut a.clock,
                    7 => &mut a.rand,
                    8 => &mut a.fs,
                    _ => &mut a.net,
                } = Some(e);
            }
        }
    }

    /// Loads the root module: a single `.gm` file, or every `.gm` file directly
    /// inside a directory (§3.1).
    pub(super) fn load_root(&mut self, root: &Path) -> Option<ModuleId> {
        if root.is_dir() {
            let m = self.load_dir_module(root, Span::DUMMY)?;
            return (!self.sess.diags.has_errors()).then_some(m);
        }
        let file = self.sess.load(root)?;
        let ast = self.sess.parse(file);
        if self.sess.diags.has_errors() {
            return None;
        }
        let dir = root.parent().map(Path::to_path_buf).unwrap_or_default();
        let canon = dir.canonicalize().unwrap_or_else(|_| dir.clone());
        let package = self.package_of(&canon);
        let name = root.file_stem().map_or("main".to_string(), |s| s.to_string_lossy().into_owned());
        let m = self.add_module(Symbol::intern(&name), vec![ast], false);
        self.modules[m.0 as usize].dir = Some(dir);
        self.modules[m.0 as usize].package = Some(package);
        self.collect_whens(m);
        Some(m)
    }

    /// Creates a module from parsed files and collects its top-level names. Its
    /// top-level `when`s wait for [`Sema::collect_whens`].
    pub(super) fn add_module(&mut self, name: Symbol, files: Vec<ast::File>, is_prelude: bool) -> ModuleId {
        let id = ModuleId(self.modules.len() as u32);
        self.modules.push(Module {
            name,
            scope: HashMap::new(),
            is_prelude,
            is_std: false,
            dir: None,
            package: None,
            pending_whens: vec![],
        });
        let mut scope: HashMap<Symbol, DeclId> = HashMap::new();
        let mut whens = vec![];
        for file in files {
            for item in file.items {
                match &item.kind {
                    ItemKind::When(_) => whens.push(item),
                    _ => self.collect_item(id, Rc::new(item), &mut scope),
                }
            }
        }
        self.modules[id.0 as usize].scope = scope;
        self.modules[id.0 as usize].pending_whens = whens;
        id
    }

    /// Declares the items of the taken branch of each top-level `when` of
    /// `module`, as if written at top level (§3.6). A condition may use the
    /// module's other names.
    pub(super) fn collect_whens(&mut self, id: ModuleId) {
        let mut whens: std::collections::VecDeque<ast::Item> =
            std::mem::take(&mut self.modules[id.0 as usize].pending_whens).into();
        while let Some(item) = whens.pop_front() {
            let ItemKind::When(w) = &item.kind else { unreachable!() };
            let branch = match self.when_cond(id, &w.cond) {
                Some(true) => w.then.clone(),
                Some(false) => w.else_.clone().unwrap_or_default(),
                None => vec![],
            };
            let mut scope = std::mem::take(&mut self.modules[id.0 as usize].scope);
            for it in branch {
                match &it.kind {
                    ItemKind::When(_) => whens.push_back(it),
                    _ => self.collect_item(id, Rc::new(it), &mut scope),
                }
            }
            self.modules[id.0 as usize].scope = scope;
        }
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
            ItemKind::When(_) => unreachable!("collected by `add_module`"),
            ItemKind::Handler(_) => self.handler_items.push((module, item.clone())),
            // Tests are compiled only for `gamma test` (§3.8).
            ItemKind::Test { .. } => {
                if self.tests_enabled {
                    self.test_items.push((module, item.clone()));
                }
            }
            ItemKind::Intrinsic(_) => self.static_asserts.push((module, item.clone())),
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
    /// prelude's public declarations (all of them, for the other core modules).
    pub fn lookup_module_name(&mut self, module: ModuleId, name: Symbol) -> Option<DeclId> {
        if let Some(&d) = self.modules[module.0 as usize].scope.get(&name) {
            return Some(d);
        }
        let prelude = self.prelude?;
        let &d = self.modules[prelude.0 as usize].scope.get(&name)?;
        let core = self.modules[module.0 as usize].is_prelude;
        if core || self.decls[d.0 as usize].item.is_pub { Some(d) } else { None }
    }

    /// The module that a module-binding name (`m` in `m.T`) denotes in `module`.
    /// Function `name` of core module `core/<module>`.
    pub fn core_fn(&mut self, module: &str, name: &str) -> Option<crate::tir::FnId> {
        let mname = Symbol::intern(&format!("core/{}", module));
        let m = *self.core_modules.iter().find(|m| self.modules[m.0 as usize].name == mname)?;
        let d = *self.modules[m.0 as usize].scope.get(&Symbol::intern(name))?;
        match self.resolve_decl(d) {
            super::DeclValue::Fn(f) => Some(f),
            _ => None,
        }
    }

    pub fn module_named(&mut self, module: ModuleId, e: &ast::Expr) -> Option<ModuleId> {
        if let ast::ExprKind::Field { base, name } = &e.kind {
            let m = self.module_named(module, base)?;
            return self.public_module_member(m, name.name);
        }
        let ast::ExprKind::Ident(id) = &e.kind else { return None };
        let d = self.lookup_module_name(module, id.name)?;
        match self.resolve_decl(d) {
            super::DeclValue::Module(m) => Some(m),
            _ => None,
        }
    }

    /// The module that the public declaration `name` of module `m` binds, if it is a
    /// module binding (reports nothing otherwise).
    pub fn public_module_member(&mut self, m: ModuleId, name: Symbol) -> Option<ModuleId> {
        let d = *self.modules[m.0 as usize].scope.get(&name)?;
        if !self.decls[d.0 as usize].item.is_pub {
            return None;
        }
        match self.resolve_decl(d) {
            super::DeclValue::Module(m) => Some(m),
            _ => None,
        }
    }

    /// The public declaration `name` of module `m`; reports a missing or private one.
    pub fn module_member(&mut self, m: ModuleId, name: ast::Ident) -> Option<DeclId> {
        let module_name = self.modules[m.0 as usize].name;
        match self.modules[m.0 as usize].scope.get(&name.name).copied() {
            Some(d) if self.decls[d.0 as usize].item.is_pub => {
                self.check_deprecated(d, name.span);
                Some(d)
            }
            Some(_) => {
                self.error(name.span, format!("`{}` is private to the module `{}` (§3.3)", name.name, module_name));
                None
            }
            None => {
                self.error(name.span, format!("the module `{}` has no declaration `{}`", module_name, name.name));
                None
            }
        }
    }

    /// The predeclared module `target` (§3.6): compile-time constants describing
    /// the compilation target, generated from the session's target.
    fn load_target_module(&mut self, prelude: ModuleId) {
        let t = &self.target;
        let src = format!(
            "pub Os :: union {{ Linux, MacOS, Windows, FreeBSD, Freestanding }}\n\
             pub Arch :: union {{ X86_64, Aarch64, Riscv64, Wasm32, Other }}\n\
             pub Endian :: union {{ Little, Big }}\n\
             pub Mode :: union {{ Debug, Checked, Fast }}\n\
             pub os :: Os.{}\n\
             pub arch :: Arch.{}\n\
             pub endian :: Endian.{}\n\
             pub ptr_bits :: {}\n\
             pub mode :: Mode.{}\n",
            t.os,
            t.arch,
            if t.little_endian { "Little" } else { "Big" },
            t.ptr_bits(),
            self.build_mode,
        );
        let file = self.sess.sources.add("<core>/target.gm", src);
        let ast = self.sess.parse(file);
        let m = self.add_module(Symbol::intern("target"), vec![ast], true);
        self.core_modules.push(m);
        let name = ast::Ident { name: Symbol::intern("target"), span: Span::DUMMY };
        let item = Rc::new(ast::Item {
            id: ast::NodeId(u32::MAX),
            span: Span::DUMMY,
            doc: None,
            attrs: vec![],
            is_pub: true,
            kind: ItemKind::Intrinsic(ast::Expr {
                id: ast::NodeId(u32::MAX),
                kind: ast::ExprKind::Underscore,
                span: Span::DUMMY,
            }),
        });
        let id = DeclId(self.decls.len() as u32);
        self.decls.push(Decl {
            name,
            module: prelude,
            item,
            state: DeclState::Done(super::DeclValue::Module(m)),
            op_of: None,
        });
        self.modules[prelude.0 as usize].scope.insert(name.name, id);
    }
}
