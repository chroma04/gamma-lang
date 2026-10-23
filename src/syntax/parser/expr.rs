//! Expressions (Appendix A.5): precedence climbing, postfix operators, primaries.

use super::types::Mode;
use super::{PResult, Parser, Restr};
use crate::diag::{Diagnostic, Edit};
use crate::source::Span;
use crate::syntax::ast::*;
use crate::syntax::token::{Keyword, Punct, TokenKind};

/// True if a token can begin an expression (used for the optional operand of
/// `return` and `break`, A.7 rule 11).
pub(super) fn can_begin_expr(kind: &TokenKind) -> bool {
    match kind {
        TokenKind::Ident(_)
        | TokenKind::Underscore
        | TokenKind::Int(_)
        | TokenKind::Float(_)
        | TokenKind::Str(_)
        | TokenKind::Intrinsic(_)
        | TokenKind::Attribute(_) => true,
        TokenKind::Keyword(k) => matches!(
            k,
            Keyword::True
                | Keyword::False
                | Keyword::Null
                | Keyword::Fn
                | Keyword::Struct
                | Keyword::Union
                | Keyword::Effect
                | Keyword::Handler
                | Keyword::Import
                | Keyword::Foreign
                | Keyword::If
                | Keyword::Match
                | Keyword::With
                | Keyword::For
                | Keyword::Return
                | Keyword::Break
                | Keyword::Continue
                | Keyword::Resume
                | Keyword::Try
                | Keyword::Not
        ),
        TokenKind::Punct(p) => matches!(
            p,
            Punct::LParen
                | Punct::LBracket
                | Punct::Dot
                | Punct::Minus
                | Punct::Tilde
                | Punct::Amp
                | Punct::Bang
                | Punct::Question
                | Punct::Caret
                | Punct::Uninit
        ),
        TokenKind::Newline | TokenKind::Eof => false,
    }
}

/// True if a token can begin a type (used for the `!` rule, A.7 rule 3).
fn can_begin_type(kind: &TokenKind) -> bool {
    match kind {
        TokenKind::Ident(_) | TokenKind::Attribute(_) => true,
        TokenKind::Keyword(k) => matches!(k, Keyword::Fn | Keyword::Struct | Keyword::Union | Keyword::Effect),
        TokenKind::Punct(p) => matches!(p, Punct::Question | Punct::Caret | Punct::LBracket | Punct::LParen),
        _ => false,
    }
}

/// The text of an ordering operator (`<`, `<=`, `>`, `>=`).
fn relational(kind: &TokenKind) -> Option<&'static str> {
    match kind {
        TokenKind::Punct(Punct::Lt) => Some("<"),
        TokenKind::Punct(Punct::Le) => Some("<="),
        TokenKind::Punct(Punct::Gt) => Some(">"),
        TokenKind::Punct(Punct::Ge) => Some(">="),
        _ => None,
    }
}

fn cmp_op(kind: &TokenKind) -> Option<BinOp> {
    Some(match kind {
        TokenKind::Keyword(Keyword::Is) => BinOp::Is,
        TokenKind::Keyword(Keyword::In) => BinOp::In,
        TokenKind::Punct(p) => match p {
            Punct::IsNot => BinOp::IsNot,
            Punct::NotIn => BinOp::NotIn,
            Punct::Lt => BinOp::Lt,
            Punct::Le => BinOp::Le,
            Punct::Gt => BinOp::Gt,
            Punct::Ge => BinOp::Ge,
            _ => return None,
        },
        _ => return None,
    })
}

fn bit_op(kind: &TokenKind) -> Option<BinOp> {
    match kind {
        TokenKind::Punct(Punct::Amp) => Some(BinOp::BitAnd),
        TokenKind::Punct(Punct::Pipe) => Some(BinOp::BitOr),
        TokenKind::Punct(Punct::Tilde) => Some(BinOp::BitXor),
        _ => None,
    }
}

fn shift_op(kind: &TokenKind) -> Option<BinOp> {
    let TokenKind::Punct(p) = kind else { return None };
    Some(match p {
        Punct::Shl => BinOp::Shl(Flavor::Checked),
        Punct::ShlWrap => BinOp::Shl(Flavor::Wrap),
        Punct::ShlSat => BinOp::Shl(Flavor::Sat),
        Punct::ShlOpt => BinOp::Shl(Flavor::Opt),
        Punct::Shr => BinOp::Shr,
        _ => return None,
    })
}

