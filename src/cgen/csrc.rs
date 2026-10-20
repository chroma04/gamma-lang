//! The C sources and libraries that packages add to a build (`c-sources`, `include-dirs`,
//! `libs` and `lib-dirs` of `gamma.toml`).
//!
//! Each C file is compiled to an object once per change: the object is kept in the cache
//! directory (`native/obj-<hash>.o`) under a name derived from the *preprocessed* file
//! (so a changed header counts), the compiler and all flags. A native build links the
//! objects into the executable ([`Link`]); the interpreter loads them as a shared
//! library (`native/lib-<hash>.so`, `.dylib` on macOS) it can call into ([`load_for_interpreter`]).
//!
//! Without a cache directory the objects are compiled into a temporary directory that
//! lives as long as the [`Link`].

use super::Options;
use super::rtcache::{cache_dir, create_private_dir, create_private_dirs, private_name, rename_or_remove};
use crate::toolchain::Toolchain;
use sha2::{Digest, Sha256};
use std::path::PathBuf;

/// One C file of a package, with the include directories of that package.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CSource {
    pub path: PathBuf,
    pub include_dirs: Vec<PathBuf>,
}

/// Everything the packages of a program add to a native build, with absolute paths.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Extras {
    pub sources: Vec<CSource>,
    /// Libraries to link (`-l<name>`), beyond those of `foreign` blocks.
    pub libs: Vec<String>,
    /// Directories searched for those libraries (`-L`).
    pub lib_dirs: Vec<PathBuf>,
}

impl Extras {
    pub fn is_empty(&self) -> bool {
        self.sources.is_empty() && self.libs.is_empty() && self.lib_dirs.is_empty()
    }
}

/// What the link step of a native build adds: compiled C sources and libraries.
#[derive(Debug, Default)]
pub struct Link {
    pub objects: Vec<PathBuf>,
    pub libs: Vec<String>,
    pub lib_dirs: Vec<PathBuf>,
    /// A directory for the objects when there is no cache, removed with the link.
    temp: Option<PathBuf>,
}

