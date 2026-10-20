//! Page assembly: the layout template, hand-written fragments and the navigation of
//! Markdown pages.

use crate::highlight::{escape, highlight, unescape};
use crate::links::{Target, relative};
use crate::markdown::{Heading, Rendered, truncate};

/// The description used when a page has none of its own.
pub const SITE_DESCRIPTION: &str =
    "Gamma, a systems programming language in which everything a function depends on appears in its signature.";

/// The values substituted for the layout's placeholders (except the body).
pub struct Meta {
    pub title: String,
    pub description: String,
    pub section: String,
    pub layout: String,
}

impl Default for Meta {
    fn default() -> Meta {
        Meta {
            title: "Gamma".into(),
            description: SITE_DESCRIPTION.into(),
            section: "home".into(),
            layout: "page".into(),
        }
    }
}

/// Fills the layout in a single pass, so substituted text is never scanned again.
pub fn apply_layout(template: &str, meta: &Meta, root: &str, body: &str) -> String {
    let mut out = String::with_capacity(template.len() + body.len());
    let mut rest = template;
    while let Some(start) = rest.find("{{") {
        out.push_str(&rest[..start]);
        let after = &rest[start + 2..];
        let Some(end) = after.find("}}") else {
            out.push_str("{{");
            rest = after;
            continue;
        };
        match &after[..end] {
            "title" => out.push_str(&escape(&meta.title)),
            "description" => out.push_str(&escape(&meta.description)),
            "root" => out.push_str(root),
            "layout" => out.push_str(&escape(&meta.layout)),
            "section" => out.push_str(&escape(&meta.section)),
            "body" => out.push_str(body),
            other => {
                out.push_str("{{");
                out.push_str(other);
                out.push_str("}}");
            }
        }
        rest = &after[end + 2..];
    }
    out.push_str(rest);
    out
}

/// Splits a fragment into its front matter and body. The first line may be a comment such as
/// `<!-- title: X; section: guide; layout: wide; description: Y -->`.
pub fn parse_fragment(text: &str) -> (Meta, &str) {
    let mut meta = Meta::default();
    let (first, rest) = text.split_once('\n').unwrap_or((text, ""));
    let Some(inner) = first.trim().strip_prefix("<!--").and_then(|s| s.strip_suffix("-->")) else {
        return (meta, text);
    };
    for item in inner.split(';') {
        let Some((key, value)) = item.split_once(':') else { continue };
        let value = value.trim().to_string();
        match key.trim() {
            "title" => meta.title = value,
            "section" => meta.section = value,
            "layout" => meta.layout = value,
            "description" => meta.description = value,
            _ => {}
        }
    }
    (meta, rest)
}

/// Replaces the escaped source inside `<pre class="code gamma"><code>…</code></pre>` with its
/// highlighted version.
pub fn highlight_fragment_code(body: &str) -> String {
    const OPEN: &str = "<pre class=\"code gamma\"><code>";
    const CLOSE: &str = "</code></pre>";
    let mut out = String::with_capacity(body.len());
    let mut rest = body;
    while let Some(start) = rest.find(OPEN) {
        let inner_start = start + OPEN.len();
        let Some(len) = rest[inner_start..].find(CLOSE) else { break };
        out.push_str(&rest[..inner_start]);
        out.push_str(&highlight(&unescape(&rest[inner_start..inner_start + len])));
        rest = &rest[inner_start + len..];
    }
    out.push_str(rest);
    out
}

/// A rendered Markdown page together with what navigation needs to know about it.
pub struct Doc {
    /// Repo-relative path of the source.
    pub src: String,
    pub target: Target,
    pub rendered: Rendered,
    /// Text of the first heading, or the file stem.
    pub title: String,
}

impl Doc {
    /// The HTML title and meta description of the page.
    pub fn meta(&self) -> Meta {
        let description = truncate(&self.rendered.first_paragraph, 200);
        Meta {
            title: format!("{} · Gamma", self.title),
            description: if description.is_empty() { SITE_DESCRIPTION.into() } else { description },
            section: self.target.section.into(),
            layout: "doc".into(),
        }
    }
}

