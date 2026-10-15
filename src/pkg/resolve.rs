//! Dependency resolution: from a root manifest and an optional lockfile to the revision
//! of every git package, the archive of every URL package, their checkouts and the
//! lockfile that records them.
//!
//! The graph is walked from the root package. Each git package is identified by its
//! repository URL (and subdirectory); every requirement on it is collected, and one
//! revision must satisfy all of them. A revision that is chosen, and whose own
//! dependencies then add a requirement it does not satisfy, makes the walk start over
//! with the larger set of requirements. Requirements only grow, so the walk ends.

use super::archive;
use super::git::{self, Refs, Store};
use super::lock::{Lock, LockedPackage};
use super::manifest::{GitSpec, Manifest, Source, UrlSource, normalize_url};
use super::{Options, Refresh, Resolution};
use semver::Version;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::{Component, Path, PathBuf};

type Key = (String, Option<String>);

/// One package's requirement on a git package.
#[derive(Debug, Clone, PartialEq, Eq)]
struct Req {
    requirer: String,
    dep: String,
    /// The URL as written by the requirer.
    url: String,
    spec: GitSpec,
}

/// The revision chosen for a git package.
#[derive(Debug, Clone)]
struct Choice {
    rev: String,
    version: Option<String>,
    reference: String,
}

/// A downloaded and unpacked archive package.
#[derive(Debug, Clone)]
struct ArchiveInfo {
    /// The SHA-256 of the archive file, hexadecimal.
    sha256: String,
    /// The canonical directory it is unpacked in.
    dir: PathBuf,
    /// The checksum of the unpacked tree.
    checksum: String,
}

/// The version a tag name stands for: `vX.Y.Z` or `X.Y.Z`.
pub fn tag_version(tag: &str) -> Option<Version> {
    Version::parse(tag.strip_prefix('v').unwrap_or(tag)).ok()
}

/// The version a lock entry records for `reference`, given the requirements on it: the
/// version of the tag when the tag was chosen by a version requirement (and not pinned by
/// name), otherwise none. This is what `pick` records, derived without the repository.
fn recorded_version(reqs: &[Req], reference: &str) -> Option<String> {
    let reqs = effective(reqs);
    let pinned = reqs.iter().any(|r| !matches!(r.spec, GitSpec::Version(_) | GitSpec::Default));
    let versioned = reqs.iter().any(|r| matches!(r.spec, GitSpec::Version(_)));
    if pinned || !versioned {
        return None;
    }
    reference.strip_prefix("tag=").and_then(tag_version).map(|v| v.to_string())
}

/// Whether a revision with `reference` and `rev` is what `spec` asks for.
fn satisfies(reference: &str, rev: &str, spec: &GitSpec) -> bool {
    match spec {
        GitSpec::Default => reference == "default",
        GitSpec::Tag(t) => reference == format!("tag={}", t),
        GitSpec::Branch(b) => reference == format!("branch={}", b),
        GitSpec::Rev(r) => rev.starts_with(r.as_str()),
        GitSpec::Version(req) => reference.strip_prefix("tag=").and_then(tag_version).is_some_and(|v| req.matches(&v)),
    }
}

/// The requirements that matter: a request for the default branch gives way to any other.
fn effective(reqs: &[Req]) -> Vec<&Req> {
    let others: Vec<&Req> = reqs.iter().filter(|r| r.spec != GitSpec::Default).collect();
    if others.is_empty() { reqs.iter().collect() } else { others }
}

fn describe(reqs: &[&Req]) -> String {
    let mut parts: Vec<String> = reqs.iter().map(|r| format!("`{}` requires {}", r.requirer, r.spec)).collect();
    parts.sort();
    parts.dedup();
    parts.join(", ")
}

