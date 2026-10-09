//! Generic functions (§16): constraints, calls with type-argument inference,
//! and monomorphization (each distinct set of type arguments gets its own copy
//! of the checked body, with types substituted).

use super::func::{FnCx, error_expr};
use super::{ModuleId, ParamInfo, Sema};
use crate::source::Span;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, Expr, FnId, HandlerId};
use crate::types::{ConstKey, EffectRef, EffectSet, GenericArg, ParamId, Ty, TyKind};
use std::collections::HashMap;

/// The closed set of constraints (§16.3).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Constraint {
    Eq,
    Ord,
    Num,
    Int,
    Signed,
    Float,
    Plain,
}

impl Constraint {
    fn parse(s: &str) -> Option<Constraint> {
        Some(match s {
            "Eq" => Constraint::Eq,
            "Ord" => Constraint::Ord,
            "Num" => Constraint::Num,
            "Int" => Constraint::Int,
            "Signed" => Constraint::Signed,
            "Float" => Constraint::Float,
            "Plain" => Constraint::Plain,
            _ => return None,
        })
    }

    /// The constraints this one implies (including itself).
    fn implies(self) -> &'static [Constraint] {
        use Constraint::*;
        match self {
            Eq => &[Eq],
            Ord => &[Ord, Eq],
            Num => &[Num, Ord, Eq],
            Int => &[Int, Num, Ord, Eq, Plain],
            Signed => &[Signed],
            Float => &[Float, Num, Ord, Eq, Plain],
            Plain => &[Plain],
        }
    }
}

/// A request to produce the body of one instantiation.
pub struct MonoRequest {
    pub generic: FnId,
    pub instance: FnId,
    pub args: Vec<Ty>,
    pub span: Span,
}

impl<'s> Sema<'s> {
    pub(super) fn declare_constraint(&mut self, module: ModuleId, c: &ast::Constraint) {
        let Some(k) = Constraint::parse(c.name.name.as_str()) else {
            self.error(
                c.name.span,
                format!(
                    "unknown constraint `{}`; the constraints are Eq, Ord, Num, Int, Signed, Float and Plain",
                    c.name.name
                ),
            );
            return;
        };
        let t = self.resolve_type(module, &c.ty);
        match t.kind() {
            TyKind::Param(p) => self.constraints.entry(*p).or_default().push(k),
            TyKind::Error => {}
            _ => self.error(c.ty.span, "a constraint applies to a type parameter of this declaration"),
        }
    }

    /// True if type parameter `p` is granted constraint `c`.
    pub fn param_has(&self, p: ParamId, c: Constraint) -> bool {
        self.constraints.get(&p).is_some_and(|cs| cs.iter().any(|k| k.implies().contains(&c)))
    }

    /// True if the (concrete or parameter) type `t` satisfies `c` (§16.3).
    pub fn satisfies(&self, t: Ty, c: Constraint) -> bool {
        if let TyKind::Param(p) = t.kind() {
            return self.param_has(*p, c);
        }
        match c {
            Constraint::Eq => self.has_eq(t),
            Constraint::Ord => self.has_ord(t),
            Constraint::Num => t.is_integer() || t.is_float(),
            Constraint::Int => t.is_integer(),
            Constraint::Signed => t.is_signed_integer() || t.is_float(),
            Constraint::Float => t.is_float(),
            Constraint::Plain => self.is_plain(t),
        }
    }

