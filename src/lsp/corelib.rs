//! Files for the embedded core library, so that editors can open the declarations that
//! the compiler holds only as text compiled into its executable.
//!
//! The compiler names these sources `<core>/name.gm`. A request for the location of one
//! is answered with `lib/core/name.gm` next to the standard library, when that file exists
//! and has exactly the embedded text; otherwise the text is written once to a read-only
//! file in the cache directory (named after a hash of the text) and that file is used.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};

/// The prefix of the names the compiler gives the embedded core sources.
const PREFIX: &str = "<core>/";

/// Whether `name` (a source file's name in the compiler) is an embedded core source.
pub fn is_core(name: &str) -> bool {
    name.starts_with(PREFIX)
}

/// The directory for core files that are not in the source tree.
fn cache_root() -> PathBuf {
    crate::cgen::cache_dir().unwrap_or_else(|| std::env::temp_dir().join("gamma-cache")).join("core")
}

/// The path of a file holding `text`, the embedded source named `name` (`<core>/list.gm`).
pub fn path_for(name: &str, text: &str) -> Option<PathBuf> {
    let file = name.strip_prefix(PREFIX)?;
    if file.is_empty() || file.contains('/') || file.contains("..") {
        return None;
    }
    let in_tree = crate::sema::std_root().parent()?.join("core").join(file);
    if std::fs::read_to_string(&in_tree).is_ok_and(|t| t == text) {
        return Some(super::analysis::canon(&in_tree));
    }
    let mut h = DefaultHasher::new();
    text.hash(&mut h);
    let dir = cache_root().join(format!("{:016x}", h.finish()));
    let path = dir.join(file);
    if std::fs::read_to_string(&path).is_ok_and(|t| t == text) {
        return Some(path);
    }
    write_read_only(&dir, &path, text).ok()?;
    Some(path)
}

fn write_read_only(dir: &Path, path: &Path, text: &str) -> std::io::Result<()> {
    std::fs::create_dir_all(dir)?;
    // Write beside the final name and rename, so that a reader never sees a partial file.
    let tmp = dir.join(format!(".{}.{}", std::process::id(), path.file_name().unwrap_or_default().to_string_lossy()));
    std::fs::write(&tmp, text)?;
    let mut perms = std::fs::metadata(&tmp)?.permissions();
    perms.set_readonly(true);
    std::fs::set_permissions(&tmp, perms)?;
    std::fs::rename(&tmp, path)
}
