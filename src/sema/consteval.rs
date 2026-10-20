//! Compile-time evaluation during checking (§17): array lengths given by typed
//! constants, compile-time value arguments, `when` conditions, constants and static
//! assertions, and the like.
//!
//! Evaluation runs the interpreter on the program as checked so far. Before it
//! does, every function the expression can reach must have a checked body, so
//! pending bodies are checked and generic instances produced on demand.

use super::Sema;
use crate::diag::Diagnostic;
use crate::interp::{self, Mode, RunConfig};
use crate::layout::Layouts;
use crate::source::Span;
use crate::tir::{self, AdtDef, AdtKind, ConstValue, Expr, ExprKind, FnId, StaticData};
use crate::types::{AdtId, ConstKey, FloatTy, Ty, TyKind};
use std::collections::HashSet;

/// The default number of interpreter steps one compile-time evaluation may
/// take (§17.3); `#fuel(n)` changes it for one declaration.
pub const STEP_BUDGET: u64 = 50_000_000;

impl<'s> Sema<'s> {
    /// Evaluates a checked, closed expression. Reports a failed evaluation and
    /// returns `None`; returns `None` silently if the expression or a function
    /// it reaches contains an error that was already reported.
    pub fn eval_tir_const(&mut self, e: &Expr) -> Option<ConstValue> {
        match self.try_eval_const(e) {
            Ok(v) => Some(v),
            Err(None) => None,
            Err(Some(t)) => {
                self.report_eval_failure(e, &t);
                None
            }
        }
    }

    /// Evaluates a checked, closed expression: `Err(None)` if it (or a function it
    /// reaches) has an error already reported, `Err(Some(trap))` if the evaluation fails.
    pub fn try_eval_const(&mut self, e: &Expr) -> Result<ConstValue, Option<interp::Trap>> {
        if e.ty.is_error() || !self.prepare_for_eval(e) {
            return Err(None);
        }
        let cfg = RunConfig { target: self.target.clone(), mode: Mode::Debug, step_budget: Some(self.fuel) };
        interp::eval_const(&self.prog, &cfg, e).map_err(Some)
    }

    /// The default `d` of a field or parameter of type `ty`, declared in `module`, as a
    /// reference to its compile-time value (§17.2). Each default is checked and evaluated
    /// once per type it is used at; a failure is reported once, at the default, and
    /// gives `None` here and at every later use.
    pub fn default_value(&mut self, module: super::ModuleId, d: &crate::syntax::ast::Expr, ty: Ty) -> Option<Expr> {
        let key = (d.span, ty);
        if let Some(c) = self.default_consts.get(&key) {
            return c.map(|id| Expr { ty, kind: ExprKind::Const(id), span: d.span });
        }
        if !self.default_active.insert(key) {
            self.error(d.span, "this default depends on itself; the cycle cannot be evaluated (§3.4)");
            return None;
        }
        let checked = self.check_const_expr(module, d, Some(ty));
        let Some(ex) = checked.filter(|e| !e.ty.is_error()) else {
            self.default_active.remove(&key);
            self.default_consts.insert(key, None);
            return None;
        };
        // A literal is its own value.
        if matches!(ex.kind, ExprKind::Int(_) | ExprKind::Float(_) | ExprKind::Bool(_) | ExprKind::Null) {
            self.default_active.remove(&key);
            return Some(ex);
        }
        let value = self.eval_tir_const(&ex);
        self.default_active.remove(&key);
        let Some(value) = value else {
            self.default_consts.insert(key, None);
            return None;
        };
        let id = tir::ConstId(self.prog.consts.len() as u32);
        self.prog.consts.push(tir::ConstDef {
            name: crate::symbol::Symbol::intern("<default>"),
            ty,
            span: d.span,
            init: ex,
            value: Some(value),
        });
        self.default_consts.insert(key, Some(id));
        Some(Expr { ty, kind: ExprKind::Const(id), span: d.span })
    }

    /// Evaluates the defaults of declared parameters now, so that a default that fails is
    /// an error even if no call omits its argument.
    pub fn check_declared_defaults(&mut self) {
        let mut done = 0;
        while done < self.default_checks.len() {
            let (module, d, ty) = self.default_checks[done].clone();
            done += 1;
            self.default_value(module, &d, ty);
        }
    }

