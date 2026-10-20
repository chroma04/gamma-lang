//! The package manifest, `gamma.toml`.
//!
//! One parser serves the compiler (to find import roots) and the package manager (to
//! resolve git and archive dependencies, and to read the build settings). Errors carry the line they refer to, and the caller
//! prints them as `gamma.toml:LINE: message`.

use semver::VersionReq;
use std::fmt;
use std::ops::Range;
use toml_edit::{Document, Item};

/// A manifest error: a message and the 1-based line it refers to.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestError {
    pub line: usize,
    pub msg: String,
}

impl fmt::Display for ManifestError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "gamma.toml:{}: {}", self.line, self.msg)
    }
}

/// What a git dependency asks of the repository.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum GitSpec {
    /// The head of the default branch.
    Default,
    /// The newest tag (`vX.Y.Z` or `X.Y.Z`) that matches a requirement.
    Version(VersionReq),
    Tag(String),
    Branch(String),
    /// A commit, by its full id or a prefix.
    Rev(String),
}

impl fmt::Display for GitSpec {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            GitSpec::Default => write!(f, "the default branch"),
            GitSpec::Version(r) => write!(f, "version `{}`", r),
            GitSpec::Tag(t) => write!(f, "tag `{}`", t),
            GitSpec::Branch(b) => write!(f, "branch `{}`", b),
            GitSpec::Rev(r) => write!(f, "rev `{}`", r),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GitSource {
    /// The URL as written in the manifest.
    pub url: String,
    pub spec: GitSpec,
    /// The directory of the package inside the repository, if it is not the root.
    pub subdir: Option<String>,
}

/// A dependency that is a source archive (`.tar.gz`) at a URL.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UrlSource {
    pub url: String,
    /// The expected SHA-256 of the archive file, lowercase hexadecimal.
    pub sha256: Option<String>,
    /// The directory of the package inside the archive (after the single top-level
    /// directory, if any, is stripped), if it is not the root.
    pub subdir: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Source {
    /// A directory, relative to the package that lists it.
    Path(String),
    Git(GitSource),
    Url(UrlSource),
}

/// The keys of a `[build]` table or a `[profile.*]` table. A key that is not written is
/// `None`; a profile replaces (does not extend) the keys it writes.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct BuildSettings {
    pub mode: Option<crate::interp::Mode>,
    pub debug_info: Option<bool>,
    pub cflags: Option<Vec<String>>,
    pub ldflags: Option<Vec<String>>,
    pub c_sources: Option<Vec<String>>,
    pub include_dirs: Option<Vec<String>>,
    pub libs: Option<Vec<String>>,
    pub lib_dirs: Option<Vec<String>>,
}

impl BuildSettings {
    /// These settings with every key `over` writes replaced by its value.
    pub fn overlay(&self, over: &BuildSettings) -> BuildSettings {
        fn pick<T: Clone>(base: &Option<T>, over: &Option<T>) -> Option<T> {
            over.clone().or_else(|| base.clone())
        }
        BuildSettings {
            mode: pick(&self.mode, &over.mode),
            debug_info: pick(&self.debug_info, &over.debug_info),
            cflags: pick(&self.cflags, &over.cflags),
            ldflags: pick(&self.ldflags, &over.ldflags),
            c_sources: pick(&self.c_sources, &over.c_sources),
            include_dirs: pick(&self.include_dirs, &over.include_dirs),
            libs: pick(&self.libs, &over.libs),
            lib_dirs: pick(&self.lib_dirs, &over.lib_dirs),
        }
    }
}

