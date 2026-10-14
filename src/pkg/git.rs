//! Git access through the `git` command-line tool, the package store on disk and the
//! content hash of a checkout.
//!
//! Layout under the store directory (`<cache>/git`):
//!
//! ```text
//! db/<name>-<16 hex of sha256(url)>/                 bare mirror of a repository
//! checkouts/<name>-<16 hex>/<12-char rev>/           a checked-out revision
//! checkouts/<name>-<16 hex>/<12-char rev>.used       its last use (the file's mtime)
//! url/<name>-<16 hex of sha256(url)>/<16 hex>/       an unpacked archive (with a `.used` stamp too)
//! ```
//!
//! Checkouts are read-only once verified. A directory and its `.used` stamp are the
//! unit the cache cleaner (`pkg::cache`) deletes.

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

    /// The directory archives are downloaded into before they are unpacked.
    pub fn downloads(&self) -> PathBuf {
        self.base.join("url")
    }

    /// Where the archive at `url` whose file has the SHA-256 `sha256` (hexadecimal) is
    /// unpacked.
    pub fn archive(&self, url: &str, sha256: &str) -> PathBuf {
        self.base.join("url").join(repo_dir_name(url)).join(&sha256[..sha256.len().min(16)])
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

/// What the checksum records for a path of a tree.
enum Entry {
    File(PathBuf),
    /// A symbolic link and where it points.
    Link(PathBuf),
}

fn collect_entries(root: &Path, rel: &str, out: &mut Vec<(String, Entry)>) -> Result<(), String> {
    let read_err = |p: &Path, e: std::io::Error| format!("cannot read {}: {}", p.display(), e);
    for e in std::fs::read_dir(root).map_err(|e| read_err(root, e))? {
        let e = e.map_err(|e| read_err(root, e))?;
        let name = e.file_name().to_string_lossy().into_owned();
        if name == ".git" {
            continue;
        }
        let path = e.path();
        let rel = if rel.is_empty() { name } else { format!("{}/{}", rel, name) };
        let ty = e.file_type().map_err(|e| read_err(&path, e))?;
        if ty.is_dir() {
            collect_entries(&path, &rel, out)?;
        } else if ty.is_file() {
            out.push((rel, Entry::File(path)));
        } else if ty.is_symlink() {
            let target = std::fs::read_link(&path).map_err(|e| read_err(&path, e))?;
            check_link(&rel, &target)?;
            out.push((rel, Entry::Link(target)));
        }
    }
    Ok(())
}

/// Checks that the symbolic link at `rel` (a path inside the tree) points to a relative
/// path that stays inside the tree. Absolute links and links that climb out of the
/// tree would make a package's contents depend on the machine, and a checkout is
/// shared by everything that uses it.
fn check_link(rel: &str, target: &Path) -> Result<(), String> {
    use std::path::Component;
    let outside = || {
        format!(
            "the package contains the symbolic link `{}` -> `{}`, which points outside the package; \
             links must stay inside the package directory",
            rel,
            target.display()
        )
    };
    let mut depth = rel.split('/').count() - 1;
    for c in target.components() {
        match c {
            Component::Normal(_) => depth += 1,
            Component::CurDir => {}
            Component::ParentDir => depth = depth.checked_sub(1).ok_or_else(outside)?,
            Component::RootDir | Component::Prefix(_) => return Err(outside()),
        }
    }
    Ok(())
}

/// The content hash of the tree at `root`: SHA-256 over its entries in sorted path
/// order (`/` separators, `.git` excluded). A regular file is the path, a NUL byte, the
/// length as 8 little-endian bytes, and the contents. A symbolic link is the path, a NUL
/// byte, eight `0xFF` bytes (a length no file has), the length of the link's target as 8
/// little-endian bytes, and the target; so a link never has the same record as a file.
/// Written `sha256:<hex>`. A link that points outside the tree is an error.
pub fn tree_checksum(root: &Path) -> Result<String, String> {
    let mut entries = vec![];
    collect_entries(root, "", &mut entries)?;
    entries.sort_by(|a, b| a.0.cmp(&b.0));
    let mut h = Sha256::new();
    for (rel, entry) in entries {
        h.update(rel.as_bytes());
        h.update([0u8]);
        match entry {
            Entry::File(path) => {
                let data = std::fs::read(&path).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
                h.update((data.len() as u64).to_le_bytes());
                h.update(&data);
            }
            Entry::Link(target) => {
                let t = target.as_os_str().as_encoded_bytes();
                h.update([0xFFu8; 8]);
                h.update((t.len() as u64).to_le_bytes());
                h.update(t);
            }
        }
    }
    Ok(format!("sha256:{}", hex(&h.finalize())))
}

/// Visits `path` and everything below it (not following symbolic links), calling `f` on
/// each directory before its entries and on each other entry.
fn walk_tree(path: &Path, f: &mut dyn FnMut(&Path, bool)) {
    let Ok(meta) = std::fs::symlink_metadata(path) else { return };
    if meta.is_dir() {
        f(path, true);
        if let Ok(rd) = std::fs::read_dir(path) {
            for e in rd.flatten() {
                walk_tree(&e.path(), f);
            }
        }
    } else if !meta.file_type().is_symlink() {
        f(path, false);
    }
}

#[cfg(unix)]
fn set_mode(path: &Path, f: impl Fn(u32) -> u32) {
    use std::os::unix::fs::PermissionsExt;
    if let Ok(meta) = std::fs::symlink_metadata(path) {
        let mode = meta.permissions().mode();
        let _ = std::fs::set_permissions(path, std::fs::Permissions::from_mode(f(mode)));
    }
}

/// Removes the write permission from the tree at `root`, files and directories alike,
/// so that editing a package in place fails instead of silently changing what every
/// build that uses the checkout sees.
pub fn make_read_only(root: &Path) {
    // Entries are changed before the directory that holds them.
    let mut all = vec![];
    walk_tree(root, &mut |p, _| all.push(p.to_path_buf()));
    for p in all.iter().rev() {
        #[cfg(unix)]
        set_mode(p, |m| m & !0o222);
        #[cfg(not(unix))]
        if let Ok(meta) = std::fs::metadata(p) {
            let mut perm = meta.permissions();
            perm.set_readonly(true);
            let _ = std::fs::set_permissions(p, perm);
        }
    }
}

/// Restores the owner's write permission in the tree at `root` (and read and search
/// permission of directories), as needed to delete it.
pub fn make_writable(root: &Path) {
    walk_tree(root, &mut |p, dir| {
        #[cfg(unix)]
        set_mode(p, |m| m | if dir { 0o700 } else { 0o200 });
        #[cfg(not(unix))]
        if let Ok(meta) = std::fs::metadata(p) {
            let mut perm = meta.permissions();
            #[allow(clippy::permissions_set_readonly_false)]
            perm.set_readonly(false);
            let _ = std::fs::set_permissions(p, perm);
        }
        let _ = dir;
    });
}

/// Normalizes the permissions of an unpacked tree: the owner can read and write
/// everything, and special bits (setuid, sticky) are dropped.
pub fn sanitize_modes(root: &Path) {
    #[cfg(unix)]
    walk_tree(root, &mut |p, dir| set_mode(p, |m| (m & 0o777) | if dir { 0o700 } else { 0o600 }));
    #[cfg(not(unix))]
    let _ = root;
}

/// Deletes the tree at `path`, which may be read-only.
pub fn remove_tree(path: &Path) -> std::io::Result<()> {
    make_writable(path);
    std::fs::remove_dir_all(path)
}

/// The file whose modification time is the last use of the checkout `dir`: next to it,
/// since the checkout itself is read-only.
pub fn stamp_of(dir: &Path) -> PathBuf {
    let mut name = dir.file_name().unwrap_or_default().to_os_string();
    name.push(".used");
    dir.with_file_name(name)
}

/// Records that the checkout `dir` is in use now.
pub fn mark_used(dir: &Path) {
    let now = std::time::SystemTime::now();
    let _ = std::fs::OpenOptions::new().create(true).append(true).open(stamp_of(dir)).and_then(|f| f.set_modified(now));
}

/// The time the checkout `dir` was last used: its stamp's, else its own modification
/// time.
pub fn last_used(dir: &Path) -> Option<std::time::SystemTime> {
    std::fs::metadata(stamp_of(dir)).and_then(|m| m.modified()).or_else(|_| std::fs::metadata(dir)?.modified()).ok()
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

    #[cfg(unix)]
    #[test]
    fn symlinks_are_part_of_the_checksum() {
        let d = tmp("links");
        std::fs::write(d.join("a"), "x").unwrap();
        let plain = tree_checksum(&d).unwrap();
        std::os::unix::fs::symlink("a", d.join("l")).unwrap();
        let with_link = tree_checksum(&d).unwrap();
        assert_ne!(plain, with_link);
        // The target matters.
        std::fs::remove_file(d.join("l")).unwrap();
        std::os::unix::fs::symlink("b", d.join("l")).unwrap();
        assert_ne!(with_link, tree_checksum(&d).unwrap());
        // A link never has the record of a file: a file `l` whose contents spell what a
        // link record would hold.
        std::fs::remove_file(d.join("l")).unwrap();
        let mut body = vec![0xFFu8; 8];
        body.extend(1u64.to_le_bytes());
        body.push(b'b');
        std::fs::write(d.join("l"), &body).unwrap();
        let as_file = tree_checksum(&d).unwrap();
        std::fs::remove_file(d.join("l")).unwrap();
        std::os::unix::fs::symlink("b", d.join("l")).unwrap();
        assert_ne!(as_file, tree_checksum(&d).unwrap());
        // Links that stay inside the tree are fine, even through `..`.
        std::fs::create_dir_all(d.join("s/t")).unwrap();
        std::os::unix::fs::symlink("../../a", d.join("s/t/up")).unwrap();
        tree_checksum(&d).unwrap();
        // Links that leave it are errors.
        std::os::unix::fs::symlink("../../../a", d.join("s/t/out")).unwrap();
        let e = tree_checksum(&d).unwrap_err();
        assert!(e.contains("s/t/out") && e.contains("outside"), "{}", e);
        std::fs::remove_file(d.join("s/t/out")).unwrap();
        std::os::unix::fs::symlink("/etc/passwd", d.join("abs")).unwrap();
        assert!(tree_checksum(&d).unwrap_err().contains("outside"));
        let _ = std::fs::remove_dir_all(&d);
    }

    #[cfg(unix)]
    #[test]
    fn read_only_trees_can_be_removed() {
        let d = tmp("ro");
        std::fs::create_dir_all(d.join("t/sub")).unwrap();
        std::fs::write(d.join("t/sub/f"), "x").unwrap();
        make_read_only(&d.join("t"));
        use std::os::unix::fs::PermissionsExt;
        let mode = |p: &str| std::fs::metadata(d.join(p)).unwrap().permissions().mode() & 0o777;
        assert_eq!(mode("t") & 0o222, 0);
        assert_eq!(mode("t/sub/f") & 0o222, 0);
        // The checksum is unaffected.
        tree_checksum(&d.join("t")).unwrap();
        remove_tree(&d.join("t")).unwrap();
        assert!(!d.join("t").exists());
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
