//! Packages and imports (§3.1, §3.2): resolving import paths to module
//! directories, loading those modules, and rejecting import cycles.
//!
//! A package is the directory tree under a `gamma.toml` manifest. The manifest
//! format is described in chapter 15 of the language guide (`crate::pkg::manifest`
//! parses it). Path dependencies are read here; git dependencies come resolved from
//! the package manager through the session.

use super::{ModuleId, Sema};
use crate::pkg::manifest::{Manifest, Source};
use crate::source::Span;
use crate::symbol::Symbol;
use std::collections::HashMap;
use std::path::{Component, Path, PathBuf};

/// A package: its root directory and dependencies by name.
#[derive(Debug)]
pub struct Package {
    pub root: PathBuf,
    pub deps: HashMap<String, PathBuf>,
}

/// The directory of the standard library: `$GAMMA_STD` if set; otherwise
/// `lib/gamma/std` next to the directory of the running executable, as in an installed
/// release (`<prefix>/bin/gamma`, `<prefix>/lib/gamma/std`); otherwise the `lib/std`
/// directory of the source tree the compiler was built from.
fn std_root() -> PathBuf {
    if let Some(p) = std::env::var_os("GAMMA_STD") {
        return PathBuf::from(p);
    }
    static INSTALLED: std::sync::OnceLock<Option<PathBuf>> = std::sync::OnceLock::new();
    let installed = INSTALLED.get_or_init(|| {
        let exe = std::env::current_exe().ok()?.canonicalize().ok()?;
        let dir = exe.parent()?.parent()?.join("lib").join("gamma").join("std");
        dir.is_dir().then_some(dir)
    });
    match installed {
        Some(dir) => dir.clone(),
        None => Path::new(env!("CARGO_MANIFEST_DIR")).join("lib").join("std"),
    }
}

impl<'s> Sema<'s> {
    /// The package containing directory `dir`: the nearest enclosing directory
    /// with a `gamma.toml`, or else the package `parent` of the importing module if `dir`
    /// lies inside it, or else `dir` itself (a package without a manifest).
    pub(super) fn package_of(&mut self, dir: &Path, parent: Option<usize>) -> usize {
        let mut root = dir.to_path_buf();
        let mut found = None;
        let mut cur = Some(dir);
        while let Some(d) = cur {
            if d.join("gamma.toml").is_file() {
                found = Some(d.to_path_buf());
                break;
            }
            cur = d.parent();
        }
        if let Some(r) = found {
            root = r;
        } else if let Some(p) = parent
            && dir.starts_with(&self.packages[p].root)
        {
            return p;
        }
        if let Some(i) = self.packages.iter().position(|p| p.root == root) {
            return i;
        }
        let deps = if root.join("gamma.toml").is_file() { self.read_manifest(&root) } else { HashMap::new() };
        self.packages.push(Package { root, deps });
        self.packages.len() - 1
    }

    /// The dependencies of the package in `root`: those the package manager resolved, or
    /// else the path dependencies of its manifest.
    fn read_manifest(&mut self, root: &Path) -> HashMap<String, PathBuf> {
        if let Some(res) = self.sess.packages.clone()
            && let Some(deps) = res.deps_of(root)
        {
            return deps.clone();
        }
        let path = root.join("gamma.toml");
        let text = match std::fs::read_to_string(&path) {
            Ok(t) => t,
            Err(e) => {
                self.error(Span::DUMMY, format!("cannot read {}: {}", path.display(), e));
                return HashMap::new();
            }
        };
        let manifest = match Manifest::parse(&text) {
            Ok(m) => m,
            Err(e) => {
                self.error(Span::DUMMY, format!("{}:{}: {}", path.display(), e.line, e.msg));
                return HashMap::new();
            }
        };
        let mut deps = HashMap::new();
        for dep in manifest.deps {
            match dep.source {
                Source::Path(p) => {
                    deps.insert(dep.name, root.join(p));
                }
                Source::Git(_) | Source::Url(_) => {
                    let kind = if matches!(dep.source, Source::Git(_)) { "git" } else { "url" };
                    let msg = format!("dependency `{}` is a {} dependency; run `gamma fetch`", dep.name, kind);
                    self.error(Span::DUMMY, format!("{}:{}: {}", path.display(), dep.line, msg));
                }
            }
        }
        deps
    }

