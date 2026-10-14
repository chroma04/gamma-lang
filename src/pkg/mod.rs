//! The package manager: manifests, git and archive dependencies, the lockfile, build
//! settings and project scaffolding. See `docs/guide/15-packages.md` for the user's view.
//!
//! The compiler asks [`prepare`] for a [`Resolution`] before it analyzes a program: the
//! directory of every dependency of every package in the graph. Path dependencies need
//! no resolution; git and archive (`url = …`) dependencies are fetched into the package
//! store and locked in
//! `gamma.lock`, next to the root package's manifest.

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
    let abs = start.canonicalize().ok()?;
    let mut cur = if abs.is_dir() { Some(abs.as_path()) } else { abs.parent() };
    while let Some(d) = cur {
        if d.join("gamma.toml").is_file() {
            return Some(d.to_path_buf());
        }
        cur = d.parent();
    }
    None
}

/// Brings the git dependencies of the package at `root` up to date: resolves what the
/// lockfile does not cover, fetches what the store lacks, and rewrites `gamma.lock` if
/// it changed. Returns `None` if the graph has no git dependencies. `explicit` is set
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
/// dependencies of its package. `None` if it has no package or no git dependencies.
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
    let tmp = path.with_extension(format!("tmp{}", std::process::id()));
    std::fs::write(&tmp, text).map_err(|e| format!("cannot write {}: {}", tmp.display(), e))?;
    std::fs::rename(&tmp, path).map_err(|e| {
        let _ = std::fs::remove_file(&tmp);
        format!("cannot write {}: {}", path.display(), e)
    })
}