/// A named `[profile.<name>]` table.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Profile {
    pub name: String,
    pub line: usize,
    pub settings: BuildSettings,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Dependency {
    pub name: String,
    /// The line of the entry in the manifest.
    pub line: usize,
    pub source: Source,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Manifest {
    pub name: Option<String>,
    pub version: Option<String>,
    pub deps: Vec<Dependency>,
    /// The `[build]` table.
    pub build: BuildSettings,
    pub profiles: Vec<Profile>,
    /// The `[lint]` table: the level (`allow`, `warn` or `deny`) of `gamma lint` rules, in
    /// order; the rule `all` sets every rule.
    pub lint: Vec<(String, crate::lint::Level)>,
}

/// Whether `s` can name a package: it is the first segment of an import path.
pub fn is_identifier(s: &str) -> bool {
    let mut chars = s.chars();
    chars.next().is_some_and(|c| c.is_ascii_alphabetic() || c == '_')
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

/// Why `s` cannot name a package or a dependency, if it cannot: it is the first
/// segment of an import path, so it must be an identifier that is no keyword,
/// no builtin type name, and not the name of the core or standard library.
pub fn name_problem(s: &str, what: &str) -> Option<String> {
    if !is_identifier(s) {
        return Some(format!("invalid {what} name `{s}`: use letters, digits and `_`, not starting with a digit"));
    }
    if s == "_" {
        return Some(format!("invalid {what} name `_`: it is not a usable name"));
    }
    if crate::syntax::token::Keyword::from_str(s).is_some() {
        return Some(format!("invalid {what} name `{s}`: it is a keyword"));
    }
    if crate::sema::is_reserved_type_name(s) || matches!(s, "string" | "cstring" | "rune") {
        return Some(format!("invalid {what} name `{s}`: it is the name of a builtin type"));
    }
    if s == "std" || s == "core" {
        return Some(format!("invalid {what} name `{s}`: it is the name of the standard library"));
    }
    None
}

/// Why `url` cannot be the location of a dependency, if it cannot. A URL is handed to
/// `git` or `curl` as an argument, so one that starts with `-` would be read as an option.
pub fn url_problem(url: &str) -> Option<String> {
    if url.starts_with('-') {
        return Some(format!("the URL `{}` starts with `-`", url));
    }
    if let Some(c) = url.chars().find(|c| c.is_whitespace() || c.is_control()) {
        return Some(format!("the URL `{}` contains the character {:?}", url.escape_debug(), c));
    }
    None
}

/// The identity of a git repository: its URL without a trailing `/` or `.git`.
pub fn normalize_url(url: &str) -> String {
    let mut u = url.trim().trim_end_matches('/');
    if let Some(s) = u.strip_suffix(".git") {
        u = s.trim_end_matches('/');
    }
    u.to_string()
}

/// Maps byte offsets of a manifest to line numbers.
struct Lines<'a>(&'a str);

impl Lines<'_> {
    fn of(&self, span: Option<Range<usize>>) -> usize {
        let off = span.map_or(0, |s| s.start).min(self.0.len());
        1 + self.0.as_bytes()[..off].iter().filter(|&&b| b == b'\n').count()
    }
}

fn err(line: usize, msg: String) -> ManifestError {
    ManifestError { line, msg }
}

