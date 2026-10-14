//! The parser (spec Appendix A).
//!
//! A hand-written recursive-descent parser with precedence climbing for
//! expressions. Types are expressions (spec §5), so there is no separate type
//! AST; "type positions" use a dedicated entry point ([`Parser::parse_type`])
//! in which `!` is the infix error-union operator.
//!
//! # Error handling
//!
//! Most parse functions return a [`PResult`]. An `Err(Abort)` means a syntax
//! error has already been reported; it unwinds (via `?`) to the nearest
//! recovery point — a statement, a declaration, a match arm, a field, … — which
//! skips to the end of the construct and carries on. Precedence errors such as
//! mixing `and` with `or` are reported without aborting, since the tree is still
//! well formed.
//!
//! # Speculation
//!
//! A few rules of Appendix A.7 need unbounded lookahead (a pattern before `:=`,
//! a type after `!`). [`Parser::speculate`] runs a parse with diagnostics
//! suppressed and rolls the token position back if it fails.
//!
//! # Context restrictions
//!
//! [`Restr`] carries the context-dependent switches: `no_brace` while parsing
//! the head of `if`/`while`/`for`/`match`/`with`/`when` (A.7 rule 4), `no_and`
//! while parsing one part of a condition, and `in_index` inside `[ ]` so that
//! `a[lo..]` may omit the upper bound. Parentheses, brackets and blocks reset
//! them.

mod expr;
mod items;
mod lits;
mod pattern;
mod stmt;
#[cfg(test)]
mod tests;
mod types;

use crate::diag::{Diagnostic, Diagnostics};
use crate::source::{FileId, Span};
use crate::symbol::Symbol;
use crate::syntax::ast::{Expr, ExprKind, File, Ident, Item, NodeIdGen};
use crate::syntax::token::{DocComment, Keyword, LexOutput, Punct, Token, TokenKind};

/// Marker for a syntax error that has already been reported.
#[derive(Debug, Clone, Copy)]
pub(super) struct Abort;

pub(super) type PResult<T> = Result<T, Abort>;

/// Maximum nesting of expressions, types and patterns before the parser gives up
/// (protects the native stack).
const MAX_DEPTH: u32 = 64;

/// Context-dependent restrictions on what the expression parser accepts.
#[derive(Clone, Copy, Default, Debug)]
pub(super) struct Restr {
    /// A `{` at this level begins a body, so no construct containing a `{` may
    /// be parsed (A.7 rule 4).
    pub no_brace: bool,
    /// A top-level `and` ends the expression (conditions, A.7 rule 2).
    pub no_and: bool,
    /// Directly inside `[ ]` after an operand: `a..` may omit its upper bound.
    pub in_index: bool,
}

/// Interned contextual keywords.
struct Words {
    const_: Symbol,
    on: Symbol,
    where_: Symbol,
    test: Symbol,
    bench: Symbol,
    cont: Symbol,
    global: Symbol,
}

/// A saved parser state for [`Parser::speculate`].
struct Snapshot {
    pos: usize,
    last_hi: u32,
    last_err_pos: Option<usize>,
    err_count: usize,
    depth: u32,
    labels: usize,
    restr: Restr,
}

pub(super) struct Parser<'a> {
    file: FileId,
    toks: &'a [Token],
    docs: &'a [DocComment],
    ids: &'a mut NodeIdGen,
    diags: &'a mut Diagnostics,
    pos: usize,
    /// End offset of the last consumed token that is not a newline.
    last_hi: u32,
    /// Labels of the enclosing `while`/`for`/`with`, innermost last (A.7 rule 11).
    labels: Vec<Symbol>,
    restr: Restr,
    depth: u32,
    /// Number of errors raised, including suppressed ones (speculation checks it).
    err_count: usize,
    /// Token index of the last reported error, to avoid cascades.
    last_err_pos: Option<usize>,
    words: Words,
    /// Tokens consumed by failed speculation so far; speculation stops paying
    /// off (and is refused) once this exceeds `spec_limit`, which keeps
    /// pathological input from taking exponential time.
    spec_waste: usize,
    spec_limit: usize,
    /// True if line terminators in the file are CR LF (for doc comment placement).
    crlf: bool,
    /// Keywords already reported as misused names; later uses of the same word
    /// in an expression are read as that name, so one mistake gives one error.
    kw_names: Vec<Keyword>,
}

