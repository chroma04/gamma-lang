//! Syntax highlighting of Gamma code with the compiler's own lexer.
//!
//! The lexer drops comments, so the text between two tokens (the "gap") is whitespace and/or
//! comments; comments in it are wrapped in `<span class="c">`. Highlighting never changes the
//! text: stripping the tags from the output and unescaping it gives back the input exactly.

use gamma::diag::Diagnostics;
use gamma::source::SourceMap;
use gamma::syntax::lexer::lex;
use gamma::syntax::token::{Keyword, Punct, Token, TokenKind};
use std::panic::{AssertUnwindSafe, catch_unwind};

/// Escapes text for HTML content and attribute values.
pub fn escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    push_escaped(&mut out, s);
    out
}

/// Appends `s` to `out`, escaped.
fn push_escaped(out: &mut String, s: &str) {
    for c in s.chars() {
        match c {
            '&' => out.push_str("&amp;"),
            '<' => out.push_str("&lt;"),
            '>' => out.push_str("&gt;"),
            '"' => out.push_str("&quot;"),
            c => out.push(c),
        }
    }
}

/// Reverses [`escape`].
pub fn unescape(s: &str) -> String {
    s.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&amp;", "&")
}

/// Highlights Gamma source as HTML (the inside of a `<code>` element). Falls back to plain
/// escaped text if the lexer's tokens cannot be used.
pub fn highlight(src: &str) -> String {
    let tokens = catch_unwind(AssertUnwindSafe(|| {
        let mut map = SourceMap::new();
        let file = map.add("snippet.gm", src.to_string());
        let mut diags = Diagnostics::new();
        lex(file, src, &mut diags).tokens
    }));
    let Ok(tokens) = tokens else { return escape(src) };
    let mut out = String::with_capacity(src.len() * 2);
    let mut pos = 0;
    for (i, tok) in tokens.iter().enumerate() {
        let (lo, hi) = (tok.span.lo as usize, tok.span.hi as usize);
        let usable = lo >= pos && lo <= hi && hi <= src.len() && src.is_char_boundary(lo) && src.is_char_boundary(hi);
        if !usable {
            // Unexpected spans (or `Eof`): stop here and show the rest as plain text.
            break;
        }
        gap(&mut out, &src[pos..lo]);
        let text = &src[lo..hi];
        match class(src, &tokens, i) {
            Some(c) => {
                out.push_str("<span class=\"");
                out.push_str(c);
                out.push_str("\">");
                push_escaped(&mut out, text);
                out.push_str("</span>");
            }
            None => push_escaped(&mut out, text),
        }
        pos = hi;
    }
    gap(&mut out, &src[pos..]);
    out
}

/// The CSS class of token `i`, if it has one.
fn class(src: &str, tokens: &[Token], i: usize) -> Option<&'static str> {
    let tok = &tokens[i];
    match &tok.kind {
        TokenKind::Keyword(_) => Some("k"),
        TokenKind::Ident(name) => {
            let name = name.as_str();
            if matches!(name, "const" | "on" | "where" | "test") {
                Some("k")
            } else if name.starts_with(|c: char| c.is_ascii_uppercase()) {
                Some("t")
            } else if is_function_name(tokens, i) {
                Some("f")
            } else {
                None
            }
        }
        TokenKind::Str(_) => Some("s"),
        TokenKind::Int(_) if src[tok.span.lo as usize..].starts_with('\'') => Some("s"),
        TokenKind::Int(_) | TokenKind::Float(_) => Some("n"),
        TokenKind::Intrinsic(_) | TokenKind::Attribute(_) => Some("a"),
        _ => None,
    }
}

/// True for an identifier directly followed by `(`, or in a definition `name :: fn`.
fn is_function_name(tokens: &[Token], i: usize) -> bool {
    let next = tokens.get(i + 1);
    if let Some(n) = next
        && n.kind.is_punct(Punct::LParen)
        && n.span.lo == tokens[i].span.hi
    {
        return true;
    }
    matches!(next, Some(n) if n.kind.is_punct(Punct::ColonColon))
        && matches!(tokens.get(i + 2), Some(n) if n.kind.is_keyword(Keyword::Fn))
}

