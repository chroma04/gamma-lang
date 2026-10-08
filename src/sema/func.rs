//! Checking function bodies: the per-function context, local scopes,
//! statements, blocks and control flow (§4, §8). Expressions are in `expr.rs`,
//! calls in `call.rs`, and the final literal-type pass in `finalize.rs`.

use super::infer::{Fallback, InferTable, VarKind};
use super::untyped::Untyped;
use super::{ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind, StmtKind};
use crate::tir::{self, Expr, FnId, LabelId, LocalId, Stmt};
use crate::types::{EffectRef, EffectSet, Ty, TyKind};
use std::rc::Rc;

/// A name bound in a local scope.
#[derive(Clone, Debug)]
pub enum LocalBinding {
    Var(LocalId),
    /// An untyped constant local (`x :: 5`), inlined at each use.
    Untyped(Untyped),
    /// A label of an enclosing loop or `with`.
    Label,
}

#[derive(Clone, Debug)]
pub struct LabelInfo {
    pub name: Option<Symbol>,
    pub id: LabelId,
    /// True for loops (`continue` allowed).
    pub is_loop: bool,
}

/// A coercion whose source was an unresolved literal type when it was checked;
/// decided after inference (§6.1).
pub struct PendingCoercion {
    pub source: Ty,
    pub target: Ty,
    pub span: Span,
}

/// A check that needs the final types of literals.
pub enum Obligation {
    /// An expression statement must have type `()` or `never`.
    UnitStatement { ty: Ty, span: Span },
    /// A lossless conversion `T(x)` (§6.5).
    Conversion { from: Ty, to: Ty, span: Span },
    /// An operator operand must be a numeric type (and, if `int_only`, an integer).
    Numeric { ty: Ty, span: Span, op: &'static str, int_only: bool, signed_only: bool },
}

pub struct FnCx<'a, 's> {
    pub sema: &'a mut Sema<'s>,
    pub module: ModuleId,
    pub locals: Vec<tir::LocalDecl>,
    /// Block scopes, innermost last.
    pub scopes: Vec<Vec<(Symbol, LocalBinding)>>,
    pub infer: InferTable,
    pub ret: Ty,
    pub declared: EffectSet,
    pub performed: Vec<(EffectRef, Span)>,
    pub labels: Vec<LabelInfo>,
    next_label: u32,
    pub pending: Vec<PendingCoercion>,
    pub obligations: Vec<Obligation>,
    /// True while checking a deferred statement (§8.8 restrictions).
    pub in_defer: bool,
    /// The number of labels in scope when the innermost deferred statement began.
    pub defer_label_base: usize,
}

impl<'s> Sema<'s> {
    /// Checks the body of a top-level function and stores it in the program.
    pub(super) fn check_fn_body(&mut self, fn_id: FnId, module: ModuleId, item: &Rc<ast::Item>) {
        let ast::ItemKind::Binding(b) = &item.kind else { return };
        let ExprKind::Fn(lit) = &b.init.kind else { return };
        let sig = self.sigs[&fn_id].clone();
        let mut cx = FnCx::new(self, module, sig.ret, sig.effects.clone());
        let mut params = vec![];
        for p in &sig.params {
            let id = cx.new_local(p.name, p.ty, false, p.span);
            if p.name.as_str() != "_" {
                cx.declare(p.name, LocalBinding::Var(id), p.span);
            }
            params.push(id);
        }
        let body = cx.fn_body(&lit.body);
        let (body, locals) = cx.finish(body);
        let def = &mut self.prog.fns[fn_id.index()];
        def.params = params;
        def.locals = locals;
        def.body = Some(body);
    }

