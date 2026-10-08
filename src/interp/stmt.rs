//! Blocks, statements, `defer`/`errdefer`, patterns and structured control flow.

use super::machine::*;
use super::value::*;
use crate::tir::*;
use crate::types::*;

impl<'a> Machine<'a> {
    pub fn eval_block(&mut self, a: Act, b: &Block) -> R {
        let mut defers: Vec<(bool, &Expr)> = Vec::new();
        let mut result: R = Ok(Value::unit());
        for s in &b.stmts {
            match s {
                Stmt::Defer { is_err, body } => defers.push((*is_err, body)),
                _ => {
                    if let Err(f) = self.exec_stmt(a, s) {
                        result = Err(f);
                        break;
                    }
                }
            }
        }
        if result.is_ok()
            && let Some(t) = &b.tail
        {
            result = self.eval(a, t);
        }
        if defers.is_empty() { result } else { self.run_defers(a, defers, result) }
    }

    /// Whether `target` is the function that lexically contains the code running in `fr`.
    fn lexically_same(&self, fr: &Frame, target: u64) -> bool {
        let mut cur = fr;
        loop {
            if cur.id == target {
                return true;
            }
            if cur.is_clause_lit {
                match &cur.parent_env {
                    Some(p) => cur = p,
                    None => return false,
                }
            } else {
                return false;
            }
        }
    }

    /// Decides whether `errdefer` statements run when `flow` leaves a block of frame `fr` (§8.8).
    fn errdefers_run(&self, fr: &Frame, flow: &Flow) -> bool {
        match flow {
            Flow::Trap(_) => false,
            Flow::Return { frame, is_err, .. } => {
                if self.lexically_same(fr, *frame) {
                    *is_err
                } else {
                    true
                }
            }
            Flow::Break { frame, .. } | Flow::Continue { frame, .. } => !self.lexically_same(fr, *frame),
            Flow::Exit(_) => true,
        }
    }

    #[inline(never)]
    fn run_defers(&mut self, a: Act, defers: Vec<(bool, &Expr)>, result: R) -> R {
        let run_err = match &result {
            Ok(_) => false,
            Err(f) => {
                if matches!(**f, Flow::Trap(_)) {
                    return result;
                }
                self.errdefers_run(a.fr, f)
            }
        };
        for (is_err, body) in defers.into_iter().rev() {
            if is_err && !run_err {
                continue;
            }
            let mark = self.temps.len();
            let r = self.eval(a, body);
            self.kill_temps(mark);
            match r {
                Ok(_) => {}
                Err(f) => match *f {
                    Flow::Trap(t) => return Err(Box::new(Flow::Trap(t))),
                    _ => return self.trap(body.span, "internal error: control flow escaped a deferred statement"),
                },
            }
        }
        result
    }

    pub fn exec_stmt(&mut self, a: Act, s: &Stmt) -> R<()> {
        let mark = self.temps.len();
        let r = self.exec_stmt_inner(a, s);
        self.kill_temps(mark);
        r
    }

    fn local_ty(&self, fr: &Frame, id: LocalId) -> Option<Ty> {
        let f = fr.func?;
        self.prog.fns[f.index()].locals.get(id.index()).map(|l| l.ty)
    }

    fn exec_stmt_inner(&mut self, a: Act, s: &Stmt) -> R<()> {
        match s {
            Stmt::Let { local, init } => {
                match init {
                    Some(e) => {
                        let v = self.eval(a, e)?;
                        let ty = self.local_ty(a.fr, *local).or(Some(e.ty));
                        self.bind_local(e.span, a.fr, *local, ty, v)?;
                    }
                    None => {
                        if let Some(ty) = self.local_ty(a.fr, *local) {
                            let ti = self.ti(ty);
                            self.local_addr(crate::source::Span::DUMMY, a.fr, *local, ti.size, ti.align)?;
                        }
                    }
                }
                Ok(())
            }
            Stmt::LetTuple { locals, init } => {
                let b = self.eval_bytes(a, init)?;
                for (i, l) in locals.iter().enumerate() {
                    if let Some(l) = l {
                        let (fty, fb) = self.tr(init.span, self.field_bytes(init.ty, &b, i))?;
                        let ty = self.local_ty(a.fr, *l).or(Some(fty));
                        self.bind_local(init.span, a.fr, *l, ty, Value::Bytes(fb))?;
                    }
                }
                Ok(())
            }
            Stmt::Assign { place, value } => {
                let p = self.eval_place(a, place)?;
                let v = self.eval(a, value)?;
                match v {
                    Value::Bytes(b) => self.write_place(place.span, p, place.ty, &b),
                    _ => self.trap(place.span, "internal error: assignment of a second-class value"),
                }
            }
            Stmt::AssignTuple { places, value } => {
                let mut ps = Vec::with_capacity(places.len());
                for p in places {
                    ps.push(match p {
                        Some(p) => Some(self.eval_place(a, p)?),
                        None => None,
                    });
                }
                let b = self.eval_bytes(a, value)?;
                for (i, p) in ps.into_iter().enumerate() {
                    if let (Some(p), Some(pe)) = (p, &places[i]) {
                        let (_, fb) = self.tr(value.span, self.field_bytes(value.ty, &b, i))?;
                        self.write_place(pe.span, p, pe.ty, &fb)?;
                    }
                }
                Ok(())
            }
            Stmt::Expr(e) => {
                self.eval(a, e)?;
                Ok(())
            }
            Stmt::Defer { .. } => Ok(()),
        }
    }