/// Parses one lexed file into an AST, reporting syntax errors to `diags`.
pub fn parse_file(file: FileId, lexed: &LexOutput, ids: &mut NodeIdGen, diags: &mut Diagnostics) -> File {
    let fallback;
    let toks: &[Token] = if matches!(lexed.tokens.last(), Some(Token { kind: TokenKind::Eof, .. })) {
        &lexed.tokens
    } else {
        let end = lexed.tokens.last().map_or(0, |t| t.span.hi);
        let mut v = lexed.tokens.clone();
        v.push(Token { kind: TokenKind::Eof, span: Span::new(file, end, end) });
        fallback = v;
        &fallback
    };
    let mut p = Parser::new(file, toks, &lexed.docs, ids, diags);
    let items = p.parse_file_items();
    let end = toks.last().map_or(0, |t| t.span.hi);
    File { items, span: Span::new(file, 0, end) }
}

impl<'a> Parser<'a> {
    fn new(
        file: FileId,
        toks: &'a [Token],
        docs: &'a [DocComment],
        ids: &'a mut NodeIdGen,
        diags: &'a mut Diagnostics,
    ) -> Parser<'a> {
        let crlf = toks.iter().any(|t| t.kind == TokenKind::Newline && t.span.len() == 2);
        Parser {
            file,
            toks,
            docs,
            ids,
            diags,
            pos: 0,
            last_hi: 0,
            labels: Vec::new(),
            restr: Restr::default(),
            depth: 0,
            err_count: 0,
            last_err_pos: None,
            words: Words {
                const_: Symbol::intern("const"),
                on: Symbol::intern("on"),
                where_: Symbol::intern("where"),
                test: Symbol::intern("test"),
                bench: Symbol::intern("bench"),
                cont: Symbol::intern("cont"),
                global: Symbol::intern("global"),
            },
            spec_waste: 0,
            spec_limit: toks.len() * 50 + 1000,
            crlf,
            kw_names: Vec::new(),
        }
    }

    // ------------------------------------------------------------------
    // Token access
    // ------------------------------------------------------------------

    pub(super) fn tok(&self) -> &'a Token {
        &self.toks[self.pos.min(self.toks.len() - 1)]
    }

    pub(super) fn kind(&self) -> &'a TokenKind {
        &self.tok().kind
    }

    pub(super) fn nth(&self, n: usize) -> &'a TokenKind {
        &self.toks[(self.pos + n).min(self.toks.len() - 1)].kind
    }

    pub(super) fn span(&self) -> Span {
        self.tok().span
    }

    pub(super) fn lo(&self) -> u32 {
        self.span().lo
    }

    /// Consumes the current token (never moves past `Eof`).
    pub(super) fn bump(&mut self) -> &'a Token {
        let t = self.tok();
        if t.kind != TokenKind::Eof {
            self.pos += 1;
            if t.kind != TokenKind::Newline {
                self.last_hi = t.span.hi;
            }
        }
        t
    }

    /// The span from `start` to the end of the last consumed token.
    pub(super) fn span_from(&self, start: u32) -> Span {
        Span::new(self.file, start, self.last_hi.max(start))
    }

    pub(super) fn at(&self, p: Punct) -> bool {
        self.kind().is_punct(p)
    }

    pub(super) fn nth_is(&self, n: usize, p: Punct) -> bool {
        self.nth(n).is_punct(p)
    }

    pub(super) fn eat(&mut self, p: Punct) -> bool {
        if self.at(p) {
            self.bump();
            true
        } else {
            false
        }
    }

    pub(super) fn at_kw(&self, k: Keyword) -> bool {
        self.kind().is_keyword(k)
    }

    pub(super) fn nth_is_kw(&self, n: usize, k: Keyword) -> bool {
        self.nth(n).is_keyword(k)
    }

    pub(super) fn eat_kw(&mut self, k: Keyword) -> bool {
        if self.at_kw(k) {
            self.bump();
            true
        } else {
            false
        }
    }

    pub(super) fn at_eof(&self) -> bool {
        matches!(self.kind(), TokenKind::Eof)
    }

    pub(super) fn at_newline(&self) -> bool {
        matches!(self.kind(), TokenKind::Newline)
    }

    /// True for the current token being the contextual keyword `sym`.
    pub(super) fn at_word(&self, sym: Symbol) -> bool {
        self.nth_is_word(0, sym)
    }

    pub(super) fn nth_is_word(&self, n: usize, sym: Symbol) -> bool {
        matches!(self.nth(n), TokenKind::Ident(s) if *s == sym)
    }

    /// An identifier or `_` at offset `n`.
    pub(super) fn nth_is_name(&self, n: usize) -> bool {
        match self.nth(n) {
            TokenKind::Ident(_) | TokenKind::Underscore => true,
            // A keyword where a binding or parameter name belongs (`esc := 1`,
            // `fn(match: i32)`) is read as a name, and reported when consumed.
            TokenKind::Keyword(_) => {
                matches!(self.nth(n + 1), TokenKind::Punct(Punct::Colon | Punct::ColonEq | Punct::ColonColon))
            }
            _ => false,
        }
    }

    /// Consumes the keyword at the current token where a name was expected,
    /// reports it once, and returns it as the name so that parsing goes on.
    pub(super) fn keyword_as_name(&mut self, k: Keyword) -> Ident {
        let span = self.span();
        self.bump();
        self.err_count += 1;
        if !self.kw_names.contains(&k) {
            self.kw_names.push(k);
        }
        let text = k.as_str();
        self.diags.push(
            Diagnostic::error(span, format!("`{text}` is a keyword and cannot be used as a name"))
                .with_note(format!("rename it, for example to `{text}_`")),
        );
        Ident { name: Symbol::intern(text), span }
    }

    /// True if the current token is a keyword already reported as a misused name.
    pub(super) fn at_reported_kw_name(&self) -> bool {
        matches!(self.kind(), TokenKind::Keyword(k) if self.kw_names.contains(k))
    }

    /// Consumes the current token as a name already reported by [`Self::keyword_as_name`].
    pub(super) fn bump_reported_kw_name(&mut self) -> Ident {
        let span = self.span();
        let TokenKind::Keyword(k) = self.kind() else { unreachable!() };
        self.bump();
        Ident { name: Symbol::intern(k.as_str()), span }
    }

    pub(super) fn skip_newlines(&mut self) {
        while self.at_newline() {
            self.bump();
        }
    }

    /// Consumes an identifier or `_` as an [`Ident`].
    pub(super) fn parse_name(&mut self, what: &str) -> PResult<Ident> {
        let span = self.span();
        match self.kind() {
            TokenKind::Ident(s) => {
                self.bump();
                Ok(Ident { name: *s, span })
            }
            TokenKind::Underscore => {
                self.bump();
                Ok(Ident { name: Symbol::intern("_"), span })
            }
            TokenKind::Keyword(k) => Ok(self.keyword_as_name(*k)),
            _ => self.expected(what),
        }
    }

    /// Consumes an identifier (not `_`) in a position that declares a name:
    /// a keyword there is reported and taken as the name.
    pub(super) fn parse_decl_ident(&mut self, what: &str) -> PResult<Ident> {
        if let TokenKind::Keyword(k) = self.kind() {
            return Ok(self.keyword_as_name(*k));
        }
        self.parse_ident(what)
    }

    /// Consumes an identifier (not `_`).
    pub(super) fn parse_ident(&mut self, what: &str) -> PResult<Ident> {
        let span = self.span();
        match self.kind() {
            TokenKind::Ident(s) => {
                self.bump();
                Ok(Ident { name: *s, span })
            }
            _ => self.expected(what),
        }
    }

    // ------------------------------------------------------------------
    // Errors and recovery
    // ------------------------------------------------------------------

    /// Reports an error without aborting.
    pub(super) fn report(&mut self, span: Span, msg: impl Into<String>) {
        self.err_count += 1;
        self.diags.error(span, msg);
    }

    /// Reports an error and returns `Err(Abort)`. A second error at the same
    /// token as the previous one is dropped (it is almost always a cascade).
    pub(super) fn fail<T>(&mut self, span: Span, msg: impl Into<String>) -> PResult<T> {
        if self.last_err_pos != Some(self.pos) {
            self.last_err_pos = Some(self.pos);
            self.report(span, msg);
        } else {
            self.err_count += 1;
        }
        Err(Abort)
    }

    /// "expected `what`, found `token`" at the current token.
    pub(super) fn expected<T>(&mut self, what: &str) -> PResult<T> {
        let span = self.span();
        let found = self.kind().describe();
        self.fail(span, format!("expected {}, found {}", what, found))
    }

    /// Consumes the punctuation `p` or fails with "expected `p` {ctx}".
    pub(super) fn expect(&mut self, p: Punct, ctx: &str) -> PResult<Span> {
        if self.at(p) {
            Ok(self.bump().span)
        } else {
            let what = if ctx.is_empty() { format!("`{}`", p.as_str()) } else { format!("`{}` {}", p.as_str(), ctx) };
            self.expected(&what)
        }
    }

    pub(super) fn expect_kw(&mut self, k: Keyword, ctx: &str) -> PResult<Span> {
        if self.at_kw(k) {
            Ok(self.bump().span)
        } else {
            let what = if ctx.is_empty() { format!("`{}`", k.as_str()) } else { format!("`{}` {}", k.as_str(), ctx) };
            self.expected(&what)
        }
    }

    /// Skips to the end of the current statement or declaration: stops before a
    /// newline or an unmatched `}` (neither is consumed), or at the end of file.
    pub(super) fn recover_to_stmt_end(&mut self) {
        let mut depth = 0usize;
        loop {
            match self.kind() {
                TokenKind::Eof => break,
                TokenKind::Newline if depth == 0 => break,
                TokenKind::Punct(Punct::LBrace) => depth += 1,
                TokenKind::Punct(Punct::RBrace) => {
                    if depth == 0 {
                        break;
                    }
                    depth -= 1;
                }
                _ => {}
            }
            self.bump();
        }
    }

    /// Checks that a statement-like construct ends at a newline, `}` or the end
    /// of file; otherwise reports an error and skips the rest of the line.
    pub(super) fn expect_terminator(&mut self, what: &str) {
        match self.kind() {
            TokenKind::Newline | TokenKind::Eof | TokenKind::Punct(Punct::RBrace) => {}
            _ => {
                let _: PResult<()> = self.expected(&format!("a newline or `;` after {}", what));
                self.recover_to_stmt_end();
            }
        }
    }

    // ------------------------------------------------------------------
    // Speculation, nesting, restrictions
    // ------------------------------------------------------------------

    fn snapshot(&self) -> Snapshot {
        Snapshot {
            pos: self.pos,
            last_hi: self.last_hi,
            last_err_pos: self.last_err_pos,
            err_count: self.err_count,
            depth: self.depth,
            labels: self.labels.len(),
            restr: self.restr,
        }
    }

    fn restore(&mut self, s: Snapshot) {
        self.pos = s.pos;
        self.last_hi = s.last_hi;
        self.last_err_pos = s.last_err_pos;
        self.err_count = s.err_count;
        self.depth = s.depth;
        self.labels.truncate(s.labels);
        self.restr = s.restr;
    }

    /// Runs `f` with diagnostics suppressed. If it succeeds without raising any
    /// error, returns its value; otherwise rolls back and returns `None`.
    pub(super) fn speculate<T>(&mut self, f: impl FnOnce(&mut Self) -> PResult<T>) -> Option<T> {
        if self.spec_waste > self.spec_limit {
            return None;
        }
        let snap = self.snapshot();
        self.diags.suppress();
        let errors_before = self.err_count;
        let r = f(self);
        self.diags.unsuppress();
        match r {
            Ok(v) if self.err_count == errors_before => Some(v),
            _ => {
                self.spec_waste += self.pos.saturating_sub(snap.pos) + 1;
                self.restore(snap);
                None
            }
        }
    }

    /// Runs `f` one nesting level deeper, failing if the input nests too deeply.
    pub(super) fn nest<T>(&mut self, f: impl FnOnce(&mut Self) -> PResult<T>) -> PResult<T> {
        if self.depth >= MAX_DEPTH {
            let span = self.span();
            return self.fail(span, "this is nested too deeply");
        }
        self.depth += 1;
        let r = f(self);
        self.depth -= 1;
        r
    }

    /// Runs `f` with the restrictions `r`, restoring the old ones afterwards.
    pub(super) fn with_restr<T>(&mut self, r: Restr, f: impl FnOnce(&mut Self) -> T) -> T {
        let old = std::mem::replace(&mut self.restr, r);
        let v = f(self);
        self.restr = old;
        v
    }

    /// Runs `f` with all restrictions cleared (inside brackets and blocks).
    pub(super) fn unrestricted<T>(&mut self, f: impl FnOnce(&mut Self) -> T) -> T {
        self.with_restr(Restr::default(), f)
    }

    // ------------------------------------------------------------------
    // Node construction
    // ------------------------------------------------------------------

    pub(super) fn mk_expr(&mut self, kind: ExprKind, span: Span) -> Expr {
        Expr { id: self.ids.next(), kind, span }
    }

    pub(super) fn error_expr(&mut self, span: Span) -> Expr {
        self.mk_expr(ExprKind::Error, span)
    }

    // ------------------------------------------------------------------
    // The file
    // ------------------------------------------------------------------

    fn parse_file_items(&mut self) -> Vec<Item> {
        let mut items = Vec::new();
        loop {
            self.skip_newlines();
            if self.at_eof() {
                break;
            }
            let before = self.pos;
            if self.at(Punct::RBrace) {
                let span = self.span();
                self.report(span, "unmatched `}`");
                self.bump();
                continue;
            }
            match self.parse_item() {
                Ok(item) => {
                    items.push(item);
                    self.expect_terminator("the declaration");
                }
                Err(Abort) => self.recover_to_stmt_end(),
            }
            if self.pos == before {
                self.bump();
            }
        }
        items
    }
}
