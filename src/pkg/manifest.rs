//! The package manifest, `gamma.toml`.
//!
//! One parser serves the compiler (to find import roots) and the package manager (to
//! resolve git dependencies). Errors carry the line they refer to, and the caller
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Source {
    /// A directory, relative to the package that lists it.
    Path(String),
    Git(GitSource),
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
}

/// Whether `s` can name a package: it is the first segment of an import path.
pub fn is_identifier(s: &str) -> bool {
    let mut chars = s.chars();
    chars.next().is_some_and(|c| c.is_ascii_alphabetic() || c == '_')
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
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
                if !is_identifier(n) {
                    return Err(err(
                        line,
                        format!("invalid package name `{}`: use letters, digits and `_`, not starting with a digit", n),
                    ));
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
                if !is_identifier(name) {
                    return Err(err(
                        line,
                        format!(
                            "invalid dependency name `{}`: use letters, digits and `_`, not starting with a digit",
                            name
                        ),
                    ));
                }
                let source = parse_dependency(name, item, line, &lines)?;
                m.deps.push(Dependency { name: name.to_string(), line, source });
            }
        }
        Ok(m)
    }
}

fn parse_dependency(name: &str, item: &Item, line: usize, lines: &Lines) -> Result<Source, ManifestError> {
    let Some(t) = item.as_table_like() else {
        return Err(err(
            line,
            format!("dependency `{}` must be a table: `{{ path = \"…\" }}` or `{{ git = \"…\" }}`", name),
        ));
    };
    const KEYS: [&str; 7] = ["path", "git", "version", "tag", "branch", "rev", "subdir"];
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
    match (string("path")?, string("git")?) {
        (Some(_), Some(_)) => {
            Err(err(line, format!("dependency `{}` has both `path` and `git`; use one of them", name)))
        }
        (None, None) => Err(err(line, format!("dependency `{}` needs a `path = \"…\"` or a `git = \"…\"`", name))),
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
            if let Some(s) = &subdir {
                let ok = !s.is_empty()
                    && !s.starts_with('/')
                    && s.split('/').all(|c| !c.is_empty() && c != ".." && c != "." && c != ".git");
                if !ok {
                    return Err(err(
                        line,
                        format!("`subdir` of dependency `{}` must be a relative path inside the repository", name),
                    ));
                }
            }
            Ok(Source::Git(GitSource { url, spec, subdir }))
        }
    }
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
        assert_eq!(fails("[dependencies]\n\"a-b\" = { path = \"b\" }\n").line, 2);
        assert!(fails("[package]\nname = \"1x\"\n").msg.contains("invalid package name"));
        assert!(fails("[package]\nname = \"a\"\nversion = \"x\"\n").msg.contains("invalid package version"));
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
