//! Calls (§11.3), effect operations (§12.1), conversions `T(x)` (§6.5) and the
//! builtin functions `print`, `eprint`, `trap`, `assert`, `expect` and `expect_eq`, with the
//! format templates of `print` and `@write` (§21.6, §21.6.1).

use super::builtins::{BuiltinFn, builtin_fn, builtin_type};
use super::func::{FnCx, LocalBinding, Obligation, error_expr};
use super::infer::VarKind;
use super::{DeclValue, ModuleId, ParamInfo};
use crate::source::Span;
use crate::syntax::ast::{self, ArgKind, ExprKind};
use crate::tir::{self, Callee, Expr, Stmt, WriteSink};
use crate::types::{EffectRef, EffectSet, Ty, TyKind};
use std::rc::Rc;

fn mk(ty: Ty, kind: tir::ExprKind, span: Span) -> Expr {
    Expr { ty, kind, span }
}

/// What a callee expression denotes.
enum CalleeKind {
    Fn(tir::FnId),
    Op(crate::types::EffectId, u32),
    Type(Ty),
    Builtin(BuiltinFn),
    /// A union variant with a payload: `.V(…)` or `U.V(…)`.
    Variant(Ty, ast::Ident),
    Value(Expr),
}

/// The progress of binding the arguments of one call to its parameters.
struct ArgBinding<'p> {
    params: &'p [ParamInfo],
    /// What is called, for messages ("function", "method", …).
    what: &'p str,
    bound: Vec<Option<Expr>>,
    /// The parameters given an argument so far, in the order written.
    written: Vec<usize>,
    /// Named arguments given explicitly (a spread supplying one later is an error).
    explicit: Vec<bool>,
    /// Statements evaluating the arguments in the order written, when that is needed.
    stmts: Vec<Stmt>,
    /// Whether positional arguments may bind each parameter: not one that a named
    /// argument names (§11.3), and never a `@caller_location()` parameter.
    positional: Vec<bool>,
    positional_max: usize,
    next_positional: usize,
    seen_named: bool,
    /// After a spread fails, missing arguments are not reported again.
    spread_failed: bool,
    has_spread: bool,
    /// Whether each argument goes into a temporary first: with spreads, or named arguments
    /// out of parameter order, arguments are still evaluated in the order written (§11.3).
    spreading: bool,
}

impl<'p> ArgBinding<'p> {
    fn new(params: &'p [ParamInfo], args: &[ast::Arg], what: &'p str) -> Self {
        let named_here: Vec<crate::symbol::Symbol> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Named(n, _) => Some(n.name),
                _ => None,
            })
            .collect();
        let positional: Vec<bool> = params.iter().map(|p| !p.is_site() && !named_here.contains(&p.name)).collect();
        let mut order = vec![];
        for a in args {
            match &a.kind {
                ArgKind::Positional(_) => order.push(order.len()),
                ArgKind::Named(n, _) => order.extend(params.iter().position(|p| p.name == n.name)),
                ArgKind::Spread(_) => {}
            }
        }
        let reordered = order.windows(2).any(|w| w[0] > w[1]);
        let has_spread = args.iter().any(|a| matches!(a.kind, ArgKind::Spread(_)));
        ArgBinding {
            params,
            what,
            bound: vec![None; params.len()],
            written: vec![],
            explicit: vec![false; params.len()],
            stmts: vec![],
            positional_max: positional.iter().filter(|&&p| p).count(),
            positional,
            next_positional: 0,
            seen_named: false,
            spread_failed: false,
            has_spread,
            spreading: reordered || has_spread,
        }
    }

    /// The first parameter at or after `i` that positional arguments may bind.
    fn skip_named(&self, mut i: usize) -> usize {
        while i < self.params.len() && !self.positional[i] {
            i += 1;
        }
        i
    }
}

impl<'a, 's> FnCx<'a, 's> {
    pub fn call(&mut self, callee: &ast::Expr, args: &[ast::Arg], expect: Option<Ty>, span: Span) -> Expr {
        // `T(5)` with a type parameter `T`: the untyped constant is typed at `T`, as
        // `x: T = 5` would, and checked against each instantiation (§16.1).
        if let ExprKind::Ident(id) = &callee.kind
            && self.lookup_local(id.name).is_none()
            && let Some(&(_, t)) = self.sema.type_params.iter().rev().find(|(n, _)| *n == id.name)
            && let [ast::Arg { kind: ast::ArgKind::Positional(a), .. }] = args
            && let Some(v) = self.try_untyped(a)
        {
            return match v {
                Ok(super::untyped::Untyped::Bool(_)) => {
                    self.error(a.span, format!("a boolean constant cannot be converted to `{}`", id.name));
                    error_expr(span)
                }
                Ok(v) => {
                    let lit = self.untyped_literal(&v, a.span);
                    let mut r = self.coerce(lit, t);
                    r.span = span;
                    r
                }
                Err(()) => error_expr(span),
            };
        }
        // `E.op(args)`: an operation named through its effect (§12.1).
        if let ExprKind::Field { base, name } = &callee.kind
            && let Some((eff, inst)) = self.effect_named(base)
        {
            let ops = &self.sema.effects[&eff].ops;
            return match ops.iter().position(|o| o.name == name.name) {
                Some(i) => match inst {
                    Some(r) => self.perform_op_as(r, i as u32, args, span),
                    None => self.perform_op(eff, i as u32, args, span),
                },
                None => {
                    let e = self.sema.effects[&eff].name;
                    self.error(name.span, format!("the effect `{}` has no operation `{}`", e, name.name));
                    error_expr(span)
                }
            };
        }
        if let ExprKind::Field { base, name } = &callee.kind
            && self.module_of(base).is_none()
        {
            return self.method_call(base, *name, args, span);
        }
        let Some(kind) = self.classify_callee(callee, expect) else { return error_expr(span) };
        match kind {
            CalleeKind::Fn(f) => {
                if !self.sema.sigs[&f].generics.is_empty() {
                    return self.generic_call(f, args, span);
                }
                let sig = self.sema.sigs[&f].clone();
                let n = sig.params.len();
                let variadic = self.sema.variadic_fns.contains(&f) && args.len() > n;
                let (fixed, extra) = if variadic { args.split_at(n) } else { (args, &[][..]) };
                let (mut args, pre) = self.bind_args(&sig.params, sig.module, fixed, span, "function");
                for a in extra {
                    args.push(self.variadic_arg(a));
                }
                self.record_effects(&sig.effects, span);
                let call = mk(sig.ret, tir::ExprKind::Call { callee: Callee::Fn(f), args }, span);
                self.finish_call(pre, call, false)
            }
            CalleeKind::Op(eff, op) => self.perform_op(eff, op, args, span),
            CalleeKind::Type(t) => self.conversion(t, args, span),
            CalleeKind::Builtin(b) => self.builtin_call(b, args, expect, span),
            CalleeKind::Variant(u, name) => self.construct_variant(u, name, Some(args), span),
            CalleeKind::Value(v) => self.call_value(v, args, span),
        }
    }

    /// The effect that `e` names (`E`, `m.E`, or an instantiation `E(T)` or alias of one),
    /// unless a local shadows it; with the instantiation if `e` gives one.
    fn effect_named(&mut self, e: &ast::Expr) -> Option<(crate::types::EffectId, Option<crate::types::EffectRef>)> {
        if let ExprKind::Call { callee, .. } = &e.kind {
            self.effect_named(callee)?;
            let module = self.module;
            let r = self.sema.resolve_effect_ref(module, e)?;
            return Some((r.effect, Some(r)));
        }
        let d = match &e.kind {
            ExprKind::Ident(id) if self.lookup_local(id.name).is_none() => {
                let module = self.module;
                self.sema.lookup_module_name(module, id.name)?
            }
            ExprKind::Field { .. } => self.qualified_decl(e)??,
            _ => return None,
        };
        match self.sema.resolve_decl(d) {
            DeclValue::Effect(eff) => Some((eff, None)),
            DeclValue::EffectInst(r) => Some((r.effect, Some(r))),
            _ => None,
        }
    }

