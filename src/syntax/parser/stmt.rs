//! Blocks, statements, bindings and control flow (Appendix A.4, spec §4 and §8).

use super::{Abort, PResult, Parser, Restr};
use crate::syntax::ast::*;
use crate::syntax::token::{Keyword, Punct, TokenKind};

/// Maps an assignment operator token to its [`AssignOp`].
fn assign_op(kind: &TokenKind) -> Option<AssignOp> {
    use BinOp::*;
    use Flavor::*;
    let TokenKind::Punct(p) = kind else { return None };
    Some(match p {
        Punct::Assign => AssignOp::Assign,
        Punct::PlusAssign => AssignOp::Compound(Add(Checked)),
        Punct::MinusAssign => AssignOp::Compound(Sub(Checked)),
        Punct::StarAssign => AssignOp::Compound(Mul(Checked)),
        Punct::SlashAssign => AssignOp::Compound(Div),
        Punct::PercentAssign => AssignOp::Compound(Rem),
        Punct::ShlAssign => AssignOp::Compound(Shl(Checked)),
        Punct::ShrAssign => AssignOp::Compound(Shr),
        Punct::AmpAssign => AssignOp::Compound(BitAnd),
        Punct::PipeAssign => AssignOp::Compound(BitOr),
        Punct::TildeAssign => AssignOp::Compound(BitXor),
        Punct::PlusWrapAssign => AssignOp::Compound(Add(Wrap)),
        Punct::MinusWrapAssign => AssignOp::Compound(Sub(Wrap)),
        Punct::StarWrapAssign => AssignOp::Compound(Mul(Wrap)),
        Punct::ShlWrapAssign => AssignOp::Compound(Shl(Wrap)),
        Punct::PlusSatAssign => AssignOp::Compound(Add(Sat)),
        Punct::MinusSatAssign => AssignOp::Compound(Sub(Sat)),
        Punct::StarSatAssign => AssignOp::Compound(Mul(Sat)),
        Punct::ShlSatAssign => AssignOp::Compound(Shl(Sat)),
        _ => return None,
    })
}

