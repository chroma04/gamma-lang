//! The lexer (spec §2).
//!
//! [`lex`] turns source text into tokens. Besides splitting the text it applies
//! the newline rules of §2.8 (deciding which line terminators become
//! [`TokenKind::Newline`] tokens) and forms the two-word operators `is not` and
//! `not in`. Lexical errors are reported to the [`Diagnostics`] and lexing
//! continues, so the result is always a complete token stream ending in `Eof`.

mod literal;
#[cfg(test)]
mod tests;

use crate::diag::{Diagnostic, Diagnostics};
use crate::source::{FileId, Span};
use crate::symbol::Symbol;
use crate::syntax::token::{DocComment, Keyword, LexOutput, Punct, Token, TokenKind};

/// Converts source text into tokens, applying the newline rules of §2.8.
pub fn lex(file: FileId, src: &str, diags: &mut Diagnostics) -> LexOutput {
    Lexer::new(file, src, diags).run()
}

/// [`lex`] for tools that keep going after errors. If the text has errors, it is lexed
/// again with a layout heuristic: an open bracket is taken to be unclosed (and closed
/// for the parser) where a line begins in the first column with a top-level declaration
/// (`name ::`, `pub`, `handler`, `test "…"` or an attribute). Without it, one missing
/// `}` or `)` makes the rest of the file part of one declaration.
pub fn lex_recovering(file: FileId, src: &str, diags: &mut Diagnostics) -> LexOutput {
    let mut first = Diagnostics::new();
    let out = Lexer::new(file, src, &mut first).run();
    if !first.has_errors() {
        for d in first.into_vec() {
            diags.push(d);
        }
        return out;
    }
    let mut lexer = Lexer::new(file, src, diags);
    lexer.recover = true;
    lexer.run()
}

fn is_ident_start(b: u8) -> bool {
    b.is_ascii_alphabetic() || b == b'_'
}

fn is_ident_cont(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

/// The kind of an open bracket on the lexer's bracket stack.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum BracketKind {
    Paren,
    Bracket,
    Brace,
    /// The `{` that opens a `match` body (§2.8).
    MatchBrace,
}

impl BracketKind {
    fn opener(self) -> Punct {
        match self {
            BracketKind::Paren => Punct::LParen,
            BracketKind::Bracket => Punct::LBracket,
            BracketKind::Brace | BracketKind::MatchBrace => Punct::LBrace,
        }
    }

    fn closer(self) -> Punct {
        match self {
            BracketKind::Paren => Punct::RParen,
            BracketKind::Bracket => Punct::RBracket,
            BracketKind::Brace | BracketKind::MatchBrace => Punct::RBrace,
        }
    }
}

struct Bracket {
    kind: BracketKind,
    /// Where the opening bracket is, for "unclosed" diagnostics.
    span: Span,
}

struct Lexer<'a> {
    file: FileId,
    src: &'a str,
    bytes: &'a [u8],
    pos: usize,
    diags: &'a mut Diagnostics,
    tokens: Vec<Token>,
    docs: Vec<DocComment>,
    /// Currently open brackets, innermost last.
    brackets: Vec<Bracket>,
    /// Bracket depths at which a `match` keyword was seen and whose body `{` has
    /// not been opened yet; the innermost is last.
    pending_matches: Vec<usize>,
    /// The span of the first line terminator seen since the last token. Whether
    /// it becomes a `Newline` token is decided when the next token is known.
    pending_newline: Option<Span>,
    /// See [`LexOutput::joined_lines`].
    joined_lines: Vec<u32>,
    /// Close open brackets where a declaration starts in the first column.
    recover: bool,
}

