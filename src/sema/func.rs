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
    /// A nested named function (§11.8), which captures nothing.
    NestedFn(FnId),
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
    /// Comparison operands must be ordered (§7.5).
    Ord { ty: Ty, span: Span },
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
    pub next_label: u32,
    pub pending: Vec<PendingCoercion>,
    pub obligations: Vec<Obligation>,
    /// True while checking a deferred statement (§8.8 restrictions).
    pub in_defer: bool,
    /// The number of labels in scope when the innermost deferred statement began.
    pub defer_label_base: usize,
    /// Spans of literal coercions already reported as failing.
    pub failed_coercions: Vec<Span>,
    /// The function whose body is being checked (absent for constant expressions).
    pub fn_id: Option<FnId>,
    /// Whether the current function body refers to locals of enclosing functions.
    pub captures: bool,
    /// The state of the enclosing functions while a nested function literal is
    /// checked, outermost first. The current function is not in this list.
    pub outer: Vec<SavedFrame>,
    /// The frame depth (index into `outer`, or `outer.len()` for the current
    /// function) that owns each entry of `scopes`.
    scope_depths: Vec<u32>,
    /// Nested function bodies, finalized together with the enclosing body.
    pub nested: Vec<NestedFn>,
    /// Set while checking a handler clause (§12.5): how its exits behave.
    pub clause: Option<ClauseInfo>,
    /// Effects handled by enclosing `with`s (and `for` loops) of this body.
    pub handled: Vec<EffectRef>,
}

/// What kind of handler clause the current frame is.
#[derive(Clone, Debug)]
pub struct ClauseInfo {
    pub op_kind: ast::OpKind,
    /// For the clause of a `for` loop's handler: the loop's label in the enclosing frame.
    pub for_label: Option<(Option<Symbol>, LabelId)>,
    /// The handler literal the clause belongs to.
    pub handler: Option<tir::HandlerId>,
    /// For a `for` loop's clause: the type of the loop's value (`break v`).
    pub break_ty: Option<Ty>,
}

/// The per-function state of an enclosing function, saved while a nested
/// function literal is checked.
pub struct SavedFrame {
    pub locals: Vec<tir::LocalDecl>,
    pub ret: Ty,
    pub declared: EffectSet,
    pub performed: Vec<(EffectRef, Span)>,
    pub labels: Vec<LabelInfo>,
    pub next_label: u32,
    pub in_defer: bool,
    pub defer_label_base: usize,
    pub fn_id: Option<FnId>,
    pub captures: bool,
    pub clause: Option<ClauseInfo>,
    pub handled: Vec<EffectRef>,
}

/// A nested function checked as part of an enclosing body.
pub struct NestedFn {
    pub fn_id: FnId,
    pub params: Vec<LocalId>,
    pub locals: Vec<tir::LocalDecl>,
    pub body: Expr,
}

impl<'s> Sema<'s> {
    /// Checks the body of a top-level function and stores it in the program.
    pub(super) fn check_fn_body(&mut self, fn_id: FnId, module: ModuleId, item: &Rc<ast::Item>) {
        let ast::ItemKind::Binding(b) = &item.kind else { return };
        let ExprKind::Fn(lit) = &b.init.kind else { return };
        let sig = self.sigs[&fn_id].clone();
        self.checking.push(fn_id);
        // A generic body is checked once, with its type parameters opaque (§16.1).
        let scope_len = self.type_params.len();
        let value_scope_len = self.value_scope.len();
        for p in &sig.params {
            if let Some(pid) = p.generic {
                if self.value_params.contains_key(&pid) {
                    self.value_scope.push((p.name, pid));
                } else {
                    self.type_params.push((p.name, Ty::intern(TyKind::Param(pid))));
                }
            }
        }
        let numeric: Vec<_> =
            sig.generics.iter().copied().filter(|p| self.param_has(*p, super::generics::Constraint::Num)).collect();
        let mut cx = FnCx::new(self, module, sig.ret, sig.effects.clone());
        cx.fn_id = Some(fn_id);
        cx.infer.numeric_params = numeric;
        let mut params = vec![];
        for p in &sig.params {
            if p.generic.is_some() {
                continue;
            }
            let id = cx.new_local(p.name, p.ty, false, p.span);
            if p.name.as_str() != "_" {
                cx.declare(p.name, LocalBinding::Var(id), p.span);
            }
            params.push(id);
        }
        let body = cx.fn_body(&lit.body);
        let (body, locals) = cx.finish(body);
        self.type_params.truncate(scope_len);
        self.value_scope.truncate(value_scope_len);
        self.checking.pop();
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
            failed_coercions: vec![],
            fn_id: None,
            captures: false,
            outer: vec![],
            scope_depths: vec![0],
            nested: vec![],
            clause: None,
            handled: vec![],
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
        // Nested functions share this body's literal inference.
        for mut n in std::mem::take(&mut self.nested) {
            if let Some(root) = self.fn_id {
                self.sema.nested_owner.insert(n.fn_id, root);
            }
            let body = self.finalize_nested(n.body);
            for l in &mut n.locals {
                l.ty = self.infer.resolve(l.ty);
            }
            let def = &mut self.sema.prog.fns[n.fn_id.index()];
            def.params = n.params;
            def.locals = n.locals;
            def.body = Some(body);
        }
        (body, self.locals)
    }

    // --- nested function frames ---

    /// The current frame depth (0 for the outermost function).
    pub fn depth(&self) -> u32 {
        self.outer.len() as u32
    }

