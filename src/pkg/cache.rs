//! Garbage collection of the cache directory (`gamma cache clean`).
//!
//! The cache holds, under [`crate::cgen::cache_dir`]:
//!
//! - `git/checkouts/<repo>/<rev>/` and `git/url/<name>/<hash>/`, read-only package trees,
//!   each with a `<rev>.used` stamp file next to it whose modification time is the last
//!   time a build used the tree;
//! - `git/db/<repo>/`, bare mirrors of repositories;
//! - `run/prog-*` (cached executables), `native/*` (compiled C sources and the shared
//!   libraries the interpreter loads) and `gm_rt-*` (the compiled runtime), whose
//!   modification times are their last use.
//!
//! A clean removes the trees unused for the given time, the mirrors that no checkout is
//! left for, the builds unused for that time and any half-written temporary file older
//! than a day; with `all` it removes all of them.

use super::git;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

/// What a clean removed.
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct Report {
    pub checkouts: usize,
    pub mirrors: usize,
    pub builds: usize,
    pub bytes: u64,
}

impl Report {
    pub fn is_empty(&self) -> bool {
        self.checkouts + self.mirrors + self.builds == 0
    }

    /// One line for the user.
    pub fn summary(&self, dry_run: bool) -> String {
        fn plural(n: usize, one: &str, many: &str) -> String {
            format!("{} {}", n, if n == 1 { one } else { many })
        }
        if self.is_empty() {
            return "Nothing to remove".into();
        }
        format!(
            "{} {}, {}, {} ({} {})",
            if dry_run { "Would remove" } else { "Removed" },
            plural(self.checkouts, "package checkout", "package checkouts"),
            plural(self.mirrors, "repository mirror", "repository mirrors"),
            plural(self.builds, "cached build", "cached builds"),
            human_size(self.bytes),
            if dry_run { "to free" } else { "freed" },
        )
    }
}

/// `n` bytes as `12.3 MiB`.
pub fn human_size(n: u64) -> String {
    const UNITS: [&str; 4] = ["B", "KiB", "MiB", "GiB"];
    let mut v = n as f64;
    let mut u = 0;
    while v >= 1024.0 && u + 1 < UNITS.len() {
        v /= 1024.0;
        u += 1;
    }
    if u == 0 { format!("{} B", n) } else { format!("{:.1} {}", v, UNITS[u]) }
}

/// How a clean selects what to remove.
#[derive(Debug, Clone)]
pub struct Policy {
    /// Remove what has not been used for this long.
    pub older_than: Duration,
    /// Remove everything, whatever its age.
    pub all: bool,
    /// Only count what would be removed.
    pub dry_run: bool,
}

/// Half-written files older than this are removed by every clean.
const TMP_MAX_AGE: Duration = Duration::from_secs(24 * 3600);

struct Cleaner {
    policy: Policy,
    now: SystemTime,
    report: Report,
}

fn size_of_tree(path: &Path) -> u64 {
    fn walk(p: &Path) -> u64 {
        let Ok(meta) = std::fs::symlink_metadata(p) else { return 0 };
        if meta.is_dir() {
            std::fs::read_dir(p).map_or(0, |rd| rd.flatten().map(|e| walk(&e.path())).sum())
        } else {
            meta.len()
        }
    }
    walk(path)
}

fn name_of(p: &Path) -> String {
    p.file_name().map_or(String::new(), |n| n.to_string_lossy().into_owned())
}

fn modified(p: &Path) -> Option<SystemTime> {
    std::fs::symlink_metadata(p).ok()?.modified().ok()
}

fn subdirs(p: &Path) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = std::fs::read_dir(p).map_or(vec![], |rd| rd.flatten().map(|e| e.path()).collect());
    v.sort();
    v
}

impl Cleaner {
    fn age(&self, t: Option<SystemTime>) -> Duration {
        t.and_then(|t| self.now.duration_since(t).ok()).unwrap_or_default()
    }

    fn stale(&self, t: Option<SystemTime>) -> bool {
        self.policy.all || self.age(t) >= self.policy.older_than
    }