    /// Reports the failed compile-time evaluation of `e` with its call stack (§17.1).
    pub fn report_eval_failure(&mut self, e: &Expr, t: &interp::Trap) {
        {
            {
                let mut d = Diagnostic::error(e.span, format!("compile-time evaluation failed: {}", t.message));
                if t.span != Span::DUMMY && t.span != e.span {
                    d = d.with_label(t.span, "the evaluation stopped here");
                }
                // The compile-time call stack, innermost call first (§17.1).
                const SHOWN: usize = 12;
                for (f, call) in t.trace.iter().take(SHOWN) {
                    if *call == Span::DUMMY || *call == e.span {
                        continue;
                    }
                    let name = self.prog.backtrace_name(*f, &self.sess.sources);
                    let what = name.map_or("a core library function".to_string(), |n| format!("`{}`", n));
                    d = d.with_label(*call, format!("{} was called here", what));
                }
                if t.trace.len() > SHOWN {
                    d = d.with_note(format!("{} more calls are not shown", t.trace.len() - SHOWN));
                }
                self.emit(d);
            }
        }
    }

    /// The key of plain data of type `ty` stored in `bytes` in the target's layout. The
    /// bytes of an integer or a float are kept little-endian in the key, whatever the target. Values
    /// that compare equal with `is` (§5.18) have equal keys: `-0.0` is keyed as `0.0`, every
    /// NaN as one NaN, and padding bytes as zeros.
    pub fn const_key_of_bytes(&self, ty: Ty, bytes: &[u8]) -> ConstKey {
        if ty.is_integer() {
            ConstKey::Bytes(ty, crate::layout::int_bytes_swap_order(bytes, self.target.little_endian).into())
        } else {
            let mut canon = vec![0u8; bytes.len()];
            self.with_layouts(|l| canonical_bytes(l, &self.prog.adts, ty, bytes, &mut canon));
            if ty.is_float() {
                canon = crate::layout::int_bytes_swap_order(&canon, self.target.little_endian);
            }
            ConstKey::Bytes(ty, canon.into())
        }
    }

    /// The canonical key of a compile-time value used as a generic argument
    /// (§16.4): plain data by its bytes, a function pointer by its function.
    pub fn const_key(&mut self, v: &ConstValue, span: Span) -> Option<ConstKey> {
        match (v.relocs.as_slice(), v.ty.kind()) {
            ([], TyKind::Optional(inner)) if is_pointer(*inner) => {
                if v.bytes.iter().all(|b| *b == 0) {
                    Some(ConstKey::Null)
                } else {
                    self.error(span, "this compile-time argument is not supported yet");
                    None
                }
            }
            ([], _) if !is_pointer(v.ty) => Some(self.const_key_of_bytes(v.ty, &v.bytes)),
            ([r], _) if v.ty == Ty::string() && r.offset == 0 => {
                let StaticData::Bytes(data) = &r.target else {
                    self.error(span, "this compile-time string is not supported yet");
                    return None;
                };
                let len = self.const_key_of_bytes(Ty::usize(), &v.bytes[v.bytes.len() / 2..]).as_u64()?;
                let bytes = usize::try_from(r.addend)
                    .ok()
                    .zip(usize::try_from(len).ok())
                    .and_then(|(start, len)| data.get(start..start.checked_add(len)?));
                let Some(bytes) = bytes else {
                    self.error(span, "this compile-time string points outside its data (§17.4)");
                    return None;
                };
                Some(ConstKey::Str(bytes.into()))
            }
            ([r], TyKind::FnPtr(_)) | ([r], TyKind::Optional(_)) if r.offset == 0 && r.addend == 0 => match r.target {
                StaticData::Fn(f) => Some(ConstKey::Fn(f.0, self.prog.fns[f.index()].name)),
                _ => {
                    self.error(span, "this compile-time argument is not supported yet");
                    None
                }
            },
            _ => {
                self.error(
                    span,
                    format!(
                        "a compile-time argument of type `{}` is not supported yet (use plain data or a function)",
                        v.ty
                    ),
                );
                None
            }
        }
    }