    /// Checks a constant expression (a typed constant's initializer) outside any function.
    pub fn check_const_expr(&mut self, module: ModuleId, e: &ast::Expr, expected: Option<Ty>) -> Option<Expr> {
        let mut cx = FnCx::new(self, module, Ty::unit(), EffectSet::empty());
        let mut ex = cx.expr(e, expected);
        if let Some(t) = expected {
            ex = cx.coerce(ex, t);
        }
        if !cx.performed.is_empty() {
            let span = cx.performed[0].1;
            cx.sema.error(span, "a constant's initializer may not perform effects");
        }
        let (ex, locals) = cx.finish(ex);
        if !locals.is_empty() {
            // Temporaries are not supported in constants yet.
            self.error(e.span, "this constant expression is not supported yet");
            return None;
        }
        Some(ex)
    }
}

impl<'a, 's> FnCx<'a, 's> {
    pub fn new(sema: &'a mut Sema<'s>, module: ModuleId, ret: Ty, declared: EffectSet) -> FnCx<'a, 's> {
        FnCx {
            sema,
            module,
            locals: vec![],
            scopes: vec![vec![]],
            infer: InferTable::default(),
            ret,
            declared,
            performed: vec![],
            labels: vec![],
            next_label: 0,
            pending: vec![],
            obligations: vec![],
            in_defer: false,
            defer_label_base: 0,
        }
    }

    pub fn error(&mut self, span: Span, msg: impl Into<String>) {
        self.sema.error(span, msg);
    }

    /// Runs the final inference pass and the effect check; returns the body and locals.
    pub fn finish(mut self, body: Expr) -> (Expr, Vec<tir::LocalDecl>) {
        let body = self.finalize(body);
        self.check_effects();
        for l in &mut self.locals {
            l.ty = self.infer.resolve(l.ty);
        }
        (body, self.locals)
    }

    fn check_effects(&mut self) {
        let performed = std::mem::take(&mut self.performed);
        let mut reported = vec![];
        for (e, span) in performed {
            if !self.declared.contains(&e) && !reported.contains(&e) {
                let name = e.effect.name();
                self.error(
                    span,
                    format!(
                        "this performs the effect `{}`, which the function does not declare (add `\\ {{{}}}`)",
                        name, name
                    ),
                );
                reported.push(e);
            }
        }
    }

    // --- locals and scopes ---

    pub fn new_local(&mut self, name: Symbol, ty: Ty, mutable: bool, span: Span) -> LocalId {
        let id = LocalId(self.locals.len() as u32);
        self.locals.push(tir::LocalDecl { name, ty, mutable, span });
        id
    }

    /// A compiler-generated local that is never named by the program.
    pub fn temp(&mut self, ty: Ty, span: Span) -> LocalId {
        self.new_local(Symbol::intern("<tmp>"), ty, true, span)
    }

    pub fn lookup_local(&self, name: Symbol) -> Option<&LocalBinding> {
        self.scopes.iter().rev().flat_map(|s| s.iter().rev()).find(|(n, _)| *n == name).map(|(_, b)| b)
    }

    /// Declares a local name, enforcing the no-shadowing rule (§4.6).
    pub fn declare(&mut self, name: Symbol, binding: LocalBinding, span: Span) {
        if name.as_str() == "_" {
            return;
        }
        if self.lookup_local(name).is_some() {
            self.error(span, format!("`{}` is already declared in this function; locals may not be shadowed", name));
        }
        self.scopes.last_mut().unwrap().push((name, binding));
    }

    pub fn push_scope(&mut self) {
        self.scopes.push(vec![]);
    }

    pub fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    pub fn local_ty(&self, id: LocalId) -> Ty {
        self.locals[id.index()].ty
    }

    pub fn new_label(&mut self, name: Option<Symbol>, is_loop: bool) -> LabelId {
        let id = LabelId(self.next_label);
        self.next_label += 1;
        self.labels.push(LabelInfo { name, id, is_loop });
        id
    }

    pub fn record_effects(&mut self, set: &EffectSet, span: Span) {
        for e in set.effects.iter() {
            self.performed.push((e.clone(), span));
        }
    }

    // --- bodies, blocks and statements ---

    fn fn_body(&mut self, body: &ast::FnBody) -> Expr {
        let ret = self.ret;
        match body {
            ast::FnBody::Expr(e) => {
                let ex = self.expr(e, Some(ret));
                self.coerce(ex, ret)
            }
            ast::FnBody::Block(b) => {
                let block = self.block(b, false);
                if !ret.is_unit() && !block.ty.is_never() {
                    self.error(
                        b.span.shrink_to_hi(),
                        format!("function returning `{}` can reach the end of its body without returning a value", ret),
                    );
                }
                block
            }
        }
    }