/// Picks the revision for `reqs` among the references of a repository.
fn pick(url: &str, reqs: &[Req], refs: &Refs, db: &Path) -> Result<Choice, String> {
    let reqs = effective(reqs);
    let mut pins: Vec<&Req> = vec![];
    for r in reqs.iter().filter(|r| !matches!(r.spec, GitSpec::Version(_) | GitSpec::Default)) {
        if !pins.iter().any(|p| p.spec == r.spec) {
            pins.push(r);
        }
    }
    let versions: Vec<&semver::VersionReq> =
        reqs.iter().filter_map(|r| if let GitSpec::Version(v) = &r.spec { Some(v) } else { None }).collect();
    let conflict = || format!("conflicting requirements for {}: {}", url, describe(&reqs));
    if pins.len() > 1 {
        return Err(conflict());
    }
    if let Some(pin) = pins.first() {
        return match &pin.spec {
            GitSpec::Tag(t) => {
                let Some((_, rev)) = refs.tags.iter().find(|(n, _)| n == t) else {
                    return Err(format!("{} has no tag `{}` (required by `{}`)", url, t, pin.requirer));
                };
                if !versions.is_empty() {
                    match tag_version(t) {
                        Some(v) if versions.iter().all(|r| r.matches(&v)) => {}
                        _ => return Err(conflict()),
                    }
                }
                Ok(Choice { rev: rev.clone(), version: None, reference: format!("tag={}", t) })
            }
            GitSpec::Branch(b) => {
                let Some((_, rev)) = refs.branches.iter().find(|(n, _)| n == b) else {
                    return Err(format!("{} has no branch `{}` (required by `{}`)", url, b, pin.requirer));
                };
                if !versions.is_empty() {
                    return Err(conflict());
                }
                Ok(Choice { rev: rev.clone(), version: None, reference: format!("branch={}", b) })
            }
            GitSpec::Rev(r) => {
                let Some(rev) = git::rev_parse(db, r)? else {
                    return Err(format!(
                        "{} has no commit `{}` on a branch or tag (required by `{}`)",
                        url, r, pin.requirer
                    ));
                };
                if !versions.is_empty() {
                    return Err(conflict());
                }
                Ok(Choice { rev, version: None, reference: format!("rev={}", r) })
            }
            _ => unreachable!(),
        };
    }
    if !versions.is_empty() {
        let best = refs
            .tags
            .iter()
            .filter_map(|(n, rev)| Some((tag_version(n)?, n, rev)))
            .filter(|(v, _, _)| versions.iter().all(|r| r.matches(v)))
            .max_by(|a, b| a.0.cmp(&b.0));
        return match best {
            Some((v, n, rev)) => {
                Ok(Choice { rev: rev.clone(), version: Some(v.to_string()), reference: format!("tag={}", n) })
            }
            None => Err(format!("no version tag of {} satisfies the requirements: {}", url, describe(&reqs))),
        };
    }
    match &refs.head {
        Some(rev) => Ok(Choice { rev: rev.clone(), version: None, reference: "default".into() }),
        None => Err(format!("{} has no default branch to use (required by `{}`)", url, reqs[0].requirer)),
    }
}

/// Removes `.` and `..` components without touching the file system.
pub fn normalize(p: &Path) -> PathBuf {
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

/// Reads and parses the manifest in `dir`, with errors that name the file and line.
pub fn read_manifest(dir: &Path) -> Result<Manifest, String> {
    let path = dir.join("gamma.toml");
    let text = std::fs::read_to_string(&path).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
    Manifest::parse(&text).map_err(|e| format!("{}:{}: {}", path.display(), e.line, e.msg))
}

pub(super) struct Resolver<'a> {
    root: PathBuf,
    opts: &'a Options,
    refresh: &'a Refresh,
    store: Store,
    old: Option<Lock>,
    constraints: BTreeMap<Key, Vec<Req>>,
    fetched: HashSet<String>,
    refs: HashMap<String, Refs>,
    /// The archives obtained so far, by URL (kept across walks).
    archives: HashMap<String, ArchiveInfo>,
    /// The names of the dependencies that are archives.
    url_deps: HashSet<String>,
    // State of one walk.
    chosen: BTreeMap<Key, Choice>,
    used_archives: std::collections::BTreeSet<Key>,
    checkouts: HashMap<Key, PathBuf>,
    checksums: HashMap<Key, String>,
    pkg_names: HashMap<Key, String>,
    pkg_deps: BTreeMap<Key, Vec<Key>>,
    dirty: bool,
    res: Resolution,
    /// The packages being walked (directory, display name, is a git package).
    stack: Vec<(PathBuf, String, bool)>,
}

