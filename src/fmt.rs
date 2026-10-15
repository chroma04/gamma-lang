//! The source formatter behind `gamma fmt`.
//!
//! The formatter works on the token stream and never changes meaning: it keeps
//! every line break of the source (except for surplus blank lines), re-indents
//! lines by bracket nesting, normalizes the spacing between the tokens of a
//! line and copies comments and literals verbatim. The text between two
//! consecutive tokens (the "gap") holds exactly the whitespace, comments and
//! semicolons, and is re-read from the source. Before returning, the result is
//! lexed again and must give the same tokens as the input.

use crate::diag::{Diagnostics, Severity};
use crate::source::FileId;
use crate::syntax::lexer::lex;
use crate::syntax::token::{Keyword, Punct, Token, TokenKind};

/// Formats Gamma source text. Fails, without producing output, if the text has
/// lexical errors.
pub fn format_source(src: &str) -> Result<String, String> {
    let toks = lex_checked(src)?;
    let out = layout(src, &toks);
    let toks2 = lex_checked(&out).map_err(|e| format!("internal error: formatted output does not lex: {}", e))?;
    if let Some(i) = first_difference(src, &toks, &out, &toks2) {
        let show = |s: &str, t: &[Token]| t.get(i).map_or("<end>".to_string(), |t| format!("{:?}", text(s, t)));
        return Err(format!(
            "internal error: formatting would change token {} ({} became {})",
            i,
            show(src, &toks),
            show(&out, &toks2)
        ));
    }
    Ok(out)
}

fn lex_checked(src: &str) -> Result<Vec<Token>, String> {
    let mut diags = Diagnostics::new();
    let lexed = lex(FileId(0), src, &mut diags);
    if let Some(d) = diags.iter().find(|d| d.severity == Severity::Error) {
        let lo = (d.span.lo as usize).min(src.len());
        let before = src.get(..lo).unwrap_or("");
        let line = before.matches('\n').count() + 1;
        let col = before.rsplit('\n').next().map_or(0, |l| l.chars().count()) + 1;
        return Err(format!("{}:{}: {}", line, col, d.message));
    }
    Ok(lexed.tokens)
}

/// The index of the first token at which the streams differ in kind or text.
fn first_difference(a_src: &str, a: &[Token], b_src: &str, b: &[Token]) -> Option<usize> {
    let same = |x: &Token, y: &Token| {
        x.kind == y.kind
            && match x.kind {
                TokenKind::Newline | TokenKind::Eof | TokenKind::Punct(Punct::IsNot | Punct::NotIn) => true,
                _ => text(a_src, x) == text(b_src, y),
            }
    };
    // The end of the text and a line break right before it are not compared: the output
    // always ends in a line break, the source may not.
    let trim = |t: &[Token]| {
        let mut n = t.len();
        if n > 0 && t[n - 1].kind == TokenKind::Eof {
            n -= 1;
        }
        if n > 0 && t[n - 1].kind == TokenKind::Newline {
            n -= 1;
        }
        n
    };
    let (a, b) = (&a[..trim(a)], &b[..trim(b)]);
    (0..a.len().max(b.len())).find(|&i| match (a.get(i), b.get(i)) {
        (Some(x), Some(y)) => !same(x, y),
        _ => true,
    })
}

fn text<'a>(src: &'a str, t: &Token) -> &'a str {
    &src[t.span.lo as usize..t.span.hi as usize]
}

/// What the layout needs to know about one (non-newline) token.
struct Info {
    lo: usize,
    hi: usize,
    text: String,
    /// No space before this token.
    tb: bool,
    /// No space after this token.
    ta: bool,
    open: bool,
    close: bool,
    /// An opening `{` that starts the body of a `match`.
    match_body: bool,
}

/// Keywords that are written tight against a following `(`.
fn tight_paren_keyword(k: Keyword) -> bool {
    matches!(
        k,
        Keyword::Fn
            | Keyword::Struct
            | Keyword::Union
            | Keyword::Effect
            | Keyword::Handler
            | Keyword::Ctl
            | Keyword::Esc
            | Keyword::Import
    )
}

fn is_punct(k: Option<&TokenKind>, p: Punct) -> bool {
    k.is_some_and(|k| k.is_punct(p))
}