    /// Makes sure every function reachable from `e` has a body. Returns false if
    /// one of them (or `e`) contains an error, or depends on a body being checked.
    fn prepare_for_eval(&mut self, e: &Expr) -> bool {
        let mut seen_fns: HashSet<FnId> = HashSet::new();
        let mut seen_consts = HashSet::new();
        let mut work: Vec<Expr> = vec![e.clone()];
        while let Some(x) = work.pop() {
            let mut fns = vec![];
            let mut consts = vec![];
            let mut ok = true;
            let mut param = None;
            walk(&x, &mut |n| {
                if n.ty.mentions_error() {
                    ok = false;
                }
                if let ExprKind::ValueParam(p) = n.kind {
                    param = Some((p, n.span));
                }
                match &n.kind {
                    ExprKind::Call { callee: tir::Callee::Fn(f), .. } | ExprKind::FnRef(f) | ExprKind::Closure(f) => {
                        fns.push(*f)
                    }
                    ExprKind::HandlerRef(h) => consts.push(Reach::Handler(h.index())),
                    ExprKind::With { handlers, .. } => {
                        for h in handlers {
                            if let tir::HandlerValue::Decl { decls, .. } = h {
                                consts.extend(decls.iter().map(|d| Reach::Decl(d.index())));
                            }
                        }
                    }
                    ExprKind::Const(c) => consts.push(Reach::Const(c.index())),
                    _ => {}
                }
            });
            if !ok {
                return false;
            }
            if let Some((p, span)) = param {
                // Generic bodies are checked once, so their parameters have no value yet.
                self.error(
                    span,
                    format!("`{}` is a compile-time parameter, whose value is not known here (§16.5)", p.name()),
                );
                return false;
            }
            for r in consts {
                match r {
                    Reach::Const(c) if seen_consts.insert(c) => work.push(self.prog.consts[c].init.clone()),
                    Reach::Handler(h) => fns.extend(self.prog.handlers[h].clauses.iter().copied()),
                    Reach::Decl(d) => fns.extend(self.prog.handler_decls[d].clauses.iter().copied()),
                    _ => {}
                }
            }
            for f in fns {
                if !seen_fns.insert(f) {
                    continue;
                }
                if !self.ensure_body(f, e.span) {
                    return false;
                }
                if let Some(b) = &self.prog.fns[f.index()].body {
                    work.push(b.clone());
                }
                if let Some(p) = self.prog.fns[f.index()].parent
                    && seen_fns.insert(p)
                    && !self.ensure_body(p, e.span)
                {
                    return false;
                }
            }
        }
        true
    }

    /// Checks `f`'s body now if it is still pending. Returns false if it cannot
    /// have one yet (it is being checked).
    pub(super) fn ensure_body(&mut self, f: FnId, span: Span) -> bool {
        if self.prog.fns[f.index()].body.is_some() {
            return true;
        }
        if let Some(i) = self.pending_bodies.iter().position(|p| p.fn_id == f) {
            let p = self.pending_bodies.remove(i);
            self.check_fn_body(p.fn_id, p.module, &p.item);
            return true;
        }
        if self.check_pending_clause(f) {
            return true;
        }
        if let Some(i) = self.mono_queue.iter().position(|r| r.instance == f) {
            let req = self.mono_queue.remove(i);
            if !self.ensure_body(req.generic, span) {
                return false;
            }
            self.produce_instance(req);
            return self.prog.fns[f.index()].body.is_some();
        }
        if self.checking.contains(&f) {
            let name = self.prog.fns[f.index()].name;
            self.error(
                span,
                format!(
                    "this compile-time evaluation calls `{}`, which depends on it; the cycle cannot be evaluated",
                    name
                ),
            );
            return false;
        }
        // Foreign functions have no body; calling one at compile time traps.
        if matches!(self.prog.fns[f.index()].kind, tir::FnKind::Foreign { .. }) {
            return true;
        }
        // A nested function or literal of the function being checked gets its body
        // when that function is finished.
        let name = self.prog.fns[f.index()].name;
        self.error(
            span,
            format!(
                "this compile-time evaluation calls `{}`, which is nested in the function being checked; it cannot run before that function is complete",
                name
            ),
        );
        false
    }
}