impl<'a> Resolver<'a> {
    pub(super) fn new(root: &Path, opts: &'a Options, refresh: &'a Refresh, old: Option<Lock>) -> Resolver<'a> {
        Resolver {
            root: root.to_path_buf(),
            opts,
            refresh,
            store: Store::for_package(root),
            old,
            constraints: BTreeMap::new(),
            fetched: HashSet::new(),
            refs: HashMap::new(),
            archives: HashMap::new(),
            url_deps: HashSet::new(),
            chosen: BTreeMap::new(),
            used_archives: Default::default(),
            checkouts: HashMap::new(),
            checksums: HashMap::new(),
            pkg_names: HashMap::new(),
            pkg_deps: BTreeMap::new(),
            dirty: false,
            res: Resolution::default(),
            stack: vec![],
        }
    }

    /// Resolves the graph of the root package. Returns the resolution and the lock it
    /// implies, or `None` if there are no git dependencies.
    pub(super) fn run(&mut self) -> Result<Option<(Resolution, Lock)>, String> {
        let root = crate::source::canonicalize(&self.root)
            .map_err(|e| format!("cannot read {}: {}", self.root.display(), e))?;
        let manifest = read_manifest(&root)?;
        loop {
            self.chosen.clear();
            self.used_archives.clear();
            self.checkouts.clear();
            self.pkg_names.clear();
            self.pkg_deps.clear();
            self.res = Resolution::default();
            self.stack.clear();
            self.dirty = false;
            let name = manifest
                .name
                .clone()
                .unwrap_or_else(|| root.file_name().map_or("root".to_string(), |s| s.to_string_lossy().into_owned()));
            self.walk(&root, &manifest, &name, None)?;
            if !self.dirty {
                break;
            }
        }
        if self.chosen.is_empty() && self.used_archives.is_empty() {
            return Ok(None);
        }
        self.check_refresh_names()?;
        let lock = self.build_lock()?;
        Ok(Some((std::mem::take(&mut self.res), lock)))
    }

    fn check_refresh_names(&self) -> Result<(), String> {
        if let Refresh::Names(names) = self.refresh {
            for n in names {
                let known = self.chosen.keys().any(|k| self.entry_name(k) == *n)
                    || self.url_deps.contains(n)
                    || self.constraints.values().flatten().any(|r| r.dep == *n);
                if !known {
                    return Err(format!("`{}` is not a git dependency of this package", n));
                }
            }
        }
        Ok(())
    }

    fn entry_name(&self, key: &Key) -> String {
        if let Some(n) = self.pkg_names.get(key) {
            return n.clone();
        }
        let mut deps: Vec<&str> = self.constraints.get(key).into_iter().flatten().map(|r| r.dep.as_str()).collect();
        deps.sort();
        deps.first().map_or("package", |s| s).to_string()
    }

    fn add_req(&mut self, key: &Key, req: Req) {
        let list = self.constraints.entry(key.clone()).or_default();
        if list.contains(&req) {
            return;
        }
        list.push(req.clone());
        if let Some(c) = self.chosen.get(key)
            && req.spec != GitSpec::Default
            && !satisfies(&c.reference, &c.rev, &req.spec)
        {
            self.chosen.remove(key);
            self.dirty = true;
        }
    }

    /// Fetches the mirror of the repository of `key` once per run.
    fn mirror(&mut self, key: &Key) -> Result<(), String> {
        if self.fetched.contains(&key.0) {
            return Ok(());
        }
        let reqs = &self.constraints[key];
        let (dep, url) = (reqs[0].dep.clone(), reqs[0].url.clone());
        eprintln!("    Fetching {} ({})", dep, url);
        let db = self.store.db(&key.0);
        git::fetch_mirror(&db, &url)?;
        let refs = git::list_refs(&db)?;
        self.fetched.insert(key.0.clone());
        self.refs.insert(key.0.clone(), refs);
        Ok(())
    }

    fn is_refreshed(&self, key: &Key) -> bool {
        match self.refresh {
            Refresh::None => false,
            Refresh::All => true,
            Refresh::Names(names) => names.iter().any(|n| {
                self.constraints[key].iter().any(|r| r.dep == *n)
                    || self.old.as_ref().and_then(|o| o.find(&key.0, key.1.as_deref())).is_some_and(|l| l.name == *n)
            }),
        }
    }

    fn choose(&mut self, key: &Key) -> Result<Choice, String> {
        if let Some(c) = self.chosen.get(key) {
            return Ok(c.clone());
        }
        let reqs = self.constraints[key].clone();
        let locked = self.old.as_ref().and_then(|o| o.find(&key.0, key.1.as_deref())).cloned();
        let reuse = match &locked {
            Some(l) if !self.is_refreshed(key) => {
                effective(&reqs).iter().all(|r| satisfies(&l.reference, &l.rev, &r.spec))
            }
            _ => false,
        };
        let choice = if let (true, Some(l)) = (reuse, locked) {
            // The version follows from the tag; one edited by hand is repaired (and
            // `--locked` then reports that the lock would change).
            let version = recorded_version(&reqs, &l.reference);
            Choice { rev: l.rev, version, reference: l.reference }
        } else {
            let (dep, url) = (&reqs[0].dep, &reqs[0].url);
            if self.opts.locked && self.old.is_none() {
                return Err(
                    "gamma.lock is missing, but --locked was given; run `gamma fetch` and commit gamma.lock".into()
                );
            }
            if self.opts.locked {
                return Err(format!(
                    "gamma.lock does not satisfy dependency `{}` ({}), and --locked was given; run `gamma fetch` and commit gamma.lock",
                    dep, url
                ));
            }
            if self.opts.offline {
                return Err(format!(
                    "dependency `{}` ({}) is not in gamma.lock or the lock is out of date; it cannot be resolved with --offline (run `gamma fetch`)",
                    dep, url
                ));
            }
            self.mirror(key)?;
            pick(&reqs[0].url, &reqs, &self.refs[&key.0], &self.store.db(&key.0))?
        };
        self.chosen.insert(key.clone(), choice.clone());
        Ok(choice)
    }

    /// The checkout of the chosen revision, made if it is missing (and then verified
    /// against the lockfile's checksum). Returns its canonical path.
    fn ensure_checkout(&mut self, key: &Key, choice: &Choice) -> Result<PathBuf, String> {
        if let Some(p) = self.checkouts.get(key) {
            return Ok(p.clone());
        }
        let dest = self.store.checkout(&key.0, &choice.rev);
        if !dest.is_dir() {
            let db = self.store.db(&key.0);
            let has = |db: &Path| {
                db.join("HEAD").is_file()
                    && git::rev_parse(db, &choice.rev).ok().flatten().as_deref() == Some(&choice.rev)
            };
            let (dep, url) = (self.constraints[key][0].dep.clone(), self.constraints[key][0].url.clone());
            if !has(&db) {
                if self.opts.offline {
                    return Err(format!(
                        "revision {} of `{}` ({}) is not in the cache, and --offline was given; run `gamma fetch`",
                        &choice.rev[..12.min(choice.rev.len())],
                        dep,
                        url
                    ));
                }
                self.mirror(key)?;
                if !has(&db) {
                    return Err(format!(
                        "revision {} of `{}` ({}) is no longer available in the repository (a tag moved or history was rewritten?); run `gamma update {}`",
                        choice.rev, dep, url, dep
                    ));
                }
            }
            git::checkout(&db, &choice.rev, &dest)?;
            let sum = git::tree_checksum(&dest).inspect_err(|_| {
                let _ = git::remove_tree(&dest);
            })?;
            if let Some(l) = self.old.as_ref().and_then(|o| o.find(&key.0, key.1.as_deref()))
                && l.rev == choice.rev
                && l.checksum != sum
            {
                let (name, expected) = (l.name.clone(), l.checksum.clone());
                let _ = git::remove_tree(&dest);
                return Err(format!("checksum mismatch for package {} (expected {}, found {})", name, expected, sum));
            }
            self.checksums.insert(key.clone(), sum);
            git::make_read_only(&dest);
        } else if !self.checksums.contains_key(key)
            && let Some(l) = self.old.as_ref().and_then(|o| o.find(&key.0, key.1.as_deref()))
            && l.rev == choice.rev
        {
            // A checkout from an earlier run: it must still be what the lock vouches for.
            let sum = git::tree_checksum(&dest)?;
            if l.checksum != sum {
                return Err(format!(
                    "checksum mismatch for package {} (expected {}, found {}); gamma.lock was edited or the cached checkout {} was modified (`gamma cache clean --all` removes it)",
                    l.name,
                    l.checksum,
                    sum,
                    dest.display()
                ));
            }
            self.checksums.insert(key.clone(), sum);
        }
        git::mark_used(&dest);
        let canon = crate::source::canonicalize(&dest).map_err(|e| format!("cannot read {}: {}", dest.display(), e))?;
        self.checkouts.insert(key.clone(), canon.clone());
        Ok(canon)
    }

    /// The unpacked archive of the dependency `dep` (downloaded if it is not in the store
    /// yet), verified against the manifest's `sha256` and the lockfile.
    fn ensure_archive(&mut self, dep: &str, u: &UrlSource) -> Result<ArchiveInfo, String> {
        if let Some(info) = self.archives.get(&u.url) {
            if let Some(h) = &u.sha256
                && *h != info.sha256
            {
                return Err(sha_mismatch(dep, &u.url, &info.sha256, h, "gamma.toml"));
            }
            return Ok(info.clone());
        }
        let lock_key = format!("url+{}", u.url);
        let locked = self.old.as_ref().and_then(|o| o.packages.iter().find(|p| p.url == lock_key)).cloned();
        let locked_hash = locked.as_ref().and_then(|l| l.archive.as_ref()).map(|a| a.trim_start_matches("sha256:"));
        // What the archive must hash to, and where that comes from.
        let expected: Option<(String, &str)> = match (&u.sha256, locked_hash) {
            (Some(h), _) => Some((h.clone(), "gamma.toml")),
            (None, Some(h)) => Some((h.to_string(), "gamma.lock")),
            (None, None) => None,
        };
        // The lock vouches for the tree only if it vouches for this archive.
        let locked_sum = locked
            .as_ref()
            .filter(|l| expected.as_ref().is_some_and(|(h, _)| l.archive.as_deref() == Some(&format!("sha256:{}", h))))
            .map(|l| l.checksum.clone());
        if self.opts.locked && locked_sum.is_none() {
            return Err(if self.old.is_none() {
                "gamma.lock is missing, but --locked was given; run `gamma fetch` and commit gamma.lock".into()
            } else {
                format!(
                    "gamma.lock does not satisfy dependency `{}` ({}), and --locked was given; run `gamma fetch` and commit gamma.lock",
                    dep, u.url
                )
            });
        }
        let summarize = |hash: &str, dir: &Path, sum: Option<String>| -> Result<ArchiveInfo, String> {
            let checksum = match sum {
                Some(s) => s,
                None => git::tree_checksum(dir)?,
            };
            let dir = crate::source::canonicalize(dir).map_err(|e| format!("cannot read {}: {}", dir.display(), e))?;
            Ok(ArchiveInfo { sha256: hash.to_string(), dir, checksum })
        };
        if let Some((hash, _)) = &expected {
            let dest = self.store.archive(&u.url, hash);
            if dest.is_dir() {
                git::mark_used(&dest);
                let info = summarize(hash, &dest, locked_sum)?;
                self.archives.insert(u.url.clone(), info.clone());
                return Ok(info);
            }
        }
        if self.opts.offline {
            return Err(format!(
                "the archive of `{}` ({}) is not in the cache, and --offline was given; run `gamma fetch`",
                dep, u.url
            ));
        }
        eprintln!("    Fetching {} ({})", dep, u.url);
        let dl = archive::download(&u.url, &self.store.downloads())?;
        if let Some((h, origin)) = &expected
            && *h != dl.sha256
        {
            return Err(sha_mismatch(dep, &u.url, &dl.sha256, h, origin));
        }
        let dest = self.store.archive(&u.url, &dl.sha256);
        if !dest.is_dir() {
            archive::unpack(&dl.path, &dest).map_err(|e| format!("dependency `{}` ({}): {}", dep, u.url, e))?;
            let sum = git::tree_checksum(&dest).map_err(|e| {
                let _ = git::remove_tree(&dest);
                format!("dependency `{}` ({}): {}", dep, u.url, e)
            })?;
            if let Some(l) = locked_sum.as_ref().filter(|l| **l != sum) {
                let _ = git::remove_tree(&dest);
                return Err(format!("checksum mismatch for package {} (expected {}, found {})", dep, l, sum));
            }
            git::make_read_only(&dest);
            git::mark_used(&dest);
            let info = summarize(&dl.sha256, &dest, Some(sum))?;
            self.archives.insert(u.url.clone(), info.clone());
            return Ok(info);
        }
        git::mark_used(&dest);
        let info = summarize(&dl.sha256, &dest, locked_sum)?;
        self.archives.insert(u.url.clone(), info.clone());
        Ok(info)
    }

    /// Visits the package in `dir`; `owner` is the git package it belongs to, with the
    /// root of its checkout.
    fn walk(
        &mut self,
        dir: &Path,
        manifest: &Manifest,
        display: &str,
        owner: Option<&(Key, PathBuf)>,
    ) -> Result<(), String> {
        if let Some(i) = self.stack.iter().position(|(d, _, _)| d == dir) {
            if owner.is_some() || self.stack[i..].iter().any(|s| s.2) {
                let names: Vec<&str> =
                    self.stack[i..].iter().map(|s| s.1.as_str()).chain(std::iter::once(display)).collect();
                return Err(format!("dependency cycle: {}", names.join(" -> ")));
            }
            return Ok(());
        }
        if self.res.packages.contains_key(dir) {
            return Ok(());
        }
        self.stack.push((dir.to_path_buf(), display.to_string(), owner.is_some()));
        let mut deps: HashMap<String, PathBuf> = HashMap::new();
        for dep in &manifest.deps {
            match &dep.source {
                Source::Path(p) => {
                    let joined = normalize(&dir.join(p));
                    let canon = crate::source::canonicalize(&joined);
                    if let Some((_, gr)) = owner {
                        match &canon {
                            Ok(c) if c.starts_with(gr) => {}
                            Ok(_) => {
                                return Err(format!(
                                    "dependency `{}` of `{}`: the path `{}` leaves the repository",
                                    dep.name, display, p
                                ));
                            }
                            Err(_) => {
                                return Err(format!(
                                    "dependency `{}` of `{}`: the path `{}` does not exist in the repository",
                                    dep.name, display, p
                                ));
                            }
                        }
                    }
                    let target = canon.as_ref().map_or(joined, |c| c.clone());
                    deps.insert(dep.name.clone(), target.clone());
                    if canon.is_ok() && target.join("gamma.toml").is_file() {
                        let m = read_manifest(&target)?;
                        self.walk(&target, &m, &dep.name, owner)?;
                    }
                }
                Source::Git(g) => {
                    let key: Key = (normalize_url(&g.url), g.subdir.clone());
                    let req = Req {
                        requirer: display.to_string(),
                        dep: dep.name.clone(),
                        url: g.url.clone(),
                        spec: g.spec.clone(),
                    };
                    self.add_req(&key, req);
                    let choice = self.choose(&key)?;
                    let co = self.ensure_checkout(&key, &choice)?;
                    let pkg_dir = match &g.subdir {
                        Some(s) => crate::source::canonicalize(&co.join(s)).ok().filter(|p| p.starts_with(&co)),
                        None => Some(co.clone()),
                    };
                    let Some(pkg_dir) = pkg_dir else {
                        return Err(format!(
                            "dependency `{}`: {} has no directory `{}`",
                            dep.name,
                            g.url,
                            g.subdir.clone().unwrap_or_default()
                        ));
                    };
                    if !pkg_dir.join("gamma.toml").is_file() {
                        return Err(format!(
                            "dependency `{}`: {} has no gamma.toml in its package directory",
                            dep.name, g.url
                        ));
                    }
                    if let Some((parent, _)) = owner {
                        self.pkg_deps.entry(parent.clone()).or_default().push(key.clone());
                    }
                    deps.insert(dep.name.clone(), pkg_dir.clone());
                    let m = read_manifest(&pkg_dir)?;
                    if let Some(n) = &m.name {
                        self.pkg_names.insert(key.clone(), n.clone());
                    }
                    self.walk(&pkg_dir, &m, &dep.name, Some(&(key, co)))?;
                }
                Source::Url(u) => {
                    let info = self.ensure_archive(&dep.name, u)?;
                    let key: Key = (format!("url+{}", u.url), u.subdir.clone());
                    let co = info.dir.clone();
                    let pkg_dir = match &u.subdir {
                        Some(s) => crate::source::canonicalize(&co.join(s)).ok().filter(|p| p.starts_with(&co)),
                        None => Some(co.clone()),
                    };
                    let Some(pkg_dir) = pkg_dir else {
                        return Err(format!(
                            "dependency `{}`: {} has no directory `{}`",
                            dep.name,
                            u.url,
                            u.subdir.clone().unwrap_or_default()
                        ));
                    };
                    if !pkg_dir.join("gamma.toml").is_file() {
                        return Err(format!(
                            "dependency `{}`: {} has no gamma.toml in its package directory",
                            dep.name, u.url
                        ));
                    }
                    if let Some((parent, _)) = owner {
                        self.pkg_deps.entry(parent.clone()).or_default().push(key.clone());
                    }
                    deps.insert(dep.name.clone(), pkg_dir.clone());
                    let m = read_manifest(&pkg_dir)?;
                    self.pkg_names.insert(key.clone(), m.name.clone().unwrap_or_else(|| dep.name.clone()));
                    self.url_deps.insert(dep.name.clone());
                    self.used_archives.insert(key.clone());
                    self.walk(&pkg_dir, &m, &dep.name, Some(&(key, co)))?;
                }
            }
        }
        self.stack.pop();
        self.res.packages.insert(dir.to_path_buf(), deps);
        Ok(())
    }

    fn build_lock(&mut self) -> Result<Lock, String> {
        let mut packages = vec![];
        let keys: Vec<Key> = self.chosen.keys().cloned().collect();
        for key in keys {
            let choice = self.chosen[&key].clone();
            let checksum = match self.checksums.get(&key) {
                Some(s) => s.clone(),
                None => match self.old.as_ref().and_then(|o| o.find(&key.0, key.1.as_deref())) {
                    Some(l) if l.rev == choice.rev => l.checksum.clone(),
                    _ => git::tree_checksum(&self.checkouts[&key])?,
                },
            };
            let mut dependencies: Vec<String> =
                self.pkg_deps.get(&key).into_iter().flatten().map(|k| self.entry_name(k)).collect();
            dependencies.sort();
            dependencies.dedup();
            packages.push(LockedPackage {
                name: self.entry_name(&key),
                url: key.0.clone(),
                subdir: key.1.clone(),
                version: choice.version,
                reference: choice.reference,
                rev: choice.rev,
                archive: None,
                checksum,
                dependencies,
            });
        }
        for key in self.used_archives.clone() {
            let info = &self.archives[key.0.strip_prefix("url+").unwrap_or(&key.0)];
            let mut dependencies: Vec<String> =
                self.pkg_deps.get(&key).into_iter().flatten().map(|k| self.entry_name(k)).collect();
            dependencies.sort();
            dependencies.dedup();
            packages.push(LockedPackage {
                name: self.entry_name(&key),
                url: key.0.clone(),
                subdir: key.1.clone(),
                version: None,
                reference: String::new(),
                rev: String::new(),
                archive: Some(format!("sha256:{}", info.sha256)),
                checksum: info.checksum.clone(),
                dependencies,
            });
        }
        Ok(Lock { packages })
    }
}

