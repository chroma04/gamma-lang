//! Function literals (§11.1, §11.6) and nested named functions (§11.8).

use super::func::{FnCx, LocalBinding, NestedFn, error_expr};
use super::generics::has_param;
use super::{FnSig, ParamInfo};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast;
use crate::tir::{self, Expr, FnId};
use crate::types::{CallConv, EffectRef, EffectSet, FnTy, Ty, TyKind};

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

impl<'a, 's> FnCx<'a, 's> {
    /// Checks a function literal used as a value. `name` is set for a nested
    /// named function `name :: fn …`, which may call itself.
    pub fn fn_literal(&mut self, lit: &ast::FnLit, expect: Option<Ty>, name: Option<Symbol>, span: Span) -> Expr {
        if name.is_none() && is_generic(lit) {
            self.error(
                lit.span,
                "a function literal used as a value cannot be generic; bind it with `name :: fn …` (§11.8)",
            );
            return error_expr(span);
        }
        // A literal written for `?^fn(…)` is the function pointer it wraps.
        let target = expect.map(|t| self.infer.resolve(t)).map(|t| match t.kind() {
            TyKind::Optional(inner) if matches!(inner.kind(), TyKind::FnPtr(_)) => *inner,
            _ => t,
        });
        let expected = target.and_then(|t| match t.kind() {
            TyKind::Fn(sig) | TyKind::FnPtr(sig) => Some((**sig).clone()),
            _ => None,
        });
        let wants_pointer = target.is_some_and(|t| matches!(t.kind(), TyKind::FnPtr(_)));
        let params = self.literal_params(lit, expected.as_ref());
        let ret_annot = lit.sig.ret.as_ref().map(|r| self.resolve_type(r));
        let ret_known = ret_annot.or(expected.as_ref().map(|s| s.ret).filter(|r| !has_param(*r)));
        let (mut effects, open) = self.literal_effects(lit, expected.as_ref());
        // The literal passed for an iterator argument (§13.5), checked for being one below.
        let iterator_arg = self.iterator_args.contains(&lit.span);
        // The return type of an `=>` literal without one comes from its body.
        let infer_ret = (ret_known.is_none() || iterator_arg) && matches!(lit.body, ast::FnBody::Expr(_));
        let infer_block = ret_known.is_none() && matches!(lit.body, ast::FnBody::Block(_));
        let ret = ret_known.unwrap_or(Ty::unit());
        let module = self.module;
        let sig = FnSig { module, params: params.clone(), ret, effects: effects.clone(), generics: vec![] };
        let fn_name = name.unwrap_or_else(|| Symbol::intern("<fn>"));
        let fn_id = self.sema.new_fn(fn_name, span, sig, tir::FnKind::Literal);
        if let Some(n) = name {
            // Visible inside its own body, so it can recurse.
            self.declare(n, LocalBinding::NestedFn(fn_id), span);
        }
        self.enter_fn(fn_id, ret, effects.clone());
        if infer_block {
            self.open_ret = Some(None);
        }
        let open_fn = name.filter(|_| infer_ret || infer_block).map(|_| fn_id);
        self.open_fns.extend(open_fn);
        let mut param_ids = vec![];
        for p in &params {
            let id = self.new_local(p.name, p.ty, false, p.span);
            if p.name.as_str() != "_" {
                self.declare(p.name, LocalBinding::Var(id), p.span);
            }
            param_ids.push(id);
        }
        let body = self.literal_body(&lit.body, ret, infer_ret);
        let body = self.declare_temps(0, body);
        if open_fn.is_some() {
            self.open_fns.pop();
        }
        let final_ret = self.ret;
        self.reject_returned_closure(lit, final_ret);
        let (locals, performed, captures) = self.exit_fn();
        if iterator_arg && !performed.iter().any(|(e, _)| Some(e.effect) == self.sema.yield_effect) {
            let ty = self.infer.resolve(final_ret);
            if !ty.is_error() {
                self.error(
                    span,
                    format!("this call is not an iterator (it returns `{}` and yields nothing), but the parameter takes an iterator (§13.5)", ty),
                );
            }
            return error_expr(span);
        }
        for id in &open {
            if !self.infer_open_effect(*id, &performed, &mut effects, fn_id, span) {
                return error_expr(span);
            }
        }
        if infer_ret || infer_block {
            let s = self.sema.sigs.get_mut(&fn_id).unwrap();
            s.ret = final_ret;
            self.sema.prog.fns[fn_id.index()].ret = final_ret;
        }
        let own =
            self.route_performed(performed, &effects, wants_pointer && name.is_none(), name.is_some() && !captures);
        let parent = if captures { self.fn_id } else { None };
        self.sema.prog.fns[fn_id.index()].parent = parent;
        // The value carries the handler context where it is written for its own effects,
        // so the function takes that context like any effectful function.
        let carries_context = !own.is_empty();
        if !own.is_empty() {
            let mut all: Vec<EffectRef> = effects.effects.to_vec();
            all.extend(own);
            self.sema.prog.fns[fn_id.index()].effects = EffectSet::new(all);
        }
        self.nested.push(NestedFn { fn_id, params: param_ids, locals, body });
        // A literal written where a pointer with a calling convention is expected has it.
        let callconv = match &expected {
            Some(s) if wants_pointer && !captures => s.callconv,
            _ => CallConv::C,
        };
        if callconv != CallConv::C {
            self.set_literal_callconv(fn_id, callconv, &effects, span);
        }
        let fty =
            FnTy { params: params.iter().map(|p| p.ty).collect(), ret: final_ret, effects, variadic: false, callconv };
        if captures {
            if wants_pointer {
                self.error(span, "a function literal that uses local variables cannot be a function pointer (§11.7)");
                return error_expr(span);
            }
            if let Some(n) = name {
                self.check_no_self_reference(fn_id, n, span);
            }
            mk(Ty::intern(TyKind::Fn(Box::new(fty))), tir::ExprKind::Closure(fn_id), span)
        } else {
            // A literal that performs effects outside its type carries the handlers where
            // it is written, so it is second-class like one that captures (§11.6).
            let ty = if carries_context || expect.is_some_and(|t| matches!(t.kind(), TyKind::Fn(_))) {
                Ty::intern(TyKind::Fn(Box::new(fty)))
            } else {
                Ty::intern(TyKind::FnPtr(Box::new(fty)))
            };
            mk(ty, tir::ExprKind::FnRef(fn_id), span)
        }
    }

