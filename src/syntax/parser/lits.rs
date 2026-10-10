//! Type literals (`struct`, `union`, `effect`), handler literals and foreign
//! blocks: the constructs whose bodies are written between braces.

use super::{Abort, PResult, Parser};
use crate::syntax::ast::*;
use crate::syntax::token::{Keyword, Punct, TokenKind};

impl Parser<'_> {
    /// `(` params `)` after `struct`, `union` or `effect`: every parameter is
    /// compile-time and written with `:`.
    pub(super) fn parse_decl_params(&mut self) -> PResult<Vec<Param>> {
        self.expect(Punct::LParen, "")?;
        let (params, variadic) = self.parse_params()?;
        let _ = variadic;
        for p in &params {
            if p.comptime {
                self.report(p.span, "parameters of a type are always compile-time; declare them with `:`, not `::`");
            } else if p.names.is_empty() || p.ty.is_none() {
                self.report(p.span, "type parameters must be named and typed, like `T: type`");
            }
        }
        Ok(params)
    }

    // ------------------------------------------------------------------
    // struct
    // ------------------------------------------------------------------

    pub(super) fn parse_struct_lit(&mut self) -> PResult<Expr> {
        self.deny_in_head("a struct literal")?;
        let start = self.lo();
        self.bump();
        let params = if self.at(Punct::LParen) { Some(self.parse_decl_params()?) } else { None };
        let where_ = self.parse_where_opt()?;
        self.expect(Punct::LBrace, "to begin the struct body")?;
        let members = self.unrestricted(|p| p.parse_struct_members());
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Struct(Box::new(StructLit { params, where_, members, span })), span))
    }

    /// The fields after `{`, through the closing `}`. Errors are recovered
    /// field by field.
    fn parse_struct_members(&mut self) -> Vec<StructMember> {
        let mut members = Vec::new();
        loop {
            self.skip_newlines();
            if self.at(Punct::RBrace) {
                self.bump();
                break;
            }
            if self.at_eof() {
                let _: PResult<()> = self.expected("`}` to close the struct body");
                break;
            }
            let before = self.pos;
            match self.parse_struct_member() {
                Ok(m) => {
                    members.push(m);
                    // Fields are separated by newlines or commas (§A.2).
                    if !self.eat(Punct::Comma) {
                        self.expect_terminator("the field");
                    }
                }
                Err(Abort) => self.recover_to_stmt_end(),
            }
            if self.pos == before {
                self.bump();
            }
        }
        members
    }

    fn parse_struct_member(&mut self) -> PResult<StructMember> {
        let start = self.lo();
        let attrs = self.parse_attrs()?;
        if self.at_kw(Keyword::When) {
            if let Some(a) = attrs.first() {
                self.report(a.span, "attributes cannot be applied to a `when` block");
            }
            return self.parse_struct_when();
        }
        let is_pub = self.eat_kw(Keyword::Pub);
        let is_using = self.eat_kw(Keyword::Using);
        let mut names = vec![self.parse_name("a field name")?];
        while self.eat(Punct::Comma) {
            names.push(self.parse_name("a field name")?);
        }
        self.expect(Punct::Colon, "and a type after the field name")?;
        let ty = self.parse_type()?;
        let default = if self.eat(Punct::Assign) { Some(self.parse_expr()?) } else { None };
        let span = self.span_from(start);
        Ok(StructMember::Field(FieldDecl { attrs, is_pub, is_using, names, ty, default, span }))
    }

    fn parse_struct_when(&mut self) -> PResult<StructMember> {
        let start = self.lo();
        self.bump();
        let cond = self.parse_head_expr()?;
        self.expect(Punct::LBrace, "to begin the `when` block")?;
        let then = self.unrestricted(|p| p.parse_struct_members());
        let else_ = if self.eat_kw(Keyword::Else) {
            self.expect(Punct::LBrace, "after `else`")?;
            Some(self.unrestricted(|p| p.parse_struct_members()))
        } else {
            None
        };
        Ok(StructMember::When { cond, then, else_, span: self.span_from(start) })
    }

    // ------------------------------------------------------------------
    // union
    // ------------------------------------------------------------------

    /// After `union`, at `(`: is this a parameter list (`union(T: type)`) rather
    /// than a tag type (`union(u8)`)?
    fn paren_is_param_list(&self) -> bool {
        if self.nth_is(1, Punct::RParen) {
            return true;
        }
        let mut k = 1;
        while self.nth_is_name(k) {
            k += 1;
            if self.nth_is(k, Punct::Comma) && self.nth_is_name(k + 1) {
                k += 1;
            } else {
                break;
            }
        }
        k > 1 && matches!(self.nth(k), TokenKind::Punct(Punct::Colon | Punct::ColonColon))
    }

    pub(super) fn parse_union_lit(&mut self) -> PResult<Expr> {
        self.deny_in_head("a union literal")?;
        let start = self.lo();
        self.bump();
        let mut tag_type = None;
        let mut params = None;
        if self.at(Punct::LParen) {
            if self.paren_is_param_list() {
                params = Some(self.parse_decl_params()?);
            } else {
                self.bump();
                tag_type = Some(self.unrestricted(|p| p.parse_type())?);
                self.expect(Punct::RParen, "to close the tag type")?;
            }
        }
        let where_ = self.parse_where_opt()?;
        self.expect(Punct::LBrace, "to begin the union body")?;
        let members = self.unrestricted(|p| p.parse_union_members());
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Union(Box::new(UnionLit { tag_type, params, where_, members, span })), span))
    }

    /// The variants after `{`, through the closing `}`. Variants are separated
    /// by commas or newlines.
    fn parse_union_members(&mut self) -> Vec<UnionMember> {
        let mut members = Vec::new();
        loop {
            self.skip_newlines();
            if self.at(Punct::RBrace) {
                self.bump();
                break;
            }
            if self.at_eof() {
                let _: PResult<()> = self.expected("`}` to close the union body");
                break;
            }
            let before = self.pos;
            match self.parse_union_member() {
                Ok(m) => {
                    members.push(m);
                    let mut sep = self.eat(Punct::Comma);
                    while self.at_newline() {
                        self.bump();
                        sep = true;
                    }
                    if !sep && !self.at(Punct::RBrace) && !self.at_eof() {
                        let _: PResult<()> = self.expected("`,` or a newline after the variant");
                        self.recover_to_stmt_end();
                    }
                }
                Err(Abort) => self.recover_to_stmt_end(),
            }
            if self.pos == before {
                self.bump();
            }
        }
        members
    }

    fn parse_union_member(&mut self) -> PResult<UnionMember> {
        let start = self.lo();
        if self.at_kw(Keyword::When) {
            self.bump();
            let cond = self.parse_head_expr()?;
            self.expect(Punct::LBrace, "to begin the `when` block")?;
            let then = self.unrestricted(|p| p.parse_union_members());
            let else_ = if self.eat_kw(Keyword::Else) {
                self.expect(Punct::LBrace, "after `else`")?;
                Some(self.unrestricted(|p| p.parse_union_members()))
            } else {
                None
            };
            return Ok(UnionMember::When { cond, then, else_, span: self.span_from(start) });
        }
        let name = self.parse_ident("a variant name")?;
        let payload = if self.eat(Punct::Colon) { Some(self.parse_type()?) } else { None };
        let value = if self.eat(Punct::Assign) { Some(self.parse_expr()?) } else { None };
        Ok(UnionMember::Variant(Variant { name, payload, value, span: self.span_from(start) }))
    }

    // ------------------------------------------------------------------
    // effect
    // ------------------------------------------------------------------

    pub(super) fn parse_effect_lit(&mut self) -> PResult<Expr> {
        self.deny_in_head("an effect literal")?;
        let start = self.lo();
        self.bump();
        let params = if self.at(Punct::LParen) { Some(self.parse_decl_params()?) } else { None };
        self.expect(Punct::LBrace, "to begin the effect body")?;
        let ops = self.unrestricted(|p| p.parse_effect_ops());
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Effect(Box::new(EffectLit { params, ops, span })), span))
    }

    fn parse_effect_ops(&mut self) -> Vec<OpDecl> {
        let mut ops = Vec::new();
        loop {
            self.skip_newlines();
            if self.at(Punct::RBrace) {
                self.bump();
                break;
            }
            if self.at_eof() {
                let _: PResult<()> = self.expected("`}` to close the effect body");
                break;
            }
            let before = self.pos;
            match self.parse_op_decl() {
                Ok(op) => {
                    ops.push(op);
                    self.expect_terminator("the operation");
                }
                Err(Abort) => self.recover_to_stmt_end(),
            }
            if self.pos == before {
                self.bump();
            }
        }
        ops
    }

    /// `name: fn(params) -> T`, with `esc` or `ctl` in place of `fn`.
    fn parse_op_decl(&mut self) -> PResult<OpDecl> {
        let start = self.lo();
        let name = self.parse_ident("an operation name")?;
        self.expect(Punct::Colon, "and an operation kind after the name")?;
        let kind = match self.kind() {
            TokenKind::Keyword(Keyword::Fn) => OpKind::Fn,
            TokenKind::Keyword(Keyword::Esc) => OpKind::Esc,
            TokenKind::Keyword(Keyword::Ctl) => OpKind::Ctl,
            _ => return self.expected("`fn`, `esc` or `ctl`"),
        };
        self.bump();
        self.expect(Punct::LParen, "to begin the parameter list")?;
        let (params, _) = self.parse_params()?;
        let ret = if self.eat(Punct::Arrow) { Some(self.parse_type()?) } else { None };
        Ok(OpDecl { name, kind, params, ret, span: self.span_from(start) })
    }

    // ------------------------------------------------------------------
    // handler
    // ------------------------------------------------------------------

    /// `handler Effect { clauses }` as an expression.
    pub(super) fn parse_handler_lit(&mut self) -> PResult<Expr> {
        self.deny_in_head("a handler literal")?;
        let start = self.lo();
        self.bump();
        let effect = self.parse_effect_ref()?;
        self.expect(Punct::LBrace, "to begin the handler body")?;
        let clauses = self.unrestricted(|p| p.parse_clauses());
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Handler(Box::new(HandlerLit { effect, clauses, span })), span))
    }

    /// The effect named by a handler: a postfix expression such as `Log`,
    /// `Yield(T)` or `m.Net`, parsed so that a following `{` is not consumed.
    pub(super) fn parse_effect_ref(&mut self) -> PResult<Expr> {
        self.with_restr(super::Restr { no_brace: true, ..Default::default() }, |p| p.parse_postfix(false))
    }

    /// Handler clauses after `{`, through the closing `}`.
    pub(super) fn parse_clauses(&mut self) -> Vec<Clause> {
        let mut clauses = Vec::new();
        loop {
            self.skip_newlines();
            if self.at(Punct::RBrace) {
                self.bump();
                break;
            }
            if self.at_eof() {
                let _: PResult<()> = self.expected("`}` to close the handler body");
                break;
            }
            let before = self.pos;
            match self.parse_clause() {
                Ok(c) => {
                    clauses.push(c);
                    self.expect_terminator("the clause");
                }
                Err(Abort) => self.recover_to_stmt_end(),
            }
            if self.pos == before {
                self.bump();
            }
        }
        clauses
    }

    /// `name(a, b, k) => expr | block`.
    fn parse_clause(&mut self) -> PResult<Clause> {
        let start = self.lo();
        let name = self.parse_ident("an operation name")?;
        self.expect(Punct::LParen, "after the operation name")?;
        let mut params = Vec::new();
        while !self.at(Punct::RParen) {
            params.push(self.parse_name("a parameter name")?);
            if !self.eat(Punct::Comma) {
                break;
            }
        }
        self.expect(Punct::RParen, "to close the clause parameters")?;
        self.expect(Punct::FatArrow, "after the clause parameters")?;
        let body = if self.at(Punct::LBrace) { self.parse_block_expr()? } else { self.parse_expr()? };
        Ok(Clause { name, params, body, span: self.span_from(start) })
    }

    // ------------------------------------------------------------------
    // foreign
    // ------------------------------------------------------------------

    /// `foreign "lib" { members }`.
    pub(super) fn parse_foreign(&mut self) -> PResult<Expr> {
        self.deny_in_head("a foreign block")?;
        let start = self.lo();
        self.bump();
        let lib = match self.kind() {
            TokenKind::Str(lit) => {
                self.bump();
                (**lit).clone()
            }
            _ => return self.expected("the library name as a string literal after `foreign`"),
        };
        self.expect(Punct::LBrace, "to begin the foreign block")?;
        let members = self.unrestricted(|p| p.parse_foreign_members());
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Foreign(Box::new(ForeignBlock { lib, members, span })), span))
    }

    fn parse_foreign_members(&mut self) -> Vec<ForeignMember> {
        let mut members = Vec::new();
        loop {
            self.skip_newlines();
            if self.at(Punct::RBrace) {
                self.bump();
                break;
            }
            if self.at_eof() {
                let _: PResult<()> = self.expected("`}` to close the foreign block");
                break;
            }
            let before = self.pos;
            match self.parse_foreign_member() {
                Ok(m) => {
                    members.push(m);
                    self.expect_terminator("the foreign declaration");
                }
                Err(Abort) => self.recover_to_stmt_end(),
            }
            if self.pos == before {
                self.bump();
            }
        }
        members
    }

    fn parse_foreign_member(&mut self) -> PResult<ForeignMember> {
        let start = self.lo();
        let mut attrs = self.parse_attrs()?;
        let global = attrs.iter().position(|a| a.name.name == self.words.global);
        if let Some(i) = global {
            attrs.remove(i);
            let name = self.parse_ident("the name of the foreign variable")?;
            self.expect(Punct::Colon, "and a type after the variable name")?;
            let ty = self.parse_type()?;
            return Ok(ForeignMember {
                attrs,
                kind: ForeignMemberKind::Global { name, ty },
                span: self.span_from(start),
            });
        }
        let name = self.parse_ident("the name of a foreign function")?;
        self.expect(Punct::ColonColon, "and a function type after the name")?;
        if !self.at_kw(Keyword::Fn) {
            return self.expected("a function type like `fn(args) -> T`");
        }
        let sig = self.parse_fn_type_sig()?;
        Ok(ForeignMember { attrs, kind: ForeignMemberKind::Fn { name, sig }, span: self.span_from(start) })
    }
}