/// True if the tokens from `j` on look like the type after an error-union `!`.
fn type_follows(toks: &[Token], mut j: usize) -> bool {
    let kind = |j: usize| toks.get(j).map(|t| &t.kind);
    loop {
        match kind(j) {
            Some(TokenKind::Ident(_) | TokenKind::Underscore) => return true,
            Some(TokenKind::Punct(Punct::Question | Punct::Caret)) => j += 1,
            Some(TokenKind::Punct(Punct::LBracket)) => {
                return match (kind(j + 1), kind(j + 2)) {
                    (Some(TokenKind::Punct(Punct::RBracket)), _) => true,
                    (Some(TokenKind::Punct(Punct::Caret)), Some(TokenKind::Punct(Punct::RBracket))) => true,
                    (Some(TokenKind::Int(_)), Some(TokenKind::Punct(Punct::RBracket))) => {
                        starts_type_after_bracket(kind(j + 3))
                    }
                    _ => false,
                };
            }
            _ => return false,
        }
    }
}

/// True if `next` can follow the `]` of a type prefix such as `[]`, `[^]` or `[N]`.
fn starts_type_after_bracket(next: Option<&TokenKind>) -> bool {
    matches!(
        next,
        Some(
            TokenKind::Ident(_)
                | TokenKind::Underscore
                | TokenKind::Intrinsic(_)
                | TokenKind::Keyword(Keyword::Fn)
                | TokenKind::Punct(Punct::Question | Punct::Caret | Punct::LBracket)
        )
    )
}

struct Open {
    info: usize,
    /// The `[` was not preceded by an operand: a type prefix or an array literal.
    prefix: bool,
    effect: bool,
    /// The `(` is the argument list of an attribute such as `#align(8)`.
    attr: bool,
    /// A `:` was seen in the current statement or argument at this level.
    colon: bool,
}

