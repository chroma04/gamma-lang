//! Top-level declarations (Appendix A.1, spec §3.4).

use super::{Abort, PResult, Parser, Restr};
use crate::syntax::ast::*;
use crate::syntax::token::{DocComment, Keyword, Punct, TokenKind};

impl Parser<'_> {
    /// One top-level declaration: `[attrs] [pub] binding`, `when`, `handler … for …`,
    /// `test "name" { … }` or `@static_assert(…)`.
    pub(super) fn parse_item(&mut self) -> PResult<Item> {
        let start = self.lo();
        let doc = self.doc_before(start);
        let attrs = self.parse_attrs()?;
        let pub_span = self.span();
        let is_pub = self.eat_kw(Keyword::Pub);
        let kind = match self.kind() {
            TokenKind::Keyword(Keyword::When) => {
                self.deny_pub(is_pub, pub_span, "a `when` block");
                ItemKind::When(self.parse_when_items()?)
            }
            TokenKind::Keyword(Keyword::Handler) => ItemKind::Handler(self.parse_handler_decl()?),
            TokenKind::Ident(name) if *name == self.words.test && matches!(self.nth(1), TokenKind::Str(_)) => {
                self.deny_pub(is_pub, pub_span, "a test");
                self.parse_test()?
            }
            TokenKind::Intrinsic(_) => {
                self.deny_pub(is_pub, pub_span, "an intrinsic call");
                ItemKind::Intrinsic(self.parse_expr()?)
            }
            TokenKind::Ident(_) | TokenKind::Underscore if self.binding_ahead() => {
                ItemKind::Binding(self.parse_binding()?)
            }
            _ => return self.expected("a declaration"),
        };
        let id = self.ids.next();
        Ok(Item { id, span: self.span_from(start), doc, attrs, is_pub, kind })
    }

    fn deny_pub(&mut self, is_pub: bool, span: crate::source::Span, what: &str) {
        if is_pub {
            self.report(span, format!("`pub` cannot be applied to {}", what));
        }
    }

    /// `test "name" { … }`.
    fn parse_test(&mut self) -> PResult<ItemKind> {
        self.bump();
        let name_span = self.span();
        let name = match self.kind() {
            TokenKind::Str(lit) => (**lit).clone(),
            _ => return self.expected("the name of the test as a string literal"),
        };
        self.bump();
        let saved = std::mem::take(&mut self.labels);
        let body = self.parse_block();
        self.labels = saved;
        Ok(ItemKind::Test { name, name_span, body: body? })
    }

    /// `handler E for T [\ {effects}] { clauses }`.
    fn parse_handler_decl(&mut self) -> PResult<HandlerDecl> {
        let start = self.lo();
        self.bump();
        let effect = self.parse_effect_ref()?;
        self.expect_kw(Keyword::For, "after the effect of a handler declaration")?;
        let for_ty = self.with_restr(Restr { no_brace: true, ..Restr::default() }, |p| p.parse_type())?;
        let effects = if self.at(Punct::Backslash) { Some(self.parse_effect_set()?) } else { None };
        self.expect(Punct::LBrace, "to begin the handler body")?;
        let saved = std::mem::take(&mut self.labels);
        let clauses = self.unrestricted(|p| p.parse_clauses());
        self.labels = saved;
        Ok(HandlerDecl { effect, for_ty, effects, clauses, span: self.span_from(start) })
    }

    /// `when cond { items } [else when … | else { items }]`.
    fn parse_when_items(&mut self) -> PResult<WhenItems> {
        let start = self.lo();
        self.bump();
        let cond = self.parse_head_expr()?;
        let then = self.parse_top_block()?;
        let mut else_ = None;
        if self.eat_kw(Keyword::Else) {
            if self.at_kw(Keyword::When) {
                let nested_start = self.lo();
                let nested = self.parse_when_items()?;
                let id = self.ids.next();
                let item = Item {
                    id,
                    span: self.span_from(nested_start),
                    doc: None,
                    attrs: Vec::new(),
                    is_pub: false,
                    kind: ItemKind::When(nested),
                };
                else_ = Some(vec![item]);
            } else {
                else_ = Some(self.parse_top_block()?);
            }
        }
        Ok(WhenItems { cond, then, else_, span: self.span_from(start) })
    }

    /// `{ items }` of a top-level `when`. Errors are recovered item by item.
    fn parse_top_block(&mut self) -> PResult<Vec<Item>> {
        self.expect(Punct::LBrace, "to begin the block")?;
        let items = self.unrestricted(|p| p.nest(|p| Ok(p.parse_top_block_items())))?;
        Ok(items)
    }

    fn parse_top_block_items(&mut self) -> Vec<Item> {
        let mut items = Vec::new();
        loop {
            self.skip_newlines();
            if self.at(Punct::RBrace) {
                self.bump();
                break;
            }
            if self.at_eof() {
                let _: PResult<()> = self.expected("`}` to close the block");
                break;
            }
            let before = self.pos;
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

    // ------------------------------------------------------------------
    // Doc comments
    // ------------------------------------------------------------------

    /// The doc comments that directly precede the declaration starting at byte
    /// `item_lo`, joined by newlines.
    ///
    /// The parser sees only token spans, not the source text, so "directly
    /// precedes" is judged from byte distances: the gap between a comment and
    /// what follows must be one line terminator plus indentation (a multiple of
    /// four bytes), and the comment must start its own line.
    fn doc_before(&self, item_lo: u32) -> Option<String> {
        let mut idx = self.docs.partition_point(|d| d.span.hi <= item_lo);
        let terminator = if self.crlf { 2 } else { 1 };
        let mut next_lo = item_lo;
        let mut lines: Vec<&str> = Vec::new();
        while idx > 0 {
            let d: &DocComment = &self.docs[idx - 1];
            let gap = next_lo - d.span.hi;
            if gap < terminator
                || !(gap - terminator).is_multiple_of(4)
                || gap - terminator > 32
                || !self.starts_line(d)
                || self.token_between(d.span.hi, next_lo)
            {
                break;
            }
            lines.push(&d.text);
            next_lo = d.span.lo;
            idx -= 1;
        }
        if lines.is_empty() {
            return None;
        }
        lines.reverse();
        Some(lines.join("\n"))
    }

    /// True if some token starts in `lo..hi`.
    fn token_between(&self, lo: u32, hi: u32) -> bool {
        let i = self.toks.partition_point(|t| t.span.lo < lo);
        self.toks.get(i).is_some_and(|t| t.span.lo < hi)
    }

    /// True if no code precedes the comment on its line.
    fn starts_line(&self, d: &DocComment) -> bool {
        let before = self.toks.partition_point(|t| t.span.lo < d.span.lo);
        match before.checked_sub(1).map(|i| &self.toks[i].kind) {
            None => true,
            Some(TokenKind::Newline | TokenKind::Punct(Punct::LBrace)) => true,
            Some(_) => false,
        }
    }
}