/// The sidebar order of a group of pages under `prefix`: the index page first, then (unless
/// `by_name`) the pages in the order the index links to them, then the others by path.
/// Returns indices into `docs`.
pub fn ordered(docs: &[Doc], index_src: &str, prefix: &str, by_name: bool) -> Vec<usize> {
    let mut order = Vec::new();
    let Some(index) = docs.iter().position(|d| d.src == index_src) else { return order };
    order.push(index);
    if !by_name {
        for target in &docs[index].rendered.link_targets {
            if let Some(i) = docs.iter().position(|d| &d.src == target)
                && target.starts_with(prefix)
                && !order.contains(&i)
            {
                order.push(i);
            }
        }
    }
    let mut rest: Vec<usize> =
        (0..docs.len()).filter(|i| docs[*i].src.starts_with(prefix) && !order.contains(i)).collect();
    rest.sort_by(|a, b| docs[*a].src.cmp(&docs[*b].src));
    order.extend(rest);
    order
}

/// The pages of the site's Markdown sections, with the sidebar orders of the guide and library.
pub struct Site {
    pub docs: Vec<Doc>,
    pub guide: Vec<usize>,
    pub std: Vec<usize>,
}

impl Site {
    pub fn new(docs: Vec<Doc>) -> Site {
        let guide = ordered(&docs, "docs/guide/README.md", "docs/guide/", true);
        let std = ordered(&docs, "docs/std/index.md", "docs/std/", false);
        Site { docs, guide, std }
    }

    /// The name a page has in lists: the first heading, except for the two index pages.
    fn label(&self, i: usize) -> &str {
        match self.docs[i].src.as_str() {
            "docs/guide/README.md" => "Contents",
            "docs/std/index.md" => "Overview",
            _ => &self.docs[i].title,
        }
    }

    /// The `<div class="doc">` body for page `i`.
    pub fn doc_body(&self, i: usize) -> String {
        let doc = &self.docs[i];
        let h2: Vec<&Heading> = doc.rendered.headings.iter().filter(|h| h.level == 2).collect();
        let mut aside = String::new();
        match doc.target.section {
            "guide" => {
                aside.push_str(&self.chapter_list(&self.guide, i, "ol"));
                aside.push_str(&flat_toc(&h2, 1));
            }
            "std" => {
                aside.push_str(&self.chapter_list(&self.std, i, "ul"));
                aside.push_str(&flat_toc(&h2, 2));
            }
            "spec" => aside.push_str(&nested_toc(&doc.rendered.headings)),
            _ => aside.push_str(&flat_toc(&h2, 2)),
        }
        let mut article = format!("<article class=\"prose\">\n{}", doc.rendered.html);
        if doc.target.section == "guide" {
            article.push_str(&self.pager(i));
        }
        article.push_str("</article>\n");
        if aside.is_empty() {
            return format!("<div class=\"doc solo\">\n{article}</div>\n");
        }
        format!("<div class=\"doc\">\n<aside class=\"side\">\n{aside}</aside>\n{article}</div>\n")
    }

    /// A `<nav class="chapters">` list of the pages in `order`, marking page `current`.
    fn chapter_list(&self, order: &[usize], current: usize, tag: &str) -> String {
        let cur = &self.docs[current].target.out;
        let mut s = format!("<nav class=\"chapters\"><{tag}>\n");
        for &j in order {
            let here = if j == current { " class=\"here\"" } else { "" };
            let href = relative(cur, &self.docs[j].target.out);
            s.push_str(&format!("<li><a{here} href=\"{href}\">{}</a></li>\n", escape(self.label(j))));
        }
        s.push_str(&format!("</{tag}></nav>\n"));
        s
    }

