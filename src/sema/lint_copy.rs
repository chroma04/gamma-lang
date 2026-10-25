//! The `mutated-copy` lint: a local initialized with a copy of a value that keeps its
//! elements in a buffer it can resize, such as a `List` or a `HashMap` (a struct holding a
//! mutable slice and the `Allocator` that resizes it), which the function then changes as
//! if it were the original:
//!
//! ```text
//! tys := cx.s.tys          // a copy of the table
//! t := tys.intern(k)       // adds to the copy; `cx.s.tys` never sees it
//! ```
//!
//! The copy shares the elements with the original but not its length or capacity, so
//! what is added through it is lost, or lands in a buffer the original then overwrites.
//! A pointer to the original (`tys := &cx.s.tys`) is what was meant. A copy that is then
//! read whole (stored back, passed on, returned) is taken to be deliberate, as is one
//! that is assigned again or that a function literal captures, and one whose original is
//! then replaced (`q := m.queue` then `m.queue = list(T)`: the copy takes over the buffer).

use super::lint::Finding;
use super::{DeclState, DeclValue, Sema};
use crate::source::Span;
use crate::tir::{Block, Expr, ExprKind, FnDef, LocalId, Stmt};
use crate::types::{Ty, TyKind};
use std::collections::{HashMap, HashSet};

/// Reports the locals of `def` that are copies of such a value and are changed.
pub(super) fn mutated_copies(sema: &Sema, def: &FnDef, out: &mut Vec<Finding>) {
    let Some(body) = &def.body else { return };
    let Some(allocator) = allocator_ty(sema) else { return };
    let mut scan = Scan {
        sema,
        allocator,
        candidates: HashMap::new(),
        changed: HashMap::new(),
        whole: HashSet::new(),
        assigned: HashSet::new(),
    };
    scan.expr(body);
    let mut found: Vec<(LocalId, Span)> = scan.changed.into_iter().collect();
    found.sort_by_key(|(l, _)| l.0);
    for (l, at) in found {
        let Some(source) = scan.candidates.get(&l) else { continue };
        if scan.whole.contains(&l) || scan.assigned.contains(sema.sess.sources.text(*source)) {
            continue;
        }
        let local = &def.locals[l.index()];
        out.push(Finding::MutatedCopy { span: at, name: local.name, local: local.span, source: *source });
    }
}

struct Scan<'a, 's> {
    sema: &'a Sema<'s>,
    /// The locals initialized with a copy of a place holding a buffer, by where the place
    /// is written.
    candidates: HashMap<LocalId, Span>,
    /// The first change made through each local.
    changed: HashMap<LocalId, Span>,
    /// The locals read whole, assigned whole or captured: their copies are deliberate.
    whole: HashSet<LocalId>,
    /// The text of every place assigned whole.
    assigned: HashSet<&'a str>,
    /// The core library's `Allocator`.
    allocator: Ty,
}

/// The core library's `Allocator` struct.
fn allocator_ty(sema: &Sema) -> Option<Ty> {
    let p = sema.prelude?;
    let d = *sema.modules[p.0 as usize].scope.get(&crate::symbol::Symbol::intern("Allocator"))?;
    match &sema.decls[d.0 as usize].state {
        DeclState::Done(DeclValue::Type(t)) => Some(*t),
        _ => None,
    }
}

