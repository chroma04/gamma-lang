//! Top-level declarations (Appendix A.1, spec §3.4).

use super::{Abort, PResult, Parser, Restr};
use crate::syntax::ast::*;
use crate::syntax::token::{DocComment, Keyword, Punct, TokenKind};

impl Parser<'_> {
    /// One top-level declaration: `[attrs] [pub] binding`, `when`, `handler … for …`,
    /// `test "name" { … }`, `bench "name" { … }` or `@static_assert(…)`.
    pub(super) fn parse_item(&mut self) -> PResult<Item> {
        let start = self.lo();
        let doc = self.doc_before(start);
        let attrs = self.parse_attrs()?;
        let pub_span = self.span();
        let vis = self.parse_visibility()?;
        let is_pub = vis.is_exported();
        let kind = match self.kind() {
            TokenKind::Keyword(Keyword::When) => {
                self.deny_pub(is_pub, pub_span, "a `when` block");
                ItemKind::When(self.parse_when_items()?)
            }
            TokenKind::Keyword(Keyword::Handler) => ItemKind::Handler(self.parse_handler_decl()?),
            TokenKind::Ident(name)
                if (*name == self.words.test || *name == self.words.bench)
                    && matches!(self.nth(1), TokenKind::Str(_)) =>
            {
                let bench = *name == self.words.bench;
                self.deny_pub(is_pub, pub_span, if bench { "a benchmark" } else { "a test" });
                self.parse_test(bench)?
            }
            TokenKind::Intrinsic(_) => {
                self.deny_pub(is_pub, pub_span, "an intrinsic call");
                ItemKind::Intrinsic(self.parse_expr()?)
            }
            TokenKind::Ident(_) | TokenKind::Underscore | TokenKind::Keyword(_) if self.binding_ahead() => {
                ItemKind::Binding(self.parse_binding()?)
            }
            _ => return self.expected("a declaration"),
        };
        let id = self.ids.next();
        Ok(Item { id, span: self.span_from(start), doc, attrs, vis, kind })
    }

    /// `pub`, `pub(package)` or nothing (§3.3).
    pub(super) fn parse_visibility(&mut self) -> PResult<Visibility> {
        if !self.eat_kw(Keyword::Pub) {
            return Ok(Visibility::Private);
        }
        if !self.eat(Punct::LParen) {
            return Ok(Visibility::Public);
        }
        match self.kind() {
            TokenKind::Ident(name) if name.as_str() == "package" => {
                self.bump();
            }
            _ => return self.expected("`package` in `pub(…)`"),
        }
        self.expect(Punct::RParen, "to close `pub(package`")?;
        Ok(Visibility::Package)
    }

    fn deny_pub(&mut self, is_pub: bool, span: crate::source::Span, what: &str) {
        if is_pub {
            self.report(span, format!("`pub` cannot be applied to {}", what));
        }
    }

    /// `test "name" { … }` or `bench "name" { … }`.
    fn parse_test(&mut self, bench: bool) -> PResult<ItemKind> {
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
        Ok(ItemKind::Test { name, name_span, body: body?, bench })
    }

    /// `handler E for T [needs effects] { clauses }`.
    fn parse_handler_decl(&mut self) -> PResult<HandlerDecl> {
        let start = self.lo();
        self.bump();
        let params = if self.at(Punct::LParen) { Some(self.parse_decl_params()?) } else { None };
        let effect = self.parse_effect_ref()?;
        self.expect_kw(Keyword::For, "after the effect of a handler declaration")?;
        let for_ty = self.with_restr(Restr { no_brace: true, ..Restr::default() }, |p| p.parse_type())?;
        let effects = self.parse_needs_opt(false)?;
        self.expect(Punct::LBrace, "to begin the handler body")?;
        let saved = std::mem::take(&mut self.labels);
        let clauses = self.unrestricted(|p| p.parse_clauses());
        self.labels = saved;
        Ok(HandlerDecl { params, effect, for_ty, effects, clauses, span: self.span_from(start) })
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
                    vis: Visibility::Private,
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
    /// A comment is adjacent to what follows when that begins on the next line,
    /// after indentation only (the lexer records where, in `next_lo`), and the
    /// comment starts its own line.
    fn doc_before(&self, item_lo: u32) -> Option<String> {
        let mut idx = self.docs.partition_point(|d| d.span.hi <= item_lo);
        let mut next_lo = item_lo;
        let mut lines: Vec<&str> = Vec::new();
        while idx > 0 {
            let d: &DocComment = &self.docs[idx - 1];
            if d.next_lo != next_lo || !self.starts_line(d) {
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
