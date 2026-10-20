//! Facts about the other modules of a package, for requests that cross module
//! boundaries (find references, rename).
//!
//! An analysis covers one module directory. To find every use of a declaration, the
//! modules of the package (and of its path dependencies) that mention the name are
//! analyzed too, and what is kept of each analysis is a [`ModuleFacts`]: plain data
//! keyed by file path. A module's facts are reused while the files its analysis read
//! (and the lists of files of their directories) are unchanged.

use super::analysis::{Analysis, canon};
use crate::driver::module_files;
use crate::ide::DocSymbol;
use crate::pkg::manifest::{Manifest, Source};
use crate::source::Span;
use std::collections::hash_map::DefaultHasher;
use std::collections::{HashMap, HashSet};
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::time::{Duration, SystemTime};

/// A place in a file, by path and byte offsets.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct Loc {
    pub path: PathBuf,
    pub lo: u32,
    pub hi: u32,
}

/// A local name and the offsets where it is visible.
pub struct LocalName {
    pub label: String,
    pub def: Loc,
    pub from: u32,
    pub to: u32,
}

/// What requests need to know of one analyzed module.
pub struct ModuleFacts {
    pub dir: PathBuf,
    /// Uses (in the module's own files) of declarations anywhere.
    pub refs: Vec<(Loc, Loc)>,
    /// The text of each of the module's files.
    pub texts: HashMap<PathBuf, Rc<String>>,
    pub locals: Vec<LocalName>,
    /// For each declared name (a top-level declaration, field, variant, …), the names
    /// declared in the same place.
    pub siblings: HashMap<Loc, Rc<Vec<String>>>,
    /// The names declared at the top level of the module.
    pub top_level: HashSet<String>,
}

fn loc_of(a: &Analysis, span: Span) -> Option<Loc> {
    Some(Loc { path: a.real_path(span.file)?, lo: span.lo, hi: span.hi })
}

impl ModuleFacts {
    /// What is kept of the analysis of module directory `dir`.
    pub fn from_analysis(a: &Analysis, dir: &Path) -> ModuleFacts {
        let in_dir = |p: &Path| p.parent() == Some(dir);
        let mut facts = ModuleFacts {
            dir: dir.to_path_buf(),
            refs: vec![],
            texts: HashMap::new(),
            locals: vec![],
            siblings: HashMap::new(),
            top_level: HashSet::new(),
        };
        for (path, &file) in &a.files {
            if in_dir(path) {
                facts.texts.insert(path.clone(), Rc::new(a.text(file).to_string()));
            }
        }
        for r in &a.index.refs {
            let (Some(from), Some(to)) = (loc_of(a, r.from), loc_of(a, r.to)) else { continue };
            if in_dir(&from.path) {
                facts.refs.push((from, to));
            }
        }
        for s in &a.index.scopes {
            if let Some(def) = loc_of(a, s.def)
                && in_dir(&def.path)
            {
                facts.locals.push(LocalName { label: s.item.label.clone(), def, from: s.from, to: s.to });
            }
        }
        let mut top_names = vec![];
        for syms in a.index.outlines.values() {
            top_names.extend(syms.iter().map(|s| s.name.clone()));
        }
        top_names.sort();
        top_names.dedup();
        facts.top_level = top_names.iter().cloned().collect();
        let top = Rc::new(top_names);
        let mut files: Vec<_> = a.index.outlines.iter().collect();
        files.sort_by_key(|(f, _)| f.0);
        for (file, syms) in files {
            if let Some(path) = a.real_path(*file) {
                facts.add_siblings(&path, syms, &top);
            }
        }
        facts
    }

    fn add_siblings(&mut self, path: &Path, syms: &[DocSymbol], group: &Rc<Vec<String>>) {
        for s in syms {
            let loc = Loc { path: path.to_path_buf(), lo: s.name_span.lo, hi: s.name_span.hi };
            self.siblings.insert(loc, group.clone());
            let mut names: Vec<String> = s.children.iter().map(|c| c.name.clone()).collect();
            names.sort();
            names.dedup();
            self.add_siblings(path, &s.children, &Rc::new(names));
        }
    }
}

