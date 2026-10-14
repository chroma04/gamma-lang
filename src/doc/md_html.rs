//! A small Markdown renderer for doc comments: paragraphs, headings, fenced and indented
//! code, bullet and numbered lists, block quotes, tables, `code`, *emphasis*, **strong**
//! and links. Everything is escaped; raw HTML in the text is shown as text.

pub fn escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            '\'' => out.push_str("&#39;"),
            c => out.push(c),
        }
    }
    out
}

/// Renders Markdown `text` to HTML.
pub fn render(text: &str) -> String {
    let lines: Vec<&str> = text.lines().collect();
    let mut out = String::new();
    let mut i = 0;
    while i < lines.len() {
        let line = lines[i];
        let t = line.trim_start();
        if line.trim().is_empty() {
            i += 1;
        } else if t.starts_with("```") {
            i += 1;
            let mut code = Vec::new();
            while i < lines.len() && !lines[i].trim_start().starts_with("```") {
                code.push(lines[i]);
                i += 1;
            }
            i += 1;
            out.push_str(&format!("<pre><code>{}</code></pre>\n", escape(&code.join("\n"))));
        } else if let Some((level, title)) = heading(t) {
            let level = (level + 3).min(6);
            out.push_str(&format!("<h{0}>{1}</h{0}>\n", level, inline(title)));
            i += 1;
        } else if is_indented(line) {
            let mut code = Vec::new();
            while i < lines.len() && (is_indented(lines[i]) || lines[i].trim().is_empty()) {
                code.push(lines[i].get(4..).unwrap_or(""));
                i += 1;
            }
            while code.last().is_some_and(|l| l.is_empty()) {
                code.pop();
            }
            out.push_str(&format!("<pre><code>{}</code></pre>\n", escape(&code.join("\n"))));
        } else if let Some(rest) = t.strip_prefix('>') {
            let mut quote = vec![rest.strip_prefix(' ').unwrap_or(rest).to_string()];
            i += 1;
            while i < lines.len() && lines[i].trim_start().starts_with('>') {
                let r = &lines[i].trim_start()[1..];
                quote.push(r.strip_prefix(' ').unwrap_or(r).to_string());
                i += 1;
            }
            out.push_str(&format!("<blockquote>\n{}</blockquote>\n", render(&quote.join("\n"))));
        } else if item_marker(t).is_some() {
            i = list(&lines, i, &mut out);
        } else if t.starts_with('|') && lines.get(i + 1).is_some_and(|l| is_table_rule(l)) {
            i = table(&lines, i, &mut out);
        } else {
            let mut para = vec![t];
            i += 1;
            while i < lines.len() && !starts_block(lines[i]) {
                para.push(lines[i].trim());
                i += 1;
            }
            out.push_str(&format!("<p>{}</p>\n", inline(&para.join(" "))));
        }
    }
    out
}

fn is_indented(line: &str) -> bool {
    line.starts_with("    ") && !line.trim().is_empty()
}

fn heading(t: &str) -> Option<(usize, &str)> {
    let n = t.chars().take_while(|&c| c == '#').count();
    (1..=6).contains(&n).then(|| t[n..].strip_prefix(' ').map(|r| (n, r.trim()))).flatten()
}

/// The marker length of a list item line, and whether it is numbered.
fn item_marker(t: &str) -> Option<(usize, bool)> {
    if t.starts_with("- ") || t.starts_with("* ") {
        return Some((2, false));
    }
    let digits = t.chars().take_while(|c| c.is_ascii_digit()).count();
    (digits > 0 && t[digits..].starts_with(". ")).then_some((digits + 2, true))
}

fn is_table_rule(l: &str) -> bool {
    let t = l.trim();
    t.starts_with('|') && t.chars().all(|c| matches!(c, '|' | '-' | ':' | ' '))
}

fn starts_block(line: &str) -> bool {
    let t = line.trim_start();
    line.trim().is_empty()
        || t.starts_with("```")
        || t.starts_with('>')
        || t.starts_with('|')
        || heading(t).is_some()
        || item_marker(t).is_some()
}

fn list(lines: &[&str], mut i: usize, out: &mut String) -> usize {
    let numbered = item_marker(lines[i].trim_start()).is_some_and(|m| m.1);
    let tag = if numbered { "ol" } else { "ul" };
    out.push_str(&format!("<{}>\n", tag));
    while i < lines.len() {
        let t = lines[i].trim_start();
        let Some((n, num)) = item_marker(t) else { break };
        if num != numbered {
            break;
        }
        let mut body = vec![t[n..].to_string()];
        i += 1;
        // Continuation lines are indented; a blank line followed by more indented text
        // continues the item as well.
        while i < lines.len() {
            let l = lines[i];
            if l.trim().is_empty() {
                if lines.get(i + 1).is_some_and(|n| n.starts_with("  ")) {
                    body.push(String::new());
                    i += 1;
                    continue;
                }
                break;
            }
            if l.starts_with("  ") || !starts_block(l) {
                body.push(l.trim_start().to_string());
                i += 1;
            } else {
                break;
            }
        }
        let text = body.join("\n");
        if body.iter().any(|l| l.is_empty()) || body.iter().skip(1).any(|l| item_marker(l).is_some()) {
            out.push_str(&format!("<li>\n{}</li>\n", render(&text)));
        } else {
            out.push_str(&format!("<li>{}</li>\n", inline(&text.replace('\n', " "))));
        }
    }
    out.push_str(&format!("</{}>\n", tag));
    i
}