fn sha_mismatch(dep: &str, url: &str, found: &str, expected: &str, origin: &str) -> String {
    format!("the archive of dependency `{}` ({}) has the SHA-256 {}, but {} says {}", dep, url, found, origin, expected)
}

#[cfg(test)]
mod tests {
    use super::*;
    use semver::VersionReq;

    fn req(who: &str, spec: GitSpec) -> Req {
        Req { requirer: who.into(), dep: "d".into(), url: "u".into(), spec }
    }

    fn vr(s: &str) -> GitSpec {
        GitSpec::Version(VersionReq::parse(s).unwrap())
    }

    fn refs() -> Refs {
        let tags = ["v1.0.0", "v1.2.0", "1.2.5", "v1.10.0", "v2.0.0", "v2.1.0-beta.1", "nightly"];
        Refs {
            tags: tags.iter().enumerate().map(|(i, t)| (t.to_string(), format!("{:040}", i + 1))).collect(),
            branches: vec![("main".into(), "b".repeat(40))],
            head: Some("b".repeat(40)),
        }
    }

    fn picked(reqs: &[Req]) -> Result<Choice, String> {
        pick("u", reqs, &refs(), Path::new("/nonexistent"))
    }

    #[test]
    fn tag_names_are_versions() {
        assert_eq!(tag_version("v1.2.3"), Some(Version::new(1, 2, 3)));
        assert_eq!(tag_version("1.2.3"), Some(Version::new(1, 2, 3)));
        assert_eq!(tag_version("nightly"), None);
        assert_eq!(tag_version("v1.2"), None);
    }