/// Decides, for every token, whether it is written tight against its
/// neighbours. The key question is whether the previous token ends an operand,
/// which separates binary from prefix operators and postfix from prefix `?`/`^`.
fn classify(src: &str, toks: &[Token]) -> Vec<Info> {
    use TokenKind as K;
    let mut infos: Vec<Info> = Vec::new();
    let mut ends = false;
    let mut prev: Option<&TokenKind> = None;
    let mut stack: Vec<Open> = Vec::new();
    // The same flag for the statements outside every bracket.
    let mut top_colon = false;
    let mut pending_matches: Vec<usize> = Vec::new();
    for (i, t) in toks.iter().enumerate() {
        let (lo, hi) = (t.span.lo as usize, t.span.hi as usize);
        let (mut tb, mut ta, mut open, mut close) = (false, false, false, false);
        let mut match_body = false;
        let mut txt = String::new();
        match &t.kind {
            K::Newline => {
                ends = false;
                prev = None;
                match stack.last_mut() {
                    Some(o) => o.colon = false,
                    None => top_colon = false,
                }
                continue;
            }
            K::Eof => break,
            K::Ident(s) => ends = s.as_str() != "const",
            K::Underscore | K::Int(_) | K::Float(_) | K::Str(_) | K::Intrinsic(_) => ends = true,
            K::Attribute(_) => ends = false,
            K::Keyword(k) => {
                if *k == Keyword::Match {
                    pending_matches.push(stack.len());
                }
                ends = matches!(k, Keyword::True | Keyword::False | Keyword::Null);
            }
            K::Punct(p) => {
                let p = *p;
                match p {
                    Punct::LParen => {
                        tb = ends
                            || matches!(prev, Some(K::Keyword(k)) if tight_paren_keyword(*k))
                            || matches!(prev, Some(K::Attribute(_) | K::Intrinsic(_)))
                            || is_punct(prev, Punct::Bang);
                        ta = true;
                        open = true;
                        let attr = matches!(prev, Some(K::Attribute(_)));
                        stack.push(Open { info: infos.len(), prefix: false, effect: false, attr, colon: false });
                        ends = false;
                    }
                    Punct::LBracket => {
                        tb = ends;
                        ta = true;
                        open = true;
                        stack.push(Open { info: infos.len(), prefix: !ends, effect: false, attr: false, colon: false });
                        ends = false;
                    }
                    Punct::LBrace => {
                        if pending_matches.last() == Some(&stack.len()) {
                            pending_matches.pop();
                            match_body = true;
                        }
                        let effect = is_punct(prev, Punct::Backslash);
                        ta = effect || adjacent_close(src, toks, i);
                        open = true;
                        stack.push(Open { info: infos.len(), prefix: false, effect, attr: false, colon: false });
                        ends = false;
                    }
                    Punct::RParen | Punct::RBracket | Punct::RBrace => {
                        close = true;
                        tb = true;
                        let o = stack.pop();
                        ends = !o.as_ref().is_some_and(|o| o.attr);
                        if p == Punct::RBracket
                            && o.as_ref().is_some_and(|o| o.prefix)
                            && starts_type_after_bracket(toks.get(i + 1).map(|t| &t.kind))
                        {
                            ta = true;
                            ends = false;
                        }
                        if p == Punct::RBrace
                            && let Some(o) = o
                        {
                            tb = o.effect || (o.info + 1 == infos.len() && src[infos[o.info].hi..lo].trim().is_empty());
                        }
                    }
                    Punct::Comma => {
                        tb = true;
                        ends = false;
                        match stack.last_mut() {
                            Some(o) => o.colon = false,
                            None => top_colon = false,
                        }
                    }
                    Punct::Dot | Punct::DotDot | Punct::DotDotEq => {
                        tb = ends;
                        ta = true;
                        // A `.` that starts a statement continues the previous line only
                        // when a name follows immediately (§2.8).
                        if p == Punct::Dot && prev.is_none() && !src[hi..].starts_with(|c: char| !c.is_whitespace()) {
                            ta = false;
                        }
                        // Between two integers (`t.0.1`, `1. 5`) keep the source spacing:
                        // writing `1.` next to `5` would read as a float.
                        if p == Punct::Dot
                            && matches!(prev, Some(K::Int(_)))
                            && toks.get(i + 1).is_some_and(|n| matches!(n.kind, K::Int(_)))
                        {
                            tb = infos.last().is_some_and(|l| l.hi == lo);
                            ta = toks[i + 1].span.lo as usize == hi;
                        }
                        ends = false;
                    }
                    // A label: `name: with …`, also in an expression.
                    Punct::Colon
                        if toks.get(i + 1).is_some_and(|n| {
                            matches!(n.kind, K::Keyword(Keyword::With | Keyword::For | Keyword::While))
                        }) =>
                    {
                        tb = true;
                        ends = false;
                    }
                    Punct::Colon => {
                        // `name: T : value` binds a constant; the second colon is spaced.
                        let seen = match stack.last_mut() {
                            Some(o) => &mut o.colon,
                            None => &mut top_colon,
                        };
                        tb = !*seen;
                        ta = false;
                        *seen = true;
                        ends = false;
                    }
                    Punct::Question | Punct::Caret => {
                        // Postfix after an operand, prefix otherwise.
                        tb = ends;
                        ta = !ends;
                    }
                    Punct::Bang => {
                        if !ends {
                            ta = true;
                        } else if type_follows(toks, i + 1) {
                            // The error-union type `E!T`.
                            tb = true;
                            ta = true;
                            ends = false;
                        } else {
                            tb = true;
                        }
                    }
                    Punct::Minus | Punct::Amp | Punct::Tilde => {
                        ta = !ends;
                        ends = false;
                    }
                    Punct::At | Punct::Hash => ends = false,
                    Punct::Uninit => ends = true,
                    Punct::IsNot | Punct::NotIn => {
                        let t = &src[lo..hi];
                        if !t.contains("/*") && !t.contains('\n') {
                            txt = p.as_str().to_string();
                        }
                        ends = false;
                    }
                    _ => ends = false,
                }
            }
        }
        if txt.is_empty() {
            txt = src[lo..hi].to_string();
        }
        infos.push(Info { lo, hi, text: txt, tb, ta, open, close, match_body });
        prev = Some(&t.kind);
    }
    infos
}