fn cells(line: &str) -> Vec<String> {
    let t = line.trim().trim_start_matches('|');
    let t = t.strip_suffix('|').unwrap_or(t);
    let mut cells = Vec::new();
    let mut cur = String::new();
    let mut chars = t.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '\\' if chars.peek() == Some(&'|') => {
                cur.push('|');
                chars.next();
            }
            '|' => cells.push(std::mem::take(&mut cur)),
            c => cur.push(c),
        }
    }
    cells.push(cur);
    cells.into_iter().map(|c| c.trim().to_string()).collect()
}

fn table(lines: &[&str], mut i: usize, out: &mut String) -> usize {
    out.push_str("<table>\n<thead><tr>");
    for c in cells(lines[i]) {
        out.push_str(&format!("<th>{}</th>", inline(&c)));
    }
    out.push_str("</tr></thead>\n<tbody>\n");
    i += 2;
    while i < lines.len() && lines[i].trim_start().starts_with('|') {
        out.push_str("<tr>");
        for c in cells(lines[i]) {
            out.push_str(&format!("<td>{}</td>", inline(&c)));
        }
        out.push_str("</tr>\n");
        i += 1;
    }
    out.push_str("</tbody>\n</table>\n");
    i
}

/// Inline Markdown: code spans, strong, emphasis and links.
pub fn inline(s: &str) -> String {
    let mut out = String::new();
    let mut rest = s;
    while let Some(start) = rest.find('`') {
        let after = &rest[start + 1..];
        let Some(len) = after.find('`') else { break };
        out.push_str(&emphasis(&rest[..start]));
        out.push_str(&format!("<code>{}</code>", escape(&after[..len])));
        rest = &after[len + 1..];
    }
    out.push_str(&emphasis(rest));
    out
}

/// Strong, emphasis and links in text without code spans.
fn emphasis(s: &str) -> String {
    let chars: Vec<char> = s.chars().collect();
    let mut out = String::new();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c == '['
            && let Some((label, url, next)) = link(&chars, i)
        {
            if safe_url(&url) {
                out.push_str(&format!("<a href=\"{}\">{}</a>", escape(&url), emphasis(&label)));
            } else {
                out.push_str(&escape(&chars[i..next].iter().collect::<String>()));
            }
            i = next;
        } else if c == '*' {
            let n = if chars.get(i + 1) == Some(&'*') { 2 } else { 1 };
            let opens = chars.get(i + n).is_some_and(|c| !c.is_whitespace() && *c != '*');
            let close = if opens { find_close(&chars, i + n, n) } else { None };
            if let Some(j) = close {
                let inner: String = chars[i + n..j].iter().collect();
                let tag = if n == 2 { "strong" } else { "em" };
                out.push_str(&format!("<{0}>{1}</{0}>", tag, emphasis(&inner)));
                i = j + n;
            } else {
                out.push('*');
                i += 1;
            }
        } else {
            out.push_str(&escape(&c.to_string()));
            i += 1;
        }
    }
    out
}

/// The index of the closing `*` or `**` (of length `n`) after `from`, if the character
/// before it is not white space.
fn find_close(chars: &[char], from: usize, n: usize) -> Option<usize> {
    let mut j = from;
    while j + n <= chars.len() {
        if chars[j..j + n].iter().all(|&c| c == '*') && !chars[j - 1].is_whitespace() && chars.get(j + n) != Some(&'*')
        {
            return Some(j);
        }
        j += 1;
    }
    None
}

/// `[label](url)` at `i`: the label, the url and the index after it.
fn link(chars: &[char], i: usize) -> Option<(String, String, usize)> {
    let close = (i + 1..chars.len()).find(|&j| chars[j] == ']')?;
    if chars.get(close + 1) != Some(&'(') {
        return None;
    }
    let end = (close + 2..chars.len()).find(|&j| chars[j] == ')')?;
    let url: String = chars[close + 2..end].iter().collect();
    if url.contains(char::is_whitespace) {
        return None;
    }
    Some((chars[i + 1..close].iter().collect(), url, end + 1))
}

fn safe_url(url: &str) -> bool {
    let lower = url.to_ascii_lowercase();
    match lower.split_once(':') {
        Some((scheme, _)) if !scheme.contains(['/', '#', '?']) => matches!(scheme, "http" | "https" | "mailto"),
        _ => true,
    }
}
