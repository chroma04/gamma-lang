//! Source archives as dependencies: download with the `curl` command-line tool (which
//! honours proxy settings and the system's certificate authorities, as `git` does for
//! git dependencies) and unpack `.tar.gz` / `.tgz` files into the package store.

use sha2::{Digest, Sha256};
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::Command;

pub(super) fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

/// A name no other process uses, for a temporary file or directory in `dir`.
fn private_name(dir: &Path, what: &str) -> PathBuf {
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    dir.join(format!(".tmp-{}-{}-{:x}", what, std::process::id(), nanos))
}

/// An archive downloaded to a temporary file.
pub struct Download {
    pub path: PathBuf,
    /// The SHA-256 of the file, hexadecimal.
    pub sha256: String,
}

impl Drop for Download {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
    }
}

/// Downloads `url` into a temporary file in `dir`.
pub fn download(url: &str, dir: &Path) -> Result<Download, String> {
    std::fs::create_dir_all(dir).map_err(|e| format!("cannot create {}: {}", dir.display(), e))?;
    let path = private_name(dir, "download");
    let out = Command::new("curl")
        .args(["--fail", "--silent", "--show-error", "--location", "--retry", "2"])
        .args(["--proto", "=http,https,file", "--proto-redir", "=http,https"])
        .arg("--output")
        .arg(&path)
        .arg("--")
        .arg(url)
        .output()
        .map_err(|e| format!("cannot run `curl` (is it installed?): {}", e))?;
    if !out.status.success() {
        let _ = std::fs::remove_file(&path);
        return Err(format!("cannot download {}: {}", url, String::from_utf8_lossy(&out.stderr).trim()));
    }
    let download = Download { sha256: file_sha256(&path)?, path };
    Ok(download)
}

/// The SHA-256 of the file at `path`, hexadecimal.
pub fn file_sha256(path: &Path) -> Result<String, String> {
    let mut f = std::fs::File::open(path).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
    let mut h = Sha256::new();
    let mut buf = vec![0u8; 1 << 16];
    loop {
        let n = f.read(&mut buf).map_err(|e| format!("cannot read {}: {}", path.display(), e))?;
        if n == 0 {
            break;
        }
        h.update(&buf[..n]);
    }
    Ok(hex(&h.finalize()))
}

/// Unpacks the gzip-compressed tar file `archive` into the new directory `dest`.
///
/// If the archive holds a single top-level directory (as the archives of source hosts
/// do), that directory becomes `dest`. The contents are written to a temporary
/// directory next to `dest` and moved into place at the end, so a failure never leaves a
/// partial `dest`. Paths that would leave the directory, hard links and special files are
/// errors; symbolic links are allowed here and checked with the checksum of the tree.
pub fn unpack(archive: &Path, dest: &Path) -> Result<(), String> {
    let parent = dest.parent().ok_or("bad archive path")?;
    std::fs::create_dir_all(parent).map_err(|e| format!("cannot create {}: {}", parent.display(), e))?;
    let tmp = private_name(parent, "unpack");
    std::fs::create_dir_all(&tmp).map_err(|e| format!("cannot create {}: {}", tmp.display(), e))?;
    let res = unpack_into(archive, &tmp).and_then(|()| place(&tmp, dest));
    let _ = std::fs::remove_dir_all(&tmp);
    res
}

fn unpack_into(archive: &Path, dir: &Path) -> Result<(), String> {
    let mut f = std::fs::File::open(archive).map_err(|e| format!("cannot read {}: {}", archive.display(), e))?;
    let mut magic = [0u8; 2];
    if f.read_exact(&mut magic).is_err() || magic != [0x1f, 0x8b] {
        return Err("the archive is not a gzip-compressed tar file (only .tar.gz and .tgz are supported)".into());
    }
    let f = std::fs::File::open(archive).map_err(|e| format!("cannot read {}: {}", archive.display(), e))?;
    let mut tar = tar::Archive::new(flate2::read::GzDecoder::new(f));
    tar.set_preserve_mtime(false);
    tar.set_unpack_xattrs(false);
    let bad = |e: std::io::Error| format!("cannot read the archive: {}", e);
    for entry in tar.entries().map_err(bad)? {
        let mut entry = entry.map_err(bad)?;
        let kind = entry.header().entry_type();
        let name = entry.path().map_err(bad)?.display().to_string();
        if kind.is_pax_global_extensions() || kind.is_pax_local_extensions() {
            continue;
        }
        if !(kind.is_file() || kind.is_dir() || kind.is_symlink()) {
            let what = if kind.is_hard_link() { "a hard link" } else { "a special file" };
            return Err(format!("the archive contains {} (`{}`), which is not allowed", what, name));
        }
        match entry.unpack_in(dir) {
            Ok(true) => {}
            Ok(false) => return Err(format!("the archive contains the path `{}`, which leaves its directory", name)),
            Err(e) => return Err(format!("cannot unpack `{}`: {}", name, e)),
        }
    }
    super::git::sanitize_modes(dir);
    Ok(())
}

