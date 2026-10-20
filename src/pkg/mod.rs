//! The package manager: manifests, git and archive dependencies, the lockfile, build
//! settings and project scaffolding. See `docs/guide/15-packages.md` for the user's view.
//!
//! The compiler asks [`prepare`] for a [`Resolution`] before it analyzes a program: the
//! directory of every dependency of every package in the graph. Path dependencies need
//! no resolution; git and archive (`url = …`) dependencies are fetched into the package
//! store and locked in `gamma.lock`, next to the root package's manifest.

pub mod archive;
pub mod build;
pub mod cache;
pub mod cli;
mod edit;
pub mod git;
pub mod lock;
pub mod manifest;
pub mod resolve;
mod scaffold;

use lock::Lock;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// Where every package of a dependency graph finds its dependencies: canonical package
/// directory to dependency name to dependency directory.
#[derive(Debug, Default, Clone)]
pub struct Resolution {
    pub packages: HashMap<PathBuf, HashMap<String, PathBuf>>,
}

impl Resolution {
    /// The dependencies of the package whose root is `root`, if it is part of the graph.
    pub fn deps_of(&self, root: &Path) -> Option<&HashMap<String, PathBuf>> {
        self.packages.get(root)
    }
}

/// The directory of the package that the graph rooted at `root` knows as `name`: a
/// dependency of `root`, or of one of its dependencies, nearest first. `res` holds the
/// resolved git and archive dependencies; path dependencies are read from the manifests.
pub fn dependency_dir(root: &Path, res: Option<&Resolution>, name: &str) -> Result<PathBuf, String> {
    let canon = |p: &Path| crate::source::canonicalize(p).unwrap_or_else(|_| p.to_path_buf());
    let deps_of = |dir: &Path| -> Result<Vec<(String, PathBuf)>, String> {
        if let Some(deps) = res.and_then(|r| r.deps_of(dir)) {
            let mut v: Vec<_> = deps.iter().map(|(n, d)| (n.clone(), d.clone())).collect();
            v.sort();
            return Ok(v);
        }
        let path = dir.join("gamma.toml");
        let Ok(text) = std::fs::read_to_string(&path) else { return Ok(vec![]) };
        let manifest =
            manifest::Manifest::parse(&text).map_err(|e| format!("{}:{}: {}", path.display(), e.line, e.msg))?;
        Ok(manifest
            .deps
            .into_iter()
            .filter_map(|d| match d.source {
                manifest::Source::Path(p) => Some((d.name, canon(&dir.join(p)))),
                _ => None,
            })
            .collect())
    };
    let root = canon(root);
    let mut seen = vec![root.clone()];
    let mut queue = std::collections::VecDeque::from([root.clone()]);
    let mut direct = vec![];
    while let Some(dir) = queue.pop_front() {
        for (n, d) in deps_of(&dir)? {
            if dir == root {
                direct.push(n.clone());
            }
            if n == name {
                return Ok(d);
            }
            if !seen.contains(&d) {
                seen.push(d.clone());
                queue.push_back(d);
            }
        }
    }
    if direct.is_empty() {
        return Err(format!("the package in {} has no dependency `{}`; it has no dependencies", root.display(), name));
    }
    Err(format!(
        "the package in {} has no dependency `{}`; its dependencies are {}",
        root.display(),
        name,
        direct.iter().map(|n| format!("`{}`", n)).collect::<Vec<_>>().join(", ")
    ))
}

/// How a resolution may use the network and the lockfile.
#[derive(Debug, Default, Clone)]
pub struct Options {
    /// Never touch the network: everything must be locked and in the store.
    pub offline: bool,
    /// The lockfile must exist and stay unchanged.
    pub locked: bool,
}

/// Which locked packages to ignore in favor of the newest revisions that are allowed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Refresh {
    None,
    All,
    /// The packages with these names.
    Names(Vec<String>),
}