    /// Calls a function pointer or second-class function value.
    pub fn call_value(&mut self, v: Expr, args: &[ast::Arg], span: Span) -> Expr {
        {
            {
                let t = self.infer.resolve(v.ty);
                let (TyKind::FnPtr(sig) | TyKind::Fn(sig)) = t.kind() else {
                    if !t.is_error() {
                        self.error(v.span, format!("cannot call a value of type `{}`", t));
                    }
                    return error_expr(span);
                };
                let params: Vec<ParamInfo> = sig
                    .params
                    .iter()
                    .map(|&ty| ParamInfo {
                        name: crate::symbol::Symbol::intern("_"),
                        ty,
                        span,
                        default: None,
                        generic: None,
                        rest: false,
                    })
                    .collect();
                let module = self.module;
                let (args, pre) = self.bind_args(&params, module, args, span, "function");
                self.record_effects(&sig.effects, span);
                let call = mk(sig.ret, tir::ExprKind::Call { callee: Callee::Indirect(Box::new(v)), args }, span);
                self.finish_call(pre, call, false)
            }
        }
    }

    /// What a call to the declaration `d` (written as `callee`) does.
    fn callee_of(&mut self, d: super::DeclId, name: crate::symbol::Symbol, callee: &ast::Expr) -> Option<CalleeKind> {
        match self.sema.resolve_decl(d) {
            DeclValue::Fn(f) => Some(CalleeKind::Fn(f)),
            DeclValue::Op(e, i) => Some(CalleeKind::Op(e, i)),
            DeclValue::Type(t) => Some(CalleeKind::Type(t)),
            DeclValue::Error => None,
            DeclValue::Const { .. } => Some(CalleeKind::Value(self.expr(callee, None))),
            _ => {
                self.error(callee.span, format!("`{}` cannot be called", name));
                None
            }
        }
    }

    fn classify_callee(&mut self, callee: &ast::Expr, expect: Option<Ty>) -> Option<CalleeKind> {
        if let ExprKind::DotIdent(name) = &callee.kind {
            return match expect.map(|t| self.infer.resolve(t)) {
                Some(t) => Some(CalleeKind::Variant(strip_wrappers(t), *name)),
                None => {
                    self.error(callee.span, format!("cannot infer the union type of `.{}` here", name.name));
                    None
                }
            };
        }
        if let ExprKind::Field { name, .. } = &callee.kind
            && let Some(d) = self.qualified_decl(callee)
        {
            return self.callee_of(d?, name.name, callee);
        }
        if let ExprKind::Field { base, name } = &callee.kind
            && let Some(t) = self.type_of_name(base)
        {
            return Some(CalleeKind::Variant(t, *name));
        }
        if let ExprKind::Ident(id) = &callee.kind {
            if self.lookup_local(id.name).is_none()
                && let Some(&(_, t)) = self.sema.type_params.iter().rev().find(|(n, _)| *n == id.name)
            {
                self.error(
                    callee.span,
                    format!(
                        "`{}(x)` would be a conversion to a type parameter, which is not lossless for every type; use `x.trunc({})`, `x.sat({})` or `x.checked({})`",
                        id.name, id.name, id.name, id.name
                    ),
                );
                let _ = t;
                return None;
            }
            if self.lookup_local(id.name).is_none() {
                let module = self.module;
                if let Some(d) = self.sema.lookup_module_name(module, id.name) {
                    self.sema.check_deprecated(d, id.span);
                    return self.callee_of(d, id.name, callee);
                }
                if let Some(b) = builtin_fn(id.name.as_str()) {
                    return Some(CalleeKind::Builtin(b));
                }
                if let Some(t) = builtin_type(id.name.as_str()) {
                    return Some(CalleeKind::Type(t));
                }
            } else if let Some(LocalBinding::NestedFn(f)) = self.lookup_local(id.name) {
                // A nested named function is called directly (and may be generic).
                let f = *f;
                if self.open_fns.contains(&f) {
                    self.recursive_without_ret(id.name, id.span);
                    return None;
                }
                return Some(CalleeKind::Fn(f));
            } else if let Some(LocalBinding::Type(t)) = self.lookup_local(id.name) {
                // A local alias: a conversion or construction, as with the type itself.
                let t = *t;
                return if t.is_error() { None } else { Some(CalleeKind::Type(t)) };
            } else if let Some(LocalBinding::Label) = self.lookup_local(id.name) {
                self.error(callee.span, format!("`{}` is a label", id.name));
                return None;
            }
        }
        // `(i32, string)(…)` constructs a tuple (§7.11).
        let module = self.module;
        if is_type_syntax(callee)
            || (matches!(&callee.kind, ExprKind::Tuple(es) if !es.is_empty())
                && self.sema.looks_like_type(module, callee))
        {
            let t = self.resolve_type(callee);
            return if t.is_error() { None } else { Some(CalleeKind::Type(t)) };
        }
        if let ExprKind::Call { .. } = &callee.kind {
            if let Some(t) = self.type_of_name(callee) {
                return Some(CalleeKind::Type(t));
            }
            // `Heap(T, N)(…)` with a wrong type application: the application's error
            // is the only one.
            if self.sema.looks_like_type(module, callee) {
                let t = self.resolve_type(callee);
                if t.is_error() {
                    return None;
                }
            }
        }
        Some(CalleeKind::Value(self.expr(callee, None)))
    }