impl Drop for Link {
    fn drop(&mut self) {
        if let Some(t) = &self.temp {
            let _ = std::fs::remove_dir_all(t);
        }
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

impl Link {
    /// Describes everything in the link that can change the executable, for the key of
    /// the executable cache: the objects (named by their content hash), the libraries
    /// and the size and modification time of the library files found in the search
    /// directories. `more` are the libraries of `foreign` blocks and `ldflags` the linker
    /// options, whose `-L` and `-l` count like the package's own.
    pub fn fingerprint(&self, more: &[String], ldflags: &[String]) -> String {
        let mut s = String::new();
        for o in &self.objects {
            s.push_str(&format!("obj {}\n", o.display()));
        }
        let mut libs: Vec<String> = more.iter().chain(&self.libs).cloned().collect();
        let mut dirs = self.lib_dirs.clone();
        let mut it = ldflags.iter();
        while let Some(w) = it.next() {
            for (prefix, is_dir) in [("-L", true), ("-l", false)] {
                if let Some(v) = w.strip_prefix(prefix) {
                    let v = if v.is_empty() { it.next().map(String::as_str).unwrap_or("") } else { v };
                    if is_dir {
                        dirs.push(PathBuf::from(v));
                    } else {
                        libs.push(v.to_string());
                    }
                }
            }
        }
        if let Some(p) = std::env::var_os("LIBRARY_PATH") {
            dirs.extend(std::env::split_paths(&p));
        }
        s.push_str(&library_stamps(&libs, &dirs));
        s
    }

    /// The linker arguments for the libraries: `-L` and run-time search directories, then
    /// `-l<name>` for each library of `foreign` blocks (`more`) and of the packages.
    pub fn library_args(&self, more: &[String]) -> Vec<String> {
        let mut args = vec![];
        for d in &self.lib_dirs {
            args.push(format!("-L{}", d.display()));
            args.push(format!("-Wl,-rpath,{}", d.display()));
        }
        let mut seen: Vec<&String> = vec![];
        for l in more.iter().chain(&self.libs) {
            if !seen.contains(&l) {
                seen.push(l);
                args.push(format!("-l{}", l));
            }
        }
        args
    }
}

/// The size and modification time of the files that may be the libraries `libs`.
fn library_stamps(libs: &[String], dirs: &[PathBuf]) -> String {
    let mut s = String::new();
    for l in libs {
        for d in dirs {
            for ext in ["a", "so", "dylib"] {
                let p = d.join(format!("lib{}.{}", l, ext));
                if let Ok(m) = std::fs::metadata(&p) {
                    let t = m.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok());
                    s.push_str(&format!("lib {} {} {:?}\n", p.display(), m.len(), t));
                }
            }
        }
    }
    s
}

/// The compiler options for a package's C files in a build with `opts`.
fn object_flags(opts: &Options, src: &CSource) -> Vec<String> {
    let opt = match (opts.debug, opts.debug_info) {
        (true, true) => "-O0",
        (true, false) => "-O1",
        (false, _) => "-O2",
    };
    let mut flags: Vec<String> = vec![opt.into(), "-fPIC".into(), "-pthread".into()];
    if opts.debug_info {
        flags.push("-g".into());
    }
    flags.extend(src.include_dirs.iter().map(|d| format!("-I{}", d.display())));
    flags
}

fn compiler_version(toolchain: &Toolchain) -> Result<Vec<u8>, String> {
    let out = toolchain
        .base_command()
        .arg("--version")
        .output()
        .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
    Ok(out.stdout)
}

/// A new private directory in the system's temporary directory: created here (never one
/// that exists, which another user could have prepared) and accessible to this user only.
fn temp_dir() -> Result<PathBuf, String> {
    let base = std::env::temp_dir();
    for _ in 0..100 {
        let dir = private_name(&base, "gamma-native", "d");
        match create_private_dir(&dir) {
            Ok(()) => return Ok(dir),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(e) => return Err(format!("cannot create {}: {}", dir.display(), e)),
        }
    }
    Err(format!("cannot create a temporary directory in {}", base.display()))
}

/// The directory for compiled C sources, if there is a cache.
fn native_cache() -> Option<PathBuf> {
    let d = cache_dir()?.join("native");
    create_private_dirs(&d).ok().map(|_| d)
}

/// Compiles the C sources of `extras` (or finds them in the cache) and returns what a
/// build with `opts` and `toolchain` links.
pub fn link_inputs(extras: &Extras, opts: &Options, toolchain: &Toolchain) -> Result<Link, String> {
    let mut link = Link::default();
    link.libs = extras.libs.clone();
    link.lib_dirs = extras.lib_dirs.clone();
    if extras.sources.is_empty() {
        return Ok(link);
    }
    let cache = native_cache();
    if cache.is_none() {
        link.temp = Some(temp_dir()?);
    }
    let version = compiler_version(toolchain)?;
    for (i, src) in extras.sources.iter().enumerate() {
        let flags = object_flags(opts, src);
        // The preprocessed text covers the file and every header it includes.
        let pre = toolchain
            .command(&flags)
            .arg("-E")
            .arg(&src.path)
            .output()
            .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
        if !pre.status.success() {
            return Err(format!(
                "the C compiler failed on {}:\n{}",
                src.path.display(),
                String::from_utf8_lossy(&pre.stderr)
            ));
        }
        let mut h = Sha256::new();
        for part in [
            env!("CARGO_PKG_VERSION").as_bytes(),
            toolchain.cc.join("\0").as_bytes(),
            toolchain.cflags.join("\0").as_bytes(),
            flags.join("\0").as_bytes(),
            &version,
            &pre.stdout,
        ] {
            h.update((part.len() as u64).to_le_bytes());
            h.update(part);
        }
        let key = hex(&h.finalize())[..32].to_string();
        let (dir, obj) = match (&cache, &link.temp) {
            (Some(c), _) => (c.clone(), c.join(format!("obj-{}.o", key))),
            (None, Some(t)) => (t.clone(), t.join(format!("obj-{}-{}.o", i, key))),
            (None, None) => unreachable!(),
        };
        if obj.is_file() {
            super::rtcache::touch(&obj);
        } else {
            let tmp = private_name(&dir, &format!("obj-{}", key), "o");
            let out = toolchain
                .command(&flags)
                .arg("-c")
                .arg("-o")
                .arg(&tmp)
                .arg(&src.path)
                .output()
                .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
            if !out.status.success() {
                let _ = std::fs::remove_file(&tmp);
                return Err(format!(
                    "the C compiler failed on {}:\n{}",
                    src.path.display(),
                    String::from_utf8_lossy(&out.stderr)
                ));
            }
            rename_or_remove(&tmp, &obj).ok_or_else(|| format!("cannot write {}", obj.display()))?;
        }
        link.objects.push(obj);
    }
    Ok(link)
}

/// Makes the C sources and libraries of `extras` available to the interpreter: the
/// objects are linked into a shared library (cached like the objects) that is loaded,
/// and the library directories are searched for the libraries `foreign` blocks name.
pub fn load_for_interpreter(extras: &Extras, opts: &Options, toolchain: &Toolchain) -> Result<(), String> {
    let mut dirs = extras.lib_dirs.clone();
    let mut it = toolchain.ldflags.iter();
    while let Some(w) = it.next() {
        if let Some(d) = w.strip_prefix("-L") {
            let d = if d.is_empty() { it.next().map(String::as_str).unwrap_or("") } else { d };
            if !d.is_empty() {
                dirs.push(PathBuf::from(d));
            }
        }
    }
    crate::interp::add_library_dirs(dirs);
    if extras.sources.is_empty() {
        return Ok(());
    }
    let link = link_inputs(extras, opts, toolchain)?;
    let ext = if cfg!(target_os = "macos") { "dylib" } else { "so" };
    let mut h = Sha256::new();
    for part in [
        env!("CARGO_PKG_VERSION").to_string(),
        toolchain.cc.join("\0"),
        toolchain.cflags.join("\0"),
        toolchain.ldflags.join("\0"),
        String::from_utf8_lossy(&compiler_version(toolchain)?).into_owned(),
        link.fingerprint(&[], &toolchain.ldflags),
    ] {
        h.update((part.len() as u64).to_le_bytes());
        h.update(part.as_bytes());
    }
    let key = hex(&h.finalize())[..32].to_string();
    let (dir, lib) = match (native_cache(), &link.temp) {
        (Some(c), _) => (c.clone(), c.join(format!("lib-{}.{}", key, ext))),
        (None, Some(t)) => (t.clone(), t.join(format!("lib-{}.{}", key, ext))),
        (None, None) => unreachable!(),
    };
    if lib.is_file() {
        super::rtcache::touch(&lib);
    } else {
        let tmp = private_name(&dir, &format!("lib-{}", key), ext);
        let mut flags: Vec<String> =
            vec![if cfg!(target_os = "macos") { "-dynamiclib".into() } else { "-shared".into() }];
        if cfg!(target_os = "macos") {
            // Symbols of the program itself (callbacks) are found when the library is used.
            flags.extend(["-undefined".into(), "dynamic_lookup".into()]);
        }
        let out = toolchain
            .command(&flags)
            .arg("-o")
            .arg(&tmp)
            .args(&link.objects)
            .args(&toolchain.ldflags)
            .args(link.library_args(&[]).iter().filter(|a| !a.starts_with("-Wl,-rpath")))
            .arg("-lm")
            .output()
            .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
        if !out.status.success() {
            let _ = std::fs::remove_file(&tmp);
            return Err(format!(
                "the C compiler failed linking the C sources:\n{}",
                String::from_utf8_lossy(&out.stderr)
            ));
        }
        rename_or_remove(&tmp, &lib).ok_or_else(|| format!("cannot write {}", lib.display()))?;
    }
    crate::interp::load_library(&lib)
}
