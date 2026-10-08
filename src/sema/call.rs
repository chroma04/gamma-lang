//! Calls (§11.3), effect operations (§12.1), conversions `T(x)` (§6.5) and the
//! builtin functions `print`, `eprint`, `trap` and `assert` (§21.6).

use super::builtins::{BuiltinFn, builtin_fn, builtin_type};
use super::func::{FnCx, LocalBinding, Obligation, error_expr};
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
                let args = self.bind_args(&sig.params, sig.module, args, span, "function");
                self.record_effects(&sig.effects, span);
                mk(sig.ret, tir::ExprKind::Call { callee: Callee::Fn(f), args }, span)
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
                let args = self.bind_args(&params, module, args, span, "function");
                self.record_effects(&sig.effects, span);
                mk(sig.ret, tir::ExprKind::Call { callee: Callee::Indirect(Box::new(v)), args }, span)
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
            if self.lookup_local(id.name).is_none() {
                let module = self.module;
                if let Some(d) = self.sema.lookup_module_name(module, id.name) {
                    return self.callee_of(d, id.name, callee);
                }
                if let Some(b) = builtin_fn(id.name.as_str()) {
                    return Some(CalleeKind::Builtin(b));
                }
                if let Some(t) = builtin_type(id.name.as_str()) {
                    return Some(CalleeKind::Type(t));
                }
            } else if let Some(LocalBinding::Label) = self.lookup_local(id.name) {
                self.error(callee.span, format!("`{}` is a label", id.name));
                return None;
            }
        }
        if is_type_syntax(callee) {
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

    /// Binds call arguments to parameters (§11.3) and coerces each to its type.
    pub fn bind_args(
        &mut self,
        params: &[ParamInfo],
        callee_module: ModuleId,
        args: &[ast::Arg],
        span: Span,
        what: &str,
    ) -> Vec<Expr> {
        let mut bound: Vec<Option<Expr>> = vec![None; params.len()];
        let mut next_positional = 0;
        let mut seen_named = false;
        for arg in args {
            match &arg.kind {
                ArgKind::Positional(e) => {
                    if seen_named {
                        self.error(arg.span, "positional arguments must come before named arguments");
                    }
                    if next_positional >= params.len() {
                        self.error(arg.span, format!("too many arguments: the {} takes {}", what, params.len()));
                        let _ = self.expr(e, None);
                        continue;
                    }
                    let p = &params[next_positional];
                    bound[next_positional] = Some(self.arg_value(e, p.ty));
                    next_positional += 1;
                }
                ArgKind::Named(name, e) => {
                    seen_named = true;
                    let Some(i) = params.iter().position(|p| p.name == name.name) else {
                        self.error(name.span, format!("the {} has no parameter named `{}`", what, name.name));
                        let _ = self.expr(e, None);
                        continue;
                    };
                    if bound[i].is_some() {
                        self.error(name.span, format!("parameter `{}` is given more than once", name.name));
                    }
                    bound[i] = Some(self.arg_value(e, params[i].ty));
                }
                ArgKind::Spread(e) => {
                    self.error(e.span, "spread arguments are not supported yet");
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
                            self.error(span, format!("missing argument for parameter `{}`", p.name));
                            out.push(error_expr(span));
                        }
                    }
                }
            }
        }
        out
    }

    fn arg_value(&mut self, e: &ast::Expr, ty: Ty) -> Expr {
        if let ExprKind::Uninit = e.kind {
            if !self.is_plain_data(ty) {
                self.error(e.span, format!("`---` requires a plain-data type, but `{}` is not (§4.4)", ty));
            }
            return mk(ty, tir::ExprKind::Uninit, e.span);
        }
        let v = self.expr(e, Some(ty));
        self.coerce(v, ty)
    }

    /// `T(x)`: a lossless conversion (§6.5).
    fn conversion(&mut self, t: Ty, args: &[ast::Arg], span: Span) -> Expr {
        // Construction (§7.11) for structs, tuples, arrays and optionals.
        if self.sema.struct_fields(t).is_some() {
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
        let v = self.expr(x, Some(t));
        // An untyped literal simply takes the type (§16.1: `T(300)`).
        if self.infer.var_kind(v.ty).is_some() && (t.is_integer() || t.is_float()) {
            if self.infer.unify(v.ty, t).is_err() {
                self.error(x.span, format!("a float literal cannot have the integer type `{}`", t));
            }
            return v;
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
        let template = positional[0];
        let ExprKind::Str(t) = &template.kind else {
            self.error(template.span, "the template of `print` must be a string literal");
            return error_expr(span);
        };
        let Some(pieces) = parse_template(&t.bytes) else {
            self.error(template.span, "invalid template: use `{}` for an argument and `{{` / `}}` for braces");
            return error_expr(span);
        };
        let n = pieces.len() - 1;
        let mut stmts = vec![];
        let values: Vec<Expr> = match (n, positional.get(1)) {
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
                        return error_expr(span);
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
                            return error_expr(span);
                        }
                    }
                }
            }
            (_, None) => {
                self.error(span, format!("the template has {} placeholder(s) but no arguments are given", n));
                return error_expr(span);
            }
        };
        let (effect, name) =
            if to_out { (self.sema.prog.ambient.out, "Out") } else { (self.sema.prog.ambient.err, "Err") };
        let Some(effect) = effect else {
            self.error(span, format!("the `{}` effect is not available", name));
            return error_expr(span);
        };
        let eref = EffectRef { effect, args: Box::new([]) };
        self.record_effects(&EffectSet::new(vec![eref.clone()]), span);
        let write = mk(
            Ty::unit(),
            tir::ExprKind::Write { sink: WriteSink::Perform { effect: eref, op: 0 }, pieces, args: values },
            span,
        );
        if stmts.is_empty() {
            return write;
        }
        stmts.push(Stmt::Expr(write));
        super::func::block_of(stmts, span)
    }
}

/// Splits a `@write` template into the literal pieces around `{}` placeholders.
pub fn parse_template(t: &[u8]) -> Option<Vec<Rc<[u8]>>> {
    let mut pieces = vec![];
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
                i += 2;
            }
            (b'{', _) | (b'}', _) => return None,
            (c, _) => {
                cur.push(c);
                i += 1;
            }
        }
    }
    pieces.push(Rc::from(cur));
    Some(pieces)
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

#[cfg(test)]
mod tests {
    use super::parse_template;

    #[test]
    fn templates() {
        let p = parse_template(b"a{}b{{c}}{}").unwrap();
        assert_eq!(p.len(), 3);
        assert_eq!(&*p[0], b"a");
        assert_eq!(&*p[1], b"b{c}");
        assert_eq!(&*p[2], b"");
        assert!(parse_template(b"{x}").is_none());
        assert!(parse_template(b"}").is_none());
    }
}
