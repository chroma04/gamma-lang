//! Markdown to HTML: headings with GitHub-style ids, code blocks, link rewriting.

use crate::highlight::{escape, highlight};
use crate::links::{rewrite_link, root_prefix};
use pulldown_cmark::{CodeBlockKind, CowStr, Event, HeadingLevel, Options, Parser, Tag, TagEnd, html};
use std::collections::HashMap;

/// A heading of a rendered page.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Heading {
    pub level: u8,
    pub id: String,
    /// The plain text of the heading.
    pub text: String,
}

/// The result of rendering one Markdown file.
#[derive(Debug, Default)]
pub struct Rendered {
    pub html: String,
    pub headings: Vec<Heading>,
    /// Plain text of the first paragraph (not yet truncated).
    pub first_paragraph: String,
    /// Repo-relative targets of the links that name files in the repo, in order of appearance.
    pub link_targets: Vec<String>,
}

/// Everything the renderer needs to know about the page being rendered.
pub struct Context<'a> {
    /// Repo-relative path of the Markdown source.
    pub src: &'a str,
    /// Output path of the page, relative to the site root.
    pub out: &'a str,
    pub is_dir: &'a dyn Fn(&str) -> bool,
}

/// The GitHub-compatible base slug of heading text: lowercase, punctuation removed except
/// `-` and `_`, spaces to `-`.
pub fn slug(text: &str) -> String {
    let mut s = String::new();
    for c in text.chars() {
        if c.is_alphanumeric() {
            s.extend(c.to_lowercase());
        } else if c == '-' || c == '_' {
            s.push(c);
        } else if c == ' ' {
            s.push('-');
        }
    }
    s
}

/// Hands out unique ids: a repeated slug gets `-1`, `-2`, ... appended.
#[derive(Default)]
pub struct IdAllocator {
    used: HashMap<String, usize>,
}

impl IdAllocator {
    pub fn unique(&mut self, base: String) -> String {
        let mut candidate = base.clone();
        while self.used.contains_key(&candidate) {
            let n = self.used.entry(base.clone()).or_insert(0);
            *n += 1;
            candidate = format!("{base}-{n}");
        }
        self.used.insert(candidate.clone(), 0);
        candidate
    }
}

/// Standard base64url without padding, as the playground's `#code=` fragment expects.
pub fn base64url(data: &[u8]) -> String {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    let mut out = String::new();
    for chunk in data.chunks(3) {
        let n = chunk.iter().enumerate().fold(0u32, |acc, (i, &b)| acc | (b as u32) << (16 - 8 * i));
        for i in 0..=chunk.len() {
            out.push(ALPHABET[(n >> (18 - 6 * i) & 63) as usize] as char);
        }
    }
    out
}

/// Renders a Markdown document to the HTML of an `<article class="prose">` body.
pub fn render(markdown: &str, cx: &Context) -> Rendered {
    let options = Options::ENABLE_TABLES | Options::ENABLE_STRIKETHROUGH | Options::ENABLE_FOOTNOTES;
    let events: Vec<Event> = Parser::new_ext(markdown, options).collect();
    let mut result = Rendered::default();
    let mut ids = IdAllocator::default();
    let mut out: Vec<Event> = Vec::with_capacity(events.len());
    let mut i = 0;
    let mut seen_paragraph = false;
    while i < events.len() {
        match &events[i] {
            Event::Start(Tag::Heading { level, .. }) => {
                let end = (i + 1..events.len())
                    .find(|&j| matches!(events[j], Event::End(TagEnd::Heading(_))))
                    .unwrap_or(events.len());
                let text = plain_text(&events[i + 1..end]);
                let id = ids.unique(slug(&text));
                let n = heading_number(*level);
                out.push(Event::Html(format!("<h{n} id=\"{id}\">").into()));
                out.extend(events[i + 1..end].iter().map(|e| rewrite_event(e.clone(), cx, &mut result.link_targets)));
                if (2..=4).contains(&n) {
                    out.push(Event::Html(format!("<a class=\"anchor\" href=\"#{id}\">#</a>").into()));
                }
                out.push(Event::Html(format!("</h{n}>\n").into()));
                result.headings.push(Heading { level: n, id, text });
                i = end + 1;
            }
            Event::Start(Tag::CodeBlock(kind)) => {
                let end = (i + 1..events.len())
                    .find(|&j| matches!(events[j], Event::End(TagEnd::CodeBlock)))
                    .unwrap_or(events.len());
                let info = match kind {
                    CodeBlockKind::Fenced(info) => info.trim().to_string(),
                    CodeBlockKind::Indented => String::new(),
                };
                let code = plain_text(&events[i + 1..end]);
                out.push(Event::Html(code_block(&info, &code, cx.out).into()));
                i = end + 1;
            }
            Event::Start(Tag::Table(_)) => {
                out.push(Event::Html("<div class=\"table\">".into()));
                out.push(events[i].clone());
                i += 1;
            }
            Event::End(TagEnd::Table) => {
                out.push(events[i].clone());
                out.push(Event::Html("</div>\n".into()));
                i += 1;
            }
            Event::Start(Tag::Paragraph) if !seen_paragraph && has_prose(&events[i + 1..paragraph_end(&events, i)]) => {
                seen_paragraph = true;
                let end = paragraph_end(&events, i);
                result.first_paragraph = plain_text(&events[i + 1..end]);
                out.push(events[i].clone());
                i += 1;
            }
            e => {
                out.push(rewrite_event(e.clone(), cx, &mut result.link_targets));
                i += 1;
            }
        }
    }
    html::push_html(&mut result.html, out.into_iter());
    result
}

