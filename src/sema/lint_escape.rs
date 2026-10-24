//! The `dangling-address` lint: the address of a local, or a slice of a local array,
//! stored where it outlives the local (§15.1). Each binding of a loop body, a `match` arm
//! or a binding condition lives until the end of its scope, and a loop variable's storage
//! is reused by every iteration, so a pointer to one that is kept longer points to a copy
//! that is gone or that holds another element.
//!
//! The address is followed through the values that hold it (tuples, structs, arrays,
//! variants, optionals, blocks and `if`s). It is reported when it is
//!
//! - assigned to a place that lives longer: rooted at a local of an enclosing scope, or
//!   reached through a pointer, a slice or a global;
//! - passed to a function, for a parameter whose declared type is a type parameter (the
//!   element of `l.push(x)`, the value of `m.put(k, v)`), together with a mutable pointer
//!   to something that lives longer, which the function may store it in;
//! - returned, inside a value (returning the address itself is a compile error).

use super::Sema;
use super::lint::Finding;
use crate::source::Span;
use crate::tir::{Callee, CondPart, Expr, ExprKind, FnDef, FnId, LocalId, Pat, PatKind, Stmt};
use crate::types::TyKind;
use std::collections::HashMap;

/// How long a place lives, as the depth of the scope that holds it; storage reached
/// through a pointer, a slice or a global outlives every scope of the function.
const OUTER: i64 = -1;

/// Reports the dangling addresses in the body of function `def`.
pub(super) fn dangling_addresses(sema: &Sema, def: &FnDef, out: &mut Vec<Finding>) {
    let Some(body) = &def.body else { return };
    let mut w = Walk { sema, def, depth: HashMap::new(), cur: 0, out, reported: vec![] };
    for p in &def.params {
        w.depth.insert(*p, 0);
    }
    w.expr(body, true);
}

struct Walk<'a, 's> {
    sema: &'a Sema<'s>,
    def: &'a FnDef,
    /// The scope depth of each local declared so far.
    depth: HashMap<LocalId, i64>,
    cur: i64,
    out: &'a mut Vec<Finding>,
    reported: Vec<Span>,
}

impl Walk<'_, '_> {
    fn declare(&mut self, l: LocalId) {
        self.depth.insert(l, self.cur);
    }

    fn declare_pat(&mut self, p: &Pat) {
        match &p.kind {
            PatKind::Bind(l) => self.declare(*l),
            PatKind::Some(q) | PatKind::Ok(q) | PatKind::Err(q) => self.declare_pat(q),
            PatKind::Variant { payload: Some(q), .. } => self.declare_pat(q),
            PatKind::Fields(fs) => fs.iter().for_each(|(_, q)| self.declare_pat(q)),
            PatKind::Or(alts) => alts.iter().for_each(|q| self.declare_pat(q)),
            _ => {}
        }
    }

    /// The local a place is rooted at, through fields and array elements.
    fn root(e: &Expr) -> Option<LocalId> {
        match &e.kind {
            ExprKind::Local(l) => Some(*l),
            ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => Self::root(base),
            ExprKind::Index { base, .. } | ExprKind::Slice { base, .. }
                if matches!(base.ty.kind(), TyKind::Array { .. } | TyKind::ParamArray { .. }) =>
            {
                Self::root(base)
            }
            _ => None,
        }
    }

    /// How long the storage of place `p` lives (see [`OUTER`]), or `None` for a place
    /// this lint does not judge (a temporary).
    fn lifetime(&self, p: &Expr) -> Option<i64> {
        match &p.kind {
            ExprKind::Local(l) => self.depth.get(l).copied(),
            ExprKind::Upvar { .. } | ExprKind::Global(_) | ExprKind::Deref(_) => Some(OUTER),
            ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => self.lifetime(base),
            ExprKind::Index { base, .. } | ExprKind::Slice { base, .. } => {
                if matches!(base.ty.kind(), TyKind::Array { .. } | TyKind::ParamArray { .. }) {
                    self.lifetime(base)
                } else {
                    Some(OUTER)
                }
            }
            _ => None,
        }
    }

    /// The addresses of locals that the value of `e` holds: `(span of the address, local)`.
    fn held(e: &Expr, out: &mut Vec<(Span, LocalId)>) {
        match &e.kind {
            ExprKind::AddrOf(p) => {
                if let Some(l) = Self::root(p) {
                    out.push((e.span, l));
                }
            }
            ExprKind::Slice { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) => {
                if let Some(l) = Self::root(base) {
                    out.push((e.span, l));
                }
            }
            ExprKind::Coerce { expr, .. } | ExprKind::MakeErr(expr) => Self::held(expr, out),
            ExprKind::Aggregate(xs) | ExprKind::Array(xs) => xs.iter().for_each(|x| Self::held(x, out)),
            ExprKind::Variant { payload: Some(x), .. } => Self::held(x, out),
            ExprKind::Block(b) => {
                if let Some(t) = &b.tail {
                    Self::held(t, out)
                }
            }
            ExprKind::If { then, else_, .. } => {
                Self::held(then, out);
                if let Some(x) = else_ {
                    Self::held(x, out);
                }
            }
            _ => {}
        }
    }

