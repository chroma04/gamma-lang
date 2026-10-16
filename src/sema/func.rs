//! Checking function bodies: the per-function context, local scopes,
//! statements, blocks and control flow (§4, §8). Expressions are in `expr.rs`,
//! calls in `call.rs`, and the final literal-type pass in `finalize.rs`.

use super::infer::{Fallback, InferTable, VarKind};
use super::untyped::Untyped;
use super::{ModuleId, Sema};
use crate::diag::Diagnostic;
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
    /// A local type alias (`T :: []u8`, §3.5).
    Type(Ty),
    /// An immutable local evaluated at compile time (§17.2): a constant in static memory.
    Const {
        ty: Ty,
        id: tir::ConstId,
    },
}

#[derive(Clone, Debug)]
pub struct LabelInfo {
    pub name: Option<Symbol>,
    pub id: LabelId,
    /// True for loops (`continue` allowed).
    pub is_loop: bool,
    /// What a `break` to this label supplies.
    pub value: LabelValue,
}

/// The value a `break` to a label supplies (§8.5).
#[derive(Clone, Copy, Debug)]
pub enum LabelValue {
    /// A `while` loop: none.
    None,
    /// A `for` loop: the iterator's result (`()` for ranges and arrays).
    Loop(Ty),
    /// A `with`: its value, of the expected type when there is one. The values are
    /// checked against the `with`'s type once its body has been checked.
    With(Option<Ty>),
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
    /// Field expressions known not to name a module member.
    pub not_module: std::collections::HashSet<ast::NodeId>,
    pub locals: Vec<tir::LocalDecl>,
    /// Block scopes, innermost last.
    pub scopes: Vec<Vec<(Symbol, LocalBinding)>>,
    pub infer: InferTable,
    pub ret: Ty,
    /// While a block-bodied literal without a return type is checked: `Some(None)` until
    /// its first `return` fixes the type (§11.1).
    pub open_ret: Option<Option<Ty>>,
    /// Literals bound with `name ::` whose return type is still being inferred.
    pub open_fns: Vec<FnId>,
    pub declared: EffectSet,
    pub performed: Vec<(EffectRef, Span)>,
    /// The calls of functions whose effects include `Yield`, as opposed to `yield` itself.
    pub iterator_calls: Vec<Span>,
    pub labels: Vec<LabelInfo>,
    pub next_label: u32,
    pub pending: Vec<PendingCoercion>,
    pub obligations: Vec<Obligation>,
    /// True while checking a deferred statement (§8.8 restrictions).
    pub in_defer: bool,
    /// The sites of performs of `fn` operations: a deferred statement may make them even
    /// when the effect has other kinds of operations (§8.8).
    pub fn_performs: Vec<Span>,
    /// Set just before checking an argument `&e` for a `^const T` parameter: `e` may be a
    /// temporary, which lives until the call returns (§7.10).
    pub temp_ref_arg: bool,
    /// The spans of the function literals made for iterator arguments (§13.5).
    pub iterator_args: Vec<Span>,
    /// The number of labels in scope when the innermost deferred statement began.
    pub defer_label_base: usize,
    /// Spans of literal coercions already reported as failing.
    pub failed_coercions: Vec<Span>,
    /// `match`es whose scrutinee had literal types not yet known when they were checked:
    /// the span of each and of its arms. Their exhaustiveness is checked in `finalize`.
    pub deferred_matches: Vec<(Span, Vec<Span>)>,
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
    /// For each scope, the length of `sema.type_params` when it began: local type
    /// aliases are visible to type expressions through that list.
    scope_types: Vec<(usize, usize, usize)>,
    /// Nested function bodies, finalized together with the enclosing body.
    pub nested: Vec<NestedFn>,
    /// Set while checking a handler clause (§12.5): how its exits behave.
    pub clause: Option<ClauseInfo>,
    /// Effects handled by enclosing `with`s (and `for` loops) of this body.
    pub handled: Vec<EffectRef>,
    /// The `break`s to each label, keyed by the frame depth and the label, with the
    /// type and span of the value given (`()` for loops). Taken out when the labeled
    /// construct is finished: a `with` checks the values.
    pub label_breaks: std::collections::HashMap<(usize, LabelId), Vec<(Ty, Span)>>,
    /// Handler literals bound with `::` whose clauses are not checked yet: they are
    /// checked at the first `with` that installs them, which gives their `break`s an
    /// expected type, or else at the end of their scope.
    pub pending_handlers: Vec<PendingHandler>,
    /// Effects performed by the clauses of a handler literal checked at a later `with`
    /// (see `pending_handlers`), with the number of handlers in scope where the literal
    /// was written: `with`s that began after that point must not handle them (§12.5).
    pub shielded_performs: std::collections::HashMap<Span, usize>,
}

/// A handler literal bound with `::` whose clauses are checked later, in the scope
/// where it was written.
pub struct PendingHandler {
    pub hid: tir::HandlerId,
    pub lit: ast::HandlerLit,
    pub span: Span,
    /// The frame depth where it was written.
    pub depth: usize,
    /// The length of each scope there.
    pub scope_lens: Vec<usize>,
    /// The length of `Sema::local_consts` at the literal.
    pub local_consts: usize,
    pub labels: usize,
    pub handled: usize,
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
    /// The effect whose operation the clause handles.
    pub effect: Option<EffectRef>,
    /// For a clause of a handler declaration: its function, under which the values of
    /// its `break`s to the installing `with` are recorded.
    pub decl_clause: Option<FnId>,
    /// For a clause of a handler literal written in a `with`: the type expected of the
    /// `with`'s value, which its `break`s give.
    pub with_expect: Option<Ty>,
}