/// The root package for work at `start` (a file or a directory): the nearest directory at
/// or above it that holds a `gamma.toml`.
pub fn find_root(start: &Path) -> Option<PathBuf> {
    let abs = crate::source::canonicalize(start).ok()?;
    let mut cur = if abs.is_dir() { Some(abs.as_path()) } else { abs.parent() };
    while let Some(d) = cur {
        if d.join("gamma.toml").is_file() {
            return Some(d.to_path_buf());
        }
        cur = d.parent();
    }
    None
}

/// Brings the git and archive dependencies of the package at `root` up to date: resolves
/// what the lockfile does not cover, fetches what the store lacks, and rewrites `gamma.lock`
/// if it changed. Returns `None` if the graph has none. `explicit` is set
/// for the package commands, which also delete a lockfile that is no longer needed.
pub fn sync(root: &Path, opts: &Options, refresh: &Refresh, explicit: bool) -> Result<Option<Resolution>, String> {
    let lock_path = root.join("gamma.lock");
    let old = Lock::read(&lock_path)?;
    let mut resolver = resolve::Resolver::new(root, opts, refresh, old.clone());
    let Some((resolution, lock)) = resolver.run()? else {
        if explicit && !opts.locked && lock_path.exists() {
            std::fs::remove_file(&lock_path).map_err(|e| format!("cannot remove {}: {}", lock_path.display(), e))?;
        }
        return Ok(None);
    };
    let text = lock.render();
    if old.as_ref().map(Lock::render).as_deref() != Some(text.as_str()) {
        if opts.locked {
            return Err(if old.is_none() {
                "gamma.lock is missing, but --locked was given; run `gamma fetch` and commit gamma.lock".into()
            } else {
                "gamma.lock needs to be updated, but --locked was given; run `gamma fetch` and commit gamma.lock".into()
            });
        }
        report_changes(old.as_ref(), &lock, refresh);
        write_atomic(&lock_path, &text)?;
    }
    Ok(Some(resolution))
}

/// What the compiler does before analyzing the program at `input`: sync the git
/// and archive dependencies of its package. `None` if it has no package or none.
pub fn prepare(input: &Path, opts: &Options) -> Result<Option<Resolution>, String> {
    match find_root(input) {
        Some(root) => sync(&root, opts, &Refresh::None, false),
        None => Ok(None),
    }
}

fn report_changes(old: Option<&Lock>, new: &Lock, refresh: &Refresh) {
    let mut pkgs: Vec<_> = new.packages.iter().collect();
    pkgs.sort_by(|a, b| (&a.name, &a.url).cmp(&(&b.name, &b.url)));
    for p in pkgs {
        match old.and_then(|o| o.find(&p.url, p.subdir.as_deref())) {
            None => eprintln!("     Locking {} {}", p.name, p.display_version()),
            Some(o) if o.rev != p.rev || o.archive != p.archive => {
                if *refresh == Refresh::None {
                    eprintln!("     Locking {} {}", p.name, p.display_version());
                } else {
                    eprintln!("    Updating {} {} -> {}", p.name, o.display_version(), p.display_version());
                }
            }
            Some(_) => {}
        }
    }
    for o in old.iter().flat_map(|o| &o.packages) {
        if new.find(&o.url, o.subdir.as_deref()).is_none() {
            eprintln!("    Removing {} {}", o.name, o.display_version());
        }
    }
}

/// Writes `text` to `path` through a temporary file, so a reader never sees half a file.
pub(crate) fn write_atomic(path: &Path, text: &str) -> Result<(), String> {
    use std::io::Write;
    let dir = path.parent().filter(|d| !d.as_os_str().is_empty()).unwrap_or(Path::new("."));
    let tmp = archive::private_name(dir, "write");
    // Written in full and flushed to disk before it replaces the file, so a crash leaves
    // the old file or the new one, never a partial one; the temporary file never stays.
    let written = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&tmp)
        .and_then(|mut f| f.write_all(text.as_bytes()).and_then(|()| f.sync_all()));
    let res = written.and_then(|()| std::fs::rename(&tmp, path));
    res.map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        format!("cannot write {}: {}", path.display(), e)
    })
}