    /// Saves the current function's state and starts a nested function body.
    pub fn enter_fn(&mut self, fn_id: FnId, ret: Ty, declared: EffectSet) {
        let saved = SavedFrame {
            locals: std::mem::take(&mut self.locals),
            ret: std::mem::replace(&mut self.ret, ret),
            declared: std::mem::replace(&mut self.declared, declared),
            performed: std::mem::take(&mut self.performed),
            labels: std::mem::take(&mut self.labels),
            next_label: std::mem::replace(&mut self.next_label, 0),
            in_defer: std::mem::replace(&mut self.in_defer, false),
            defer_label_base: std::mem::replace(&mut self.defer_label_base, 0),
            fn_id: self.fn_id.replace(fn_id),
            captures: std::mem::replace(&mut self.captures, false),
            clause: self.clause.take(),
            handled: std::mem::take(&mut self.handled),
        };
        self.outer.push(saved);
        self.push_scope();
    }

    /// Ends a nested function body, restoring the enclosing function's state.
    /// Returns the nested function's locals, performed effects and whether it captures.
    pub fn exit_fn(&mut self) -> (Vec<tir::LocalDecl>, Vec<(EffectRef, Span)>, bool) {
        self.pop_scope();
        let saved = self.outer.pop().expect("exit_fn without enter_fn");
        let locals = std::mem::replace(&mut self.locals, saved.locals);
        self.ret = saved.ret;
        self.declared = saved.declared;
        let performed = std::mem::replace(&mut self.performed, saved.performed);
        self.labels = saved.labels;
        self.next_label = saved.next_label;
        self.in_defer = saved.in_defer;
        self.defer_label_base = saved.defer_label_base;
        self.fn_id = saved.fn_id;
        let captures = std::mem::replace(&mut self.captures, saved.captures);
        self.clause = saved.clause;
        self.handled = saved.handled;
        (locals, performed, captures)
    }

    /// The declaration of a local of the frame at `depth`.
    pub fn local_at(&self, depth: u32, id: LocalId) -> &tir::LocalDecl {
        if depth == self.depth() { &self.locals[id.index()] } else { &self.outer[depth as usize].locals[id.index()] }
    }

    /// Looks up a local name, returning its binding and the depth of the frame owning it.
    pub fn lookup_local_at(&self, name: Symbol) -> Option<(LocalBinding, u32)> {
        for (scope, depth) in self.scopes.iter().zip(&self.scope_depths).rev() {
            if let Some((_, b)) = scope.iter().rev().find(|(n, _)| *n == name) {
                return Some((b.clone(), *depth));
            }
        }
        None
    }

    /// Marks the frames above `depth` as capturing (they refer to a local of that frame).
    pub fn note_capture(&mut self, depth: u32) {
        let cur = self.depth();
        if depth < cur {
            self.captures = true;
            for f in self.outer.iter_mut().skip(depth as usize + 1) {
                f.captures = true;
            }
        }
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
        self.scope_depths.push(self.depth());
    }

    pub fn pop_scope(&mut self) {
        self.scopes.pop();
        self.scope_depths.pop();
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
            ExprKind::Match(m) => self.match_expr(m, false, None),
            ExprKind::With(w) => self.with_expr(w, false, None),
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
        if let ExprKind::Fn(lit) = &b.init.kind
            && b.kind == ast::BindingKind::Const
            && b.ty.is_none()
        {
            return self.nested_fn_binding(name, lit, b.span);
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
        if mutable && matches!(self.infer.resolve(ty).kind(), TyKind::Fn(_) | TyKind::Handler(_)) {
            self.error(
                b.span,
                "a second-class value (a function literal or handler) may only be bound with `::`, not stored in a variable (§11.6)",
            );
        }
        let id = self.new_local(name.name, ty, mutable, name.span);
        // The name is in scope only after its initializer (§4.6).
        self.declare(name.name, LocalBinding::Var(id), name.span);
        Stmt::Let { local: id, init: Some(init) }
    }

    /// Plain data (§5.19); a type parameter is plain data if it is constrained `Plain`.
    pub fn is_plain_data(&mut self, t: Ty) -> bool {
        t.is_error() || self.sema.is_plain(t)
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
        if let ExprKind::Ident(id) = &place_ast.kind {
            let immutable = match self.lookup_local_at(id.name) {
                Some((LocalBinding::Var(l), depth)) => !self.local_at(depth, l).mutable,
                _ => true,
            };
            if immutable {
                self.error(
                    place_ast.span,
                    format!("cannot assign to `{}`: it is immutable (declare it with `:=`)", id.name),
                );
                let _ = self.expr(value, None);
                return Stmt::Expr(unit(span));
            }
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
            // Constants live in read-only static storage (§15.1).
            tir::ExprKind::Const(_) => Some(false),
            tir::ExprKind::Upvar { levels, local } => Some(self.local_at(self.depth() - levels, *local).mutable),
            tir::ExprKind::Deref(p) => Some(match p.ty.kind() {
                TyKind::Ptr { is_const, .. } => !is_const,
                _ => false,
            }),
            tir::ExprKind::Field { base, .. } => self.place_mutability(base),
            tir::ExprKind::Index { base, .. } => match base.ty.kind() {
                TyKind::Slice { is_const, .. } | TyKind::ManyPtr { is_const, .. } => Some(!is_const),
                TyKind::Array { .. } | TyKind::ParamArray { .. } => self.place_mutability(base),
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
                ast::CondPart::Bind { pat, value } => parts.push(self.binding_cond(pat, value)),
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
            if matches!(f.iter.kind, ExprKind::Call { .. }) {
                return self.for_iterator(f);
            }
            if let Some(e) = self.for_each_loop(f) {
                return e;
            }
            self.error(f.iter.span, "`for` iterates over a range, an array or slice, or a call to an iterator");
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
        let item = local(i, elem);
        let lp = self.loop_with_pattern(f, elem, item, cond, step);
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