/// Emits the text between tokens: whitespace as is, comments wrapped in `<span class="c">`.
fn gap(out: &mut String, text: &str) {
    let mut rest = text;
    while !rest.is_empty() {
        let Some(start) = rest.find("//").into_iter().chain(rest.find("/*")).min() else {
            push_escaped(out, rest);
            return;
        };
        push_escaped(out, &rest[..start]);
        let comment = &rest[start..];
        let len =
            if comment.starts_with("//") { comment.find('\n').unwrap_or(comment.len()) } else { block_len(comment) };
        out.push_str("<span class=\"c\">");
        push_escaped(out, &comment[..len]);
        out.push_str("</span>");
        rest = &comment[len..];
    }
}

/// The length of the (nesting) block comment at the start of `s`, or all of `s` if unclosed.
fn block_len(s: &str) -> usize {
    let b = s.as_bytes();
    let (mut depth, mut i) = (0, 0);
    while i < b.len() {
        if b[i..].starts_with(b"/*") {
            depth += 1;
            i += 2;
        } else if b[i..].starts_with(b"*/") {
            depth -= 1;
            i += 2;
            if depth == 0 {
                return i;
            }
        } else {
            i += 1;
        }
    }
    s.len()
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Strips tags and unescapes, which must give back the original text.
    pub fn plain(html: &str) -> String {
        let mut out = String::new();
        let mut in_tag = false;
        for c in html.chars() {
            match c {
                '<' => in_tag = true,
                '>' if in_tag => in_tag = false,
                c if !in_tag => out.push(c),
                _ => {}
            }
        }
        unescape(&out)
    }

    #[test]
    fn classes() {
        let h = highlight("main :: fn() needs Out {\n    x := foo(1) // hi\n    @write(\"a<b\", 'c', 2.5)\n}\n");
        assert!(h.contains("<span class=\"f\">main</span>"));
        assert!(h.contains("<span class=\"k\">fn</span>"));
        assert!(h.contains("<span class=\"k\">needs</span>"));
        assert!(h.contains("<span class=\"t\">Out</span>"));
        assert!(h.contains("<span class=\"f\">foo</span>"));
        assert!(h.contains("<span class=\"c\">// hi</span>"));
        assert!(h.contains("<span class=\"a\">@write</span>"));
        assert!(h.contains("<span class=\"s\">&quot;a&lt;b&quot;</span>"));
        assert!(h.contains("<span class=\"s\">'c'</span>"));
        assert!(h.contains("<span class=\"n\">2.5</span>"));
    }

    #[test]
    fn comments_and_garbage_round_trip() {
        for src in [
            "/* a /* b */ c */ x := 1 /// doc\n//! mod\n",
            "/* unclosed",
            "\"unterminated",
            "x := `raw\nmore`  é ∀ $ ?",
            "{ ( [",
            "",
            "\n\n  ",
        ] {
            assert_eq!(plain(&highlight(src)), src, "{src:?}");
        }
    }

    #[test]
    fn docs_round_trip() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        let mut files = vec![root.join("gamma-spec.md")];
        for dir in ["docs", "docs/guide", "docs/std"] {
            for e in std::fs::read_dir(root.join(dir)).unwrap().flatten() {
                if e.path().extension().is_some_and(|x| x == "md") {
                    files.push(e.path());
                }
            }
        }
        let mut blocks = 0;
        for f in files {
            let text = std::fs::read_to_string(&f).unwrap();
            let mut in_block = false;
            let mut cur = String::new();
            for line in text.lines() {
                if let Some(info) = line.strip_prefix("```") {
                    if in_block {
                        assert_eq!(plain(&highlight(&cur)), cur, "{}", f.display());
                        blocks += 1;
                        cur.clear();
                    }
                    in_block = !in_block && (info.starts_with("gamma"));
                    if in_block {
                        cur.clear();
                    }
                } else if in_block {
                    cur.push_str(line);
                    cur.push('\n');
                }
            }
        }
        assert!(blocks > 100, "found only {blocks} blocks");
    }
}