    /// A literal cannot return a second-class function (§11.6).
    fn reject_returned_closure(&mut self, lit: &ast::FnLit, final_ret: Ty) {
        if matches!(self.infer.resolve(final_ret).kind(), TyKind::Fn(_)) {
            let at = match (&lit.sig.ret, &lit.body) {
                (Some(r), _) => r.span,
                (None, ast::FnBody::Expr(e)) => e.span,
                (None, _) => lit.sig.span,
            };
            self.error(
                at,
                "a second-class function cannot be returned; return a function pointer `^fn(…)` instead (§11.6)",
            );
        }
    }

    /// Gives the literal `fn_id` a calling convention other than C, which needs effects at most `{Sys}`.
    fn set_literal_callconv(&mut self, fn_id: FnId, callconv: CallConv, effects: &EffectSet, span: Span) {
        if self.sema.within_sys(effects) {
            self.sema.prog.fns[fn_id.index()].callconv = callconv;
        } else {
            self.error(
                span,
                "a function with a calling convention other than C must have effects at most `{Sys}` (§19.2)",
            );
        }
    }

    /// The parameters of a literal. Types come from annotations, or from the expected function type.
    fn literal_params(&mut self, lit: &ast::FnLit, expected: Option<&FnTy>) -> Vec<ParamInfo> {
        let mut params: Vec<ParamInfo> = vec![];
        for p in &lit.sig.params {
            if p.comptime {
                self.error(p.span, "compile-time parameters are not supported in function literals");
            }
            let annotated = p.ty.as_ref().map(|t| self.resolve_type(t));
            for n in &p.names {
                let i = params.len();
                let ty = match (annotated, expected) {
                    (Some(t), _) => t,
                    // Parts of the expectation that mention type parameters still to be
                    // inferred (a generic call's `K`) say nothing yet.
                    (None, Some(sig)) if i < sig.params.len() && !has_param(sig.params[i]) => sig.params[i],
                    _ => {
                        self.error(
                            n.span,
                            format!("the type of parameter `{}` cannot be inferred here; annotate it", n.name),
                        );
                        Ty::error()
                    }
                };
                params.push(ParamInfo { name: n.name, ty, span: n.span, default: None, generic: None, rest: false });
            }
        }
        if let Some(sig) = expected
            && sig.params.len() != params.len()
        {
            self.error(
                lit.sig.span,
                format!("expected a function with {} parameters, but this one has {}", sig.params.len(), params.len()),
            );
        }
        params
    }