/// The per-function state of an enclosing function, saved while a nested
/// function literal is checked.
pub struct SavedFrame {
    pub locals: Vec<tir::LocalDecl>,
    pub ret: Ty,
    pub open_ret: Option<Option<Ty>>,
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
        let outer = self.enter_module_scope();
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
        let own_name = self.nested_generic.get(&fn_id).copied();
        let mut cx = FnCx::new(self, module, sig.ret, sig.effects.clone());
        cx.fn_id = Some(fn_id);
        // A generic nested function can call itself by name (§11.8).
        if let Some(n) = own_name {
            cx.declare(n.name, LocalBinding::NestedFn(fn_id), n.span);
        }
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
        self.leave_module_scope(outer);
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
        if locals.is_empty() {
            return Some(ex);
        }
        // An initializer that needs temporaries (such as one with a spread) becomes the
        // body of a function without parameters, called where the value is needed.
        let (ty, span) = (ex.ty, ex.span);
        let sig = super::FnSig { module, params: vec![], ret: ty, effects: EffectSet::empty(), generics: vec![] };
        let f = self.new_fn(Symbol::intern("<constant>"), span, sig, tir::FnKind::Named);
        let def = &mut self.prog.fns[f.index()];
        def.locals = locals;
        def.body = Some(ex);
        Some(Expr { ty, kind: tir::ExprKind::Call { callee: tir::Callee::Fn(f), args: vec![] }, span })
    }
}

