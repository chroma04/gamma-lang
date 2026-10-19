//! The rules that need only the syntax tree, and the facts about the tree that the other
//! rules need: what kind of binding declares each name, the names written in `when`
//! branches, and the `#allow` attributes.

use super::{Site, lint};
use crate::diag::{Diagnostic, Edit};
use crate::source::{SourceMap, Span};
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind, BinOp, CondPart, ExprKind, IfElse, PatternKind, StmtKind, UnOp};
use std::collections::{HashMap, HashSet};

/// What the walk of one file found.
#[derive(Default)]
pub struct FileFacts {
    pub diags: Vec<Diagnostic>,
    /// How each local name is declared, by the span of the name.
    pub sites: HashMap<Span, Site>,
    /// The spans of `when` items and statements (§3.6, §8.9), whose untaken branches are
    /// not checked.
    pub whens: Vec<Span>,
    /// Top-level declarations with `#allow`: the declaration and the rules it allows.
    pub allows: Vec<(Span, Vec<String>)>,
}

pub struct Walk<'a> {
    sm: &'a SourceMap,
    pub facts: FileFacts,
    /// The labels in scope (of enclosing loops and `with`s) and whether they are used.
    labels: Vec<(Symbol, Span, bool)>,
}

impl<'a> Walk<'a> {
    pub fn new(sm: &'a SourceMap) -> Walk<'a> {
        Walk { sm, facts: FileFacts::default(), labels: vec![] }
    }

    fn text(&self, span: Span) -> &'a str {
        self.sm.text(span)
    }

    fn push(&mut self, d: Diagnostic) {
        self.facts.diags.push(d);
    }

    pub fn file(&mut self, file: &ast::File) {
        for item in &file.items {
            self.item(item);
        }
    }

    fn item(&mut self, item: &ast::Item) {
        let allowed: Vec<String> = item
            .attrs
            .iter()
            .filter(|a| a.name.name.as_str() == "allow")
            .flat_map(|a| a.args.iter().flatten())
            .filter_map(|e| match &e.kind {
                ExprKind::Str(s) => Some(String::from_utf8_lossy(&s.bytes).into_owned()),
                _ => None,
            })
            .collect();
        if !allowed.is_empty() {
            self.facts.allows.push((item.span, allowed));
        }
        match &item.kind {
            ast::ItemKind::Binding(b) => {
                let foreign = matches!(b.init.kind, ExprKind::Foreign(_));
                if let Some(name) = b.names.first()
                    && !foreign
                {
                    self.top_level_name(*name, b);
                }
                if let Some(t) = &b.ty {
                    self.expr(t);
                }
                self.expr(&b.init);
            }
            ast::ItemKind::When(w) => {
                self.facts.whens.push(w.span);
                self.expr(&w.cond);
                for i in w.then.iter().chain(w.else_.iter().flatten()) {
                    self.item(i);
                }
            }
            ast::ItemKind::Handler(h) => {
                for c in &h.clauses {
                    self.expr(&c.body);
                }
            }
            ast::ItemKind::Test { body, .. } => self.block(body),
            ast::ItemKind::Intrinsic(e) => self.expr(e),
        }
    }

    // --- naming ---

    fn top_level_name(&mut self, name: ast::Ident, b: &ast::Binding) {
        let mut inner = &b.init.kind;
        while let ExprKind::Attributed { inner: i, .. } = inner {
            inner = &i.kind;
        }
        match inner {
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Effect(_) => {
                self.check_name(name, Case::Upper, "type")
            }
            ExprKind::Fn(_) => self.check_name(name, Case::Snake, "function"),
            ExprKind::Import(_) => self.check_name(name, Case::Snake, "import"),
            _ => {}
        }
        // The members of a `#c_union` mirror a C union's (§5.11.1).
        let c_union = match &b.init.kind {
            ExprKind::Attributed { attrs, .. } => attrs.iter().any(|a| a.name.name.as_str() == "c_union"),
            _ => false,
        };
        match inner {
            ExprKind::Struct(s) => self.fields(&s.members),
            ExprKind::Union(u) if !c_union => self.variants(&u.members),
            ExprKind::Effect(e) => {
                for op in &e.ops {
                    self.check_name(op.name, Case::Snake, "operation");
                }
            }
            _ => {}
        }
    }