    /// Checks a block. If `value` is true, the block's value is used: its final
    /// statement, if an expression, supplies the value (§7.12).
    pub fn block(&mut self, b: &ast::Block, value: bool) -> Expr {
        self.block_expected(b, value, None)
    }

    pub fn block_expected(&mut self, b: &ast::Block, value: bool, expected: Option<Ty>) -> Expr {
        self.push_scope();
        let mut stmts = vec![];
        let mut tail = None;
        let mut diverges = false;
        let n = b.stmts.len();
        for (i, s) in b.stmts.iter().enumerate() {
            if diverges {
                self.error(s.span, "unreachable code");
                break;
            }
            let last = i + 1 == n;
            if last
                && value
                && let StmtKind::Expr(e) = &s.kind
            {
                let ex = self.expr(e, expected);
                diverges = ex.ty.is_never();
                tail = Some(ex);
                break;
            }
            let st = self.stmt(s);
            diverges = stmt_diverges(&st);
            stmts.push(st);
        }
        self.pop_scope();
        let ty = match &tail {
            Some(t) => t.ty,
            None if diverges => Ty::never(),
            None => Ty::unit(),
        };
        Expr { ty, kind: tir::ExprKind::Block(Box::new(tir::Block { stmts, tail, span: b.span })), span: b.span }
    }

    pub fn stmt(&mut self, s: &ast::Stmt) -> Stmt {
        match &s.kind {
            StmtKind::Binding { attrs, binding } => {
                for a in attrs {
                    self.error(a.span, format!("attribute `#{}` is not supported here yet", a.name.name));
                }
                self.binding(binding)
            }
            StmtKind::Assign { places, op, value } => self.assign(places, *op, value, s.span),
            StmtKind::Expr(e) => Stmt::Expr(self.stmt_expr(e)),
            StmtKind::Defer { is_err, body } => {
                let (was, base) = (self.in_defer, self.defer_label_base);
                self.in_defer = true;
                self.defer_label_base = self.labels.len();
                self.push_scope();
                let inner = self.stmt(body);
                self.pop_scope();
                self.in_defer = was;
                self.defer_label_base = base;
                let body = stmt_to_expr(inner, body.span);
                Stmt::Defer { is_err: *is_err, body }
            }
            StmtKind::While { label, cond, body } => Stmt::Expr(self.while_loop(*label, cond, body, s.span)),
            StmtKind::Block(b) => Stmt::Expr(self.block(b, false)),
            StmtKind::When(w) => {
                self.error(w.span, "`when` is not supported yet");
                Stmt::Expr(unit(s.span))
            }
            StmtKind::Discard(e) => {
                self.error(e.span, "`discard` is permitted only in a `ctl` clause");
                Stmt::Expr(unit(s.span))
            }
        }
    }

    /// An expression in statement position: its value must be `()` or `never` (§7.14).
    pub fn stmt_expr(&mut self, e: &ast::Expr) -> Expr {
        let ex = match &e.kind {
            ExprKind::If(i) => self.if_expr(i, false, None),
            ExprKind::Block(b) => self.block(b, false),
            ExprKind::For(f) => self.for_loop(f),
            _ => self.expr(e, None),
        };
        self.obligations.push(Obligation::UnitStatement { ty: ex.ty, span: e.span });
        ex
    }

