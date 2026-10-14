//! Markdown output.

use super::collect::{Decl, Kind, Module};

/// The page of one module.
pub fn module(m: &Module, index: Option<&str>) -> String {
    let mut out = format!("# Module `{}`\n\n", m.name);
    if let Some(link) = index {
        out.push_str(&format!("[All modules]({})\n\n", link));
    }
    if !m.doc.is_empty() {
        out.push_str(&m.doc);
        out.push_str("\n\n");
    }
    for kind in Kind::ALL {
        let decls: Vec<&Decl> = m.decls.iter().filter(|d| d.kind == kind).collect();
        if decls.is_empty() {
            continue;
        }
        out.push_str(&format!("## {}\n\n", kind.section()));
        for d in decls {
            declaration(&mut out, d);
        }
    }
    finish(out)
}

fn declaration(out: &mut String, d: &Decl) {
    out.push_str(&format!("### `{}`\n\n", d.name));
    out.push_str(&format!("```gamma\n{}\n```\n\n", d.sig));
    if let Some(doc) = &d.doc {
        out.push_str(doc);
        out.push_str("\n\n");
    }
    if !d.members.is_empty() {
        out.push_str(&format!("{}:\n\n", d.member_label));
        for (name, doc) in &d.members {
            let mut lines = doc.lines();
            out.push_str(&format!("- `{}`: {}\n", name, lines.next().unwrap_or("")));
            for l in lines {
                out.push_str(&if l.is_empty() { "\n".to_string() } else { format!("  {}\n", l) });
            }
        }
        out.push('\n');
    }
}

/// The index of a set of modules.
pub fn index(title: &str, modules: &[Module]) -> String {
    let mut out = format!("# {}\n\n| Module | Description |\n| --- | --- |\n", title);
    for m in modules {
        out.push_str(&format!("| [`{}`]({}.md) | {} |\n", m.name, m.path, summary(m).replace('|', "\\|")));
    }
    finish(out)
}

/// The first sentence of the module's documentation.
pub fn summary(m: &Module) -> String {
    let para = m.doc.split("\n\n").next().unwrap_or("");
    let line = para.split_whitespace().collect::<Vec<_>>().join(" ");
    match line.find(". ") {
        Some(i) => line[..=i].to_string(),
        None => line,
    }
}

/// Ends the text with exactly one line feed.
fn finish(mut s: String) -> String {
    let trimmed = s.trim_end().len();
    s.truncate(trimmed);
    s.push('\n');
    s
}