    fn report(&mut self, at: Span, l: LocalId, why: &'static str) {
        let local = &self.def.locals[l.index()];
        if at == Span::DUMMY || local.name.as_str().starts_with('<') || self.reported.contains(&at) {
            return;
        }
        self.reported.push(at);
        self.out.push(Finding::DanglingAddress { span: at, name: local.name, local: local.span, why });
    }

    /// Reports the addresses that `value` holds of locals that `target` (a lifetime)
    /// outlives.
    fn check_store(&mut self, value: &Expr, target: i64, why: &'static str) {
        let mut held = vec![];
        Self::held(value, &mut held);
        for (at, l) in held {
            if self.depth.get(&l).is_some_and(|&d| target < d) {
                self.report(at, l, why);
            }
        }
    }

    fn call(&mut self, c: FnId, args: &[Expr]) {
        let template = self.sema.instance_of.get(&c).map_or(c, |(g, _)| *g);
        let Some(sig) = self.sema.sigs.get(&template) else { return };
        let params: Vec<_> = sig.params.iter().filter(|p| p.generic.is_none()).map(|p| p.ty).collect();
        // The longest-lived storage that the call may write to, through a mutable pointer.
        let target = args
            .iter()
            .filter(|a| matches!(a.ty.kind(), TyKind::Ptr { is_const: false, .. }))
            .filter_map(|a| match &a.kind {
                ExprKind::AddrOf(p) => self.lifetime(p),
                _ => Some(OUTER),
            })
            .min();
        let Some(target) = target else { return };
        for (a, t) in args.iter().zip(params) {
            if matches!(t.kind(), TyKind::Param(_)) {
                self.check_store(a, target, "passed to be stored");
            }
        }
    }

    /// Walks `e`; `tail` is whether its value is the function's result.
    fn expr(&mut self, e: &Expr, tail: bool) {
        match &e.kind {
            ExprKind::Block(b) => {
                self.cur += 1;
                for s in &b.stmts {
                    self.stmt(s);
                }
                if let Some(t) = &b.tail {
                    self.expr(t, tail);
                }
                self.cur -= 1;
                return;
            }
            ExprKind::Return { levels: 0, value } => {
                self.check_store(value, OUTER, "returned");
                self.expr(value, false);
                return;
            }
            ExprKind::Match { scrutinee, arms } => {
                self.expr(scrutinee, false);
                for a in arms {
                    self.cur += 1;
                    self.declare_pat(&a.pat);
                    if let Some(g) = &a.guard {
                        self.expr(g, false);
                    }
                    self.expr(&a.body, tail);
                    self.cur -= 1;
                }
                return;
            }
            ExprKind::If { cond, then, else_ } => {
                self.cur += 1;
                self.cond(cond);
                self.expr(then, tail);
                self.cur -= 1;
                if let Some(x) = else_ {
                    self.expr(x, tail);
                }
                return;
            }
            ExprKind::Loop { cond, body, step, .. } => {
                self.cur += 1;
                self.cond(cond);
                self.expr_block(body);
                if let Some(s) = step {
                    self.expr(s, false);
                }
                self.cur -= 1;
                return;
            }
            ExprKind::Else { lhs, capture, rhs } => {
                self.expr(lhs, false);
                self.cur += 1;
                if let Some(l) = capture {
                    self.declare(*l);
                }
                self.expr(rhs, tail);
                self.cur -= 1;
                return;
            }
            ExprKind::Call { callee: Callee::Fn(c), args } => self.call(*c, args),
            _ => {}
        }
        if tail {
            self.check_store(e, OUTER, "returned");
        }
        e.for_each_child(&mut |c| self.expr(c, false));
    }

    fn expr_block(&mut self, b: &crate::tir::Block) {
        self.cur += 1;
        for s in &b.stmts {
            self.stmt(s);
        }
        if let Some(t) = &b.tail {
            self.expr(t, false);
        }
        self.cur -= 1;
    }

    fn cond(&mut self, cond: &[CondPart]) {
        for c in cond {
            match c {
                CondPart::Bool(x) => self.expr(x, false),
                CondPart::Match { pat, value } => {
                    self.expr(value, false);
                    self.declare_pat(pat);
                }
            }
        }
    }

    fn stmt(&mut self, s: &Stmt) {
        match s {
            Stmt::Let { local, init } => {
                if let Some(x) = init {
                    self.expr(x, false);
                }
                self.declare(*local);
            }
            Stmt::LetTuple { locals, init } => {
                self.expr(init, false);
                for l in locals.iter().flatten() {
                    self.declare(*l);
                }
            }
            Stmt::Assign { place, value } => {
                self.expr(place, false);
                self.expr(value, false);
                if let Some(t) = self.lifetime(place) {
                    self.check_store(value, t, "stored");
                }
            }
            Stmt::AssignTuple { places, value } => {
                for p in places.iter().flatten() {
                    self.expr(p, false);
                }
                self.expr(value, false);
            }
            Stmt::Expr(x) => self.expr(x, false),
            Stmt::Defer { body, .. } => self.expr(body, false),
        }
    }
}