/// The index of the end of the paragraph starting at `start`.
fn paragraph_end(events: &[Event], start: usize) -> usize {
    (start + 1..events.len()).find(|&j| matches!(events[j], Event::End(TagEnd::Paragraph))).unwrap_or(events.len())
}

/// Whether a paragraph has words outside links: a line of navigation links does not
/// describe the page.
fn has_prose(events: &[Event]) -> bool {
    let mut depth = 0;
    events.iter().any(|e| match e {
        Event::Start(Tag::Link { .. }) => {
            depth += 1;
            false
        }
        Event::End(TagEnd::Link) => {
            depth -= 1;
            false
        }
        Event::Text(t) | Event::Code(t) => depth == 0 && t.chars().any(char::is_alphanumeric),
        _ => false,
    })
}

fn heading_number(level: HeadingLevel) -> u8 {
    level as u8
}

/// The text of inline events with markup removed (soft breaks become spaces).
fn plain_text(events: &[Event]) -> String {
    let mut s = String::new();
    for e in events {
        match e {
            Event::Text(t) | Event::Code(t) => s.push_str(t),
            Event::SoftBreak | Event::HardBreak => s.push(' '),
            _ => {}
        }
    }
    s
}

/// Rewrites the destination of links and images.
fn rewrite_event<'a>(event: Event<'a>, cx: &Context, targets: &mut Vec<String>) -> Event<'a> {
    let mut fix = |dest: &CowStr<'a>| -> CowStr<'a> {
        let (new, target) = rewrite_link(dest, cx.src, cx.out, cx.is_dir);
        targets.extend(target);
        new.into()
    };
    match event {
        Event::Start(Tag::Link { link_type, dest_url, title, id }) => {
            Event::Start(Tag::Link { link_type, dest_url: fix(&dest_url), title, id })
        }
        Event::Start(Tag::Image { link_type, dest_url, title, id }) => {
            Event::Start(Tag::Image { link_type, dest_url: fix(&dest_url), title, id })
        }
        e => e,
    }
}

/// The HTML for a fenced (or indented) code block with the given info string.
fn code_block(info: &str, code: &str, cur_out: &str) -> String {
    let lang = info.split_whitespace().next().unwrap_or("");
    let code = code.strip_suffix('\n').unwrap_or(code);
    match lang {
        "gamma" => {
            let extra = if info.split_whitespace().nth(1) == Some("error") { " error" } else { "" };
            let pre = format!("<pre class=\"code gamma{extra}\"><code>{}</code></pre>\n", highlight(code));
            if info == "gamma" && code.lines().any(|l| l.starts_with("main :: fn")) {
                let href = format!("{}playground.html#code={}", root_prefix(cur_out), base64url(code.as_bytes()));
                format!(
                    "<div class=\"runnable\">{pre}<a class=\"try\" href=\"{href}\">Run in the playground</a></div>\n"
                )
            } else {
                pre
            }
        }
        "output" => format!("<pre class=\"code output\"><code>{}</code></pre>\n", escape(code)),
        "" => format!("<pre class=\"code\"><code>{}</code></pre>\n", escape(code)),
        lang => {
            let safe: String = lang.chars().filter(|c| c.is_ascii_alphanumeric() || *c == '-' || *c == '_').collect();
            format!("<pre class=\"code lang-{safe}\"><code>{}</code></pre>\n", escape(code))
        }
    }
}