    /// Deletes `path` (a tree or a file) and adds its size to the report.
    fn delete(&mut self, path: &Path) {
        self.report.bytes += size_of_tree(path);
        if self.policy.dry_run {
            return;
        }
        if path.is_dir() {
            let _ = git::remove_tree(path);
        } else {
            let _ = std::fs::remove_file(path);
        }
    }

    /// Removes a half-written temporary entry if it is old enough; true if it was one.
    fn tmp(&mut self, path: &Path) -> bool {
        if !name_of(path).starts_with(".tmp-") {
            return false;
        }
        if self.policy.all || self.age(modified(path)) >= TMP_MAX_AGE {
            self.delete(path);
        }
        true
    }

    /// Cleans a directory of `<repo>/<tree>` package trees; returns the repositories that
    /// keep a tree.
    fn trees(&mut self, root: &Path) -> Vec<String> {
        let mut kept = vec![];
        for repo in subdirs(root) {
            if self.tmp(&repo) || !repo.is_dir() {
                continue;
            }
            let mut left = 0;
            for entry in subdirs(&repo) {
                let name = name_of(&entry);
                if self.tmp(&entry) {
                    continue;
                }
                if name.ends_with(".used") {
                    // A stamp whose tree is gone.
                    if !repo.join(name.trim_end_matches(".used")).is_dir() {
                        self.delete(&entry);
                    }
                    continue;
                }
                if entry.is_dir() && !self.stale(git::last_used(&entry)) {
                    left += 1;
                    continue;
                }
                if entry.is_dir() {
                    self.report.checkouts += 1;
                    self.delete(&entry);
                    self.delete(&git::stamp_of(&entry));
                }
            }
            if left == 0 && !self.policy.dry_run {
                let _ = std::fs::remove_dir_all(&repo);
            }
            if left > 0 {
                kept.push(name_of(&repo));
            }
        }
        kept
    }

    fn mirrors(&mut self, db: &Path, kept: &[String]) {
        for mirror in subdirs(db) {
            if self.tmp(&mirror) || !mirror.is_dir() {
                continue;
            }
            if self.policy.all || !kept.contains(&name_of(&mirror)) {
                self.report.mirrors += 1;
                self.delete(&mirror);
            }
        }
    }

    /// Cleans the files of `dir` whose names start with one of `prefixes`, by their
    /// modification times.
    fn builds(&mut self, dir: &Path, prefixes: &[&str]) {
        for f in subdirs(dir) {
            let name = name_of(&f);
            if !prefixes.iter().any(|p| name.starts_with(p)) {
                continue;
            }
            if name.contains(".tmp-") {
                if self.policy.all || self.age(modified(&f)) >= TMP_MAX_AGE {
                    self.delete(&f);
                }
                continue;
            }
            // The runtime's source goes with its object.
            if name.ends_with(".c") && f.with_extension("o").is_file() {
                continue;
            }
            if !self.stale(modified(&f)) {
                continue;
            }
            if !name.ends_with(".c") {
                self.report.builds += 1;
            }
            self.delete(&f);
            if name.ends_with(".o") && name.starts_with("gm_rt-") {
                self.delete(&f.with_extension("c"));
            }
        }
    }
}