fn add_op(kind: &TokenKind) -> Option<BinOp> {
    let TokenKind::Punct(p) = kind else { return None };
    Some(match p {
        Punct::Plus => BinOp::Add(Flavor::Checked),
        Punct::PlusWrap => BinOp::Add(Flavor::Wrap),
        Punct::PlusSat => BinOp::Add(Flavor::Sat),
        Punct::PlusOpt => BinOp::Add(Flavor::Opt),
        Punct::Minus => BinOp::Sub(Flavor::Checked),
        Punct::MinusWrap => BinOp::Sub(Flavor::Wrap),
        Punct::MinusSat => BinOp::Sub(Flavor::Sat),
        Punct::MinusOpt => BinOp::Sub(Flavor::Opt),
        _ => return None,
    })
}

fn mul_op(kind: &TokenKind) -> Option<BinOp> {
    let TokenKind::Punct(p) = kind else { return None };
    Some(match p {
        Punct::Star => BinOp::Mul(Flavor::Checked),
        Punct::StarWrap => BinOp::Mul(Flavor::Wrap),
        Punct::StarSat => BinOp::Mul(Flavor::Sat),
        Punct::StarOpt => BinOp::Mul(Flavor::Opt),
        Punct::Slash => BinOp::Div,
        Punct::Percent => BinOp::Rem,
        _ => return None,
    })
}

/// True for an unparenthesized level 3/4 expression (arithmetic).
fn is_arith(e: &Expr) -> bool {
    matches!(
        e.kind,
        ExprKind::Binary { op: BinOp::Add(_) | BinOp::Sub(_) | BinOp::Mul(_) | BinOp::Div | BinOp::Rem, .. }
    )
}

/// The result of parsing the inside of `[ ]` after an operand.
enum Subscript {
    Index(Expr),
    Slice { lo: Option<Box<Expr>>, hi: Option<Box<Expr>>, inclusive: bool },
}