    fn binding(&mut self, b: &ast::Binding) -> Stmt {
        if b.names.len() != 1 {
            self.error(b.span, "destructuring bindings are not supported yet");
            return Stmt::Expr(unit(b.span));
        }
        let name = b.names[0];
        if let ExprKind::Fn(_) = &b.init.kind
            && b.kind == ast::BindingKind::Const
        {
            self.error(b.span, "nested functions are not supported yet");
            return Stmt::Expr(unit(b.span));
        }
        let annot = b.ty.as_ref().map(|t| self.resolve_type(t));
        // An immutable binding of an untyped constant expression is an untyped constant (§6.2).
        if b.kind == ast::BindingKind::Const
            && annot.is_none()
            && let Some(v) = self.try_untyped(&b.init)
        {
            if let Ok(v) = v {
                self.declare(name.name, LocalBinding::Untyped(v), name.span);
            }
            return Stmt::Expr(unit(b.span));
        }
        let mutable = b.kind == ast::BindingKind::Var;
        let init = if let ExprKind::Uninit = b.init.kind {
            match annot {
                Some(t) => {
                    if !self.is_plain_data(t) {
                        self.error(b.init.span, format!("`---` requires a plain-data type, but `{}` is not (§4.4)", t));
                    }
                    Expr { ty: t, kind: tir::ExprKind::Uninit, span: b.init.span }
                }
                None => {
                    self.error(b.init.span, "`---` needs a type annotation");
                    error_expr(b.init.span)
                }
            }
        } else {
            let ex = self.expr(&b.init, annot);
            match annot {
                Some(t) => self.coerce(ex, t),
                None => ex,
            }
        };
        let ty = annot.unwrap_or(init.ty);
        if ty.is_never() && annot.is_none() {
            // `x := return 5` — the binding is unreachable; give it the error type quietly.
        }
        if matches!(ty.kind(), TyKind::Type) {
            self.error(b.init.span, "types cannot be stored in variables; use `::` at top level for a type alias");
        }
        let id = self.new_local(name.name, ty, mutable, name.span);
        // The name is in scope only after its initializer (§4.6).
        self.declare(name.name, LocalBinding::Var(id), name.span);
        Stmt::Let { local: id, init: Some(init) }
    }

    pub fn is_plain_data(&mut self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize | TyKind::Float(_) => true,
            TyKind::Array { elem, .. } => self.is_plain_data(*elem),
            TyKind::Tuple { elems, .. } => elems.iter().all(|e| self.is_plain_data(*e)),
            TyKind::Error => true,
            _ => false,
        }
    }

    fn assign(&mut self, places: &[ast::Expr], op: ast::AssignOp, value: &ast::Expr, span: Span) -> Stmt {
        if places.len() != 1 {
            self.error(span, "multiple assignment is not supported yet");
            return Stmt::Expr(unit(span));
        }
        let place_ast = &places[0];
        if let ExprKind::Underscore = place_ast.kind {
            if op != ast::AssignOp::Assign {
                self.error(span, "`_` can only be assigned with `=`");
            }
            let v = self.expr(value, None);
            return Stmt::Expr(Expr {
                ty: Ty::unit(),
                kind: tir::ExprKind::Block(Box::new(discard_block(v, span))),
                span,
            });
        }
        let place = self.expr(place_ast, None);
        self.require_mutable_place(&place, place_ast.span);
        match op {
            ast::AssignOp::Assign => {
                let v = self.expr(value, Some(place.ty));
                let v = self.assign_coerce(v, place.ty);
                Stmt::Assign { place, value: v }
            }
            ast::AssignOp::Compound(bop) => {
                // `p op= e` is `p = p op e` with `p` evaluated once (§4.7).
                if is_simple_place(&place) {
                    let rhs = self.binary_op(bop, place.clone(), value, span);
                    let rhs = self.assign_coerce(rhs, place.ty);
                    Stmt::Assign { place, value: rhs }
                } else {
                    let pty = Ty::ptr(false, place.ty);
                    let tmp = self.temp(pty, span);
                    let addr = Expr { ty: pty, kind: tir::ExprKind::AddrOf(Box::new(place.clone())), span };
                    let deref = |sp| Expr {
                        ty: place.ty,
                        kind: tir::ExprKind::Deref(Box::new(Expr {
                            ty: pty,
                            kind: tir::ExprKind::Local(tmp),
                            span: sp,
                        })),
                        span: sp,
                    };
                    let rhs = self.binary_op(bop, deref(span), value, span);
                    let rhs = self.assign_coerce(rhs, place.ty);
                    let block = tir::Block {
                        stmts: vec![
                            Stmt::Let { local: tmp, init: Some(addr) },
                            Stmt::Assign { place: deref(span), value: rhs },
                        ],
                        tail: None,
                        span,
                    };
                    Stmt::Expr(Expr { ty: Ty::unit(), kind: tir::ExprKind::Block(Box::new(block)), span })
                }
            }
        }
    }

    /// Both sides of an assignment have equal types after coercion (§6.1, §6.4).
    fn assign_coerce(&mut self, v: Expr, target: Ty) -> Expr {
        self.coerce(v, target)
    }