impl Parser<'_> {
    // ------------------------------------------------------------------
    // Blocks
    // ------------------------------------------------------------------

    /// `{ stmt … }`. Statement-level errors are recovered inside the block, so
    /// this fails only if the `{` is missing.
    pub(super) fn parse_block(&mut self) -> PResult<Block> {
        let start = self.lo();
        self.expect(Punct::LBrace, "to begin a block")?;
        let stmts = self.unrestricted(|p| p.nest(|p| Ok(p.parse_block_body())))?;
        let id = self.ids.next();
        Ok(Block { id, stmts, span: self.span_from(start) })
    }

    /// The statements after `{`, through the closing `}`.
    fn parse_block_body(&mut self) -> Vec<Stmt> {
        let mut stmts = Vec::new();
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
            let start = self.lo();
            match self.parse_stmt() {
                Ok(s) => {
                    stmts.push(s);
                    self.expect_terminator("the statement");
                }
                Err(Abort) => {
                    self.recover_to_stmt_end();
                    let span = self.span_from(start);
                    let e = self.error_expr(span);
                    let id = self.ids.next();
                    stmts.push(Stmt { id, kind: StmtKind::Expr(e), span });
                }
            }
            if self.pos == before {
                self.bump();
            }
        }
        stmts
    }

    // ------------------------------------------------------------------
    // Statements
    // ------------------------------------------------------------------

    pub(super) fn parse_stmt(&mut self) -> PResult<Stmt> {
        let start = self.lo();
        let kind = match self.kind() {
            TokenKind::Attribute(_) => {
                let attrs = self.parse_attrs()?;
                if !self.binding_ahead() {
                    return self.expected("a declaration after the attribute");
                }
                let binding = self.parse_binding()?;
                StmtKind::Binding { attrs, binding }
            }
            TokenKind::Keyword(Keyword::Defer | Keyword::Errdefer) => {
                let is_err = self.at_kw(Keyword::Errdefer);
                self.bump();
                let body = self.parse_defer_body()?;
                StmtKind::Defer { is_err, body: Box::new(body) }
            }
            TokenKind::Keyword(Keyword::When) => StmtKind::When(self.parse_when_stmt()?),
            TokenKind::Keyword(Keyword::While) => self.parse_while(None)?,
            TokenKind::Keyword(Keyword::Discard) => {
                self.bump();
                StmtKind::Discard(self.parse_expr()?)
            }
            TokenKind::Punct(Punct::LBrace) => StmtKind::Block(self.parse_block()?),
            TokenKind::Ident(_) | TokenKind::Underscore => self.parse_name_stmt()?,
            _ => self.parse_expr_or_assign()?,
        };
        let id = self.ids.next();
        Ok(Stmt { id, kind, span: self.span_from(start) })
    }

    /// A statement starting with an identifier or `_` (A.7 rule 1).
    fn parse_name_stmt(&mut self) -> PResult<StmtKind> {
        if matches!(self.kind(), TokenKind::Ident(_))
            && self.nth_is(1, Punct::Colon)
            && matches!(self.nth(2), TokenKind::Keyword(Keyword::For | Keyword::While | Keyword::With))
        {
            let label = self.parse_ident("a label")?;
            self.bump();
            return match self.kind() {
                TokenKind::Keyword(Keyword::While) => self.parse_while(Some(label)),
                TokenKind::Keyword(Keyword::For) => Ok(StmtKind::Expr(self.parse_for(Some(label))?)),
                _ => Ok(StmtKind::Expr(self.parse_with(Some(label))?)),
            };
        }
        if self.binding_ahead() {
            let binding = self.parse_binding()?;
            return Ok(StmtKind::Binding { attrs: Vec::new(), binding });
        }
        self.parse_expr_or_assign()
    }

    /// True if a binding (`x :: e`, `x := e`, `x: T …`, `a, b := e`) starts here.
    pub(super) fn binding_ahead(&self) -> bool {
        if !self.nth_is_name(0) {
            return false;
        }
        match self.nth(1) {
            TokenKind::Punct(Punct::ColonColon | Punct::ColonEq | Punct::Colon) => true,
            TokenKind::Punct(Punct::Comma) => {
                let mut k = 0;
                while self.nth_is_name(k) && self.nth_is(k + 1, Punct::Comma) {
                    k += 2;
                }
                self.nth_is_name(k)
                    && matches!(self.nth(k + 1), TokenKind::Punct(Punct::ColonColon | Punct::ColonEq | Punct::Colon))
            }
            _ => false,
        }
    }

    /// `names (:: | :=) e`, `names : T (: | =) e`.
    pub(super) fn parse_binding(&mut self) -> PResult<Binding> {
        let start = self.lo();
        let mut names = vec![self.parse_name("a name")?];
        while self.eat(Punct::Comma) {
            names.push(self.parse_name("a name")?);
        }
        let mut ty = None;
        let kind = match self.kind() {
            TokenKind::Punct(Punct::ColonColon) => {
                self.bump();
                BindingKind::Const
            }
            TokenKind::Punct(Punct::ColonEq) => {
                self.bump();
                BindingKind::Var
            }
            TokenKind::Punct(Punct::Colon) => {
                self.bump();
                ty = Some(Box::new(self.parse_type()?));
                match self.kind() {
                    TokenKind::Punct(Punct::Colon) => {
                        self.bump();
                        BindingKind::Const
                    }
                    TokenKind::Punct(Punct::Assign) => {
                        self.bump();
                        BindingKind::Var
                    }
                    _ => return self.expected("`=` or `:` and an initializer after the type"),
                }
            }
            _ => return self.expected("`::`, `:=` or `:` in a binding"),
        };
        let init = Box::new(self.parse_expr()?);
        let id = self.ids.next();
        Ok(Binding { id, names, ty, kind, init, span: self.span_from(start) })
    }

    /// An expression statement, or an assignment `place[, place…] op= e`.
    fn parse_expr_or_assign(&mut self) -> PResult<StmtKind> {
        let first = self.parse_expr()?;
        let mut places = vec![first];
        while self.at(Punct::Comma) {
            self.bump();
            places.push(self.parse_expr()?);
        }
        if let Some(op) = assign_op(self.kind()) {
            self.bump();
            let value = Box::new(self.parse_expr()?);
            return Ok(StmtKind::Assign { places, op, value });
        }
        if places.len() > 1 {
            return self.expected("an assignment operator after the list of places");
        }
        match places.pop() {
            Some(e) => Ok(StmtKind::Expr(e)),
            None => Err(Abort),
        }
    }

    /// The statement after `defer` / `errdefer`: a block, an expression, or an assignment.
    fn parse_defer_body(&mut self) -> PResult<Stmt> {
        let start = self.lo();
        let kind =
            if self.at(Punct::LBrace) { StmtKind::Block(self.parse_block()?) } else { self.parse_expr_or_assign()? };
        let id = self.ids.next();
        Ok(Stmt { id, kind, span: self.span_from(start) })
    }

    // ------------------------------------------------------------------
    // when
    // ------------------------------------------------------------------

    fn parse_when_stmt(&mut self) -> PResult<WhenStmt> {
        let start = self.lo();
        self.bump();
        let cond = self.parse_head_expr()?;
        let then = self.parse_block()?;
        let else_ = if self.eat_kw(Keyword::Else) {
            if self.at_kw(Keyword::When) {
                Some(WhenElse::When(Box::new(self.parse_when_stmt()?)))
            } else {
                Some(WhenElse::Block(self.parse_block()?))
            }
        } else {
            None
        };
        Ok(WhenStmt { cond, then, else_, span: self.span_from(start) })
    }

    // ------------------------------------------------------------------
    // Conditions, if, while
    // ------------------------------------------------------------------

    /// `cond = cond_part { 'and' cond_part }` (A.7 rule 2).
    pub(super) fn parse_cond(&mut self) -> PResult<Cond> {
        let start = self.lo();
        let restr = Restr { no_brace: true, no_and: true, in_index: false };
        let parts = self.with_restr(restr, |p| {
            let mut parts = Vec::new();
            loop {
                parts.push(p.parse_cond_part()?);
                if !p.eat_kw(Keyword::And) {
                    break;
                }
            }
            Ok(parts)
        })?;
        // The parts are joined by `and`, so an `or` directly inside one of them
        // mixes the two operators (§7.8).
        if parts.len() > 1 {
            for part in &parts {
                let e = match part {
                    CondPart::Expr(e) | CondPart::Bind { value: e, .. } => e,
                };
                if matches!(e.kind, ExprKind::Binary { op: BinOp::Or, .. }) {
                    self.report(e.span, "cannot mix `and` and `or` without parentheses");
                }
            }
        }
        Ok(Cond { parts, span: self.span_from(start) })
    }

    fn parse_cond_part(&mut self) -> PResult<CondPart> {
        let bind = self.speculate(|p| {
            let pat = p.parse_pattern()?;
            if p.at(Punct::ColonEq) { Ok(pat) } else { Err(Abort) }
        });
        match bind {
            Some(pat) => {
                self.bump();
                let value = self.parse_expr()?;
                Ok(CondPart::Bind { pat, value })
            }
            None => Ok(CondPart::Expr(self.parse_expr()?)),
        }
    }

    /// `if cond { … } { else if cond { … } } [else { … } | else e => …]`.
    pub(super) fn parse_if(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        let mut branches = Vec::new();
        let cond = self.parse_cond()?;
        let block = self.parse_block()?;
        branches.push((cond, block));
        let mut else_ = None;
        while self.at_kw(Keyword::Else) {
            if self.nth_is_kw(1, Keyword::If) {
                self.bump();
                self.bump();
                let cond = self.parse_cond()?;
                let block = self.parse_block()?;
                branches.push((cond, block));
            } else if self.nth_is(1, Punct::LBrace) {
                self.bump();
                else_ = Some(IfElse::Block(self.parse_block()?));
                break;
            } else if self.nth_is_name(1) && self.nth_is(2, Punct::FatArrow) {
                self.bump();
                let name = self.parse_name("a name")?;
                self.bump();
                let body = Box::new(self.parse_capture_body()?);
                else_ = Some(IfElse::Capture { name, body });
                break;
            } else {
                // `if c { … } else d` is the fallback form `e else d`, handled by the caller.
                break;
            }
        }
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::If(Box::new(IfExpr { branches, else_, span })), span))
    }

    /// Parses a loop or `with` body with `label` in scope.
    fn parse_labeled_block(&mut self, label: Option<Ident>) -> PResult<Block> {
        if let Some(l) = label {
            self.labels.push(l.name);
        }
        let r = self.parse_block();
        if label.is_some() {
            self.labels.pop();
        }
        r
    }

    /// `while cond { … }`, after an optional label.
    fn parse_while(&mut self, label: Option<Ident>) -> PResult<StmtKind> {
        self.bump();
        let cond = self.parse_cond()?;
        let body = self.parse_labeled_block(label)?;
        Ok(StmtKind::While { label, cond, body })
    }

    // ------------------------------------------------------------------
    // for, with, match
    // ------------------------------------------------------------------

    /// `[label:] for p, q in e { … }`; the label has been consumed.
    pub(super) fn parse_for(&mut self, label: Option<Ident>) -> PResult<Expr> {
        let start = label.map_or(self.lo(), |l| l.span.lo);
        self.bump();
        let mut pats = vec![self.parse_pattern()?];
        while self.eat(Punct::Comma) {
            pats.push(self.parse_pattern()?);
        }
        self.expect_kw(Keyword::In, "after the patterns of the `for`")?;
        let iter = self.parse_head_expr()?;
        let body = self.parse_labeled_block(label)?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::For(Box::new(ForExpr { label, pats, iter, body, span })), span))
    }

    /// `[label:] with e, f [on s] { … }`; the label has been consumed.
    pub(super) fn parse_with(&mut self, label: Option<Ident>) -> PResult<Expr> {
        let start = label.map_or(self.lo(), |l| l.span.lo);
        self.bump();
        let mut handlers = vec![self.parse_head_expr()?];
        while self.eat(Punct::Comma) {
            handlers.push(self.parse_head_expr()?);
        }
        let on = if self.at_word(self.words.on) {
            self.bump();
            Some(self.parse_head_expr()?)
        } else {
            None
        };
        let body = self.parse_labeled_block(label)?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::With(Box::new(WithExpr { label, handlers, on, body, span })), span))
    }

    /// `match e { arm … }`.
    pub(super) fn parse_match(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        let scrutinee = self.parse_head_expr()?;
        self.expect(Punct::LBrace, "to begin the match arms")?;
        let arms = self.unrestricted(|p| p.nest(|p| Ok(p.parse_arms())))?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Match(Box::new(MatchExpr { scrutinee, arms, span })), span))
    }

    /// The arms after `{`, through the closing `}`.
    fn parse_arms(&mut self) -> Vec<Arm> {
        let mut arms = Vec::new();
        loop {
            self.skip_newlines();
            if self.at(Punct::RBrace) {
                self.bump();
                break;
            }
            if self.at_eof() {
                let _: PResult<()> = self.expected("`}` to close the match");
                break;
            }
            let before = self.pos;
            match self.parse_arm() {
                Ok(arm) => {
                    arms.push(arm);
                    self.expect_terminator("the match arm");
                }
                Err(Abort) => self.recover_to_stmt_end(),
            }
            if self.pos == before {
                self.bump();
            }
        }
        arms
    }

    /// `p1, p2 [if guard] => body`.
    fn parse_arm(&mut self) -> PResult<Arm> {
        let start = self.lo();
        let mut pats = vec![self.parse_pattern()?];
        while self.eat(Punct::Comma) {
            pats.push(self.parse_pattern()?);
        }
        let guard = if self.eat_kw(Keyword::If) { Some(self.parse_expr()?) } else { None };
        self.expect(Punct::FatArrow, "after the pattern of the match arm")?;
        let body = if self.at(Punct::LBrace) { self.parse_block_expr()? } else { self.parse_expr()? };
        Ok(Arm { pats, guard, body, span: self.span_from(start) })
    }
}