impl<'a> Lexer<'a> {
    fn new(file: FileId, src: &'a str, diags: &'a mut Diagnostics) -> Lexer<'a> {
        Lexer {
            file,
            src,
            bytes: src.as_bytes(),
            pos: 0,
            diags,
            tokens: Vec::new(),
            docs: Vec::new(),
            brackets: Vec::new(),
            pending_matches: Vec::new(),
            pending_newline: None,
            joined_lines: Vec::new(),
            recover: false,
        }
    }

    fn run(mut self) -> LexOutput {
        if self.src.starts_with('\u{feff}') {
            self.pos = '\u{feff}'.len_utf8();
        }
        loop {
            self.skip_trivia();
            match self.peek() {
                Some(b) => self.lex_token(b),
                None => break,
            }
        }
        let end = self.src.len();
        self.flush_newline(&TokenKind::Eof, end);
        for i in 0..self.brackets.len() {
            let b = &self.brackets[i];
            let msg = format!("unclosed `{}`", b.kind.opener().as_str());
            let span = b.span;
            self.diags.error(span, msg);
        }
        let eof = self.span(end, end);
        self.tokens.push(Token { kind: TokenKind::Eof, span: eof });
        LexOutput { tokens: self.tokens, docs: self.docs, joined_lines: self.joined_lines }
    }

    // ----- helpers -----

    fn span(&self, lo: usize, hi: usize) -> Span {
        Span::new(self.file, lo as u32, hi as u32)
    }

    fn error(&mut self, lo: usize, hi: usize, msg: impl Into<String>) {
        let span = self.span(lo, hi);
        self.diags.error(span, msg);
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn peek_at(&self, n: usize) -> Option<u8> {
        self.bytes.get(self.pos + n).copied()
    }

    /// The character starting at `self.pos`, which must not be at the end.
    fn current_char(&self) -> char {
        self.src[self.pos..].chars().next().unwrap_or('\0')
    }

    // ----- whitespace and comments -----

    fn skip_trivia(&mut self) {
        while let Some(b) = self.peek() {
            match b {
                b' ' | b'\t' => self.pos += 1,
                b'\n' => {
                    self.note_newline(self.pos, self.pos + 1);
                    self.pos += 1;
                }
                b'\r' => {
                    if self.peek_at(1) == Some(b'\n') {
                        self.note_newline(self.pos, self.pos + 2);
                        self.pos += 2;
                    } else {
                        self.lone_cr();
                    }
                }
                b'/' => match self.peek_at(1) {
                    Some(b'/') => self.line_comment(),
                    Some(b'*') => self.block_comment(),
                    _ => break,
                },
                _ => break,
            }
        }
    }

    /// Reports a CR that is not part of a CR LF pair and skips it.
    fn lone_cr(&mut self) {
        self.error(self.pos, self.pos + 1, "a carriage return must be followed by a line feed");
        self.pos += 1;
    }

    fn note_newline(&mut self, lo: usize, hi: usize) {
        if self.pending_newline.is_none() {
            self.pending_newline = Some(self.span(lo, hi));
        }
    }

    fn line_comment(&mut self) {
        let lo = self.pos;
        let is_doc = self.peek_at(2) == Some(b'/') && self.peek_at(3) != Some(b'/');
        let mut i = lo + 2;
        while let Some(&b) = self.bytes.get(i) {
            match b {
                b'\n' => break,
                b'\r' if self.bytes.get(i + 1) == Some(&b'\n') => break,
                b'\r' => self.error(i, i + 1, "a carriage return must be followed by a line feed"),
                _ => {}
            }
            i += 1;
        }
        if is_doc {
            let text = &self.src[lo + 3..i];
            let text = text.strip_prefix(' ').unwrap_or(text).to_string();
            let span = self.span(lo, i);
            self.docs.push(DocComment { span, text });
        }
        self.pos = i;
    }

    /// Skips a (possibly nested) block comment starting at `self.pos`.
    fn block_comment(&mut self) {
        let lo = self.pos;
        let mut i = lo + 2;
        let mut depth = 1;
        while depth > 0 {
            match (self.bytes.get(i), self.bytes.get(i + 1)) {
                (None, _) => {
                    self.error(lo, lo + 2, "unterminated block comment");
                    break;
                }
                (Some(b'/'), Some(b'*')) => {
                    depth += 1;
                    i += 2;
                }
                (Some(b'*'), Some(b'/')) => {
                    depth -= 1;
                    i += 2;
                }
                (Some(b'\n'), _) => {
                    self.note_newline(i, i + 1);
                    i += 1;
                }
                (Some(b'\r'), Some(b'\n')) => {
                    self.note_newline(i, i + 2);
                    i += 2;
                }
                (Some(b'\r'), _) => {
                    self.error(i, i + 1, "a carriage return must be followed by a line feed");
                    i += 1;
                }
                _ => i += 1,
            }
        }
        self.pos = i;
    }

    /// Looks past spaces, tabs and block comments that contain no line
    /// terminator for the word `word` (not followed by an identifier character).
    /// Returns the position just after it. Used to form `is not` and `not in`.
    fn second_word_end(&self, mut i: usize, word: &str) -> Option<usize> {
        loop {
            match self.bytes.get(i) {
                Some(b' ' | b'\t') => i += 1,
                Some(b'/') if self.bytes.get(i + 1) == Some(&b'*') => i = self.inline_block_comment_end(i)?,
                _ => break,
            }
        }
        let rest = &self.bytes[i..];
        let end = i + word.len();
        if rest.starts_with(word.as_bytes()) && !self.bytes.get(end).is_some_and(|&b| is_ident_cont(b)) {
            Some(end)
        } else {
            None
        }
    }

    /// The end of the block comment starting at `lo`, or `None` if it is
    /// unterminated or contains a line terminator.
    fn inline_block_comment_end(&self, lo: usize) -> Option<usize> {
        let mut i = lo + 2;
        let mut depth = 1;
        while depth > 0 {
            match (self.bytes.get(i)?, self.bytes.get(i + 1)) {
                (b'/', Some(b'*')) => {
                    depth += 1;
                    i += 2;
                }
                (b'*', Some(b'/')) => {
                    depth -= 1;
                    i += 2;
                }
                (b'\n' | b'\r', _) => return None,
                _ => i += 1,
            }
        }
        Some(i)
    }

    // ----- tokens -----

    fn lex_token(&mut self, b: u8) {
        let lo = self.pos;
        match b {
            b'a'..=b'z' | b'A'..=b'Z' | b'_' => self.lex_word(),
            b'0'..=b'9' => self.lex_number(),
            b'"' => self.lex_string(lo, crate::syntax::token::StrKind::Regular),
            b'`' => self.lex_raw_string(),
            b'\'' => self.lex_char(),
            b';' => {
                self.pos += 1;
                self.emit_semicolon(lo);
            }
            b'@' | b'#' => {
                let is_ident = self.peek_at(1).is_some_and(is_ident_start);
                if is_ident {
                    self.pos += 1;
                    let name = self.scan_ident();
                    let sym = Symbol::intern(name);
                    let kind = if b == b'@' { TokenKind::Intrinsic(sym) } else { TokenKind::Attribute(sym) };
                    self.emit(kind, lo, self.pos);
                } else {
                    self.pos += 1;
                    let p = if b == b'@' { Punct::At } else { Punct::Hash };
                    self.emit(TokenKind::Punct(p), lo, self.pos);
                }
            }
            // C's `==` and `!=`: report them, and read them as `is` and `is not`.
            b'=' | b'!' if self.bytes.get(self.pos + 1) == Some(&b'=') => {
                let eq = self.bytes[self.pos] == b'=';
                self.pos += 2;
                let (op, kind) = if eq {
                    ("is", TokenKind::Keyword(Keyword::Is))
                } else {
                    ("is not", TokenKind::Punct(Punct::IsNot))
                };
                let written = if eq { "==" } else { "!=" };
                // The word needs a space on each side (`a==b` becomes `a is b`).
                let gap = |at: Option<&u8>| if at.is_some_and(|c| !c.is_ascii_whitespace()) { " " } else { "" };
                let before = gap(lo.checked_sub(1).and_then(|i| self.bytes.get(i)));
                let after = gap(self.bytes.get(self.pos));
                let d = Diagnostic::error(
                    self.span(lo, self.pos),
                    format!("`{}` is not an operator; compare with `{}` (§7.6)", written, op),
                )
                .with_fix(
                    format!("write `{op}`"),
                    self.span(lo, self.pos),
                    format!("{before}{op}{after}"),
                );
                self.diags.push(d);
                self.emit(kind, lo, self.pos);
            }
            _ => match self.scan_punct() {
                Some(p) => self.emit(TokenKind::Punct(p), lo, self.pos),
                None => self.unexpected_char(),
            },
        }
    }

    /// Consumes an identifier at `self.pos` and returns its text.
    fn scan_ident(&mut self) -> &'a str {
        let start = self.pos;
        while self.peek().is_some_and(is_ident_cont) {
            self.pos += 1;
        }
        &self.src[start..self.pos]
    }

    /// Lexes an identifier, keyword, `_`, or the start of a C string.
    fn lex_word(&mut self) {
        let lo = self.pos;
        let text = self.scan_ident();
        if text == "c" && self.peek() == Some(b'"') {
            self.lex_string(lo, crate::syntax::token::StrKind::C);
            return;
        }
        if text == "_" {
            self.emit(TokenKind::Underscore, lo, self.pos);
            return;
        }
        let Some(kw) = Keyword::from_str(text) else {
            self.emit(TokenKind::Ident(Symbol::intern(text)), lo, self.pos);
            return;
        };
        let two_word = match kw {
            Keyword::Is => self.second_word_end(self.pos, "not").map(|end| (Punct::IsNot, end)),
            Keyword::Not => self.second_word_end(self.pos, "in").map(|end| (Punct::NotIn, end)),
            _ => None,
        };
        match two_word {
            Some((p, end)) => {
                self.pos = end;
                self.emit(TokenKind::Punct(p), lo, end);
            }
            None => self.emit(TokenKind::Keyword(kw), lo, self.pos),
        }
    }

    /// Reports a character that cannot start any token and skips it.
    fn unexpected_char(&mut self) {
        let lo = self.pos;
        let c = self.current_char();
        self.pos += c.len_utf8();
        let msg = if c.is_whitespace() && !c.is_ascii() {
            format!("unexpected whitespace character U+{:04X}; only space and tab are whitespace", c as u32)
        } else if !c.is_ascii() {
            format!(
                "unexpected character `{}` (U+{:04X}); non-ASCII characters are only allowed in comments, \
                 strings and character literals",
                c, c as u32
            )
        } else if c.is_ascii_graphic() {
            format!("unexpected character `{}`", c)
        } else {
            format!("unexpected character U+{:04X}", c as u32)
        };
        self.error(lo, self.pos, msg);
    }

    /// Scans an operator or punctuation token by maximal munch (§2.6) and
    /// advances past it. Returns `None` (consuming nothing) if there is none.
    fn scan_punct(&mut self) -> Option<Punct> {
        use Punct::*;
        let rest = &self.bytes[self.pos..];
        let at = |i: usize| rest.get(i).copied();
        let (p, len) = match rest[0] {
            b'+' | b'-' | b'*' => {
                let (plain, assign, wrap, sat, opt, wrap_assign, sat_assign) = match rest[0] {
                    b'+' => (Plus, PlusAssign, PlusWrap, PlusSat, PlusOpt, PlusWrapAssign, PlusSatAssign),
                    b'-' => (Minus, MinusAssign, MinusWrap, MinusSat, MinusOpt, MinusWrapAssign, MinusSatAssign),
                    _ => (Star, StarAssign, StarWrap, StarSat, StarOpt, StarWrapAssign, StarSatAssign),
                };
                match at(1) {
                    Some(b'-') if rest[0] == b'-' && at(2) == Some(b'-') => (Uninit, 3),
                    Some(b'>') if rest[0] == b'-' => (Arrow, 2),
                    Some(b'=') => (assign, 2),
                    Some(b'%') if at(2) == Some(b'=') => (wrap_assign, 3),
                    Some(b'%') => (wrap, 2),
                    Some(b'|') if at(2) == Some(b'=') => (sat_assign, 3),
                    Some(b'|') => (sat, 2),
                    Some(b'?') => (opt, 2),
                    _ => (plain, 1),
                }
            }
            b'/' => {
                if at(1) == Some(b'=') {
                    (SlashAssign, 2)
                } else {
                    (Slash, 1)
                }
            }
            b'%' => {
                if at(1) == Some(b'=') {
                    (PercentAssign, 2)
                } else {
                    (Percent, 1)
                }
            }
            b'<' => match (at(1), at(2), at(3)) {
                (Some(b'<'), Some(b'%'), Some(b'=')) => (ShlWrapAssign, 4),
                (Some(b'<'), Some(b'|'), Some(b'=')) => (ShlSatAssign, 4),
                (Some(b'<'), Some(b'%'), _) => (ShlWrap, 3),
                (Some(b'<'), Some(b'|'), _) => (ShlSat, 3),
                (Some(b'<'), Some(b'?'), _) => (ShlOpt, 3),
                (Some(b'<'), Some(b'='), _) => (ShlAssign, 3),
                (Some(b'<'), _, _) => (Shl, 2),
                (Some(b'='), _, _) => (Le, 2),
                _ => (Lt, 1),
            },
            b'>' => match (at(1), at(2)) {
                (Some(b'>'), Some(b'=')) => (ShrAssign, 3),
                (Some(b'>'), _) => (Shr, 2),
                (Some(b'='), _) => (Ge, 2),
                _ => (Gt, 1),
            },
            b'&' => {
                if at(1) == Some(b'=') {
                    (AmpAssign, 2)
                } else {
                    (Amp, 1)
                }
            }
            b'|' => {
                if at(1) == Some(b'=') {
                    (PipeAssign, 2)
                } else {
                    (Pipe, 1)
                }
            }
            b'~' => {
                if at(1) == Some(b'=') {
                    (TildeAssign, 2)
                } else {
                    (Tilde, 1)
                }
            }
            b'=' => {
                if at(1) == Some(b'>') {
                    (FatArrow, 2)
                } else {
                    (Assign, 1)
                }
            }
            b'.' => match (at(1), at(2)) {
                (Some(b'.'), Some(b'.')) => (Ellipsis, 3),
                (Some(b'.'), Some(b'=')) => (DotDotEq, 3),
                (Some(b'.'), _) => (DotDot, 2),
                _ => (Dot, 1),
            },
            b':' => match at(1) {
                Some(b':') => (ColonColon, 2),
                Some(b'=') => (ColonEq, 2),
                _ => (Colon, 1),
            },
            b'?' => (Question, 1),
            b'!' => (Bang, 1),
            b'^' => (Caret, 1),
            b'\\' => (Backslash, 1),
            b',' => (Comma, 1),
            b'(' => (LParen, 1),
            b')' => (RParen, 1),
            b'[' => (LBracket, 1),
            b']' => (RBracket, 1),
            b'{' => (LBrace, 1),
            b'}' => (RBrace, 1),
            _ => return None,
        };
        self.pos += len;
        Some(p)
    }

    // ----- emitting tokens and newlines -----

    /// Appends a token, first resolving any pending newline and updating the
    /// bracket stack.
    fn emit(&mut self, kind: TokenKind, lo: usize, hi: usize) {
        let span = self.span(lo, hi);
        if let TokenKind::Punct(p @ (Punct::RParen | Punct::RBracket | Punct::RBrace)) = kind
            && !self.brackets.iter().any(|b| b.kind.closer() == p)
        {
            self.error(lo, hi, format!("unmatched closing `{}`", p.as_str()));
            return;
        }
        if self.recover && !self.brackets.is_empty() && self.declaration_starts_at(lo, &kind) {
            self.close_all_brackets(lo);
        }
        self.flush_newline(&kind, hi);
        match kind {
            TokenKind::Punct(Punct::LParen) => self.open_bracket(BracketKind::Paren, span),
            TokenKind::Punct(Punct::LBracket) => self.open_bracket(BracketKind::Bracket, span),
            TokenKind::Punct(Punct::LBrace) => {
                let is_match_body = self.pending_matches.last() == Some(&self.brackets.len());
                if is_match_body {
                    self.pending_matches.pop();
                }
                self.open_bracket(if is_match_body { BracketKind::MatchBrace } else { BracketKind::Brace }, span);
            }
            TokenKind::Punct(p @ (Punct::RParen | Punct::RBracket | Punct::RBrace)) => self.close_bracket(p, span),
            TokenKind::Keyword(Keyword::Match) => self.pending_matches.push(self.brackets.len()),
            _ => {}
        }
        self.tokens.push(Token { kind, span });
    }

    /// Whether the token of `kind` at `lo` begins a line in the first column and looks like
    /// the start of a top-level declaration.
    fn declaration_starts_at(&self, lo: usize, kind: &TokenKind) -> bool {
        if lo != 0 && self.bytes[lo - 1] != b'\n' {
            return false;
        }
        let rest = &self.src[lo..];
        let word_len = rest.bytes().take_while(|b| is_ident_cont(*b)).count();
        let (word, after) = rest.split_at(word_len);
        let after = after.trim_start_matches([' ', '\t']);
        match kind {
            TokenKind::Attribute(_) => true,
            TokenKind::Keyword(Keyword::Pub | Keyword::Handler) => true,
            TokenKind::Ident(_) => {
                after.starts_with("::") || (matches!(word, "test" | "bench") && after.starts_with('"'))
            }
            _ => false,
        }
    }

    /// Closes every open bracket before the declaration at `lo`, reporting each as unclosed.
    fn close_all_brackets(&mut self, lo: usize) {
        self.pending_newline = None;
        let here = self.span(lo, lo);
        while let Some(b) = self.brackets.pop() {
            let msg = format!("unclosed `{}`", b.kind.opener().as_str());
            let note = format!("a declaration starts here, so this `{}` was not closed", b.kind.opener().as_str());
            self.diags.push(Diagnostic::error(b.span, msg).with_label(here, note));
            self.tokens.push(Token { kind: TokenKind::Punct(b.kind.closer()), span: here });
        }
        self.pending_matches.clear();
        self.push_newline(here);
    }

    fn open_bracket(&mut self, kind: BracketKind, span: Span) {
        self.brackets.push(Bracket { kind, span });
    }

    /// Pops the bracket closed by `closer`, which is known to be open. Brackets
    /// opened after it were never closed and are reported.
    fn close_bracket(&mut self, closer: Punct, span: Span) {
        let i = self.brackets.iter().rposition(|b| b.kind.closer() == closer).unwrap_or(0);
        for skipped in self.brackets.split_off(i + 1) {
            let msg = format!("unclosed `{}`", skipped.kind.opener().as_str());
            let note =
                format!("this `{}` does not match the open `{}`", closer.as_str(), skipped.kind.opener().as_str());
            self.diags.push(Diagnostic::error(skipped.span, msg).with_label(span, note));
        }
        self.brackets.truncate(i);
        let depth = self.brackets.len();
        while self.pending_matches.last().is_some_and(|&d| d > depth) {
            self.pending_matches.pop();
        }
    }

    fn emit_semicolon(&mut self, lo: usize) {
        self.flush_newline(&TokenKind::Newline, lo);
        let span = self.span(lo, lo + 1);
        self.push_newline(span);
    }

    /// Appends a `Newline` unless it would be first or repeat the previous token.
    fn push_newline(&mut self, span: Span) {
        if self.tokens.last().is_some_and(|t| t.kind != TokenKind::Newline) {
            self.tokens.push(Token { kind: TokenKind::Newline, span });
        }
    }

    /// Decides whether the pending line terminator (if any) is significant given
    /// the token about to be emitted, which ends at byte `next_hi` (§2.8).
    fn flush_newline(&mut self, next: &TokenKind, next_hi: usize) {
        let Some(span) = self.pending_newline.take() else { return };
        let innermost = self.brackets.last().map(|b| b.kind);
        if matches!(innermost, Some(BracketKind::Paren | BracketKind::Bracket)) {
            return;
        }
        let Some(last) = self.tokens.last() else { return };
        if suppresses_newline_after(&last.kind) {
            self.joined_lines.push(span.lo);
            return;
        }
        if innermost != Some(BracketKind::MatchBrace) {
            // A line `.V(x) := v else …` is a pattern binding, not a continuation.
            let dot_name = next.is_punct(Punct::Dot)
                && self.bytes.get(next_hi).is_some_and(|&b| is_ident_start(b))
                && !line_binds(&self.bytes[next_hi..]);
            if dot_name || starts_continuation(next) {
                return;
            }
        }
        self.push_newline(span);
    }
}