    fn fields(&mut self, members: &[ast::StructMember]) {
        for m in members {
            match m {
                ast::StructMember::Field(f) => {
                    for n in &f.names {
                        self.check_name(*n, Case::Snake, "field");
                    }
                }
                ast::StructMember::When { then, else_, .. } => {
                    self.fields(then);
                    self.fields(else_.as_deref().unwrap_or(&[]));
                }
            }
        }
    }

    fn variants(&mut self, members: &[ast::UnionMember]) {
        for m in members {
            match m {
                ast::UnionMember::Variant(v) => self.check_name(v.name, Case::Upper, "variant"),
                ast::UnionMember::When { then, else_, .. } => {
                    self.variants(then);
                    self.variants(else_.as_deref().unwrap_or(&[]));
                }
            }
        }
    }

    fn check_name(&mut self, name: ast::Ident, case: Case, what: &str) {
        let s = name.name.as_str().trim_start_matches('_');
        if s.is_empty() || case.accepts(s) {
            return;
        }
        let want = match case {
            Case::Upper => "UpperCamelCase",
            Case::Snake => "snake_case",
            Case::Any => "snake_case, SCREAMING_SNAKE_CASE or UpperCamelCase",
        };
        let mut d = lint(name.span, "naming", format!("the {} `{}` is not written in {}", what, name.name, want));
        if let Some(better) = case.convert(s) {
            d = d.with_note(format!("write `{}`", better));
        }
        self.push(d);
    }

    // --- bindings ---

    fn binding_stmt(&mut self, b: &ast::Binding) {
        if let [name] = b.names.as_slice() {
            if let ExprKind::Fn(_) = b.init.kind {
                self.check_name(*name, Case::Snake, "function");
            } else {
                // A local `::` binding may be a constant or a type alias.
                let case = if b.kind == ast::BindingKind::Const { Case::Any } else { Case::Snake };
                self.check_name(*name, case, "local");
            }
            // The text from the name to the end of `:=` or `::`, when there is no type.
            let from = b.ty.as_ref().map_or(name.span.hi, |t| t.span.hi);
            let op = self.binding_op(Span::new(name.span.file, from, b.init.span.lo));
            let site = Site::Single {
                var: b.kind == ast::BindingKind::Var,
                typed: b.ty.is_some(),
                op,
                keep_mutable: is_literal(&b.init)
                    || is_constant_call(&b.init)
                    || self.text(b.init.span).contains('@')
                    || self.text(b.init.span).starts_with(['[', '^', '?']),
                fn_lit: matches!(b.init.kind, ExprKind::Fn(_)),
                uninit: matches!(b.init.kind, ExprKind::Uninit),
            };
            self.facts.sites.insert(name.span, site);
        } else {
            for n in &b.names {
                self.check_name(*n, Case::Snake, "local");
                self.facts.sites.insert(n.span, Site::Replace(n.span, "_"));
            }
        }
        if let Some(t) = &b.ty {
            self.expr(t);
        }
        self.paren_at("the initializer", &b.init, false);
        self.expr(&b.init);
    }

    /// The span of the binding operator (`:=`, `::`, `=` or `:`) in `between`.
    fn binding_op(&self, between: Span) -> Option<Span> {
        let text = self.text(between);
        let off = text.find(|c: char| !c.is_whitespace())?;
        let len = if text[off..].starts_with(":=") || text[off..].starts_with("::") { 2 } else { 1 };
        let lo = between.lo + off as u32;
        Some(Span::new(between.file, lo, lo + len))
    }

