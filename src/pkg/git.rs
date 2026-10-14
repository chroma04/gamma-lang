//! Git access through the `git` command-line tool, the package store on disk and the
//! content hash of a checkout.
//!
//! Layout under the store directory (`<cache>/git`):
//!
//! ```text
//! db/<name>-<16 hex of sha256(url)>/                 bare mirror of a repository
//! checkouts/<name>-<16 hex>/<12-char rev>/           a checked-out revision
//! ```

use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use std::process::Command;

/// The directories where fetched packages live.
#[derive(Debug, Clone)]
pub struct Store {
    base: PathBuf,
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

/// A directory name for a repository: the last segment of its URL and a hash of the URL.
fn repo_dir_name(url: &str) -> String {
    let last = url.rsplit(['/', ':']).find(|s| !s.is_empty()).unwrap_or("repo");
    let clean: String = last
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' || c == '.' { c } else { '_' })
        .collect();
    let hash = hex(&Sha256::digest(url.as_bytes()));
    format!("{}-{}", clean.trim_start_matches('.'), &hash[..16])
}

impl Store {
    pub fn new(base: PathBuf) -> Store {
        Store { base }
    }

    /// The store for the package at `root`: under the cache directory, or, when the
    /// cache is disabled, in `<root>/.gamma/deps`.
    pub fn for_package(root: &Path) -> Store {
        match crate::cgen::cache_dir() {
            Some(d) => Store::new(d.join("git")),
            None => Store::new(root.join(".gamma").join("deps")),
        }
    }

    /// The bare mirror of the repository with the normalized URL `url`.
    pub fn db(&self, url: &str) -> PathBuf {
        self.base.join("db").join(repo_dir_name(url))
    }

