//! Facts read off the typed IR after checking: the type of every expression, the
//! function each call resolves to, and what each local is.

use crate::ide::Ref;
use crate::sema::Sema;
use crate::source::Span;
use crate::symbol::Symbol;
use crate::tir::{Callee, Expr, ExprKind, FnId};
use crate::types::Ty;
use std::collections::HashMap;

pub(super) struct TypeFact {
    pub ty: Ty,
    /// Worth showing on hover (not a block or a control-flow construct).
    pub hover: bool,
}

#[derive(Default)]
pub(super) struct Facts {
    pub types: HashMap<Span, TypeFact>,
    /// The types of all expressions at a span, innermost first: a variant payload built
    /// from a call's arguments has the span of the call that builds the variant.
    pub all_types: HashMap<Span, Vec<Ty>>,
    /// The function a call at a span resolves to (a generic's definition, not an instance).
    pub calls: HashMap<Span, FnId>,
    /// Declared locals by the span of their name: name, type, mutable.
    pub locals: HashMap<Span, (Symbol, Ty, bool)>,
    /// References from uses of locals to their declarations.
    pub refs: Vec<Ref>,
}

impl<'s> Sema<'s> {
    /// The generic function that `f` instantiates, or `f`.
    pub(crate) fn generic_of(&self, f: FnId) -> FnId {
        self.instance_of.get(&f).map_or(f, |(g, _)| *g)
    }

    /// The facts of the program's functions, except those of the embedded core library,
    /// which no editor can open.
    pub(super) fn collect_facts(&self) -> Facts {
        let mut facts = Facts::default();
        let core: std::collections::HashSet<_> = self
            .sess
            .sources
            .files()
            .filter(|(_, f)| f.path.to_string_lossy().starts_with('<'))
            .map(|(id, _)| id)
            .collect();
        for (i, def) in self.prog.fns.iter().enumerate() {
            let f = FnId(i as u32);
            // Instances repeat the generic definition's spans with concrete types.
            if self.instance_of.contains_key(&f) || core.contains(&def.span.file) {
                continue;
            }
            for l in &def.locals {
                if l.span != Span::DUMMY && !l.name.as_str().starts_with('<') {
                    facts.locals.entry(l.span).or_insert((l.name, l.ty, l.mutable));
                }
            }
            if let Some(body) = &def.body {
                self.visit_fact(f, body, &mut facts);
            }
        }
        for c in &self.prog.consts {
            self.visit_fact(FnId(u32::MAX), &c.init, &mut facts);
        }
        facts
    }

    /// The function whose frame holds the local that an upvar `levels` frames up names.
    fn frame_up(&self, mut f: FnId, levels: u32) -> Option<FnId> {
        for _ in 0..levels {
            f = self.prog.fns.get(f.index())?.parent?;
        }
        Some(f)
    }

    fn visit_fact(&self, f: FnId, e: &Expr, facts: &mut Facts) {
        // Children are visited first, so the innermost expression of a span wins.
        e.for_each_child(&mut |c| self.visit_fact(f, c, facts));
        if e.span == Span::DUMMY {
            return;
        }
        let hover = !matches!(
            e.kind,
            ExprKind::Block(_)
                | ExprKind::If { .. }
                | ExprKind::Match { .. }
                | ExprKind::Loop { .. }
                | ExprKind::With { .. }
                | ExprKind::Coerce { .. }
                | ExprKind::Return { .. }
                | ExprKind::Break { .. }
                | ExprKind::Continue { .. }
        );
        facts.types.entry(e.span).or_insert(TypeFact { ty: e.ty, hover });
        let all = facts.all_types.entry(e.span).or_default();
        if !all.contains(&e.ty) {
            all.push(e.ty);
        }
        match &e.kind {
            ExprKind::Local(id) => {
                if let Some(l) = self.prog.fns.get(f.index()).and_then(|d| d.locals.get(id.index())) {
                    local_ref(e.span, l, facts);
                }
            }
            ExprKind::Upvar { levels, local } => {
                if let Some(owner) = self.frame_up(f, *levels)
                    && let Some(l) = self.prog.fns[owner.index()].locals.get(local.index())
                {
                    local_ref(e.span, l, facts);
                }
            }
            ExprKind::Call { callee: Callee::Fn(c), .. } => {
                facts.calls.entry(e.span).or_insert(self.generic_of(*c));
            }
            _ => {}
        }
    }
}

fn local_ref(from: Span, l: &crate::tir::LocalDecl, facts: &mut Facts) {
    if l.span != Span::DUMMY && !l.name.as_str().starts_with('<') {
        facts.refs.push(Ref { from, to: l.span });
    }
}