    fn pattern(&mut self, p: &ast::Pattern, ctx: PatSite) {
        match &p.kind {
            PatternKind::Bind(id) => {
                self.check_name(*id, Case::Snake, "local");
                let site = match ctx {
                    PatSite::Top => Site::Pattern { fix: None },
                    PatSite::Nested => Site::Pattern { fix: Some((id.span, "_")) },
                    PatSite::Shorthand => Site::Pattern { fix: Some((id.span.shrink_to_hi(), ": _")) },
                };
                self.facts.sites.insert(id.span, site);
            }
            PatternKind::Err(inner) => self.pattern(inner, PatSite::Nested),
            PatternKind::Path { path, args } => {
                // `S(x)` names the field `x` (§9.1); `.V(x)` and `T.V(x)` bind the payload.
                let structish = matches!(path.kind, ExprKind::Ident(_));
                for a in args.iter().flatten() {
                    let ctx = match (&a.name, structish) {
                        (None, true) => PatSite::Shorthand,
                        _ => PatSite::Nested,
                    };
                    self.pattern(&a.pat, ctx);
                }
            }
            PatternKind::Tuple(args) => {
                for a in args {
                    self.pattern(&a.pat, PatSite::Nested);
                }
            }
            PatternKind::Lit(e) => self.expr(e),
            PatternKind::Range { lo, hi, .. } => {
                self.expr(lo);
                self.expr(hi);
            }
            PatternKind::Wildcard | PatternKind::Null => {}
        }
    }

    /// The error capture `else e => …`; `removable` if `else …` without it means the same.
    fn capture(&mut self, name: ast::Ident, removable: bool) {
        self.check_name(name, Case::Snake, "local");
        let src = &self.sm.file(name.span.file).src[name.span.hi as usize..];
        let fix = src.trim_start().strip_prefix("=>").filter(|_| removable).map(|rest| {
            let end = src.len() - rest.trim_start_matches([' ', '\t']).len();
            (Span::new(name.span.file, name.span.lo, name.span.hi + end as u32), "")
        });
        self.facts.sites.insert(name.span, Site::Pattern { fix });
    }

    // --- statements ---

    fn block(&mut self, b: &ast::Block) {
        for s in &b.stmts {
            self.stmt(s);
        }
        if let Some(last) = b.stmts.last()
            && let StmtKind::Defer { is_err, .. } = &last.kind
        {
            let msg = if *is_err {
                "this `errdefer` never runs: it is the last statement of its block, so nothing after it can leave the block with an error"
            } else {
                "this `defer` runs at once: it is the last statement of its block"
            };
            self.push(lint(last.span, "useless-defer", msg));
        }
    }

    fn stmt(&mut self, s: &ast::Stmt) {
        match &s.kind {
            StmtKind::Binding { binding, .. } => self.binding_stmt(binding),
            StmtKind::Assign { places, op, value } => {
                for p in places {
                    self.expr(p);
                }
                if *op == ast::AssignOp::Assign {
                    self.paren_at("the assigned value", value, false);
                }
                self.expr(value);
            }
            StmtKind::Expr(e) => self.expr(e),
            StmtKind::Defer { body, .. } => self.stmt(body),
            StmtKind::When(w) => self.when_stmt(w),
            StmtKind::While { label, cond, body } => {
                self.cond(cond, true);
                for part in &cond.parts {
                    if let CondPart::Expr(e) = part
                        && let ExprKind::Bool(false) = e.kind
                    {
                        self.push(lint(e.span, "constant-condition", "this loop never runs: its condition is `false`"));
                    }
                }
                self.labeled(*label, |w| w.block(body));
            }
            StmtKind::Cancel(e) => self.expr(e),
            StmtKind::LetElse { pat, value, fallback } => {
                self.pattern(pat, PatSite::Top);
                self.expr(value);
                self.expr(fallback);
            }
            StmtKind::Block(b) => self.block(b),
        }
    }

    fn when_stmt(&mut self, w: &ast::WhenStmt) {
        self.facts.whens.push(w.span);
        self.expr(&w.cond);
        self.block(&w.then);
        match &w.else_ {
            Some(ast::WhenElse::Block(b)) => self.block(b),
            Some(ast::WhenElse::When(w)) => self.when_stmt(w),
            None => {}
        }
    }