/// Truncates text to about `max` characters at a word boundary, adding an ellipsis.
pub fn truncate(text: &str, max: usize) -> String {
    let text = text.split_whitespace().collect::<Vec<_>>().join(" ");
    if text.chars().count() <= max {
        return text;
    }
    let cut: String = text.chars().take(max).collect();
    let cut = cut.rsplit_once(' ').map_or(cut.as_str(), |(head, _)| head);
    format!("{}…", cut.trim_end_matches(|c: char| c.is_ascii_punctuation()))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn render_str(md: &str) -> Rendered {
        let no_dirs = |_: &str| false;
        render(md, &Context { src: "docs/guide/a.md", out: "guide/a.html", is_dir: &no_dirs })
    }

    #[test]
    fn slugs() {
        assert_eq!(slug("17.3 Compile-time `if`"), "173-compile-time-if");
        assert_eq!(slug("A_b: c & d?"), "a_b-c--d");
        assert_eq!(slug("Éclair"), "éclair");
    }

    #[test]
    fn duplicate_ids() {
        let mut ids = IdAllocator::default();
        assert_eq!(ids.unique("x".into()), "x");
        assert_eq!(ids.unique("x".into()), "x-1");
        assert_eq!(ids.unique("x".into()), "x-2");
        assert_eq!(ids.unique("x-1".into()), "x-1-1");
    }

    #[test]
    fn base64() {
        assert_eq!(base64url(b"a"), "YQ");
        assert_eq!(base64url(b"ab"), "YWI");
        assert_eq!(base64url(b"abc"), "YWJj");
        assert_eq!(base64url(&[0xfb, 0xff]), "-_8");
    }

    #[test]
    fn headings_and_anchors() {
        let r = render_str("# T\n\n## One `x`\n\n## One `x`\n\n##### Deep\n");
        assert!(r.html.contains("<h2 id=\"one-x\">One <code>x</code><a class=\"anchor\" href=\"#one-x\">#</a></h2>"));
        assert!(r.html.contains("id=\"one-x-1\""));
        assert!(r.html.contains("<h5 id=\"deep\">Deep</h5>"));
        assert_eq!(r.headings.len(), 4);
    }

    #[test]
    fn code_blocks() {
        let r = render_str(
            "```gamma\nmain :: fn() {}\n```\n\n```output\n<hi>\n```\n\n```sh\nls\n```\n\n```gamma error\nx\n```\n",
        );
        assert!(r.html.contains("<div class=\"runnable\"><pre class=\"code gamma\">"));
        assert!(r.html.contains("href=\"../playground.html#code=bWFpbiA6OiBmbigpIHt9\""));
        assert!(
            r.html.contains("<pre class=\"code output\"><code>&lt;hi&gt;\n</code>") || r.html.contains("&lt;hi&gt;")
        );
        assert!(r.html.contains("<pre class=\"code lang-sh\">"));
        assert!(r.html.contains("<pre class=\"code gamma error\">"));
        assert_eq!(r.html.matches("class=\"try\"").count(), 1);
    }

    #[test]
    fn tables_and_links() {
        let r = render_str("First *para* with `code`.\n\n| a | b |\n|---|---|\n| [x](b.md#s) | 1 |\n");
        assert!(r.html.contains("<div class=\"table\"><table>"));
        assert!(r.html.contains("href=\"b.html#s\""));
        assert_eq!(r.first_paragraph, "First para with code.");
        assert_eq!(r.link_targets, vec!["docs/guide/b.md"]);
    }

    #[test]
    fn truncation() {
        assert_eq!(truncate("short text", 200), "short text");
        assert_eq!(truncate("alpha beta gamma delta", 12), "alpha beta…");
    }
}