/// Cleans the cache directory `dir` as `policy` says.
pub fn clean(dir: &Path, policy: &Policy) -> Result<Report, String> {
    if !dir.is_dir() {
        return Ok(Report::default());
    }
    let mut c = Cleaner { policy: policy.clone(), now: SystemTime::now(), report: Report::default() };
    let kept_git = c.trees(&dir.join("git").join("checkouts"));
    c.mirrors(&dir.join("git").join("db"), &kept_git);
    c.trees(&dir.join("git").join("url"));
    c.builds(&dir.join("run"), &["prog-"]);
    c.builds(&dir.join("native"), &["obj-", "lib-"]);
    c.builds(dir, &["gm_rt-"]);
    if policy.all && !policy.dry_run {
        for sub in ["git", "run", "native"] {
            let _ = std::fs::remove_dir_all(dir.join(sub));
        }
    }
    Ok(c.report)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gamma-cache-test-{}-{}", std::process::id(), name));
        let _ = git::remove_tree(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn age(p: &Path, days: u64) {
        let t = SystemTime::now() - Duration::from_secs(days * 86400);
        std::fs::File::open(p).unwrap().set_modified(t).unwrap();
    }

    fn policy(days: u64, all: bool) -> Policy {
        Policy { older_than: Duration::from_secs(days * 86400), all, dry_run: false }
    }

    #[test]
    fn sizes_read_well() {
        assert_eq!(human_size(10), "10 B");
        assert_eq!(human_size(1536), "1.5 KiB");
        assert_eq!(human_size(3 << 20), "3.0 MiB");
    }

    #[test]
    fn removes_old_trees_mirrors_and_builds() {
        let d = tmp("clean");
        for p in [
            "git/checkouts/r-1/aaa",
            "git/checkouts/r-1/bbb",
            "git/checkouts/s-2/ccc",
            "git/db/r-1",
            "git/db/s-2",
            "git/db/t-3",
        ] {
            std::fs::create_dir_all(d.join(p)).unwrap();
            std::fs::write(d.join(p).join("f"), "data").unwrap();
        }
        git::make_read_only(&d.join("git/checkouts"));
        // `aaa` and `ccc` are stale; `bbb` was used just now.
        git::mark_used(&d.join("git/checkouts/r-1/aaa"));
        age(&git::stamp_of(&d.join("git/checkouts/r-1/aaa")), 40);
        git::mark_used(&d.join("git/checkouts/r-1/bbb"));
        git::mark_used(&d.join("git/checkouts/s-2/ccc"));
        age(&git::stamp_of(&d.join("git/checkouts/s-2/ccc")), 31);
        std::fs::create_dir_all(d.join("run")).unwrap();
        std::fs::write(d.join("run/prog-old"), "x").unwrap();
        std::fs::write(d.join("run/prog-new"), "x").unwrap();
        age(&d.join("run/prog-old"), 60);
        std::fs::write(d.join("gm_rt-1.o"), "x").unwrap();
        std::fs::write(d.join("gm_rt-1.c"), "x").unwrap();
        age(&d.join("gm_rt-1.o"), 60);
        std::fs::write(d.join("other.txt"), "keep").unwrap();

        let mut p = policy(30, false);
        p.dry_run = true;
        let dry = clean(&d, &p).unwrap();
        assert!(d.join("git/checkouts/r-1/aaa").exists() && d.join("run/prog-old").exists());
        let r = clean(&d, &policy(30, false)).unwrap();
        assert_eq!((r.checkouts, r.mirrors, r.builds), (2, 2, 2), "{:?}", r);
        assert_eq!(r, dry);
        assert!(r.bytes > 0);
        assert!(!d.join("git/checkouts/r-1/aaa").exists() && !d.join("git/checkouts/r-1/aaa.used").exists());
        assert!(d.join("git/checkouts/r-1/bbb").is_dir());
        assert!(!d.join("git/checkouts/s-2").exists());
        // s-2 has no checkout left, t-3 never had one; r-1 keeps its mirror.
        assert!(d.join("git/db/r-1").is_dir() && !d.join("git/db/s-2").exists() && !d.join("git/db/t-3").exists());
        assert!(!d.join("run/prog-old").exists() && d.join("run/prog-new").exists());
        assert!(!d.join("gm_rt-1.o").exists() && !d.join("gm_rt-1.c").exists());
        assert!(d.join("other.txt").exists());

        let r = clean(&d, &policy(30, true)).unwrap();
        assert_eq!(r.checkouts, 1);
        assert!(!d.join("git").exists() && !d.join("run").exists() && d.join("other.txt").exists());
        assert_eq!(clean(&d, &policy(30, true)).unwrap(), Report::default());
        let _ = std::fs::remove_dir_all(&d);
    }
}