    fn cond(&mut self, c: &ast::Cond, head: bool) {
        for part in &c.parts {
            match part {
                CondPart::Bind { pat, value } => {
                    self.pattern(pat, PatSite::Top);
                    self.expr(value);
                }
                CondPart::Expr(e) => {
                    if c.parts.len() == 1 {
                        self.paren_at("the condition", e, head);
                    }
                    self.expr(e);
                }
            }
        }
    }

    /// Runs `f` with `label` in scope, and reports it if nothing breaks to or continues it.
    fn labeled(&mut self, label: Option<ast::Ident>, f: impl FnOnce(&mut Self)) {
        let Some(l) = label else { return f(self) };
        self.labels.push((l.name, l.span, false));
        f(self);
        let (_, _, used) = self.labels.pop().unwrap();
        if !used {
            let file = l.span.file;
            // `name:` and the blanks after it.
            let rest = &self.sm.file(file).src[l.span.hi as usize..];
            let colon = rest.find(':').unwrap_or(0);
            let after = rest[colon + 1..].len() - rest[colon + 1..].trim_start_matches([' ', '\t']).len();
            let end = l.span.hi + (colon + 1 + after) as u32;
            let d = lint(l.span, "unused-label", format!("the label `{}` is never used", l.name)).with_fix(
                "remove the label",
                Span::new(file, l.span.lo, end),
                "",
            );
            self.push(d);
        }
    }

    fn use_label(&mut self, label: &Option<ast::Ident>) {
        if let Some(l) = label
            && let Some(entry) = self.labels.iter_mut().rev().find(|(n, _, _)| *n == l.name)
        {
            entry.2 = true;
        }
    }

    // --- expressions ---

    fn expr(&mut self, e: &ast::Expr) {
        match &e.kind {
            ExprKind::Int(_)
            | ExprKind::Float(_)
            | ExprKind::Str(_)
            | ExprKind::Bool(_)
            | ExprKind::Null
            | ExprKind::Uninit
            | ExprKind::Ident(_)
            | ExprKind::Underscore
            | ExprKind::DotIdent(_)
            | ExprKind::Import(_)
            | ExprKind::Error => {}
            ExprKind::Paren(x)
            | ExprKind::OptUnwrap(x)
            | ExprKind::ResUnwrap(x)
            | ExprKind::Deref(x)
            | ExprKind::OptionalType(x)
            | ExprKind::ContType(x)
            | ExprKind::PtrType { inner: x, .. }
            | ExprKind::SliceType { inner: x, .. }
            | ExprKind::ManyPtrType { inner: x, .. } => self.expr(x),
            ExprKind::Tuple(elems) => {
                for el in elems {
                    self.expr(&el.value);
                }
            }
            ExprKind::Unary { op, operand } => {
                if *op == UnOp::Not {
                    let mut inner = &**operand;
                    while let ExprKind::Paren(x) = &inner.kind {
                        inner = x;
                    }
                    if let ExprKind::Unary { op: UnOp::Not, operand: x } = &inner.kind {
                        let text = self.text(x.span).to_string();
                        let d = lint(e.span, "double-negation", "`not not x` is `x`").with_fix(
                            "remove both `not`s",
                            e.span,
                            paren_unless_simple(x, &text, false),
                        );
                        self.push(d);
                    }
                }
                self.expr(operand);
            }
            ExprKind::Binary { lhs, rhs, .. } | ExprKind::Bang { lhs, rhs } => {
                self.expr(lhs);
                self.expr(rhs);
            }
            ExprKind::ErrorUnionType { err, ok } => {
                self.expr(err);
                self.expr(ok);
            }
            ExprKind::Else { lhs, capture, rhs } => {
                if let Some(c) = capture {
                    self.capture(*c, true);
                }
                self.expr(lhs);
                self.expr(rhs);
            }
            ExprKind::Call { callee, args, .. } => {
                self.expr(callee);
                self.args(args);
            }
            ExprKind::Intrinsic { args, .. } => self.args(args.as_deref().unwrap_or(&[])),
            ExprKind::Index { base, index } => {
                self.expr(base);
                self.expr(index);
            }
            ExprKind::Slice { base, lo, hi, .. } => {
                self.expr(base);
                for x in lo.iter().chain(hi.iter()) {
                    self.expr(x);
                }
            }
            ExprKind::Field { base, .. } | ExprKind::TupleField { base, .. } => self.expr(base),
            ExprKind::ArrayType { len, elem } => {
                if let Some(l) = len {
                    self.expr(l);
                }
                self.expr(elem);
            }
            ExprKind::FnType(sig) => self.sig(sig, false),
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Effect(_) | ExprKind::Foreign(_) => {}
            ExprKind::Attributed { inner, .. } => self.expr(inner),
            ExprKind::Fn(f) => self.fn_lit(f),
            ExprKind::Handler(h) => {
                for c in &h.clauses {
                    self.expr(&c.body);
                }
            }
            ExprKind::If(i) => self.if_expr(e, i),
            ExprKind::Match(m) => {
                self.paren_at("the matched value", &m.scrutinee, true);
                self.expr(&m.scrutinee);
                for arm in &m.arms {
                    for p in &arm.pats {
                        self.pattern(p, PatSite::Nested);
                    }
                    if let Some(g) = &arm.guard {
                        self.expr(g);
                    }
                    self.expr(&arm.body);
                }
            }
            ExprKind::With(w) => {
                for h in &w.handlers {
                    self.expr(h);
                }
                if let Some(on) = &w.on {
                    self.expr(on);
                }
                self.labeled(w.label, |s| s.block(&w.body));
            }
            ExprKind::For(f) => {
                for p in &f.pats {
                    self.pattern(p, PatSite::Nested);
                }
                self.paren_at("the iterated value", &f.iter, true);
                self.expr(&f.iter);
                self.labeled(f.label, |s| s.block(&f.body));
            }
            ExprKind::Block(b) => self.block(b),
            ExprKind::Return(v) => {
                if let Some(v) = v {
                    self.paren_at("the returned value", v, false);
                    self.expr(v);
                }
            }
            ExprKind::Break { label, value } => {
                self.use_label(label);
                if let Some(v) = value {
                    self.expr(v);
                }
            }
            ExprKind::Continue { label } => self.use_label(label),
            ExprKind::Resume { cont, value } => {
                self.expr(cont);
                if let Some(v) = value {
                    self.expr(v);
                }
            }
        }
    }

