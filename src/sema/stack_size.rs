//! `@stack_size(f)` (§14.8): a compile-time bound on the stack a call of `f` needs.
//!
//! The bound is the costliest chain of frames from `f` down, each frame costed by
//! [`tir::frame_cost`], plus what the run time needs on a fiber stack. The analysis
//! follows the calls of the program with what is statically known at each point:
//!
//! - the handlers installed by enclosing `with` statements, so that performing one of
//!   their operations is a call to a known clause (run in the handler context of its
//!   `with`, §12.5);
//! - the functions passed for parameters and bound to immutable locals, so that a call
//!   `less(a, b)` inside a sort is a call to the literal the sort's caller passed.
//!
//! A `with … on` body runs on a fiber of its own, so only its `ctl` clauses, which run
//! on the driver's stack, count for the function containing it. A `ctl` perform
//! suspends the running fiber; its clause runs on another stack. Every function appears
//! at most once on a call chain (recursion is an error), which makes "the activation of
//! the function owning a captured local" unambiguous.

use super::Sema;
use crate::diag::Diagnostic;
use crate::source::Span;
use crate::tir::{self, Callee, Expr, ExprKind, FnId, HandlerValue, LocalId, Stmt, WriteSink};
use crate::types::{EffectRef, TyKind};
use std::collections::HashMap;

/// A handler installed by an enclosing `with`.
#[derive(Clone, PartialEq, Eq, Hash)]
struct Installed {
    effect: EffectRef,
    /// The clauses, by operation, when the handler is statically known.
    clauses: Option<Vec<FnId>>,
}

/// Why no bound exists.
enum Fail {
    /// The program has errors that were already reported.
    Quiet,
    /// The analysis met something it cannot bound, at `span`.
    At(Span, String),
}

type R<T> = Result<T, Fail>;

/// An activation on the current call chain.
struct Frame {
    f: FnId,
    /// Functions known to be the values of parameters and immutable locals.
    bound: HashMap<LocalId, FnId>,
}

type MemoKey = (FnId, Vec<Installed>, Vec<(LocalId, FnId)>);

struct Analysis<'x, 's> {
    sema: &'x mut Sema<'s>,
    path: Vec<Frame>,
    /// The handlers installed around the code being analyzed, innermost last.
    env: Vec<Installed>,
    memo: HashMap<MemoKey, u64>,
    costs: HashMap<FnId, u64>,
}

impl<'s> Sema<'s> {
    /// The value of `@stack_size(f)`, or `None` after reporting why there is none.
    pub fn stack_size(&mut self, f: FnId, span: Span) -> Option<u64> {
        let mut a = Analysis { sema: self, path: vec![], env: vec![], memo: HashMap::new(), costs: HashMap::new() };
        match a.call(f, &[], vec![], span) {
            Ok(frames) => Some((tir::STACK_RESERVE + tir::STACK_BASE).saturating_add(frames).max(tir::MIN_STACK)),
            Err(Fail::Quiet) => None,
            Err(Fail::At(at, why)) => {
                let chain: Vec<String> =
                    a.path.iter().map(|fr| format!("`{}`", a.sema.prog.fns[fr.f.index()].name)).collect();
                let name = a.sema.prog.fns[f.index()].name;
                let mut d = Diagnostic::error(at, format!("`@stack_size({})` has no bound: {}", name, why))
                    .with_label(span, "stack size needed here");
                if chain.len() > 1 {
                    d = d.with_note(format!("reached through {}", chain.join(" → ")));
                }
                self.emit(d);
                None
            }
        }
    }
}

impl Analysis<'_, '_> {
    fn name(&self, f: FnId) -> String {
        self.sema.prog.fns[f.index()].name.to_string()
    }

