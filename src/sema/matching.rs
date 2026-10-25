//! `match` (§8.6), binding conditions (§8.2) and pattern tests in `is` (§7.6).

use super::exhaust::Checker;
use super::func::{FnCx, error_expr};
use super::pattern::PatCtx;
use crate::diag::Diagnostic;
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind, PatternKind};
use crate::tir::{self, Expr, LocalId, Pat, PatKind};
use crate::types::Ty;

impl<'a, 's> FnCx<'a, 's> {
    /// True if `t` is a `#c_union` type.
    pub fn is_c_union(&self, t: Ty) -> bool {
        match t.kind() {
            crate::types::TyKind::Adt(id, _) => {
                matches!(self.sema.prog.adts.get(id).map(|d| &d.kind), Some(tir::AdtKind::CUnion { .. }))
            }
            _ => false,
        }
    }

    pub fn checker(&self) -> Checker<'_> {
        Checker { adts: &self.sema.prog.adts, target: &self.sema.target }
    }

    /// Checks a `match`. With `value` false, arms are statements (§7.12).
    pub fn match_expr(&mut self, m: &ast::MatchExpr, value: bool, expect: Option<Ty>) -> Expr {
        let scrutinee = self.expr(&m.scrutinee, None);
        // A scrutinee whose type still contains literal types (`a := 1; match a …`) keeps
        // them open: a later statement may still fix them (§6.1). Integer patterns restrict
        // them to integers, and exhaustiveness is checked once they are final.
        let mut sty = self.infer.resolve(scrutinee.ty);
        let deferred = sty.has_infer();
        if self.is_c_union(sty) {
            self.error(m.scrutinee.span, c_union_match(sty));
            sty = Ty::error();
        }
        let mut arms = vec![];
        // A pattern that was reported stands for nothing; checking coverage with it would
        // only add follow-on errors.
        let mut bad_pattern = false;
        for arm in &m.arms {
            self.push_scope();
            let before = self.sema.sess.diags.error_count();
            let pat = self.arm_pattern(&arm.pats, sty);
            bad_pattern |= self.sema.sess.diags.error_count() > before;
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
        if deferred {
            self.deferred_matches.push((m.span, m.arms.iter().map(|a| a.span).collect()));
        } else if !sty.is_error() && !bad_pattern {
            let report = {
                let checker = self.checker();
                let list: Vec<(&Pat, bool)> = arms.iter().map(|a| (&a.pat, a.guard.is_some())).collect();
                checker.check_match(sty, &list)
            };
            let arm_spans: Vec<Span> = m.arms.iter().map(|a| a.span).collect();
            self.report_match(report, m.span, &arm_spans);
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

    pub fn report_match(&mut self, report: super::exhaust::MatchReport, span: Span, arms: &[Span]) {
        if let Some(missing) = report.missing {
            let mut d =
                Diagnostic::error(span, format!("this `match` is not exhaustive: `{}` is not covered", missing));
            if let Some(at) = report.present_only {
                d = d.with_label(at, "this name matches only a present value").with_note(
                    "a name in the position of an optional matches only a present value (§9.3); to bind the \
                     optional as it is, absent value included, write `?` before the name (`.V(?x)`)",
                );
            }
            self.sema.emit(d);
        }
        for i in report.unreachable {
            self.error(arms[i], "this arm can never be chosen: earlier arms cover it");
        }
    }

    /// Checks a `match` whose exhaustiveness was deferred until its scrutinee's literal
    /// types were final.
    pub fn check_deferred_match(&mut self, e: &Expr) {
        let tir::ExprKind::Match { scrutinee, arms } = &e.kind else { return };
        let Some(i) = self.deferred_matches.iter().position(|d| d.0 == e.span) else { return };
        let (span, arm_spans) = self.deferred_matches.swap_remove(i);
        if scrutinee.ty.is_error() {
            return;
        }
        let report = {
            let checker = self.checker();
            let list: Vec<(&Pat, bool)> = arms.iter().map(|a| (&a.pat, a.guard.is_some())).collect();
            checker.check_match(scrutinee.ty, &list)
        };
        self.report_match(report, span, &arm_spans);
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
            let optional =
                matches!(vty.kind(), crate::types::TyKind::Optional(_) | crate::types::TyKind::Result { .. });
            if optional && matches!(pat.kind, ast::PatternKind::Wildcard) {
                // `_` matches the whole optional or result, absent or not (§9.3).
                self.error(
                    pat.span,
                    "this condition always succeeds: `_` matches a missing value too; bind a name (`v := …`) or test `… is not null`",
                );
            } else {
                self.error(pat.span, "this condition always succeeds; use a plain binding instead");
            }
        }
        self.declare_binds(binds);
        tir::CondPart::Match { pat: p, value: v }
    }

    /// If the right operand of `is` is a pattern form that is not a value (§7.6),
    /// returns it as a pattern.
    pub fn is_pattern_operand(&mut self, rhs: &ast::Expr, lhs_ty: Ty) -> Option<ast::Pattern> {
        let pat = expr_to_pattern(rhs)?;
        // `T.V(…)` is a variant pattern only if `T` names a type; `x.method(…)` is a value.
        if let PatternKind::Path { path, .. } = &pat.kind
            && let ExprKind::Field { base, .. } = &path.kind
            && self.type_of_name(base).is_none()
        {
            return None;
        }
        // `x is null` tests for absence, which needs no equality on the payload (§7.6).
        let optional = matches!(self.infer.resolve(lhs_ty).kind(), crate::types::TyKind::Optional(_));
        if optional && matches!(pat.kind, PatternKind::Null) {
            return Some(pat);
        }
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
        let ty = self.infer.resolve(ty);
        if let PatternKind::Err(inner) = &p.kind {
            let mut t = ty;
            while let crate::types::TyKind::Optional(i) = t.kind() {
                t = *i;
            }
            return match t.kind() {
                crate::types::TyKind::Result { err, .. } => self.omits_payload(inner, *err),
                _ => false,
            };
        }
        let ty = super::call::strip_wrappers(ty);
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
            _ => false,
        }
    }

    /// Checks `lhs in (p1, p2, …)` where a member is a pattern form (§7.7): `lhs` matches
    /// one of the members, each written as on the right of `is`.
    pub fn in_patterns(&mut self, lhs: Expr, members: &[ast::TupleElem], negated: bool, span: Span) -> Expr {
        let mut ty = self.infer.resolve(lhs.ty);
        if self.is_c_union(ty) {
            self.error(lhs.span, c_union_match(ty));
            ty = Ty::error();
        }
        let mut arms = vec![];
        for m in members {
            let Some(p) = expr_to_pattern(&m.value).filter(|_| m.name.is_none()) else {
                self.error(
                    m.value.span,
                    "when a member of an inline tuple of `in` is a pattern, every member must be a pattern or a literal (§7.7)",
                );
                return super::func::error_expr(span);
            };
            let mut binds = vec![];
            let pat = self.pattern(&p, ty, PatCtx::IsTest, &mut binds);
            arms.push(tir::Arm {
                pat,
                guard: None,
                body: Expr { ty: Ty::bool(), kind: tir::ExprKind::Bool(!negated), span },
            });
        }
        let otherwise = Expr { ty: Ty::bool(), kind: tir::ExprKind::Bool(negated), span };
        arms.push(tir::Arm { pat: Pat { ty, kind: PatKind::Wild, span }, guard: None, body: otherwise });
        Expr { ty: Ty::bool(), kind: tir::ExprKind::Match { scrutinee: Box::new(lhs), arms }, span }
    }

    /// Checks `lhs is <pattern>`.
    pub fn is_pattern_test(&mut self, lhs: Expr, p: &ast::Pattern, negated: bool, span: Span) -> Expr {
        let mut ty = self.infer.resolve(lhs.ty);
        if self.is_c_union(ty) {
            self.error(lhs.span, c_union_match(ty));
            ty = Ty::error();
        }
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
pub(super) fn expr_to_pattern(e: &ast::Expr) -> Option<ast::Pattern> {
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

/// The error for matching a value of `#c_union` type `t` (§5.11.1).
pub(super) fn c_union_match(t: Ty) -> String {
    format!("`{}` is a `#c_union`: it has no tag, so it cannot be matched; read one of its fields instead (§5.11.1)", t)
}