    /// Binds call arguments to parameters (§11.3) and coerces each to its type. With
    /// spread arguments (§11.4), every argument is evaluated into a temporary, in the
    /// order written, by the returned statements, which must run before the call
    /// (see [`FnCx::finish_call`]).
    pub fn bind_args(
        &mut self,
        params: &[ParamInfo],
        callee_module: ModuleId,
        args: &[ast::Arg],
        span: Span,
        what: &str,
    ) -> (Vec<Expr>, Vec<Stmt>) {
        let named_names: Vec<crate::symbol::Symbol> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Named(n, _) => Some(n.name),
                _ => None,
            })
            .collect();
        let packed =
            pack_rest_args(params, |i| !params[i].is_site() && !named_names.contains(&params[i].name), args, span);
        let packed_args;
        let args = match packed {
            Some(Ok(v)) => {
                packed_args = v;
                &packed_args[..]
            }
            Some(Err((sp, msg))) => {
                self.error(sp, msg);
                return (params.iter().map(|_| error_expr(span)).collect(), vec![]);
            }
            None => args,
        };
        let mut st = ArgBinding::new(params, args, what);
        for arg in args {
            match &arg.kind {
                ArgKind::Positional(e) => self.bind_positional(&mut st, e, arg.span),
                ArgKind::Named(name, e) => self.bind_named(&mut st, *name, e),
                ArgKind::Spread(e) => self.bind_spread(&mut st, e),
            }
        }
        let out = self.fill_unbound(st.bound, params, callee_module, st.spread_failed, what, span);
        (out, st.stmts)
    }

    /// Binds the next positional argument `e`.
    fn bind_positional(&mut self, st: &mut ArgBinding, e: &ast::Expr, arg_span: Span) {
        if st.seen_named {
            self.error(arg_span, "positional arguments must come before a spread that supplies names");
        }
        st.next_positional = st.skip_named(st.next_positional);
        let i = st.next_positional;
        if i >= st.params.len() {
            self.error(arg_span, format!("too many arguments: the {} takes {}", st.what, st.positional_max));
            let _ = self.expr(e, None);
            return;
        }
        let mut v = self.arg_value(e, st.params[i].ty);
        if st.spreading {
            v = self.bind_temp(v, &mut st.stmts);
        }
        self.place_iter_pre(&mut st.bound, &mut st.written, i, &mut st.stmts);
        st.bound[i] = Some(v);
        st.next_positional += 1;
    }

    /// Binds the named argument `name: e`.
    fn bind_named(&mut self, st: &mut ArgBinding, name: ast::Ident, e: &ast::Expr) {
        let Some(i) = st.params.iter().position(|p| p.name == name.name) else {
            self.error(name.span, format!("the {} has no parameter named `{}`", st.what, name.name));
            let _ = self.expr(e, None);
            return;
        };
        if st.explicit[i] || (st.bound[i].is_some() && !st.has_spread) {
            self.error(name.span, format!("parameter `{}` is given more than once", name.name));
        }
        st.explicit[i] = true;
        // A later explicit named argument overrides one supplied by a spread.
        let mut v = self.arg_value(e, st.params[i].ty);
        if st.spreading {
            v = self.bind_temp(v, &mut st.stmts);
        }
        self.place_iter_pre(&mut st.bound, &mut st.written, i, &mut st.stmts);
        st.bound[i] = Some(v);
    }

    /// Binds the parts of the spread argument `..e`; the first part that fits no parameter ends it.
    fn bind_spread(&mut self, st: &mut ArgBinding, e: &ast::Expr) {
        let Some(parts) = self.spread_parts(e, &mut st.stmts) else {
            st.spread_failed = true;
            return;
        };
        let params = st.params;
        for (name, proj) in parts {
            let i = match name {
                // An unnamed tuple's elements are positional arguments.
                None => {
                    if st.seen_named {
                        self.error(e.span, "positional arguments must come before a spread that supplies names");
                        st.spread_failed = true;
                        break;
                    }
                    st.next_positional = st.skip_named(st.next_positional);
                    if st.next_positional >= params.len() {
                        self.error(e.span, format!("too many arguments: the {} takes {}", st.what, st.positional_max));
                        st.spread_failed = true;
                        break;
                    }
                    st.next_positional += 1;
                    st.next_positional - 1
                }
                Some(n) => {
                    st.seen_named = true;
                    let Some(i) = params.iter().position(|p| p.name == n) else {
                        self.error(
                            e.span,
                            format!("the {} has no parameter named `{}` (supplied by this spread)", st.what, n),
                        );
                        st.spread_failed = true;
                        break;
                    };
                    if st.explicit[i] || st.bound[i].is_some() {
                        self.error(e.span, format!("parameter `{}` is given more than once", n));
                        st.spread_failed = true;
                        break;
                    }
                    i
                }
            };
            st.bound[i] = Some(self.coerce(proj, params[i].ty));
        }
    }

    /// The bound arguments in parameter order; a parameter left without one takes its default, or
    /// is reported missing (unless a failed spread already explains it).
    fn fill_unbound(
        &mut self,
        bound: Vec<Option<Expr>>,
        params: &[ParamInfo],
        callee_module: ModuleId,
        spread_failed: bool,
        what: &str,
        span: Span,
    ) -> Vec<Expr> {
        let mut out = vec![];
        for (i, b) in bound.into_iter().enumerate() {
            match b {
                Some(v) => out.push(v),
                None => {
                    let p = &params[i];
                    match &p.default {
                        Some(d) => {
                            let d = d.clone();
                            let v = self.param_default(callee_module, &d, p.ty, p.is_site(), span);
                            match v {
                                Some(v) => out.push(v),
                                None => out.push(error_expr(span)),
                            }
                        }
                        None => {
                            if !spread_failed {
                                if p.name.as_str() == "_" {
                                    self.error(span, format!("missing argument: the {} takes {}", what, params.len()));
                                } else {
                                    self.error(span, format!("missing argument for parameter `{}`", p.name));
                                }
                            }
                            out.push(error_expr(span));
                        }
                    }
                }
            }
        }
        out
    }

    /// The value of the default `d` of a parameter of type `ty` declared in `module`,
    /// for a call at `span` that omits the argument. A default is a compile-time value
    /// (§17.2) computed once; `@caller_location()` is the call's location.
    pub(super) fn param_default(
        &mut self,
        module: super::ModuleId,
        d: &ast::Expr,
        ty: Ty,
        site: bool,
        span: Span,
    ) -> Option<Expr> {
        if !site {
            return self.sema.default_value(module, d, ty);
        }
        let location = self.call_site(span);
        let saved = self.sema.caller_site.replace(location);
        let v = self.sema.check_const_expr(module, d, Some(ty));
        self.sema.caller_site = saved;
        v
    }

    /// `call` preceded by the statements that evaluate its arguments (from
    /// [`FnCx::bind_args`]). The callee value of an indirect call, and with `receiver`
    /// the first argument, were evaluated before the arguments, so they move into
    /// temporaries ahead of them.
    pub fn finish_call(&mut self, pre: Vec<Stmt>, mut call: Expr, receiver: bool) -> Expr {
        if pre.is_empty() {
            return call;
        }
        let mut stmts = vec![];
        if let tir::ExprKind::Call { callee, args } = &mut call.kind {
            if let Callee::Indirect(c) = callee {
                let sp = c.span;
                let v = std::mem::replace(&mut **c, error_expr(sp));
                **c = self.bind_temp(v, &mut stmts);
            }
            if receiver && let Some(a) = args.first_mut() {
                let sp = a.span;
                let v = std::mem::replace(a, error_expr(sp));
                *a = self.bind_temp(v, &mut stmts);
            }
        }
        stmts.extend(pre);
        let (ty, span) = (call.ty, call.span);
        mk(ty, tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(call), span })), span)
    }

    /// For an argument written as a call where the parameter is an iterator parameter
    /// (§13.5), the function literal `fn() => call` that is passed instead.
    pub(super) fn iterator_arg(&mut self, e: &ast::Expr, ty: Ty) -> Option<ast::Expr> {
        let TyKind::Fn(sig) = ty.kind() else { return None };
        let yield_eff = self.sema.yield_effect?;
        if !sig.params.is_empty() || !sig.effects.effects.iter().any(|x| x.effect == yield_eff) {
            return None;
        }
        let mut inner = e;
        while let ExprKind::Paren(x) = &inner.kind {
            inner = x;
        }
        if !matches!(inner.kind, ExprKind::Call { .. }) {
            return None;
        }
        // The callee's arguments and receiver are evaluated here, once; the literal only
        // makes the call (§13.5).
        let call = self.hoist_iterator_call(inner);
        let sig = ast::FnSig { params: vec![], variadic: false, ret: None, effects: None, span: e.span };
        let lit = ast::FnLit { sig, where_: vec![], body: ast::FnBody::Expr(call), span: e.span };
        self.iterator_args.push(e.span);
        Some(ast::Expr { id: e.id, kind: ExprKind::Fn(Box::new(lit)), span: e.span })
    }

    /// After the argument for parameter `i` was checked: if it is an iterator whose call
    /// arguments are evaluated first (§13.5), the arguments written before it are evaluated
    /// into temporaries, then those statements are added to `stmts`; this keeps the
    /// left-to-right order.
    fn place_iter_pre(
        &mut self,
        bound: &mut [Option<Expr>],
        written: &mut Vec<usize>,
        i: usize,
        stmts: &mut Vec<Stmt>,
    ) {
        if !self.iter_pre.is_empty() {
            for &j in written.iter() {
                if let Some(v) = bound[j].take() {
                    bound[j] = Some(self.bind_temp(v, stmts));
                }
            }
            stmts.append(&mut self.iter_pre);
        }
        written.push(i);
    }

    /// `f(a1, …, an)` rewritten so that `a1 … an` and a method call's receiver are
    /// evaluated now, into hidden locals (statements in `iter_pre`), leaving a call
    /// of those locals for the iterator literal to make each time it runs.
    fn hoist_iterator_call(&mut self, call: &ast::Expr) -> ast::Expr {
        let ExprKind::Call { callee, args, trailing_comma } = &call.kind else { return call.clone() };
        let callee = match &callee.kind {
            ExprKind::Field { base, name } if !self.denotes_type(base) => {
                let base = self.hoist_iter_value(base, true);
                ast::Expr {
                    id: callee.id,
                    kind: ExprKind::Field { base: Box::new(base), name: *name },
                    span: callee.span,
                }
            }
            _ => (**callee).clone(),
        };
        let args = args
            .iter()
            .map(|a| {
                let kind = match &a.kind {
                    ast::ArgKind::Positional(e) => ast::ArgKind::Positional(self.hoist_iter_value(e, false)),
                    ast::ArgKind::Named(n, e) => ast::ArgKind::Named(*n, self.hoist_iter_value(e, false)),
                    ast::ArgKind::Spread(e) => ast::ArgKind::Spread(e.clone()),
                };
                ast::Arg { kind, span: a.span }
            })
            .collect();
        ast::Expr {
            id: call.id,
            kind: ExprKind::Call { callee: Box::new(callee), args, trailing_comma: *trailing_comma },
            span: call.span,
        }
    }

    /// Whether `e` is a constant expression or names a global: evaluating it again is
    /// unobservable, and it may depend on the type expected where it is written.
    fn iter_stable(&mut self, e: &ast::Expr) -> bool {
        match &e.kind {
            ExprKind::Int(_)
            | ExprKind::Float(_)
            | ExprKind::Str(_)
            | ExprKind::Bool(_)
            | ExprKind::Null
            | ExprKind::Uninit
            | ExprKind::Underscore
            | ExprKind::DotIdent(_) => true,
            ExprKind::Ident(id) => self.lookup_local(id.name).is_none(),
            ExprKind::Paren(x) => self.iter_stable(x),
            ExprKind::Field { base, .. } => self.iter_stable(base),
            ExprKind::Unary { op, operand } => {
                matches!(op, ast::UnOp::Neg | ast::UnOp::BitNot | ast::UnOp::Not) && self.iter_stable(operand)
            }
            ExprKind::Binary { lhs, rhs, .. } => self.iter_stable(lhs) && self.iter_stable(rhs),
            // `.V(args)`: a union value built from its context.
            ExprKind::Call { callee, args, .. } => {
                matches!(callee.kind, ExprKind::DotIdent(_))
                    && args.iter().all(|a| match &a.kind {
                        ast::ArgKind::Positional(x) | ast::ArgKind::Named(_, x) => self.iter_stable(x),
                        ast::ArgKind::Spread(_) => false,
                    })
            }
            _ => self.denotes_type(e),
        }
    }

    /// The expression that stands for `e` in the literal made for an iterator argument:
    /// `e` is evaluated now. A place (`receiver`, or an array, which would otherwise be
    /// copied) stays a place, with the pointers and indexes in it evaluated now; any other
    /// value goes into a hidden local.
    fn hoist_iter_value(&mut self, e: &ast::Expr, receiver: bool) -> ast::Expr {
        let synth = |kind: ExprKind| ast::Expr { id: ast::NodeId(u32::MAX), kind, span: e.span };
        if self.iter_stable(e) {
            return e.clone();
        }
        let place = match &e.kind {
            ExprKind::Paren(x) => return self.hoist_iter_value(x, receiver),
            ExprKind::Ident(_) => Some(e.clone()),
            ExprKind::Field { base, name } => {
                let base = self.hoist_iter_value(base, true);
                Some(synth(ExprKind::Field { base: Box::new(base), name: *name }))
            }
            ExprKind::TupleField { base, index, index_span } => {
                let base = self.hoist_iter_value(base, true);
                Some(synth(ExprKind::TupleField { base: Box::new(base), index: *index, index_span: *index_span }))
            }
            ExprKind::Index { base, index } => {
                let base = self.hoist_iter_value(base, true);
                let index = self.hoist_iter_value(index, false);
                Some(synth(ExprKind::Index { base: Box::new(base), index: Box::new(index) }))
            }
            ExprKind::Deref(x) => {
                let x = self.hoist_iter_value(x, false);
                Some(synth(ExprKind::Deref(Box::new(x))))
            }
            ExprKind::Unary { op: ast::UnOp::AddrOf, operand } => {
                let operand = self.hoist_iter_value(operand, true);
                return synth(ExprKind::Unary { op: ast::UnOp::AddrOf, operand: Box::new(operand) });
            }
            _ => None,
        };
        let expr = place.as_ref().unwrap_or(e);
        let mark = self.performed.len();
        let mut v = self.expr(expr, None);
        // An iterator passed on (`filtered(fib(), f)`): not a value; its own arguments are
        // evaluated now too, and the call stays where it is.
        if let ExprKind::Call { .. } = &expr.kind
            && self.iterator_calls.contains(&expr.span)
        {
            self.performed.truncate(mark);
            return self.hoist_iterator_call(expr);
        }
        let ty = self.infer.resolve(v.ty);
        let keep_place = place.is_some() && (receiver || matches!(ty.kind(), TyKind::Array { .. }));
        // A second-class function cannot be stored, and an untyped constant takes its type
        // from the parameter it is passed for: both are evaluated where they are used.
        if ty.is_error() || ty.has_infer() || matches!(ty.kind(), TyKind::Fn(_)) || keep_place {
            return expr.clone();
        }
        let mut pre = std::mem::take(&mut self.iter_pre);
        self.lift_temps(&mut v, &mut pre);
        let name = crate::symbol::Symbol::intern(&format!("<iter{}>", self.locals.len()));
        let local = self.new_local(name, ty, true, e.span);
        pre.push(Stmt::Let { local, init: Some(v) });
        self.iter_pre = pre;
        self.declare(name, LocalBinding::Var(local), e.span);
        synth(ExprKind::Ident(ast::Ident { name, span: e.span }))
    }

    pub(super) fn arg_value(&mut self, e: &ast::Expr, ty: Ty) -> Expr {
        if let Some(lit) = self.iterator_arg(e, ty) {
            // Checking the literal's own call must not place the statements made above.
            let pre = std::mem::take(&mut self.iter_pre);
            let v = self.arg_value(&lit, ty);
            self.iter_pre = pre;
            return v;
        }
        if let ExprKind::Uninit = e.kind {
            if !self.is_plain_data(ty) {
                self.error(e.span, super::func::uninit_message(ty));
            }
            return mk(ty, tir::ExprKind::Uninit, e.span);
        }
        self.temp_ref_arg = is_const_ref_arg(e, ty);
        let v = self.expr(e, Some(ty));
        self.temp_ref_arg = false;
        if self.iterator_call_as_value(e, &v, ty) {
            return error_expr(e.span);
        }
        let v = self.array_arg_as_slice(v, ty);
        self.coerce(v, ty)
    }

    /// `T(x)`: a lossless conversion (§6.5).
    fn conversion(&mut self, t: Ty, args: &[ast::Arg], span: Span) -> Expr {
        // A `#c_union` is built from exactly one of its fields (§5.11.1).
        if let Some(fields) = self.sema.c_union_fields(t) {
            let fields = fields.to_vec();
            let [ast::Arg { kind: ArgKind::Named(n, x), .. }] = args else {
                self.error(span, format!("`{}(field: value)` names exactly one field of the union", t));
                return error_expr(span);
            };
            let Some(f) = fields.iter().find(|f| f.name == n.name) else {
                self.error(n.span, format!("`{}` has no field `{}`", t, n.name));
                return error_expr(span);
            };
            let v = self.expr(x, Some(f.ty));
            let v = self.coerce(v, f.ty);
            return mk(t, tir::ExprKind::Aggregate(vec![v]), span);
        }
        // `Mode(u)`: a `#bits` struct from its underlying integer (§5.10.5).
        let one_positional = matches!(args, [ast::Arg { kind: ArgKind::Positional(_), .. }]);
        // Construction (§7.11) for structs, tuples, arrays and optionals.
        if self.sema.struct_fields(t).is_some() && !(one_positional && self.sema.bits_struct_ty(t).is_some()) {
            return self.construct_struct(t, args, span);
        }
        match t.kind() {
            TyKind::Tuple { elems, names } => return self.construct_tuple(t, elems, names.as_deref(), args, span),
            TyKind::Array { elem, kind: crate::types::ArrayKind::Enum(index), .. } => {
                return self.construct_enum_array(t, *index, *elem, args, span);
            }
            TyKind::Array { len, elem, .. } => return self.construct_array(Some(t), Some(*len), *elem, args, span),
            TyKind::ParamArray { elem, .. } => return self.construct_param_array(t, *elem, args, span),
            TyKind::Optional(inner) => return self.construct_optional(t, *inner, args, span),
            _ => {}
        }
        let [ast::Arg { kind: ArgKind::Positional(x), .. }] = args else {
            self.error(span, format!("construction of `{}` is not supported yet; a conversion takes one argument", t));
            return error_expr(span);
        };
        // `Mode(u)`: the integer is of the underlying type, so a literal takes it.
        if let Some(u) = self.sema.bits_struct_ty(t) {
            let mut v = self.expr(x, Some(u));
            if self.infer.var_kind(v.ty).is_some() {
                v = self.coerce(v, u);
            }
            // Only a plain bit struct converts from its integer, a literal included.
            self.obligations.push(Obligation::Conversion { from: v.ty, to: t, span });
            return mk(t, tir::ExprKind::Builtin { op: tir::Builtin::Convert, args: vec![v] }, span);
        }
        // Only an untyped literal (or arithmetic on literals) takes its type from the target;
        // anything else is typed on its own, so that the target does not reach into the
        // branches of `f32(~(if c { a } else { b }))` with `a, b: i16`.
        let v = self.expr(x, if is_literal_tree(x) { Some(t) } else { None });
        // An untyped literal simply takes the type (§16.1: `T(300)`), and so does a
        // variable of literal type converted to an integer type. A float type is taken
        // by literals only: `f64(i)` converts `i` from the integer type it ends up with.
        if self.infer.var_kind(v.ty).is_some() && (t.is_integer() || (t.is_float() && is_literal(&v))) {
            if self.infer.unify(v.ty, t).is_ok() {
                if !is_literal(&v) {
                    self.infer.note_fixed_by(v.ty, span);
                }
                return v;
            }
            // A variable already restricted to other types, such as an index (§6.1):
            // an ordinary conversion from the type it ends up with.
            if !is_literal(&v) || !self.infer.var_kind(v.ty).is_some_and(|k| k == VarKind::Float) {
                self.obligations.push(Obligation::Conversion { from: v.ty, to: t, span });
                return mk(t, tir::ExprKind::Builtin { op: tir::Builtin::Convert, args: vec![v] }, span);
            }
            self.error(x.span, format!("a float literal cannot have the integer type `{}`", t));
            return v;
        }
        // A value that coerces to `T` implicitly (§6.4) converts by that coercion:
        // `string(buf)` with `buf: []u8`.
        let vt = self.infer.resolve(v.ty);
        if !vt.has_infer() && vt != t && self.coercion_steps(vt, t).is_some() {
            return self.coerce(v, t);
        }
        // `T(x)` of a `T` is a copy of the value (not a place).
        if !vt.has_infer() && vt == t && !vt.is_error() {
            return mk(t, tir::ExprKind::Coerce { kind: tir::Coercion::TupleRename, expr: Box::new(v) }, span);
        }
        self.obligations.push(Obligation::Conversion { from: v.ty, to: t, span });
        mk(t, tir::ExprKind::Builtin { op: tir::Builtin::Convert, args: vec![v] }, span)
    }

    /// `?T(x)` holds `x`; `?T(null)` is the absent value (§7.11).
    pub fn construct_optional(&mut self, t: Ty, inner: Ty, args: &[ast::Arg], span: Span) -> Expr {
        let [ast::Arg { kind: ArgKind::Positional(x), .. }] = args else {
            self.error(span, format!("`{}(…)` takes exactly one value", t));
            return error_expr(span);
        };
        if let ExprKind::Null = x.kind {
            return mk(t, tir::ExprKind::Null, span);
        }
        let v = self.expr(x, Some(inner));
        let v = self.coerce(v, inner);
        mk(t, tir::ExprKind::Coerce { kind: tir::Coercion::WrapOptional, expr: Box::new(v) }, span)
    }

    fn builtin_call(&mut self, b: BuiltinFn, args: &[ast::Arg], _expect: Option<Ty>, span: Span) -> Expr {
        match b {
            BuiltinFn::Print | BuiltinFn::Eprint => self.print(b == BuiltinFn::Print, args, span),
            BuiltinFn::Trap => {
                let msg = match args {
                    [] => mk(Ty::string(), tir::ExprKind::Str(Rc::from(&b"trap"[..])), span),
                    [ast::Arg { kind: ArgKind::Positional(m), .. }] => {
                        let v = self.expr(m, Some(Ty::string()));
                        self.coerce(v, Ty::string())
                    }
                    _ => {
                        self.error(span, "`trap` takes one optional message argument");
                        return error_expr(span);
                    }
                };
                mk(Ty::never(), tir::ExprKind::Trap { message: Box::new(msg) }, span)
            }
            BuiltinFn::ExpectEq => self.expect_eq(args, span),
            BuiltinFn::Assert => {
                let what = "assert";
                let (cond, msg) = match args {
                    [ast::Arg { kind: ArgKind::Positional(c), .. }] => (c, None),
                    [ast::Arg { kind: ArgKind::Positional(c), .. }, ast::Arg { kind: ArgKind::Positional(m), .. }] => {
                        (c, Some(m))
                    }
                    _ => {
                        self.error(span, format!("`{}` takes a condition and an optional message", what));
                        return error_expr(span);
                    }
                };
                let c = self.expr(cond, Some(Ty::bool()));
                let c = self.expect_bool(c);
                let m = match msg {
                    Some(m) => {
                        let v = self.expr(m, Some(Ty::string()));
                        self.coerce(v, Ty::string())
                    }
                    None => mk(Ty::string(), tir::ExprKind::Str(Rc::from(&b"assertion failed"[..])), span),
                };
                mk(Ty::unit(), tir::ExprKind::Builtin { op: tir::Builtin::Assert, args: vec![c, m] }, span)
            }
        }
    }

    /// `expect_eq(actual, expected)` (§21.6): unless the two are equal by `is`, writes both
    /// values into the failure report and fails the current test, performing `Test`.
    fn expect_eq(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        let [ast::Arg { kind: ArgKind::Positional(a), .. }, ast::Arg { kind: ArgKind::Positional(b), .. }] = args
        else {
            self.error(span, "`expect_eq` takes the actual value and the expected one");
            return error_expr(span);
        };
        let Some(effect) = self.sema.prog.ambient.test else {
            return error_expr(span);
        };
        let l = self.expr(a, None);
        let r = self.expr(b, Some(l.ty));
        let (l, r) = self.equalize(l, r, span);
        let (lt, rt) = (self.settle(l.ty, a.span), self.settle(r.ty, b.span));
        let (x, y) = (self.temp(lt, a.span), self.temp(rt, b.span));
        let local = |id, ty| mk(ty, tir::ExprKind::Local(id), span);
        let differ = mk(
            Ty::bool(),
            tir::ExprKind::Eq { lhs: Box::new(local(x, lt)), rhs: Box::new(local(y, rt)), negated: true },
            span,
        );
        let eref = EffectRef { effect, args: Box::new([]) };
        self.record_effects(&EffectSet::new(vec![eref.clone()]), span);
        // `Test.note` (operation 0) takes the report; `Test.fail` (operation 1) ends it.
        let write = mk(
            Ty::unit(),
            tir::ExprKind::Write {
                sink: WriteSink::Perform { effect: eref.clone(), op: 0 },
                pieces: vec![Rc::from(&b"expected: "[..]), Rc::from(&b"\nactual:   "[..]), Rc::from(&b"\n"[..])],
                specs: vec![tir::FmtSpec::default(); 2],
                args: vec![local(y, rt), local(x, lt)],
            },
            span,
        );
        let msg = mk(Ty::string(), tir::ExprKind::Str(Rc::from(&b"expectation failed: the values differ"[..])), span);
        let function = self.location_function();
        let loc = self.sema.source_location(span, function);
        let stop = mk(
            Ty::never(),
            tir::ExprKind::Call { callee: tir::Callee::Perform { effect: eref, op: 1 }, args: vec![msg, loc] },
            span,
        );
        let fail = tir::Block { stmts: vec![Stmt::Expr(write)], tail: Some(stop), span };
        let check = mk(
            Ty::unit(),
            tir::ExprKind::If {
                cond: vec![tir::CondPart::Bool(differ)],
                then: Box::new(mk(Ty::never(), tir::ExprKind::Block(Box::new(fail)), span)),
                else_: None,
            },
            span,
        );
        let stmts =
            vec![Stmt::Let { local: x, init: Some(l) }, Stmt::Let { local: y, init: Some(r) }, Stmt::Expr(check)];
        super::func::block_of(stmts, span)
    }

    /// `print(template, args…)` (§21.6, §21.6.1).
    fn print(&mut self, to_out: bool, args: &[ast::Arg], span: Span) -> Expr {
        let Some(ast::Arg { kind: ArgKind::Positional(template), .. }) = args.first() else {
            self.error(span, "`print` takes a template string and the values for its placeholders");
            return error_expr(span);
        };
        if args.iter().any(|a| matches!(a.kind, ArgKind::Named(..))) {
            self.error(span, "`print` takes a template string and the values for its placeholders");
            return error_expr(span);
        }
        let (effect, name) =
            if to_out { (self.sema.prog.ambient.out, "Out") } else { (self.sema.prog.ambient.err, "Err") };
        let Some(effect) = effect else {
            self.error(span, format!("the `{}` effect is not available", name));
            return error_expr(span);
        };
        let eref = EffectRef { effect, args: Box::new([]) };
        self.record_effects(&EffectSet::new(vec![eref.clone()]), span);
        self.build_write(WriteSink::Perform { effect: eref, op: 0 }, template, &args[1..], span)
    }

    /// A write of `template` with the values `rest` (the arguments after the template:
    /// one per placeholder, or a single spread of a tuple of them) to `sink`. A template
    /// that is a compile-time `string` parameter is split in each instance of the generic
    /// function (§16.1).
    fn build_write(&mut self, sink: WriteSink, template: &ast::Expr, rest: &[ast::Arg], span: Span) -> Expr {
        if let ExprKind::Ident(_) = template.kind {
            let t = self.expr(template, Some(Ty::string()));
            if let tir::ExprKind::ValueParam(_) = t.kind {
                let args = match rest {
                    [] => None,
                    [ast::Arg { kind: ArgKind::Spread(e), .. }] => {
                        let v = self.expr(e, None);
                        match self.elements_tuple(v, e.span) {
                            Some(v) => Some(Box::new(v)),
                            None => return error_expr(span),
                        }
                    }
                    _ => {
                        let mut elems = vec![];
                        for a in rest {
                            match &a.kind {
                                ArgKind::Positional(e) => elems.push(self.expr(e, None)),
                                _ => {
                                    self.error(a.span, "a spread passes a template's values only on its own");
                                    return error_expr(span);
                                }
                            }
                        }
                        let tys: Vec<Ty> = elems.iter().map(|e| self.infer.resolve(e.ty)).collect();
                        let ty = Ty::intern(TyKind::Tuple { elems: tys.into_boxed_slice(), names: None });
                        Some(Box::new(mk(ty, tir::ExprKind::Aggregate(elems), span)))
                    }
                };
                let ty = args.as_ref().map_or(Ty::unit(), |a| self.infer.resolve(a.ty));
                let tmp = self.temp(ty, span);
                let kind = tir::ExprKind::WriteDeferred { sink, template: Box::new(t), args, tmp };
                return mk(Ty::unit(), kind, span);
            }
        }
        let bytes: Vec<u8> = match &template.kind {
            ExprKind::Str(t) => t.bytes.to_vec(),
            _ => {
                let module = self.module;
                match self.sema.const_string(module, template) {
                    Some(t) => t.into_bytes(),
                    None => {
                        self.error(
                            template.span,
                            "the template must be a compile-time string: a literal, a constant, or a `string` compile-time parameter",
                        );
                        return error_expr(span);
                    }
                }
            }
        };
        let Some((pieces, specs, values, mut stmts)) = self.template_args(&bytes, template.span, rest, span) else {
            return error_expr(span);
        };
        // A `#soa` slice is written as its columns, not its pointers (§5.6.4).
        let values = values.into_iter().map(|v| self.soa_columns_tuple(v)).collect();
        let write = mk(Ty::unit(), tir::ExprKind::Write { sink, pieces, specs, args: values }, span);
        if stmts.is_empty() {
            return write;
        }
        stmts.push(Stmt::Expr(write));
        super::func::block_of(stmts, span)
    }

    /// The pieces of a `@write` template (§21.6.1) and the argument values for its
    /// placeholders, with statements that must run first.
    fn template_args(
        &mut self,
        template: &[u8],
        template_span: Span,
        rest: &[ast::Arg],
        span: Span,
    ) -> Option<TemplateArgs> {
        let (pieces, specs) = match parse_template_at(template) {
            Ok(t) => t,
            Err((msg, stray)) => {
                let mut d = crate::sema::Diagnostic::error(template_span, format!("invalid template: {}", msg));
                // A lone `}` in a string literal: found in the literal's source text.
                if let Some(at) = stray {
                    let text = self.sema.sess.sources.text(template_span);
                    if let Some(off) = source_offset_of(text, at) {
                        let lo = template_span.lo + off as u32;
                        let brace = Span { lo, hi: lo + 1, ..template_span };
                        d = d.with_fix("write `}}` for a literal `}`", brace, "}}");
                    }
                }
                self.sema.emit(d);
                return None;
            }
        };
        let n = pieces.len() - 1;
        let mut stmts = vec![];
        let placeholders = |n: usize| if n == 1 { "1 placeholder".to_string() } else { format!("{} placeholders", n) };
        let values: Vec<Expr> = match rest {
            // `..t`: a tuple holding one value per placeholder.
            [ast::Arg { kind: ArgKind::Spread(a), .. }] => {
                let v = self.expr(a, None);
                let written = self.infer.resolve(v.ty);
                let v = self.elements_tuple(v, a.span)?;
                let t = self.infer.resolve(v.ty);
                match t.kind() {
                    TyKind::Tuple { elems, .. } if elems.len() == n => {
                        let elems = elems.clone();
                        let tmp = self.temp(t, a.span);
                        stmts.push(Stmt::Let { local: tmp, init: Some(v) });
                        (0..n)
                            .map(|i| {
                                let base = mk(t, tir::ExprKind::Local(tmp), a.span);
                                mk(elems[i], tir::ExprKind::Field { base: Box::new(base), index: i as u32 }, a.span)
                            })
                            .collect()
                    }
                    TyKind::Error => return None,
                    _ => {
                        self.error(
                            a.span,
                            format!(
                                "the template has {}, so this spread must have {} values, not `{}`",
                                placeholders(n),
                                n,
                                written
                            ),
                        );
                        return None;
                    }
                }
            }
            _ => {
                let mut vals = vec![];
                for a in rest {
                    match &a.kind {
                        ArgKind::Positional(e) => vals.push(e),
                        _ => {
                            self.error(a.span, "a spread passes a template's values only on its own");
                            return None;
                        }
                    }
                }
                if vals.len() != n {
                    let given =
                        if vals.len() == 1 { "1 value is".to_string() } else { format!("{} values are", vals.len()) };
                    self.error(span, format!("the template has {}, but {} given", placeholders(n), given));
                    return None;
                }
                vals.iter().map(|e| self.expr(e, None)).collect()
            }
        };
        for (spec, v) in specs.iter().zip(&values) {
            if spec.is_default() {
                continue;
            }
            // An untyped literal takes the type the spec asks for, or its fallback.
            let wants_float = spec.kind == tir::FmtKind::Exp || spec.precision.is_some();
            if wants_float && self.infer.var_kind(v.ty) == Some(VarKind::Int) {
                let _ = self.infer.unify(v.ty, Ty::f64());
            }
            let t = self.settle(v.ty, v.span);
            // A generic argument is checked in each instance (§16.1).
            if super::generics::has_param(t) {
                continue;
            }
            if let Err(msg) = check_spec(spec, t) {
                self.error(v.span, msg);
            }
        }
        Some((pieces, specs, values, stmts))
    }

    /// `@write(sink, template, args…)` (§21.6.1): formatting into a second-class function.
    pub fn write_intrinsic(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        let positional: Vec<&ast::Expr> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Positional(e) => Some(e),
                _ => None,
            })
            .collect();
        if positional.len() < 2
            || !matches!(args[0].kind, ArgKind::Positional(_))
            || !matches!(args[1].kind, ArgKind::Positional(_))
            || args.iter().any(|a| matches!(a.kind, ArgKind::Named(..)))
        {
            self.error(span, "`@write` takes a sink, a template string and the values for its placeholders");
            return error_expr(span);
        }
        let bytes = Ty::intern(TyKind::Slice { elem: Ty::int(false, 8), is_const: true });
        let sink_ty = Ty::intern(TyKind::Fn(Box::new(crate::types::FnTy {
            params: Box::new([bytes]),
            ret: Ty::unit(),
            effects: EffectSet::empty(),
            variadic: false,
            callconv: Default::default(),
        })));
        let sink = self.expr(positional[0], Some(sink_ty));
        let st = self.infer.resolve(sink.ty);
        match st.kind() {
            // The sink's effects are the effects of the `@write`.
            TyKind::Fn(sig) | TyKind::FnPtr(sig)
                if sig.params.len() == 1 && sig.params[0] == bytes && sig.ret.is_unit() =>
            {
                let effects = sig.effects.clone();
                self.record_effects(&effects, span);
            }
            TyKind::Error => return error_expr(span),
            _ => {
                self.error(positional[0].span, format!("a `@write` sink is a `fn(bytes: []const u8)`, not `{}`", st));
                return error_expr(span);
            }
        }
        self.build_write(WriteSink::Fn(Box::new(sink)), positional[1], &args[2..], span)
    }
}