    /// Where revision `rev` of the repository is checked out.
    pub fn checkout(&self, url: &str, rev: &str) -> PathBuf {
        self.base.join("checkouts").join(repo_dir_name(url)).join(&rev[..rev.len().min(12)])
    }
}

fn git(git_dir: Option<&Path>, args: &[&str], env: &[(&str, &Path)]) -> Result<String, String> {
    let mut cmd = Command::new("git");
    if let Some(d) = git_dir {
        cmd.arg("--git-dir").arg(d);
    }
    cmd.args(args).env("GIT_TERMINAL_PROMPT", "0");
    for (k, v) in env {
        cmd.env(k, v);
    }
    let out = cmd.output().map_err(|e| format!("cannot run `git` (is it installed?): {}", e))?;
    if out.status.success() {
        Ok(String::from_utf8_lossy(&out.stdout).into_owned())
    } else {
        let err = String::from_utf8_lossy(&out.stderr);
        let sub = args.iter().find(|a| !a.starts_with('-')).unwrap_or(&"git");
        Err(format!("`git {}` failed: {}", sub, err.trim()))
    }
}

/// Branches, tags (peeled to commits) and the default branch's head of a mirror.
#[derive(Debug, Default, Clone)]
pub struct Refs {
    pub tags: Vec<(String, String)>,
    pub branches: Vec<(String, String)>,
    pub head: Option<String>,
}

/// Brings the mirror `db` of `url` up to date, creating it if needed.
pub fn fetch_mirror(db: &Path, url: &str) -> Result<(), String> {
    if !db.join("HEAD").is_file() {
        if let Some(p) = db.parent() {
            std::fs::create_dir_all(p).map_err(|e| format!("cannot create {}: {}", p.display(), e))?;
        }
        git(None, &["init", "-q", "--bare", &db.to_string_lossy()], &[])?;
    }
    git(
        Some(db),
        &["fetch", "-q", "--tags", "--force", url, "+refs/heads/*:refs/heads/*", "+HEAD:refs/gamma/head"],
        &[],
    )
    .map_err(|e| format!("cannot fetch {}: {}", url, e))?;
    Ok(())
}

pub fn list_refs(db: &Path) -> Result<Refs, String> {
    let out = git(
        Some(db),
        &[
            "for-each-ref",
            "--format=%(refname)%09%(objectname)%09%(*objectname)",
            "refs/heads",
            "refs/tags",
            "refs/gamma",
        ],
        &[],
    )?;
    let mut refs = Refs::default();
    for line in out.lines() {
        let mut parts = line.split('\t');
        let (Some(name), Some(obj)) = (parts.next(), parts.next()) else { continue };
        let target = parts.next().filter(|s| !s.is_empty()).unwrap_or(obj).to_string();
        if let Some(t) = name.strip_prefix("refs/tags/") {
            refs.tags.push((t.to_string(), target));
        } else if let Some(b) = name.strip_prefix("refs/heads/") {
            refs.branches.push((b.to_string(), target));
        } else if name == "refs/gamma/head" {
            refs.head = Some(target);
        }
    }
    Ok(refs)
}

/// The full commit id for `rev` (an id prefix), if the mirror has exactly one.
pub fn rev_parse(db: &Path, rev: &str) -> Result<Option<String>, String> {
    let spec = format!("{}^{{commit}}", rev);
    match git(Some(db), &["rev-parse", "--verify", "--quiet", &spec], &[]) {
        Ok(s) => Ok(Some(s.trim().to_string()).filter(|s| s.len() == 40 || s.len() == 64)),
        Err(_) => Ok(None),
    }
}

/// The contents of `path` at `rev`, if it exists there.
pub fn show_file(db: &Path, rev: &str, path: &str) -> Option<String> {
    git(Some(db), &["cat-file", "blob", &format!("{}:{}", rev, path)], &[]).ok()
}

/// The names of the tags of the repository at `url`, without cloning it.
pub fn remote_tags(url: &str) -> Result<Vec<String>, String> {
    let out =
        git(None, &["ls-remote", "--tags", "--refs", url], &[]).map_err(|e| format!("cannot list {}: {}", url, e))?;
    Ok(out.lines().filter_map(|l| l.split('\t').nth(1)?.strip_prefix("refs/tags/").map(str::to_string)).collect())
}

/// Checks out `rev` into `dest`, through a temporary directory so that a failure never
/// leaves a partial checkout. Does nothing if `dest` exists.
pub fn checkout(db: &Path, rev: &str, dest: &Path) -> Result<(), String> {
    if dest.is_dir() {
        return Ok(());
    }
    let parent = dest.parent().ok_or("bad checkout path")?;
    std::fs::create_dir_all(parent).map_err(|e| format!("cannot create {}: {}", parent.display(), e))?;
    let tag = format!(".tmp-{}-{}", std::process::id(), &rev[..12.min(rev.len())]);
    let tmp = parent.join(&tag);
    let index = parent.join(format!("{}.index", tag));
    let _ = std::fs::remove_dir_all(&tmp);
    std::fs::create_dir_all(&tmp).map_err(|e| format!("cannot create {}: {}", tmp.display(), e))?;
    let res = git(
        Some(db),
        &["-c", "core.bare=false", "--work-tree", &tmp.to_string_lossy(), "checkout", "-q", "-f", rev, "--", "."],
        &[("GIT_INDEX_FILE", &index)],
    );
    let _ = std::fs::remove_file(&index);
    if let Err(e) = res {
        let _ = std::fs::remove_dir_all(&tmp);
        return Err(e);
    }
    if let Err(e) = std::fs::rename(&tmp, dest) {
        let _ = std::fs::remove_dir_all(&tmp);
        // Another process may have finished the same checkout first.
        if !dest.is_dir() {
            return Err(format!("cannot create {}: {}", dest.display(), e));
        }
    }
    Ok(())
}

fn collect_files(root: &Path, rel: &str, out: &mut Vec<(String, PathBuf)>) -> std::io::Result<()> {
    for e in std::fs::read_dir(root)? {
        let e = e?;
        let name = e.file_name().to_string_lossy().into_owned();
        if name == ".git" {
            continue;
        }
        let path = e.path();
        let rel = if rel.is_empty() { name } else { format!("{}/{}", rel, name) };
        let ty = e.file_type()?;
        if ty.is_dir() {
            collect_files(&path, &rel, out)?;
        } else if ty.is_file() {
            out.push((rel, path));
        }
    }
    Ok(())
}

/// The content hash of the tree at `root`: SHA-256 over its regular files in sorted
/// path order (`/` separators, `.git` excluded), each as the path, a NUL byte, the
/// length as 8 little-endian bytes, and the contents. Written `sha256:<hex>`.
pub fn tree_checksum(root: &Path) -> Result<String, String> {
    let mut files = vec![];
    collect_files(root, "", &mut files).map_err(|e| format!("cannot read {}: {}", root.display(), e))?;
    files.sort();
    let mut h = Sha256::new();
    for (rel, path) in files {
        let data = std::fs::read(&path).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
        h.update(rel.as_bytes());
        h.update([0u8]);
        h.update((data.len() as u64).to_le_bytes());
        h.update(&data);
    }
    Ok(format!("sha256:{}", hex(&h.finalize())))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gamma-git-test-{}-{}", std::process::id(), name));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn checksum_is_stable_and_content_sensitive() {
        let d = tmp("sum");
        std::fs::create_dir_all(d.join("sub")).unwrap();
        std::fs::write(d.join("a.gm"), "x").unwrap();
        std::fs::write(d.join("sub/b.gm"), "y").unwrap();
        std::fs::create_dir_all(d.join(".git")).unwrap();
        std::fs::write(d.join(".git/config"), "ignored").unwrap();
        let a = tree_checksum(&d).unwrap();
        assert_eq!(a, tree_checksum(&d).unwrap());
        assert!(a.starts_with("sha256:") && a.len() == 7 + 64);
        std::fs::write(d.join(".git/config"), "still ignored").unwrap();
        assert_eq!(a, tree_checksum(&d).unwrap());
        std::fs::write(d.join("sub/b.gm"), "z").unwrap();
        assert_ne!(a, tree_checksum(&d).unwrap());
        // The hash covers names as well as contents.
        std::fs::write(d.join("sub/b.gm"), "y").unwrap();
        std::fs::rename(d.join("a.gm"), d.join("c.gm")).unwrap();
        assert_ne!(a, tree_checksum(&d).unwrap());
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn checksum_of_a_known_tree() {
        // sha256("f" NUL 0x0100000000000000 "ab") computed independently.
        let d = tmp("known");
        std::fs::write(d.join("f"), "ab").unwrap();
        let mut h = Sha256::new();
        h.update(b"f\0");
        h.update(2u64.to_le_bytes());
        h.update(b"ab");
        assert_eq!(tree_checksum(&d).unwrap(), format!("sha256:{}", hex(&h.finalize())));
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn store_paths() {
        let s = Store::new(PathBuf::from("/c/git"));
        let db = s.db("https://github.com/x/http-gm");
        let name = db.file_name().unwrap().to_string_lossy().into_owned();
        assert!(name.starts_with("http-gm-") && name.len() == "http-gm-".len() + 16, "{}", name);
        assert_eq!(s.db("https://github.com/x/http-gm"), db);
        assert_ne!(s.db("https://github.com/y/http-gm"), db);
        let co = s.checkout("https://github.com/x/http-gm", &"ab".repeat(20));
        assert!(co.starts_with("/c/git/checkouts") && co.ends_with("abababababab"));
    }
}
