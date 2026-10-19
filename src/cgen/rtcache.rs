//! The cache directory, and in it the separately compiled runtime objects and the
//! built programs.
//!
//! Compiling the runtime is most of the work of compiling a small program, so Debug
//! builds (and Safe ones) link with a runtime object compiled once (see [`super::compile`]).
//! Objects are
//! kept in the cache directory under a name derived from everything that affects them:
//! the runtime's source, the C compiler (its command with its arguments, `CFLAGS` and its
//! `--version` output) and the flags. The directory is `$GAMMA_CACHE_DIR` if set (an empty value turns the cache
//! off), else `$XDG_CACHE_HOME/gamma`, else `~/Library/Caches/gamma` on macOS and
//! `~/.cache/gamma` elsewhere; on Windows `%LOCALAPPDATA%\gamma` (`%USERPROFILE%` if that is
//! unset) when `XDG_CACHE_HOME` is not set.
//!
//! [`cached_program`] keeps whole executables in `run/` under a key that covers everything that
//! affects them, for `gamma run --backend=c`; the most recent few are kept.
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
    if cfg!(windows) {
        let base = std::env::var_os("LOCALAPPDATA").or_else(|| std::env::var_os("USERPROFILE"));
        return Some(PathBuf::from(base.filter(|d| !d.is_empty())?).join("gamma"));
    }
    let home = PathBuf::from(std::env::var_os("HOME").filter(|d| !d.is_empty())?);
    Some(if cfg!(target_os = "macos") { home.join("Library/Caches/gamma") } else { home.join(".cache/gamma") })
}

/// The runtime object for `toolchain` and `flags`, compiled now if it is not in the
/// cache. `None` if there is no cache or the object cannot be made; the caller then
/// compiles the runtime with the program.
pub fn runtime_object(toolchain: &Toolchain, flags: &[String]) -> Option<PathBuf> {
    runtime_object_from(toolchain, flags, &super::separate_runtime_source(), "gm_rt")
}

/// The object of the C file `source` compiled with `toolchain` and `flags`, kept in the
/// cache under a name starting with `prefix`; compiled now if it is not there. `None` if
/// there is no cache or the object cannot be made.
pub fn runtime_object_from(toolchain: &Toolchain, flags: &[String], source: &str, prefix: &str) -> Option<PathBuf> {
    let dir = cache_dir()?;
    let version = toolchain.base_command().arg("--version").output().ok().filter(|o| o.status.success())?;
    let mut h = std::collections::hash_map::DefaultHasher::new();
    (env!("CARGO_PKG_VERSION"), source, &toolchain.cc, &toolchain.cflags, flags, &version.stdout).hash(&mut h);
    let stem = format!("{prefix}-{:016x}", h.finish());
    let obj = dir.join(format!("{stem}.o"));
    if obj.is_file() {
        // Mark it as recently used, so the cache cleaner keeps it.
        touch(&obj);
        return Some(obj);
    }
    std::fs::create_dir_all(&dir).ok()?;
    // The source stays next to the object (a debugger reads it); writing it is idempotent
    // but goes through a private file too, so a concurrent build never compiles a partial one.
    let c_file = dir.join(format!("{stem}.c"));
    if !c_file.is_file() {
        let tmp = private_name(&dir, &stem, "c");
        std::fs::write(&tmp, source).ok()?;
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
    let exe = dir.join(if generated.target.os == "Windows" { format!("{stem}.exe") } else { stem.clone() });
    if exe.is_file() {
        // Mark it as recently used, so pruning keeps it.
        touch(&exe);
        return Ok(Some(exe));
    }
    if std::fs::create_dir_all(&dir).is_err() {
        return Ok(None);
    }
    let tmp = private_name(&dir, &stem, "exe");
    let built = super::compile(generated, toolchain, link, &tmp);
    for i in 0..super::units::MAX_UNITS {
        let _ = std::fs::remove_file(super::units::unit_path(&tmp, i));
    }
    built?;
    if rename_or_remove(&tmp, &exe).is_none() {
        return Ok(None);
    }
    prune(&dir);
    Ok(Some(exe))
}

/// Sets the modification time of the cached file `p` to now, so the cache cleaner keeps
/// it. Windows sets times only through a handle opened for writing. Elsewhere a read-only
/// handle does, and opening for writing must be avoided: `p` may be an executable that
/// another process is running, and on macOS opening a running signed program for
/// writing can get that process killed.
pub(crate) fn touch(p: &Path) {
    let now = std::time::SystemTime::now();
    let f = if cfg!(windows) {
        std::fs::OpenOptions::new().write(true).open(p).or_else(|_| std::fs::File::open(p))
    } else {
        std::fs::File::open(p)
    };
    let _ = f.and_then(|f| f.set_modified(now));
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

// Marking a cached file as used needs no write access to it outside Windows (see `touch`).
#[cfg(all(test, unix))]
mod tests {
    use super::*;

    #[test]
    fn touch_marks_a_read_only_file_as_used() {
        use std::os::unix::fs::PermissionsExt;
        let dir = std::env::temp_dir().join(format!("gamma-touch-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let p = dir.join("prog");
        std::fs::write(&p, b"x").unwrap();
        let old = std::time::SystemTime::UNIX_EPOCH + std::time::Duration::from_secs(1_000_000_000);
        std::fs::File::open(&p).unwrap().set_modified(old).unwrap();
        std::fs::set_permissions(&p, std::fs::Permissions::from_mode(0o555)).unwrap();
        touch(&p);
        let t = std::fs::metadata(&p).unwrap().modified().unwrap();
        std::fs::remove_dir_all(&dir).unwrap();
        assert!(t > old + std::time::Duration::from_secs(86400), "{t:?}");
    }
}