/// A template's literal pieces, the values for its placeholders, and statements that
/// must run before them.
type TemplateArgs = (Vec<Rc<[u8]>>, Vec<tir::FmtSpec>, Vec<Expr>, Vec<Stmt>);

/// The offset in the source text `lit` of a `"…"` string literal of the byte `at` of its
/// value, when that byte is written as itself and the escapes before it are one-byte ones
/// (`\n`, `\"` and the like).
fn source_offset_of(lit: &str, at: usize) -> Option<usize> {
    let b = lit.as_bytes();
    if b.len() < 2 || b[0] != b'"' || lit.starts_with("\"\"\"") || b[b.len() - 1] != b'"' {
        return None;
    }
    let (mut i, mut n) = (1, 0);
    while i < b.len() - 1 {
        if n == at {
            return (b[i] != b'\\').then_some(i);
        }
        if b[i] == b'\\' {
            if !matches!(b.get(i + 1), Some(b'n' | b't' | b'r' | b'0' | b'\\' | b'"' | b'\'')) {
                return None;
            }
            i += 2;
        } else {
            i += 1;
        }
        n += 1;
    }
    None
}

/// A parsed `@write` template: the literal pieces around the placeholders and each
/// placeholder's format spec (`pieces.len() == specs.len() + 1`).
pub type Template = (Vec<Rc<[u8]>>, Vec<tir::FmtSpec>);