    /// Reports an error unless `place` is a mutable place (§7.2).
    pub fn require_mutable_place(&mut self, place: &Expr, span: Span) {
        match self.place_mutability(place) {
            Some(true) => {}
            Some(false) => self.error(span, "cannot assign to an immutable place"),
            None => {
                if !place.ty.is_error() {
                    self.error(
                        span,
                        "the left side of an assignment must be a place (a variable, `p^`, field or element)",
                    )
                }
            }
        }
    }

    /// `Some(mutable)` if `e` is a place, `None` otherwise.
    pub fn place_mutability(&self, e: &Expr) -> Option<bool> {
        if e.ty.is_error() {
            return Some(true);
        }
        match &e.kind {
            tir::ExprKind::Local(l) => Some(self.locals[l.index()].mutable),
            tir::ExprKind::Deref(p) => Some(match p.ty.kind() {
                TyKind::Ptr { is_const, .. } => !is_const,
                _ => false,
            }),
            tir::ExprKind::Field { base, .. } => self.place_mutability(base),
            tir::ExprKind::Index { base, .. } => match base.ty.kind() {
                TyKind::Slice { is_const, .. } | TyKind::ManyPtr { is_const, .. } => Some(!is_const),
                TyKind::Array { .. } => self.place_mutability(base),
                _ => None,
            },
            _ => None,
        }
    }

    // --- loops ---

    fn while_loop(&mut self, label: Option<ast::Ident>, cond: &ast::Cond, body: &ast::Block, span: Span) -> Expr {
        self.push_scope();
        if let Some(l) = label {
            self.declare(l.name, LocalBinding::Label, l.span);
        }
        let cond = self.cond(cond);
        let id = self.new_label(label.map(|l| l.name), true);
        let body = self.block(body, false);
        self.labels.pop();
        self.pop_scope();
        let infinite =
            cond.iter().all(|c| matches!(c, tir::CondPart::Bool(Expr { kind: tir::ExprKind::Bool(true), .. })));
        let breaks = contains_break_to(&body, id);
        let ty = if infinite && !breaks { Ty::never() } else { Ty::unit() };
        let body = match body.kind {
            tir::ExprKind::Block(b) => b,
            _ => unreachable!(),
        };
        Expr { ty, kind: tir::ExprKind::Loop { label: id, cond, body, step: None }, span }
    }

    /// Checks an `if`/`while` condition (§8.2). Binding parts are not supported yet.
    pub fn cond(&mut self, c: &ast::Cond) -> Vec<tir::CondPart> {
        let mut parts = vec![];
        for p in &c.parts {
            match p {
                ast::CondPart::Expr(e) => {
                    let ex = self.expr(e, Some(Ty::bool()));
                    let ex = self.expect_bool(ex);
                    parts.push(tir::CondPart::Bool(ex));
                }
                ast::CondPart::Bind { pat, .. } => {
                    self.error(pat.span, "binding conditions are not supported yet");
                }
            }
        }
        parts
    }

    pub fn expect_bool(&mut self, e: Expr) -> Expr {
        if !e.ty.is_bool() && !e.ty.is_never() && !e.ty.is_error() {
            let t = self.infer.resolve(e.ty);
            self.error(e.span, format!("expected `bool`, found `{}` (there are no truthy values)", t));
        }
        e
    }