/// Rule 1 of §2.8: a newline after this token is not significant.
fn suppresses_newline_after(kind: &TokenKind) -> bool {
    match kind {
        TokenKind::Keyword(k) => {
            matches!(k, Keyword::Is | Keyword::In | Keyword::And | Keyword::Or | Keyword::Else | Keyword::Needs)
        }
        // Every operator except the postfix/sigil ones, `...`, `---` and closing brackets.
        TokenKind::Punct(p) => !matches!(
            p,
            Punct::Question
                | Punct::Bang
                | Punct::Caret
                | Punct::At
                | Punct::Hash
                | Punct::Ellipsis
                | Punct::Uninit
                | Punct::RParen
                | Punct::RBracket
                | Punct::RBrace
        ),
        _ => false,
    }
}

/// True if the rest of a line, `rest`, has a `:=` outside brackets, which makes a line
/// starting with `.name` a pattern binding (§2.8 rule 2).
pub(crate) fn line_binds(rest: &[u8]) -> bool {
    let mut depth = 0i32;
    let mut i = 0;
    while i < rest.len() {
        match rest[i] {
            b'\n' => return false,
            b'(' | b'[' | b'{' => depth += 1,
            b')' | b']' | b'}' => {
                depth -= 1;
                if depth < 0 {
                    return false;
                }
            }
            b'"' | b'\'' => {
                let q = rest[i];
                i += 1;
                while i < rest.len() && rest[i] != q && rest[i] != b'\n' {
                    i += if rest[i] == b'\\' { 2 } else { 1 };
                }
            }
            b'/' if rest.get(i + 1) == Some(&b'/') => return false,
            b'`' => return false,
            b':' if depth == 0 && rest.get(i + 1) == Some(&b'=') => return true,
            _ => {}
        }
        i += 1;
    }
    false
}

/// Rule 2 of §2.8 (except for `.name`, which needs the following byte): a
/// newline before this token is not significant.
fn starts_continuation(kind: &TokenKind) -> bool {
    matches!(kind, TokenKind::Keyword(Keyword::Else | Keyword::And | Keyword::Or))
}