/// Splits a `@write` template into the literal pieces around its placeholders and their
/// format specs (§21.6.1), or explains what is wrong with it.
pub fn parse_template(t: &[u8]) -> Result<Template, String> {
    parse_template_at(t).map_err(|(msg, _)| msg)
}

/// Like [`parse_template`]; an error also carries the byte offset of a lone `}` that
/// should be written `}}`, when that is the mistake.
fn parse_template_at(t: &[u8]) -> Result<Template, (String, Option<usize>)> {
    let mut pieces = vec![];
    let mut specs = vec![];
    let mut cur = vec![];
    let mut i = 0;
    while i < t.len() {
        match (t[i], t.get(i + 1)) {
            (b'{', Some(b'{')) | (b'}', Some(b'}')) => {
                cur.push(t[i]);
                i += 2;
            }
            (b'{', Some(b'}')) => {
                pieces.push(Rc::from(std::mem::take(&mut cur)));
                specs.push(tir::FmtSpec::default());
                i += 2;
            }
            (b'{', Some(b':')) => {
                let Some(len) = t[i + 2..].iter().position(|&c| c == b'}') else {
                    return Err(("a `{:` placeholder has no closing `}`".into(), None));
                };
                let spec = parse_spec(&t[i + 2..i + 2 + len]).map_err(|m| (m, None))?;
                pieces.push(Rc::from(std::mem::take(&mut cur)));
                specs.push(spec);
                i += len + 3;
            }
            (b'{', _) => {
                return Err(("use `{}` or `{:spec}` for an argument and `{{` for a literal brace".into(), None));
            }
            (b'}', _) => return Err(("use `}}` for a literal `}`".into(), Some(i))),
            (c, _) => {
                cur.push(c);
                i += 1;
            }
        }
    }
    pieces.push(Rc::from(cur));
    Ok((pieces, specs))
}