    fn args(&mut self, args: &[ast::Arg]) {
        for a in args {
            match &a.kind {
                ArgKind::Positional(x) | ArgKind::Named(_, x) | ArgKind::Spread(x) => self.expr(x),
            }
        }
    }

    fn sig(&mut self, sig: &ast::FnSig, literal: bool) {
        for p in &sig.params {
            if literal {
                // Type and compile-time value parameters are named like types and constants.
                let ty_param = p.comptime
                    || p.ty.as_ref().is_some_and(
                        |t| matches!(&t.kind, ExprKind::Ident(i) if matches!(i.name.as_str(), "type" | "effects")),
                    );
                if !ty_param {
                    for n in &p.names {
                        self.check_name(*n, Case::Snake, "parameter");
                    }
                }
            }
            if let Some(t) = &p.ty {
                self.expr(t);
            }
            if let Some(d) = &p.default {
                self.expr(d);
            }
        }
        if let Some(r) = &sig.ret {
            self.expr(r);
        }
    }

    fn fn_lit(&mut self, f: &ast::FnLit) {
        self.sig(&f.sig, true);
        // Labels are not visible inside function literals (§A.7 rule 11).
        let labels = std::mem::take(&mut self.labels);
        match &f.body {
            ast::FnBody::Block(b) => self.block(b),
            ast::FnBody::Expr(e) => self.expr(e),
        }
        self.labels = labels;
    }