    pub fn has_ord(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize | TyKind::Float(_) | TyKind::ManyPtr { .. } => true,
            TyKind::Tuple { elems, .. } => elems.iter().all(|e| self.has_ord(*e)),
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } | TyKind::Slice { elem, .. } => {
                self.has_ord(*elem)
            }
            TyKind::Param(p) => self.param_has(*p, Constraint::Ord),
            TyKind::Adt(..) => self.union_variants(t).is_some_and(|(vs, _)| vs.iter().all(|v| v.payload.is_none())),
            _ => false,
        }
    }

    /// The types `is` is defined for (§7.6).
    pub fn has_eq(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Fn(_) | TyKind::Handler(_) | TyKind::Cont(_) | TyKind::Type => false,
            TyKind::Param(p) => self.param_has(*p, Constraint::Eq),
            TyKind::Tuple { elems, .. } => elems.iter().all(|e| self.has_eq(*e)),
            TyKind::Array { elem, .. }
            | TyKind::ParamArray { elem, .. }
            | TyKind::Slice { elem, .. }
            | TyKind::Optional(elem) => self.has_eq(*elem),
            TyKind::Result { err, ok } => self.has_eq(*err) && self.has_eq(*ok),
            TyKind::Adt(id, _) => match self.prog.adts.get(id).map(|d| &d.kind) {
                Some(tir::AdtKind::Struct { fields, .. }) => fields.iter().all(|f| self.has_eq(f.ty)),
                Some(tir::AdtKind::Union { variants, .. }) => {
                    variants.iter().all(|v| v.payload.is_none_or(|p| self.has_eq(p)))
                }
                Some(tir::AdtKind::CUnion { .. }) => false,
                None => true,
            },
            _ => true,
        }
    }

    pub fn is_plain(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize | TyKind::Float(_) => true,
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => self.is_plain(*elem),
            TyKind::Tuple { elems, .. } => elems.iter().all(|e| self.is_plain(*e)),
            TyKind::Param(p) => self.param_has(*p, Constraint::Plain),
            TyKind::Adt(id, _) => match self.prog.adts.get(id).map(|d| &d.kind) {
                Some(tir::AdtKind::Struct { fields, bits: None, .. }) => fields.iter().all(|f| self.is_plain(f.ty)),
                Some(tir::AdtKind::Struct { fields, bits: Some(_), .. }) => {
                    fields.iter().all(|f| f.ty.is_integer() || f.ty.is_bool())
                }
                _ => false,
            },
            _ => false,
        }
    }

    /// The instance of generic function `f` for `args` (allocated on first request).
    pub fn instantiate(&mut self, f: FnId, args: Vec<Ty>, span: Span) -> FnId {
        if let Some(&i) = self.instances.get(&(f, args.clone())) {
            return i;
        }
        let sig = self.sigs[&f].clone();
        let map: Vec<(ParamId, Ty)> = sig.generics.iter().copied().zip(args.iter().copied()).collect();
        let params: Vec<ParamInfo> = sig
            .params
            .iter()
            .filter(|p| p.generic.is_none())
            .map(|p| ParamInfo { ty: self.subst(p.ty, &map), ..p.clone() })
            .collect();
        let inst_sig = super::FnSig {
            module: sig.module,
            params,
            ret: self.subst(sig.ret, &map),
            effects: subst_effects(&sig.effects, &map),
            generics: vec![],
        };
        let name = self.prog.fns[f.index()].name;
        let id = self.new_fn(name, self.prog.fns[f.index()].span, inst_sig, tir::FnKind::Named);
        self.instances.insert((f, args.clone()), id);
        self.instance_of.insert(id, (f, args.clone()));
        if !args.iter().any(|a| has_param(*a)) {
            self.mono_queue.push(MonoRequest { generic: f, instance: id, args, span });
        }
        id
    }

    /// Produces the bodies of all requested instantiations (after all generic
    /// bodies have been checked). Instantiating may request more.
    pub(super) fn monomorphize(&mut self) {
        let mut depth_guard = 0;
        while let Some(req) = self.mono_queue.pop() {
            depth_guard += 1;
            if depth_guard > 100_000 {
                self.error(
                    req.span,
                    "too many generic instantiations (is a generic function recursing on ever-larger types?)",
                );
                return;
            }
            self.produce_instance(req);
        }
    }

    pub(super) fn produce_instance(&mut self, req: MonoRequest) {
        let sig = self.sigs[&req.generic].clone();
        let map: Vec<(ParamId, Ty)> = sig.generics.iter().copied().zip(req.args.iter().copied()).collect();
        let g = &self.prog.fns[req.generic.index()];
        let Some(body) = g.body.clone() else { return };
        let params = g.params.clone();
        let locals = g.locals.clone();
        let hints = g.hints;
        let locals: Vec<tir::LocalDecl> =
            locals.into_iter().map(|l| tir::LocalDecl { ty: self.subst(l.ty, &map), ..l }).collect();
        let mut cx = MonoCx { map: &map, fn_map: HashMap::new(), handler_map: HashMap::new(), span: req.span };
        cx.fn_map.insert(req.generic, req.instance);
        let body = self.mono_expr(&mut cx, body);
        let def = &mut self.prog.fns[req.instance.index()];
        def.params = params;
        def.locals = locals;
        def.hints = hints;
        def.body = Some(body);
    }

    fn mono_expr(&mut self, cx: &mut MonoCx, mut e: Expr) -> Expr {
        let had_param = has_param(e.ty);
        e.ty = self.subst(e.ty, cx.map);
        // A compile-time value parameter becomes its argument (§16.5).
        if let tir::ExprKind::ValueParam(p) = e.kind
            && let Some((_, v)) = cx.map.iter().find(|(q, _)| *q == p)
            && let TyKind::Value(k) = v.kind()
        {
            e.kind = match k {
                ConstKey::Bytes(t, b) => match k.as_int() {
                    Some(i) => tir::ExprKind::Int(i),
                    None if t.is_bool() => tir::ExprKind::Bool(b.first() == Some(&1)),
                    None => tir::ExprKind::ConstBytes(b.clone().into()),
                },
                ConstKey::Fn(f, _) => tir::ExprKind::FnRef(FnId(*f)),
                ConstKey::Null => tir::ExprKind::Null,
                ConstKey::Str(s) => tir::ExprKind::Str(s.clone().into()),
            };
            return e;
        }
        // Literals typed by a parameter are checked against each instantiation (§16.1).
        match &mut e.kind {
            tir::ExprKind::Int(v) if had_param => {
                if e.ty.is_float() {
                    let r = num_rational::BigRational::from_integer(v.clone());
                    e.kind = tir::ExprKind::UntypedFloat(std::rc::Rc::new(r));
                } else if !super::finalize::int_fits(v, e.ty, &self.target) {
                    let msg = format!("the literal {} does not fit in `{}` (in an instantiation used here)", v, e.ty);
                    self.error(cx.span, msg);
                }
            }
            _ => {}
        }
        if let tir::ExprKind::SizeOf { of, align } = &e.kind {
            let t = self.subst(*of, cx.map);
            if !has_param(t) {
                let (size, al) = self.with_layouts(|l| (l.size_of(t), l.align_of(t)));
                e.kind = tir::ExprKind::Int((if *align { al } else { size }).into());
            } else {
                e.kind = tir::ExprKind::SizeOf { of: t, align: *align };
            }
        }
        if let tir::ExprKind::UntypedFloat(r) = &e.kind {
            let r = (**r).clone();
            match e.ty.kind() {
                TyKind::Float(f) => match crate::float::round_rational(&r, *f) {
                    Some(bits) => e.kind = tir::ExprKind::Float(crate::float::to_f64(bits, *f)),
                    None => self.error(
                        cx.span,
                        format!("a literal is too large for `{}` (in an instantiation used here)", e.ty),
                    ),
                },
                _ => self.error(
                    cx.span,
                    format!("a float literal cannot have the type `{}` (in an instantiation used here)", e.ty),
                ),
            }
        }
        match &mut e.kind {
            tir::ExprKind::Call { callee: tir::Callee::Fn(id), .. }
            | tir::ExprKind::FnRef(id)
            | tir::ExprKind::Closure(id) => {
                *id = self.mono_fn(cx, *id);
            }
            tir::ExprKind::Call { callee: tir::Callee::Perform { effect, .. }, .. } => {
                *effect = subst_eref(effect, cx.map);
            }
            tir::ExprKind::HandlerRef(h) => {
                *h = self.mono_handler(cx, *h);
            }
            tir::ExprKind::Write { sink: tir::WriteSink::Perform { effect, .. }, .. } => {
                *effect = subst_eref(effect, cx.map);
            }
            tir::ExprKind::With { handlers, .. } => {
                for h in handlers.iter_mut() {
                    if let tir::HandlerValue::Decl { value, decls } = h {
                        let t = self.subst(value.ty, cx.map);
                        *decls = self.handlers_for(t);
                    }
                }
            }
            _ => {}
        }
        let mut children = vec![];
        e.for_each_child_mut(&mut |c| children.push(std::mem::replace(c, super::func::unit(c.span))));
        let mut done = children.into_iter().map(|c| self.mono_expr(cx, c));
        e.for_each_child_mut(&mut |c| *c = done.next().unwrap());
        self.mono_pats(cx, &mut e);
        e
    }

    /// Substitutes types inside the patterns directly owned by `e`.
    fn mono_pats(&mut self, cx: &mut MonoCx, e: &mut Expr) {
        match &mut e.kind {
            tir::ExprKind::Match { arms, .. } => {
                for a in arms.iter_mut() {
                    self.mono_pat(cx, &mut a.pat);
                }
            }
            tir::ExprKind::If { cond, .. } | tir::ExprKind::Loop { cond, .. } => {
                for c in cond.iter_mut() {
                    if let tir::CondPart::Match { pat, .. } = c {
                        self.mono_pat(cx, pat);
                    }
                }
            }
            _ => {}
        }
    }

    fn mono_pat(&mut self, cx: &mut MonoCx, p: &mut tir::Pat) {
        p.ty = self.subst(p.ty, cx.map);
        match &mut p.kind {
            tir::PatKind::Some(q) | tir::PatKind::Ok(q) | tir::PatKind::Err(q) => self.mono_pat(cx, q),
            tir::PatKind::Variant { payload: Some(q), .. } => self.mono_pat(cx, q),
            tir::PatKind::Fields(fs) => fs.iter_mut().for_each(|(_, q)| self.mono_pat(cx, q)),
            tir::PatKind::Or(ps) => ps.iter_mut().for_each(|q| self.mono_pat(cx, q)),
            _ => {}
        }
    }

    /// Maps a function referenced from a generic body to the right one for this
    /// instantiation: nested functions are copied, instances re-instantiated.
    fn mono_fn(&mut self, cx: &mut MonoCx, id: FnId) -> FnId {
        if let Some(&n) = cx.fn_map.get(&id) {
            return n;
        }
        if let Some((g, args)) = self.instance_of.get(&id).cloned() {
            if args.iter().any(|a| has_param(*a)) {
                let args: Vec<Ty> = args.iter().map(|a| self.subst(*a, cx.map)).collect();
                return self.instantiate(g, args, cx.span);
            }
            return id;
        }
        if !self.nested_owner.contains_key(&id) {
            return id;
        }
        // A nested function of the generic: copy it for this instantiation.
        let def = &self.prog.fns[id.index()];
        let (name, span, kind, parent) = (def.name, def.span, def.kind, def.parent);
        let sig = self.sigs[&id].clone();
        let new_sig = super::FnSig {
            params: sig.params.iter().map(|p| ParamInfo { ty: self.subst(p.ty, cx.map), ..p.clone() }).collect(),
            ret: self.subst(sig.ret, cx.map),
            effects: subst_effects(&sig.effects, cx.map),
            ..sig
        };
        let new_id = self.new_fn(name, span, new_sig, kind);
        cx.fn_map.insert(id, new_id);
        self.nested_owner.insert(new_id, new_id);
        let def = &self.prog.fns[id.index()];
        let (params, locals, body) = (def.params.clone(), def.locals.clone(), def.body.clone());
        let locals: Vec<tir::LocalDecl> =
            locals.into_iter().map(|l| tir::LocalDecl { ty: self.subst(l.ty, cx.map), ..l }).collect();
        let parent = parent.map(|p| self.mono_fn(cx, p));
        let body = body.map(|b| self.mono_expr(cx, b));
        let nd = &mut self.prog.fns[new_id.index()];
        nd.params = params;
        nd.locals = locals;
        nd.body = body;
        nd.parent = parent;
        new_id
    }

    fn mono_handler(&mut self, cx: &mut MonoCx, h: HandlerId) -> HandlerId {
        if let Some(&n) = cx.handler_map.get(&h) {
            return n;
        }
        let lit = &self.prog.handlers[h.index()];
        let effect = subst_eref(&lit.effect, cx.map);
        let clauses = lit.clauses.clone();
        let new_h = HandlerId(self.prog.handlers.len() as u32);
        self.prog.handlers.push(tir::HandlerLit { effect, clauses: vec![] });
        cx.handler_map.insert(h, new_h);
        let clauses = clauses.into_iter().map(|c| self.mono_fn(cx, c)).collect();
        self.prog.handlers[new_h.index()].clauses = clauses;
        new_h
    }
}