/// Parses a format spec, the text between `{:` and `}`:
/// `[[fill]align][+][#][0][width][.precision][kind]`.
fn parse_spec(s: &[u8]) -> Result<tir::FmtSpec, String> {
    let mut spec = tir::FmtSpec::default();
    let align_of = |c: u8| match c {
        b'<' => Some(tir::FmtAlign::Left),
        b'>' => Some(tir::FmtAlign::Right),
        b'^' => Some(tir::FmtAlign::Center),
        _ => None,
    };
    let mut i = 0;
    if s.len() >= 2
        && let Some(a) = align_of(s[1])
    {
        if s[0] == b'{' || !s[0].is_ascii() || s[0].is_ascii_control() {
            return Err("the fill character of a format spec is a printable ASCII character other than a brace".into());
        }
        spec.fill = s[0];
        spec.align = a;
        i = 2;
    } else if let Some(a) = s.first().and_then(|&c| align_of(c)) {
        spec.align = a;
        i = 1;
    }
    if s.get(i) == Some(&b'+') {
        spec.plus = true;
        i += 1;
    }
    if s.get(i) == Some(&b'#') {
        spec.alt = true;
        i += 1;
    }
    if s.get(i) == Some(&b'0') {
        spec.zero = true;
        i += 1;
    }
    let number = |i: &mut usize| -> Result<Option<u32>, String> {
        let start = *i;
        while *i < s.len() && s[*i].is_ascii_digit() {
            *i += 1;
        }
        if *i == start {
            return Ok(None);
        }
        std::str::from_utf8(&s[start..*i])
            .unwrap()
            .parse::<u32>()
            .ok()
            .filter(|&n| n <= 1 << 16)
            .map(Some)
            .ok_or_else(|| "a format width or precision is at most 65536".to_string())
    };
    spec.width = number(&mut i)?.unwrap_or(0);
    if s.get(i) == Some(&b'.') {
        i += 1;
        match number(&mut i)? {
            Some(p) => spec.precision = Some(p),
            None => return Err("a `.` in a format spec is followed by the precision".into()),
        }
    }
    if let Some(&c) = s.get(i) {
        spec.kind = match c {
            b'x' => tir::FmtKind::Hex,
            b'X' => tir::FmtKind::HexUpper,
            b'o' => tir::FmtKind::Oct,
            b'b' => tir::FmtKind::Bin,
            b'e' => tir::FmtKind::Exp,
            _ => return Err(format!("unknown format spec `{{:{}}}`", String::from_utf8_lossy(s))),
        };
        i += 1;
    }
    if i != s.len() {
        return Err(format!(
            "invalid format spec `{{:{}}}`: expected `[[fill]align][+][#][0][width][.precision][kind]`",
            String::from_utf8_lossy(s)
        ));
    }
    if spec.zero && spec.align != tir::FmtAlign::Auto {
        return Err("a format spec has either `0` padding or an alignment, not both".into());
    }
    if spec.alt
        && !matches!(spec.kind, tir::FmtKind::Hex | tir::FmtKind::HexUpper | tir::FmtKind::Oct | tir::FmtKind::Bin)
    {
        return Err("`#` (the base prefix) goes with `x`, `X`, `o` or `b`".into());
    }
    if spec.precision.is_some() && !matches!(spec.kind, tir::FmtKind::Display | tir::FmtKind::Exp) {
        return Err("a precision goes with floats, not with an integer base".into());
    }
    Ok(spec)
}