    fn if_expr(&mut self, e: &ast::Expr, i: &ast::IfExpr) {
        for (c, b) in &i.branches {
            self.cond(c, true);
            for part in &c.parts {
                if let CondPart::Expr(x) = part
                    && let ExprKind::Bool(v) = x.kind
                {
                    let msg = if v {
                        "this condition is always true"
                    } else {
                        "this condition is always false: the branch never runs"
                    };
                    self.push(
                        lint(x.span, "constant-condition", msg).with_note(
                            "for a condition known at compile time, `when` leaves the other branch unchecked",
                        ),
                    );
                }
            }
            self.block(b);
        }
        match &i.else_ {
            Some(IfElse::Block(b)) => self.block(b),
            Some(IfElse::Capture { name, body }) => {
                self.capture(*name, matches!(body.kind, ExprKind::Block(_)));
                self.expr(body);
            }
            None => {}
        }
        self.needless_bool(e, i);
        self.empty_branches(i);
        self.collapsible_else(i);
        self.identical_branches(i);
    }

    /// `if c { true } else { false }` is `c`.
    fn needless_bool(&mut self, e: &ast::Expr, i: &ast::IfExpr) {
        let ([(c, then)], Some(IfElse::Block(els))) = (i.branches.as_slice(), &i.else_) else { return };
        let [CondPart::Expr(c)] = c.parts.as_slice() else { return };
        enum Shape {
            Value(bool),
            Return(bool),
        }
        let shape = |b: &ast::Block| -> Option<Shape> {
            let [s] = b.stmts.as_slice() else { return None };
            let StmtKind::Expr(x) = &s.kind else { return None };
            match &x.kind {
                ExprKind::Bool(v) => Some(Shape::Value(*v)),
                ExprKind::Return(Some(r)) => match r.kind {
                    ExprKind::Bool(v) => Some(Shape::Return(v)),
                    _ => None,
                },
                _ => None,
            }
        };
        let (prefix, negate) = match (shape(then), shape(els)) {
            (Some(Shape::Value(a)), Some(Shape::Value(b))) if a != b => ("", !a),
            (Some(Shape::Return(a)), Some(Shape::Return(b))) if a != b => ("return ", !a),
            _ => return,
        };
        if self.has_comment(i.branches[0].1.span) || self.has_comment(els.span) {
            return;
        }
        let text = self.text(c.span);
        let value = if negate { self.negation(c) } else { text.to_string() };
        let msg = if negate {
            "this `if` gives the negation of its condition"
        } else {
            "this `if` gives the value of its condition"
        };
        let d = lint(e.span, "needless-bool", msg).with_fix(
            format!("replace the `if` with `{}{}`", prefix, value),
            e.span,
            format!("{}{}", prefix, value),
        );
        self.push(d);
    }

    /// `not c`, written with `is not` or `not in` when `c` is an `is` or `in` test.
    fn negation(&self, c: &ast::Expr) -> String {
        if let ExprKind::Binary { op, lhs, rhs } = &c.kind {
            let flipped = match op {
                BinOp::Is => Some(BinOp::IsNot),
                BinOp::IsNot => Some(BinOp::Is),
                BinOp::In => Some(BinOp::NotIn),
                BinOp::NotIn => Some(BinOp::In),
                _ => None,
            };
            if let Some(f) = flipped {
                return format!("{} {} {}", self.text(lhs.span), f.as_str(), self.text(rhs.span));
            }
        }
        format!("not {}", paren_unless_simple(c, self.text(c.span), true))
    }

    fn empty_branches(&mut self, i: &ast::IfExpr) {
        let empty = |w: &Self, b: &ast::Block| b.stmts.is_empty() && !w.has_comment(b.span);
        if let ([(_, then)], None) = (i.branches.as_slice(), &i.else_)
            && empty(self, then)
        {
            self.push(lint(then.span, "empty-branch", "this `if` has an empty body and no `else`"));
        }
        if let Some(IfElse::Block(els)) = &i.else_
            && empty(self, els)
        {
            let prev = i.branches.last().unwrap().1.span;
            let span = Span::new(els.span.file, prev.hi, els.span.hi);
            let d = lint(els.span, "empty-branch", "this `else` is empty").with_fix("remove the `else`", span, "");
            self.push(d);
        }
    }