impl<'a> Scan<'a, '_> {
    fn block(&mut self, b: &Block) {
        for s in &b.stmts {
            match s {
                Stmt::Let { local, init: Some(init) } => {
                    if is_place(init) && holds_buffer(self.sema, self.allocator, init.ty, &mut HashSet::new()) {
                        self.candidates.insert(*local, init.span);
                    }
                    self.expr(init);
                }
                Stmt::Assign { place, value } => {
                    self.assigned.insert(self.sema.sess.sources.text(place.span));
                    match rooted_local(place) {
                        // The whole local: a new value replaces the copy.
                        Some((l, true)) => {
                            self.whole.insert(l);
                        }
                        // A field of the copy: a change the original does not see.
                        Some((l, false)) => {
                            self.changed.entry(l).or_insert(place.span);
                        }
                        None => self.expr(place),
                    }
                    self.expr(value);
                }
                Stmt::AssignTuple { places, value } => {
                    for p in places.iter().flatten() {
                        match rooted_local(p) {
                            Some((l, true)) => {
                                self.whole.insert(l);
                            }
                            Some((l, false)) => {
                                self.changed.entry(l).or_insert(p.span);
                            }
                            None => self.expr(p),
                        }
                    }
                    self.expr(value);
                }
                Stmt::Let { init: None, .. } => {}
                Stmt::LetTuple { init, .. } | Stmt::Expr(init) | Stmt::Defer { body: init, .. } => self.expr(init),
            }
        }
        if let Some(t) = &b.tail {
            self.expr(t);
        }
    }

    fn expr(&mut self, e: &Expr) {
        match &e.kind {
            ExprKind::Block(b) => return self.block(b),
            ExprKind::Local(l) => {
                self.whole.insert(*l);
                return;
            }
            // `&x` made a `^const` pointer: read only.
            ExprKind::Coerce { expr: inner, .. }
                if matches!(e.ty.kind(), TyKind::Ptr { is_const: true, .. })
                    && matches!(&inner.kind, ExprKind::AddrOf(p) if rooted_local(p).is_some()) =>
            {
                if let ExprKind::AddrOf(p) = &inner.kind {
                    self.place_reads(p);
                }
                return;
            }
            // `&x` or `&x.f` as a pointer that may change it: a method of the copy that
            // takes `^T`, such as `push`.
            ExprKind::AddrOf(p) => {
                if let Some((l, _)) = rooted_local(p) {
                    if matches!(e.ty.kind(), TyKind::Ptr { is_const: false, .. }) {
                        self.changed.entry(l).or_insert(e.span);
                    }
                    return self.place_reads(p);
                }
            }
            // Reading a field of the copy (`x.items`) is fine.
            ExprKind::Field { .. } => {
                if rooted_local(e).is_some() {
                    return self.place_reads(e);
                }
            }
            // Function literals capture the locals they use: leave those alone.
            ExprKind::Closure(f) | ExprKind::FnRef(f) => {
                if let Some(def) = self.sema.prog.fns.get(f.index())
                    && let Some(b) = &def.body
                {
                    captured(b, &mut self.whole);
                }
            }
            _ => {}
        }
        e.for_each_child(&mut |c| self.expr(c));
    }

    /// The index expressions inside a place rooted at a local.
    fn place_reads(&mut self, p: &Expr) {
        match &p.kind {
            ExprKind::Field { base, .. } => self.place_reads(base),
            ExprKind::Index { base, index, .. } => {
                self.place_reads(base);
                self.expr(index);
            }
            _ => {}
        }
    }
}

/// The locals of the enclosing function that the body of a function literal uses.
fn captured(e: &Expr, out: &mut HashSet<LocalId>) {
    if let ExprKind::Upvar { levels: 1, local } = &e.kind {
        out.insert(*local);
    }
    e.for_each_child(&mut |c| captured(c, out));
}

/// Whether evaluating `e` reads a place (rather than computing a new value).
fn is_place(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Deref(_) | ExprKind::Global(_) => true,
        ExprKind::Field { base, .. } => is_place(base) || matches!(base.kind, ExprKind::Call { .. }),
        ExprKind::Index { .. } => true,
        _ => false,
    }
}

/// The local a place is part of, through fields and elements of arrays (not through a
/// pointer or a slice, whose target is elsewhere), and whether it is the whole local.
fn rooted_local(p: &Expr) -> Option<(LocalId, bool)> {
    match &p.kind {
        ExprKind::Local(l) => Some((*l, true)),
        ExprKind::Field { base, .. } => rooted_local(base).map(|(l, _)| (l, false)),
        ExprKind::Index { base, .. } if matches!(base.ty.kind(), TyKind::Array { .. }) => {
            rooted_local(base).map(|(l, _)| (l, false))
        }
        _ => None,
    }
}

/// Whether a value of type `t` holds, by value, a struct with a mutable slice and the
/// `Allocator` that resizes it: the elements of a buffer whose length and capacity live in
/// the value itself.
fn holds_buffer(sema: &Sema, allocator: Ty, t: Ty, seen: &mut HashSet<Ty>) -> bool {
    if !seen.insert(t) {
        return false;
    }
    match t.kind() {
        TyKind::Adt(id, _) => match sema.prog.adts.get(id).map(|d| &d.kind) {
            Some(crate::tir::AdtKind::Struct { fields, bits: None, .. }) => {
                let owns = fields.iter().any(|f| matches!(f.ty.kind(), TyKind::Slice { is_const: false, .. }))
                    && fields.iter().any(|f| f.ty == allocator);
                owns || fields.iter().any(|f| holds_buffer(sema, allocator, f.ty, seen))
            }
            _ => false,
        },
        TyKind::Tuple { elems, .. } => elems.iter().any(|e| holds_buffer(sema, allocator, *e, seen)),
        TyKind::Array { elem, .. } => holds_buffer(sema, allocator, *elem, seen),
        _ => false,
    }
}