/// How long before it was read a file must have been last modified for its cached text
/// to be trusted by (mtime, size) alone: a later write within the resolution of the
/// file system's clock could leave both unchanged.
const SETTLED: Duration = Duration::from_secs(3);

/// What was read of one file on disk.
struct Cached {
    mtime: Option<SystemTime>,
    size: u64,
    /// Whether (mtime, size) are enough to tell that the file is unchanged.
    settled: bool,
    hash: u64,
    /// The text, unless the file is not valid UTF-8.
    text: Option<Rc<String>>,
}

/// The files read from disk, by path, read again only when they appear to have changed.
#[derive(Default)]
struct FileCache {
    files: HashMap<PathBuf, Cached>,
}

/// The hash of a file that cannot be read.
const UNREADABLE: u64 = 0;

impl FileCache {
    /// A hash of the file's contents and its text; the text is read only if the file's
    /// modification time or size differ from when it was last read.
    fn load(&mut self, path: &Path) -> (u64, Option<Rc<String>>) {
        let Ok(meta) = std::fs::metadata(path) else {
            self.files.remove(path);
            return (UNREADABLE, None);
        };
        let (mtime, size) = (meta.modified().ok(), meta.len());
        if let Some(c) = self.files.get(path)
            && c.settled
            && c.mtime == mtime
            && c.size == size
        {
            return (c.hash, c.text.clone());
        }
        let now = SystemTime::now();
        let Ok(bytes) = std::fs::read(path) else {
            self.files.remove(path);
            return (UNREADABLE, None);
        };
        let mut h = DefaultHasher::new();
        bytes.hash(&mut h);
        let hash = h.finish();
        let text = String::from_utf8(bytes).ok().map(Rc::new);
        let settled = mtime.is_some_and(|m| now.duration_since(m).is_ok_and(|age| age > SETTLED));
        self.files.insert(path.to_path_buf(), Cached { mtime, size, settled, hash, text: text.clone() });
        (hash, text)
    }

    fn forget(&mut self, path: &Path) {
        self.files.remove(path);
    }
}

/// The buffer standing in for the file at `path`, if there is one. Paths are made
/// canonical only if a buffer has the file's name, which most files have not.
fn buffer_for<'a>(path: &Path, overlays: &'a HashMap<PathBuf, String>) -> Option<&'a String> {
    let name = path.file_name();
    if overlays.keys().any(|k| k.file_name() == name) { overlays.get(&canon(path)) } else { None }
}

/// A hash of the files of the module directories `dirs` (their lists of files and
/// contents, the buffers standing in for the files on disk).
fn stamp(files: &mut FileCache, dirs: &[PathBuf], overlays: &HashMap<PathBuf, String>) -> u64 {
    let mut h = DefaultHasher::new();
    for dir in dirs {
        dir.hash(&mut h);
        let mut names = module_files(dir, overlays).unwrap_or_default();
        names.sort();
        for f in names {
            f.hash(&mut h);
            match buffer_for(&f, overlays) {
                Some(text) => text.hash(&mut h),
                None => files.load(&f).0.hash(&mut h),
            }
        }
    }
    h.finish()
}

struct Entry {
    facts: Rc<ModuleFacts>,
    /// The directories whose files the analysis read, and their stamp.
    dirs: Vec<PathBuf>,
    stamp: u64,
}

#[derive(Default)]
pub struct Workspace {
    cache: HashMap<PathBuf, Entry>,
    files: FileCache,
}

/// The directories to skip when looking for modules.
fn skipped(name: &str) -> bool {
    name.starts_with('.') || name == "target" || name == "node_modules"
}

/// The most module directories looked at; larger trees are not searched further.
const MAX_DIRS: usize = 4000;

