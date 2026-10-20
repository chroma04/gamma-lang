//! Static site generator for the Gamma website.
//!
//! Renders the repository's Markdown documentation and the hand-written HTML fragments in
//! `web/pages/` into the layout `web/layout.html`, copies `web/static/` to `assets/`, writes
//! the playground's example list and finally checks all internal links.
//!
//! Usage: `gamma-sitegen --out <dir> [--root <repo dir>]` (the root defaults to the current
//! directory).

mod check;
mod examples;
mod highlight;
mod links;
mod markdown;
mod pages;

use markdown::Context;
use pages::{Doc, Site};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::ExitCode;

fn main() -> ExitCode {
    match run() {
        Ok(true) => ExitCode::SUCCESS,
        Ok(false) => ExitCode::FAILURE,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}

/// Parses the arguments and builds the site. Returns whether the link check was clean.
fn run() -> Result<bool, String> {
    let mut out: Option<PathBuf> = None;
    let mut root = PathBuf::from(".");
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--out" => out = Some(args.next().ok_or("--out needs a directory")?.into()),
            "--root" => root = args.next().ok_or("--root needs a directory")?.into(),
            _ => return Err(format!("unknown argument `{arg}`\nusage: gamma-sitegen --out <dir> [--root <repo dir>]")),
        }
    }
    let out = out.ok_or("missing --out <dir>")?;
    let pages = build(&root, &out)?;
    println!("wrote {pages} pages to {}", out.display());
    let problems = check::check_site(&out);
    for p in &problems {
        eprintln!("{p}");
    }
    if !problems.is_empty() {
        eprintln!("{} broken links", problems.len());
    }
    Ok(problems.is_empty())
}

/// Writes `text` to `out/rel`, creating directories.
fn write(out: &Path, rel: &str, text: &str) -> Result<(), String> {
    let path = out.join(rel);
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    }
    fs::write(&path, text).map_err(|e| format!("{}: {e}", path.display()))
}

/// Repo-relative paths of the Markdown files that become pages.
fn markdown_sources(root: &Path) -> Vec<String> {
    let mut found: Vec<String> = ["gamma-spec.md", "CHANGELOG.md", "SECURITY.md"]
        .iter()
        .filter(|f| root.join(f).is_file())
        .map(|f| f.to_string())
        .collect();
    for path in check::walk(&root.join("docs")) {
        if path.extension().is_some_and(|e| e == "md")
            && let Ok(rel) = path.strip_prefix(root)
        {
            found.push(rel.to_string_lossy().replace('\\', "/"));
        }
    }
    found.retain(|rel| links::page_for(rel).is_some());
    found
}

/// Builds the whole site into `out` and returns the number of HTML pages written.
fn build(root: &Path, out: &Path) -> Result<usize, String> {
    let layout = fs::read_to_string(root.join("web/layout.html")).map_err(|e| format!("web/layout.html: {e}"))?;

    // Static assets.
    let static_dir = root.join("web/static");
    for file in check::walk(&static_dir) {
        let rel = file.strip_prefix(&static_dir).map_err(|e| e.to_string())?;
        let dest = out.join("assets").join(rel);
        fs::create_dir_all(dest.parent().unwrap_or(out)).map_err(|e| e.to_string())?;
        fs::copy(&file, &dest).map_err(|e| format!("{}: {e}", file.display()))?;
    }
    write(out, "assets/examples.js", &examples::build(root)?)?;

    // Markdown pages.
    let is_dir = |rel: &str| root.join(rel).is_dir();
    let mut docs = Vec::new();
    for src in markdown_sources(root) {
        let target = links::page_for(&src).ok_or("unmapped source")?;
        let text = fs::read_to_string(root.join(&src)).map_err(|e| format!("{src}: {e}"))?;
        let rendered = markdown::render(&text, &Context { src: &src, out: &target.out, is_dir: &is_dir });
        let title = rendered.headings.iter().find(|h| h.level == 1).or(rendered.headings.first()).map_or_else(
            || src.trim_end_matches(".md").rsplit('/').next().unwrap_or("Gamma").to_string(),
            |h| h.text.clone(),
        );
        docs.push(Doc { src, target, rendered, title });
    }
    let site = Site::new(docs);
    let mut count = 0;
    for (i, doc) in site.docs.iter().enumerate() {
        let root_prefix = links::root_prefix(&doc.target.out);
        let html = pages::apply_layout(&layout, &doc.meta(), &root_prefix, &site.doc_body(i));
        write(out, &doc.target.out, &html)?;
        count += 1;
    }

    // Hand-written fragments.
    let pages_dir = root.join("web/pages");
    for file in check::walk(&pages_dir) {
        if file.extension().is_none_or(|e| e != "html") {
            continue;
        }
        let rel = file.strip_prefix(&pages_dir).map_err(|e| e.to_string())?.to_string_lossy().replace('\\', "/");
        let text = fs::read_to_string(&file).map_err(|e| format!("{}: {e}", file.display()))?;
        let (meta, body) = pages::parse_fragment(&text);
        let root_prefix = links::root_prefix(&rel);
        let body = pages::highlight_fragment_code(&body.replace("{{root}}", &root_prefix));
        write(out, &rel, &pages::apply_layout(&layout, &meta, &root_prefix, &body))?;
        count += 1;
    }
    Ok(count)
}