impl Manifest {
    pub fn parse(text: &str) -> Result<Manifest, ManifestError> {
        let lines = Lines(text);
        let doc = Document::parse(text).map_err(|e| err(lines.of(e.span()), e.message().to_string()))?;
        let mut m = Manifest::default();
        if let Some(pkg) = doc.get("package") {
            let Some(t) = pkg.as_table_like() else {
                return Err(err(lines.of(pkg.span()), "`package` must be a table".into()));
            };
            if let Some(item) = t.get("name") {
                let line = lines.of(item.span());
                let n = item.as_str().ok_or_else(|| err(line, "the package name must be a string".into()))?;
                if let Some(problem) = name_problem(n, "package") {
                    return Err(err(line, problem));
                }
                m.name = Some(n.to_string());
            }
            if let Some(item) = t.get("version") {
                let line = lines.of(item.span());
                let v = item.as_str().ok_or_else(|| err(line, "the package version must be a string".into()))?;
                if let Err(e) = semver::Version::parse(v) {
                    return Err(err(line, format!("invalid package version `{}`: {}", v, e)));
                }
                m.version = Some(v.to_string());
            }
        }
        if let Some(deps) = doc.get("dependencies") {
            let Some(t) = deps.as_table_like() else {
                return Err(err(lines.of(deps.span()), "`dependencies` must be a table".into()));
            };
            for (name, item) in t.iter() {
                let line = lines.of(t.get_key_value(name).and_then(|(k, _)| k.span()).or_else(|| item.span()));
                if let Some(problem) = name_problem(name, "dependency") {
                    return Err(err(line, problem));
                }
                let source = parse_dependency(name, item, line, &lines)?;
                m.deps.push(Dependency { name: name.to_string(), line, source });
            }
        }
        if let Some(b) = doc.get("build") {
            let Some(t) = b.as_table_like() else {
                return Err(err(lines.of(b.span()), "`build` must be a table".into()));
            };
            m.build = parse_build(t, "[build]", &lines)?;
        }
        if let Some(l) = doc.get("lint") {
            let Some(t) = l.as_table_like() else {
                return Err(err(lines.of(l.span()), "`lint` must be a table of rule names and levels".into()));
            };
            for (name, item) in t.iter() {
                let line = lines.of(t.get_key_value(name).and_then(|(k, _)| k.span()).or_else(|| item.span()));
                if name != "all" && crate::lint::rule(name).is_none() {
                    return Err(err(
                        line,
                        format!("unknown lint rule `{}` in [lint] (`gamma lint --rules` lists them)", name),
                    ));
                }
                let level = item.as_str().and_then(crate::lint::Level::parse).ok_or_else(|| {
                    err(line, format!("the level of `{}` in [lint] must be \"allow\", \"warn\" or \"deny\"", name))
                })?;
                m.lint.push((name.to_string(), level));
            }
        }
        if let Some(ps) = doc.get("profile") {
            let Some(t) = ps.as_table_like() else {
                return Err(err(lines.of(ps.span()), "`profile` must be a table of `[profile.<name>]` tables".into()));
            };
            for (name, item) in t.iter() {
                let line = lines.of(t.get_key_value(name).and_then(|(k, _)| k.span()).or_else(|| item.span()));
                if name.is_empty() || !name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-') {
                    return Err(err(
                        line,
                        format!("invalid profile name `{}`: use letters, digits, `-` and `_`", name),
                    ));
                }
                let Some(pt) = item.as_table_like() else {
                    return Err(err(line, format!("profile `{}` must be a table", name)));
                };
                let settings = parse_build(pt, &format!("[profile.{}]", name), &lines)?;
                m.profiles.push(Profile { name: name.to_string(), line, settings });
            }
        }
        Ok(m)
    }

    /// The build settings for the profile `profile` (`None`: the `[build]` table alone).
    /// An unknown profile is an error naming the known ones.
    pub fn build_settings(&self, profile: Option<&str>) -> Result<BuildSettings, String> {
        let Some(name) = profile else { return Ok(self.build.clone()) };
        match self.profiles.iter().find(|p| p.name == name) {
            Some(p) => Ok(self.build.overlay(&p.settings)),
            None if self.profiles.is_empty() => {
                Err(format!("unknown profile `{}`: gamma.toml defines no `[profile.<name>]` tables", name))
            }
            None => {
                let known: Vec<&str> = self.profiles.iter().map(|p| p.name.as_str()).collect();
                Err(format!("unknown profile `{}` (known profiles: {})", name, known.join(", ")))
            }
        }
    }
}

const BUILD_KEYS: [&str; 8] =
    ["mode", "debug-info", "cflags", "ldflags", "c-sources", "include-dirs", "libs", "lib-dirs"];