/// Moves the contents of `tmp` (or its only directory) to `dest`.
fn place(tmp: &Path, dest: &Path) -> Result<(), String> {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(tmp)
        .map_err(|e| format!("cannot read {}: {}", tmp.display(), e))?
        .flatten()
        .map(|e| e.path())
        .collect();
    let src = match entries.as_slice() {
        [one] if std::fs::symlink_metadata(one).is_ok_and(|m| m.is_dir()) => entries.pop().unwrap(),
        _ => tmp.to_path_buf(),
    };
    if let Err(e) = std::fs::rename(&src, dest) {
        // Another process may have finished the same job first.
        if !dest.is_dir() {
            return Err(format!("cannot create {}: {}", dest.display(), e));
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gamma-archive-test-{}-{}", std::process::id(), name));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn make(files: &[(&str, &str)]) -> Vec<u8> {
        let mut b = tar::Builder::new(Vec::new());
        for (p, data) in files {
            let mut h = tar::Header::new_gnu();
            h.set_size(data.len() as u64);
            h.set_mode(0o644);
            h.set_cksum();
            b.append_data(&mut h, p, data.as_bytes()).unwrap();
        }
        let tar = b.into_inner().unwrap();
        let mut gz = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::fast());
        std::io::Write::write_all(&mut gz, &tar).unwrap();
        gz.finish().unwrap()
    }

    #[test]
    fn strips_a_single_top_level_directory() {
        let d = tmp("strip");
        std::fs::write(d.join("a.tgz"), make(&[("pkg-1.0/gamma.toml", "x"), ("pkg-1.0/src/m.gm", "y")])).unwrap();
        unpack(&d.join("a.tgz"), &d.join("out")).unwrap();
        assert_eq!(std::fs::read_to_string(d.join("out/gamma.toml")).unwrap(), "x");
        assert_eq!(std::fs::read_to_string(d.join("out/src/m.gm")).unwrap(), "y");
        // Two top-level entries: nothing is stripped.
        std::fs::write(d.join("b.tgz"), make(&[("gamma.toml", "x"), ("src/m.gm", "y")])).unwrap();
        unpack(&d.join("b.tgz"), &d.join("out2")).unwrap();
        assert!(d.join("out2/gamma.toml").is_file() && d.join("out2/src/m.gm").is_file());
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn rejects_escaping_paths_and_non_archives() {
        let d = tmp("bad");
        std::fs::write(d.join("n.tgz"), b"plain text").unwrap();
        assert!(unpack(&d.join("n.tgz"), &d.join("o1")).unwrap_err().contains("gzip"));
        assert!(!d.join("o1").exists());
        let mut b = tar::Builder::new(Vec::new());
        let mut h = tar::Header::new_gnu();
        h.set_size(1);
        h.set_mode(0o644);
        let name = b"../escape";
        h.as_old_mut().name[..name.len()].copy_from_slice(name);
        h.set_cksum();
        b.append(&h, &b"z"[..]).unwrap();
        let tar = b.into_inner().unwrap();
        let mut gz = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::fast());
        std::io::Write::write_all(&mut gz, &tar).unwrap();
        std::fs::write(d.join("e.tgz"), gz.finish().unwrap()).unwrap();
        let e = unpack(&d.join("e.tgz"), &d.join("o2")).unwrap_err();
        assert!(e.contains("leaves its directory"), "{}", e);
        assert!(!d.join("o2").exists() && !d.join("escape").exists());
        let _ = std::fs::remove_dir_all(&d);
    }
}