    fn for_loop(&mut self, f: &ast::ForExpr) -> Expr {
        let span = f.span;
        let ExprKind::Binary { op: op @ (ast::BinOp::Range | ast::BinOp::RangeIncl), lhs, rhs } = &f.iter.kind else {
            self.error(f.iter.span, "only `for` over ranges is supported yet");
            return error_expr(span);
        };
        let inclusive = *op == ast::BinOp::RangeIncl;
        let lo = self.expr(lhs, None);
        let hi = self.expr(rhs, None);
        let elem = self.unify_operands(lo.ty, hi.ty, f.iter.span, "range bounds");
        self.infer.set_fallback(elem, Fallback::Usize);
        self.obligations.push(Obligation::Numeric {
            ty: elem,
            span: f.iter.span,
            op: "range",
            int_only: true,
            signed_only: false,
        });
        if f.pats.len() != 1 {
            self.error(f.span, "a range yields integers; use a single pattern");
        }
        // { lo_t := lo; hi_t := hi; i := lo_t; going := lo_t <(=) hi_t;
        //   loop going { p :: i; body } step { if i is hi_t { going = false } else { i += 1 } } }
        // The half-open form uses `i < hi_t` as the condition and `i += 1` as the step.
        let hi_t = self.temp(elem, span);
        let i = self.temp(elem, span);
        let local = |id, ty| Expr { ty, kind: tir::ExprKind::Local(id), span };
        let one = Expr { ty: elem, kind: tir::ExprKind::Int(1.into()), span };
        let incr = Stmt::Assign {
            place: local(i, elem),
            value: Expr {
                ty: elem,
                kind: tir::ExprKind::Binary {
                    op: tir::BinOp::Add(ast::Flavor::Wrap),
                    lhs: Box::new(local(i, elem)),
                    rhs: Box::new(one),
                },
                span,
            },
        };
        let mut stmts = vec![Stmt::Let { local: i, init: Some(lo) }, Stmt::Let { local: hi_t, init: Some(hi) }];
        let (cond, step) = if inclusive {
            let going = self.temp(Ty::bool(), span);
            stmts.push(Stmt::Let {
                local: going,
                init: Some(Expr {
                    ty: Ty::bool(),
                    kind: tir::ExprKind::Binary {
                        op: tir::BinOp::Le,
                        lhs: Box::new(local(i, elem)),
                        rhs: Box::new(local(hi_t, elem)),
                    },
                    span,
                }),
            });
            let at_end = Expr {
                ty: Ty::bool(),
                kind: tir::ExprKind::Eq {
                    lhs: Box::new(local(i, elem)),
                    rhs: Box::new(local(hi_t, elem)),
                    negated: false,
                },
                span,
            };
            let stop = Stmt::Assign {
                place: local(going, Ty::bool()),
                value: Expr { ty: Ty::bool(), kind: tir::ExprKind::Bool(false), span },
            };
            let step = Expr {
                ty: Ty::unit(),
                kind: tir::ExprKind::If {
                    cond: vec![tir::CondPart::Bool(at_end)],
                    then: Box::new(block_of(vec![stop], span)),
                    else_: Some(Box::new(block_of(vec![incr], span))),
                },
                span,
            };
            (vec![tir::CondPart::Bool(local(going, Ty::bool()))], step)
        } else {
            let lt = Expr {
                ty: Ty::bool(),
                kind: tir::ExprKind::Binary {
                    op: tir::BinOp::Lt,
                    lhs: Box::new(local(i, elem)),
                    rhs: Box::new(local(hi_t, elem)),
                },
                span,
            };
            (vec![tir::CondPart::Bool(lt)], block_of(vec![incr], span))
        };
        self.push_scope();
        if let Some(l) = f.label {
            self.declare(l.name, LocalBinding::Label, l.span);
        }
        let mut body_stmts = vec![];
        if let Some(p) = f.pats.first() {
            match &p.kind {
                ast::PatternKind::Bind(n) => {
                    let v = self.new_local(n.name, elem, false, n.span);
                    self.declare(n.name, LocalBinding::Var(v), n.span);
                    body_stmts.push(Stmt::Let { local: v, init: Some(local(i, elem)) });
                }
                ast::PatternKind::Wildcard => {}
                _ => self.error(p.span, "this pattern is not supported in `for` yet"),
            }
        }
        let id = self.new_label(f.label.map(|l| l.name), true);
        let body = self.block(&f.body, false);
        self.labels.pop();
        self.pop_scope();
        body_stmts.push(Stmt::Expr(body));
        let lp = Expr {
            ty: Ty::unit(),
            kind: tir::ExprKind::Loop {
                label: id,
                cond,
                body: Box::new(tir::Block { stmts: body_stmts, tail: None, span }),
                step: Some(Box::new(step)),
            },
            span,
        };
        stmts.push(Stmt::Expr(lp));
        block_of(stmts, span)
    }

    // --- type expressions inside bodies ---