fn parse_build(t: &dyn toml_edit::TableLike, what: &str, lines: &Lines) -> Result<BuildSettings, ManifestError> {
    let mut b = BuildSettings::default();
    for (k, v) in t.iter() {
        let line = lines.of(t.get_key_value(k).and_then(|(k, _)| k.span()).or_else(|| v.span()));
        if !BUILD_KEYS.contains(&k) {
            return Err(err(
                line,
                format!("{} has an unknown key `{}` (known keys: {})", what, k, BUILD_KEYS.join(", ")),
            ));
        }
        let list = || -> Result<Vec<String>, ManifestError> {
            let bad = || err(line, format!("`{}` in {} must be an array of strings", k, what));
            let a = v.as_array().ok_or_else(bad)?;
            a.iter().map(|e| e.as_str().map(str::to_string).ok_or_else(bad)).collect()
        };
        match k {
            "mode" => {
                let s = v.as_str().ok_or_else(|| err(line, format!("`mode` in {} must be a string", what)))?;
                b.mode = Some(match s {
                    "debug" => crate::interp::Mode::Debug,
                    "checked" => crate::interp::Mode::Checked,
                    "safe" => crate::interp::Mode::Safe,
                    "fast" => crate::interp::Mode::Fast,
                    _ => {
                        return Err(err(
                            line,
                            format!("invalid mode `{}` in {}: use debug, checked, safe or fast", s, what),
                        ));
                    }
                });
            }
            "debug-info" => {
                b.debug_info = Some(
                    v.as_bool().ok_or_else(|| err(line, format!("`debug-info` in {} must be true or false", what)))?,
                );
            }
            "cflags" => b.cflags = Some(list()?),
            "ldflags" => b.ldflags = Some(list()?),
            "c-sources" | "include-dirs" | "lib-dirs" => {
                let l = list()?;
                if l.iter().any(|p| p.is_empty()) {
                    return Err(err(line, format!("`{}` in {} holds an empty path", k, what)));
                }
                match k {
                    "c-sources" => b.c_sources = Some(l),
                    "include-dirs" => b.include_dirs = Some(l),
                    _ => b.lib_dirs = Some(l),
                }
            }
            _ => {
                let l = list()?;
                let bad = |n: &&String| {
                    n.is_empty() || n.starts_with('-') || n.contains(|c: char| c.is_whitespace() || c == '/')
                };
                if let Some(n) = l.iter().find(bad) {
                    return Err(err(
                        line,
                        format!("invalid library name `{}` in {}: write `z`, not `-lz` or a path", n, what),
                    ));
                }
                b.libs = Some(l);
            }
        }
    }
    Ok(b)
}