/// True if the token after the `{` at `toks[i]` is `}` with only whitespace between.
fn adjacent_close(src: &str, toks: &[Token], i: usize) -> bool {
    match (toks.get(i), toks.get(i + 1)) {
        (Some(a), Some(b)) if b.kind.is_punct(Punct::RBrace) => {
            src[a.span.hi as usize..b.span.lo as usize].trim().is_empty()
        }
        _ => false,
    }
}

enum Item<'a> {
    Tok(usize),
    Semi,
    Nl,
    Line { text: &'a str, ws: &'a str },
    Block(&'a str),
}

/// Splits the whitespace/comment gap between two tokens into items.
fn scan_gap<'a>(gap: &'a str, items: &mut Vec<Item<'a>>) {
    let b = gap.as_bytes();
    let mut i = 0;
    let mut ws_start = 0;
    while i < b.len() {
        match b[i] {
            b'\n' => {
                items.push(Item::Nl);
                i += 1;
                ws_start = i;
            }
            b';' => {
                items.push(Item::Semi);
                i += 1;
                ws_start = i;
            }
            b'/' if b.get(i + 1) == Some(&b'/') => {
                let end = gap[i..].find('\n').map_or(gap.len(), |n| i + n);
                items.push(Item::Line { text: gap[i..end].trim_end(), ws: &gap[ws_start..i] });
                i = end;
                ws_start = i;
            }
            b'/' if b.get(i + 1) == Some(&b'*') => {
                let mut depth = 1;
                let mut j = i + 2;
                while depth > 0 && j < b.len() {
                    if b[j] == b'/' && b.get(j + 1) == Some(&b'*') {
                        depth += 1;
                        j += 2;
                    } else if b[j] == b'*' && b.get(j + 1) == Some(&b'/') {
                        depth -= 1;
                        j += 2;
                    } else {
                        j += 1;
                    }
                }
                items.push(Item::Block(&gap[i..j]));
                i = j;
                ws_start = i;
            }
            _ => i += 1,
        }
    }
}

#[derive(Clone, Copy)]
enum Prev {
    None,
    Tok(usize),
    Other,
}

/// True if writing two tokens next to each other would lex differently.
fn would_merge(a: &str, b: &str) -> bool {
    let (Some(x), Some(y)) = (a.chars().last(), b.chars().next()) else { return false };
    if "()[]{},;\"'`".contains(x) || "()[]{},;\"'`".contains(y) {
        return false;
    }
    let joined = format!("{}{}", a, b);
    let mut diags = Diagnostics::new();
    let lexed = lex(FileId(0), &joined, &mut diags);
    let n = lexed.tokens.iter().filter(|t| !matches!(t.kind, TokenKind::Newline | TokenKind::Eof)).count();
    n != 2 || diags.has_errors()
}

/// True for a `.` directly followed by a name, which joins the line to the previous one,
/// unless the line is a pattern binding `.V(…) := …` (§2.8).
fn continues_with_name(src: &str, info: &Info) -> bool {
    info.text == "."
        && src[info.hi..].starts_with(|c: char| c.is_ascii_alphabetic() || c == '_')
        && !crate::syntax::lexer::line_binds(&src.as_bytes()[info.hi..])
}

/// True if the lexer joins the next line to a line ending in this token (§2.8).
fn continues_line(info: &Info) -> bool {
    if info.open || info.close {
        return false;
    }
    match info.text.as_str() {
        "is" | "in" | "and" | "or" | "else" => true,
        "," | ";" | "?" | "!" | "^" | "@" | "#" | "..." | "---" => false,
        t => t.chars().next().is_some_and(|c| "+-*/%<>&|~=:.\\".contains(c) || t == "is not" || t == "not in"),
    }
}

