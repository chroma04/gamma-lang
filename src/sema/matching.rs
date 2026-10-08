//! `match` (§8.6), binding conditions (§8.2) and pattern tests in `is` (§7.6).

use super::exhaust::Checker;
use super::func::{FnCx, error_expr};
use super::pattern::PatCtx;
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind, PatternKind};
use crate::tir::{self, Expr, LocalId, Pat, PatKind};
use crate::types::Ty;

impl<'a, 's> FnCx<'a, 's> {
    pub fn checker(&self) -> Checker<'_> {
        Checker { adts: &self.sema.prog.adts, target: &self.sema.target }
    }

    /// Checks a `match`. With `value` false, arms are statements (§7.12).
    pub fn match_expr(&mut self, m: &ast::MatchExpr, value: bool, expect: Option<Ty>) -> Expr {
        let scrutinee = self.expr(&m.scrutinee, None);
        let sty = self.infer.resolve(scrutinee.ty);
        if sty.has_infer() {
            // Integer literals as scrutinees take their fallback now.
            self.infer.unify(sty, Ty::i32()).ok();
        }
        let sty = self.infer.resolve(scrutinee.ty);
        let mut arms = vec![];
        for arm in &m.arms {
            self.push_scope();
            let pat = self.arm_pattern(&arm.pats, sty);
            let guard = arm.guard.as_ref().map(|g| {
                let e = self.expr(g, Some(Ty::bool()));
                self.expect_bool(e)
            });
            let body = match &arm.body.kind {
                ExprKind::Block(b) => self.block_expected(b, value, expect),
                _ if value => self.expr(&arm.body, expect),
                _ => self.stmt_expr(&arm.body),
            };
            self.pop_scope();
            arms.push(tir::Arm { pat, guard, body });
        }
        // Exhaustiveness and reachability.
        if !sty.is_error() {
            let report = {
                let checker = self.checker();
                let list: Vec<(&Pat, bool)> = arms.iter().map(|a| (&a.pat, a.guard.is_some())).collect();
                checker.check_match(sty, &list)
            };
            if let Some(missing) = report.missing {
                self.error(m.span, format!("this `match` is not exhaustive: `{}` is not covered", missing));
            }
            for i in report.unreachable {
                self.error(m.arms[i].span, "this arm can never be chosen: earlier arms cover it");
            }
        }
        let ty = if value {
            match expect {
                Some(t) => t,
                None => arms.iter().map(|a| a.body.ty).find(|t| !t.is_never()).unwrap_or(Ty::never()),
            }
        } else if !arms.is_empty() && arms.iter().all(|a| a.body.ty.is_never()) || arms.is_empty() {
            Ty::never()
        } else {
            Ty::unit()
        };
        if value && !ty.is_never() {
            for a in &mut arms {
                let body = std::mem::replace(&mut a.body, error_expr(m.span));
                a.body = if body.ty.is_never() { body } else { self.coerce(body, ty) };
            }
        }
        Expr { ty, kind: tir::ExprKind::Match { scrutinee: Box::new(scrutinee), arms }, span: m.span }
    }

    /// The pattern of an arm: alternatives must bind the same names with the same types.
    fn arm_pattern(&mut self, pats: &[ast::Pattern], ty: Ty) -> Pat {
        let mut first_binds: Vec<(Symbol, LocalId, Span)> = vec![];
        let mut alts = vec![];
        for (i, p) in pats.iter().enumerate() {
            let mut binds = vec![];
            let mut pt = self.pattern(p, ty, PatCtx::Refutable, &mut binds);
            if i == 0 {
                first_binds = binds;
            } else {
                let mut names: Vec<Symbol> = binds.iter().map(|b| b.0).collect();
                let mut first_names: Vec<Symbol> = first_binds.iter().map(|b| b.0).collect();
                names.sort();
                first_names.sort();
                if names != first_names {
                    self.error(p.span, "every alternative of an arm must bind the same names");
                }
                // Make the alternative bind the first alternative's locals.
                let mut map = vec![];
                for (n, id, sp) in &binds {
                    if let Some((_, first_id, _)) = first_binds.iter().find(|b| b.0 == *n) {
                        let (t1, t2) = (self.local_ty(*first_id), self.local_ty(*id));
                        if t1 != t2 {
                            self.error(
                                *sp,
                                format!("`{}` has type `{}` here but `{}` in the first alternative", n, t2, t1),
                            );
                        }
                        map.push((*id, *first_id));
                    }
                }
                remap_binds(&mut pt, &map);
            }
            alts.push(pt);
        }
        self.declare_binds(first_binds);
        if alts.len() == 1 {
            alts.pop().unwrap()
        } else {
            let span = alts[0].span.to(alts.last().unwrap().span);
            Pat { ty, kind: PatKind::Or(alts), span }
        }
    }

    /// `pattern := value` in a condition (§8.2): refutable; binds names for the rest.
    pub fn binding_cond(&mut self, pat: &ast::Pattern, value: &ast::Expr) -> tir::CondPart {
        let v = self.expr(value, None);
        let vty = self.infer.resolve(v.ty);
        let mut binds = vec![];
        let p = self.pattern(pat, vty, PatCtx::Refutable, &mut binds);
        if !vty.is_error() && self.checker().irrefutable(&p) {
            self.error(pat.span, "this condition always succeeds; use a plain binding instead");
        }
        self.declare_binds(binds);
        tir::CondPart::Match { pat: p, value: v }
    }

    /// If the right operand of `is` is a pattern form that is not a value (§7.6),
    /// returns it as a pattern.
    pub fn is_pattern_operand(&mut self, rhs: &ast::Expr, lhs_ty: Ty) -> Option<ast::Pattern> {
        let pat = expr_to_pattern(rhs)?;
        if contains_wildcard(&pat) || self.omits_payload(&pat, lhs_ty) || self.binds_unknown_name(&pat) {
            return Some(pat);
        }
        None
    }

    /// True if a nested name in `p` is not a known value: an attempt to bind,
    /// which `is` does not allow (§7.6).
    fn binds_unknown_name(&mut self, p: &ast::Pattern) -> bool {
        match &p.kind {
            PatternKind::Bind(n) => {
                let module = self.module;
                self.lookup_local(n.name).is_none() && self.sema.lookup_module_name(module, n.name).is_none()
            }
            PatternKind::Err(q) => self.binds_unknown_name(q),
            PatternKind::Path { args: Some(args), .. } | PatternKind::Tuple(args) => {
                args.iter().any(|a| self.binds_unknown_name(&a.pat))
            }
            _ => false,
        }
    }

    /// True if `p` names a variant with a payload without giving one (`.Circle`).
    fn omits_payload(&mut self, p: &ast::Pattern, ty: Ty) -> bool {
        let ty = super::call::strip_wrappers(self.infer.resolve(ty));
        match &p.kind {
            PatternKind::Path { path, args: None } => {
                let name = match &path.kind {
                    ExprKind::DotIdent(n) | ExprKind::Field { name: n, .. } => n.name,
                    _ => return false,
                };
                self.sema
                    .union_variants(ty)
                    .is_some_and(|(vs, _)| vs.iter().any(|v| v.name == name && v.payload.is_some()))
            }
            PatternKind::Err(inner) => match ty.kind() {
                crate::types::TyKind::Result { err, .. } => self.omits_payload(inner, *err),
                _ => false,
            },
            _ => false,
        }
    }

    /// Checks `lhs is <pattern>`.
    pub fn is_pattern_test(&mut self, lhs: Expr, p: &ast::Pattern, negated: bool, span: Span) -> Expr {
        let ty = self.infer.resolve(lhs.ty);
        let mut binds = vec![];
        let pat = self.pattern(p, ty, PatCtx::IsTest, &mut binds);
        let then = Expr { ty: Ty::bool(), kind: tir::ExprKind::Bool(!negated), span };
        let otherwise = Expr { ty: Ty::bool(), kind: tir::ExprKind::Bool(negated), span };
        let wild = Pat { ty, kind: PatKind::Wild, span };
        let arms =
            vec![tir::Arm { pat, guard: None, body: then }, tir::Arm { pat: wild, guard: None, body: otherwise }];
        Expr { ty: Ty::bool(), kind: tir::ExprKind::Match { scrutinee: Box::new(lhs), arms }, span }
    }
}