fn parse_dependency(name: &str, item: &Item, line: usize, lines: &Lines) -> Result<Source, ManifestError> {
    let Some(t) = item.as_table_like() else {
        return Err(err(
            line,
            format!("dependency `{}` must be a table: `{{ path = \"…\" }}` or `{{ git = \"…\" }}`", name),
        ));
    };
    const KEYS: [&str; 9] = ["path", "git", "url", "sha256", "version", "tag", "branch", "rev", "subdir"];
    for (k, v) in t.iter() {
        if !KEYS.contains(&k) {
            let l = lines.of(t.get_key_value(k).and_then(|(k, _)| k.span()).or_else(|| v.span()));
            return Err(err(l, format!("dependency `{}` has an unknown key `{}`", name, k)));
        }
    }
    let string = |key: &str| -> Result<Option<String>, ManifestError> {
        match t.get(key) {
            None => Ok(None),
            Some(v) => match v.as_str() {
                Some(s) => Ok(Some(s.to_string())),
                None => Err(err(lines.of(v.span()), format!("`{}` of dependency `{}` must be a string", key, name))),
            },
        }
    };
    let sources: Vec<&str> = ["path", "git", "url"].into_iter().filter(|k| t.get(k).is_some()).collect();
    if sources.len() > 1 {
        return Err(err(
            line,
            format!("dependency `{}` has both `{}` and `{}`; use one of them", name, sources[0], sources[1]),
        ));
    }
    if let Some(url) = string("url")? {
        for k in ["version", "tag", "branch", "rev"] {
            if t.get(k).is_some() {
                return Err(err(line, format!("`{}` of dependency `{}` needs `git`", k, name)));
            }
        }
        if url.trim().is_empty() {
            return Err(err(line, format!("the `url` of dependency `{}` is empty", name)));
        }
        if let Some(p) = url_problem(url.trim()) {
            return Err(err(line, format!("invalid `url` of dependency `{}`: {}", name, p)));
        }
        let sha256 = match string("sha256")? {
            None => None,
            Some(h) => {
                let h = h.trim().strip_prefix("sha256:").unwrap_or(h.trim()).to_ascii_lowercase();
                if h.len() != 64 || !h.chars().all(|c| c.is_ascii_hexdigit()) {
                    return Err(err(
                        lines.of(t.get("sha256").and_then(Item::span)),
                        format!("`sha256` of dependency `{}` must be 64 hexadecimal digits", name),
                    ));
                }
                Some(h)
            }
        };
        let subdir = string("subdir")?;
        check_subdir(name, &subdir, line)?;
        return Ok(Source::Url(UrlSource { url: url.trim().to_string(), sha256, subdir }));
    }
    if t.get("sha256").is_some() {
        return Err(err(line, format!("`sha256` of dependency `{}` needs `url`", name)));
    }
    match (string("path")?, string("git")?) {
        (Some(_), Some(_)) => unreachable!(),
        (None, None) => {
            Err(err(line, format!("dependency `{}` needs a `path = \"…\"`, a `git = \"…\"` or a `url = \"…\"`", name)))
        }
        (Some(p), None) => {
            for k in ["tag", "branch", "rev", "subdir"] {
                if t.get(k).is_some() {
                    return Err(err(line, format!("`{}` of dependency `{}` needs `git`", k, name)));
                }
            }
            // A `version` next to a path is informational.
            string("version")?;
            if p.is_empty() {
                return Err(err(line, format!("the `path` of dependency `{}` is empty", name)));
            }
            Ok(Source::Path(p))
        }
        (None, Some(url)) => {
            if normalize_url(&url).is_empty() {
                return Err(err(line, format!("the `git` URL of dependency `{}` is empty", name)));
            }
            if let Some(p) = url_problem(url.trim()) {
                return Err(err(line, format!("invalid `git` URL of dependency `{}`: {}", name, p)));
            }
            let present: Vec<&str> =
                ["version", "tag", "branch", "rev"].into_iter().filter(|k| t.get(k).is_some()).collect();
            if present.len() > 1 {
                return Err(err(
                    line,
                    format!(
                        "dependency `{}` combines `{}` and `{}`; use only one of version, tag, branch, rev",
                        name, present[0], present[1]
                    ),
                ));
            }
            let spec = if let Some(v) = string("version")? {
                let req = VersionReq::parse(v.trim()).map_err(|e| {
                    err(
                        lines.of(t.get("version").and_then(Item::span)),
                        format!("invalid version requirement `{}`: {}", v, e),
                    )
                })?;
                GitSpec::Version(req)
            } else if let Some(v) = string("tag")? {
                GitSpec::Tag(v)
            } else if let Some(v) = string("branch")? {
                GitSpec::Branch(v)
            } else if let Some(v) = string("rev")? {
                if v.is_empty() || !v.chars().all(|c| c.is_ascii_hexdigit()) {
                    return Err(err(line, format!("`rev` of dependency `{}` must be a hexadecimal commit id", name)));
                }
                GitSpec::Rev(v.to_ascii_lowercase())
            } else {
                GitSpec::Default
            };
            let subdir = string("subdir")?;
            check_subdir(name, &subdir, line)?;
            Ok(Source::Git(GitSource { url, spec, subdir }))
        }
    }
}

/// Whether `s` is a relative path inside a repository, usable as a `subdir`.
pub fn is_subdir(s: &str) -> bool {
    !s.is_empty() && !s.starts_with('/') && s.split('/').all(|c| !c.is_empty() && c != ".." && c != "." && c != ".git")
}

