//! Generic functions (§16): constraints, calls with type-argument inference,
//! and monomorphization (each distinct set of type arguments gets its own copy
//! of the checked body, with types substituted).

use super::func::{FnCx, error_expr};
use super::{ModuleId, ParamInfo, Sema};
use crate::source::Span;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, Expr, FnId, HandlerId};
use crate::types::{EffectRef, EffectSet, GenericArg, ParamId, Ty, TyKind};
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
            TyKind::Array { elem, .. } | TyKind::Slice { elem, .. } => self.has_ord(*elem),
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
            TyKind::Array { elem, .. } | TyKind::Slice { elem, .. } | TyKind::Optional(elem) => self.has_eq(*elem),
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
            TyKind::Array { elem, .. } => self.is_plain(*elem),
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
            .map(|p| ParamInfo { ty: subst_ty(p.ty, &map), ..p.clone() })
            .collect();
        let inst_sig = super::FnSig {
            module: sig.module,
            params,
            ret: subst_ty(sig.ret, &map),
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

    fn produce_instance(&mut self, req: MonoRequest) {
        let sig = self.sigs[&req.generic].clone();
        let map: Vec<(ParamId, Ty)> = sig.generics.iter().copied().zip(req.args.iter().copied()).collect();
        let g = &self.prog.fns[req.generic.index()];
        let Some(body) = g.body.clone() else { return };
        let params = g.params.clone();
        let locals: Vec<tir::LocalDecl> =
            g.locals.iter().map(|l| tir::LocalDecl { ty: subst_ty(l.ty, &map), ..l.clone() }).collect();
        let mut cx = MonoCx { map: &map, fn_map: HashMap::new(), handler_map: HashMap::new(), span: req.span };
        cx.fn_map.insert(req.generic, req.instance);
        let body = self.mono_expr(&mut cx, body);
        let def = &mut self.prog.fns[req.instance.index()];
        def.params = params;
        def.locals = locals;
        def.body = Some(body);
    }

    fn mono_expr(&mut self, cx: &mut MonoCx, mut e: Expr) -> Expr {
        let had_param = has_param(e.ty);
        e.ty = subst_ty(e.ty, cx.map);
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
                        let t = subst_ty(value.ty, cx.map);
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
        fn fix(p: &mut tir::Pat, map: &[(ParamId, Ty)]) {
            p.ty = subst_ty(p.ty, map);
            match &mut p.kind {
                tir::PatKind::Some(q) | tir::PatKind::Ok(q) | tir::PatKind::Err(q) => fix(q, map),
                tir::PatKind::Variant { payload: Some(q), .. } => fix(q, map),
                tir::PatKind::Fields(fs) => fs.iter_mut().for_each(|(_, q)| fix(q, map)),
                tir::PatKind::Or(ps) => ps.iter_mut().for_each(|q| fix(q, map)),
                _ => {}
            }
        }
        match &mut e.kind {
            tir::ExprKind::Match { arms, .. } => arms.iter_mut().for_each(|a| fix(&mut a.pat, cx.map)),
            tir::ExprKind::If { cond, .. } | tir::ExprKind::Loop { cond, .. } => {
                for c in cond.iter_mut() {
                    if let tir::CondPart::Match { pat, .. } = c {
                        fix(pat, cx.map);
                    }
                }
            }
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
                let args: Vec<Ty> = args.iter().map(|a| subst_ty(*a, cx.map)).collect();
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
            params: sig.params.iter().map(|p| ParamInfo { ty: subst_ty(p.ty, cx.map), ..p.clone() }).collect(),
            ret: subst_ty(sig.ret, cx.map),
            effects: subst_effects(&sig.effects, cx.map),
            ..sig
        };
        let new_id = self.new_fn(name, span, new_sig, kind);
        cx.fn_map.insert(id, new_id);
        self.nested_owner.insert(new_id, new_id);
        let def = &self.prog.fns[id.index()];
        let params = def.params.clone();
        let locals: Vec<tir::LocalDecl> =
            def.locals.iter().map(|l| tir::LocalDecl { ty: subst_ty(l.ty, cx.map), ..l.clone() }).collect();
        let body = def.body.clone();
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
        let sig = self.sema.sigs[&f].clone();
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
        if args.iter().any(|a| matches!(a.kind, ArgKind::Spread(_))) {
            self.error(span, "spread arguments are not supported in generic calls yet");
            return error_expr(span);
        }
        let non_inferable: Vec<usize> = (0..sig.params.len())
            .filter(|&i| sig.params[i].generic.is_none() && !named.contains(&sig.params[i].name))
            .collect();
        let r = non_inferable.iter().filter(|&&i| sig.params[i].default.is_none()).count();
        let n = non_inferable.len();
        let omit = positional.len() >= r && positional.len() <= n;
        // Which parameter each positional argument binds to.
        let targets: Vec<usize> = if omit {
            non_inferable.clone()
        } else {
            (0..sig.params.len()).filter(|&i| !named.contains(&sig.params[i].name)).collect()
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
        for (i, e) in &sources {
            let p = &sig.params[*i];
            if let Some(pid) = p.generic {
                let t = self.resolve_type(e);
                binds.insert(pid, t);
            } else {
                let v = self.expr(e, None);
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
        let mut targs = vec![];
        for pid in &sig.generics {
            match binds.get(pid) {
                Some(t) => targs.push(*t),
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
            let pty = subst_ty(p.ty, &map);
            match values[i].take() {
                Some(v) => out.push(self.coerce(v, pty)),
                None => match &p.default {
                    Some(d) => {
                        let d = d.clone();
                        match self.sema.check_const_expr(sig.module, &d, Some(pty)) {
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
            ty: subst_ty(sig.ret, &map),
            kind: tir::ExprKind::Call { callee: tir::Callee::Fn(inst), args: out },
            span,
        }
    }
}
