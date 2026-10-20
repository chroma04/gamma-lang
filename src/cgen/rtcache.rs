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
use std::sync::atomic::{AtomicU64, Ordering};

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
    create_private_dirs(&dir).ok()?;
    // The source stays next to the object (a debugger reads it); writing it is idempotent
    // but goes through a private file too, so a concurrent build never compiles a partial one.
    let c_file = dir.join(format!("{stem}.c"));
    if !c_file.is_file() {
        let tmp = private_name(&dir, &stem, "c");
        write_new(&tmp, source.as_bytes()).ok()?;
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

/// A file name in `dir` no other process or thread uses.
pub(super) fn private_name(dir: &Path, stem: &str, ext: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    dir.join(format!("{stem}.tmp-{}-{nanos:x}-{n}.{ext}", std::process::id()))
}

/// Creates the new directory `dir` (failing if it exists), accessible to this user only
/// where the system has such modes.
pub(super) fn create_private_dir(dir: &Path) -> std::io::Result<()> {
    private_dir_builder().create(dir)
}

/// Creates `dir` and its missing parents like [`create_private_dir`]; fine if it exists.
pub(super) fn create_private_dirs(dir: &Path) -> std::io::Result<()> {
    private_dir_builder().recursive(true).create(dir)
}

/// A builder of directories accessible to this user only (mode 0700 on Unix).
pub fn private_dir_builder() -> std::fs::DirBuilder {
    #[cfg_attr(not(unix), allow(unused_mut))]
    let mut b = std::fs::DirBuilder::new();
    #[cfg(unix)]
    std::os::unix::fs::DirBuilderExt::mode(&mut b, 0o700);
    b
}

/// Writes `data` to the file `path`, which must not exist (a symbolic link in its place is
/// not followed).
pub(super) fn write_new(path: &Path, data: &[u8]) -> std::io::Result<()> {
    use std::io::Write;
    std::fs::OpenOptions::new().write(true).create_new(true).open(path)?.write_all(data)
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
    (link.fingerprint(&generated.libs, &toolchain.ldflags), &link.libs, &link.lib_dirs).hash(&mut h);
    if generated.target.os == "Wasi" {
        // `wasm-opt` rewrites the linked module.
        super::wasi::wasm_opt_version().hash(&mut h);
    }
    let stem = format!("prog-{:016x}", h.finish());
    let exe = dir.join(match generated.target.os {
        "Windows" => format!("{stem}.exe"),
        "Wasi" => format!("{stem}.wasm"),
        _ => stem.clone(),
    });
    if exe.is_file() {
        // Mark it as recently used, so pruning keeps it.
        touch(&exe);
        return Ok(Some(exe));
    }
    if create_private_dirs(&dir).is_err() {
        return Ok(None);
    }
    let tmp = private_name(&dir, &stem, "exe");
    let built = super::compile(generated, toolchain, link, &tmp);
    for i in 0..super::units::MAX_UNITS {
        let _ = std::fs::remove_file(super::units::unit_path(&tmp, i));
    }
    if let Err(e) = built {
        // A failure after linking (a later step on the executable) leaves it behind.
        let _ = std::fs::remove_file(&tmp);
        return Err(e);
    }
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
/// any used in the last ten minutes (another process may be about to run it), and the
/// leftovers of builds that died, which are over an hour old.
fn prune(dir: &Path) {
    let Ok(rd) = std::fs::read_dir(dir) else { return };
    let stale = std::time::SystemTime::now() - std::time::Duration::from_secs(3600);
    let mut progs: Vec<(std::time::SystemTime, PathBuf)> = rd
        .flatten()
        .filter(|e| e.file_name().to_string_lossy().starts_with("prog-"))
        .filter(|e| {
            if !e.file_name().to_string_lossy().contains(".tmp-") {
                return true;
            }
            if e.metadata().and_then(|m| m.modified()).is_ok_and(|t| t < stale) {
                let _ = std::fs::remove_file(e.path());
            }
            false
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

    #[test]
    fn private_names_are_unique() {
        let dir = Path::new("/x");
        assert_ne!(private_name(dir, "s", "o"), private_name(dir, "s", "o"));
    }

    #[cfg(unix)]
    #[test]
    fn private_directories_are_new_and_owner_only() {
        use std::os::unix::fs::PermissionsExt;
        let dir = std::env::temp_dir().join(format!("gamma-private-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        create_private_dir(&dir).unwrap();
        let mode = std::fs::metadata(&dir).unwrap().permissions().mode() & 0o777;
        assert!(create_private_dir(&dir).is_err());
        let file = dir.join("f");
        write_new(&file, b"x").unwrap();
        assert!(write_new(&file, b"y").is_err());
        std::fs::remove_dir_all(&dir).unwrap();
        assert_eq!(mode, 0o700);
    }

    #[test]
    fn prune_removes_old_temporary_files_only() {
        let dir = std::env::temp_dir().join(format!("gamma-prune-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let old = dir.join("prog-1.tmp-1-2-3.exe");
        let new = dir.join("prog-2.tmp-1-2-3.exe");
        std::fs::write(&old, b"x").unwrap();
        std::fs::write(&new, b"x").unwrap();
        let t = std::time::SystemTime::now() - std::time::Duration::from_secs(7200);
        // Windows sets times only through a handle opened for writing.
        std::fs::OpenOptions::new().write(true).open(&old).unwrap().set_modified(t).unwrap();
        prune(&dir);
        let (old_left, new_left) = (old.exists(), new.exists());
        std::fs::remove_dir_all(&dir).unwrap();
        assert!(!old_left && new_left);
    }
}