    pub fn resolve_type(&mut self, e: &ast::Expr) -> Ty {
        let module = self.module;
        self.sema.resolve_type(module, e)
    }

    /// Like [`Sema::try_untyped`], but local untyped constants are visible.
    pub fn try_untyped(&mut self, e: &ast::Expr) -> Option<Result<Untyped, ()>> {
        match &e.kind {
            ExprKind::Ident(id) => match self.lookup_local(id.name) {
                Some(LocalBinding::Untyped(v)) => Some(Ok(v.clone())),
                Some(_) => None,
                None => self.sema.try_untyped(self.module, e),
            },
            ExprKind::Paren(inner) => self.try_untyped(inner),
            ExprKind::Unary { op: ast::UnOp::Neg, operand } => {
                let v = self.try_untyped(operand)?;
                Some(v.and_then(|v| super::untyped::negate(&v).map_err(|m| self.error(e.span, m))))
            }
            ExprKind::Binary { op, lhs, rhs } => {
                if !untyped_op(*op) {
                    return None;
                }
                let a = self.try_untyped(lhs)?;
                let b = self.try_untyped(rhs)?;
                match (a, b) {
                    (Ok(a), Ok(b)) => Some(super::untyped::binary(*op, &a, &b).map_err(|m| self.error(e.span, m))),
                    _ => Some(Err(())),
                }
            }
            _ => self.sema.try_untyped(self.module, e),
        }
    }

    /// The type of a fresh variable for an untyped literal.
    pub fn literal_var(&mut self, kind: VarKind) -> Ty {
        self.infer.new_var(kind)
    }
}

/// Operators allowed in untyped constant expressions (§6.2).
pub fn untyped_op(op: ast::BinOp) -> bool {
    use ast::BinOp::*;
    match op {
        Add(f) | Sub(f) | Mul(f) | Shl(f) => f == ast::Flavor::Checked,
        Div | Rem | Shr | BitAnd | BitOr | BitXor | Lt | Le | Gt | Ge | Is | IsNot => true,
        _ => false,
    }
}

pub fn unit(span: Span) -> Expr {
    Expr { ty: Ty::unit(), kind: tir::ExprKind::Aggregate(vec![]), span }
}

pub fn error_expr(span: Span) -> Expr {
    Expr { ty: Ty::error(), kind: tir::ExprKind::Aggregate(vec![]), span }
}

pub fn block_of(stmts: Vec<Stmt>, span: Span) -> Expr {
    Expr { ty: Ty::unit(), kind: tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: None, span })), span }
}

fn discard_block(v: Expr, span: Span) -> tir::Block {
    tir::Block { stmts: vec![Stmt::Expr(v)], tail: None, span }
}

fn stmt_to_expr(s: Stmt, span: Span) -> Expr {
    match s {
        Stmt::Expr(e) => e,
        other => block_of(vec![other], span),
    }
}

fn stmt_diverges(s: &Stmt) -> bool {
    match s {
        Stmt::Expr(e) => e.ty.is_never(),
        Stmt::Let { init: Some(e), .. } => e.ty.is_never(),
        Stmt::Assign { value, .. } => value.ty.is_never(),
        _ => false,
    }
}

/// A place whose re-evaluation has no side effects and is cheap.
fn is_simple_place(e: &Expr) -> bool {
    match &e.kind {
        tir::ExprKind::Local(_) | tir::ExprKind::Upvar { .. } | tir::ExprKind::Global(_) => true,
        tir::ExprKind::Field { base, .. } => is_simple_place(base),
        tir::ExprKind::Deref(p) => matches!(p.kind, tir::ExprKind::Local(_)),
        _ => false,
    }
}

/// True if `e` contains a `break` targeting `label` in the current function.
fn contains_break_to(e: &Expr, label: LabelId) -> bool {
    let mut found = false;
    visit(e, &mut |x| {
        if let tir::ExprKind::Break { target: tir::Target::Label { levels: 0, label: l }, .. } = x.kind
            && l == label
        {
            found = true;
        }
    });
    found
}

/// Calls `f` on every expression in `e` (pre-order), including inside statements.
pub fn visit(e: &Expr, f: &mut dyn FnMut(&Expr)) {
    f(e);
    e.for_each_child(&mut |c| visit(c, f));
}