/// Whether a run of spaces between `prev` and `cur` is kept: it aligns the
/// operator of consecutive declarations, fields or match arms into a column,
/// so it is kept only when the line above or below has the same operator in
/// the same column (or, after `:`, a token in the same column). Columns count
/// from each line's own indentation, which the formatter rewrites: the
/// decision is the same for the source and for its formatted output.
fn keeps_padding(src: &str, prev: &Info, cur: &Info) -> bool {
    let op = matches!(cur.text.as_str(), "::" | "=>");
    if !op && prev.text != ":" {
        return false;
    }
    let indent_of = |line: &str| line.chars().take_while(|c| c.is_whitespace()).count();
    let line_start = src[..cur.lo].rfind('\n').map_or(0, |i| i + 1);
    let line_end = src[cur.lo..].find('\n').map_or(src.len(), |i| cur.lo + i);
    let above = src[..line_start.saturating_sub(1)].rsplit('\n').next().filter(|_| line_start > 0);
    let below = src.get(line_end + 1..).filter(|_| line_end < src.len()).and_then(|r| r.split('\n').next());
    let own = &src[line_start..line_end];
    let col = column(src, cur.lo).saturating_sub(indent_of(own));
    [above, below].into_iter().flatten().any(|line| {
        let col = col + indent_of(line);
        let rest: String = line.chars().skip(col).collect();
        let before: String = line.chars().take(col).collect();
        let spaced = before.ends_with(' ') && !before.trim().is_empty();
        if op {
            spaced && rest.starts_with(cur.text.as_str())
        } else {
            spaced && rest.starts_with(|c: char| !c.is_whitespace())
        }
    })
}

/// An open bracket during layout.
struct Frame {
    /// The indentation of the line the bracket was opened on.
    base: usize,
    /// A `{` that does not start a `match` body.
    plain: bool,
    /// A `(` or `[`.
    paren: bool,
    /// Where the first token after the bracket on its line is, in the source
    /// and in the output (as columns). Continuation lines that were aligned
    /// to it in the source stay aligned to it.
    visual: Option<(usize, usize)>,
    /// A `{` on a line that starts with `if`, `when` or `else`: an `else` line
    /// right after its `}` continues the chain at the statement's indentation.
    chain: bool,
}

fn column(text: &str, pos: usize) -> usize {
    text[..pos].rsplit('\n').next().map_or(0, |l| l.chars().count())
}