/// Whether a placeholder's spec suits an argument of type `t` (§21.6.1): a base needs an
/// integer, a precision or `e` a float, and `+` or `0` a number.
pub fn check_spec(spec: &tir::FmtSpec, t: Ty) -> Result<(), String> {
    let int = t.is_integer();
    let float = t.is_float();
    let want = match spec.kind {
        tir::FmtKind::Hex | tir::FmtKind::HexUpper | tir::FmtKind::Oct | tir::FmtKind::Bin if !int => {
            Some("an integer, for a base")
        }
        tir::FmtKind::Exp if !float => Some("a float, for `e`"),
        _ if spec.precision.is_some() && !float => Some("a float, for a precision"),
        _ if (spec.plus || spec.zero) && !(int || float) => Some("a number, for `+` or `0`"),
        _ => None,
    };
    match want {
        Some(w) if !matches!(t.kind(), TyKind::Error) => Err(format!("this placeholder needs {}, not `{}`", w, t)),
        _ => Ok(()),
    }
}

fn is_type_syntax(e: &ast::Expr) -> bool {
    matches!(
        e.kind,
        ExprKind::OptionalType(_)
            | ExprKind::PtrType { .. }
            | ExprKind::SliceType { .. }
            | ExprKind::ManyPtrType { .. }
            | ExprKind::ArrayType { .. }
            | ExprKind::ErrorUnionType { .. }
            | ExprKind::FnType(_)
    )
}

