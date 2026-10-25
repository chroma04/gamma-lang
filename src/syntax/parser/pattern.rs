//! Patterns (Appendix A.6, spec §9.1).

use super::{PResult, Parser};
use crate::source::Span;
use crate::syntax::ast::*;
use crate::syntax::token::{Keyword, Punct, TokenKind};

impl Parser<'_> {
    fn mk_pat(&mut self, kind: PatternKind, span: Span) -> Pattern {
        Pattern { id: self.ids.next(), kind, span }
    }

    pub(super) fn parse_pattern(&mut self) -> PResult<Pattern> {
        self.nest(|p| p.parse_pattern_inner())
    }

    fn parse_pattern_inner(&mut self) -> PResult<Pattern> {
        let start = self.lo();
        match self.kind() {
            TokenKind::Underscore => {
                self.bump();
                let span = self.span_from(start);
                Ok(self.mk_pat(PatternKind::Wildcard, span))
            }
            TokenKind::Keyword(Keyword::Null) => {
                self.bump();
                let span = self.span_from(start);
                Ok(self.mk_pat(PatternKind::Null, span))
            }
            TokenKind::Punct(Punct::Bang) => {
                self.bump();
                let inner = self.parse_pattern()?;
                let span = self.span_from(start);
                Ok(self.mk_pat(PatternKind::Err(Box::new(inner)), span))
            }
            TokenKind::Punct(Punct::Dot) => {
                self.bump();
                let name = self.parse_ident("a variant name after `.`")?;
                let path = self.mk_expr(ExprKind::DotIdent(name), self.span_from(start));
                let args = if self.at(Punct::LParen) { Some(self.parse_pat_args()?) } else { None };
                let span = self.span_from(start);
                Ok(self.mk_pat(PatternKind::Path { path: Box::new(path), args }, span))
            }
            TokenKind::Punct(Punct::Question) => {
                self.bump();
                let name = self.parse_ident("a name after `?`")?;
                let span = self.span_from(start);
                Ok(self.mk_pat(PatternKind::Whole(name), span))
            }
            TokenKind::Punct(Punct::LParen) => self.parse_tuple_pattern(),
            TokenKind::Punct(Punct::Minus)
            | TokenKind::Int(_)
            | TokenKind::Float(_)
            | TokenKind::Str(_)
            | TokenKind::Keyword(Keyword::True | Keyword::False) => self.parse_literal_pattern(),
            TokenKind::Ident(_) => self.parse_path_pattern(),
            _ => self.expected("a pattern"),
        }
    }

    /// A literal (optionally negated), possibly the lower bound of a range.
    fn parse_literal_pattern(&mut self) -> PResult<Pattern> {
        let start = self.lo();
        let lo = self.parse_literal_bound()?;
        if matches!(self.kind(), TokenKind::Punct(Punct::DotDot | Punct::DotDotEq)) {
            return self.finish_range_pattern(start, lo);
        }
        let span = self.span_from(start);
        Ok(self.mk_pat(PatternKind::Lit(Box::new(lo)), span))
    }

    /// `[-] literal`, as the expression stored in the pattern.
    fn parse_literal_bound(&mut self) -> PResult<Expr> {
        let start = self.lo();
        let negated = self.eat(Punct::Minus);
        let tok = self.tok();
        let span = tok.span;
        let kind = match &tok.kind {
            TokenKind::Int(l) => ExprKind::Int(l.clone()),
            TokenKind::Float(l) => ExprKind::Float(l.clone()),
            TokenKind::Str(l) if !negated => ExprKind::Str(l.clone()),
            TokenKind::Keyword(Keyword::True) if !negated => ExprKind::Bool(true),
            TokenKind::Keyword(Keyword::False) if !negated => ExprKind::Bool(false),
            _ => return self.expected(if negated { "a numeric literal after `-`" } else { "a literal" }),
        };
        self.bump();
        let lit = self.mk_expr(kind, span);
        if negated {
            let whole = self.span_from(start);
            return Ok(self.mk_expr(ExprKind::Unary { op: UnOp::Neg, operand: Box::new(lit) }, whole));
        }
        Ok(lit)
    }

    /// The upper bound after `..` / `..=`: a literal or a qualified constant.
    fn finish_range_pattern(&mut self, start: u32, lo: Expr) -> PResult<Pattern> {
        let inclusive = self.at(Punct::DotDotEq);
        self.bump();
        let hi = if matches!(self.kind(), TokenKind::Ident(_)) {
            let path = self.parse_qualified_path()?;
            if !matches!(path.kind, ExprKind::Field { .. }) {
                return self.fail(path.span, "a range bound must be a literal or a qualified constant such as `m.MAX`");
            }
            path
        } else {
            self.parse_literal_bound()?
        };
        let span = self.span_from(start);
        Ok(self.mk_pat(PatternKind::Range { lo: Box::new(lo), hi: Box::new(hi), inclusive }, span))
    }

    /// `name` or `a.b.c` as an expression.
    fn parse_qualified_path(&mut self) -> PResult<Expr> {
        let id = self.parse_ident("a name")?;
        let mut path = self.mk_expr(ExprKind::Ident(id), id.span);
        while self.at(Punct::Dot) && matches!(self.nth(1), TokenKind::Ident(_)) {
            self.bump();
            let name = self.parse_ident("a name")?;
            let span = self.span_from(id.span.lo);
            path = self.mk_expr(ExprKind::Field { base: Box::new(path), name }, span);
        }
        Ok(path)
    }

    /// `x`, `m.CONST`, `T.V`, `T.V(p…)`, `S(f: p, …)`, or a range with a
    /// qualified lower bound.
    fn parse_path_pattern(&mut self) -> PResult<Pattern> {
        let start = self.lo();
        let mut path = self.parse_qualified_path()?;
        // `G(args)(f: p, …)`: the first parentheses instantiate a generic struct type.
        if self.at(Punct::LParen) && self.after_parens_is(Punct::LParen) {
            let (args, trailing_comma) = self.parse_call_args()?;
            let span = self.span_from(start);
            path = self.mk_expr(ExprKind::Call { callee: Box::new(path), args, trailing_comma }, span);
        }
        if self.at(Punct::LParen) {
            let args = self.parse_pat_args()?;
            let span = self.span_from(start);
            return Ok(self.mk_pat(PatternKind::Path { path: Box::new(path), args: Some(args) }, span));
        }
        match path.kind {
            ExprKind::Ident(id) => {
                let span = self.span_from(start);
                Ok(self.mk_pat(PatternKind::Bind(id), span))
            }
            _ => {
                if matches!(self.kind(), TokenKind::Punct(Punct::DotDot | Punct::DotDotEq)) {
                    return self.finish_range_pattern(start, path);
                }
                let span = self.span_from(start);
                Ok(self.mk_pat(PatternKind::Path { path: Box::new(path), args: None }, span))
            }
        }
    }

    /// Whether the token after the parenthesized group starting here is `p`.
    fn after_parens_is(&self, p: Punct) -> bool {
        let mut depth = 0usize;
        let mut i = 0;
        loop {
            match self.nth(i) {
                TokenKind::Punct(Punct::LParen | Punct::LBracket | Punct::LBrace) => depth += 1,
                TokenKind::Punct(Punct::RParen | Punct::RBracket | Punct::RBrace) => {
                    depth = depth.saturating_sub(1);
                    if depth == 0 {
                        return self.nth(i + 1).is_punct(p);
                    }
                }
                TokenKind::Eof => return false,
                _ => {}
            }
            i += 1;
        }
    }

    /// `( p, name: p, … )`
    fn parse_pat_args(&mut self) -> PResult<Vec<PatArg>> {
        self.expect(Punct::LParen, "")?;
        self.unrestricted(|p| {
            let mut args = Vec::new();
            while !p.at(Punct::RParen) {
                args.push(p.parse_pat_arg()?);
                if !p.eat(Punct::Comma) {
                    break;
                }
            }
            p.expect(Punct::RParen, "to close the pattern arguments")?;
            Ok(args)
        })
    }

    fn parse_pat_arg(&mut self) -> PResult<PatArg> {
        let start = self.lo();
        let name = if self.nth_is_name(0) && self.nth_is(1, Punct::Colon) {
            let n = self.parse_name("a field name")?;
            self.bump();
            Some(n)
        } else {
            None
        };
        let pat = self.parse_pattern()?;
        Ok(PatArg { name, pat, span: self.span_from(start) })
    }

    /// `()`, `(p)`, `(p,)`, `(p, q)`, `(a: p, b: q)`.
    fn parse_tuple_pattern(&mut self) -> PResult<Pattern> {
        let start = self.lo();
        let mut args = self.parse_pat_args()?;
        let had_comma = self.toks[..self.pos].iter().rev().skip(1).take(1).any(|t| t.kind.is_punct(Punct::Comma));
        let span = self.span_from(start);
        if args.len() == 1
            && args[0].name.is_none()
            && !had_comma
            && let Some(a) = args.pop()
        {
            return Ok(a.pat);
        }
        Ok(self.mk_pat(PatternKind::Tuple(args), span))
    }
}
