//! Fibers in generated C (§14): `with … on`, `ctl` performs, `resume`, `cancel`
//! and `@fiber`. The run-time half (context switching, the fiber pool, the
//! checks on continuations) is in `runtime/gm_rt.c`.
//!
//! The body of a `with … on` runs on a fiber, so it is moved out into a C
//! function of its own (`gm_wb…`), which reaches the enclosing frame through a
//! pointer: inside it, `F` is a macro for `(*gm_Fp)`, so the body's code is the
//! same as if it were inline. Its ladder ends at the fiber's base with the exit
//! still in progress; the driver loop then runs the ladder of the `with` itself.

use super::func::FnGen;
use super::{CResult, unsupported};
use crate::tir::{Expr, LabelId};
use crate::types::{CallConv, Ty, TyKind};
use std::fmt::Write;

impl<'g, 'a> FnGen<'g, 'a> {
    /// Generates a C function from the code `emit` produces, with its own scopes and
    /// handler context `cx`. Returns the function's text.
    fn detached(&mut self, head: &str, cx: &str, emit: impl FnOnce(&mut Self) -> CResult<()>) -> CResult<String> {
        let saved_out = std::mem::replace(&mut self.out, format!("{} {{\n", head));
        let saved_scopes = std::mem::take(&mut self.scopes);
        let saved_cx = std::mem::replace(&mut self.cx, cx.to_string());
        let saved_indent = std::mem::replace(&mut self.indent, 1);
        let saved_check = std::mem::replace(&mut self.no_check, false);
        let saved_lad = std::mem::replace(&mut self.lad_used, false);
        let r = self.without_runs(emit);
        self.end_lines();
        let text = std::mem::replace(&mut self.out, saved_out);
        self.scopes = saved_scopes;
        self.cx = saved_cx;
        self.indent = saved_indent;
        self.no_check = saved_check;
        self.lad_used = saved_lad;
        r.map(|_| text)
    }

    /// The rest of `with h on stack { body }`, once its handlers are installed in the
    /// context `cx`, whose nodes point at the `gm_with` named `w`.
    pub fn with_on(
        &mut self,
        e: &'a Expr,
        label: LabelId,
        w: &str,
        cx: &str,
        stack: &'a Expr,
        body: &'a Expr,
    ) -> CResult<String> {
        let st = self.expr(stack)?;
        let sa = self.addr_of(stack.ty, &st);
        let (p, l) = (self.slice_ptr(&sa), self.slice_len(&sa));
        let loc = self.loc(e);
        let body_fn = self.outline_body(label, e.ty, body)?;
        let fb = format!("fb{}", self.fresh());
        let end = format!("we{}", self.fresh());
        self.line(&format!("{w}.body_cx = {cx};"));
        self.line(&format!("gm_fiber *{fb} = gm_fiber_new(&{w}, (uint8_t *){p}, {l}, {body_fn}, 0, {loc});"));
        self.line(&format!("{fb}->frame = &F; {fb}->env = env; {fb}->inst = inst; {fb}->cx = {cx};"));
        // The clauses of its `ctl` operations run here, as if called by the `with`.
        self.call_site(e.span);
        self.line(&format!("gm_with_go(&{w}, {fb});"));
        self.line("for (;;) {");
        self.indent += 1;
        self.line(&format!("if ({w}.event == GM_EV_PERFORM) {{"));
        self.line(&format!("    gm_with_clause(&{w});"));
        self.line(&format!(
            "    if (gm_unw.kind == GM_RESUME) {{ gm_unw.kind = GM_NONE; gm_with_go(&{w}, {w}.next); continue; }}"
        ));
        // A clause's exit crosses the `with` unless it is a `break` to the `with` (§14.4).
        self.line(&format!(
            "    gm_with_end(&{w}, !(gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == {}), {loc});",
            label.0
        ));
        self.line(&format!("}} else if ({w}.event_fiber == {fb} && !gm_unw.kind) {{"));
        self.line(&format!("    gm_with_end(&{w}, false, {loc});"));
        self.line(&format!("    goto {end};"));
        self.line("} else {");
        self.line(&format!("    if (!gm_unw.kind) gm_trap(\"a fiber's function returned\", {loc});"));
        self.line(&format!("    gm_with_end(&{w}, true, {loc});"));
        self.line("}");
        self.line(&format!("if (!gm_unw.kind) gm_trap(\"internal error: a `ctl` clause finished normally\", {loc});"));
        let scope = self.with_scope(label, end.clone());
        self.scopes.push(scope);
        self.ladder();
        let scope = self.scopes.pop().unwrap();
        self.end_scope(scope, false);
        self.indent -= 1;
        self.line("}");
        self.label(&end);
        Ok(if e.ty.is_never() { self.zero(e.ty) } else { self.tmp(e.ty, &format!("F.w{}", label.0)) })
    }

