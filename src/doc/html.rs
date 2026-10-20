//! HTML output: one self-contained page per module (styles inline, no scripts) and an
//! index page.

use super::collect::{Decl, Kind, Module};
use super::markdown::summary;
use super::md_html::{escape, inline, render, url_path};

const STYLE: &str = "\
:root { color-scheme: light dark; --fg: #1f2328; --bg: #ffffff; --muted: #59636e; --code-bg: #f3f4f6; --rule: #d1d9e0; --link: #0550ae; }
@media (prefers-color-scheme: dark) { :root { --fg: #e6edf3; --bg: #0d1117; --muted: #9198a1; --code-bg: #161b22; --rule: #30363d; --link: #58a6ff; } }
body { margin: 0 auto; max-width: 52rem; padding: 1.5rem 1rem 4rem; background: var(--bg); color: var(--fg); font: 16px/1.55 system-ui, sans-serif; }
a { color: var(--link); }
h1, h2, h3 { line-height: 1.25; }
h2 { margin-top: 2.5rem; padding-bottom: .3rem; border-bottom: 1px solid var(--rule); }
h3 { margin-top: 2rem; }
code, pre { font: 14px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
code { background: var(--code-bg); padding: .1em .3em; border-radius: 4px; }
pre { background: var(--code-bg); padding: .75rem 1rem; border-radius: 6px; overflow-x: auto; }
pre code { background: none; padding: 0; }
table { border-collapse: collapse; }
th, td { border: 1px solid var(--rule); padding: .3rem .6rem; text-align: left; vertical-align: top; }
blockquote { margin-left: 0; padding-left: 1rem; border-left: 3px solid var(--rule); color: var(--muted); }
.contents { columns: 14rem; column-gap: 2rem; padding-left: 1.2rem; }
.members dt { margin-top: .5rem; }
.members dd { margin-left: 1.5rem; }
";

fn page(title: &str, body: &str) -> String {
    format!(
        "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n\
         <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n\
         <title>{}</title>\n<style>\n{}</style>\n</head>\n<body>\n{}</body>\n</html>\n",
        escape(title),
        STYLE,
        body
    )
}

/// The page of one module.
pub fn module(m: &Module, index: Option<&str>) -> String {
    let mut body = format!("<h1>Module <code>{}</code></h1>\n", escape(&m.name));
    if let Some(link) = index {
        body.push_str(&format!("<p><a href=\"{}\">All modules</a></p>\n", escape(link)));
    }
    if !m.doc.is_empty() {
        body.push_str(&render(&m.doc));
    }
    let ids = ids(m);
    if !m.decls.is_empty() {
        body.push_str("<h2 id=\"contents\">Contents</h2>\n<ul class=\"contents\">\n");
        for kind in Kind::ALL {
            for (d, id) in m.decls.iter().zip(&ids).filter(|(d, _)| d.kind == kind) {
                body.push_str(&format!("<li><a href=\"#{}\"><code>{}</code></a></li>\n", id, escape(&d.name)));
            }
        }
        body.push_str("</ul>\n");
    }
    for kind in Kind::ALL {
        if !m.decls.iter().any(|d| d.kind == kind) {
            continue;
        }
        body.push_str(&format!("<h2>{}</h2>\n", kind.section()));
        for (d, id) in m.decls.iter().zip(&ids).filter(|(d, _)| d.kind == kind) {
            declaration(&mut body, d, id);
        }
    }
    page(&format!("Module {}", m.name), &body)
}

/// A unique anchor for each declaration.
fn ids(m: &Module) -> Vec<String> {
    let mut seen: Vec<String> = Vec::new();
    for d in &m.decls {
        let base: String =
            d.name.chars().map(|c| if c.is_ascii_alphanumeric() || c == '_' { c } else { '-' }).collect();
        let mut id = base.clone();
        let mut n = 2;
        while seen.contains(&id) || id == "contents" {
            id = format!("{}-{}", base, n);
            n += 1;
        }
        seen.push(id);
    }
    seen
}

fn declaration(out: &mut String, d: &Decl, id: &str) {
    out.push_str(&format!("<h3 id=\"{}\"><code>{}</code></h3>\n", id, escape(&d.name)));
    out.push_str(&format!("<pre><code>{}</code></pre>\n", escape(&d.sig)));
    if let Some(doc) = &d.doc {
        out.push_str(&render(doc));
    }
    if !d.members.is_empty() {
        out.push_str(&format!("<p>{}:</p>\n<dl class=\"members\">\n", d.member_label));
        for (name, doc) in &d.members {
            out.push_str(&format!(
                "<dt><code>{}</code></dt>\n<dd>{}</dd>\n",
                escape(name),
                inline(&doc.replace('\n', " "))
            ));
        }
        out.push_str("</dl>\n");
    }
}

/// The index of a set of modules.
pub fn index(title: &str, modules: &[Module]) -> String {
    let mut body = format!(
        "<h1>{}</h1>\n<table>\n<thead><tr><th>Module</th><th>Description</th></tr></thead>\n<tbody>\n",
        escape(title)
    );
    for m in modules {
        body.push_str(&format!(
            "<tr><td><a href=\"{}.html\"><code>{}</code></a></td><td>{}</td></tr>\n",
            escape(&url_path(&m.path)),
            escape(&m.name),
            inline(&summary(m))
        ));
    }
    body.push_str("</tbody>\n</table>\n");
    page(title, &body)
}