    #[test]
    fn highest_matching_version() {
        let c = picked(&[req("a", vr("1.2"))]).unwrap();
        assert_eq!((c.version.as_deref(), c.reference.as_str()), (Some("1.10.0"), "tag=v1.10.0"));
        // Caret and tilde follow Cargo.
        assert_eq!(picked(&[req("a", vr("~1.2"))]).unwrap().version.as_deref(), Some("1.2.5"));
        assert_eq!(picked(&[req("a", vr("^2"))]).unwrap().version.as_deref(), Some("2.0.0"));
        // Prereleases only when asked for.
        assert_eq!(picked(&[req("a", vr(">=2.1.0-beta.0"))]).unwrap().version.as_deref(), Some("2.1.0-beta.1"));
        // All requirements must hold.
        assert_eq!(picked(&[req("a", vr("1")), req("b", vr("<1.5"))]).unwrap().version.as_deref(), Some("1.2.5"));
    }

    #[test]
    fn conflicts_name_both_requirers() {
        let e = picked(&[req("alpha", vr("^1")), req("beta", vr("^2"))]).unwrap_err();
        assert!(e.contains("alpha") && e.contains("beta") && e.contains("no version tag"), "{}", e);
        let pins = [req("alpha", GitSpec::Tag("v1.0.0".into())), req("beta", GitSpec::Branch("main".into()))];
        let e = picked(&pins).unwrap_err();
        assert!(e.contains("conflicting") && e.contains("alpha") && e.contains("beta"), "{}", e);
        let e = picked(&[req("alpha", GitSpec::Branch("main".into())), req("beta", vr("1"))]).unwrap_err();
        assert!(e.contains("conflicting"), "{}", e);
        assert!(picked(&[req("a", GitSpec::Tag("nope".into()))]).unwrap_err().contains("no tag"));
    }