impl<'s> Sema<'s> {
    /// `@static_assert(cond, msg)` (§17.7). A condition that depends on the parameters
    /// of the generic body it is in is returned, to be checked for each instance.
    pub fn static_assert(
        &mut self,
        module: super::ModuleId,
        args: &[crate::syntax::ast::Arg],
        span: Span,
    ) -> Option<Expr> {
        use crate::syntax::ast::ArgKind;
        let (Some(ArgKind::Positional(cond)), msg) = (args.first().map(|a| &a.kind), args.get(1).map(|a| &a.kind))
        else {
            self.error(span, "`@static_assert(cond, \"message\")` takes a condition and a message");
            return None;
        };
        let ex = self.check_const_expr(module, cond, Some(Ty::bool()))?;
        let msg = match msg {
            Some(ArgKind::Positional(m)) => self.const_string(module, m),
            _ => None,
        };
        if depends_on_params(&ex) {
            let id = self.deferred_asserts.len() as u32;
            self.deferred_asserts.push(msg);
            let op = crate::tir::Builtin::StaticAssert(id);
            return Some(Expr { ty: Ty::unit(), kind: ExprKind::Builtin { op, args: vec![ex] }, span });
        }
        self.check_static_assert(&ex, msg.as_deref(), span, None);
        None
    }

    /// Reports a failed static assertion with condition `ex`; `instance` is the use
    /// that instantiated the generic body it is in.
    pub fn check_static_assert(&mut self, ex: &Expr, msg: Option<&str>, span: Span, instance: Option<Span>) {
        let Some(v) = self.eval_tir_const(ex) else { return };
        if v.bytes.first() != Some(&1) {
            let mut d = Diagnostic::error(span, format!("static assertion failed: {}", msg.unwrap_or("(no message)")));
            if let Some(at) = instance {
                d = d.with_label(at, "in the instance used here");
            }
            self.emit(d);
        }
    }

    /// The value of a constant string expression.
    pub fn const_string(&mut self, module: super::ModuleId, e: &crate::syntax::ast::Expr) -> Option<String> {
        let ex = self.check_const_expr(module, e, Some(Ty::string()))?;
        let v = self.eval_tir_const(&ex)?;
        match self.const_key(&v, e.span)? {
            ConstKey::Str(s) => Some(String::from_utf8_lossy(&s).into_owned()),
            _ => None,
        }
    }

    /// The core library's `SourceLocation` type, if it declares one.
    pub fn source_location_ty(&mut self) -> Option<Ty> {
        let d = self.prelude.and_then(|p| {
            self.modules[p.0 as usize].scope.get(&crate::symbol::Symbol::intern("SourceLocation")).copied()
        })?;
        match self.resolve_decl(d) {
            super::DeclValue::Type(ty) => Some(ty),
            _ => None,
        }
    }

    /// A `SourceLocation` value (§21.7) for `span` in function `function`.
    pub fn source_location(&mut self, span: Span, function: crate::symbol::Symbol) -> Expr {
        let ty = self.source_location_ty();
        let Some(ty) = ty else {
            self.error(span, "the core library does not declare `SourceLocation`");
            return super::func::error_expr(span);
        };
        let f = self.sess.sources.file(span.file);
        let (line, col) = f.line_col(span.lo);
        let file = f.path.to_string_lossy().into_owned();
        let leaf = |ty: Ty, kind: ExprKind| Expr { ty, kind, span };
        let fields = vec![
            leaf(Ty::string(), ExprKind::Str(std::rc::Rc::from(file.as_bytes()))),
            leaf(Ty::u32(), ExprKind::Int(line.into())),
            leaf(Ty::u32(), ExprKind::Int(col.into())),
            leaf(Ty::string(), ExprKind::Str(std::rc::Rc::from(function.as_str().as_bytes()))),
        ];
        leaf(ty, ExprKind::Aggregate(fields))
    }

    /// Evaluates the condition of a `when` (§3.6, §8.9). It may not depend on a
    /// generic parameter (§16.7).
    pub fn when_cond(&mut self, module: super::ModuleId, cond: &crate::syntax::ast::Expr) -> Option<bool> {
        let ex = self.check_const_expr(module, cond, Some(Ty::bool()))?;
        let mut generic = false;
        walk(&ex, &mut |n| {
            generic |= super::generics::has_param(n.ty)
                || matches!(n.kind, ExprKind::ValueParam(_))
                || matches!(
                    &n.kind,
                    ExprKind::SizeOf { .. }
                        | ExprKind::TypeIs { .. }
                        | ExprKind::IntLimit { .. }
                        | ExprKind::TypeName { .. }
                );
        });
        if generic {
            self.error(cond.span, "a `when` condition may not depend on a generic parameter (§16.7)");
            return None;
        }
        let v = self.eval_tir_const(&ex)?;
        Some(v.bytes.first() == Some(&1))
    }
}

