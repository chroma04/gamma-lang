//! Generic functions (§16): constraints, calls with type-argument inference,
//! and monomorphization (each distinct set of type arguments gets its own copy
//! of the checked body, with types substituted).

use super::func::{FnCx, error_expr};
use super::{ModuleId, ParamInfo, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind};
use crate::tir::{self, Expr, FnId, HandlerId};
use crate::types::{ConstKey, GenericArg, ParamId, Ty, TyKind};
use std::collections::{HashMap, HashSet};

/// The closed set of constraints (§16.3).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Constraint {
    Eq,
    Ord,
    Num,
    Int,
    Neg,
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
            "Neg" => Constraint::Neg,
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
            Neg => &[Neg, Num, Ord, Eq, Plain],
            Float => &[Float, Neg, Num, Ord, Eq, Plain],
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
    /// How many instantiations led to this one (0 for one requested by checked code).
    pub depth: u32,
}

/// The deepest chain of instantiations requesting each other (§16.7).
const MAX_MONO_DEPTH: u32 = 256;

impl<'s> Sema<'s> {
    pub(super) fn declare_constraint(&mut self, module: ModuleId, c: &ast::Constraint) {
        let Some(k) = Constraint::parse(c.name.name.as_str()) else {
            let mut msg = format!(
                "unknown constraint `{}`; the constraints are Eq, Ord, Num, Int, Neg, Float and Plain",
                c.name.name
            );
            if c.name.name.as_str() == "Signed" {
                msg.push_str("; `Signed` is now `Neg`");
            }
            self.error(c.name.span, msg);
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
    /// Ends a signature or generic type declaration: once the outermost one has declared
    /// its `where` clause, checks the constraints that waited for it.
    pub(super) fn end_constraint_defer(&mut self) {
        self.constraint_defer -= 1;
        if self.constraint_defer > 0 {
            return;
        }
        for (t, c, pid, template, span) in std::mem::take(&mut self.deferred_constraints) {
            if !self.satisfies(t, c) {
                self.error(
                    span,
                    format!("`{}` does not satisfy `{:?}({})` required by `{}`", t, c, pid.name(), template.name()),
                );
            }
        }
    }

    pub fn satisfies(&self, t: Ty, c: Constraint) -> bool {
        if let TyKind::Param(p) = t.kind() {
            return self.param_has(*p, c);
        }
        match c {
            Constraint::Eq => self.has_eq(t),
            Constraint::Ord => self.has_ord(t),
            Constraint::Num => t.is_integer() || t.is_float(),
            Constraint::Int => t.is_integer(),
            Constraint::Neg => t.is_signed_integer() || t.is_float(),
            Constraint::Float => t.is_float(),
            Constraint::Plain => self.is_plain(t),
        }
    }

    pub fn has_ord(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Bool
            | TyKind::Int(_)
            | TyKind::Usize
            | TyKind::Isize
            | TyKind::Float(_)
            | TyKind::ManyPtr { .. } => true,
            TyKind::Tuple { elems, .. } => elems.iter().all(|e| self.has_ord(*e)),
            // Vectors are not ordered: `.lanes_lt(w)` and its kin compare lanes (§5.6.2).
            // Nor are math vectors: no order of points is the natural one (§5.6.3).
            TyKind::Array { kind: crate::types::ArrayKind::Simd | crate::types::ArrayKind::Math, .. } => false,
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
        self.has_eq_in(t, &mut Vec::new())
    }

    /// `has_eq` with the ADTs being examined: a type that reaches itself again is assumed to have
    /// equality (the other parts decide), so self-recursive types terminate.
    fn has_eq_in(&self, t: Ty, seen: &mut Vec<Ty>) -> bool {
        match t.kind() {
            TyKind::Fn(_) | TyKind::Handler(_) | TyKind::Cont(_) | TyKind::Type => false,
            TyKind::Param(p) => self.param_has(*p, Constraint::Eq),
            TyKind::Tuple { elems, .. } => elems.iter().all(|e| self.has_eq_in(*e, seen)),
            TyKind::Array { elem, .. }
            | TyKind::ParamArray { elem, .. }
            | TyKind::Slice { elem, .. }
            | TyKind::Optional(elem) => self.has_eq_in(*elem, seen),
            TyKind::Result { err, ok } => self.has_eq_in(*err, seen) && self.has_eq_in(*ok, seen),
            TyKind::Adt(id, _) => {
                if seen.contains(&t) {
                    return true;
                }
                seen.push(t);
                let r = match self.prog.adts.get(id).map(|d| &d.kind) {
                    Some(tir::AdtKind::Struct { fields, .. }) => fields.iter().all(|f| self.has_eq_in(f.ty, seen)),
                    Some(tir::AdtKind::Union { variants, .. }) => {
                        variants.iter().all(|v| v.payload.is_none_or(|p| self.has_eq_in(p, seen)))
                    }
                    Some(tir::AdtKind::CUnion { .. }) => false,
                    None => true,
                };
                seen.pop();
                r
            }
            _ => true,
        }
    }

    pub fn is_plain(&self, t: Ty) -> bool {
        // Types are shared DAGs, so the answer for a type with no definition under
        // construction is remembered.
        let stable = !has_param(t) && self.adts_in_progress.is_empty() && self.adt_inst_depth == 0;
        if stable && let Some(&r) = self.plain_cache.borrow().get(&t) {
            return r;
        }
        let r = self.is_plain_uncached(t);
        if stable {
            self.plain_cache.borrow_mut().insert(t, r);
        }
        r
    }

    fn is_plain_uncached(&self, t: Ty) -> bool {
        match t.kind() {
            TyKind::Int(_) | TyKind::Usize | TyKind::Isize | TyKind::Float(_) => true,
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } => self.is_plain(*elem),
            TyKind::Tuple { elems, .. } => elems.iter().all(|e| self.is_plain(*e)),
            TyKind::Param(p) => self.param_has(*p, Constraint::Plain),
            TyKind::Adt(id, _) => match self.prog.adts.get(id).map(|d| &d.kind) {
                Some(tir::AdtKind::Struct { fields, bits: None, .. }) => fields.iter().all(|f| self.is_plain(f.ty)),
                // A `#bits` struct whose fields are `bool`, integers or such structs (§5.19).
                Some(tir::AdtKind::Struct { fields, bits: Some(_), .. }) => fields.iter().all(|f| {
                    f.ty.is_integer() || f.ty.is_bool() || (self.bits_struct_ty(f.ty).is_some() && self.is_plain(f.ty))
                }),
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
            effects: self.subst_effects(&sig.effects, &map),
            generics: vec![],
        };
        let name = self.prog.fns[f.index()].name;
        let kind = match self.prog.fns[f.index()].kind {
            tir::FnKind::Clause => tir::FnKind::Clause,
            _ => tir::FnKind::Named,
        };
        self.report_soa_errors(span);
        let id = self.new_fn(name, self.prog.fns[f.index()].span, inst_sig, kind);
        self.instances.insert((f, args.clone()), id);
        self.instance_of.insert(id, (f, args.clone()));
        if Some(sig.module) == self.prelude
            && let Some(which) = match name.as_str() {
                "new" => Some(tir::HeapFn::New),
                "destroy" => Some(tir::HeapFn::Destroy),
                _ => None,
            }
        {
            self.prog.heap_fns.insert(id, which);
            if self.prog.heap_allocator.is_none()
                && let Some(p) = self.prelude
                && let Some(&d) = self.modules[p.0 as usize].scope.get(&Symbol::intern("heap_allocator"))
                && let super::DeclValue::Fn(h) = self.resolve_decl(d)
            {
                self.prog.heap_allocator = Some(h);
            }
        }
        if !args.iter().any(|a| has_param(*a)) {
            let depth = self.mono_depth + 1;
            if depth > MAX_MONO_DEPTH {
                self.error(
                    span,
                    format!(
                        "generic instantiations nest more than {} deep; does a generic function call itself \
                         with ever-larger type arguments? (§16.7)",
                        MAX_MONO_DEPTH
                    ),
                );
                return id;
            }
            self.mono_queue.push(MonoRequest { generic: f, instance: id, args, span, depth });
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
                    "more than 100000 generic instantiations, the limit of this implementation (§23.5)",
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
        let outer_depth = std::mem::replace(&mut self.mono_depth, req.depth);
        self.check_len_exprs(&map, Some(req.span));
        let mut cx =
            MonoCx { map: &map, locals: vec![], fn_map: HashMap::new(), handler_map: HashMap::new(), span: req.span };
        cx.fn_map.insert(req.generic, req.instance);
        cx.locals = locals.into_iter().map(|l| tir::LocalDecl { ty: self.mono_ty(&mut cx, l.ty), ..l }).collect();
        let body = self.mono_expr(&mut cx, body);
        let locals = std::mem::take(&mut cx.locals);
        self.report_soa_errors(req.span);
        self.mono_depth = outer_depth;
        // An instance may need a type larger than any object (a computed array length).
        for l in &locals {
            if !self.type_size_ok(l.ty, req.span) {
                break;
            }
        }
        let def = &mut self.prog.fns[req.instance.index()];
        def.params = params;
        def.locals = locals;
        def.hints = hints;
        def.body = Some(body);
    }

    fn mono_expr(&mut self, cx: &mut MonoCx, mut e: Expr) -> Expr {
        let had_param = has_param(e.ty);
        e.ty = self.mono_ty(cx, e.ty);
        // A compile-time value parameter becomes its argument (§16.5).
        if let tir::ExprKind::ValueParam(p) = e.kind
            && let Some((_, v)) = cx.map.iter().find(|(q, _)| *q == p)
            && let TyKind::Value(k) = v.kind()
        {
            e.kind = match k {
                ConstKey::Bytes(t, b) => match k.as_int() {
                    Some(i) => tir::ExprKind::Int(i),
                    None if t.is_bool() => tir::ExprKind::Bool(b.first() == Some(&1)),
                    None if t.is_float() => tir::ExprKind::ConstBytes(
                        crate::layout::int_bytes_swap_order(b, self.target.little_endian).into(),
                    ),
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
                    let msg = format!(
                        "the literal {} does not fit in `{}` (in an instantiation used here)",
                        super::untyped::show_int(v),
                        e.ty
                    );
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
        if let tir::ExprKind::IntLimit { of, max } = &e.kind {
            let (t, max) = (self.subst(*of, cx.map), *max);
            e.kind = match super::expr::int_limit(t, max, &self.target) {
                Some(v) => tir::ExprKind::Int(v),
                None => tir::ExprKind::IntLimit { of: t, max },
            };
        }
        if let tir::ExprKind::TypeName { of } = &e.kind {
            let t = self.subst(*of, cx.map);
            e.kind = if has_param(t) {
                tir::ExprKind::TypeName { of: t }
            } else {
                tir::ExprKind::Str(std::rc::Rc::from(t.to_string().as_bytes()))
            };
        }
        if let tir::ExprKind::TypeIs { a, b, negated } = &e.kind {
            let (a, b, negated) = (self.subst(*a, cx.map), self.subst(*b, cx.map), *negated);
            e.kind = if has_param(a) || has_param(b) {
                tir::ExprKind::TypeIs { a, b, negated }
            } else {
                tir::ExprKind::Bool((a == b) != negated)
            };
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
                *effect = self.subst_eref(effect, cx.map);
            }
            tir::ExprKind::HandlerRef(h) => {
                *h = self.mono_handler(cx, *h);
            }
            tir::ExprKind::Write { sink: tir::WriteSink::Perform { effect, .. }, .. } => {
                *effect = self.subst_eref(effect, cx.map);
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
        // A static assertion of the generic body, checked for this instance.
        if let tir::ExprKind::Builtin { op: tir::Builtin::StaticAssert(id), args } = &e.kind {
            let msg = self.deferred_asserts[*id as usize].clone();
            let (cond, span) = (args[0].clone(), e.span);
            self.check_static_assert(&cond, msg.as_deref(), span, Some(cx.span));
            return super::func::unit(span);
        }
        if let tir::ExprKind::WriteDeferred { .. } = e.kind {
            return self.resolve_write(cx, e);
        }
        if let tir::ExprKind::SoaDeferred { .. } = e.kind {
            return self.soa_expand(&mut cx.locals, e);
        }
        // `.bits` between types that mention parameters is size-checked per instance (§6.6).
        if let tir::ExprKind::Builtin { op: tir::Builtin::Bits, args } = &e.kind
            && !cx.map.is_empty()
            && !has_param(e.ty)
            && !has_param(args[0].ty)
        {
            let (src, t) = (args[0].ty, e.ty);
            let (a, b) = self.with_layouts(|l| (l.size_of(src), l.size_of(t)));
            if a != b {
                let msg = format!(
                    "`.bits` needs equal sizes, but `{}` has {} bytes and `{}` has {} (in an instantiation used here)",
                    src, a, t, b
                );
                self.error(cx.span, msg);
            }
        }
        // Format specs on generic arguments are checked in each instance.
        if let tir::ExprKind::Write { specs, args, .. } = &e.kind
            && !cx.map.is_empty()
        {
            self.check_specs(cx, specs, args);
        }
        e
    }

    fn check_specs(&mut self, cx: &MonoCx, specs: &[tir::FmtSpec], args: &[Expr]) {
        for (spec, a) in specs.iter().zip(args) {
            if let Err(msg) = super::call::check_spec(spec, a.ty) {
                self.error(cx.span, format!("{} (in an instantiation used here)", msg));
            }
        }
    }

    /// Splits the template of a `WriteDeferred`, now a string, and its arguments
    /// (§21.6.1), reporting a mismatch at the instantiating call (§16.1).
    fn resolve_write(&mut self, cx: &mut MonoCx, e: Expr) -> Expr {
        let span = e.span;
        let tir::ExprKind::WriteDeferred { sink, template, args, tmp } = e.kind else { unreachable!() };
        let tir::ExprKind::Str(t) = &template.kind else {
            self.error(cx.span, "internal error: a template parameter without a string value");
            return e_unit(span);
        };
        let (pieces, specs) = match super::call::parse_template(t) {
            Ok(t) => t,
            Err(msg) => {
                self.error(cx.span, format!("invalid template (in an instantiation used here): {}", msg));
                return e_unit(span);
            }
        };
        let n = pieces.len() - 1;
        let at = |cx: &MonoCx| cx.span;
        // The values are a tuple of one per placeholder (§21.6.1).
        let tys: Option<Vec<Ty>> = match &args {
            None => Some(vec![]),
            Some(a) => match a.ty.kind() {
                TyKind::Tuple { elems, .. } => Some(elems.to_vec()),
                _ => None,
            },
        };
        match &tys {
            Some(t) if t.len() == n => {
                for (spec, t) in specs.iter().zip(t) {
                    if let Err(msg) = super::call::check_spec(spec, *t) {
                        self.error(cx.span, format!("{} (in an instantiation used here)", msg));
                    }
                }
            }
            _ => {
                let given = match (&tys, &args) {
                    (Some(t), _) if t.len() == 1 => "1 value is".to_string(),
                    (Some(t), _) => format!("{} values are", t.len()),
                    (None, Some(a)) => format!("a `{}` is", a.ty),
                    (None, None) => "no values are".to_string(),
                };
                let what = if n == 1 { "1 placeholder".to_string() } else { format!("{} placeholders", n) };
                let msg = format!("the template has {}, but {} given (in an instantiation used here)", what, given);
                self.error(at(cx), msg);
                return e_unit(span);
            }
        }
        let write =
            |args: Vec<Expr>| Expr { ty: Ty::unit(), kind: tir::ExprKind::Write { sink, pieces, specs, args }, span };
        let Some(a) = args else { return write(vec![]) };
        if n == 0 {
            // Evaluated for its effects, if any, then written as nothing.
            let stmts = vec![tir::Stmt::Let { local: tmp, init: Some(*a) }, tir::Stmt::Expr(write(vec![]))];
            return super::func::block_of(stmts, span);
        }
        let elems = tys.unwrap();
        let local = |ty| Expr { ty, kind: tir::ExprKind::Local(tmp), span };
        let fields = (0..n)
            .map(|i| Expr {
                ty: elems[i],
                kind: tir::ExprKind::Field { base: Box::new(local(a.ty)), index: i as u32 },
                span,
            })
            .collect();
        let stmts = vec![tir::Stmt::Let { local: tmp, init: Some(*a) }, tir::Stmt::Expr(write(fields))];
        super::func::block_of(stmts, span)
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
            effects: self.subst_effects(&sig.effects, cx.map),
            ..sig
        };
        let new_id = self.new_fn(name, span, new_sig, kind);
        cx.fn_map.insert(id, new_id);
        self.nested_owner.insert(new_id, new_id);
        let def = &self.prog.fns[id.index()];
        let (params, locals, body) = (def.params.clone(), def.locals.clone(), def.body.clone());
        // Including the effects a literal performs where it is written (§11.6).
        let def_effects = def.effects.clone();
        let effects = self.subst_effects(&def_effects, cx.map);
        let locals: Vec<tir::LocalDecl> =
            locals.into_iter().map(|l| tir::LocalDecl { ty: self.mono_ty(cx, l.ty), ..l }).collect();
        let parent = parent.map(|p| self.mono_fn(cx, p));
        let outer_locals = std::mem::replace(&mut cx.locals, locals);
        let body = body.map(|b| self.mono_expr(cx, b));
        let locals = std::mem::replace(&mut cx.locals, outer_locals);
        let nd = &mut self.prog.fns[new_id.index()];
        nd.params = params;
        nd.locals = locals;
        nd.body = body;
        nd.parent = parent;
        nd.effects = effects;
        new_id
    }

    /// Substitutes type parameters in `t`; the type of a handler literal of the generic
    /// becomes that of its copy.
    fn mono_ty(&mut self, cx: &mut MonoCx, t: Ty) -> Ty {
        match t.kind() {
            TyKind::Handler(h) => Ty::intern(TyKind::Handler(self.mono_handler(cx, HandlerId(*h)).0)),
            _ => self.subst(t, cx.map),
        }
    }

    fn mono_handler(&mut self, cx: &mut MonoCx, h: HandlerId) -> HandlerId {
        if let Some(&n) = cx.handler_map.get(&h) {
            return n;
        }
        let lit = &self.prog.handlers[h.index()];
        let (lit_effect, clauses) = (lit.effect.clone(), lit.clauses.clone());
        let effect = self.subst_eref(&lit_effect, cx.map);
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
    /// The locals of the function whose body is being produced, which lowering a
    /// `#soa` operation may add to.
    locals: Vec<tir::LocalDecl>,
    fn_map: HashMap<FnId, FnId>,
    handler_map: HashMap<HandlerId, HandlerId>,
    span: Span,
}

/// The error for a type argument `t` with the second-class part `part`.
pub fn not_first_class(t: Ty, part: Ty) -> String {
    if t == part {
        format!("`{}` is not a first-class type, so it cannot be a type argument (§16.1)", t)
    } else {
        format!("`{}` contains the second-class type `{}`, so it cannot be a type argument (§16.1)", t, part)
    }
}

/// The second-class part of `t` (a `fn(…)` type, a handler, `type`), if it has one: such
/// a type is not a valid type argument (§16.1). Function pointers are first-class.
pub fn second_class_part(t: Ty) -> Option<Ty> {
    match t.kind() {
        TyKind::Fn(_) => Some(t),
        _ => inner_second_class_part(t),
    }
}

/// `second_class_part` for the parts of a type, where a `fn(…)` type was already
/// reported when the type was formed (§11.6).
fn inner_second_class_part(t: Ty) -> Option<Ty> {
    match t.kind() {
        TyKind::Fn(_) => None,
        TyKind::Handler(_) | TyKind::Type => Some(t),
        TyKind::FnPtr(_) => None,
        TyKind::Ptr { pointee: x, .. }
        | TyKind::ManyPtr { elem: x, .. }
        | TyKind::Slice { elem: x, .. }
        | TyKind::Array { elem: x, .. }
        | TyKind::ParamArray { elem: x, .. }
        | TyKind::Optional(x)
        | TyKind::Cont(x) => inner_second_class_part(*x),
        TyKind::Result { err, ok } => inner_second_class_part(*err).or_else(|| inner_second_class_part(*ok)),
        TyKind::Tuple { elems, .. } => elems.iter().find_map(|x| inner_second_class_part(*x)),
        TyKind::Adt(_, args) => args.iter().find_map(|a| match a {
            crate::types::GenericArg::Type(x) => inner_second_class_part(*x),
            _ => None,
        }),
        _ => None,
    }
}

pub fn has_param(t: Ty) -> bool {
    t.has_param()
}

/// Replaces type parameters in `t`, without re-instantiating the generic types it contains
/// (see [`Sema::subst`] for that).
pub fn subst_ty(t: Ty, map: &[(ParamId, Ty)]) -> Ty {
    if map.is_empty() || !has_param(t) {
        return t;
    }
    t.map(&mut |x| match x.kind() {
        TyKind::Param(p) => map.iter().find(|(q, _)| q == p).map(|(_, a)| *a),
        _ => None,
    })
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
                        TyKind::Array { .. } | TyKind::ParamArray { .. } => {
                            let Some((len, _)) = self.spread_array(vt, e.span) else { return error_expr(span) };
                            for i in 0..len {
                                let lit = Box::new(crate::syntax::token::IntLit {
                                    value: i.into(),
                                    suffix: None,
                                    is_char: false,
                                });
                                let index = Box::new(synthetic(ast::ExprKind::Int(lit), e.span));
                                let elem = synthetic(ast::ExprKind::Index { base: Box::new(base()), index }, e.span);
                                written.push(ast::Arg { kind: ArgKind::Positional(elem), span: e.span });
                            }
                            continue;
                        }
                        _ => match self.sema.struct_fields(vt) {
                            Some(fs) if self.hidden_field(vt).is_none() => fs.iter().map(|f| Some(f.name)).collect(),
                            _ => {
                                self.error(
                                    e.span,
                                    format!(
                                        "a spread needs a tuple, an array or a struct with visible fields, not `{}`",
                                        vt
                                    ),
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
        let call = self.generic_call_packed(f, receiver, &written, span);
        let ty = call.ty;
        Expr { ty, kind: tir::ExprKind::Block(Box::new(tir::Block { stmts, tail: Some(call), span })), span }
    }

    pub fn generic_call_with(&mut self, f: FnId, receiver: Option<Expr>, args: &[ast::Arg], span: Span) -> Expr {
        // A rest parameter's arguments become one tuple argument (§11.3).
        if self.sema.sigs[&f].params.iter().any(|p| p.rest) {
            let sig = self.sema.sigs[&f].clone();
            let effect_params = self.sema.effect_params.clone();
            let recv = receiver.as_ref().and_then(|_| sig.params.iter().position(|p| p.generic.is_none()));
            let named: Vec<crate::symbol::Symbol> = args
                .iter()
                .filter_map(|a| match &a.kind {
                    ArgKind::Named(n, _) => Some(n.name),
                    _ => None,
                })
                .collect();
            let slot = |i: usize| {
                Some(i) != recv
                    && !inferable_param(&sig, i, &effect_params)
                    && !sig.params[i].is_site()
                    && !named.contains(&sig.params[i].name)
            };
            match super::call::pack_rest_args(&sig.params, slot, args, span) {
                Some(Ok(packed)) => return self.generic_call_packed(f, receiver, &packed, span),
                Some(Err((sp, msg))) => {
                    self.error(sp, msg);
                    return error_expr(span);
                }
                None => {}
            }
        }
        self.generic_call_packed(f, receiver, args, span)
    }

    fn generic_call_packed(&mut self, f: FnId, receiver: Option<Expr>, args: &[ast::Arg], span: Span) -> Expr {
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
        // `f(u8)` with no run-time argument names an instantiation (§16.7).
        let inst_only = receiver.is_none() && self.is_instantiation(&sig, args, &positional);
        // An inferable compile-time parameter is never bound positionally (§16.2): it is
        // inferred, or written by name. Positional arguments bind the other parameters,
        // in order.
        let effect_params = self.sema.effect_params.clone();
        let inferable = |i: usize| inferable_param(&sig, i, &effect_params);
        let targets: Vec<usize> = (0..sig.params.len())
            .filter(|&i| {
                !inferable(i)
                    && !named.contains(&sig.params[i].name)
                    && Some(i) != recv_index
                    && !sig.params[i].is_site()
            })
            .collect();
        if positional.len() > targets.len() {
            let what = if receiver.is_some() { " besides the receiver" } else { "" };
            let mut msg = format!("too many arguments: the function takes {}{}", targets.len(), what);
            // `max(i32, xs)`: a type written where it would be inferred.
            if let Some(&first) = positional.first()
                && !self.clearly_value(first)
                && self.denotes_type(first)
                && let Some(p) = (0..sig.params.len()).find(|&i| inferable(i) && !named.contains(&sig.params[i].name))
            {
                msg.push_str(&format!(
                    "; a type parameter that the arguments determine is written by name, if at all: `{}: {}`",
                    sig.params[p].name,
                    self.sema.sess.sources.text(first.span)
                ));
            }
            self.error(span, msg);
            return error_expr(span);
        }
        // Inferable parameters written by name fix their arguments' types: the run-time
        // arguments are then coerced to them rather than inferring.
        let explicit = sig.params.iter().enumerate().any(|(i, p)| inferable(i) && named.contains(&p.name));
        let mut binds: HashMap<ParamId, Ty> = HashMap::new();
        // Arguments whose types still contain untyped literals.
        let mut literal_args: Vec<(Ty, Ty, Span)> = vec![];
        // Set when two arguments bind a type parameter differently (reported once).
        let mut conflict = false;
        let mut values: Vec<Option<Expr>> = vec![None; sig.params.len()];
        // The statements that evaluate the call arguments of iterator arguments (§13.5).
        let mut iter_pre: Vec<(usize, Vec<tir::Stmt>)> = vec![];
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
        // Types first, then run-time arguments, then compile-time values, whose types may
        // mention type parameters that the run-time arguments determine.
        // Function literals come after the other run-time arguments: they take their
        // parameter types from what those determine.
        let rank = |i: usize, e: &ast::Expr| match sig.params[i].generic {
            Some(p) if self.sema.value_params.contains_key(&p) => 3,
            Some(_) => 0,
            None if matches!(e.kind, ast::ExprKind::Fn(_)) => 2,
            None => 1,
        };
        let source_order: Vec<usize> = sources.iter().map(|(i, _)| *i).collect();
        sources.sort_by_key(|(i, e)| rank(*i, e));
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
        // Type parameters the receiver fixes: arguments are coerced to them (§16.2).
        let fixed: Vec<ParamId> = binds.keys().copied().collect();
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
                let t = if self.sema.effect_params.contains(&pid) {
                    let module = self.module;
                    self.sema.resolve_effect_arg(module, e)
                } else {
                    self.resolve_type(e)
                };
                binds.insert(pid, t);
            } else {
                let bound: Vec<(ParamId, Ty)> = binds.iter().map(|(k, v)| (*k, *v)).collect();
                let expect = self.sema.subst(p.ty, &bound);
                // A function type with parameters still unbound still helps a function
                // literal argument: it takes the parts that are known.
                let partial = matches!(expect.kind(), TyKind::Fn(_) | TyKind::FnPtr(_));
                // Whether the parameter's type still mentions a type parameter of the callee
                // that no argument has bound yet. (Parameters of an enclosing generic body
                // are fixed there: `g(?T, null)` expects a `?T`.)
                let mut unbound = false;
                p.ty.walk(&mut |x| {
                    unbound |=
                        matches!(x.kind(), TyKind::Param(q) if sig.generics.contains(q) && !binds.contains_key(q))
                });
                // `f(u8)` for `f :: fn(T: type, x: T)`: a type where a value is expected,
                // meant for a type parameter that is written by name (§16.2).
                if !matches!(p.ty.kind(), TyKind::Type)
                    && !self.clearly_value(e)
                    && !matches!(e.kind, ast::ExprKind::Tuple(_) | ast::ExprKind::Paren(_))
                    && self.denotes_type(e)
                    && let Some(q) = sig.params.iter().enumerate().find(|(k, q)| {
                        q.generic.is_some()
                            && inferable_param(&sig, *k, &self.sema.effect_params)
                            && !named.contains(&q.name)
                    })
                {
                    let text = self.sema.sess.sources.text(e.span).to_string();
                    self.error(
                        e.span,
                        format!(
                            "expected a value for `{}`, found a type; a type parameter that the arguments determine is written by name, if at all: `{}: {}`",
                            p.name, q.1.name, text
                        ),
                    );
                    return error_expr(span);
                }
                // `---` for a parameter whose type is known: `new(T, ---)`.
                if let ast::ExprKind::Uninit = e.kind
                    && !unbound
                {
                    values[*i] = Some(self.arg_value(e, expect));
                    continue;
                }
                let lit = self.iterator_arg(e, expect);
                if !self.iter_pre.is_empty() {
                    iter_pre.push((*i, std::mem::take(&mut self.iter_pre)));
                }
                let e = lit.as_ref().unwrap_or(e);
                self.temp_ref_arg = super::call::is_const_ref_arg(e, p.ty);
                let v = self.expr(e, (!unbound || partial).then_some(expect));
                self.temp_ref_arg = false;
                if lit.is_none() && self.iterator_call_as_value(e, &v, p.ty) {
                    return error_expr(span);
                }
                let v = self.array_as_slice(v, p.ty);
                let mut mentions_free = false;
                p.ty.walk(&mut |x| mentions_free |= matches!(x.kind(), TyKind::Param(q) if !fixed.contains(q)));
                if !fixed.is_empty() && !mentions_free && !has_param(expect) {
                    // Coerced to `expect` with the others below.
                    values[*i] = Some(v);
                    continue;
                }
                let t = self.infer.resolve(v.ty);
                if t.has_infer() {
                    literal_args.push((p.ty, v.ty, v.span));
                }
                let mut b2 = HashMap::new();
                super::effects::match_params_pub(p.ty, t, &mut b2);
                for (k, v2) in b2 {
                    match binds.get(&k) {
                        Some(prev) if *prev != v2 && explicit => {}
                        Some(prev) if *prev != v2 && !same_int_value(*prev, v2) => {
                            conflict = true;
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
        if conflict {
            return error_expr(span);
        }
        // A type parameter that only untyped literals determine takes their fallback
        // types (§16.2): `log("{}", 5)` makes it `i32`.
        if sig.generics.iter().any(|p| !binds.contains_key(p) && !self.sema.value_params.contains_key(p)) {
            for (pty, aty, aspan) in std::mem::take(&mut literal_args) {
                let free: Vec<ParamId> = sig.generics.iter().copied().filter(|p| !binds.contains_key(p)).collect();
                let mut mentions = false;
                pty.walk(&mut |x| mentions |= matches!(x.kind(), TyKind::Param(p) if free.contains(p)));
                if !mentions {
                    continue;
                }
                let t = self.settle(aty, aspan);
                let mut b2 = HashMap::new();
                super::effects::match_params_pub(pty, t, &mut b2);
                for (k, v2) in b2 {
                    binds.entry(k).or_insert(v2);
                }
            }
        }
        // An omitted argument whose default is `()` determines its parameter's type:
        // `print`'s `args: A = ()` makes `A` the unit type when there are no arguments.
        for (p, v) in sig.params.iter().zip(&values) {
            if let (TyKind::Param(pid), None, Some(d)) = (p.ty.kind(), v, &p.default)
                && matches!(&d.kind, ast::ExprKind::Tuple(elems) if elems.is_empty())
            {
                binds.entry(*pid).or_insert(Ty::unit());
            }
        }
        // An effect-set parameter named in an effect of the function's own set
        // (`needs Sched(E)`) takes its effects from the one instantiation of that effect
        // available here, as an operation call does (§12.1, §16.6).
        for pid in &sig.generics {
            if binds.contains_key(pid) || !self.sema.effect_params.contains(pid) {
                continue;
            }
            for e in sig.effects.effects.iter().filter(|e| effect_args_name(e, *pid)) {
                if let [one] = self.available(e.effect).as_slice() {
                    for (x, y) in e.args.iter().zip(one.args.iter()) {
                        if let (GenericArg::Type(x), GenericArg::Type(y)) = (x, y) {
                            super::effects::match_params_pub(*x, *y, &mut binds);
                        }
                    }
                }
            }
        }
        // An argument with an error (already reported) leaves its parameters unbound;
        // say nothing more about them.
        let failed_arg = values.iter().flatten().any(|v| {
            let mut bad = false;
            self.infer.resolve(v.ty).walk(&mut |t| bad |= t.is_error());
            bad
        });
        let mut targs = vec![];
        for pid in &sig.generics {
            if failed_arg && !binds.contains_key(pid) {
                return error_expr(span);
            }
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
                // An effect-set parameter that nothing determines is empty (§16.6).
                None if self.sema.effect_params.contains(pid) => {
                    targs.push(Ty::intern(TyKind::Effects(crate::types::EffectSet::empty())))
                }
                None => {
                    // An array where a slice is expected says nothing about the slice's
                    // element type: arrays do not convert to slices (§6.7).
                    let array_for_slice = sig.params.iter().zip(&values).any(|(p, v)| {
                        matches!(p.ty.kind(), TyKind::Slice { .. })
                            && v.as_ref().is_some_and(|v| matches!(v.ty.kind(), TyKind::Array { .. }))
                    });
                    let pointer_for_slice = sig.params.iter().zip(&values).any(|(p, v)| {
                        matches!(p.ty.kind(), TyKind::Slice { .. })
                            && v.as_ref().is_some_and(|v| matches!(self.infer.resolve(v.ty).kind(), TyKind::Ptr { .. }))
                    });
                    let name = self.sema.prog.fns[f.index()].name;
                    let hint = if array_for_slice {
                        "; an array is not a slice: pass `a[..]`"
                    } else if pointer_for_slice && name.as_str() == "free" {
                        "; `free` releases a slice from `make`: release a value from `new` with `destroy(p)`"
                    } else if pointer_for_slice {
                        "; a pointer is not a slice"
                    } else {
                        ""
                    };
                    let how = match sig.params.iter().position(|q| q.generic == Some(*pid)) {
                        Some(k) if inferable_param(&sig, k, &self.sema.effect_params) => {
                            format!("write it by name: `{}({}: …)`", name, pid.name())
                        }
                        _ => format!("write it: `{}({}, …)`", name, pid.name()),
                    };
                    self.error(span, format!("cannot infer the type parameter `{}`; {}{}", pid.name(), how, hint));
                    return error_expr(span);
                }
            }
        }
        // Constraints are checked at each instantiation (§16.3).
        for (pid, t) in sig.generics.iter().zip(&targs) {
            if t.is_error() {
                return error_expr(span);
            }
            if let Some(part) = second_class_part(*t) {
                self.error(span, not_first_class(*t, part));
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
        if inst_only {
            let inst = self.sema.instantiate(f, targs, span);
            return self.fn_ref(inst, span);
        }
        let map: Vec<(ParamId, Ty)> = sig.generics.iter().copied().zip(targs.iter().copied()).collect();
        // The run-time arguments in the order written (the receiver first).
        let written: Vec<usize> = recv_index
            .into_iter()
            .chain(sources.iter().map(|(i, _)| *i))
            .filter(|&i| sig.params[i].generic.is_none() && values[i].is_some())
            .collect();
        let mut out = vec![];
        // The position in `out` of each parameter's argument.
        let mut slot = vec![usize::MAX; sig.params.len()];
        for (i, p) in sig.params.iter().enumerate() {
            if p.generic.is_some() {
                continue;
            }
            let pty = self.sema.subst(p.ty, &map);
            slot[i] = out.len();
            match values[i].take() {
                Some(v) => out.push(self.coerce(v, pty)),
                None => match &p.default {
                    Some(d) => {
                        let d = d.clone();
                        let v = self.param_default(sig.module, &d, pty, p.is_site(), span);
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
        let effects = self.sema.subst_effects(&sig.effects, &map);
        self.record_effects(&effects, span);
        // Named arguments out of parameter order are evaluated in the order written
        // (§11.3), into temporaries.
        let mut pre = vec![];
        if !iter_pre.is_empty() {
            // An iterator's call arguments are evaluated at its place among the arguments.
            let mut written = written;
            written.sort_by_key(|i| source_order.iter().position(|k| k == i));
            for i in written {
                match iter_pre.iter().position(|(k, _)| *k == i) {
                    Some(n) => pre.append(&mut iter_pre.remove(n).1),
                    None => {
                        let v = std::mem::replace(&mut out[slot[i]], e_unit(span));
                        out[slot[i]] = self.bind_temp(v, &mut pre);
                    }
                }
            }
        } else if written.windows(2).any(|w| w[0] > w[1]) {
            for i in written {
                let v = std::mem::replace(&mut out[slot[i]], e_unit(span));
                out[slot[i]] = self.bind_temp(v, &mut pre);
            }
        }
        let inst = self.sema.instantiate(f, targs, span);
        let call = Expr {
            ty: self.sema.subst(sig.ret, &map),
            kind: tir::ExprKind::Call { callee: tir::Callee::Fn(inst), args: out },
            span,
        };
        self.finish_call(pre, call, false)
    }
}

impl<'a, 's> FnCx<'a, 's> {
    /// Whether a call of generic function `sig` gives only compile-time arguments and
    /// leaves out a required run-time one: it then denotes the instantiation as a
    /// function value rather than calling it (§16.7). An argument for a type parameter
    /// must be a type, so `id(x)` for `id :: fn(T: type, x: T)` is still a call.
    fn is_instantiation(&mut self, sig: &super::FnSig, args: &[ast::Arg], positional: &[&ast::Expr]) -> bool {
        let named = |p: &ParamInfo| args.iter().any(|a| matches!(&a.kind, ArgKind::Named(n, _) if n.name == p.name));
        // No run-time argument is given, and at least one is needed.
        if sig.params.iter().any(|p| p.generic.is_none() && named(p)) {
            return false;
        }
        let needs_runtime = sig.params.iter().any(|p| p.generic.is_none() && p.default.is_none() && !p.is_site());
        if !needs_runtime || args.is_empty() {
            return false;
        }
        // The positional arguments bind the leading positional parameters, which must all
        // be compile-time ones, and every compile-time parameter must be given.
        let targets: Vec<usize> = (0..sig.params.len())
            .filter(|&i| {
                !inferable_param(sig, i, &self.sema.effect_params) && !named(&sig.params[i]) && !sig.params[i].is_site()
            })
            .collect();
        if positional.len() > targets.len()
            || targets[..positional.len()].iter().any(|&i| sig.params[i].generic.is_none())
        {
            return false;
        }
        let given = |i: usize| named(&sig.params[i]) || targets[..positional.len()].contains(&i);
        if (0..sig.params.len()).any(|i| sig.params[i].generic.is_some() && !given(i)) {
            return false;
        }
        for (e, &i) in positional.iter().zip(&targets) {
            let p = &sig.params[i];
            let is_value = p.generic.is_some_and(|g| self.sema.value_params.contains_key(&g));
            if !is_value && !self.denotes_type(e) {
                return false;
            }
        }
        true
    }

    /// A generic function named as a value where a function pointer type is expected:
    /// the type arguments are taken from that type (§16.7). None if they cannot be.
    pub fn generic_fn_from_expected(&mut self, f: FnId, expect: Option<Ty>, span: Span) -> Option<Expr> {
        let want = expect.map(|t| self.infer.resolve(t))?;
        let TyKind::FnPtr(want) = want.kind() else { return None };
        let sig = self.sema.sigs[&f].clone();
        let runtime: Vec<&ParamInfo> = sig.params.iter().filter(|p| p.generic.is_none()).collect();
        if runtime.len() != want.params.len() {
            return None;
        }
        let mut binds = HashMap::new();
        for (p, w) in runtime.iter().zip(want.params.iter()) {
            super::effects::match_params_pub(p.ty, *w, &mut binds);
        }
        super::effects::match_params_pub(sig.ret, want.ret, &mut binds);
        let mut targs = vec![];
        for pid in &sig.generics {
            if self.sema.value_params.contains_key(pid) {
                return None;
            }
            targs.push(*binds.get(pid)?);
        }
        for (pid, t) in sig.generics.iter().zip(&targs) {
            if let Some(part) = second_class_part(*t) {
                self.error(span, not_first_class(*t, part));
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
        let inst = self.sema.instantiate(f, targs, span);
        Some(self.fn_ref(inst, span))
    }

    /// True if `e` cannot denote a type: a literal or a local variable.
    fn clearly_value(&self, e: &ast::Expr) -> bool {
        match &e.kind {
            ast::ExprKind::Ident(id) => {
                self.lookup_local(id.name).is_some_and(|b| !matches!(b, super::func::LocalBinding::Type(_)))
            }
            ast::ExprKind::Int(_) | ast::ExprKind::Float(_) | ast::ExprKind::Str(_) | ast::ExprKind::Bool(_) => true,
            _ => false,
        }
    }

    /// Evaluates the argument of a compile-time value parameter of type `ty` (§16.5).
    fn comptime_arg(&mut self, ty: Ty, e: &ast::Expr) -> Option<Ty> {
        if let ast::ExprKind::Ident(id) = &e.kind
            && self.lookup_local(id.name).is_some_and(|b| {
                !matches!(
                    b,
                    super::func::LocalBinding::NestedFn(_)
                        | super::func::LocalBinding::Untyped(_)
                        | super::func::LocalBinding::Const { .. }
                )
            })
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
            // A computed length `[A + B]T` (§16.5) is a `usize`.
            TyKind::LenExpr { .. } if ty == Ty::usize() => return Some(v),
            TyKind::LenExpr { .. } => {
                self.error(span, format!("`{}` = {} is a `usize`, but `{}` has type `{}`", name, v, name, ty));
                return None;
            }
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
                    self.error(span, format!("`{}` = {} does not fit in `{}`", name, super::untyped::show_int(&i), ty));
                    return None;
                }
                let size = self.sema.with_layouts(|l| l.size_of(ty));
                Some(Ty::intern(TyKind::Value(crate::types::ConstKey::int(ty, &i, size))))
            }
            _ => Some(v),
        }
    }
}

/// Whether `a` and `b` are the same integer value, possibly of different types: a
/// compile-time argument `N: 3` and an array length 3 bind `N` alike.
/// Whether the compile-time parameter `i` of `sig` is inferable (§16.2): another
/// parameter's type mentions it, other than inside a computed array length. An inferable
/// parameter is never bound positionally.
pub fn inferable_param(sig: &super::FnSig, i: usize, effect_params: &HashSet<ParamId>) -> bool {
    let Some(pid) = sig.params[i].generic else { return false };
    if effect_params.contains(&pid) && sig.effects.effects.iter().any(|e| effect_args_name(e, pid)) {
        return true;
    }
    let is_pid = |t: Ty| matches!(t.kind(), TyKind::Param(x) if *x == pid);
    sig.params.iter().enumerate().any(|(j, q)| {
        if j == i {
            return false;
        }
        let (mut all, mut computed) = (0, 0);
        q.ty.walk(&mut |t| match t.kind() {
            TyKind::LenExpr { lhs, rhs, .. } => computed += is_pid(*lhs) as u32 + is_pid(*rhs) as u32,
            _ => all += is_pid(t) as u32,
        });
        all > computed
    })
}

/// Whether parameter `pid` occurs in the arguments of effect `e` (`Sched(E)`,
/// `Sched(E + Out)`), so that the instantiation of `e` available at a call determines it.
fn effect_args_name(e: &crate::types::EffectRef, pid: ParamId) -> bool {
    !e.is_splice()
        && e.args.iter().any(|a| {
            let GenericArg::Type(t) = a else { return false };
            let mut found = false;
            t.walk(&mut |x| found |= matches!(x.kind(), TyKind::Param(q) if *q == pid));
            found
        })
}

fn same_int_value(a: Ty, b: Ty) -> bool {
    match (a.kind(), b.kind()) {
        (TyKind::Value(x), TyKind::Value(y)) => x.as_int().is_some_and(|i| y.as_int() == Some(i)),
        _ => false,
    }
}

fn e_unit(span: Span) -> Expr {
    super::func::unit(span)
}
