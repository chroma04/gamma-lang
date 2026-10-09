//! Pointer checks over the whole program: the dangling-pointer cases rejected at
//! compile time (§15.1), returning the address of, or a slice of, a local or
//! parameter of the returning function; and pointers into `#packed` fields that
//! would be misaligned (§5.10.4).

use super::Sema;
use crate::tir::{AdtKind, Expr, ExprKind, FnDef, Program};
use crate::types::TyKind;
use std::collections::HashSet;

impl Sema<'_> {
    pub(super) fn check_escapes(&mut self) {
        let mut found = vec![];
        let mut found_packed = vec![];
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
        // Generic templates are checked through their instances, whose types are known.
        for def in &self.prog.fns {
            let Some(body) = &def.body else { continue };
            unaligned(&self.prog, body, &mut |e, t| {
                if !super::generics::has_param(t) {
                    found_packed.push((e.span, t));
                }
            });
        }
        for (span, t) in found_packed {
            if self.with_layouts(|l| l.align_of(t)) > 1 {
                found.push((
                    span,
                    format!(
                        "this points into a field of a `#packed` struct, where a `{}` may be misaligned; copy the field to a local first (§5.10.4)",
                        t
                    ),
                ));
            }
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

/// Calls `f` with each expression in `e` that takes an address inside a field of a
/// `#packed` struct, and the type of what that address points to.
fn unaligned<'a>(prog: &Program, e: &'a Expr, f: &mut dyn FnMut(&'a Expr, crate::types::Ty)) {
    match &e.kind {
        ExprKind::AddrOf(p) if in_packed(prog, p) => f(e, p.ty),
        ExprKind::Slice { base, .. } if in_packed(prog, base) => {
            if let TyKind::Array { elem, .. } = base.ty.kind() {
                f(e, *elem)
            }
        }
        _ => {}
    }
    e.for_each_child(&mut |c| unaligned(prog, c, f));
}

/// True if the place `e` lies inside a field of a `#packed` struct value.
fn in_packed(prog: &Program, e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Field { base, .. } => {
            let packed = match base.ty.kind() {
                TyKind::Adt(id, _) => {
                    matches!(prog.adts.get(id).map(|d| &d.kind), Some(AdtKind::Struct { packed: true, .. }))
                }
                _ => false,
            };
            packed || in_packed(prog, base)
        }
        ExprKind::VariantPayload { base, .. } => in_packed(prog, base),
        ExprKind::Index { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) => in_packed(prog, base),
        _ => false,
    }
}