pub struct MonoCx<'m> {
    map: &'m [(ParamId, Ty)],
    fn_map: HashMap<FnId, FnId>,
    handler_map: HashMap<HandlerId, HandlerId>,
    span: Span,
}

pub fn has_param(t: Ty) -> bool {
    let mut found = false;
    t.walk(&mut |x| found |= matches!(x.kind(), TyKind::Param(_)));
    found
}

/// Replaces type parameters in `t`.
pub fn subst_ty(t: Ty, map: &[(ParamId, Ty)]) -> Ty {
    if map.is_empty() || !has_param(t) {
        return t;
    }
    t.map(&mut |x| match x.kind() {
        TyKind::Param(p) => map.iter().find(|(q, _)| q == p).map(|(_, a)| *a),
        TyKind::Fn(sig) | TyKind::FnPtr(sig) if !sig.effects.is_empty() => {
            let s = crate::types::FnTy {
                params: sig.params.iter().map(|t| subst_ty(*t, map)).collect(),
                ret: subst_ty(sig.ret, map),
                effects: subst_effects(&sig.effects, map),
                variadic: sig.variadic,
                callconv: sig.callconv,
            };
            Some(Ty::intern(if matches!(x.kind(), TyKind::Fn(_)) {
                TyKind::Fn(Box::new(s))
            } else {
                TyKind::FnPtr(Box::new(s))
            }))
        }
        _ => None,
    })
}