    /// `else { if c { … } }` is `else if c { … }`.
    fn collapsible_else(&mut self, i: &ast::IfExpr) {
        let Some(IfElse::Block(els)) = &i.else_ else { return };
        let [s] = els.stmts.as_slice() else { return };
        let StmtKind::Expr(inner) = &s.kind else { return };
        let ExprKind::If(_) = inner.kind else { return };
        let mut d = lint(els.span, "collapsible-else-if", "this `else` holds only an `if`: write `else if`");
        let file = els.span.file;
        let before = self.text(Span::new(file, els.span.lo, inner.span.lo));
        let after = self.text(Span::new(file, inner.span.hi, els.span.hi));
        let blank = |t: &str| t.chars().all(|c| c.is_whitespace() || c == '{' || c == '}');
        if blank(before) && blank(after) {
            let text = dedent(self.text(inner.span), self.indent_of(inner.span), self.indent_of(els.span));
            d = d.with_fix("merge the `else` and the `if`", els.span, text);
        }
        self.push(d);
    }

    fn identical_branches(&mut self, i: &ast::IfExpr) {
        let Some(IfElse::Block(els)) = &i.else_ else { return };
        if els.stmts.is_empty() {
            return;
        }
        let norm = |s: &str| s.split_whitespace().collect::<Vec<_>>().join(" ");
        let first = norm(self.text(els.span));
        if i.branches.iter().all(|(_, b)| norm(self.text(b.span)) == first) {
            let span = i.branches[0].1.span;
            self.push(
                lint(span, "identical-branches", "every branch of this `if` does the same")
                    .with_label(els.span, "the same as this `else`"),
            );
        }
    }

    /// Reports parentheses around the whole of `e` where none are needed. `head`: `e`
    /// is in the head of `if`, `while`, `for` or `match`, where a construct with a `{`
    /// must stay parenthesized (§A.7 rule 4).
    fn paren_at(&mut self, what: &str, e: &ast::Expr, head: bool) {
        let ExprKind::Paren(inner) = &e.kind else { return };
        // Inside parentheses, newlines do not end the expression (§2.8).
        if self.text(e.span).contains('\n') || (head && self.text(e.span).contains('{')) {
            return;
        }
        let file = e.span.file;
        let d = lint(e.span, "unnecessary-parens", format!("unnecessary parentheses around {}", what)).with_fix_edits(
            "remove the parentheses",
            vec![
                Edit { span: Span::new(file, e.span.lo, inner.span.lo), text: String::new() },
                Edit { span: Span::new(file, inner.span.hi, e.span.hi), text: String::new() },
            ],
        );
        self.push(d);
    }

    /// Whether the text at `span` holds a comment.
    fn has_comment(&self, span: Span) -> bool {
        self.text(span).contains("//")
    }

    /// The indentation of the line `span` starts on.
    fn indent_of(&self, span: Span) -> usize {
        let src = &self.sm.file(span.file).src;
        let start = src[..span.lo as usize].rfind('\n').map_or(0, |i| i + 1);
        src[start..].len() - src[start..].trim_start_matches([' ', '\t']).len()
    }
}

/// Where a pattern is written, which decides how its unused bindings are fixed.
#[derive(Clone, Copy)]
enum PatSite {
    /// The whole pattern of a binding condition or `let … else`: `_` would change what
    /// it matches.
    Top,
    /// A sub-pattern, a match arm or a `for` pattern: `_` discards.
    Nested,
    /// A positional name in a struct pattern, which names a field (`S(x)` is `S(x: x)`).
    Shorthand,
}

#[derive(Clone, Copy)]
enum Case {
    Upper,
    Snake,
    /// snake_case, SCREAMING_SNAKE_CASE or UpperCamelCase.
    Any,
}

impl Case {
    fn accepts(self, s: &str) -> bool {
        let snake = !s.chars().any(|c| c.is_ascii_uppercase());
        match self {
            Case::Upper => s.starts_with(|c: char| c.is_ascii_uppercase()) && !s.contains('_'),
            Case::Snake => snake,
            Case::Any => snake || !s.chars().any(|c| c.is_ascii_lowercase()) || Case::Upper.accepts(s),
        }
    }

