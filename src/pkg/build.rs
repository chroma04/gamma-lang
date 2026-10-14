//! Build settings from `gamma.toml`: the `[build]` table and the `[profile.*]` tables.
//!
//! The root package's settings (`mode`, `debug-info`, `cflags`, `ldflags`) configure the
//! compiler and the C toolchain; [`root_settings`] reads them before a session starts.
//! The settings that add C code to a native build (`c-sources`, `include-dirs`, `libs`,
//! `lib-dirs`) are collected from the root package and every dependency by [`collect`].

use super::manifest::{BuildSettings, Manifest, Source};
use super::resolve::{normalize, read_manifest};
use super::{Resolution, find_root};
use crate::cgen::{CSource, Extras};
use std::collections::HashSet;
use std::path::{Path, PathBuf};

/// The build settings of the package that contains `input`, for `profile`: the profile
/// over `[build]`. `None` if `input` is not in a package. A profile needs a package.
pub fn root_settings(input: &Path, profile: Option<&str>) -> Result<Option<BuildSettings>, String> {
    let Some(root) = find_root(input) else {
        return match profile {
            Some(p) => {
                Err(format!("`--profile={}` needs a package, but no gamma.toml was found for {}", p, input.display()))
            }
            None => Ok(None),
        };
    };
    let manifest = read_manifest(&root)?;
    let path = root.join("gamma.toml");
    manifest.build_settings(profile).map(Some).map_err(|e| format!("{}: {}", path.display(), e))
}

/// A path from a manifest of a dependency, resolved against its package: relative, and
/// inside the package.
fn dep_path(dir: &Path, rel: &str, key: &str, pkg: &str) -> Result<PathBuf, String> {
    let joined = normalize(&dir.join(rel));
    if Path::new(rel).is_absolute() || !joined.starts_with(dir) {
        return Err(format!(
            "`{}` of dependency `{}` names `{}`, which is not inside the package; paths in a dependency's [build] must be relative and stay in its directory",
            key, pkg, rel
        ));
    }
    Ok(joined)
}

struct Collector<'a> {
    resolution: Option<&'a Resolution>,
    seen: HashSet<PathBuf>,
    extras: Extras,
}

impl Collector<'_> {
    fn add_lib(&mut self, name: &str) {
        if !self.extras.libs.iter().any(|l| l == name) {
            self.extras.libs.push(name.to_string());
        }
    }

    fn add_lib_dir(&mut self, dir: PathBuf) {
        if !self.extras.lib_dirs.contains(&dir) {
            self.extras.lib_dirs.push(dir);
        }
    }

    fn visit(
        &mut self,
        dir: &Path,
        manifest: &Manifest,
        name: &str,
        settings: &BuildSettings,
        root: bool,
    ) -> Result<(), String> {
        let resolve = |rel: &str, key: &str| -> Result<PathBuf, String> {
            if root { Ok(normalize(&dir.join(rel))) } else { dep_path(dir, rel, key, name) }
        };
        let include_dirs: Vec<PathBuf> =
            settings.include_dirs.iter().flatten().map(|d| resolve(d, "include-dirs")).collect::<Result<_, _>>()?;
        for s in settings.c_sources.iter().flatten() {
            let path = resolve(s, "c-sources")?;
            if !path.is_file() {
                return Err(format!(
                    "{}: c-sources of package `{}` lists `{}`, which is not a file",
                    dir.join("gamma.toml").display(),
                    name,
                    s
                ));
            }
            let src = CSource { path, include_dirs: include_dirs.clone() };
            if !self.extras.sources.contains(&src) {
                self.extras.sources.push(src);
            }
        }
        for l in settings.libs.iter().flatten() {
            self.add_lib(l);
        }
        for d in settings.lib_dirs.iter().flatten() {
            let p = resolve(d, "lib-dirs")?;
            self.add_lib_dir(p);
        }
        for dep in &manifest.deps {
            let target = match self.resolution.and_then(|r| r.deps_of(dir)).and_then(|m| m.get(&dep.name)) {
                Some(p) => p.clone(),
                None => match &dep.source {
                    Source::Path(p) => normalize(&dir.join(p)),
                    _ => continue,
                },
            };
            let Ok(target) = target.canonicalize() else { continue };
            if !target.join("gamma.toml").is_file() || !self.seen.insert(target.clone()) {
                continue;
            }
            let m = read_manifest(&target)?;
            // A dependency contributes its `[build]` table, not its profiles.
            let settings = m.build.clone();
            self.visit(&target, &m, &dep.name, &settings, false)?;
        }
        Ok(())
    }
}

/// The C sources and libraries that the package containing `input` and all its
/// dependencies add to a native build. `resolution` locates the git and archive
/// dependencies (see [`super::prepare`]). `profile` selects the root package's profile.
pub fn collect(input: &Path, resolution: Option<&Resolution>, profile: Option<&str>) -> Result<Extras, String> {
    let Some(root) = find_root(input) else { return Ok(Extras::default()) };
    let manifest = read_manifest(&root)?;
    let settings =
        manifest.build_settings(profile).map_err(|e| format!("{}: {}", root.join("gamma.toml").display(), e))?;
    let mut c = Collector { resolution, seen: HashSet::from([root.clone()]), extras: Extras::default() };
    let name = manifest.name.clone().unwrap_or_else(|| "the root package".to_string());
    c.visit(&root, &manifest, &name, &settings, true)?;
    Ok(c.extras)
}