fn find_module_dirs(root: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(root) else { return };
    let mut has_gm = false;
    let mut subdirs = vec![];
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            if !skipped(&e.file_name().to_string_lossy()) {
                subdirs.push(p);
            }
        } else if p.extension().is_some_and(|x| x == "gm") {
            has_gm = true;
        }
    }
    if has_gm {
        out.push(canon(root));
    }
    subdirs.sort();
    for d in subdirs {
        if out.len() >= MAX_DIRS {
            return;
        }
        find_module_dirs(&d, out);
    }
}

/// The root directory of the package that contains module directory `dir`: the nearest
/// directory with a `gamma.toml`, or `dir` itself.
pub fn package_root(dir: &Path) -> PathBuf {
    let mut cur = Some(dir);
    while let Some(d) = cur {
        if d.join("gamma.toml").is_file() {
            return d.to_path_buf();
        }
        cur = d.parent();
    }
    dir.to_path_buf()
}

/// The roots of the packages that a package at `root` depends on by path, transitively.
fn path_dependencies(root: &Path, seen: &mut Vec<PathBuf>) {
    let Ok(text) = std::fs::read_to_string(root.join("gamma.toml")) else { return };
    let Ok(manifest) = Manifest::parse(&text) else { return };
    for dep in manifest.deps {
        if let Source::Path(p) = dep.source {
            let dir = canon(&root.join(p));
            if !seen.contains(&dir) {
                seen.push(dir.clone());
                path_dependencies(&dir, seen);
            }
        }
    }
}

/// The roots of the package containing `dir` and of its path dependencies; the
/// package's own root is first.
pub fn package_roots(dir: &Path) -> Vec<PathBuf> {
    let root = canon(&package_root(dir));
    let mut roots = vec![root.clone()];
    path_dependencies(&root, &mut roots);
    roots
}

impl Workspace {
    /// The module directories of the packages rooted at `roots`, including directories
    /// that so far exist only as unsaved buffers.
    pub fn module_dirs(roots: &[PathBuf], overlays: &HashMap<PathBuf, String>) -> Vec<PathBuf> {
        let mut dirs = vec![];
        for r in roots {
            find_module_dirs(r, &mut dirs);
        }
        for p in overlays.keys() {
            if let Some(d) = p.parent()
                && roots.iter().any(|r| d.starts_with(r))
                && !d.components().any(|c| skipped(&c.as_os_str().to_string_lossy()))
                && !dirs.iter().any(|x| x == d)
            {
                dirs.push(d.to_path_buf());
            }
        }
        dirs.sort();
        dirs.dedup();
        dirs
    }

    /// Whether some file of module directory `dir` mentions `name`.
    pub fn mentions(&mut self, dir: &Path, name: &str, overlays: &HashMap<PathBuf, String>) -> bool {
        module_files(dir, overlays).unwrap_or_default().iter().any(|f| match buffer_for(f, overlays) {
            Some(t) => t.contains(name),
            None => self.files.load(f).1.is_some_and(|t| t.contains(name)),
        })
    }

    /// Drops what was read of the file at `path`, which changed on disk.
    pub fn file_changed(&mut self, path: &Path) {
        self.files.forget(path);
    }

    /// The facts of the module in `dir`, analyzed now unless the cached ones are still
    /// valid.
    pub fn facts(&mut self, dir: &Path, overlays: &HashMap<PathBuf, String>, recycle: bool) -> Rc<ModuleFacts> {
        if let Some(e) = self.cache.get(dir)
            && stamp(&mut self.files, &e.dirs, overlays) == e.stamp
        {
            return e.facts.clone();
        }
        let primary =
            module_files(dir, overlays).and_then(|f| f.into_iter().min()).unwrap_or_else(|| dir.join("main.gm"));
        let a = Analysis::run(dir, overlays, &canon(&primary), recycle);
        self.store(dir, &a, overlays)
    }