fn check_subdir(name: &str, subdir: &Option<String>, line: usize) -> Result<(), ManifestError> {
    if let Some(s) = subdir
        && !is_subdir(s)
    {
        return Err(err(
            line,
            format!("`subdir` of dependency `{}` must be a relative path inside the repository", name),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dep(text: &str) -> Source {
        Manifest::parse(&format!("[dependencies]\n{}\n", text)).unwrap().deps.remove(0).source
    }

    fn spec(text: &str) -> GitSpec {
        match dep(text) {
            Source::Git(g) => g.spec,
            s => panic!("{:?}", s),
        }
    }

    fn fails(text: &str) -> ManifestError {
        Manifest::parse(text).unwrap_err()
    }

    #[test]
    fn path_dependencies() {
        let m = Manifest::parse(
            "[package]\nname = \"app\"  # comment\nversion = \"0.1.0\"\n\n[dependencies]\njson = { path = \"../json\", version = \"1\" }\n",
        )
        .unwrap();
        assert_eq!(m.name.as_deref(), Some("app"));
        assert_eq!(m.version.as_deref(), Some("0.1.0"));
        assert_eq!(m.deps.len(), 1);
        assert_eq!(m.deps[0].source, Source::Path("../json".into()));
        assert_eq!(m.deps[0].line, 6);
    }

    #[test]
    fn git_dependencies() {
        assert_eq!(
            spec("h = { git = \"https://x/h\", version = \"1.2\" }"),
            GitSpec::Version(VersionReq::parse("^1.2").unwrap())
        );
        assert_eq!(spec("h = { git = \"u\", tag = \"v1\" }"), GitSpec::Tag("v1".into()));
        assert_eq!(spec("h = { git = \"u\", branch = \"main\" }"), GitSpec::Branch("main".into()));
        assert_eq!(spec("h = { git = \"u\", rev = \"ABC123\" }"), GitSpec::Rev("abc123".into()));
        assert_eq!(spec("h = { git = \"u\" }"), GitSpec::Default);
        assert!(
            matches!(dep("h = { git = \"u\", subdir = \"a/b\" }"), Source::Git(g) if g.subdir.as_deref() == Some("a/b"))
        );
        // The table form.
        let m = Manifest::parse("[dependencies.h]\ngit = \"u\"\ntag = \"t\"\n").unwrap();
        assert!(matches!(&m.deps[0].source, Source::Git(g) if g.spec == GitSpec::Tag("t".into())));
    }

    #[test]
    fn errors_have_lines() {
        assert!(fails("[package\n").line >= 1);
        let e = fails("[dependencies]\n\nx = \"1.0\"\n");
        assert_eq!(e.line, 3);
        assert!(e.msg.contains("must be a table"));
        let e = fails("[dependencies]\nx = { path = \"a\", color = \"red\" }\n");
        assert_eq!((e.line, e.msg.as_str()), (2, "dependency `x` has an unknown key `color`"));
        assert_eq!(e.to_string(), "gamma.toml:2: dependency `x` has an unknown key `color`");
        let e = fails("[dependencies]\nx = { path = \"a\", git = \"b\" }\n");
        assert!(e.msg.contains("both `path` and `git`"));
        let e = fails("[dependencies]\nx = { git = \"b\", tag = \"t\", branch = \"m\" }\n");
        assert!(e.msg.contains("use only one"));
        assert!(fails("[dependencies]\nx = {}\n").msg.contains("needs"));
        assert!(fails("[dependencies]\nx = { git = \"b\", version = \"abc\" }\n").msg.contains("invalid version"));
        assert!(fails("[dependencies]\nx = { git = \"b\", rev = \"zz\" }\n").msg.contains("hexadecimal"));
        assert!(fails("[dependencies]\nx = { git = \"b\", subdir = \"../a\" }\n").msg.contains("subdir"));
        assert!(fails("[dependencies]\nx = { path = \"b\", tag = \"v\" }\n").msg.contains("needs `git`"));
        assert!(fails("[dependencies]\nx = { git = \"--upload-pack=touch x\" }\n").msg.contains("starts with `-`"));
        assert!(fails("[dependencies]\nx = { git = \" -u\" }\n").msg.contains("starts with `-`"));
        assert!(fails("[dependencies]\nx = { git = \"a b\" }\n").msg.contains("contains the character"));
        assert!(fails("[dependencies]\nx = { url = \"-K/etc/x\" }\n").msg.contains("starts with `-`"));
        assert_eq!(fails("[dependencies]\n\"a-b\" = { path = \"b\" }\n").line, 2);
        assert!(fails("[package]\nname = \"1x\"\n").msg.contains("invalid package name"));
        assert!(fails("[package]\nname = \"a\"\nversion = \"x\"\n").msg.contains("invalid package version"));
    }

    #[test]
    fn build_tables_and_profiles() {
        let m = Manifest::parse(
            "[build]\nmode = \"checked\"\ndebug-info = true\ncflags = [\"-O2\"]\nlibs = [\"z\"]\nc-sources = [\"c/a.c\"]\n\n[profile.release]\nmode = \"fast\"\ncflags = [\"-DX\"]\n",
        )
        .unwrap();
        assert_eq!(m.build.mode, Some(crate::interp::Mode::Checked));
        assert_eq!(m.build.debug_info, Some(true));
        assert_eq!(m.profiles.len(), 1);
        assert_eq!((m.profiles[0].name.as_str(), m.profiles[0].line), ("release", 8));
        // No profile: [build] alone. A profile replaces the keys it writes.
        assert_eq!(m.build_settings(None).unwrap(), m.build);
        let r = m.build_settings(Some("release")).unwrap();
        assert_eq!(r.mode, Some(crate::interp::Mode::Fast));
        assert_eq!(r.cflags, Some(vec!["-DX".to_string()]));
        assert_eq!(r.libs, Some(vec!["z".to_string()]));
        assert_eq!(r.debug_info, Some(true));
        let e = m.build_settings(Some("nope")).unwrap_err();
        assert!(e.contains("unknown profile `nope`") && e.contains("release"), "{}", e);
        assert!(Manifest::default().build_settings(Some("x")).unwrap_err().contains("defines no"));
        assert_eq!(fails("[build]\nspeed = 1\n").line, 2);
        assert!(fails("[profile.p]\nmode = \"warp\"\n").msg.contains("invalid mode"));
    }

    #[test]
    fn url_dependencies() {
        let hash = "AB".repeat(32);
        let src = dep(&format!("u = {{ url = \"https://h/u-1.tar.gz\", sha256 = \"{}\", subdir = \"p\" }}", hash));
        assert_eq!(
            src,
            Source::Url(UrlSource {
                url: "https://h/u-1.tar.gz".into(),
                sha256: Some("ab".repeat(32)),
                subdir: Some("p".into())
            })
        );
        assert!(matches!(dep("u = { url = \"x\" }"), Source::Url(UrlSource { sha256: None, subdir: None, .. })));
        assert!(fails("[dependencies]\nu = { url = \"x\", sha256 = \"12\" }\n").msg.contains("64 hexadecimal"));
        assert!(fails("[dependencies]\nu = { url = \"x\", path = \"p\" }\n").msg.contains("both"));
        assert!(fails("[dependencies]\nu = { sha256 = \"12\", path = \"p\" }\n").msg.contains("needs `url`"));
    }

    #[test]
    fn identifiers_and_urls() {
        assert!(is_identifier("json"));
        assert!(is_identifier("_a1"));
        assert!(!is_identifier("1a"));
        assert!(!is_identifier("a-b"));
        assert!(!is_identifier(""));
        assert_eq!(normalize_url("https://h/x/y.git/"), "https://h/x/y");
        assert_eq!(normalize_url("https://h/x/y/"), "https://h/x/y");
        assert_eq!(normalize_url("/tmp/r.git"), "/tmp/r");
    }
}