impl Parser<'_> {
    /// Parses an expression.
    pub(super) fn parse_expr(&mut self) -> PResult<Expr> {
        self.nest(|p| p.parse_else())
    }

    /// Parses an expression with `{` reserved for the body of the construct
    /// whose head this is (A.7 rule 4).
    pub(super) fn parse_head_expr(&mut self) -> PResult<Expr> {
        self.with_restr(Restr { no_brace: true, ..Restr::default() }, |p| p.parse_expr())
    }

    fn binary(&mut self, op: BinOp, lhs: Expr, rhs: Expr) -> Expr {
        let span = lhs.span.to(rhs.span);
        self.mk_expr(ExprKind::Binary { op, lhs: Box::new(lhs), rhs: Box::new(rhs) }, span)
    }

    // ------------------------------------------------------------------
    // Level 11: else
    // ------------------------------------------------------------------

    fn parse_else(&mut self) -> PResult<Expr> {
        let lhs = self.parse_logic()?;
        if !self.at_kw(Keyword::Else) {
            return Ok(lhs);
        }
        self.bump();
        // `else x => d`: error capture.
        if self.nth_is_name(0) && self.nth_is(1, Punct::FatArrow) {
            let capture = self.parse_name("a name")?;
            self.bump();
            let rhs = self.parse_capture_body()?;
            self.ambiguous_try_else(&lhs, &rhs);
            let span = lhs.span.to(rhs.span);
            return Ok(
                self.mk_expr(ExprKind::Else { lhs: Box::new(lhs), capture: Some(capture), rhs: Box::new(rhs) }, span)
            );
        }
        // `e else { … }`: a block fallback (not inside a condition, where `{` begins the body).
        let rhs = if self.at(Punct::LBrace) && !self.restr.no_brace {
            self.parse_block_expr()?
        } else {
            self.nest(|p| p.parse_else())?
        };
        self.ambiguous_try_else(&lhs, &rhs);
        let span = lhs.span.to(rhs.span);
        Ok(self.mk_expr(ExprKind::Else { lhs: Box::new(lhs), capture: None, rhs: Box::new(rhs) }, span))
    }

    /// Reports `try x else d`, whose `try` could apply to either side of the `else`.
    fn ambiguous_try_else(&mut self, lhs: &Expr, rhs: &Expr) {
        let ExprKind::Unary { op: UnOp::Try, operand } = &lhs.kind else { return };
        let at = |lo, hi| Span { file: lhs.span.file, lo, hi };
        let paren =
            |lo, hi| vec![Edit { span: at(lo, lo), text: "(".into() }, Edit { span: at(hi, hi), text: ")".into() }];
        let d = Diagnostic::error(lhs.span, "`try x else d` is ambiguous: write `try (x else d)` or `(try x) else d`")
            .with_fix_edits("write `try (x else d)`", paren(operand.span.lo, rhs.span.hi))
            .with_fix_edits("write `(try x) else d`", paren(lhs.span.lo, lhs.span.hi));
        self.report_diag(d);
    }

    /// The body after `else x =>`: a block or an expression.
    pub(super) fn parse_capture_body(&mut self) -> PResult<Expr> {
        if self.at(Punct::LBrace) { self.parse_block_expr() } else { self.parse_expr() }
    }

    // ------------------------------------------------------------------
    // Level 10: and, or
    // ------------------------------------------------------------------

    fn parse_logic(&mut self) -> PResult<Expr> {
        let mut lhs = self.parse_not()?;
        let mut chain: Option<BinOp> = None;
        loop {
            let op = match self.kind() {
                TokenKind::Keyword(Keyword::And) if !self.restr.no_and => BinOp::And,
                TokenKind::Keyword(Keyword::Or) => BinOp::Or,
                _ => break,
            };
            match chain {
                Some(prev) if prev != op => {
                    let span = self.span();
                    self.report(span, "cannot mix `and` and `or` without parentheses");
                }
                Some(_) => {}
                None => chain = Some(op),
            }
            self.bump();
            let rhs = self.parse_not()?;
            lhs = self.binary(op, lhs, rhs);
        }
        Ok(lhs)
    }

    // ------------------------------------------------------------------
    // Level 9: not
    // ------------------------------------------------------------------

    fn parse_not(&mut self) -> PResult<Expr> {
        if self.at_kw(Keyword::Not) {
            let start = self.lo();
            self.bump();
            let operand = self.nest(|p| p.parse_not())?;
            let span = self.span_from(start);
            return Ok(self.mk_expr(ExprKind::Unary { op: UnOp::Not, operand: Box::new(operand) }, span));
        }
        self.parse_cmp()
    }

    // ------------------------------------------------------------------
    // Level 8: comparison
    // ------------------------------------------------------------------

    fn parse_cmp(&mut self) -> PResult<Expr> {
        let start_pos = self.pos;
        let lhs = self.parse_range()?;
        let Some(op) = cmp_op(self.kind()) else { return Ok(lhs) };
        let first_rel = relational(self.kind()).is_some();
        self.bump();
        let rhs = self.parse_range()?;
        let e = self.binary(op, lhs, rhs);
        if cmp_op(self.kind()).is_some() {
            let span = self.span();
            let mut d = Diagnostic::error(span, "comparison operators cannot be chained; use `and`");
            // `a < b < c` is `a < b and b < c` when `b` is a name (so evaluating it twice is
            // harmless), both operators are orderings, and the chain is not the operand of a
            // `not` (which would then apply to the first comparison only).
            let ExprKind::Binary { rhs: mid, .. } = &e.kind else { unreachable!() };
            let negated = start_pos > 0 && self.toks[start_pos - 1].kind.is_keyword(Keyword::Not);
            let fix = match (&mid.kind, relational(self.kind())) {
                (ExprKind::Ident(mid), Some(second)) if first_rel && !negated => {
                    Some(format!("and {} {}", mid.name.as_str(), second))
                }
                _ => None,
            };
            let mut count = 0;
            while cmp_op(self.kind()).is_some() {
                self.bump();
                self.parse_range()?;
                count += 1;
            }
            if let (Some(text), 1) = (fix, count) {
                d = d.with_fix("write `a < b and b < c`", span, text);
            }
            self.report_diag(d);
        }
        Ok(e)
    }

    // ------------------------------------------------------------------
    // Level 7: ranges
    // ------------------------------------------------------------------

    fn parse_range(&mut self) -> PResult<Expr> {
        let lhs = self.parse_bit()?;
        let op = match self.kind() {
            TokenKind::Punct(Punct::DotDot) => BinOp::Range,
            TokenKind::Punct(Punct::DotDotEq) => BinOp::RangeIncl,
            _ => return Ok(lhs),
        };
        self.bump();
        let rhs = if self.restr.in_index && self.at(Punct::RBracket) {
            // `a[lo..]`: the missing upper bound is represented by an empty
            // error node that the index parser recognizes.
            let span = self.span().shrink_to_lo();
            self.error_expr(span)
        } else {
            self.parse_bit()?
        };
        let e = self.binary(op, lhs, rhs);
        if matches!(self.kind(), TokenKind::Punct(Punct::DotDot | Punct::DotDotEq)) {
            let span = self.span();
            self.report(span, "range operators cannot be chained");
            while matches!(self.kind(), TokenKind::Punct(Punct::DotDot | Punct::DotDotEq)) {
                self.bump();
                self.parse_bit()?;
            }
        }
        Ok(e)
    }

    // ------------------------------------------------------------------
    // Level 6: bitwise
    // ------------------------------------------------------------------

    fn parse_bit(&mut self) -> PResult<Expr> {
        let mut lhs = self.parse_shift()?;
        let mut chain: Option<BinOp> = None;
        while let Some(op) = bit_op(self.kind()) {
            match chain {
                Some(prev) if prev != op => {
                    let span = self.span();
                    self.report(span, "cannot mix `&`, `|` and `~` in one expression without parentheses");
                }
                Some(_) => {}
                None => chain = Some(op),
            }
            let tok = self.bump();
            // C's `&&` and `||`: two adjacent tokens here.
            if matches!(op, BinOp::BitAnd | BinOp::BitOr)
                && self.at(if op == BinOp::BitAnd { Punct::Amp } else { Punct::Pipe })
                && self.span().lo == tok.span.hi
            {
                let span = tok.span.to(self.span());
                let (c, word) = if op == BinOp::BitAnd { ("&&", "and") } else { ("||", "or") };
                // `a&&b` needs spaces around the word.
                let before = if lhs.span.hi == span.lo { " " } else { "" };
                self.bump();
                let after = if self.span().lo == span.hi { " " } else { "" };
                let d = Diagnostic::error(span, format!("`{}` is not an operator; use `{}`", c, word)).with_fix(
                    format!("replace `{c}` with `{word}`"),
                    span,
                    format!("{before}{word}{after}"),
                );
                return self.fail_diag(d);
            }
            let rhs = self.parse_shift()?;
            lhs = self.binary(op, lhs, rhs);
        }
        Ok(lhs)
    }

    // ------------------------------------------------------------------
    // Level 5: shifts
    // ------------------------------------------------------------------

    fn parse_shift(&mut self) -> PResult<Expr> {
        let lhs = self.parse_add()?;
        let Some(op) = shift_op(self.kind()) else { return Ok(lhs) };
        self.bump();
        let rhs = self.parse_add()?;
        if is_arith(&lhs) {
            self.report(lhs.span, "an arithmetic operand of a shift must be parenthesized");
        } else if is_arith(&rhs) {
            self.report(rhs.span, "an arithmetic operand of a shift must be parenthesized");
        }
        let e = self.binary(op, lhs, rhs);
        if shift_op(self.kind()).is_some() {
            let span = self.span();
            self.report(span, "shifts cannot be chained without parentheses");
            while shift_op(self.kind()).is_some() {
                self.bump();
                self.parse_add()?;
            }
        }
        Ok(e)
    }

    // ------------------------------------------------------------------
    // Levels 4 and 3: additive and multiplicative
    // ------------------------------------------------------------------

    fn parse_add(&mut self) -> PResult<Expr> {
        let mut lhs = self.parse_mul()?;
        while let Some(op) = add_op(self.kind()) {
            self.bump();
            let rhs = self.parse_mul()?;
            lhs = self.binary(op, lhs, rhs);
        }
        Ok(lhs)
    }

    fn parse_mul(&mut self) -> PResult<Expr> {
        let mut lhs = self.parse_prefix()?;
        while let Some(op) = mul_op(self.kind()) {
            self.bump();
            let rhs = self.parse_prefix()?;
            lhs = self.binary(op, lhs, rhs);
        }
        Ok(lhs)
    }

    // ------------------------------------------------------------------
    // Level 2: prefix operators
    // ------------------------------------------------------------------

    fn parse_prefix(&mut self) -> PResult<Expr> {
        let op = match self.kind() {
            TokenKind::Punct(Punct::Minus) => UnOp::Neg,
            TokenKind::Punct(Punct::Tilde) => UnOp::BitNot,
            TokenKind::Punct(Punct::Amp) => UnOp::AddrOf,
            TokenKind::Punct(Punct::Bang) => UnOp::MakeErr,
            TokenKind::Keyword(Keyword::Try) => UnOp::Try,
            _ => return self.parse_postfix(true),
        };
        let start = self.lo();
        self.bump();
        let operand = self.nest(|p| p.parse_prefix())?;
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Unary { op, operand: Box::new(operand) }, span))
    }

    // ------------------------------------------------------------------
    // Level 1: postfix operators
    // ------------------------------------------------------------------

    /// A primary followed by postfix operators. With `allow_bang` false a `!`
    /// is left alone (it belongs to an enclosing error-union type).
    pub(super) fn parse_postfix(&mut self, allow_bang: bool) -> PResult<Expr> {
        let base = self.parse_primary()?;
        self.parse_postfix_ops(base, allow_bang)
    }

    pub(super) fn parse_postfix_ops(&mut self, mut e: Expr, allow_bang: bool) -> PResult<Expr> {
        let mut links = 0u32;
        loop {
            let start = e.span.lo;
            if matches!(
                self.kind(),
                TokenKind::Punct(Punct::Question | Punct::Caret | Punct::LParen | Punct::LBracket | Punct::Dot)
            ) || (allow_bang && matches!(self.kind(), TokenKind::Punct(Punct::Bang)))
            {
                links += 1;
                if links > super::MAX_POSTFIX_CHAIN {
                    let span = self.span();
                    return self.fail(
                        span,
                        "more than 100000 postfix operations in a row, the limit of this implementation (§23.5)",
                    );
                }
            }
            match self.kind() {
                TokenKind::Punct(Punct::Question) => {
                    self.bump();
                    let span = self.span_from(start);
                    e = self.mk_expr(ExprKind::OptUnwrap(Box::new(e)), span);
                }
                TokenKind::Punct(Punct::Bang) if allow_bang => {
                    e = self.parse_bang(e)?;
                }
                TokenKind::Punct(Punct::Caret) => {
                    self.bump();
                    let span = self.span_from(start);
                    e = self.mk_expr(ExprKind::Deref(Box::new(e)), span);
                }
                TokenKind::Punct(Punct::LParen) => {
                    let (args, trailing_comma) = self.parse_call_args()?;
                    let span = self.span_from(start);
                    e = self.mk_expr(ExprKind::Call { callee: Box::new(e), args, trailing_comma }, span);
                }
                TokenKind::Punct(Punct::LBracket) => {
                    e = self.parse_subscript(e)?;
                }
                TokenKind::Punct(Punct::Dot) => {
                    e = self.parse_dot_suffix(e)?;
                }
                _ => break,
            }
        }
        Ok(e)
    }

    /// `e.name` or `e.0`.
    fn parse_dot_suffix(&mut self, base: Expr) -> PResult<Expr> {
        let start = base.span.lo;
        self.bump();
        let span = self.span();
        match self.kind() {
            TokenKind::Ident(name) => {
                self.bump();
                let name = Ident { name: *name, span };
                let span = self.span_from(start);
                Ok(self.mk_expr(ExprKind::Field { base: Box::new(base), name }, span))
            }
            TokenKind::Keyword(k) => {
                let name = self.keyword_as_name(*k);
                let span = self.span_from(start);
                Ok(self.mk_expr(ExprKind::Field { base: Box::new(base), name }, span))
            }
            TokenKind::Int(lit) => {
                self.bump();
                let index = match u32::try_from(&lit.value) {
                    Ok(i) if lit.suffix.is_none() => i,
                    _ => {
                        self.report(span, "invalid tuple index");
                        0
                    }
                };
                let whole = self.span_from(start);
                Ok(self.mk_expr(ExprKind::TupleField { base: Box::new(base), index, index_span: span }, whole))
            }
            _ => self.expected("a field name or tuple index after `.`"),
        }
    }

    /// `e!` followed by a type is an error-union `Bang`; otherwise an unwrap
    /// (A.7 rule 3).
    fn parse_bang(&mut self, lhs: Expr) -> PResult<Expr> {
        let start = lhs.span.lo;
        if can_begin_type(self.nth(1)) {
            let rhs = self.speculate(|p| {
                p.bump();
                p.parse_type()
            });
            if let Some(rhs) = rhs {
                let span = self.span_from(start);
                return Ok(self.mk_expr(ExprKind::Bang { lhs: Box::new(lhs), rhs: Box::new(rhs) }, span));
            }
        }
        self.bump();
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::ResUnwrap(Box::new(lhs)), span))
    }

    /// `a[i]`, `a[lo..hi]`, `a[lo..=hi]`, `a[lo..]`, `a[..hi]`, `a[..]`.
    fn parse_subscript(&mut self, base: Expr) -> PResult<Expr> {
        let start = base.span.lo;
        self.bump();
        let sub = self.with_restr(Restr { in_index: true, ..Restr::default() }, |p| p.parse_subscript_inner())?;
        self.expect(Punct::RBracket, "to close the subscript")?;
        let span = self.span_from(start);
        Ok(match sub {
            Subscript::Index(index) => {
                self.mk_expr(ExprKind::Index { base: Box::new(base), index: Box::new(index) }, span)
            }
            Subscript::Slice { lo, hi, inclusive } => {
                self.mk_expr(ExprKind::Slice { base: Box::new(base), lo, hi, inclusive }, span)
            }
        })
    }

    fn parse_subscript_inner(&mut self) -> PResult<Subscript> {
        if matches!(self.kind(), TokenKind::Punct(Punct::DotDot | Punct::DotDotEq)) {
            let inclusive = self.at(Punct::DotDotEq);
            let op_span = self.bump().span;
            let hi = if self.at(Punct::RBracket) { None } else { Some(Box::new(self.parse_expr()?)) };
            if inclusive && hi.is_none() {
                self.report(op_span, "an inclusive range needs an upper bound");
            }
            return Ok(Subscript::Slice { lo: None, hi, inclusive });
        }
        let first = self.parse_expr()?;
        match first {
            Expr { kind: ExprKind::Binary { op: op @ (BinOp::Range | BinOp::RangeIncl), lhs, rhs }, span, .. } => {
                let inclusive = op == BinOp::RangeIncl;
                let open = matches!(rhs.kind, ExprKind::Error) && rhs.span.is_empty();
                if open && inclusive {
                    self.report(span, "an inclusive range needs an upper bound");
                }
                Ok(Subscript::Slice { lo: Some(lhs), hi: if open { None } else { Some(rhs) }, inclusive })
            }
            other => Ok(Subscript::Index(other)),
        }
    }

    // ------------------------------------------------------------------
    // Calls
    // ------------------------------------------------------------------

    /// `( args )`; returns the arguments and whether a trailing comma was written.
    pub(super) fn parse_call_args(&mut self) -> PResult<(Vec<Arg>, bool)> {
        self.expect(Punct::LParen, "")?;
        self.unrestricted(|p| {
            let mut args = Vec::new();
            let mut trailing = false;
            while !p.at(Punct::RParen) {
                args.push(p.parse_arg()?);
                trailing = false;
                if p.eat(Punct::Comma) {
                    trailing = true;
                } else {
                    break;
                }
            }
            p.expect(Punct::RParen, "to close the argument list")?;
            Ok((args, trailing))
        })
    }

    fn parse_arg(&mut self) -> PResult<Arg> {
        let start = self.lo();
        if self.at(Punct::DotDot) {
            self.bump();
            let e = self.parse_expr()?;
            return Ok(Arg { kind: ArgKind::Spread(e), span: self.span_from(start) });
        }
        if self.nth_is_name(0) && self.nth_is(1, Punct::Colon) {
            if matches!(self.kind(), TokenKind::Underscore) {
                let span = self.span();
                self.diags.push(crate::diag::Diagnostic::error(span, "`_` cannot name an argument (§11.3)"));
            }
            let name = self.parse_name("an argument name")?;
            self.bump();
            let e = self.parse_expr()?;
            return Ok(Arg { kind: ArgKind::Named(name, e), span: self.span_from(start) });
        }
        let e = self.parse_expr()?;
        Ok(Arg { kind: ArgKind::Positional(e), span: self.span_from(start) })
    }

    // ------------------------------------------------------------------
    // Primaries
    // ------------------------------------------------------------------

    /// Fails if a construct that contains a `{` appears in a head (A.7 rule 4).
    pub(super) fn deny_in_head(&mut self, what: &str) -> PResult<()> {
        if self.restr.no_brace {
            let span = self.span();
            return self.fail(span, format!("{} must be parenthesized in this position", what));
        }
        Ok(())
    }

    pub(super) fn parse_primary(&mut self) -> PResult<Expr> {
        let tok = self.tok();
        let span = tok.span;
        match &tok.kind {
            TokenKind::Int(lit) => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Int(lit.clone()), span))
            }
            TokenKind::Float(lit) => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Float(lit.clone()), span))
            }
            TokenKind::Str(lit) => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Str(lit.clone()), span))
            }
            TokenKind::Keyword(Keyword::True) => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Bool(true), span))
            }
            TokenKind::Keyword(Keyword::False) => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Bool(false), span))
            }
            TokenKind::Keyword(Keyword::Null) => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Null, span))
            }
            TokenKind::Underscore => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Underscore, span))
            }
            TokenKind::Punct(Punct::Uninit) => {
                self.bump();
                Ok(self.mk_expr(ExprKind::Uninit, span))
            }
            // A labeled `with` or `for` used as a value (A.5).
            TokenKind::Ident(_)
                if self.nth_is(1, Punct::Colon)
                    && matches!(self.nth(2), TokenKind::Keyword(Keyword::With | Keyword::For)) =>
            {
                let label = self.parse_ident("a label")?;
                self.bump();
                if self.nth_is_kw(0, Keyword::With) {
                    self.deny_in_head("a `with` expression")?;
                    self.parse_with(Some(label))
                } else {
                    self.deny_in_head("a `for` expression")?;
                    self.parse_for(Some(label))
                }
            }
            TokenKind::Keyword(_) if self.at_reported_kw_name() => {
                let id = self.bump_reported_kw_name();
                Ok(self.mk_expr(ExprKind::Ident(id), span))
            }
            TokenKind::Ident(name) => {
                if *name == self.words.cont && self.nth_is(1, Punct::LParen) {
                    return self.parse_cont_type();
                }
                self.bump();
                Ok(self.mk_expr(ExprKind::Ident(Ident { name: *name, span }), span))
            }
            TokenKind::Punct(Punct::LParen) => self.parse_paren(Mode::Expr),
            TokenKind::Punct(Punct::Dot) => {
                self.bump();
                let name = self.parse_ident("a variant name after `.`")?;
                let span = self.span_from(span.lo);
                Ok(self.mk_expr(ExprKind::DotIdent(name), span))
            }
            TokenKind::Punct(Punct::Question | Punct::Caret | Punct::LBracket) => self.parse_type_prefix(Mode::Expr),
            TokenKind::Keyword(Keyword::Fn) => self.parse_fn_expr(),
            TokenKind::Keyword(Keyword::Struct) => self.parse_struct_lit(),
            TokenKind::Keyword(Keyword::Union) => self.parse_union_lit(),
            TokenKind::Keyword(Keyword::Effect) => self.parse_effect_lit(),
            TokenKind::Keyword(Keyword::Handler) => self.parse_handler_lit(),
            TokenKind::Keyword(Keyword::Import) => self.parse_import(),
            TokenKind::Keyword(Keyword::Foreign) => self.parse_foreign(),
            TokenKind::Keyword(Keyword::If) => {
                self.deny_in_head("an `if` expression")?;
                self.parse_if()
            }
            TokenKind::Keyword(Keyword::Match) => {
                self.deny_in_head("a `match` expression")?;
                self.parse_match()
            }
            TokenKind::Keyword(Keyword::With) => {
                self.deny_in_head("a `with` expression")?;
                self.parse_with(None)
            }
            TokenKind::Keyword(Keyword::For) => {
                self.deny_in_head("a `for` expression")?;
                self.parse_for(None)
            }
            TokenKind::Keyword(Keyword::Return) => self.parse_return(),
            TokenKind::Keyword(Keyword::Break) => self.parse_break(),
            TokenKind::Keyword(Keyword::Continue) => self.parse_continue(),
            TokenKind::Keyword(Keyword::Resume) => self.parse_resume(),
            TokenKind::Intrinsic(_) => self.parse_intrinsic(),
            TokenKind::Attribute(_) => self.parse_attributed(Mode::Expr),
            TokenKind::Keyword(Keyword::Not) => {
                self.fail(span, "`not` cannot be an operand here; parenthesize it: `(not x)`")
            }
            TokenKind::Keyword(Keyword::Try) => self.fail(span, "`try` cannot be an operand here; parenthesize it"),
            _ => self.expected("an expression"),
        }
    }

    /// `(` … `)` as an expression or a type: parenthesized, or a tuple.
    pub(super) fn parse_paren(&mut self, mode: Mode) -> PResult<Expr> {
        let start = self.lo();
        self.expect(Punct::LParen, "")?;
        self.unrestricted(|p| p.parse_paren_inner(mode, start))
    }

    fn parse_paren_inner(&mut self, mode: Mode, start: u32) -> PResult<Expr> {
        let mut elems: Vec<TupleElem> = Vec::new();
        let mut comma = false;
        let mut named: Option<bool> = None;
        while !self.at(Punct::RParen) {
            let elem_start = self.lo();
            let name = if self.nth_is_name(0) && self.nth_is(1, Punct::Colon) {
                let n = self.parse_name("a name")?;
                self.bump();
                Some(n)
            } else {
                None
            };
            match named {
                None => named = Some(name.is_some()),
                Some(n) if n != name.is_some() => {
                    let span = Span::new(self.file, elem_start, self.span().hi);
                    self.report(span, "tuple elements must be either all named or all unnamed");
                }
                Some(_) => {}
            }
            let value = match mode {
                Mode::Expr => self.parse_expr()?,
                Mode::Type => self.parse_type()?,
            };
            elems.push(TupleElem { name, value });
            comma = false;
            if self.eat(Punct::Comma) {
                comma = true;
            } else {
                break;
            }
        }
        self.expect(Punct::RParen, "to close the parenthesis")?;
        let span = self.span_from(start);
        if elems.len() == 1 && !comma && elems[0].name.is_none() {
            let inner = elems.pop().map(|e| e.value);
            if let Some(inner) = inner {
                return Ok(self.mk_expr(ExprKind::Paren(Box::new(inner)), span));
            }
        }
        Ok(self.mk_expr(ExprKind::Tuple(elems), span))
    }

    fn parse_import(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        match self.kind() {
            TokenKind::Str(lit) => {
                self.bump();
                let span = self.span_from(start);
                Ok(self.mk_expr(ExprKind::Import(lit.clone()), span))
            }
            _ => self.expected("a string literal naming the module after `import`"),
        }
    }

    /// `@name` or `@name(args)`.
    pub(super) fn parse_intrinsic(&mut self) -> PResult<Expr> {
        let tok = self.bump();
        let TokenKind::Intrinsic(sym) = &tok.kind else { return self.expected("an intrinsic") };
        let name_span = tok.span;
        let name = Ident { name: *sym, span: name_span };
        let args = if self.at(Punct::LParen) { Some(self.parse_call_args()?.0) } else { None };
        let span = self.span_from(name_span.lo);
        Ok(self.mk_expr(ExprKind::Intrinsic { name, args }, span))
    }

    // ------------------------------------------------------------------
    // Jumps
    // ------------------------------------------------------------------

    fn parse_return(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        let value = if can_begin_expr(self.kind()) { Some(Box::new(self.parse_expr()?)) } else { None };
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Return(value), span))
    }

    /// If the current token is an identifier naming an enclosing label, consumes it.
    fn eat_label(&mut self) -> Option<Ident> {
        if let TokenKind::Ident(sym) = self.kind()
            && self.labels.contains(sym)
        {
            let span = self.span();
            self.bump();
            return Some(Ident { name: *sym, span });
        }
        None
    }

    fn parse_break(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        let label = self.eat_label();
        // `break L v` with `L` not in scope: an identifier followed by something that
        // cannot continue an expression.
        if label.is_none()
            && let TokenKind::Ident(sym) = self.kind()
            && matches!(
                self.nth(1),
                TokenKind::Ident(_)
                    | TokenKind::Int(_)
                    | TokenKind::Float(_)
                    | TokenKind::Str(_)
                    | TokenKind::Keyword(Keyword::Null | Keyword::True | Keyword::False)
            )
        {
            let span = self.span();
            let msg = format!("`{}` is not a label of an enclosing `while`, `for` or `with`", sym);
            return self.fail(span, msg);
        }
        let value = if can_begin_expr(self.kind()) { Some(Box::new(self.parse_expr()?)) } else { None };
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Break { label, value }, span))
    }

    fn parse_continue(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        let label = self.eat_label();
        if label.is_none()
            && let TokenKind::Ident(sym) = self.kind()
        {
            let span = self.span();
            let msg = format!("`{}` is not a label of an enclosing `while`, `for` or `with`", sym);
            return self.fail(span, msg);
        }
        let span = self.span_from(start);
        Ok(self.mk_expr(ExprKind::Continue { label }, span))
    }

    /// `resume k(v)` / `resume k()` (A.7 rule 10).
    fn parse_resume(&mut self) -> PResult<Expr> {
        let start = self.lo();
        self.bump();
        let operand = self.parse_postfix(true)?;
        let span = self.span_from(start);
        let Expr { kind, span: operand_span, .. } = operand;
        if let ExprKind::Call { callee, mut args, .. } = kind
            && args.len() <= 1
            && args.iter().all(|a| matches!(a.kind, ArgKind::Positional(_)))
        {
            let value = args.pop().and_then(|a| match a.kind {
                ArgKind::Positional(e) => Some(Box::new(e)),
                _ => None,
            });
            return Ok(self.mk_expr(ExprKind::Resume { cont: callee, value }, span));
        }
        self.fail(operand_span, "`resume` expects a continuation call with at most one value: `resume k(value)`")
    }

    /// A block used as an expression (match arm bodies, handler clause bodies,
    /// `else e => { … }`).
    pub(super) fn parse_block_expr(&mut self) -> PResult<Expr> {
        self.deny_in_head("a block")?;
        let block = self.parse_block()?;
        let span = block.span;
        Ok(self.mk_expr(ExprKind::Block(Box::new(block)), span))
    }
}