    /// The name in this case, when the conversion is unambiguous.
    fn convert(self, s: &str) -> Option<String> {
        match self {
            Case::Upper => {
                let out: String = s
                    .split('_')
                    .filter(|w| !w.is_empty())
                    .map(|w| {
                        let mut c = w.chars();
                        let first = c.next().unwrap().to_ascii_uppercase();
                        std::iter::once(first).chain(c).collect::<String>()
                    })
                    .collect();
                Some(out)
            }
            Case::Snake | Case::Any => {
                let mut out = String::new();
                let chars: Vec<char> = s.chars().collect();
                for (i, &c) in chars.iter().enumerate() {
                    if c.is_ascii_uppercase() {
                        let prev_lower = i > 0 && (chars[i - 1].is_ascii_lowercase() || chars[i - 1].is_ascii_digit());
                        let next_lower = chars.get(i + 1).is_some_and(|n| n.is_ascii_lowercase());
                        if i > 0 && !out.ends_with('_') && (prev_lower || next_lower) {
                            out.push('_');
                        }
                        out.push(c.to_ascii_lowercase());
                    } else {
                        out.push(c);
                    }
                }
                Some(out)
            }
        }
    }
}

/// Whether `e` is a literal (or a negated one), whose binding `x :: 1` would be an untyped
/// constant rather than a variable of an inferred type (§4.1).
fn is_literal(e: &ast::Expr) -> bool {
    match &e.kind {
        ExprKind::Int(_) | ExprKind::Float(_) | ExprKind::Str(_) | ExprKind::Bool(_) => true,
        ExprKind::Unary { op: UnOp::Neg | UnOp::BitNot, operand } | ExprKind::Paren(operand) => is_literal(operand),
        ExprKind::Binary { lhs, rhs, .. } => is_literal(lhs) && is_literal(rhs),
        _ => false,
    }
}

/// Whether `e` is a call or tuple whose arguments are all literals: as the initializer of
/// a `::` binding it would be evaluated at compile time (§17.2), which a foreign function
/// cannot be.
fn is_constant_call(e: &ast::Expr) -> bool {
    match &e.kind {
        ExprKind::Call { args, .. } => args.iter().all(|a| match &a.kind {
            ArgKind::Positional(x) | ArgKind::Named(_, x) => is_literal(x),
            ArgKind::Spread(_) => false,
        }),
        ExprKind::Tuple(elems) => elems.iter().all(|el| is_literal(&el.value)),
        _ => false,
    }
}

/// `text` (the source of `e`), parenthesized if it is an `and`/`or` chain or an `else`,
/// which `not` binds tighter than (§B.1); with `comparisons`, also a comparison, which
/// reads better parenthesized after `not`.
pub(super) fn paren_unless_simple(e: &ast::Expr, text: &str, comparisons: bool) -> String {
    let needs = match &e.kind {
        ExprKind::Binary { op: BinOp::And | BinOp::Or, .. } | ExprKind::Else { .. } => true,
        ExprKind::Binary { op, .. } => {
            comparisons
                && matches!(
                    op,
                    BinOp::Is | BinOp::IsNot | BinOp::In | BinOp::NotIn | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge
                )
        }
        _ => false,
    };
    if needs { format!("({})", text) } else { text.to_string() }
}

/// `text` with its lines after the first moved left from indentation `from` to `to`.
fn dedent(text: &str, from: usize, to: usize) -> String {
    let shift = from.saturating_sub(to);
    let mut out = String::new();
    for (i, line) in text.split('\n').enumerate() {
        if i > 0 {
            out.push('\n');
            let blanks = line.len() - line.trim_start_matches([' ', '\t']).len();
            out.push_str(&line[blanks.min(shift)..]);
        } else {
            out.push_str(line);
        }
    }
    out
}

/// The names written as identifiers in `spans` of the token list `tokens`.
pub fn names_in(tokens: &[crate::syntax::token::Token], spans: &[Span], out: &mut HashSet<Symbol>) {
    for t in tokens {
        if let crate::syntax::token::TokenKind::Ident(s) = t.kind
            && spans.iter().any(|w| w.lo <= t.span.lo && t.span.hi <= w.hi)
        {
            out.insert(s);
        }
    }
}