fn remap_binds(p: &mut Pat, map: &[(LocalId, LocalId)]) {
    match &mut p.kind {
        PatKind::Bind(id) => {
            if let Some((_, to)) = map.iter().find(|(from, _)| from == id) {
                *id = *to;
            }
        }
        PatKind::Some(q) | PatKind::Ok(q) | PatKind::Err(q) => remap_binds(q, map),
        PatKind::Variant { payload: Some(q), .. } => remap_binds(q, map),
        PatKind::Fields(fs) => fs.iter_mut().for_each(|(_, q)| remap_binds(q, map)),
        PatKind::Or(ps) => ps.iter_mut().for_each(|q| remap_binds(q, map)),
        _ => {}
    }
}

/// Converts an expression written on the right of `is` into a pattern, if it has
/// the shape of one (§A.7 rule 9).
fn expr_to_pattern(e: &ast::Expr) -> Option<ast::Pattern> {
    let kind = match &e.kind {
        ExprKind::Underscore => PatternKind::Wildcard,
        ExprKind::Null => PatternKind::Null,
        ExprKind::DotIdent(_) | ExprKind::Field { .. } => PatternKind::Path { path: Box::new(e.clone()), args: None },
        ExprKind::Call { callee, args, .. }
            if matches!(callee.kind, ExprKind::DotIdent(_) | ExprKind::Field { .. }) =>
        {
            let mut pargs = vec![];
            for a in args {
                let (name, value) = match &a.kind {
                    ast::ArgKind::Positional(v) => (None, v),
                    ast::ArgKind::Named(n, v) => (Some(*n), v),
                    ast::ArgKind::Spread(_) => return None,
                };
                pargs.push(ast::PatArg { name, pat: nested_pattern(value)?, span: a.span });
            }
            PatternKind::Path { path: callee.clone(), args: Some(pargs) }
        }
        ExprKind::Unary { op: ast::UnOp::MakeErr, operand } => PatternKind::Err(Box::new(expr_to_pattern(operand)?)),
        ExprKind::Int(_) | ExprKind::Str(_) | ExprKind::Bool(_) => PatternKind::Lit(Box::new(e.clone())),
        ExprKind::Unary { op: ast::UnOp::Neg, operand } if matches!(operand.kind, ExprKind::Int(_)) => {
            PatternKind::Lit(Box::new(e.clone()))
        }
        ExprKind::Tuple(elems) => {
            let mut pargs = vec![];
            for el in elems {
                pargs.push(ast::PatArg { name: el.name, pat: nested_pattern(&el.value)?, span: el.value.span });
            }
            PatternKind::Tuple(pargs)
        }
        ExprKind::Paren(inner) => return expr_to_pattern(inner),
        _ => return None,
    };
    Some(ast::Pattern { id: e.id, kind, span: e.span })
}

/// Like [`expr_to_pattern`], but a bare name inside a pattern is a binding.
fn nested_pattern(e: &ast::Expr) -> Option<ast::Pattern> {
    if let ExprKind::Ident(n) = &e.kind {
        return Some(ast::Pattern { id: e.id, kind: PatternKind::Bind(*n), span: e.span });
    }
    expr_to_pattern(e)
}

fn contains_wildcard(p: &ast::Pattern) -> bool {
    match &p.kind {
        PatternKind::Wildcard => true,
        PatternKind::Err(q) => contains_wildcard(q),
        PatternKind::Path { args: Some(args), .. } | PatternKind::Tuple(args) => {
            args.iter().any(|a| contains_wildcard(&a.pat))
        }
        _ => false,
    }
}
