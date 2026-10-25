//! The `present-only-binding` lint: a `match` arm with a bare name in the position of an
//! optional (or a result) below the top of its pattern, which matches only a present
//! value (§9.3), where no arm names the absent case at that position and a catch-all
//! arm takes it instead. `.Return(v) => …` followed by `_ => …` sends `.Return(null)` to
//! the catch-all, which is rarely what was meant. A catch-all arm with an empty body
//! (`_ => {}`) ignores the rest on purpose, and is not reported.

use super::lint::Finding;
use crate::source::Span;
use crate::tir::{Arm, Expr, ExprKind, FnDef, Pat, PatKind};

/// One step from a pattern's top to a position below it.
#[derive(Clone, Copy, PartialEq)]
enum Step {
    Variant(u32),
    Field(u32),
    /// Into the present value of an optional, or the success value of a result.
    Present,
    Error,
}

/// Reports the arms of a `match` (with `arms`, in function `def`) whose present-only
/// bindings leave the absent case to a catch-all arm.
pub(super) fn present_only_bindings(def: &FnDef, arms: &[Arm], out: &mut Vec<Finding>) {
    let Some(catch_all) = arms.iter().find(|a| a.guard.is_none() && is_catch_all(&a.pat)) else { return };
    // `_ => {}` ignores whatever the other arms do not take, the absent case included.
    if is_empty(&catch_all.body) {
        return;
    }
    for (i, a) in arms.iter().enumerate() {
        let mut found = vec![];
        present_bindings(&a.pat, true, &mut vec![], &mut found);
        for (path, span, l, is_result) in found {
            let handled = arms
                .iter()
                .enumerate()
                .any(|(j, b)| j != i && b.guard.is_none() && !is_catch_all(&b.pat) && names_absent(&b.pat, &path));
            if !handled {
                let name = def.locals[l.index()].name;
                out.push(Finding::PresentOnlyBinding { span, name, catch_all: catch_all.pat.span, is_result });
            }
        }
    }
}

fn is_empty(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Block(b) => b.stmts.is_empty() && b.tail.as_ref().is_none_or(is_empty),
        ExprKind::Aggregate(xs) => xs.is_empty(),
        _ => false,
    }
}

fn is_catch_all(p: &Pat) -> bool {
    match &p.kind {
        PatKind::Wild | PatKind::Bind(_) => true,
        PatKind::Or(alts) => alts.iter().any(is_catch_all),
        _ => false,
    }
}

/// The bare bindings below the top of `p` that match only a present value (or a success
/// value): their paths, spans, locals, and whether the value is a result's.
fn present_bindings(
    p: &Pat,
    top: bool,
    path: &mut Vec<Step>,
    out: &mut Vec<(Vec<Step>, Span, crate::tir::LocalId, bool)>,
) {
    match &p.kind {
        PatKind::Some(q) | PatKind::Ok(q) => {
            if let PatKind::Bind(l) = q.kind
                && !top
            {
                out.push((path.clone(), q.span, l, matches!(p.kind, PatKind::Ok(_))));
                return;
            }
            path.push(Step::Present);
            present_bindings(q, top, path, out);
            path.pop();
        }
        PatKind::Err(q) => {
            path.push(Step::Error);
            present_bindings(q, top, path, out);
            path.pop();
        }
        PatKind::Variant { variant, payload: Some(q) } => {
            path.push(Step::Variant(*variant));
            present_bindings(q, false, path, out);
            path.pop();
        }
        PatKind::Fields(fs) => {
            for (k, q) in fs {
                path.push(Step::Field(*k));
                present_bindings(q, false, path, out);
                path.pop();
            }
        }
        PatKind::Or(alts) => alts.iter().for_each(|q| present_bindings(q, top, path, out)),
        _ => {}
    }
}

/// Whether `p` matches the absent value (`null`, or an error) at `path` by naming it, or
/// a pattern around it that takes any value there, below the top.
fn names_absent(p: &Pat, path: &[Step]) -> bool {
    let Some((step, rest)) = path.split_first() else {
        return matches!(p.kind, PatKind::Null | PatKind::Err(_) | PatKind::Wild | PatKind::Bind(_));
    };
    match (&p.kind, step) {
        (PatKind::Or(alts), _) => alts.iter().any(|q| names_absent(q, path)),
        (PatKind::Wild | PatKind::Bind(_), _) => true,
        (PatKind::Variant { variant, payload: Some(q) }, Step::Variant(v)) if variant == v => {
            names_absent_below(q, rest)
        }
        (PatKind::Fields(fs), Step::Field(k)) => match fs.iter().find(|(i, _)| i == k) {
            Some((_, q)) => names_absent_below(q, rest),
            None => true,
        },
        (PatKind::Some(q) | PatKind::Ok(q), Step::Present) | (PatKind::Err(q), Step::Error) => {
            names_absent_below(q, rest)
        }
        _ => false,
    }
}

/// `names_absent` below the top of a pattern, where a binding or `_` takes any value.
fn names_absent_below(p: &Pat, path: &[Step]) -> bool {
    match &p.kind {
        PatKind::Wild | PatKind::Bind(_) => true,
        _ => names_absent(p, path),
    }
}