    #[test]
    fn pins_and_defaults() {
        let c = picked(&[req("a", GitSpec::Tag("v1.0.0".into())), req("b", vr("^1"))]).unwrap();
        assert_eq!(c.reference, "tag=v1.0.0");
        let c = picked(&[req("a", GitSpec::Default)]).unwrap();
        assert_eq!((c.reference.as_str(), c.rev.as_str()), ("default", "b".repeat(40).as_str()));
        // A request for the default branch gives way to another.
        let c = picked(&[req("a", GitSpec::Default), req("b", GitSpec::Branch("main".into()))]).unwrap();
        assert_eq!(c.reference, "branch=main");
        let c = picked(&[req("a", GitSpec::Tag("v1.0.0".into())), req("b", GitSpec::Tag("v1.0.0".into()))]).unwrap();
        assert_eq!(c.reference, "tag=v1.0.0");
    }

    #[test]
    fn locked_references_satisfy() {
        let rev = "ab".repeat(20);
        assert!(satisfies("tag=v1.2.3", &rev, &vr("1.2")));
        assert!(!satisfies("tag=v1.2.3", &rev, &vr("1.3")));
        assert!(!satisfies("branch=main", &rev, &vr("1")));
        assert!(satisfies("branch=main", &rev, &GitSpec::Branch("main".into())));
        assert!(satisfies("default", &rev, &GitSpec::Default));
        assert!(satisfies("rev=abab", &rev, &GitSpec::Rev("abab".into())));
        assert!(!satisfies("rev=abab", &rev, &GitSpec::Rev("cd".into())));
        assert!(satisfies("tag=v1.0.0", &rev, &GitSpec::Tag("v1.0.0".into())));
    }
}