    /// The declared effects of a literal, or the expected ones, and the effects among the latter
    /// that are still open (their arguments mention type parameters to be inferred).
    fn literal_effects(
        &mut self,
        lit: &ast::FnLit,
        expected: Option<&FnTy>,
    ) -> (EffectSet, Vec<crate::types::EffectId>) {
        let effects = match &lit.sig.effects {
            Some(list) => {
                let module = self.module;
                self.sema.resolve_effect_set(module, list)
            }
            None => expected.map(|s| s.effects.clone()).unwrap_or_default(),
        };
        // Expected effects that mention type parameters still to be inferred (a generic
        // call's `Yield(T)`) are taken from what the body performs.
        let open: Vec<crate::types::EffectId> = if lit.sig.effects.is_none() {
            effects.effects.iter().filter(|e| has_param_args(e)).map(|e| e.effect).collect()
        } else {
            vec![]
        };
        if open.is_empty() {
            (effects, open)
        } else {
            (EffectSet::new(effects.effects.iter().filter(|e| !has_param_args(e)).cloned().collect()), open)
        }
    }

    /// Checks the body of a literal whose function has been entered.
    fn literal_body(&mut self, body: &ast::FnBody, ret: Ty, infer_ret: bool) -> Expr {
        match body {
            ast::FnBody::Expr(e) if infer_ret => {
                let b = self.expr(e, None);
                self.ret = b.ty;
                b
            }
            ast::FnBody::Expr(e) => {
                let b = self.expr(e, Some(ret));
                self.coerce(b, ret)
            }
            ast::FnBody::Block(b) => {
                let block = self.block(b, false);
                // The return type is that of the `return`s: `()` if none has a value.
                if let Some(open) = self.open_ret.take() {
                    self.ret = open.unwrap_or(Ty::unit());
                }
                self.finish_block_body(block, b)
            }
        }
    }

    /// Adds to `effects` (and the function's signature) the instance of the open effect `id` that
    /// the body performed. False after an error: the instance is ambiguous.
    fn infer_open_effect(
        &mut self,
        id: crate::types::EffectId,
        performed: &[(EffectRef, Span)],
        effects: &mut EffectSet,
        fn_id: FnId,
        span: Span,
    ) -> bool {
        let mut found: Vec<EffectRef> = vec![];
        for (e, _) in performed {
            if e.effect == id && !found.contains(e) {
                found.push(e.clone());
            }
        }
        if found.len() > 1 {
            let list: Vec<String> = found.iter().map(|e| format!("`{}`", e)).collect();
            self.error(
                span,
                format!(
                    "this performs {}, so the type it is passed at cannot be inferred; write the type argument",
                    list.join(" and ")
                ),
            );
            return false;
        }
        if let Some(e) = found.pop() {
            let mut all = effects.effects.to_vec();
            all.push(e);
            *effects = EffectSet::new(all);
            self.sema.prog.fns[fn_id.index()].effects = effects.clone();
            if let Some(s) = self.sema.sigs.get_mut(&fn_id) {
                s.effects = effects.clone();
            }
        }
        true
    }

    /// Effects in the literal's own set are performed where it is called; the others are
    /// performed where it is written (§11.6, §12.3). Returns those the literal performs outside
    /// its set, which it carries the handlers for.
    fn route_performed(
        &mut self,
        performed: Vec<(EffectRef, Span)>,
        effects: &EffectSet,
        pointer_literal: bool,
        undeclared_is_error: bool,
    ) -> Vec<EffectRef> {
        let mut own = vec![];
        for (e, sp) in performed {
            if !effects.contains(&e) {
                if pointer_literal {
                    let n = e.to_string();
                    self.error(
                        sp,
                        format!("a function pointer performs only the effects in its type, and `{}` is not among them (§11.7)", n),
                    );
                    continue;
                }
                own.push(e.clone());
                if undeclared_is_error {
                    self.undeclared_effect(&e, sp, effects);
                } else {
                    self.performed.push((e, sp));
                }
            }
        }
        own
    }

