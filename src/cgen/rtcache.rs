//! The cache of separately compiled runtime objects.
//!
//! Compiling the runtime is most of the work of compiling a small program, so Debug
//! builds link with a runtime object compiled once (see [`super::compile`]). Objects are
//! kept in the cache directory under a name derived from everything that affects them:
//! the runtime's source, the C compiler (its command with its arguments, `CFLAGS` and its
//! `--version` output) and the flags. The directory is `$GAMMA_CACHE_DIR` if set (an empty value turns the cache
//! off), else `$XDG_CACHE_HOME/gamma`, else `~/Library/Caches/gamma` on macOS and
//! `~/.cache/gamma` elsewhere.
//!
//! Concurrent builds may compile the same object at once: each writes a file of its
//! own and renames it into place, so a reader never sees a partial object.

use crate::toolchain::Toolchain;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};

/// The cache directory, or `None` if there is none.
pub fn cache_dir() -> Option<PathBuf> {
    if let Some(dir) = std::env::var_os("GAMMA_CACHE_DIR") {
        return if dir.is_empty() { None } else { Some(PathBuf::from(dir)) };
    }
    if let Some(dir) = std::env::var_os("XDG_CACHE_HOME").filter(|d| !d.is_empty()) {
        return Some(PathBuf::from(dir).join("gamma"));
    }
    let home = PathBuf::from(std::env::var_os("HOME").filter(|d| !d.is_empty())?);
    Some(if cfg!(target_os = "macos") { home.join("Library/Caches/gamma") } else { home.join(".cache/gamma") })
}

/// The runtime object for `toolchain` and `flags`, compiled now if it is not in the
/// cache. `None` if there is no cache or the object cannot be made; the caller then
/// compiles the runtime with the program.
pub fn runtime_object(toolchain: &Toolchain, flags: &[String]) -> Option<PathBuf> {
    let dir = cache_dir()?;
    let version = toolchain.base_command().arg("--version").output().ok().filter(|o| o.status.success())?;
    let source = super::separate_runtime_source();
    let mut h = std::collections::hash_map::DefaultHasher::new();
    (env!("CARGO_PKG_VERSION"), &source, &toolchain.cc, &toolchain.cflags, flags, &version.stdout).hash(&mut h);
    let stem = format!("gm_rt-{:016x}", h.finish());
    let obj = dir.join(format!("{stem}.o"));
    if obj.is_file() {
        // Mark it as recently used, so the cache cleaner keeps it.
        let _ = std::fs::File::open(&obj).and_then(|f| f.set_modified(std::time::SystemTime::now()));
        return Some(obj);
    }
    std::fs::create_dir_all(&dir).ok()?;
    // The source stays next to the object (a debugger reads it); writing it is idempotent
    // but goes through a private file too, so a concurrent build never compiles a partial one.
    let c_file = dir.join(format!("{stem}.c"));
    if !c_file.is_file() {
        let tmp = private_name(&dir, &stem, "c");
        std::fs::write(&tmp, &source).ok()?;
        rename_or_remove(&tmp, &c_file)?;
    }
    let tmp = private_name(&dir, &stem, "o");
    let ok =
        toolchain.command(flags).arg("-c").arg("-o").arg(&tmp).arg(&c_file).output().is_ok_and(|o| o.status.success());
    if !ok {
        let _ = std::fs::remove_file(&tmp);
        return None;
    }
    rename_or_remove(&tmp, &obj)?;
    Some(obj)
}

/// A file name in `dir` no other process uses.
pub(super) fn private_name(dir: &Path, stem: &str, ext: &str) -> PathBuf {
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    dir.join(format!("{stem}.tmp-{}-{nanos:x}.{ext}", std::process::id()))
}

/// Renames `from` to `to` (replacing it), removing `from` if that fails.
pub(super) fn rename_or_remove(from: &Path, to: &Path) -> Option<()> {
    if std::fs::rename(from, to).is_ok() {
        return Some(());
    }
    let _ = std::fs::remove_file(from);
    None
}

/// How many executables `gamma run --backend=c` keeps; older ones are removed.
const KEPT_PROGRAMS: usize = 32;

/// A cached executable of `generated` built with `toolchain`, built now if it is not in
/// the cache; `Ok(None)` if there is no cache. The key covers everything that affects the
/// executable: the generated code and its options, the libraries, the target, the
/// compiler and its flags, and what `link` adds (the package's C objects, which are named
/// by a hash of their preprocessed source and flags, and its libraries).
pub fn cached_program(
    generated: &super::Generated,
    toolchain: &Toolchain,
    link: &super::Link,
) -> Result<Option<PathBuf>, String> {
    let Some(dir) = cache_dir().map(|d| d.join("run")) else { return Ok(None) };
    let Some(version) = toolchain.base_command().arg("--version").output().ok().filter(|o| o.status.success()) else {
        return Ok(None);
    };
    let mut h = std::collections::hash_map::DefaultHasher::new();
    (env!("CARGO_PKG_VERSION"), super::RT_H, super::RT_C, &generated.code, &generated.libs).hash(&mut h);
    (format!("{:?}", generated.opts), format!("{:?}", generated.target)).hash(&mut h);
    (&toolchain.cc, &toolchain.cflags, &toolchain.ldflags, &version.stdout).hash(&mut h);
    (link.fingerprint(), &link.libs, &link.lib_dirs).hash(&mut h);
    let stem = format!("prog-{:016x}", h.finish());
    let exe = dir.join(&stem);
    if exe.is_file() {
        // Mark it as recently used, so pruning keeps it.
        let _ = std::fs::File::open(&exe).and_then(|f| f.set_modified(std::time::SystemTime::now()));
        return Ok(Some(exe));
    }
    if std::fs::create_dir_all(&dir).is_err() {
        return Ok(None);
    }
    let tmp = private_name(&dir, &stem, "exe");
    let built = super::compile(generated, toolchain, link, &tmp);
    let _ = std::fs::remove_file(tmp.with_extension("c"));
    built?;
    if rename_or_remove(&tmp, &exe).is_none() {
        return Ok(None);
    }
    prune(&dir);
    Ok(Some(exe))
}

/// Removes all but the most recently used `KEPT_PROGRAMS` executables in `dir`, sparing
/// any used in the last ten minutes (another process may be about to run it).
fn prune(dir: &Path) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    let mut progs: Vec<(std::time::SystemTime, PathBuf)> = rd
        .flatten()
        .filter(|e| {
            e.file_name().to_string_lossy().starts_with("prog-") && !e.file_name().to_string_lossy().contains(".tmp-")
        })
        .filter_map(|e| Some((e.metadata().ok()?.modified().ok()?, e.path())))
        .collect();
    if progs.len() <= KEPT_PROGRAMS {
        return;
    }
    progs.sort();
    let recent = std::time::SystemTime::now() - std::time::Duration::from_secs(600);
    for (t, p) in &progs[..progs.len() - KEPT_PROGRAMS] {
        if *t < recent {
            let _ = std::fs::remove_file(p);
        }
    }
}