impl<'a, 's> FnCx<'a, 's> {
    pub fn new(sema: &'a mut Sema<'s>, module: ModuleId, ret: Ty, declared: EffectSet) -> FnCx<'a, 's> {
        FnCx {
            sema,
            module,
            not_module: Default::default(),
            locals: vec![],
            scopes: vec![vec![]],
            infer: InferTable::default(),
            ret,
            open_ret: None,
            open_fns: vec![],
            declared,
            performed: vec![],
            iterator_calls: vec![],
            labels: vec![],
            next_label: 0,
            pending: vec![],
            obligations: vec![],
            in_defer: false,
            fn_performs: vec![],
            temp_ref_arg: false,
            iterator_args: vec![],
            defer_label_base: 0,
            failed_coercions: vec![],
            deferred_matches: vec![],
            pending_handlers: vec![],
            shielded_performs: std::collections::HashMap::new(),
            fn_id: None,
            captures: false,
            outer: vec![],
            scope_depths: vec![0],
            scope_types: vec![],
            nested: vec![],
            clause: None,
            handled: vec![],
            label_breaks: Default::default(),
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
            // An inferred return type may still have been a literal's variable.
            let ret = self.infer.resolve(self.sema.prog.fns[n.fn_id.index()].ret);
            self.sema.prog.fns[n.fn_id.index()].ret = ret;
            if let Some(s) = self.sema.sigs.get_mut(&n.fn_id) {
                s.ret = ret;
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
            open_ret: self.open_ret.take(),
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

    /// Puts `f`'s state in place of the current function's, which it returns.
    fn swap_frame(&mut self, f: SavedFrame) -> SavedFrame {
        SavedFrame {
            locals: std::mem::replace(&mut self.locals, f.locals),
            ret: std::mem::replace(&mut self.ret, f.ret),
            open_ret: std::mem::replace(&mut self.open_ret, f.open_ret),
            declared: std::mem::replace(&mut self.declared, f.declared),
            performed: std::mem::replace(&mut self.performed, f.performed),
            labels: std::mem::replace(&mut self.labels, f.labels),
            next_label: std::mem::replace(&mut self.next_label, f.next_label),
            in_defer: std::mem::replace(&mut self.in_defer, f.in_defer),
            defer_label_base: std::mem::replace(&mut self.defer_label_base, f.defer_label_base),
            fn_id: std::mem::replace(&mut self.fn_id, f.fn_id),
            captures: std::mem::replace(&mut self.captures, f.captures),
            clause: std::mem::replace(&mut self.clause, f.clause),
            handled: std::mem::replace(&mut self.handled, f.handled),
        }
    }

    /// Binds `name :: handler E { … }`: the clauses are checked later (`check_pending_handler`).
    pub fn pending_handler_literal(&mut self, lit: &ast::HandlerLit, span: Span) -> Expr {
        let Some(hid) = self.new_handler_literal(lit) else { return error_expr(span) };
        self.pending_handlers.push(PendingHandler {
            hid,
            lit: lit.clone(),
            span,
            depth: self.outer.len(),
            scope_lens: self.scopes.iter().map(|s| s.len()).collect(),
            local_consts: self.sema.local_consts.len(),
            labels: self.labels.len(),
            handled: self.handled.len(),
        });
        Expr { ty: Ty::intern(TyKind::Handler(hid.0)), kind: tir::ExprKind::HandlerRef(hid), span }
    }

    /// Checks the clauses of the pending handler literal `hid`, if it is one, as if at the
    /// place where it was written, with `with_expect` as the type its `break`s give.
    pub fn check_pending_handler(&mut self, hid: tir::HandlerId, with_expect: Option<Ty>) {
        let Some(i) = self.pending_handlers.iter().position(|p| p.hid == hid) else { return };
        let p = self.pending_handlers.remove(i);
        // Return to the frame, scopes, labels and handlers of the literal's position.
        let mut frames = vec![];
        while self.outer.len() > p.depth {
            let f = self.outer.pop().expect("frame");
            frames.push(self.swap_frame(f));
        }
        let hidden_scopes = self.scopes.split_off(p.scope_lens.len());
        let hidden_depths = self.scope_depths.split_off(p.scope_lens.len());
        let mut hidden_tails = vec![];
        for (scope, &len) in self.scopes.iter_mut().zip(&p.scope_lens) {
            hidden_tails.push(scope.split_off(len.min(scope.len())));
        }
        let hidden_consts = self.sema.local_consts.split_off(p.local_consts.min(self.sema.local_consts.len()));
        let hidden_labels = self.labels.split_off(p.labels.min(self.labels.len()));
        let hidden_handled = self.handled.split_off(p.handled.min(self.handled.len()));
        let performed = self.performed.len();
        self.handler_clauses(hid, &p.lit, with_expect, p.span);
        for (_, at) in &self.performed[performed..] {
            self.shielded_performs.insert(*at, p.handled);
        }
        self.handled.extend(hidden_handled);
        self.labels.extend(hidden_labels);
        self.sema.local_consts.extend(hidden_consts);
        for (scope, tail) in self.scopes.iter_mut().zip(hidden_tails) {
            scope.extend(tail);
        }
        self.scopes.extend(hidden_scopes);
        self.scope_depths.extend(hidden_depths);
        while let Some(f) = frames.pop() {
            let outer = self.swap_frame(f);
            self.outer.push(outer);
        }
    }

    /// Checks the pending handler literals written in the scope that is ending.
    pub fn check_pending_handlers_of_scope(&mut self) {
        let (depth, scopes) = (self.outer.len(), self.scopes.len());
        while let Some(p) = self.pending_handlers.iter().find(|p| p.depth == depth && p.scope_lens.len() >= scopes) {
            let hid = p.hid;
            self.check_pending_handler(hid, None);
        }
    }

    /// Ends a nested function body, restoring the enclosing function's state.
    /// Returns the nested function's locals, performed effects and whether it captures.
    pub fn exit_fn(&mut self) -> (Vec<tir::LocalDecl>, Vec<(EffectRef, Span)>, bool) {
        self.pop_scope();
        let saved = self.outer.pop().expect("exit_fn without enter_fn");
        let locals = std::mem::replace(&mut self.locals, saved.locals);
        self.ret = saved.ret;
        self.open_ret = saved.open_ret;
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
                let declared = self.declared.clone();
                self.undeclared_effect(&e, span, &declared);
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

    /// Declares a local name, enforcing the no-shadowing rule and the rule that a local may
    /// not be named like a type visible here (§4.6).
    pub fn declare(&mut self, name: Symbol, binding: LocalBinding, span: Span) {
        if name.as_str() == "_" {
            return;
        }
        if self.lookup_local(name).is_some() {
            self.error(span, format!("`{}` is already declared in this function; locals may not be shadowed", name));
        } else if !matches!(binding, LocalBinding::Type(_) | LocalBinding::Label)
            && self.sema.names_type(self.module, name)
        {
            self.error(span, format!("`{}` names a type; a local may not have the name of a type (§4.6)", name));
        } else if super::is_reserved_type_name(name.as_str()) {
            // `cont`, and a local alias or label named like a predeclared type.
            self.error(span, format!("`{}` is a reserved type name and cannot be redeclared", name));
        }
        // Compile-time expressions of the body see its constants, and its variables hide
        // the top-level names they shadow (`n := 3; a: [n]u8` is an error, §4.6).
        if matches!(binding, LocalBinding::Untyped(_) | LocalBinding::Const { .. } | LocalBinding::Var(_)) {
            self.sema.local_consts.push((name, binding.clone()));
        }
        self.scopes.last_mut().unwrap().push((name, binding));
    }

    pub fn push_scope(&mut self) {
        self.scopes.push(vec![]);
        self.scope_depths.push(self.depth());
        self.scope_types.push((self.sema.type_params.len(), self.sema.local_fns.len(), self.sema.local_consts.len()));
    }

    pub fn pop_scope(&mut self) {
        self.scopes.pop();
        self.scope_depths.pop();
        if let Some((n, f, c)) = self.scope_types.pop() {
            self.sema.type_params.truncate(n);
            self.sema.local_fns.truncate(f);
            self.sema.local_consts.truncate(c);
        }
    }

    pub fn local_ty(&self, id: LocalId) -> Ty {
        self.locals[id.index()].ty
    }

    pub fn new_label(&mut self, name: Option<Symbol>, is_loop: bool, value: LabelValue) -> LabelId {
        let id = LabelId(self.next_label);
        self.next_label += 1;
        self.labels.push(LabelInfo { name, id, is_loop, value });
        id
    }

    pub fn record_effects(&mut self, set: &EffectSet, span: Span) {
        for e in set.effects.iter() {
            if Some(e.effect) == self.sema.yield_effect {
                self.iterator_calls.push(span);
            }
            self.performed.push((e.clone(), span));
        }
    }

    /// The message for an effect performed at `span` that the function, which declares
    /// `declared`, does not declare. A call of an iterator from a function with no `Yield` of
    /// its own was most likely meant as the source of a `for` loop.
    pub fn undeclared_effect_message(&self, e: &EffectRef, span: Span, declared: &EffectSet) -> String {
        let name = e.to_string();
        let yield_eff = self.sema.yield_effect;
        if Some(e.effect) == yield_eff
            && self.iterator_calls.contains(&span)
            && !declared.effects.iter().any(|d| Some(d.effect) == yield_eff)
        {
            // The call's callee: the text before its argument list, after any receiver.
            let text = self.sema.sess.sources.text(span);
            let callee = text.split('(').next().unwrap_or(text);
            let callee = callee.rsplit('.').next().unwrap_or(callee).trim();
            return format!(
                "`{callee}` is an iterator, not a value: call it as the source of a `for` loop (`for x in {callee}(…)`) or pass it to a function that runs it, such as `collect({callee}(…))` (§13.5)"
            );
        }
        format!("this performs the effect `{}`, which the function does not declare (add `needs {}`)", name, name)
    }

    /// Reports an effect performed at `span` that the function, which declares `declared`,
    /// does not declare.
    pub fn undeclared_effect(&mut self, e: &EffectRef, span: Span, declared: &EffectSet) {
        let msg = self.undeclared_effect_message(e, span, declared);
        self.error(span, msg);
    }

    // --- bodies, blocks and statements ---

    pub fn fn_body(&mut self, body: &ast::FnBody) -> Expr {
        let ret = self.ret;
        match body {
            ast::FnBody::Expr(e) => {
                let ex = self.expr(e, Some(ret));
                self.coerce(ex, ret)
            }
            ast::FnBody::Block(b) => {
                let block = self.block(b, false);
                self.finish_block_body(block, b)
            }
        }
    }

    /// A block body that has been checked: falling off the end of an `E!()` function returns
    /// success (§11.1); other non-unit functions must return a value.
    pub fn finish_block_body(&mut self, block: Expr, b: &ast::Block) -> Expr {
        // A test's error type may have been decided by the body.
        let ret = self.ret;
        if let TyKind::Result { ok, .. } = ret.kind()
            && ok.is_unit()
            && !block.ty.is_never()
        {
            let span = b.span.shrink_to_hi();
            let success = self.coerce(unit(span), ret);
            let stmts = vec![Stmt::Expr(block)];
            return Expr {
                ty: ret,
                kind: tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(success), span: b.span })),
                span: b.span,
            };
        }
        if !ret.is_unit() && !block.ty.is_never() {
            self.missing_return(b, ret);
        }
        block
    }

    /// Reports that a block body returning `ret` can end without a value. A final
    /// expression statement gets one error saying how to return it, instead of an unused
    /// value plus a missing return.
    pub fn missing_return(&mut self, b: &ast::Block, ret: Ty) {
        if let Some(ast::Stmt { kind: ast::StmtKind::Expr(e), .. }) = b.stmts.last()
            && !matches!(
                e.kind,
                ExprKind::If(_) | ExprKind::Match(_) | ExprKind::For(_) | ExprKind::With(_) | ExprKind::Block(_)
            )
            && let Some(k) = self.obligations.iter().rposition(|o| {
                matches!(o, Obligation::UnitStatement { ty, span } if *span == e.span && {
                    let t = self.infer.resolve(*ty);
                    !t.is_unit() && !t.is_never()
                })
            })
        {
            self.obligations.remove(k);
            self.error(
                e.span,
                format!(
                    "a block body's last expression is not its value: write `return …`, or `=> …` for a body that is one expression (this function returns `{}`)",
                    ret
                ),
            );
            return;
        }
        self.error(
            b.span.shrink_to_hi(),
            format!("function returning `{}` can reach the end of its body without returning a value", ret),
        );
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
        // Why the statements so far end control flow, for the "unreachable code" error.
        let mut cause: Option<(Span, &'static str)> = None;
        let n = b.stmts.len();
        for (i, s) in b.stmts.iter().enumerate() {
            if diverges {
                let mut d = Diagnostic::error(s.span, "unreachable code");
                if let Some((span, why)) = cause {
                    d = d.with_label(span, why);
                }
                self.sema.emit(d);
                break;
            }
            let last = i + 1 == n;
            // `P := v else d`: `if P := v { rest of the block } else { d }` (§4.5).
            if let StmtKind::LetElse { pat, value: v, fallback } = &s.kind {
                let rest_span = match b.stmts.get(i + 1) {
                    Some(next) => next.span.to(b.span.shrink_to_hi()),
                    None => s.span.shrink_to_hi(),
                };
                let rest = ast::Block { id: b.id, stmts: b.stmts[i + 1..].to_vec(), span: rest_span };
                let fb = ast::Block {
                    id: b.id,
                    stmts: vec![ast::Stmt { id: s.id, kind: StmtKind::Expr(fallback.clone()), span: fallback.span }],
                    span: fallback.span,
                };
                let cond = ast::Cond {
                    parts: vec![ast::CondPart::Bind { pat: pat.clone(), value: v.clone() }],
                    span: pat.span.to(v.span),
                };
                let lowered =
                    ast::IfExpr { branches: vec![(cond, rest)], else_: Some(ast::IfElse::Block(fb)), span: s.span };
                let ex = self.if_expr(&lowered, value, expected);
                diverges = ex.ty.is_never();
                if let tir::ExprKind::If { else_: Some(e), .. } = &ex.kind
                    && !e.ty.is_never()
                    && !e.ty.is_error()
                {
                    // One error, not also an unused value or a missing return after it.
                    self.obligations
                        .retain(|o| !matches!(o, Obligation::UnitStatement { span, .. } if *span == fallback.span));
                    self.error(
                        fallback.span,
                        "the `else` of a pattern binding must leave the block: `return`, `break`, `continue` or `trap(…)` (§4.5)",
                    );
                    diverges = true;
                }
                if value {
                    tail = Some(ex);
                } else {
                    stmts.push(Stmt::Expr(ex));
                }
                break;
            }
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
            if diverges {
                cause = stmt_never_cause(&st);
            }
            stmts.push(st);
        }
        if !self.pending_handlers.is_empty() {
            self.check_pending_handlers_of_scope();
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
                    self.error(
                        a.span,
                        format!("attribute `#{}` does not apply to a local binding (§20.2)", a.name.name),
                    );
                }
                self.binding(binding)
            }
            StmtKind::Assign { places, op, value } => self.assign(places, *op, value, s.span),
            StmtKind::Expr(e) => Stmt::Expr(self.stmt_expr(e)),
            StmtKind::Defer { is_err, body } => {
                let (was, base) = (self.in_defer, self.defer_label_base);
                self.in_defer = true;
                self.defer_label_base = self.labels.len();
                let performed_base = self.performed.len();
                self.push_scope();
                let inner = self.stmt(body);
                self.pop_scope();
                // It may only perform `fn` operations, unless it handles them itself (§8.8).
                let exits: Vec<(EffectRef, Span)> = self.performed[performed_base..]
                    .iter()
                    .filter(|(e, sp)| {
                        !self.fn_performs.contains(sp)
                            && self
                                .sema
                                .effects
                                .get(&e.effect)
                                .is_some_and(|info| info.ops.iter().any(|o| o.kind != ast::OpKind::Fn))
                    })
                    .cloned()
                    .collect();
                for (e, sp) in exits {
                    self.error(
                        sp,
                        format!(
                            "a deferred statement may not perform `{}`, whose operations can exit or suspend, unless it handles it (§8.8)",
                            e
                        ),
                    );
                }
                self.in_defer = was;
                self.defer_label_base = base;
                let body = stmt_to_expr(inner, body.span);
                Stmt::Defer { is_err: *is_err, body }
            }
            StmtKind::While { label, cond, body } => Stmt::Expr(self.while_loop(*label, cond, body, s.span)),
            StmtKind::Block(b) => Stmt::Expr(self.block(b, false)),
            StmtKind::When(w) => Stmt::Expr(self.when_stmt(w)),
            StmtKind::Cancel(e) => Stmt::Expr(self.cancel_stmt(e, s.span)),
            StmtKind::LetElse { value, .. } => {
                self.error(s.span, "a binding with a pattern that can fail must be a statement of a block");
                Stmt::Expr(self.expr(value, None))
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
            _ => {
                let errors = self.sema.sess.diags.error_count();
                let ex = self.expr(e, None);
                // A call that was already reported (wrong arguments, say) has a value of
                // doubtful type: no second error about it being unused.
                if self.sema.sess.diags.error_count() != errors {
                    return ex;
                }
                ex
            }
        };
        self.obligations.push(Obligation::UnitStatement { ty: ex.ty, span: e.span });
        ex
    }

    /// Evaluates the initializer of immutable local `b` at compile time if it is a
    /// constant expression (§17.2) whose value may live in static memory (§17.4): it reads
    /// no locals and no compile-time parameters, and checks again as a constant. Returns
    /// the constant's binding, or `None` to evaluate the local at run time. Running out of
    /// steps falls back to run time; any other failure is a compile error.
    fn opportunistic_const(&mut self, b: &ast::Binding, annot: Option<Ty>, init: &Expr) -> Option<LocalBinding> {
        if matches!(b.init.kind, ExprKind::Uninit)
            || self.sema.type_params.iter().any(|&(_, t)| super::generics::has_param(t))
            || !self.sema.value_scope.is_empty()
            || !closed_const(init)
        {
            return None;
        }
        let module = self.module;
        let n = self.sema.sess.diags.len();
        let checked = self.sema.check_const_expr(module, &b.init, annot);
        // Checked as a constant, the initializer may fail where it is fine at run time
        // (it uses a local type alias, for instance): then it simply runs.
        let ok = self.sema.sess.diags.len() == n;
        self.sema.sess.diags.truncate(n);
        let cex = checked.filter(|_| ok)?;
        if cex.ty.is_error() || super::decls::non_constant_part(cex.ty).is_some() {
            return None;
        }
        let n = self.sema.sess.diags.len();
        let value = match self.sema.try_eval_const(&cex) {
            Ok(v) => v,
            // What cannot be evaluated yet (it calls a function nested in this one) runs.
            Err(None) => {
                self.sema.sess.diags.truncate(n);
                return None;
            }
            Err(Some(t)) if t.message.contains("step budget") => return None,
            Err(Some(t)) => {
                self.sema.report_eval_failure(&cex, &t);
                return None;
            }
        };
        let id = tir::ConstId(self.sema.prog.consts.len() as u32);
        let ty = cex.ty;
        let name = b.names[0];
        self.sema.prog.consts.push(tir::ConstDef {
            name: name.name,
            ty,
            span: name.span,
            init: cex,
            value: Some(value),
        });
        Some(LocalBinding::Const { ty, id })
    }

    /// `a, b := e`: binds the elements of a tuple (§4.5).
    fn destructure(&mut self, b: &ast::Binding) -> Stmt {
        let annot = b.ty.as_ref().map(|t| self.resolve_type(t));
        let init = if let ExprKind::Uninit = b.init.kind {
            if b.kind != ast::BindingKind::Var {
                self.error(b.init.span, "an immutable binding cannot be `---`: it could never be given a value (§4.4)");
            }
            match annot {
                Some(t) if self.is_plain_data(t) => Expr { ty: t, kind: tir::ExprKind::Uninit, span: b.init.span },
                _ => {
                    self.error(b.init.span, "`---` needs a plain-data tuple type annotation");
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
        let t = self.infer.resolve(init.ty);
        let elems: Vec<Ty> = match t.kind() {
            TyKind::Tuple { elems, .. } if elems.len() == b.names.len() => elems.to_vec(),
            TyKind::Tuple { elems, .. } => {
                self.error(
                    b.span,
                    format!("{} names cannot destructure a tuple of {} elements", b.names.len(), elems.len()),
                );
                vec![Ty::error(); b.names.len()]
            }
            TyKind::Error => vec![Ty::error(); b.names.len()],
            _ => {
                self.error(b.init.span, format!("destructuring needs a tuple, but this is `{}` (§4.5)", t));
                vec![Ty::error(); b.names.len()]
            }
        };
        let mutable = b.kind == ast::BindingKind::Var;
        let mut locals = vec![];
        let mut declared = vec![];
        for (name, ty) in b.names.iter().zip(elems) {
            if name.name.as_str() == "_" {
                locals.push(None);
                continue;
            }
            let id = self.new_local(name.name, ty, mutable, name.span);
            locals.push(Some(id));
            declared.push((name.name, id, name.span));
        }
        // The names are in scope only after the initializer (§4.6).
        for (n, id, sp) in declared {
            self.declare(n, LocalBinding::Var(id), sp);
        }
        Stmt::LetTuple { locals, init }
    }

    fn binding(&mut self, b: &ast::Binding) -> Stmt {
        if b.names.len() != 1 {
            return self.destructure(b);
        }
        let name = b.names[0];
        if let ExprKind::Fn(lit) = &b.init.kind
            && b.kind == ast::BindingKind::Const
            && b.ty.is_none()
        {
            return self.nested_fn_binding(name, lit, b.span);
        }
        if b.kind == ast::BindingKind::Const
            && b.ty.is_none()
            && let Some(t) = self.local_type_init(&b.init)
        {
            self.declare(name.name, LocalBinding::Type(t), name.span);
            self.sema.type_params.push((name.name, t));
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
        let errors_before = self.sema.sess.diags.error_count();
        let mut handler_init = &b.init;
        while let ExprKind::Paren(e) = &handler_init.kind {
            handler_init = e;
        }
        let init = if let ExprKind::Handler(lit) = &handler_init.kind
            && b.kind == ast::BindingKind::Const
            && annot.is_none()
            && !self.in_defer
        {
            self.pending_handler_literal(lit, b.init.span)
        } else if let ExprKind::Uninit = b.init.kind {
            if !mutable {
                self.error(b.init.span, "an immutable binding cannot be `---`: it could never be given a value (§4.4)");
            }
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
        // An immutable local whose initializer is a constant expression is evaluated at
        // compile time when that succeeds within the step budget (§17.2).
        if b.kind == ast::BindingKind::Const
            && errors_before == self.sema.sess.diags.error_count()
            && let Some(c) = self.opportunistic_const(b, annot, &init)
        {
            self.declare(name.name, c, name.span);
            return Stmt::Expr(unit(b.span));
        }
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

    /// `a, b = e`: assigns the elements of a tuple; every element is computed before any
    /// store (§4.7).
    fn assign_tuple(&mut self, places: &[ast::Expr], op: ast::AssignOp, value: &ast::Expr, span: Span) -> Stmt {
        if op != ast::AssignOp::Assign {
            self.error(span, "a compound assignment takes one place");
        }
        let mut targets = vec![];
        for p in places {
            if let ExprKind::Underscore = p.kind {
                targets.push(None);
                continue;
            }
            if !self.assignable_name(p) {
                let _ = self.expr(value, None);
                return Stmt::Expr(unit(span));
            }
            let place = self.expr(p, None);
            self.require_mutable_place(&place, p.span);
            targets.push(Some(place));
        }
        let expect = targets.iter().map(|t| t.as_ref().map(|p| p.ty)).collect::<Option<Vec<Ty>>>().map(Ty::tuple);
        let v = self.expr(value, expect);
        let vt = self.infer.resolve(v.ty);
        let elems: Vec<Ty> = match vt.kind() {
            TyKind::Tuple { elems, .. } if elems.len() == targets.len() => elems.to_vec(),
            TyKind::Error => return Stmt::Expr(unit(span)),
            _ => {
                self.error(
                    value.span,
                    format!(
                        "assigning to {} places needs a tuple of {} elements, not `{}`",
                        targets.len(),
                        targets.len(),
                        vt
                    ),
                );
                return Stmt::Expr(unit(span));
            }
        };
        let target_ty = Ty::tuple(targets.iter().zip(&elems).map(|(t, e)| t.as_ref().map_or(*e, |p| p.ty)).collect());
        let v = self.assign_coerce(v, target_ty);
        Stmt::AssignTuple { places: targets, value: v }
    }

    /// For an assignment to a plain name: whether it names mutable storage (reports it if not).
    fn assignable_name(&mut self, place_ast: &ast::Expr) -> bool {
        let ExprKind::Ident(id) = &place_ast.kind else { return true };
        let immutable = match self.lookup_local_at(id.name) {
            Some((LocalBinding::Var(l), depth)) => !self.local_at(depth, l).mutable,
            Some(_) => true,
            // A `#global` is mutable storage (§19.3); a declaration with errors was reported.
            None => {
                let module = self.module;
                !matches!(
                    self.sema.lookup_module_name(module, id.name).map(|d| self.sema.resolve_decl(d)),
                    Some(super::decls::DeclValue::Global { .. } | super::decls::DeclValue::Error)
                )
            }
        };
        if immutable {
            self.error(
                place_ast.span,
                format!("cannot assign to `{}`: it is immutable (declare it with `:=`)", id.name),
            );
        }
        !immutable
    }

    fn assign(&mut self, places: &[ast::Expr], op: ast::AssignOp, value: &ast::Expr, span: Span) -> Stmt {
        if places.len() != 1 {
            return self.assign_tuple(places, op, value, span);
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
        if !self.assignable_name(place_ast) {
            let _ = self.expr(value, None);
            return Stmt::Expr(unit(span));
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
                    let rhs = self.compound_op(bop, place.clone(), value, span);
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
                    let rhs = self.compound_op(bop, deref(span), value, span);
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

    /// `place op value` for `place op= value`: the right operand of an arithmetic or
    /// bitwise operator may have a narrower numeric type that converts losslessly to the
    /// place's type (§4.7, §6.5).
    fn compound_op(&mut self, op: ast::BinOp, place: Expr, value: &ast::Expr, span: Span) -> Expr {
        use ast::BinOp as B;
        if !matches!(op, B::Add(_) | B::Sub(_) | B::Mul(_) | B::Div | B::Rem | B::BitAnd | B::BitOr | B::BitXor) {
            return self.binary_op(op, place, value, span);
        }
        let numeric = |t: Ty| t.is_integer() || t.is_float();
        if !numeric(self.infer.resolve(place.ty)) {
            return self.binary_op(op, place, value, span);
        }
        let r = self.expr(value, Some(place.ty));
        let (pt, rt) = (self.infer.resolve(place.ty), self.infer.resolve(r.ty));
        let r = if pt != rt && numeric(pt) && numeric(rt) && super::finalize::lossless(rt, pt) {
            let sp = r.span;
            Expr { ty: pt, kind: tir::ExprKind::Builtin { op: tir::Builtin::Convert, args: vec![r] }, span: sp }
        } else {
            r
        };
        self.arith_op(op, place, r, span)
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
            tir::ExprKind::Global(_) => Some(true),
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
        let id = self.new_label(label.map(|l| l.name), true, LabelValue::None);
        let body = self.block(body, false);
        self.labels.pop();
        self.pop_scope();
        let infinite =
            cond.iter().all(|c| matches!(c, tir::CondPart::Bool(Expr { kind: tir::ExprKind::Bool(true), .. })));
        let breaks = self.label_breaks.remove(&(self.outer.len(), id)).is_some();
        let ty = if infinite && !breaks { Ty::never() } else { Ty::unit() };
        let body = match body.kind {
            tir::ExprKind::Block(b) => b,
            _ => unreachable!(),
        };
        Expr { ty, kind: tir::ExprKind::Loop { label: id, cond, body, step: None }, span }
    }

    /// Checks an `if`/`while` condition (§8.2): boolean parts and `pattern := value` binding parts.
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

    pub fn for_loop(&mut self, f: &ast::ForExpr) -> Expr {
        let span = f.span;
        let ExprKind::Binary { op: op @ (ast::BinOp::Range | ast::BinOp::RangeIncl), lhs, rhs } = &f.iter.kind else {
            if matches!(f.iter.kind, ExprKind::Call { .. }) {
                return self.for_iterator(f);
            }
            if let Some(e) = self.for_each_loop(f) {
                return e;
            }
            self.error(f.iter.span, "`for` iterates over a range, an array, slice or tuple, or a call to an iterator");
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
        self.range_loop(f, lo, hi, elem, inclusive)
    }

    /// A `for` loop over the integers from `lo` to `hi` (of type `elem`), excluding
    /// `hi` unless `inclusive`.
    pub fn range_loop(&mut self, f: &ast::ForExpr, lo: Expr, hi: Expr, elem: Ty, inclusive: bool) -> Expr {
        let span = f.span;
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

    /// The type that the initializer `e` of a local `::` binding denotes, if it is a
    /// type expression: the binding is then a local alias (§3.5).
    fn local_type_init(&mut self, e: &ast::Expr) -> Option<Ty> {
        match &e.kind {
            ExprKind::ArrayType { .. } | ExprKind::OptionalType(_) if self.is_construction(e) => {
                let module = self.module;
                self.sema.looks_like_type(module, e).then(|| self.resolve_type(e))
            }
            ExprKind::Tuple(_) => self.local_is_type(e).then(|| self.resolve_type(e)),
            ExprKind::OptionalType(_)
            | ExprKind::PtrType { .. }
            | ExprKind::SliceType { .. }
            | ExprKind::ManyPtrType { .. }
            | ExprKind::ArrayType { .. }
            | ExprKind::ErrorUnionType { .. }
            | ExprKind::FnType(_)
            | ExprKind::ContType(_) => Some(self.resolve_type(e)),
            ExprKind::Intrinsic { name, .. } if name.name.as_str() == "type_of" => Some(self.resolve_type(e)),
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Effect(_) => {
                self.error(
                    e.span,
                    "types are declared at top level; inside a function only aliases are allowed (§3.5)",
                );
                Some(Ty::error())
            }
            ExprKind::Paren(inner) => self.local_type_init(inner),
            ExprKind::Ident(id) => {
                if let Some(b) = self.lookup_local(id.name) {
                    return match b {
                        LocalBinding::Type(t) => Some(*t),
                        _ => None,
                    };
                }
                if let Some(&(_, t)) = self.sema.type_params.iter().rev().find(|(n, _)| *n == id.name) {
                    return Some(t);
                }
                let module = self.module;
                if let Some(d) = self.sema.lookup_module_name(module, id.name) {
                    return match self.sema.resolve_decl(d) {
                        super::DeclValue::Type(t) => Some(t),
                        _ => None,
                    };
                }
                super::builtins::builtin_type(id.name.as_str())
            }
            ExprKind::Field { base, name } => {
                let module = self.module;
                let m = self.sema.module_named(module, base)?;
                let d = self.sema.module_member(m, *name)?;
                match self.sema.resolve_decl(d) {
                    super::DeclValue::Type(t) => Some(t),
                    _ => None,
                }
            }
            _ => None,
        }
    }

    /// True if `e` is `[N]T(…)` or `?T(…)`, possibly followed by a postfix chain: a
    /// construction rather than a type (§A.7 rule 12).
    fn is_construction(&self, e: &ast::Expr) -> bool {
        match &e.kind {
            ExprKind::ArrayType { elem: inner, .. } | ExprKind::OptionalType(inner) => {
                super::expr::construction_call(inner).is_some() || super::expr::hoist_prefix(e).is_some()
            }
            _ => false,
        }
    }

    /// True if `e` denotes a type, given the local aliases in scope (the local counterpart
    /// of [`Sema::looks_like_type`]); reports nothing.
    fn local_is_type(&mut self, e: &ast::Expr) -> bool {
        match &e.kind {
            ExprKind::Tuple(elems) => elems.iter().all(|el| self.local_is_type(&el.value)),
            ExprKind::Paren(inner) => self.local_is_type(inner),
            ExprKind::Ident(_) | ExprKind::Field { .. } => self.local_type_init(e).is_some(),
            ExprKind::ArrayType { .. } | ExprKind::OptionalType(_) if self.is_construction(e) => {
                let module = self.module;
                self.sema.looks_like_type(module, e)
            }
            ExprKind::OptionalType(_)
            | ExprKind::PtrType { .. }
            | ExprKind::SliceType { .. }
            | ExprKind::ManyPtrType { .. }
            | ExprKind::ArrayType { .. }
            | ExprKind::ErrorUnionType { .. }
            | ExprKind::FnType(_)
            | ExprKind::ContType(_) => true,
            _ => false,
        }
    }

    pub fn resolve_type(&mut self, e: &ast::Expr) -> Ty {
        // `@type_of(e)`: the type of `e`, which is checked but not evaluated, so
        // its effects do not count.
        if let ExprKind::Intrinsic { name, args } = &e.kind
            && name.name.as_str() == "type_of"
        {
            let Some([ast::Arg { kind: ast::ArgKind::Positional(x), .. }]) = args.as_deref() else {
                self.error(e.span, "`@type_of(e)` takes one expression");
                return Ty::error();
            };
            let performed = self.performed.len();
            let v = self.expr(x, None);
            self.performed.truncate(performed);
            // The type is needed now, so an untyped literal takes its fallback (§6.1).
            return self.settle(v.ty);
        }
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

/// True if `e` mentions no locals, compile-time parameters or types of parameters: a
/// candidate for evaluation without its function's frame.
fn closed_const(e: &Expr) -> bool {
    let mut ok = !super::generics::has_param(e.ty)
        && !matches!(
            e.kind,
            tir::ExprKind::Local(_)
                | tir::ExprKind::Upvar { .. }
                | tir::ExprKind::ValueParam(_)
                | tir::ExprKind::SizeOf { .. }
                | tir::ExprKind::TypeIs { .. }
                | tir::ExprKind::IntLimit { .. }
                | tir::ExprKind::TypeName { .. }
                | tir::ExprKind::Closure(_)
                | tir::ExprKind::HandlerRef(_)
        );
    if ok {
        e.for_each_child(&mut |c| ok = ok && closed_const(c));
    }
    ok
}

/// The expression that makes statement `s` diverge, with the reason it never completes.
fn stmt_never_cause(s: &Stmt) -> Option<(Span, &'static str)> {
    match s {
        Stmt::Expr(e) | Stmt::Let { init: Some(e), .. } => never_cause(e),
        Stmt::Assign { value, .. } => never_cause(value),
        _ => None,
    }
}

/// Why `e`, an expression of type `never`, never completes: the location of the
/// responsible construct and a sentence about it.
fn never_cause(e: &Expr) -> Option<(Span, &'static str)> {
    use tir::ExprKind as K;
    match &e.kind {
        K::Loop { .. } => Some((e.span, "this loop never ends: it has no `break`")),
        K::Return { .. } => Some((e.span, "this `return` leaves the function")),
        K::Break { .. } => Some((e.span, "this `break` leaves the loop")),
        K::Continue { .. } => Some((e.span, "this `continue` starts the next iteration of the loop")),
        K::Trap { .. } => Some((e.span, "this stops the program")),
        K::Try { .. } => Some((e.span, "this `try` always returns")),
        K::Resume { .. } => Some((e.span, "this `resume` ends the handler clause")),
        K::Cancel(_) => Some((e.span, "this `cancel` ends the handler clause")),
        K::Call { callee: tir::Callee::Perform { .. }, .. } => {
            Some((e.span, "this operation never returns to its caller: its result type is `never`"))
        }
        K::Call { .. } => Some((e.span, "this call never returns: its result type is `never`")),
        K::If { .. } => Some((e.span, "this `if` never completes: each of its branches leaves")),
        K::Match { .. } => Some((e.span, "this `match` never completes: each of its arms leaves")),
        K::With { .. } => Some((e.span, "the body of this `with` never completes")),
        K::Block(b) => match (&b.tail, b.stmts.last()) {
            (Some(t), _) if t.ty.is_never() => never_cause(t),
            (_, Some(last)) => stmt_never_cause(last),
            _ => None,
        },
        _ => None,
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

/// Calls `f` on every expression in `e` (pre-order), including inside statements.
pub fn visit(e: &Expr, f: &mut dyn FnMut(&Expr)) {
    f(e);
    e.for_each_child(&mut |c| visit(c, f));
}

impl<'a, 's> FnCx<'a, 's> {
    /// `when c { … } else { … }` in a body: only the taken branch is checked (§8.9).
    fn when_stmt(&mut self, w: &ast::WhenStmt) -> Expr {
        let module = self.module;
        let Some(taken) = self.sema.when_cond(module, &w.cond) else { return unit(w.span) };
        if taken {
            let b = self.block(&w.then, false);
            return discard(b);
        }
        match &w.else_ {
            Some(ast::WhenElse::Block(b)) => {
                let b = self.block(b, false);
                discard(b)
            }
            Some(ast::WhenElse::When(inner)) => self.when_stmt(inner),
            None => unit(w.span),
        }
    }
}

/// A block used as a statement: its value, if any, is dropped.
fn discard(b: Expr) -> Expr {
    if b.ty == Ty::unit() || b.ty.is_never() || b.ty.is_error() {
        return b;
    }
    let span = b.span;
    Expr { ty: Ty::unit(), kind: tir::ExprKind::Block(Box::new(discard_block(b, span))), span }
}