    pub fn recursive_without_ret(&mut self, name: Symbol, span: Span) {
        self.error(
            span,
            format!("`{}` refers to itself, so it needs a declared return type: write `-> R` (§11.1)", name),
        );
    }

    /// A capturing function cannot refer to itself (§11.8).
    fn check_no_self_reference(&mut self, fn_id: FnId, name: Symbol, span: Span) {
        let refers = self.nested.iter().find(|n| n.fn_id == fn_id).is_some_and(|n| {
            let mut found = false;
            super::func::visit(&n.body, &mut |e| {
                if let tir::ExprKind::FnRef(f) | tir::ExprKind::Call { callee: tir::Callee::Fn(f), .. } = &e.kind
                    && *f == fn_id
                {
                    found = true;
                }
            });
            found
        });
        if refers {
            self.error(
                span,
                format!("`{}` uses local variables of the enclosing function, so it cannot call itself (§11.8)", name),
            );
        }
    }

    /// `name :: fn …` inside a function body.
    pub fn nested_fn_binding(&mut self, name: crate::syntax::ast::Ident, lit: &ast::FnLit, span: Span) -> tir::Stmt {
        if is_generic(lit) {
            return self.nested_generic_fn(name, lit, span);
        }
        let v = self.fn_literal(lit, None, Some(name.name), span);
        match v.kind {
            tir::ExprKind::Closure(_) => {
                // Captures: an immutable local holding a second-class value. Rebind
                // the name (declared as a nested function for the body) to the local.
                let id = self.new_local(name.name, v.ty, false, name.span);
                if let Some(scope) = self.scopes.last_mut()
                    && let Some(entry) = scope.iter_mut().rev().find(|(n, _)| *n == name.name)
                {
                    entry.1 = LocalBinding::Var(id);
                }
                tir::Stmt::Let { local: id, init: Some(v) }
            }
            tir::ExprKind::FnRef(f) => {
                self.sema.local_fns.push((name.name, f));
                tir::Stmt::Expr(super::func::unit(span))
            }
            _ => tir::Stmt::Expr(super::func::unit(span)),
        }
    }
}

/// Whether a function literal has compile-time parameters or constraints (§16.1).
fn is_generic(lit: &ast::FnLit) -> bool {
    !lit.where_.is_empty()
        || lit.sig.params.iter().any(|p| {
            p.comptime
                || p.ty
                    .as_ref()
                    .is_some_and(|t| matches!(&t.kind, ast::ExprKind::Ident(i) if i.name.as_str() == "type"))
        })
}

impl<'a, 's> FnCx<'a, 's> {
    /// A generic nested function `name :: fn(T: type, …)`. It captures nothing (§11.8), so
    /// it is declared like a private top-level function of the module: checked once and
    /// instantiated per call. Its body sees the module's names and its own, not the
    /// enclosing function's locals.
    fn nested_generic_fn(&mut self, name: ast::Ident, lit: &ast::FnLit, span: Span) -> tir::Stmt {
        let synthetic = ast::NodeId(u32::MAX);
        let init = ast::Expr { id: synthetic, kind: ast::ExprKind::Fn(Box::new(lit.clone())), span };
        let binding = ast::Binding {
            id: synthetic,
            names: vec![name],
            ty: None,
            kind: ast::BindingKind::Const,
            init: Box::new(init),
            span,
        };
        let item = std::rc::Rc::new(ast::Item {
            id: synthetic,
            span,
            doc: None,
            attrs: vec![],
            vis: ast::Visibility::Private,
            kind: ast::ItemKind::Binding(binding),
        });
        let module = self.module;
        if let super::DeclValue::Fn(f) = self.sema.declare_fn(module, name, lit, &item) {
            self.sema.nested_generic.insert(f, name);
            self.declare(name.name, LocalBinding::NestedFn(f), name.span);
        }
        tir::Stmt::Expr(super::func::unit(span))
    }
}

/// Whether an effect's arguments mention a type parameter.
fn has_param_args(e: &EffectRef) -> bool {
    e.args.iter().any(|a| matches!(a, crate::types::GenericArg::Type(t) if has_param(*t)))
}