    // --- conditions and patterns -------------------------------------------------------------------

    pub fn eval_conds(&mut self, a: Act, conds: &[CondPart]) -> R<bool> {
        for c in conds {
            match c {
                CondPart::Bool(e) => {
                    let b = self.eval_bytes(a, e)?;
                    if !self.truthy(&b) {
                        return Ok(false);
                    }
                }
                CondPart::Match { pat, value } => {
                    let b = self.eval_bytes(a, value)?;
                    if !self.match_pat(a, pat, &b)? {
                        return Ok(false);
                    }
                }
            }
        }
        Ok(true)
    }

    pub fn match_pat(&mut self, a: Act, pat: &Pat, b: &[u8]) -> R<bool> {
        let span = pat.span;
        match &pat.kind {
            PatKind::Wild => Ok(true),
            PatKind::Bind(l) => {
                self.bind_local(span, a.fr, *l, Some(pat.ty), Value::from_slice(b))?;
                Ok(true)
            }
            PatKind::Const(e) => {
                let c = self.eval_bytes(a, e)?;
                if e.ty != pat.ty
                    && let TyKind::Optional(inner) = pat.ty.kind()
                    && *inner == e.ty
                {
                    if !self.opt_present(pat.ty, b) {
                        return Ok(false);
                    }
                    return self.tr(span, self.eq(e.ty, &self.opt_payload(pat.ty, b), &c));
                }
                self.tr(span, self.eq(pat.ty, b, &c))
            }
            PatKind::Range { lo, hi, inclusive } => {
                let v = self.int_of(pat.ty, b);
                let lo = super::num::Int::from_big(lo.clone());
                let hi = super::num::Int::from_big(hi.clone());
                let ok_hi = if *inclusive { v.cmp(&hi).is_le() } else { v.cmp(&hi).is_lt() };
                Ok(v.cmp(&lo).is_ge() && ok_hi)
            }
            PatKind::Null => Ok(!self.opt_present(pat.ty, b)),
            PatKind::Some(p) => {
                if !self.opt_present(pat.ty, b) {
                    return Ok(false);
                }
                let payload = self.opt_payload(pat.ty, b);
                self.match_pat(a, p, &payload)
            }
            PatKind::Ok(p) => {
                if self.res_is_err(pat.ty, b) {
                    return Ok(false);
                }
                let payload = self.res_payload(pat.ty, b);
                self.match_pat(a, p, &payload)
            }
            PatKind::Err(p) => {
                if !self.res_is_err(pat.ty, b) {
                    return Ok(false);
                }
                let payload = self.res_payload(pat.ty, b);
                self.match_pat(a, p, &payload)
            }
            PatKind::Variant { variant, payload } => {
                let idx = self.tr(span, self.variant_index(pat.ty, b))?;
                if idx != *variant as usize {
                    return Ok(false);
                }
                match payload {
                    None => Ok(true),
                    Some(p) => {
                        let pt = self.variant_payload_ty(pat.ty, idx).unwrap_or(p.ty);
                        let off = self.payload_offset(pat.ty);
                        let sz = self.size_of(pt);
                        let pb = super::bytes::Bytes::from_slice(&b[off..off + sz]);
                        self.match_pat(a, p, &pb)
                    }
                }
            }
            PatKind::Fields(fields) => {
                for (i, p) in fields {
                    let (_, fb) = self.tr(span, self.field_bytes(pat.ty, b, *i as usize))?;
                    if !self.match_pat(a, p, &fb)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
        }
    }

    pub fn eval_match(&mut self, a: Act, e: &Expr, scrutinee: &Expr, arms: &[Arm]) -> R {
        let b = self.eval_bytes(a, scrutinee)?;
        for arm in arms {
            if !self.match_pat(a, &arm.pat, &b)? {
                continue;
            }
            if let Some(g) = &arm.guard {
                let gv = self.eval_bytes(a, g)?;
                if !self.truthy(&gv) {
                    continue;
                }
            }
            return self.eval(a, &arm.body);
        }
        self.trap(e.span, "no match arm matched the scrutinee")
    }

    pub fn eval_else(&mut self, a: Act, e: &Expr, lhs: &Expr, capture: Option<LocalId>, rhs: &Expr) -> R {
        let b = self.eval_bytes(a, lhs)?;
        let (present, payload_ty, payload) = match lhs.ty.kind() {
            TyKind::Optional(t) => {
                let p = self.opt_present(lhs.ty, &b);
                (p, *t, if p { self.opt_payload(lhs.ty, &b) } else { super::bytes::Bytes::empty() })
            }
            TyKind::Result { err, ok } => {
                let is_err = self.res_is_err(lhs.ty, &b);
                let pl = self.res_payload(lhs.ty, &b);
                if is_err && let Some(c) = capture {
                    self.bind_local(e.span, a.fr, c, Some(*err), Value::Bytes(pl.clone()))?;
                }
                (!is_err, *ok, pl)
            }
            _ => return self.trap(e.span, "internal error: `else` on a non-optional, non-result"),
        };
        if !present {
            return self.eval(a, rhs);
        }
        if e.ty == payload_ty {
            Ok(Value::Bytes(payload))
        } else if e.ty == lhs.ty {
            Ok(Value::Bytes(b))
        } else {
            // A success value lifted into a result with a different error type.
            Ok(Value::Bytes(self.make_res(e.ty, false, &payload)))
        }
    }

    pub fn eval_try(&mut self, a: Act, e: &Expr, expr: &Expr, lift: Option<Lift>, levels: u32) -> R {
        let b = self.eval_bytes(a, expr)?;
        let Some(target) = self.up_frame(a.fr, levels) else {
            return self.trap(e.span, "internal error: try without an enclosing function");
        };
        match expr.ty.kind() {
            TyKind::Optional(_) => {
                if self.opt_present(expr.ty, &b) {
                    Ok(Value::Bytes(self.opt_payload(expr.ty, &b)))
                } else {
                    let null = self.make_none(target.ret_ty);
                    Err(Box::new(Flow::Return { frame: target.id, value: Value::Bytes(null), is_err: false }))
                }
            }
            TyKind::Result { .. } => {
                if !self.res_is_err(expr.ty, &b) {
                    return Ok(Value::Bytes(self.res_payload(expr.ty, &b)));
                }
                let ev = self.res_payload(expr.ty, &b);
                let (rerr, _) = self.res_types(target.ret_ty);
                let ev = match lift {
                    Some(Lift::Variant(v)) => self.make_variant(rerr, v as usize, &ev),
                    None => ev,
                };
                let value = self.make_res(target.ret_ty, true, &ev);
                Err(Box::new(Flow::Return { frame: target.id, value: Value::Bytes(value), is_err: true }))
            }
            _ => self.trap(e.span, "internal error: `try` on a non-optional, non-result"),
        }
    }

    pub fn eval_loop(&mut self, a: Act, label: LabelId, cond: &[CondPart], body: &Block, step: Option<&Expr>) -> R {
        let mark = self.temps.len();
        loop {
            self.kill_temps(mark);
            if !self.eval_conds(a, cond)? {
                return Ok(Value::unit());
            }
            match self.eval_block(a, body) {
                Ok(_) => {}
                Err(f) => match *f {
                    Flow::Break { frame, label: l, value } if frame == a.fr.id && l == label => return Ok(value),
                    Flow::Continue { frame, label: l } if frame == a.fr.id && l == label => {}
                    other => return Err(Box::new(other)),
                },
            }
            if let Some(s) = step {
                self.eval(a, s)?;
            }
        }
    }
}
