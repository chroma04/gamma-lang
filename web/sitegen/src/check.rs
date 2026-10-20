//! Link checking of the written site: relative `href`s must point to existing files, and
//! `#fragment`s into generated pages to existing ids.

use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};

/// All files below `dir`, recursively.
pub fn walk(dir: &Path) -> Vec<PathBuf> {
    let mut files = Vec::new();
    if let Ok(entries) = fs::read_dir(dir) {
        for e in entries.flatten() {
            let p = e.path();
            if p.is_dir() {
                files.extend(walk(&p));
            } else {
                files.push(p);
            }
        }
    }
    files.sort();
    files
}

/// The values of attribute `name` (as in `name="…"`) in an HTML text.
fn attr_values<'a>(html: &'a str, name: &str) -> Vec<&'a str> {
    let needle = format!(" {name}=\"");
    let mut values = Vec::new();
    let mut rest = html;
    while let Some(i) = rest.find(&needle) {
        rest = &rest[i + needle.len()..];
        if let Some(end) = rest.find('"') {
            values.push(&rest[..end]);
            rest = &rest[end..];
        }
    }
    values
}

/// Normalizes `a/b/../c` without touching the file system; `None` if it climbs above the start.
fn normalize(path: &Path) -> Option<PathBuf> {
    let mut out = PathBuf::new();
    for c in path.components() {
        match c {
            std::path::Component::ParentDir => {
                if !out.pop() {
                    return None;
                }
            }
            std::path::Component::CurDir => {}
            c => out.push(c),
        }
    }
    Some(out)
}

/// Checks every HTML page under `out`; returns one message per broken link.
pub fn check_site(out: &Path) -> Vec<String> {
    let mut problems = Vec::new();
    let mut ids: HashMap<PathBuf, HashSet<String>> = HashMap::new();
    let pages: Vec<PathBuf> = walk(out).into_iter().filter(|p| p.extension().is_some_and(|e| e == "html")).collect();
    for page in &pages {
        let html = fs::read_to_string(page).unwrap_or_default();
        let shown = page.strip_prefix(out).unwrap_or(page).display().to_string();
        for href in attr_values(&html, "href") {
            let href = href.replace("&amp;", "&");
            let skip = href.is_empty()
                || href.starts_with("//")
                || href.split(['/', '?', '#']).next().is_some_and(|h| h.contains(':'));
            if skip {
                continue;
            }
            let (path, frag) = match href.split_once('#') {
                Some((p, f)) => (p, Some(f)),
                None => (href.as_str(), None),
            };
            let path = path.split('?').next().unwrap_or("");
            let target = if path.is_empty() {
                page.clone()
            } else {
                let joined = page.parent().unwrap_or(Path::new("")).join(path);
                let Some(mut t) = normalize(&joined).filter(|t| t.starts_with(out)) else {
                    problems.push(format!("{shown}: link {href} leaves the site"));
                    continue;
                };
                if t.is_dir() {
                    t.push("index.html");
                }
                t
            };
            if !target.is_file() {
                problems.push(format!("{shown}: broken link {href}"));
                continue;
            }
            // Fragments with `=` are application state (the playground's `#code=…`), not ids.
            let Some(frag) = frag.filter(|f| !f.is_empty() && !f.contains('=')) else { continue };
            if target.extension().is_none_or(|e| e != "html") {
                continue;
            }
            let known = ids.entry(target.clone()).or_insert_with(|| {
                let text = fs::read_to_string(&target).unwrap_or_default();
                attr_values(&text, "id").into_iter().chain(attr_values(&text, "name")).map(String::from).collect()
            });
            if !known.contains(frag) {
                problems.push(format!("{shown}: missing anchor in link {href}"));
            }
        }
    }
    problems
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_broken_links() {
        let dir = std::env::temp_dir().join(format!("sitegen-check-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(dir.join("a")).unwrap();
        fs::write(dir.join("index.html"), "<a href=\"a/p.html#x\">ok</a> <a href=\"a/p.html#nope\">b</a> <a href=\"gone.html\">c</a> <a href=\"https://x.org\">d</a>").unwrap();
        fs::write(dir.join("a/p.html"), "<h2 id=\"x\"></h2><a href=\"../index.html\">up</a> <a href=\"#x\">self</a>")
            .unwrap();
        let problems = check_site(&dir);
        let _ = fs::remove_dir_all(&dir);
        assert_eq!(problems.len(), 2, "{problems:?}");
        assert!(problems[0].contains("nope") && problems[1].contains("gone.html"));
    }
}
