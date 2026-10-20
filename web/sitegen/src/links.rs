//! Mapping between source files and output pages, and relative link computation.

/// Where a source Markdown file ends up.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Target {
    /// Output path relative to the site root, e.g. `guide/02-basics.html`.
    pub out: String,
    /// Navigation section: `guide`, `spec`, `std` or `docs`.
    pub section: &'static str,
}

/// The output page for a repo-relative Markdown path, or `None` if it is not rendered.
/// `README.md` is not rendered, but links to it are handled by [`rewrite_link`].
pub fn page_for(rel: &str) -> Option<Target> {
    let stem = rel.strip_suffix(".md")?;
    let (out, section) = match rel {
        "gamma-spec.md" => ("spec".to_string(), "spec"),
        "CHANGELOG.md" => ("changelog".to_string(), "docs"),
        "SECURITY.md" => ("docs/security".to_string(), "docs"),
        "docs/std.md" => ("docs/std".to_string(), "docs"),
        _ if rel.starts_with("docs/guide/") => (stem.strip_prefix("docs/")?.to_string(), "guide"),
        _ if rel.starts_with("docs/std/") => (stem.strip_prefix("docs/")?.to_string(), "std"),
        _ if rel.starts_with("docs/") && !rel[5..].contains('/') => (stem.to_string(), "docs"),
        _ => return None,
    };
    Some(Target { out: format!("{out}.html"), section })
}

/// Joins `link` onto the directory of `from` (both `/`-separated, repo-relative) and
/// normalizes `.` and `..`. A leading `/` means the repo root. `None` if it escapes the root.
pub fn resolve(from: &str, link: &str) -> Option<String> {
    let mut parts: Vec<&str> = Vec::new();
    if !link.starts_with('/') {
        parts.extend(from.split('/'));
        parts.pop();
    }
    for seg in link.split('/') {
        match seg {
            "" | "." => {}
            ".." => {
                parts.pop()?;
            }
            s => parts.push(s),
        }
    }
    Some(parts.join("/"))
}

/// The prefix that leads from a page at `out` back to the site root: `""`, `"../"`, ...
pub fn root_prefix(out: &str) -> String {
    "../".repeat(out.matches('/').count())
}

/// The path of `to` relative to the page `from` (both relative to the site root).
pub fn relative(from: &str, to: &str) -> String {
    let from_dirs: Vec<&str> = from.split('/').collect();
    let from_dirs = &from_dirs[..from_dirs.len() - 1];
    let to_parts: Vec<&str> = to.split('/').collect();
    let mut common = 0;
    while common < from_dirs.len() && common + 1 < to_parts.len() && from_dirs[common] == to_parts[common] {
        common += 1;
    }
    let mut s = "../".repeat(from_dirs.len() - common);
    s.push_str(&to_parts[common..].join("/"));
    s
}

/// True for links that leave the site or stay on the page and need no rewriting.
fn is_external(dest: &str) -> bool {
    dest.is_empty() || dest.starts_with('#') || dest.starts_with("//") || {
        // A URL scheme: letters, then `:` before any `/`.
        let head = dest.split(['/', '?', '#']).next().unwrap_or("");
        head.contains(':')
    }
}

/// Where the repository's files live on GitHub.
const GITHUB: &str = "https://github.com/chroma04/gamma-lang";

/// Rewrites a link found in the Markdown file `src` (repo-relative) for the page `cur_out`.
/// Returns the new destination and, when the link names another rendered page or file in the
/// repo, its resolved repo-relative path. `is_dir` tells whether a repo path is a directory.
pub fn rewrite_link(dest: &str, src: &str, cur_out: &str, is_dir: &dyn Fn(&str) -> bool) -> (String, Option<String>) {
    if is_external(dest) {
        return (dest.to_string(), None);
    }
    let (path, frag) = match dest.find('#') {
        Some(i) => (&dest[..i], &dest[i..]),
        None => (dest, ""),
    };
    let Some(resolved) = resolve(src, path) else { return (dest.to_string(), None) };
    if resolved == "README.md" {
        return (format!("{}index.html{frag}", root_prefix(cur_out)), Some(resolved));
    }
    if let Some(t) = page_for(&resolved) {
        return (format!("{}{frag}", relative(cur_out, &t.out)), Some(resolved));
    }
    let kind = if path.ends_with('/') || resolved.is_empty() || is_dir(&resolved) { "tree" } else { "blob" };
    let url = if resolved.is_empty() { GITHUB.to_string() } else { format!("{GITHUB}/{kind}/{resolved}") };
    (format!("{url}{frag}"), Some(resolved))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mapping() {
        assert_eq!(page_for("gamma-spec.md").unwrap().out, "spec.html");
        assert_eq!(page_for("docs/guide/README.md").unwrap().out, "guide/README.html");
        assert_eq!(page_for("docs/std/index.md").unwrap().section, "std");
        assert_eq!(page_for("docs/std/hash/sha256.md").unwrap().out, "std/hash/sha256.html");
        assert_eq!(page_for("docs/std.md").unwrap().out, "docs/std.html");
        assert_eq!(page_for("docs/lint.md").unwrap().out, "docs/lint.html");
        assert_eq!(page_for("CHANGELOG.md").unwrap().out, "changelog.html");
        assert_eq!(page_for("README.md"), None);
        assert_eq!(page_for("lib/x.md"), None);
    }

    #[test]
    fn relative_paths() {
        assert_eq!(relative("guide/a.html", "guide/b.html"), "b.html");
        assert_eq!(relative("guide/a.html", "spec.html"), "../spec.html");
        assert_eq!(relative("spec.html", "std/hash/x.html"), "std/hash/x.html");
        assert_eq!(relative("std/hash/x.html", "std/index.html"), "../index.html");
        assert_eq!(root_prefix("std/hash/x.html"), "../../");
        assert_eq!(root_prefix("spec.html"), "");
    }

    #[test]
    fn rewriting() {
        let dir = |p: &str| p == "lib" || p == "examples";
        let r = |d: &str, s: &str, o: &str| rewrite_link(d, s, o, &dir).0;
        let g = "https://github.com/chroma04/gamma-lang";
        assert_eq!(r("../../gamma-spec.md#x", "docs/guide/02-basics.md", "guide/02-basics.html"), "../spec.html#x");
        assert_eq!(r("03-b.md", "docs/guide/02-basics.md", "guide/02-basics.html"), "03-b.html");
        assert_eq!(r("../README.md", "docs/lint.md", "docs/lint.html"), "../index.html");
        assert_eq!(r("../lib", "docs/lint.md", "docs/lint.html"), format!("{g}/tree/lib"));
        assert_eq!(r("../src/main.rs", "docs/lint.md", "docs/lint.html"), format!("{g}/blob/src/main.rs"));
        assert_eq!(r("https://x.org/a", "docs/lint.md", "docs/lint.html"), "https://x.org/a");
        assert_eq!(r("#top", "docs/lint.md", "docs/lint.html"), "#top");
        assert_eq!(r("arena.md", "docs/std/index.md", "std/index.html"), "arena.html");
        assert_eq!(r("../std.md", "docs/std/index.md", "std/index.html"), "../docs/std.html");
    }
}
