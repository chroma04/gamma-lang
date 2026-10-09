//! The dangling-pointer cases rejected at compile time (§15.1): returning the
//! address of, or a slice of, a local or parameter of the returning function.

use super::Sema;
use crate::tir::{Expr, ExprKind, FnDef};
use crate::types::TyKind;
use std::collections::HashSet;

impl Sema<'_> {
    pub(super) fn check_escapes(&mut self) {
        let mut found = vec![];
        for def in &self.prog.fns {
            let Some(body) = &def.body else { continue };
            returned(body, &mut |e| {
                if let Some(msg) = escaping(def, e) {
                    found.push((e.span, msg));
                }
            });
            returns(body, &mut |e| {
                returned(e, &mut |e| {
                    if let Some(msg) = escaping(def, e) {
                        found.push((e.span, msg));
                    }
                })
            });
        }
        // Instances of generic functions repeat their template's spans.
        let mut seen = HashSet::new();
        for (span, msg) in found {
            if seen.insert(span) {
                self.error(span, msg);
            }
        }
    }
}

/// Calls `f` on the value of each `return` of the function itself in `e`.
fn returns<'a>(e: &'a Expr, f: &mut dyn FnMut(&'a Expr)) {
    if let ExprKind::Return { levels: 0, value } = &e.kind {
        f(value);
    }
    e.for_each_child(&mut |c| returns(c, f));
}

/// Calls `f` on each expression whose value is directly the value of `e`.
fn returned<'a>(e: &'a Expr, f: &mut dyn FnMut(&'a Expr)) {
    match &e.kind {
        ExprKind::Coerce { expr, .. } => returned(expr, f),
        ExprKind::Block(b) => {
            if let Some(t) = &b.tail {
                returned(t, f)
            }
        }
        ExprKind::If { then, else_, .. } => {
            returned(then, f);
            if let Some(e) = else_ {
                returned(e, f);
            }
        }
        ExprKind::Match { arms, .. } => {
            for a in arms {
                returned(&a.body, f);
            }
        }
        _ => f(e),
    }
}

/// The error for returning `e` from `def`, if it points into one of `def`'s locals.
fn escaping(def: &FnDef, e: &Expr) -> Option<String> {
    let (what, place) = match &e.kind {
        ExprKind::AddrOf(p) => ("the address of", p),
        ExprKind::Slice { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) => ("a slice of", base),
        _ => return None,
    };
    let local = root_local(place)?;
    let name = def.locals[local.index()].name;
    let kind = if def.params.contains(&local) { "parameter" } else { "local" };
    Some(format!("this returns {what} the {kind} `{name}`, which dies when the function returns (§15.1)"))
}

/// The local whose storage the place `e` is part of, if it is one of the function's own.
fn root_local(e: &Expr) -> Option<crate::tir::LocalId> {
    match &e.kind {
        ExprKind::Local(l) => Some(*l),
        ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => root_local(base),
        ExprKind::Index { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) => root_local(base),
        _ => None,
    }
}