    /// The stack used by a call of `g` (its frame and everything below it) with `args`
    /// (when they are known), in the handler context `env`.
    fn call(&mut self, g: FnId, args: &[Expr], env: Vec<Installed>, span: Span) -> R<u64> {
        if self.path.iter().any(|fr| fr.f == g) {
            return Err(Fail::At(span, format!("`{}` is recursive", self.name(g))));
        }
        if !self.sema.ensure_body(g, span) {
            return Err(Fail::Quiet);
        }
        let bound = self.bind(g, args);
        let def = &self.sema.prog.fns[g.index()];
        let Some(body) = def.body.clone() else {
            return Err(Fail::At(span, format!("`{}` is a foreign function", self.name(g))));
        };
        // A function that captures nothing depends only on its arguments and handlers.
        let key = (def.parent.is_none()
            && env
                .iter()
                .flat_map(|h| h.clauses.iter().flatten())
                .all(|c| self.sema.prog.fns[c.index()].parent.is_none()))
        .then(|| {
            let mut b: Vec<(LocalId, FnId)> = bound.iter().map(|(l, f)| (*l, *f)).collect();
            b.sort_by_key(|(l, f)| (l.index(), f.index()));
            (g, env.clone(), b)
        });
        if let Some(k) = &key
            && let Some(&c) = self.memo.get(k)
        {
            return Ok(c);
        }
        let saved = std::mem::replace(&mut self.env, env);
        self.path.push(Frame { f: g, bound });
        let r = self.expr(&body);
        if r.is_ok() {
            self.path.pop();
        }
        self.env = saved;
        let c = r?.saturating_add(self.frame(g));
        if let Some(k) = key {
            self.memo.insert(k, c);
        }
        Ok(c)
    }

    fn frame(&mut self, g: FnId) -> u64 {
        if let Some(&c) = self.costs.get(&g) {
            return c;
        }
        let sema = &*self.sema;
        let c = sema.with_layouts(|l| tir::frame_cost(&sema.prog.fns[g.index()], &mut |t| l.size_of(t)));
        self.costs.insert(g, c);
        c
    }

    /// The function `e` is known to evaluate to, if any.
    fn target(&self, e: &Expr) -> Option<FnId> {
        match &e.kind {
            ExprKind::FnRef(f) | ExprKind::Closure(f) => Some(*f),
            ExprKind::Coerce { expr, .. } => self.target(expr),
            ExprKind::Local(l) => self.path.last()?.bound.get(l).copied(),
            ExprKind::Upvar { levels, local } => {
                let mut owner = self.path.last()?.f;
                for _ in 0..*levels {
                    owner = self.sema.prog.fns[owner.index()].parent?;
                }
                self.path.iter().rev().find(|fr| fr.f == owner)?.bound.get(local).copied()
            }
            _ => None,
        }
    }

    /// The functions known to be passed for `g`'s parameters.
    fn bind(&self, g: FnId, args: &[Expr]) -> HashMap<LocalId, FnId> {
        let params = &self.sema.prog.fns[g.index()].params;
        params.iter().zip(args).filter_map(|(p, a)| Some((*p, self.target(a)?))).collect()
    }