    /// Previous/next links between guide pages.
    fn pager(&self, current: usize) -> String {
        let Some(pos) = self.guide.iter().position(|&j| j == current) else { return String::new() };
        let cur = &self.docs[current].target.out;
        let link = |j: usize, class: &str, arrow: (&str, &str)| {
            let href = relative(cur, &self.docs[j].target.out);
            format!("<a class=\"{class}\" href=\"{href}\">{}{}{}</a>", arrow.0, escape(self.label(j)), arrow.1)
        };
        let mut s = String::from("<nav class=\"pager\">");
        if pos > 0 {
            s.push_str(&link(self.guide[pos - 1], "prev", ("← ", "")));
        }
        if pos + 1 < self.guide.len() {
            s.push_str(&link(self.guide[pos + 1], "next", ("", " →")));
        }
        s.push_str("</nav>\n");
        s
    }
}

/// A one-level table of contents; empty if there are fewer than `min` entries.
fn flat_toc(headings: &[&Heading], min: usize) -> String {
    if headings.len() < min {
        return String::new();
    }
    let mut s = String::from("<nav class=\"toc\"><ul>\n");
    for h in headings {
        s.push_str(&format!("<li><a href=\"#{}\">{}</a></li>\n", h.id, escape(&h.text)));
    }
    s.push_str("</ul></nav>\n");
    s
}

/// A two-level table of contents: `##` sections with their `###` subsections nested.
fn nested_toc(headings: &[Heading]) -> String {
    let mut s = String::from("<nav class=\"toc\"><ul>\n");
    let mut open = false; // a top-level <li> is open
    let mut sub = false; // its nested <ul> is open
    for h in headings {
        match h.level {
            2 => {
                if sub {
                    s.push_str("</ul>");
                }
                if open {
                    s.push_str("</li>\n");
                }
                s.push_str(&format!("<li><a href=\"#{}\">{}</a>", h.id, escape(&h.text)));
                open = true;
                sub = false;
            }
            3 if open => {
                if !sub {
                    s.push_str("\n<ul>\n");
                    sub = true;
                }
                s.push_str(&format!("<li><a href=\"#{}\">{}</a></li>\n", h.id, escape(&h.text)));
            }
            _ => {}
        }
    }
    if sub {
        s.push_str("</ul>");
    }
    if open {
        s.push_str("</li>\n");
    }
    s.push_str("</ul></nav>\n");
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_substitution() {
        let meta = Meta { title: "A & B".into(), ..Meta::default() };
        let out = apply_layout("<t>{{title}}</t>{{root}}|{{body}}|{{x}}", &meta, "../", "{{title}}");
        assert_eq!(out, "<t>A &amp; B</t>../|{{title}}|{{x}}");
    }

    #[test]
    fn fragments() {
        let (m, body) = parse_fragment(
            "<!-- title: P · Gamma; section: playground; layout: wide; description: Run it -->\n<p>x</p>\n",
        );
        assert_eq!((m.title.as_str(), m.section.as_str(), m.layout.as_str()), ("P · Gamma", "playground", "wide"));
        assert_eq!(m.description, "Run it");
        assert_eq!(body, "<p>x</p>\n");
        let (m, body) = parse_fragment("<p>no front matter</p>");
        assert_eq!((m.title.as_str(), m.section.as_str(), m.layout.as_str()), ("Gamma", "home", "page"));
        assert_eq!(body, "<p>no front matter</p>");
    }

    #[test]
    fn fragment_code_is_highlighted() {
        let out = highlight_fragment_code(
            "<pre class=\"code gamma\"><code>x := &quot;a&lt;b&quot;</code></pre><pre class=\"code\"><code>y</code></pre>",
        );
        assert!(out.contains("<span class=\"s\">&quot;a&lt;b&quot;</span>"));
        assert!(out.ends_with("<pre class=\"code\"><code>y</code></pre>"));
    }

    #[test]
    fn nested_toc_shape() {
        let h = |level, id: &str| Heading { level, id: id.into(), text: id.into() };
        let toc = nested_toc(&[h(1, "t"), h(2, "a"), h(3, "a1"), h(3, "a2"), h(4, "x"), h(2, "b")]);
        assert_eq!(toc.matches("<ul>").count(), 2);
        assert!(toc.contains("<a href=\"#a2\">a2</a></li>\n</ul></li>\n<li><a href=\"#b\">b</a></li>"));
    }
}
