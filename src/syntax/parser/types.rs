//! Type syntax (Appendix A.2), function signatures and literals (A.3),
//! parameters, effect sets, `where` clauses and attributes.
//!
//! Types are expressions. In *type positions* (after `:` in a binding, parameter
//! or field, after `->`, in effect sets, …) [`Parser::parse_type`] is used: `!`
//! is the infix error-union operator there. In expression positions the same
//! syntax is reached through [`Parser::parse_primary`] with [`Mode::Expr`].

use super::{PResult, Parser};
use crate::syntax::ast::*;
use crate::syntax::token::{Keyword, Punct, TokenKind};

/// Whether the operand of a type prefix is parsed as a type or as an expression.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(super) enum Mode {
    /// Expression position: the operand is a postfix expression (A.7 rule 12).
    Expr,
    /// Type position.
    Type,
}

impl Parser<'_> {
    /// Parses a type (A.2 `type`): a prefix type optionally followed by `!` and
    /// the success type.
    pub(super) fn parse_type(&mut self) -> PResult<Expr> {
        self.nest(|p| {
            let lhs = p.parse_prefix_type()?;
            if p.at(Punct::Bang) {
                p.bump();
                let ok = p.parse_type()?;
                let span = lhs.span.to(ok.span);
                return Ok(p.mk_expr(ExprKind::ErrorUnionType { err: Box::new(lhs), ok: Box::new(ok) }, span));
            }
            Ok(lhs)
        })
    }

    /// A.2 `prefix_type`.
    pub(super) fn parse_prefix_type(&mut self) -> PResult<Expr> {
        match self.kind() {
            TokenKind::Punct(Punct::Question | Punct::Caret | Punct::LBracket) => self.parse_type_prefix(Mode::Type),
            TokenKind::Keyword(Keyword::Fn) => self.parse_fn_type_only(),
            TokenKind::Punct(Punct::LParen) => self.parse_paren(Mode::Type),
            TokenKind::Keyword(Keyword::Struct) => self.parse_struct_lit(),
            TokenKind::Keyword(Keyword::Union) => self.parse_union_lit(),
            TokenKind::Keyword(Keyword::Effect) => self.parse_effect_lit(),
            TokenKind::Attribute(_) => self.parse_attributed(Mode::Type),
            TokenKind::Intrinsic(_) => self.parse_intrinsic(),
            TokenKind::Ident(name) => {
                if *name == self.words.cont && self.nth_is(1, Punct::LParen) {
                    self.parse_cont_type()
                } else {
                    self.parse_type_name()
                }
            }
            _ => self.expected("a type"),
        }
    }

    /// `name`, `m.T`, `G(args)` and combinations.
    fn parse_type_name(&mut self) -> PResult<Expr> {
        let id = self.parse_ident("a type")?;
        let mut e = self.mk_expr(ExprKind::Ident(id), id.span);
        loop {
            let start = e.span.lo;
            if self.at(Punct::Dot) && matches!(self.nth(1), TokenKind::Ident(_)) {
                self.bump();
                let name = self.parse_ident("a name")?;
                let span = self.span_from(start);
                e = self.mk_expr(ExprKind::Field { base: Box::new(e), name }, span);
            } else if self.at(Punct::LParen) {
                let (args, trailing_comma) = self.parse_call_args()?;
                let span = self.span_from(start);
                e = self.mk_expr(ExprKind::Call { callee: Box::new(e), args, trailing_comma }, span);
            } else {
                break;
            }
        }
        Ok(e)
    }

    /// Consumes the contextual keyword `const` after `^`, `[]` or `[^]`.
    fn eat_const(&mut self) -> bool {
        if self.at_word(self.words.const_) {
            self.bump();
            true
        } else {
            false
        }
    }

    /// The operand of a type prefix operator.
    fn parse_type_operand(&mut self, mode: Mode) -> PResult<Expr> {
        self.nest(|p| match mode {
            Mode::Type => p.parse_prefix_type(),
            Mode::Expr => p.parse_postfix(false),
        })
    }

    /// `?T`, `^T`, `^const T`, `[]T`, `[^]T`, `[N]T`, `[_]T`.
    pub(super) fn parse_type_prefix(&mut self, mode: Mode) -> PResult<Expr> {
        let start = self.lo();
        match self.kind() {
            TokenKind::Punct(Punct::Question) => {
                self.bump();
                let inner = self.parse_type_operand(mode)?;
                let span = self.span_from(start);
                Ok(self.mk_expr(ExprKind::OptionalType(Box::new(inner)), span))
            }
            TokenKind::Punct(Punct::Caret) => {
                self.bump();
                let is_const = self.eat_const();
                let inner = self.parse_type_operand(mode)?;
                let span = self.span_from(start);
                Ok(self.mk_expr(ExprKind::PtrType { is_const, inner: Box::new(inner) }, span))
            }
            TokenKind::Punct(Punct::LBracket) => {
                self.bump();
                if self.at(Punct::RBracket) {
                    self.bump();
                    let is_const = self.eat_const();
                    let inner = self.parse_type_operand(mode)?;
                    let span = self.span_from(start);
                    Ok(self.mk_expr(ExprKind::SliceType { is_const, inner: Box::new(inner) }, span))
                } else if self.at(Punct::Caret) && self.nth_is(1, Punct::RBracket) {
                    self.bump();
                    self.bump();
                    let is_const = self.eat_const();
                    let inner = self.parse_type_operand(mode)?;
                    let span = self.span_from(start);
                    Ok(self.mk_expr(ExprKind::ManyPtrType { is_const, inner: Box::new(inner) }, span))
                } else if matches!(self.kind(), TokenKind::Underscore) && self.nth_is(1, Punct::RBracket) {
                    self.bump();
                    self.bump();
                    let elem = self.parse_type_operand(mode)?;
                    let span = self.span_from(start);
                    Ok(self.mk_expr(ExprKind::ArrayType { len: None, elem: Box::new(elem) }, span))
                } else {
                    let len = self.unrestricted(|p| p.parse_expr())?;
                    self.expect(Punct::RBracket, "to close the array length")?;
                    let elem = self.parse_type_operand(mode)?;
                    let span = self.span_from(start);
                    Ok(self.mk_expr(ExprKind::ArrayType { len: Some(Box::new(len)), elem: Box::new(elem) }, span))
                }
            }
            _ => self.expected("a type"),
        }
    }

    /// `cont(T)`.
    pub(super) fn parse_cont_type(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        self.bump();
        let inner = self.unrestricted(|p| p.parse_type())?;
        self.expect(Punct::RParen, "to close `cont(`")?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::ContType(Box::new(inner)), span))
    }

    /// Attributes followed by a type or literal.
    pub(super) fn parse_attributed(&mut self, mode: Mode) -> PResult<Expr> {
        let start = self.lo();
        let attrs = self.parse_attrs()?;
        let inner = self.nest(|p| match mode {
            Mode::Type => p.parse_prefix_type(),
            Mode::Expr => p.parse_postfix(false),
        })?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Attributed { attrs, inner: Box::new(inner) }, span))
    }

    // ------------------------------------------------------------------
    // Attributes
    // ------------------------------------------------------------------

    /// `{ '#' name [ '(' args ')' ] }`; newlines after an attribute are skipped
    /// (A.7 rule 13).
    pub(super) fn parse_attrs(&mut self) -> PResult<Vec<Attribute>> {
        let mut attrs = Vec::new();
        while let TokenKind::Attribute(sym) = self.kind() {
            let tok = self.bump();
            let name = Ident { name: *sym, span: tok.span };
            let args = if self.at(Punct::LParen) { Some(self.parse_attr_args()?) } else { None };
            let span = self.span_from(tok.span.lo);
            attrs.push(Attribute { name, args, span });
            self.skip_newlines();
        }
        Ok(attrs)
    }

    fn parse_attr_args(&mut self) -> PResult<Vec<Expr>> {
        self.expect(Punct::LParen, "")?;
        self.unrestricted(|p| {
            let mut args = Vec::new();
            while !p.at(Punct::RParen) {
                args.push(p.parse_expr()?);
                if !p.eat(Punct::Comma) {
                    break;
                }
            }
            p.expect(Punct::RParen, "to close the attribute arguments")?;
            Ok(args)
        })
    }

    // ------------------------------------------------------------------
    // Functions
    // ------------------------------------------------------------------

    /// `fn(params) [-> T] [\ {E}]`.
    fn parse_fn_sig(&mut self) -> PResult<FnSig> {
        let start = self.lo();
        self.expect_kw(Keyword::Fn, "")?;
        self.expect(Punct::LParen, "to begin the parameter list")?;
        let (params, variadic) = self.parse_params()?;
        let ret = if self.eat(Punct::Arrow) { Some(self.parse_type()?) } else { None };
        let effects = if self.at(Punct::Backslash) { Some(self.parse_effect_set()?) } else { None };
        Ok(FnSig { params, variadic, ret, effects, span: self.span_from(start) })
    }

    /// A function signature that is a type: `fn(A, B) -> R \ {E}`.
    pub(super) fn parse_fn_type_sig(&mut self) -> PResult<FnSig> {
        let mut sig = self.parse_fn_sig()?;
        self.fn_sig_to_type(&mut sig);
        Ok(sig)
    }

    /// A function type in a type position (never a literal).
    pub(super) fn parse_fn_type_only(&mut self) -> PResult<Expr> {
        let start = self.lo();
        let sig = self.parse_fn_type_sig()?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::FnType(Box::new(sig)), span))
    }

    /// A function type or literal in an expression position (A.7 rule 5).
    pub(super) fn parse_fn_expr(&mut self) -> PResult<Expr> {
        let start = self.lo();
        let mut sig = self.parse_fn_sig()?;
        let where_ = self.parse_where_opt()?;
        let is_lit = self.at(Punct::FatArrow) || (self.at(Punct::LBrace) && !self.restr.no_brace);
        if !is_lit {
            if !where_.is_empty() {
                return self.expected("a function body after the `where` clause");
            }
            self.fn_sig_to_type(&mut sig);
            let span = self.span_from(start);
            return Ok(self.mk_expr(ExprKind::FnType(Box::new(sig)), span));
        }
        for p in &sig.params {
            if p.names.is_empty() {
                self.report(p.span, "function literal parameters must be named");
            }
        }
        let saved_labels = std::mem::take(&mut self.labels);
        let body = if self.eat(Punct::FatArrow) {
            if self.at(Punct::LBrace) {
                let span = self.span();
                self.report(span, "`=>` takes an expression; a block body is written without it: `fn(…) -> R { … }`");
                self.parse_block().map(FnBody::Block)
            } else {
                self.parse_expr().map(FnBody::Expr)
            }
        } else {
            self.parse_block().map(FnBody::Block)
        };
        self.labels = saved_labels;
        let body = body?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Fn(Box::new(FnLit { sig, where_, body, span })), span))
    }

    /// In a function type, a parameter group that is only names (`fn(i32, u8)`)
    /// is a list of unnamed parameter types.
    fn fn_sig_to_type(&mut self, sig: &mut FnSig) {
        let params = std::mem::take(&mut sig.params);
        for p in params {
            if p.ty.is_none() && p.default.is_none() && !p.names.is_empty() {
                for n in p.names {
                    let ty = self.mk_expr(ExprKind::Ident(n), n.span);
                    sig.params.push(Param {
                        names: vec![],
                        comptime: false,
                        ty: Some(ty),
                        default: None,
                        span: n.span,
                    });
                }
            } else {
                sig.params.push(p);
            }
        }
    }

    /// The parameters after `(`, through the closing `)`. Returns whether the
    /// list ended with `...`.
    pub(super) fn parse_params(&mut self) -> PResult<(Vec<Param>, bool)> {
        self.unrestricted(|p| {
            let mut params = Vec::new();
            let mut variadic = false;
            while !p.at(Punct::RParen) {
                if p.at(Punct::Ellipsis) {
                    p.bump();
                    variadic = true;
                    p.eat(Punct::Comma);
                    break;
                }
                params.push(p.parse_param_group()?);
                if !p.eat(Punct::Comma) {
                    break;
                }
            }
            p.expect(Punct::RParen, "to close the parameter list")?;
            Ok((params, variadic))
        })
    }

    /// `a, b: T = d`, `N :: usize`, a bare `x`, or (in function types) a bare type.
    fn parse_param_group(&mut self) -> PResult<Param> {
        let start = self.lo();
        // Count the leading `name {, name}` run.
        let mut k = 0;
        let mut names_found = 0;
        while self.nth_is_name(k) {
            names_found += 1;
            k += 1;
            if self.nth_is(k, Punct::Comma) && self.nth_is_name(k + 1) {
                k += 1;
            } else {
                break;
            }
        }
        let follow = self.nth(k);
        let named = names_found > 0
            && matches!(
                follow,
                TokenKind::Punct(Punct::Colon | Punct::ColonColon | Punct::Comma | Punct::RParen | Punct::Assign)
            );
        if !named {
            let ty = self.parse_type()?;
            return Ok(Param {
                names: vec![],
                comptime: false,
                ty: Some(ty),
                default: None,
                span: self.span_from(start),
            });
        }
        let mut names = Vec::new();
        for i in 0..names_found {
            if i > 0 {
                self.bump();
            }
            names.push(self.parse_name("a parameter name")?);
        }
        let mut comptime = false;
        let ty = if self.at(Punct::Colon) || self.at(Punct::ColonColon) {
            comptime = self.at(Punct::ColonColon);
            self.bump();
            Some(self.parse_type()?)
        } else {
            None
        };
        let default = if self.eat(Punct::Assign) { Some(self.parse_expr()?) } else { None };
        Ok(Param { names, comptime, ty, default, span: self.span_from(start) })
    }

    /// `\ { E1, E2 }`.
    pub(super) fn parse_effect_set(&mut self) -> PResult<Vec<Expr>> {
        self.expect(Punct::Backslash, "")?;
        self.expect(Punct::LBrace, "to begin the effect set")?;
        self.unrestricted(|p| {
            let mut effects = Vec::new();
            p.skip_newlines();
            while !p.at(Punct::RBrace) {
                effects.push(p.parse_type()?);
                p.skip_newlines();
                if !p.eat(Punct::Comma) {
                    break;
                }
                p.skip_newlines();
            }
            p.expect(Punct::RBrace, "to close the effect set")?;
            Ok(effects)
        })
    }

    /// An optional `where Ord(T), Eq(K)` clause.
    pub(super) fn parse_where_opt(&mut self) -> PResult<Vec<Constraint>> {
        let mut out = Vec::new();
        if !self.at_word(self.words.where_) {
            return Ok(out);
        }
        self.bump();
        loop {
            let start = self.lo();
            let name = self.parse_ident("a constraint such as `Ord(T)`")?;
            self.expect(Punct::LParen, "after the constraint name")?;
            let ty = self.unrestricted(|p| p.parse_type())?;
            self.expect(Punct::RParen, "to close the constraint")?;
            out.push(Constraint { name, ty, span: self.span_from(start) });
            if self.eat(Punct::Comma) && matches!(self.kind(), TokenKind::Ident(_)) && self.nth_is(1, Punct::LParen) {
                continue;
            }
            break;
        }
        Ok(out)
    }
}