    /// The stack used below the current frame while evaluating `e`.
    fn expr(&mut self, e: &Expr) -> R<u64> {
        if e.ty.is_error() {
            return Err(Fail::Quiet);
        }
        let mut m = 0;
        match &e.kind {
            ExprKind::Call { callee, args } => {
                for a in args {
                    m = m.max(self.expr(a)?);
                }
                let c = match callee {
                    Callee::Fn(g) => self.call(*g, args, self.env.clone(), e.span)?,
                    Callee::Indirect(f) => {
                        m = m.max(self.expr(f)?);
                        let Some(g) = self.target(f) else {
                            return Err(Fail::At(
                                e.span,
                                "this calls a function value whose target is not known statically".into(),
                            ));
                        };
                        self.call(g, args, self.env.clone(), e.span)?
                    }
                    Callee::Perform { effect, op } => self.perform(effect, *op, args, e.span)?,
                };
                m = m.max(c);
            }
            ExprKind::Write { sink, args, .. } => {
                for a in args {
                    m = m.max(self.expr(a)?);
                }
                let c = match sink {
                    WriteSink::Fn(f) => {
                        m = m.max(self.expr(f)?);
                        let Some(g) = self.target(f) else {
                            return Err(Fail::At(
                                e.span,
                                "this writes to a function whose target is not known statically".into(),
                            ));
                        };
                        self.call(g, &[], self.env.clone(), e.span)?
                    }
                    WriteSink::Perform { effect, op } => self.perform(effect, *op, &[], e.span)?,
                };
                m = m.max(c);
            }
            ExprKind::With { handlers, on, body, .. } => {
                let mut installed = vec![];
                for h in handlers {
                    match h {
                        HandlerValue::Literal(x) => {
                            m = m.max(self.expr(x)?);
                            let TyKind::Handler(id) = x.ty.kind() else { return Err(Fail::Quiet) };
                            let lit = &self.sema.prog.handlers[*id as usize];
                            installed
                                .push(Installed { effect: lit.effect.clone(), clauses: Some(lit.clauses.clone()) });
                        }
                        HandlerValue::Decl { decls, value } => {
                            m = m.max(self.expr(value)?);
                            for d in decls {
                                let d = &self.sema.prog.handler_decls[d.index()];
                                installed
                                    .push(Installed { effect: d.effect.clone(), clauses: Some(d.clauses.clone()) });
                            }
                        }
                    }
                }
                if let Some(stack) = on {
                    // The body runs on its own fiber; the `ctl` clauses run here, called
                    // from the `with`, in the handler context outside it.
                    m = m.max(self.expr(stack)?);
                    for h in &installed {
                        for (op, c) in h.clauses.iter().flatten().enumerate() {
                            if self.sema.prog.ctl_ops.contains(&(h.effect.effect, op as u32)) {
                                m = m.max(self.call(*c, &[], self.env.clone(), e.span)?);
                            }
                        }
                    }
                } else {
                    let depth = self.env.len();
                    self.env.extend(installed);
                    let r = self.expr(body);
                    self.env.truncate(depth);
                    m = m.max(r?);
                }
            }
            ExprKind::Block(b) => {
                // Locals are distinct within a function, so binding them all first is
                // the same as binding each at its `let`.
                let f = self.path.last().expect("the analysis runs inside a function").f;
                for s in &b.stmts {
                    if let Stmt::Let { local, init: Some(init) } = s
                        && !self.sema.prog.fns[f.index()].locals[local.index()].mutable
                        && let Some(t) = self.target(init)
                    {
                        self.path.last_mut().unwrap().bound.insert(*local, t);
                    }
                }
                let mut kids = vec![];
                b.for_each_child(&mut |c| kids.push(c));
                for k in kids {
                    m = m.max(self.expr(k)?);
                }
            }
            _ => {
                let mut kids = vec![];
                e.for_each_child(&mut |c| kids.push(c));
                for k in kids {
                    m = m.max(self.expr(k)?);
                }
            }
        }
        Ok(m)
    }

    /// The stack used by performing operation `op` of `effect`.
    fn perform(&mut self, effect: &EffectRef, op: u32, args: &[Expr], span: Span) -> R<u64> {
        if self.sema.prog.ctl_ops.contains(&(effect.effect, op)) {
            // The fiber suspends; the clause runs on its driver's stack.
            return Ok(0);
        }
        let Some(i) = self.env.iter().rposition(|h| &h.effect == effect) else {
            return Err(Fail::At(span, format!("the handler of `{}` performed here is not known statically", effect)));
        };
        let Some(clauses) = &self.env[i].clauses else {
            return Err(Fail::At(span, format!("the handler of `{}` performed here is not known statically", effect)));
        };
        let c = clauses[op as usize];
        self.call(c, args, self.env[..i].to_vec(), span)
    }
}