fn layout(src: &str, toks: &[Token]) -> String {
    let infos = classify(src, toks);
    let bom = src.starts_with('\u{feff}');
    let base = if bom { '\u{feff}'.len_utf8() } else { 0 };

    let mut items: Vec<Item> = Vec::new();
    let mut prev_end = base;
    for (k, info) in infos.iter().enumerate() {
        scan_gap(&src[prev_end..info.lo], &mut items);
        items.push(Item::Tok(k));
        prev_end = info.hi;
    }
    scan_gap(&src[prev_end..], &mut items);

    let mut out = String::new();
    if bom {
        out.push('\u{feff}');
    }
    let mut line_open = false;
    let mut nl = 0usize;
    let mut prev = Prev::None;
    // For every open bracket, the indentation of the line it was opened on (its
    // "base") and whether it is a `{` that does not start a `match` body.
    let mut stack: Vec<Frame> = Vec::new();
    // The base for brackets opened on the current line, and the stack depth
    // below which it was last lowered.
    let mut cur_base = 0usize;
    let mut min_len = 0usize;
    // The previous line ends in a token after which the lexer joins the next line.
    let mut cont_next = false;
    let mut after_lbrace = false;
    // The first token of the current line, and whether the previous token was
    // a `}` closing a `chain` block.
    let mut line_head: Option<usize> = None;
    let mut after_chain = false;

    for (idx, item) in items.iter().enumerate() {
        if let Item::Nl = item {
            nl += 1;
            continue;
        }
        let close_tok = matches!(item, Item::Tok(k) if infos[*k].close);
        let close_brace = matches!(item, Item::Tok(k) if infos[*k].close && infos[*k].text == "}");
        let trailing = line_open && nl == 0 && matches!(item, Item::Line { .. });
        if nl > 0 && line_open {
            out.push('\n');
            if nl >= 2 && !after_lbrace && !close_brace {
                out.push('\n');
            }
            line_open = false;
        }
        nl = 0;
        let first = !line_open;
        if first {
            line_head = items[idx..].iter().find_map(|it| if let Item::Tok(j) = it { Some(*j) } else { None });
            let indent = if close_tok {
                let f = stack.pop();
                after_chain = f.as_ref().is_some_and(|f| f.chain) && close_brace;
                let v = f.map_or(0, |f| f.base);
                cur_base = v;
                v * 4
            } else {
                cur_base = stack.last().map_or(0, |f| f.base + 1);
                let plain_ctx = stack.last().is_none_or(|f| f.plain);
                let starts_cont = |j: usize| {
                    matches!(infos[j].text.as_str(), "and" | "or" | "else") || continues_with_name(src, &infos[j])
                };
                let next_tok = items[idx..].iter().find_map(|it| if let Item::Tok(j) = it { Some(*j) } else { None });
                let chain_else = after_chain && matches!(item, Item::Tok(k) if infos[*k].text == "else");
                let cont = plain_ctx
                    && !chain_else
                    && (cont_next
                        || match item {
                            Item::Tok(k) => starts_cont(*k),
                            _ => next_tok.is_some_and(starts_cont),
                        });
                let first_tok = match item {
                    Item::Tok(k) => Some(*k),
                    _ => next_tok,
                };
                let aligned = stack
                    .last()
                    .filter(|f| f.paren)
                    .and_then(|f| f.visual)
                    .filter(|&(orig, _)| first_tok.is_some_and(|k| column(src, infos[k].lo) == orig));
                match aligned {
                    Some((_, col)) => col,
                    None => (cur_base + usize::from(cont)) * 4,
                }
            };
            min_len = stack.len();
            out.push_str(&" ".repeat(indent));
            line_open = true;
            prev = Prev::None;
        } else if close_tok && let Some(f) = stack.pop() {
            after_chain = f.chain && close_brace;
            if stack.len() < min_len {
                cur_base = f.base;
                min_len = stack.len();
            }
        }
        let mut lbrace = false;
        match item {
            Item::Nl => {}
            Item::Semi => {
                out.push(';');
                prev = Prev::Other;
                cont_next = false;
            }
            Item::Line { text, ws } => {
                if !first {
                    if !ws.is_empty() && ws.bytes().all(|b| b == b' ') {
                        out.push_str(ws);
                    } else {
                        out.push(' ');
                    }
                }
                out.push_str(text);
                prev = Prev::Other;
            }
            Item::Block(text) => {
                if !first && !matches!(prev, Prev::Tok(p) if infos[p].ta) {
                    out.push(' ');
                }
                out.push_str(text);
                prev = Prev::Other;
            }
            Item::Tok(k) => {
                let info = &infos[*k];
                let mut pad = 1;
                let space = match prev {
                    Prev::None => false,
                    Prev::Tok(p) => {
                        let tight = infos[p].ta || info.tb;
                        let gap = &src[infos[p].hi..info.lo];
                        let word = |c: Option<char>| c.is_some_and(|c| c.is_alphanumeric() || "_\"'`".contains(c));
                        if !tight
                            && gap.len() >= 2
                            && gap.bytes().all(|b| b == b' ')
                            && keeps_padding(src, &infos[p], info)
                        {
                            // Columns of aligned declarations and match arms.
                            pad = gap.len();
                        }
                        // Words that touch in the source (`t.1u8`) stay together.
                        let glued =
                            gap.is_empty() && word(infos[p].text.chars().last()) && word(info.text.chars().next());
                        !glued && (!tight || would_merge(&infos[p].text, &info.text))
                    }
                    Prev::Other => !info.tb,
                };
                if space {
                    for _ in 0..pad {
                        out.push(' ');
                    }
                }
                if let (Prev::Tok(p), Some(f)) = (prev, stack.last_mut())
                    && f.paren
                    && f.visual.is_none()
                    && infos[p].open
                    && !info.close
                {
                    let out_col = out.rsplit('\n').next().map_or(0, |l| l.chars().count());
                    f.visual = Some((column(src, info.lo), out_col));
                }
                out.push_str(&info.text);
                if info.open {
                    let brace = info.text == "{";
                    let chain =
                        brace && line_head.is_some_and(|h| matches!(infos[h].text.as_str(), "if" | "when" | "else"));
                    stack.push(Frame {
                        base: cur_base,
                        plain: brace && !info.match_body,
                        paren: !brace,
                        visual: None,
                        chain,
                    });
                }
                cont_next = continues_line(info);
                if !info.close {
                    after_chain = false;
                }
                lbrace = info.text == "{" && !info.ta;
                prev = Prev::Tok(*k);
            }
        }
        after_lbrace = lbrace || (trailing && after_lbrace);
    }
    if line_open {
        out.push('\n');
    }
    out
}
