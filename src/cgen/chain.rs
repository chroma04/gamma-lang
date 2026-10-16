//! Long chains of operations, such as `a + b + c + …` or `x.abs().abs()…`.
//!
//! Each operation of a chain takes the value of the one before it as its first operand.
//! Emitted naively, every temporary of every link is declared in the same C block, and
//! the C compilers' cost for such a function grows much faster than its length (Clang
//! tracks the lifetime of each variable of a scope at every branch out of it, so tens
//! of thousands of links take minutes). A chain of more than [`MIN_LINKS`] links is
//! therefore emitted as an accumulator variable that each link updates inside a block of
//! its own, which bounds the temporaries live in any scope by the size of one link.
//! Evaluation order is unchanged: the first operand of a link is still evaluated before
//! its other operands, and traps, early exits and defers behave as before because the
//! links are ordinary nested blocks.

use super::func::FnGen;
use super::{CResult, unsupported};
use crate::tir::{Builtin, Callee, Expr, ExprKind};
use crate::types::Ty;

/// The number of links above which a chain is emitted through an accumulator. Ordinary
/// code stays below it and is emitted unchanged.
const MIN_LINKS: usize = 256;

/// The link being emitted: its first operand (`child`) is read from the accumulator.
pub struct Spine {
    child: *const Expr,
    acc: String,
    used: bool,
}

impl<'g, 'a> FnGen<'g, 'a> {
    /// The operand of `e` that is evaluated first and whose value is the previous link of
    /// a chain, if `e` is a link that can be emitted through an accumulator. Both values
    /// must be plain C values (not large aggregates passed by address).
    fn chain_child(&self, e: &'a Expr) -> Option<&'a Expr> {
        let child = match &e.kind {
            ExprKind::Unary { operand, .. } => &**operand,
            ExprKind::Binary { lhs, .. } | ExprKind::Logic { lhs, .. } => &**lhs,
            ExprKind::Builtin { op, args } => {
                // These evaluate their operands specially, or not first.
                if matches!(
                    op,
                    Builtin::Assert
                        | Builtin::Fiber
                        | Builtin::Asm { .. }
                        | Builtin::Sys(_)
                        | Builtin::Unreachable
                        | Builtin::Get
                        | Builtin::GetPtr
                        | Builtin::First
                        | Builtin::Last
                        | Builtin::HeapAlloc
                        | Builtin::AllocTag
                        | Builtin::AllocCheck
                        | Builtin::HeapFree
                ) {
                    return None;
                }
                args.first()?
            }
            // The callee of a call through a value is evaluated before its arguments.
            ExprKind::Call { callee: Callee::Fn(_), args } => args.first()?,
            _ => return None,
        };
        let plain = |t: Ty| !t.is_unit() && !t.is_never() && !self.g.ty.by_ref(t);
        (plain(e.ty) && plain(child.ty)).then_some(child)
    }

    /// Emits `e` if it is a chain of more than [`MIN_LINKS`] links, returning the
    /// accumulator that holds its value.
    pub(super) fn long_chain(&mut self, e: &'a Expr) -> Option<CResult<String>> {
        let mut n = 0;
        let mut cur = e;
        while let Some(c) = self.chain_child(cur) {
            n += 1;
            if n > MIN_LINKS {
                return Some(self.emit_chain(e));
            }
            cur = c;
        }
        None
    }

    fn emit_chain(&mut self, top: &'a Expr) -> CResult<String> {
        let mut links = vec![];
        let mut bottom = top;
        while let Some(c) = self.chain_child(bottom) {
            links.push(bottom);
            bottom = c;
        }
        let first = self.expr(bottom)?;
        let mut acc = format!("c{}", self.fresh());
        let mut acc_ty = bottom.ty;
        let cty = self.g.ty.cty(acc_ty);
        self.line(&format!("{cty} {acc} = {first};"));
        let mut child = bottom;
        for link in links.into_iter().rev() {
            // A link of another type starts a new accumulator.
            let dest = if link.ty == acc_ty {
                acc.clone()
            } else {
                let n = format!("c{}", self.fresh());
                let cty = self.g.ty.cty(link.ty);
                self.line(&format!("{cty} {n};"));
                n
            };
            self.line("{");
            self.indent += 1;
            let saved = self.spine.replace(Spine { child, acc: acc.clone(), used: false });
            let v = self.expr_node(link);
            let used = std::mem::replace(&mut self.spine, saved).is_some_and(|s| s.used);
            let v = v?;
            if !used {
                return unsupported(link.span, "a long chain of operations");
            }
            self.line(&format!("{dest} = {v};"));
            self.indent -= 1;
            self.line("}");
            acc = dest;
            acc_ty = link.ty;
            child = link;
        }
        Ok(acc)
    }

    /// The accumulator standing for `e`, if `e` is the operand the current link reads from it.
    pub(super) fn chain_operand(&mut self, e: &Expr) -> Option<String> {
        match &mut self.spine {
            Some(s) if std::ptr::eq(s.child, e) => {
                s.used = true;
                Some(s.acc.clone())
            }
            _ => None,
        }
    }
}