/// The type inside optional and result wrappers, where a variant shorthand's
/// union type is found (`x: ?Shape = .Empty`).
pub fn strip_wrappers(mut t: Ty) -> Ty {
    loop {
        match t.kind() {
            TyKind::Optional(i) => t = *i,
            TyKind::Result { ok, .. } => t = *ok,
            _ => return t,
        }
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// An argument in the variadic position of a foreign call (§19.2): a C-compatible
    /// scalar at least as wide as C `int` (integers) or `f64` (floats).
    fn variadic_arg(&mut self, a: &ast::Arg) -> Expr {
        let ArgKind::Positional(e) = &a.kind else {
            self.error(a.span, "variadic arguments are written positionally");
            return error_expr(a.span);
        };
        let v = self.expr(e, None);
        let t = self.infer.resolve(v.ty);
        let ok = match t.kind() {
            TyKind::Int(i) => i.bits >= 32 && matches!(i.bits, 32 | 64),
            TyKind::Usize | TyKind::Isize | TyKind::Rawptr | TyKind::Ptr { .. } | TyKind::ManyPtr { .. } => true,
            TyKind::Float(f) => *f == crate::types::FloatTy::F64,
            TyKind::Optional(inner) => crate::layout::is_nullable_pointer(*inner),
            TyKind::Infer(_) | TyKind::Error => true,
            _ => false,
        };
        if !ok {
            self.error(
                e.span,
                format!(
                    "`{}` cannot be passed to a variadic parameter: use a C-compatible scalar of at least C `int` width or `f64`, converting explicitly (§19.2)",
                    t
                ),
            );
        }
        v
    }
}

/// True if the syntax tree `e` is built from literals, parentheses and operators only.
fn is_literal_tree(e: &ast::Expr) -> bool {
    match &e.kind {
        ExprKind::Int(_) | ExprKind::Float(_) => true,
        ExprKind::Paren(inner) => is_literal_tree(inner),
        ExprKind::Unary { operand, .. } => is_literal_tree(operand),
        ExprKind::Binary { lhs, rhs, .. } => is_literal_tree(lhs) && is_literal_tree(rhs),
        _ => false,
    }
}

/// True if `e` is built from literals only (no variables or calls).
fn is_literal(e: &Expr) -> bool {
    match &e.kind {
        tir::ExprKind::Int(_) | tir::ExprKind::Float(_) | tir::ExprKind::UntypedFloat(_) => true,
        tir::ExprKind::Unary { operand, .. } => is_literal(operand),
        tir::ExprKind::Binary { lhs, rhs, .. } => is_literal(lhs) && is_literal(rhs),
        _ => false,
    }
}

/// True if argument `e` is `&x` (possibly parenthesized) for a parameter of type `ty`
/// that is a read-only pointer, or `x[…]` for a read-only slice parameter, where `x` may
/// be a temporary (§7.2).
pub(super) fn is_const_ref_arg(e: &ast::Expr, ty: Ty) -> bool {
    let mut e = e;
    while let ExprKind::Paren(inner) = &e.kind {
        e = inner;
    }
    // `[_]T(…)[..]` is parsed with the array type outermost.
    let hoisted = super::expr::hoist_prefix(e);
    if let Some(h) = &hoisted {
        e = h;
    }
    match &e.kind {
        ExprKind::Unary { op: ast::UnOp::AddrOf, .. } => matches!(ty.kind(), TyKind::Ptr { is_const: true, .. }),
        ExprKind::Slice { .. } => matches!(ty.kind(), TyKind::Slice { is_const: true, .. }),
        _ => false,
    }
}

/// Packs the arguments for a rest parameter (§11.3): the positional arguments after those
/// of the positional parameters before it become one tuple argument, or, when the only
/// one is a spread `..t`, `t` itself. `slot(i)` tells whether parameter `i` is bound
/// positionally. `None` if `params` has no rest parameter.
pub fn pack_rest_args(
    params: &[ParamInfo],
    slot: impl Fn(usize) -> bool,
    args: &[ast::Arg],
    span: Span,
) -> Option<Result<Vec<ast::Arg>, (Span, String)>> {
    let r = params.iter().position(|p| p.rest)?;
    let before = (0..r).filter(|&i| slot(i)).count();
    let mut out = vec![];
    let mut rest: Vec<&ast::Arg> = vec![];
    let mut seen = 0;
    for a in args {
        match &a.kind {
            ArgKind::Named(n, _) if n.name == params[r].name => {
                return Some(Err((
                    a.span,
                    format!(
                        "the rest parameter `{}` takes the remaining positional arguments; it cannot be named",
                        n.name
                    ),
                )));
            }
            ArgKind::Named(..) => out.push(a.clone()),
            ArgKind::Positional(_) if seen < before => {
                seen += 1;
                out.push(a.clone());
            }
            ArgKind::Spread(_) if seen < before => {
                return Some(Err((
                    a.span,
                    "a function with a rest parameter takes a spread only as its whole rest arguments".to_string(),
                )));
            }
            ArgKind::Positional(_) | ArgKind::Spread(_) => rest.push(a),
        }
    }
    let synthetic = |kind: ast::ExprKind, span: Span| ast::Expr { id: ast::NodeId(u32::MAX), kind, span };
    let packed = match rest[..] {
        [ast::Arg { kind: ArgKind::Spread(e), span }] => ast::Arg {
            kind: ArgKind::Positional(synthetic(ast::ExprKind::Elements(Box::new(e.clone())), e.span)),
            span: *span,
        },
        _ => {
            if let Some(a) = rest.iter().find(|a| matches!(a.kind, ArgKind::Spread(_))) {
                return Some(Err((
                    a.span,
                    "a spread passes the rest arguments only on its own: `f(a, ..t)`".to_string(),
                )));
            }
            let elems: Vec<ast::TupleElem> = rest
                .iter()
                .map(|a| match &a.kind {
                    ArgKind::Positional(e) => ast::TupleElem { name: None, value: e.clone() },
                    _ => unreachable!(),
                })
                .collect();
            let sp = match (rest.first(), rest.last()) {
                (Some(f), Some(l)) => Span { file: f.span.file, lo: f.span.lo, hi: l.span.hi },
                _ => Span { file: span.file, lo: span.hi.saturating_sub(1), hi: span.hi.saturating_sub(1) },
            };
            ast::Arg { kind: ArgKind::Positional(synthetic(ast::ExprKind::Tuple(elems), sp)), span: sp }
        }
    };
    // The rest arguments go where the first of them stood (named arguments keep their places).
    let pos = args
        .iter()
        .position(|a| rest.first().is_some_and(|f| std::ptr::eq(a, *f)))
        .map(|k| args[..k].iter().filter(|a| !rest.iter().any(|x| std::ptr::eq(*x, *a))).count())
        .unwrap_or(out.len());
    out.insert(pos.min(out.len()), packed);
    Some(Ok(out))
}

#[cfg(test)]
mod tests {
    use super::parse_template;

    #[test]
    fn templates() {
        let (p, _) = parse_template(b"a{}b{{c}}{}").unwrap();
        assert_eq!(p.len(), 3);
        assert_eq!(&*p[0], b"a");
        assert_eq!(&*p[1], b"b{c}");
        assert_eq!(&*p[2], b"");
        assert!(parse_template(b"{x}").is_err());
        assert!(parse_template(b"}").is_err());
    }
}