    /// Moves the body of a `with … on` into a function that runs on its fiber.
    fn outline_body(&mut self, label: LabelId, ty: Ty, body: &'a Expr) -> CResult<String> {
        let name = format!("gm_wb{}_{}", self.tag, label.0);
        let fid = self.fid.0;
        let saved = std::mem::replace(&mut self.outlined, true);
        let text = self.detached(&format!("static void {}(gm_fiber *fb)", name), "cx", |fg| {
            fg.line(&format!("struct gm_fr{fid} *gm_Fp = (struct gm_fr{fid} *)fb->frame;"));
            fg.line("void *env = fb->env; gm_node *cx = fb->cx; gm_node *inst = fb->inst;");
            fg.line("(void)env; (void)cx; (void)inst;");
            // The body belongs to the frame of the enclosing function, whose record the
            // driver's chain ends with: its record stands in for that one.
            if fg.g.opts.debug {
                let fname = match fg.g.prog.backtrace_name(fg.fid, fg.g.sources) {
                    Some(n) => super::c_string(n.as_bytes()),
                    None => "NULL".into(),
                };
                fg.line(&format!(
                    "gm_bt B = {{ gm_bt_top ? gm_bt_top->prev : NULL, {fname}, NULL, 0, 0 }}; gm_bt_top = &B;"
                ));
            }
            fg.out.push_str("#define F (*gm_Fp)\n");
            let v = fg.expr(body)?;
            if !body.ty.is_never() && !ty.is_never() {
                fg.line(&format!("F.w{} = {};", label.0, v));
            }
            fg.out.push_str("gm_fprop:\n    gm_fiber_finish(fb);\n#undef F\n}\n");
            Ok(())
        });
        self.outlined = saved;
        let text = text?;
        let _ = writeln!(self.g.protos, "static void {}(gm_fiber *fb);", name);
        self.g.bodies.push_str(&text);
        Ok(name)
    }

    /// Performs `ctl` operation `op` through node `n` (§14.2): the arguments go into a
    /// request on the fiber's stack, and a per-site thunk calls the clause on the driver.
    pub fn ctl_perform(&mut self, n: &str, op: u32, vals: &[String], tys: &[Ty], ret: Ty) -> CResult<String> {
        let id = self.fresh();
        let req = format!("gm_pr{}_{}", self.tag, id);
        let thunk = format!("gm_pt{}_{}", self.tag, id);
        let mut fields = String::from("gm_node *n;");
        for (i, t) in tys.iter().enumerate() {
            let _ = write!(fields, " {} a{};", self.g.ty.cty(*t), i);
        }
        let _ = writeln!(self.g.frames, "struct {} {{ {} }};", req, fields);
        let cont = Ty::intern(TyKind::Cont(ret));
        let mut ptys = tys.to_vec();
        ptys.push(cont);
        let fp = self.fn_ptr_type(Ty::never(), &ptys);
        let kc = self.g.ty.cty(cont);
        let mut args: Vec<String> = (0..tys.len())
            .map(|i| if self.g.ty.by_ref(tys[i]) { format!("&r->a{}", i) } else { format!("r->a{}", i) })
            .collect();
        args.push("kb".into());
        let head = format!("static void {}(void *req, gm_cont k)", thunk);
        let _ = writeln!(self.g.protos, "{};", head);
        let _ = writeln!(
            self.g.bodies,
            "{head} {{ struct {req} *r = (struct {req} *)req; gm_node *n = r->n; {kc} kb; memcpy(&kb, &k, sizeof kb); (({fp})n->clauses[{op}])(n->env, n->hctx, n, {}); }}",
            args.join(", ")
        );
        let r = format!("pr{}", self.fresh());
        let mut init = vec![n.to_string()];
        init.extend(vals.iter().cloned());
        self.line(&format!("struct {} {} = {{{}}};", req, r, init.join(", ")));
        let res = (!ret.is_unit() && !ret.is_never()).then(|| self.tmp_uninit(ret));
        let res_ptr = res.as_ref().map_or("NULL".to_string(), |r| format!("&{}", r));
        self.line(&format!("gm_ctl_perform({}->fw, {}, &{}, {});", n, thunk, r, res_ptr));
        self.check_unwind();
        Ok(match res {
            Some(r) => r,
            None => self.zero(ret),
        })
    }