pub fn subst_eref(e: &EffectRef, map: &[(ParamId, Ty)]) -> EffectRef {
    EffectRef {
        effect: e.effect,
        args: e
            .args
            .iter()
            .map(|a| match a {
                GenericArg::Type(t) => GenericArg::Type(subst_ty(*t, map)),
                v => v.clone(),
            })
            .collect(),
    }
}

pub fn subst_effects(s: &EffectSet, map: &[(ParamId, Ty)]) -> EffectSet {
    EffectSet::new(s.effects.iter().map(|e| subst_eref(e, map)).collect())
}

impl<'a, 's> FnCx<'a, 's> {
    /// A call to a generic function (§16.2).
    pub fn generic_call(&mut self, f: FnId, args: &[ast::Arg], span: Span) -> Expr {
        self.generic_call_with(f, None, args, span)
    }

    /// A generic call; `receiver`, if given, is the method receiver bound to the first
    /// run-time parameter (§7.10).
    /// A generic call with spread arguments (§11.4). Type inference works on the
    /// written arguments, so each spread (and each argument before one, to keep the
    /// written evaluation order) is evaluated into a local first, and the spread is
    /// replaced by arguments naming that local's elements.
    fn generic_call_spread(&mut self, f: FnId, receiver: Option<Expr>, args: &[ast::Arg], span: Span) -> Expr {
        let mut stmts = vec![];
        let receiver = receiver.map(|r| self.bind_temp(r, &mut stmts));
        let last_spread = args.iter().rposition(|a| matches!(a.kind, ArgKind::Spread(_))).unwrap_or(0);
        let synthetic = |kind: ast::ExprKind, span: Span| ast::Expr { id: ast::NodeId(u32::MAX), kind, span };
        let mut written = vec![];
        for (k, a) in args.iter().enumerate() {
            let (e, named) = match &a.kind {
                ArgKind::Spread(e) => (e, None),
                ArgKind::Positional(e) if k < last_spread => (e, Some(None)),
                ArgKind::Named(n, e) if k < last_spread => (e, Some(Some(*n))),
                _ => {
                    written.push(a.clone());
                    continue;
                }
            };
            // A literal or a name has no effects, so it can stay where it is.
            if named.is_some()
                && matches!(e.kind, ast::ExprKind::Ident(_) | ast::ExprKind::Int(_) | ast::ExprKind::Str(_))
            {
                written.push(a.clone());
                continue;
            }
            let v = self.expr(e, None);
            let vt = self.infer.resolve(v.ty);
            let name = crate::symbol::Symbol::intern(&format!("<arg{}>", self.locals.len()));
            let local = self.new_local(name, vt, false, e.span);
            stmts.push(tir::Stmt::Let { local, init: Some(v) });
            self.declare(name, super::func::LocalBinding::Var(local), e.span);
            let base = || synthetic(ast::ExprKind::Ident(ast::Ident { name, span: e.span }), e.span);
            match named {
                Some(None) => written.push(ast::Arg { kind: ArgKind::Positional(base()), span: a.span }),
                Some(Some(n)) => written.push(ast::Arg { kind: ArgKind::Named(n, base()), span: a.span }),
                None => {
                    let parts: Vec<Option<crate::symbol::Symbol>> = match vt.kind() {
                        TyKind::Tuple { elems, names: None } => vec![None; elems.len()],
                        TyKind::Tuple { names: Some(ns), .. } => ns.iter().map(|n| Some(*n)).collect(),
                        TyKind::Error => return error_expr(span),
                        _ => match self.sema.struct_fields(vt) {
                            Some(fs) if self.hidden_field(vt).is_none() => fs.iter().map(|f| Some(f.name)).collect(),
                            _ => {
                                self.error(
                                    e.span,
                                    format!("a spread needs a tuple or a struct with visible fields, not `{}`", vt),
                                );
                                return error_expr(span);
                            }
                        },
                    };
                    for (i, p) in parts.into_iter().enumerate() {
                        let arg = match p {
                            None => ArgKind::Positional(synthetic(
                                ast::ExprKind::TupleField {
                                    base: Box::new(base()),
                                    index: i as u32,
                                    index_span: e.span,
                                },
                                e.span,
                            )),
                            Some(n) => {
                                let id = ast::Ident { name: n, span: e.span };
                                ArgKind::Named(
                                    id,
                                    synthetic(ast::ExprKind::Field { base: Box::new(base()), name: id }, e.span),
                                )
                            }
                        };
                        written.push(ast::Arg { kind: arg, span: e.span });
                    }
                }
            }
        }
        let call = self.generic_call_with(f, receiver, &written, span);
        let ty = call.ty;
        Expr { ty, kind: tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(call), span })), span }
    }

    pub fn generic_call_with(&mut self, f: FnId, receiver: Option<Expr>, args: &[ast::Arg], span: Span) -> Expr {
        if args.iter().any(|a| matches!(a.kind, ArgKind::Spread(_))) {
            return self.generic_call_spread(f, receiver, args, span);
        }
        let sig = self.sema.sigs[&f].clone();
        let recv_index = receiver.as_ref().and_then(|_| sig.params.iter().position(|p| p.generic.is_none()));
        let named: Vec<crate::symbol::Symbol> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Named(n, _) => Some(n.name),
                _ => None,
            })
            .collect();
        let positional: Vec<&ast::Expr> = args
            .iter()
            .filter_map(|a| match &a.kind {
                ArgKind::Positional(e) => Some(e),
                _ => None,
            })
            .collect();
        // A compile-time value parameter is inferable only if another parameter's
        // type mentions it (§16.2).
        let inferable = |i: usize| match sig.params[i].generic {
            None => false,
            Some(pid) if self.sema.value_params.contains_key(&pid) => sig.params.iter().any(|q| {
                let mut found = false;
                q.ty.walk(&mut |t| found |= matches!(t.kind(), TyKind::Param(x) if *x == pid));
                found
            }),
            Some(_) => true,
        };
        let non_inferable: Vec<usize> = (0..sig.params.len())
            .filter(|&i| {
                !inferable(i)
                    && !named.contains(&sig.params[i].name)
                    && Some(i) != recv_index
                    && !sig.params[i].is_site()
            })
            .collect();
        let r = non_inferable.iter().filter(|&&i| sig.params[i].default.is_none()).count();
        let n = non_inferable.len();
        let omit = positional.len() >= r && positional.len() <= n;
        // Which parameter each positional argument binds to.
        let targets: Vec<usize> = if omit {
            non_inferable.clone()
        } else {
            (0..sig.params.len())
                .filter(|&i| !named.contains(&sig.params[i].name) && Some(i) != recv_index && !sig.params[i].is_site())
                .collect()
        };
        if positional.len() > targets.len() {
            self.error(span, format!("too many arguments: the function takes {}", targets.len()));
            return error_expr(span);
        }
        let mut binds: HashMap<ParamId, Ty> = HashMap::new();
        let mut values: Vec<Option<Expr>> = vec![None; sig.params.len()];
        let mut sources: Vec<(usize, &ast::Expr)> =
            positional.iter().enumerate().map(|(k, e)| (targets[k], *e)).collect();
        for a in args {
            if let ArgKind::Named(nm, e) = &a.kind {
                match sig.params.iter().position(|p| p.name == nm.name) {
                    Some(i) => sources.push((i, e)),
                    None => self.error(nm.span, format!("the function has no parameter named `{}`", nm.name)),
                }
            }
        }
        // The receiver binds first, so that the arguments are checked against what it fixes.
        let receiver = match (recv_index, receiver) {
            (Some(i), Some(r)) => {
                let t = self.infer.resolve(r.ty);
                super::effects::match_params_pub(sig.params[i].ty, t, &mut binds);
                values[i] = Some(r);
                None
            }
            (_, r) => r,
        };
        for (i, e) in &sources {
            let p = &sig.params[*i];
            if let Some(pid) = p.generic
                && self.sema.value_params.contains_key(&pid)
            {
                let bound: Vec<(ParamId, Ty)> = binds.iter().map(|(k, v)| (*k, *v)).collect();
                let ty = self.sema.subst(p.ty, &bound);
                match self.comptime_arg(ty, e) {
                    Some(v) => {
                        binds.insert(pid, v);
                    }
                    None => return error_expr(span),
                }
            } else if let Some(pid) = p.generic {
                let t = self.resolve_type(e);
                binds.insert(pid, t);
            } else {
                let bound: Vec<(ParamId, Ty)> = binds.iter().map(|(k, v)| (*k, *v)).collect();
                let expect = self.sema.subst(p.ty, &bound);
                // A function type with parameters still unbound still helps a function
                // literal argument: it takes the parts that are known.
                let partial = matches!(expect.kind(), TyKind::Fn(_) | TyKind::FnPtr(_));
                let v = self.expr(e, (!has_param(expect) || partial).then_some(expect));
                let t = self.infer.resolve(v.ty);
                let mut b2 = HashMap::new();
                super::effects::match_params_pub(p.ty, t, &mut b2);
                for (k, v2) in b2 {
                    match binds.get(&k) {
                        Some(prev) if *prev != v2 && !omit => {}
                        Some(prev) if *prev != v2 => {
                            self.error(
                                e.span,
                                format!(
                                    "`{}` is inferred as both `{}` and `{}`; write it explicitly",
                                    k.name(),
                                    prev,
                                    v2
                                ),
                            );
                        }
                        Some(_) => {}
                        None => {
                            binds.insert(k, v2);
                        }
                    }
                }
                values[*i] = Some(v);
            }
        }
        let _ = receiver;
        let mut targs = vec![];
        for pid in &sig.generics {
            match binds.get(pid) {
                Some(t) if self.sema.value_params.contains_key(pid) => {
                    let pty = self.sema.value_params[pid];
                    match self.normalize_value(*t, pty, pid.name(), span) {
                        Some(v) => targs.push(v),
                        None => return error_expr(span),
                    }
                }
                Some(t) => targs.push(*t),
                None if self.sema.value_params.contains_key(pid) => {
                    let p = sig.params.iter().find(|p| p.generic == Some(*pid)).unwrap();
                    match p.default.clone() {
                        Some(d) => match self.comptime_arg(p.ty, &d) {
                            Some(v) => targs.push(v),
                            None => return error_expr(span),
                        },
                        None => {
                            self.error(span, format!("missing argument for the compile-time parameter `{}`", p.name));
                            return error_expr(span);
                        }
                    }
                }
                None => {
                    self.error(
                        span,
                        format!("cannot infer the type parameter `{}`; write it: `f({}, …)`", pid.name(), pid.name()),
                    );
                    return error_expr(span);
                }
            }
        }
        // Constraints are checked at each instantiation (§16.3).
        for (pid, t) in sig.generics.iter().zip(&targs) {
            if t.is_error() {
                return error_expr(span);
            }
            if matches!(t.kind(), TyKind::Fn(_) | TyKind::Handler(_) | TyKind::Type) {
                self.error(span, format!("`{}` is not a first-class type, so it cannot be a type argument (§16.1)", t));
            }
            for c in self.sema.constraints.get(pid).cloned().unwrap_or_default() {
                if !self.sema.satisfies(*t, c) {
                    self.error(
                        span,
                        format!("`{}` does not satisfy `{:?}({})` required by this function", t, c, pid.name()),
                    );
                }
            }
        }
        let map: Vec<(ParamId, Ty)> = sig.generics.iter().copied().zip(targs.iter().copied()).collect();
        let mut out = vec![];
        for (i, p) in sig.params.iter().enumerate() {
            if p.generic.is_some() {
                continue;
            }
            let pty = self.sema.subst(p.ty, &map);
            match values[i].take() {
                Some(v) => out.push(self.coerce(v, pty)),
                None => match &p.default {
                    Some(d) => {
                        let d = d.clone();
                        let site = self.call_site(span);
                        let saved = self.sema.caller_site.replace(site);
                        let v = self.sema.check_const_expr(sig.module, &d, Some(pty));
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
                },
            }
        }
        let effects = subst_effects(&sig.effects, &map);
        self.record_effects(&effects, span);
        let inst = self.sema.instantiate(f, targs, span);
        Expr {
            ty: self.sema.subst(sig.ret, &map),
            kind: tir::ExprKind::Call { callee: tir::Callee::Fn(inst), args: out },
            span,
        }
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// Evaluates the argument of a compile-time value parameter of type `ty` (§16.5).
    fn comptime_arg(&mut self, ty: Ty, e: &ast::Expr) -> Option<Ty> {
        if let ast::ExprKind::Ident(id) = &e.kind
            && self.lookup_local(id.name).is_some()
        {
            self.error(
                e.span,
                format!("a compile-time argument must be a constant expression, but `{}` is a variable", id.name),
            );
            return None;
        }
        let module = self.module;
        self.sema.comptime_value(module, ty, e)
    }

    /// Converts an inferred value argument (an array length) to the parameter's type.
    fn normalize_value(&mut self, v: Ty, ty: Ty, name: crate::symbol::Symbol, span: Span) -> Option<Ty> {
        let k = match v.kind() {
            TyKind::Value(k) => k,
            TyKind::Param(p) if self.sema.value_params.contains_key(p) => return Some(v),
            _ => {
                self.error(span, format!("`{}` is a compile-time value parameter, but a type was given", name));
                return None;
            }
        };
        match k {
            crate::types::ConstKey::Bytes(t, _) if *t == ty => Some(v),
            crate::types::ConstKey::Bytes(t, _) if t.is_integer() && ty.is_integer() => {
                let i = k.as_int()?;
                if !super::finalize::int_fits(&i, ty, &self.sema.target) {
                    self.error(span, format!("`{}` = {} does not fit in `{}`", name, i, ty));
                    return None;
                }
                let size = self.sema.with_layouts(|l| l.size_of(ty));
                Some(Ty::intern(TyKind::Value(crate::types::ConstKey::int(ty, &i, size))))
            }
            _ => Some(v),
        }
    }
}