    /// `import "path"` written in `module`: the imported module (§3.2).
    pub(super) fn import(&mut self, module: ModuleId, path: &str, span: Span) -> Option<ModuleId> {
        let Some(from_dir) = self.modules[module.0 as usize].dir.clone() else {
            self.error(span, "the core library cannot import modules");
            return None;
        };
        let package = self.modules[module.0 as usize].package;
        let relative = path.starts_with("./") || path.starts_with("../");
        let mut name = None;
        let dir = if relative {
            let dir = normalize(&from_dir.join(path));
            let root = package.map(|p| self.packages[p].root.clone());
            let canon = dir
                .canonicalize()
                .unwrap_or_else(|_| normalize(&std::env::current_dir().unwrap_or_default().join(&dir)));
            if root.is_some_and(|r| !canon.starts_with(&r)) {
                self.error(span, format!("the import path `{}` leaves the package (§3.2)", path));
                return None;
            }
            dir
        } else {
            let (first, rest) = path.split_once('/').unwrap_or((path, ""));
            let base = if first == "std" {
                std_root()
            } else {
                match package.and_then(|p| self.packages[p].deps.get(first).cloned()) {
                    Some(r) => r,
                    None => {
                        self.error(
                            span,
                            format!(
                                "unknown package `{}`: a path starts with `./`, `../`, `std/`, or a dependency listed in gamma.toml (§3.2)",
                                first
                            ),
                        );
                        return None;
                    }
                }
            };
            if rest.is_empty() && first != "std" {
                // A package's root module is named after the dependency, not after its
                // directory (a fetched git package lives in a directory named by revision).
                name = Some(first);
            }
            normalize(&if rest.is_empty() { base } else { base.join(rest) })
        };
        let m = self.load_dir_module(&dir, span, if relative { package } else { None }, name)?;
        self.import_edges.push((module, m, span));
        Some(m)
    }

    /// The module of directory `dir`, loading it the first time. `parent` is the package of
    /// the module that imports it by a relative path. The module is named `name`, or else
    /// after its directory.
    pub(super) fn load_dir_module(
        &mut self,
        dir: &Path,
        span: Span,
        parent: Option<usize>,
        name: Option<&str>,
    ) -> Option<ModuleId> {
        let key = dir.canonicalize().unwrap_or_else(|_| dir.to_path_buf());
        if let Some(&m) = self.module_by_dir.get(&key) {
            return Some(m);
        }
        let mut paths: Vec<PathBuf> = match std::fs::read_dir(dir) {
            Ok(rd) => rd
                .flatten()
                .map(|e| e.path())
                .filter(|p| p.is_file() && p.extension().is_some_and(|e| e == "gm"))
                .collect(),
            Err(_) => {
                self.error(span, format!("cannot find the module directory {}", dir.display()));
                return None;
            }
        };
        paths.sort();
        if paths.is_empty() {
            self.error(span, format!("the module directory {} has no `.gm` files", dir.display()));
            return None;
        }
        let mut files = vec![];
        for p in &paths {
            let f = self.sess.load(p)?;
            files.push(self.sess.parse(f));
        }
        let name = match name {
            Some(n) => n.to_string(),
            None => key.file_name().map_or("module".to_string(), |s| s.to_string_lossy().into_owned()),
        };
        let package = self.package_of(&key, parent);
        let m = self.add_module(Symbol::intern(&name), files, false);
        self.modules[m.0 as usize].dir = Some(dir.to_path_buf());
        // Standard library modules may use the system intrinsics, like the core library.
        let std = std_root().canonicalize().unwrap_or_else(|_| std_root());
        self.modules[m.0 as usize].is_std = key.starts_with(&std);
        self.modules[m.0 as usize].package = Some(package);
        self.module_by_dir.insert(key, m);
        // Its top-level `when`s are processed now that its directory is known.
        self.collect_whens(m);
        Some(m)
    }

    /// Reports each cycle in the import graph (§3.2).
    pub(super) fn check_import_cycles(&mut self) {
        let n = self.modules.len();
        let mut state = vec![0u8; n]; // 0 new, 1 on the stack, 2 done
        let mut stack: Vec<ModuleId> = vec![];
        let edges = self.import_edges.clone();
        for start in 0..n {
            if state[start] == 0 {
                self.visit(ModuleId(start as u32), &edges, &mut state, &mut stack);
            }
        }
    }

    fn visit(
        &mut self,
        m: ModuleId,
        edges: &[(ModuleId, ModuleId, Span)],
        state: &mut [u8],
        stack: &mut Vec<ModuleId>,
    ) {
        state[m.0 as usize] = 1;
        stack.push(m);
        for &(from, to, span) in edges.iter().filter(|e| e.0 == m) {
            match state[to.0 as usize] {
                0 => self.visit(to, edges, state, stack),
                1 => {
                    let i = stack.iter().position(|x| *x == to).unwrap_or(0);
                    let names: Vec<String> =
                        stack[i..].iter().chain([&to]).map(|x| self.modules[x.0 as usize].name.to_string()).collect();
                    let _ = from;
                    self.error(span, format!("import cycle: {}", names.join(" -> ")));
                }
                _ => {}
            }
        }
        stack.pop();
        state[m.0 as usize] = 2;
    }
}

/// Removes `.` and `..` components without touching the file system.
fn normalize(p: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for c in p.components() {
        match c {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            other => out.push(other.as_os_str()),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalizes_paths() {
        assert_eq!(normalize(Path::new("/a/b/./../c")), PathBuf::from("/a/c"));
    }
}