    /// `resume k(v)` (§14.3): stores `v` for the fiber and ends the clause with GM_RESUME.
    pub fn resume(&mut self, e: &'a Expr, cont: &'a Expr, value: &'a Expr) -> CResult<String> {
        let k = self.expr(cont)?;
        let ka = self.addr_of(cont.ty, &k);
        let v = self.expr(value)?;
        let loc = self.loc(e);
        let rp = format!("rp{}", self.fresh());
        self.line(&format!("void *{} = gm_resume(inst->fw, {}, {});", rp, ka, loc));
        if value.ty.is_unit() || value.ty.is_never() {
            self.line(&format!("(void){};", rp));
        } else {
            let c = self.g.ty.cty(value.ty);
            self.line(&format!("GM_ST({}, {}, {});", c, rp, v));
        }
        self.start_exit("GM_RESUME", "(&F.h)", "0", "0");
        Ok(self.zero(e.ty))
    }

    /// `cancel k` (§14.5).
    pub fn cancel(&mut self, e: &'a Expr, cont: &'a Expr) -> CResult<String> {
        let k = self.expr(cont)?;
        let ka = self.addr_of(cont.ty, &k);
        let loc = self.loc(e);
        self.line(&format!("gm_cancel_cont(inst->fw, {}, {});", ka, loc));
        self.check_unwind();
        Ok(self.unit())
    }

    /// `@fiber(f, arg, stack)` (§14.6): a fiber whose start function calls `f(arg)`.
    pub fn fiber_builtin(&mut self, e: &'a Expr, args: &'a [Expr]) -> CResult<String> {
        let TyKind::FnPtr(sig) = args[0].ty.kind() else {
            return unsupported(e.span, "`@fiber` with this function");
        };
        let hidden = !self.g.within_sys(&sig.effects);
        let f = self.expr(&args[0])?;
        let a = self.expr(&args[1])?;
        let s = self.expr(&args[2])?;
        let sa = self.addr_of(args[2].ty, &s);
        let (p, l) = (self.slice_ptr(&sa), self.slice_len(&sa));
        let aty = args[1].ty;
        let ac = self.g.ty.cty(aty);
        let size = self.g.ty.size(aty);
        let k = self.fresh();
        let start = format!("gm_fs{}_{}", self.tag, k);
        let text = self.detached(&format!("static void {}(gm_fiber *fb)", start), "fb->cx", |fg| {
            let arg = if size == 0 { fg.zero(aty) } else { format!("GM_LD({}, fb->arg)", ac) };
            let arg = fg.tmp(aty, &arg);
            fg.call_indirect("fb->fn", "NULL", "fb->cx", &[arg], &[aty], Ty::never(), hidden, CallConv::C);
            fg.out.push_str("}\n");
            Ok(())
        })?;
        let _ = writeln!(self.g.protos, "static void {}(gm_fiber *fb);", start);
        self.g.bodies.push_str(&text);
        let loc = self.loc(e);
        let fb = format!("nf{}", self.fresh());
        self.line(&format!("gm_fiber *{fb} = gm_fiber_new(inst->fw, (uint8_t *){p}, {l}, {start}, {size}, {loc});"));
        self.line(&format!("{fb}->fn = (void *){f}; {fb}->cx = inst->fw->body_cx;"));
        if size > 0 {
            self.line(&format!("GM_ST({}, {}->arg, {});", ac, fb, a));
        }
        let kc = format!("kc{}", self.fresh());
        self.line(&format!("gm_cont {} = gm_fiber_cont(inst->fw, {});", kc, fb));
        let c = self.g.ty.cty(e.ty);
        Ok(self.tmp(e.ty, &format!("GM_LD({}, &{})", c, kc)))
    }
}