enum Reach {
    Const(usize),
    Handler(usize),
    Decl(usize),
}

fn is_pointer(t: Ty) -> bool {
    matches!(
        t.kind(),
        TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Slice { .. } | TyKind::FnPtr(_) | TyKind::Rawptr
    )
}

fn walk(e: &Expr, f: &mut impl FnMut(&Expr)) {
    f(e);
    e.for_each_child(&mut |c| walk(c, f));
}

/// True if `e` depends on type or compile-time value parameters of a generic body.
fn depends_on_params(e: &Expr) -> bool {
    let mut found = crate::sema::generics::has_param(e.ty)
        || matches!(&e.kind, ExprKind::ValueParam(_))
        || matches!(&e.kind, ExprKind::SizeOf { of, .. } if crate::sema::generics::has_param(*of))
        || matches!(&e.kind, ExprKind::TypeIs { .. } | ExprKind::IntLimit { .. } | ExprKind::TypeName { .. });
    e.for_each_child(&mut |c| found |= depends_on_params(c));
    found
}

type AdtMap = std::collections::HashMap<AdtId, AdtDef>;

/// Copies the bytes of a value of type `ty` from `src` to `dst` (zeroed, of the same
/// length), leaving padding zero and giving equal floats equal bytes.
fn canonical_bytes(l: &Layouts, adts: &AdtMap, ty: Ty, src: &[u8], dst: &mut [u8]) {
    let n = src.len().min(dst.len());
    match ty.kind() {
        TyKind::Float(f) => {
            let (bits, man) = match f {
                FloatTy::F16 => (16, 10),
                FloatTy::F32 => (32, 23),
                FloatTy::F64 => (64, 52),
            };
            let size = bits / 8;
            if n < size {
                return;
            }
            let little = l.target.little_endian;
            let mut raw = 0u64;
            for i in 0..size {
                let b = if little { src[size - 1 - i] } else { src[i] };
                raw = raw << 8 | b as u64;
            }
            let mantissa = raw & ((1 << man) - 1);
            let exp_ones = (raw >> man) & ((1 << (bits - 1 - man)) - 1) == (1 << (bits - 1 - man)) - 1;
            let canon = if exp_ones && mantissa != 0 {
                // The quiet NaN with no payload.
                (((1u64 << (bits - 1 - man)) - 1) << man) | (1 << (man - 1))
            } else if raw & !(1 << (bits - 1)) == 0 {
                0
            } else {
                raw
            };
            for i in 0..size {
                let b = (canon >> (8 * i)) as u8;
                dst[if little { i } else { size - 1 - i }] = b;
            }
        }
        TyKind::Array { elem, .. } => {
            let size = l.size_of(*elem) as usize;
            if size > 0 {
                for (s, d) in src.chunks_exact(size).zip(dst.chunks_exact_mut(size)) {
                    canonical_bytes(l, adts, *elem, s, d);
                }
            }
        }
        TyKind::Tuple { elems, .. } => {
            let offsets = l.layout(ty).offsets;
            for (e, off) in elems.iter().zip(offsets) {
                canonical_field(l, adts, *e, off as usize, src, dst);
            }
        }
        TyKind::Adt(id, _) => match adts.get(id).map(|d| &d.kind) {
            Some(AdtKind::Struct { fields, bits: None, .. }) => {
                let offsets = l.layout(ty).offsets;
                for (f, off) in fields.iter().zip(offsets) {
                    canonical_field(l, adts, f.ty, off as usize, src, dst);
                }
            }
            _ => dst[..n].copy_from_slice(&src[..n]),
        },
        _ => dst[..n].copy_from_slice(&src[..n]),
    }
}

fn canonical_field(l: &Layouts, adts: &AdtMap, ty: Ty, off: usize, src: &[u8], dst: &mut [u8]) {
    let end = off.saturating_add(l.size_of(ty) as usize);
    if end <= src.len() && end <= dst.len() {
        canonical_bytes(l, adts, ty, &src[off..end], &mut dst[off..end]);
    }
}
