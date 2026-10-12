//! Calls (§11.3), effect operations (§12.1), conversions `T(x)` (§6.5) and the
//! builtin functions `print`, `eprint`, `trap` and `assert` (§21.6).

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
                return Some(CalleeKind::Fn(*f));
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
        if let ExprKind::Call { .. } = &callee.kind
            && let Some(t) = self.type_of_name(callee)
        {
            return Some(CalleeKind::Type(t));
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
        let mut bound: Vec<Option<Expr>> = vec![None; params.len()];
        let mut next_positional = 0;
        let mut seen_named = false;
        let spreading = args.iter().any(|a| matches!(a.kind, ArgKind::Spread(_)));
        let mut stmts = vec![];
        // Named arguments given explicitly (a spread supplying one later is an error).
        let mut explicit = vec![false; params.len()];
        // Positional arguments never bind a `@caller_location()` parameter.
        let positional_max = params.iter().filter(|p| !p.is_site()).count();
        // After a spread fails, missing arguments are not reported again.
        let mut spread_failed = false;
        for arg in args {
            match &arg.kind {
                ArgKind::Positional(e) => {
                    if seen_named {
                        self.error(arg.span, "positional arguments must come before named arguments");
                    }
                    if next_positional >= positional_max {
                        self.error(arg.span, format!("too many arguments: the {} takes {}", what, positional_max));
                        let _ = self.expr(e, None);
                        continue;
                    }
                    let p = &params[next_positional];
                    let mut v = self.arg_value(e, p.ty);
                    if spreading {
                        v = self.bind_temp(v, &mut stmts);
                    }
                    bound[next_positional] = Some(v);
                    next_positional += 1;
                }
                ArgKind::Named(name, e) => {
                    seen_named = true;
                    let Some(i) = params.iter().position(|p| p.name == name.name) else {
                        self.error(name.span, format!("the {} has no parameter named `{}`", what, name.name));
                        let _ = self.expr(e, None);
                        continue;
                    };
                    if explicit[i] || (bound[i].is_some() && !spreading) {
                        self.error(name.span, format!("parameter `{}` is given more than once", name.name));
                    }
                    explicit[i] = true;
                    // A later explicit named argument overrides one supplied by a spread.
                    let mut v = self.arg_value(e, params[i].ty);
                    if spreading {
                        v = self.bind_temp(v, &mut stmts);
                    }
                    bound[i] = Some(v);
                }
                ArgKind::Spread(e) => {
                    let Some(parts) = self.spread_parts(e, &mut stmts) else {
                        spread_failed = true;
                        continue;
                    };
                    for (name, proj) in parts {
                        let i = match name {
                            // An unnamed tuple's elements are positional arguments.
                            None => {
                                if seen_named {
                                    self.error(e.span, "positional arguments must come before named arguments");
                                    spread_failed = true;
                                    break;
                                }
                                if next_positional >= positional_max {
                                    self.error(
                                        e.span,
                                        format!("too many arguments: the {} takes {}", what, positional_max),
                                    );
                                    spread_failed = true;
                                    break;
                                }
                                next_positional += 1;
                                next_positional - 1
                            }
                            Some(n) => {
                                seen_named = true;
                                let Some(i) = params.iter().position(|p| p.name == n) else {
                                    self.error(
                                        e.span,
                                        format!(
                                            "the {} has no parameter named `{}` (supplied by this spread)",
                                            what, n
                                        ),
                                    );
                                    spread_failed = true;
                                    break;
                                };
                                if explicit[i] || bound[i].is_some() {
                                    self.error(e.span, format!("parameter `{}` is given more than once", n));
                                    spread_failed = true;
                                    break;
                                }
                                i
                            }
                        };
                        bound[i] = Some(self.coerce(proj, params[i].ty));
                    }
                }
            }
        }
        let mut out = vec![];
        for (i, b) in bound.into_iter().enumerate() {
            match b {
                Some(v) => out.push(v),
                None => {
                    let p = &params[i];
                    match &p.default {
                        Some(d) => {
                            let d = d.clone();
                            let site = self.call_site(span);
                            let saved = self.sema.caller_site.replace(site);
                            let v = self.sema.check_const_expr(callee_module, &d, Some(p.ty));
                            self.sema.caller_site = saved;
                            match v {
                                Some(v) => out.push(v),
                                None => out.push(error_expr(span)),
                            }
                        }
                        None => {
                            if !spread_failed {
                                self.error(span, format!("missing argument for parameter `{}`", p.name));
                            }
                            out.push(error_expr(span));
                        }
                    }
                }
            }
        }
        (out, stmts)
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

    pub(super) fn arg_value(&mut self, e: &ast::Expr, ty: Ty) -> Expr {
        if let ExprKind::Uninit = e.kind {
            if !self.is_plain_data(ty) {
                self.error(e.span, format!("`---` requires a plain-data type, but `{}` is not (§4.4)", ty));
            }
            return mk(ty, tir::ExprKind::Uninit, e.span);
        }
        self.temp_ref_arg = is_const_ref_arg(e, ty);
        let v = self.expr(e, Some(ty));
        self.temp_ref_arg = false;
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
            TyKind::Array { len, elem } => return self.construct_array(Some(*len), *elem, args, span),
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
            let v = self.expr(x, Some(u));
            if self.infer.var_kind(v.ty).is_some() {
                let v = self.coerce(v, u);
                return mk(t, tir::ExprKind::Builtin { op: tir::Builtin::Convert, args: vec![v] }, span);
            }
            self.obligations.push(Obligation::Conversion { from: v.ty, to: t, span });
            return mk(t, tir::ExprKind::Builtin { op: tir::Builtin::Convert, args: vec![v] }, span);
        }
        let v = self.expr(x, Some(t));
        // An untyped literal simply takes the type (§16.1: `T(300)`), and so does a
        // variable of literal type converted to an integer type. A float type is taken
        // by literals only: `f64(i)` converts `i` from the integer type it ends up with.
        if self.infer.var_kind(v.ty).is_some() && (t.is_integer() || (t.is_float() && is_literal(&v))) {
            if self.infer.unify(v.ty, t).is_ok() {
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
            BuiltinFn::Assert | BuiltinFn::Expect => {
                let is_expect = b == BuiltinFn::Expect;
                let what = if is_expect { "expect" } else { "assert" };
                if is_expect && !self.sema.in_test {
                    self.error(span, "`expect` may be called only in tests (§21.6)");
                }
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
                    None => {
                        let text: &[u8] = if is_expect { b"expectation failed" } else { b"assertion failed" };
                        mk(Ty::string(), tir::ExprKind::Str(Rc::from(text)), span)
                    }
                };
                let op = if is_expect { tir::Builtin::Expect } else { tir::Builtin::Assert };
                mk(Ty::unit(), tir::ExprKind::Builtin { op, args: vec![c, m] }, span)
            }
        }
    }

    /// `expect_eq(actual, expected)` (§21.6): in a test, fails it unless the two are equal
    /// by `is`, writing both values to `Err` first.
    fn expect_eq(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        if !self.sema.in_test {
            self.error(span, "`expect_eq` may be called only in tests (§21.6)");
            return error_expr(span);
        }
        let [ast::Arg { kind: ArgKind::Positional(a), .. }, ast::Arg { kind: ArgKind::Positional(b), .. }] = args
        else {
            self.error(span, "`expect_eq` takes the actual value and the expected one");
            return error_expr(span);
        };
        let Some(effect) = self.sema.prog.ambient.err else {
            return error_expr(span);
        };
        let l = self.expr(a, None);
        let r = self.expr(b, Some(l.ty));
        let (l, r) = self.equalize(l, r, span);
        let (lt, rt) = (self.infer.settle(l.ty), self.infer.settle(r.ty));
        let (x, y) = (self.temp(lt, a.span), self.temp(rt, b.span));
        let local = |id, ty| mk(ty, tir::ExprKind::Local(id), span);
        let differ = mk(
            Ty::bool(),
            tir::ExprKind::Eq { lhs: Box::new(local(x, lt)), rhs: Box::new(local(y, rt)), negated: true },
            span,
        );
        let eref = EffectRef { effect, args: Box::new([]) };
        self.record_effects(&EffectSet::new(vec![eref.clone()]), span);
        let write = mk(
            Ty::unit(),
            tir::ExprKind::Write {
                sink: WriteSink::Perform { effect: eref, op: 0 },
                pieces: vec![Rc::from(&b"expected: "[..]), Rc::from(&b"\nactual:   "[..]), Rc::from(&b"\n"[..])],
                specs: vec![tir::FmtSpec::default(); 2],
                args: vec![local(y, rt), local(x, lt)],
            },
            span,
        );
        let msg = mk(Ty::string(), tir::ExprKind::Str(Rc::from(&b"expectation failed: the values differ"[..])), span);
        let stop = mk(Ty::never(), tir::ExprKind::Trap { message: Box::new(msg) }, span);
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

    /// `print(template, args)` (§21.6, §21.6.1).
    fn print(&mut self, to_out: bool, args: &[ast::Arg], span: Span) -> Expr {
        let positional: Vec<&ast::Expr> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Positional(e) => Some(e),
                _ => None,
            })
            .collect();
        if positional.len() != args.len() || positional.is_empty() || positional.len() > 2 {
            self.error(span, "`print` takes a template string and, optionally, the arguments");
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
        self.build_write(WriteSink::Perform { effect: eref, op: 0 }, positional[0], positional.get(1).copied(), span)
    }

    /// A write of `template` with `args` to `sink`. A template that is a compile-time
    /// `string` parameter is split in each instance of the generic function (§16.1).
    fn build_write(&mut self, sink: WriteSink, template: &ast::Expr, args: Option<&ast::Expr>, span: Span) -> Expr {
        if let ExprKind::Ident(_) = template.kind {
            let t = self.expr(template, Some(Ty::string()));
            if let tir::ExprKind::ValueParam(_) = t.kind {
                let args = args.map(|a| Box::new(self.expr(a, None)));
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
        let Some((pieces, specs, values, mut stmts)) = self.template_args(&bytes, template.span, args, span) else {
            return error_expr(span);
        };
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
        args: Option<&ast::Expr>,
        span: Span,
    ) -> Option<TemplateArgs> {
        let (pieces, specs) = match parse_template(template) {
            Ok(t) => t,
            Err(msg) => {
                self.error(template_span, format!("invalid template: {}", msg));
                return None;
            }
        };
        let n = pieces.len() - 1;
        let mut stmts = vec![];
        let values: Vec<Expr> = match (n, args) {
            (0, None) => vec![],
            (0, Some(a)) => {
                let v = self.expr(a, None);
                if !self.infer.resolve(v.ty).is_unit() {
                    self.error(a.span, "the template has no `{}` placeholders, so the arguments must be `()`");
                }
                vec![]
            }
            (1, Some(a)) => vec![self.expr(a, None)],
            (_, Some(a)) => {
                if let ExprKind::Tuple(elems) = &a.kind
                    && elems.iter().all(|e| e.name.is_none())
                {
                    if elems.len() != n {
                        self.error(
                            a.span,
                            format!("the template has {} placeholders but {} arguments are given", n, elems.len()),
                        );
                        return None;
                    }
                    elems.iter().map(|e| self.expr(&e.value, None)).collect()
                } else {
                    let v = self.expr(a, None);
                    let t = self.infer.resolve(v.ty);
                    match t.kind() {
                        TyKind::Tuple { elems, .. } if elems.len() == n => {
                            let tmp = self.temp(t, a.span);
                            stmts.push(Stmt::Let { local: tmp, init: Some(v) });
                            (0..n)
                                .map(|i| {
                                    let base = mk(t, tir::ExprKind::Local(tmp), a.span);
                                    mk(elems[i], tir::ExprKind::Field { base: Box::new(base), index: i as u32 }, a.span)
                                })
                                .collect()
                        }
                        _ => {
                            self.error(
                                a.span,
                                format!(
                                    "the template has {} placeholders, so the arguments must be a tuple of {} values",
                                    n, n
                                ),
                            );
                            return None;
                        }
                    }
                }
            }
            (_, None) => {
                self.error(span, format!("the template has {} placeholder(s) but no arguments are given", n));
                return None;
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
            let t = self.infer.settle(v.ty);
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

    /// `@write(sink, template, args)` (§21.6.1): formatting into a second-class function.
    pub fn write_intrinsic(&mut self, args: &[ast::Arg], span: Span) -> Expr {
        let positional: Vec<&ast::Expr> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Positional(e) => Some(e),
                _ => None,
            })
            .collect();
        if positional.len() != args.len() || !(2..=3).contains(&positional.len()) {
            self.error(span, "`@write` takes a sink, a template string and, optionally, the arguments");
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
        self.build_write(WriteSink::Fn(Box::new(sink)), positional[1], positional.get(2).copied(), span)
    }
}

/// A template's literal pieces, the values for its placeholders, and statements that
/// must run before them.
type TemplateArgs = (Vec<Rc<[u8]>>, Vec<tir::FmtSpec>, Vec<Expr>, Vec<Stmt>);

/// A parsed `@write` template: the literal pieces around the placeholders and each
/// placeholder's format spec (`pieces.len() == specs.len() + 1`).
pub type Template = (Vec<Rc<[u8]>>, Vec<tir::FmtSpec>);

/// Splits a `@write` template into the literal pieces around its placeholders and their
/// format specs (§21.6.1), or explains what is wrong with it.
pub fn parse_template(t: &[u8]) -> Result<Template, String> {
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
                    return Err("a `{:` placeholder has no closing `}`".into());
                };
                let spec = parse_spec(&t[i + 2..i + 2 + len])?;
                pieces.push(Rc::from(std::mem::take(&mut cur)));
                specs.push(spec);
                i += len + 3;
            }
            (b'{', _) => {
                return Err("use `{}` or `{:spec}` for an argument and `{{` for a literal brace".into());
            }
            (b'}', _) => return Err("use `}}` for a literal `}`".into()),
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