    /// Keeps the facts of an analysis of the module in `dir` that is current.
    pub fn store(&mut self, dir: &Path, a: &Analysis, overlays: &HashMap<PathBuf, String>) -> Rc<ModuleFacts> {
        let facts = Rc::new(ModuleFacts::from_analysis(a, dir));
        // The standard library is taken to stay as it is.
        let std = canon(&crate::sema::std_root());
        let mut dirs: Vec<PathBuf> = a.files.keys().filter_map(|f| f.parent().map(Path::to_path_buf)).collect();
        dirs.push(dir.to_path_buf());
        dirs.sort();
        dirs.dedup();
        dirs.retain(|d| !d.starts_with(&std));
        let stamp = stamp(&mut self.files, &dirs, overlays);
        self.cache.insert(dir.to_path_buf(), Entry { facts: facts.clone(), dirs, stamp });
        facts
    }
}

/// The uses of a declaration, and the modules they were found in.
pub struct Found {
    pub uses: Vec<Loc>,
    pub modules: Vec<Rc<ModuleFacts>>,
}

impl Found {
    /// The text of a file of one of the modules found.
    pub fn text(&self, path: &Path) -> Option<Rc<String>> {
        self.modules.iter().find_map(|m| m.texts.get(path).cloned())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = canon(&std::env::temp_dir()).join(format!("gamma-ws-{}-{}", name, std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn facts_are_reused_until_a_file_changes() {
        let dir = temp("reuse");
        std::fs::write(dir.join("a.gm"), "one :: fn() -> i32 => 1\n").unwrap();
        let mut ws = Workspace::default();
        let none = HashMap::new();
        let first = ws.facts(&dir, &none, false);
        assert!(Rc::ptr_eq(&first, &ws.facts(&dir, &none, false)));
        // A buffer in place of the file, then a new file in the directory.
        let buffer = HashMap::from([(dir.join("a.gm"), "two :: fn() -> i32 => 2\n".to_string())]);
        let second = ws.facts(&dir, &buffer, false);
        assert!(!Rc::ptr_eq(&first, &second));
        assert!(second.top_level.contains("two") && !second.top_level.contains("one"));
        assert!(Rc::ptr_eq(&second, &ws.facts(&dir, &buffer, false)));
        let more = HashMap::from([(dir.join("b.gm"), "three :: fn() -> i32 => 3\n".to_string())]);
        let third = ws.facts(&dir, &more, false);
        assert!(third.top_level.contains("one") && third.top_level.contains("three"));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn files_are_read_again_when_they_change() {
        let dir = temp("filecache");
        let file = dir.join("a.gm");
        std::fs::write(&file, "one\n").unwrap();
        let mut ws = Workspace::default();
        let none = HashMap::new();
        assert!(ws.mentions(&dir, "one", &none));
        // Same size, written just now: the modification time cannot be trusted to differ.
        std::fs::write(&file, "two\n").unwrap();
        assert!(ws.mentions(&dir, "two", &none) && !ws.mentions(&dir, "one", &none));
        // A file old enough to be trusted by (mtime, size) is not read again, until told.
        let old = SystemTime::now() - Duration::from_secs(3600);
        std::fs::File::options().write(true).open(&file).unwrap().set_modified(old).unwrap();
        assert!(ws.mentions(&dir, "two", &none));
        std::fs::write(&file, "six\n").unwrap();
        std::fs::File::options().write(true).open(&file).unwrap().set_modified(old).unwrap();
        assert!(ws.mentions(&dir, "two", &none));
        ws.file_changed(&file);
        assert!(ws.mentions(&dir, "six", &none) && !ws.mentions(&dir, "two", &none));
        std::fs::remove_file(&file).unwrap();
        assert!(!ws.mentions(&dir, "six", &none));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn module_directories_are_found_below_the_package_root() {
        let dir = temp("dirs");
        for d in ["a", "a/b", ".hidden", "target"] {
            std::fs::create_dir_all(dir.join(d)).unwrap();
            std::fs::write(dir.join(d).join("m.gm"), "").unwrap();
        }
        let found = Workspace::module_dirs(std::slice::from_ref(&dir), &HashMap::new());
        assert_eq!(found, vec![dir.join("a"), dir.join("a/b")]);
        let buffers = HashMap::from([(dir.join("new/x.gm"), String::new())]);
        let found = Workspace::module_dirs(std::slice::from_ref(&dir), &buffers);
        assert!(found.contains(&dir.join("new")));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
