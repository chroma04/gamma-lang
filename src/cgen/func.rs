//! Functions: frames, blocks, statements, scopes, and the cleanup code that
//! runs on non-local exits.
//!
//! Every Gamma function becomes `static RET gm_fN(void *env, gm_node *cx,
//! gm_node *inst, params…)`. Its locals live in a frame struct `F` so that
//! function literals and handler clauses can reach them through `env`.
//!
//! `break`, `continue`, `return`, `try` and `Env.exit` set `gm_unw` and run the
//! *ladder* for the current position: the pending `defer`s from the innermost
//! scope outwards, with a test at each loop or `with` boundary for whether it is
//! the exit's target. The same ladder runs after any call that may exit
//! non-locally, when `gm_unw.kind` is set on return.

use super::{CResult, Gen, conv_attr, unsupported};
use crate::tir::{Block, Expr, ExprKind, FnDef, FnId, FnKind, LabelId, Stmt};
use crate::types::Ty;
use std::fmt::Write;

pub enum Scope<'a> {
    /// A block: its `defer`s registered so far, `(is_errdefer, body)`.
    Block(Vec<(bool, &'a Expr)>),
    /// A loop: C labels for leaving it and for its `continue` point.
    Loop { label: LabelId, brk: String, cont: String },
    /// A `with`: the C label after it.
    With { label: LabelId, end: String },
}

pub struct FnGen<'g, 'a> {
    pub g: &'g mut Gen<'a>,
    pub fid: FnId,
    pub def: &'a FnDef,
    pub out: String,
    pub indent: usize,
    counter: u32,
    pub scopes: Vec<Scope<'a>>,
    /// The C expression for the current handler context.
    pub cx: String,
    /// Set while generating a `defer` body: it cannot exit non-locally.
    pub no_check: bool,
    /// Set while generating the body of a `with … on`, moved into a function of its
    /// own: its ladder ends at the fiber's base.
    pub outlined: bool,
}

/// Generates function `f` into `g`.
pub fn gen_fn(g: &mut Gen, f: FnId) -> CResult<()> {
    let prog = g.prog;
    let def = &prog.fns[f.index()];
    if let FnKind::Foreign { .. } = def.kind {
        return g.foreign_decl(f);
    }
    let Some(body) = &def.body else {
        return unsupported(def.span, format!("the function `{}` has no body", def.name));
    };
    // The frame struct.
    let mut frame = format!("struct gm_fr{} {{\n    gm_frame h;\n", f.0);
    for (i, l) in def.locals.iter().enumerate() {
        let _ = writeln!(frame, "    {} l{}; /* {} */", g.ty.cty(l.ty), i, l.name);
    }
    let _ = writeln!(frame, "    {} ret;", g.ty.cty(def.ret));
    let mut withs = vec![];
    collect_withs(body, &mut withs);
    for (label, ty) in &withs {
        let _ = writeln!(frame, "    {} w{};", g.ty.cty(*ty), label.0);
    }
    frame.push_str("};\n");
    g.frames.push_str(&frame);
    // The signature: plain functions have the C calling convention (decisions in
    // docs/architecture.md); the others take the hidden env, cx and inst first.
    let plain = g.is_plain(f);
    let void_ret = plain && def.ret.is_unit();
    let ret = if void_ret {
        "void".to_string()
    } else if plain {
        g.ty.plain_cty(def.ret)
    } else {
        g.ty.cty(def.ret)
    };
    let mut params =
        if plain { vec![] } else { vec!["void *env".to_string(), "gm_node *cx".into(), "gm_node *inst".into()] };
    for (i, p) in def.params.iter().enumerate() {
        let t = def.locals[p.index()].ty;
        let c = if plain { g.ty.plain_cty(t) } else { g.ty.cty(t) };
        params.push(format!("{} a{}", c, i));
    }
    if params.is_empty() {
        params.push("void".into());
    }
    let sig = format!(
        "static __attribute__((aligned(4))) {}{} {}({})",
        conv_attr(def.callconv),
        ret,
        g.fn_name(f),
        params.join(", ")
    );
    let _ = writeln!(g.protos, "{};", sig);
    let mut fg = FnGen {
        g,
        fid: f,
        def,
        out: String::new(),
        indent: 1,
        counter: 0,
        scopes: vec![],
        cx: "cx".into(),
        no_check: false,
        outlined: false,
    };
    let _ = writeln!(fg.out, "{} {{", sig);
    if plain {
        fg.line("void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;");
    }
    fg.line(&format!("struct gm_fr{} F;", f.0));
    // Running out of a fiber's stack is checked (§14.1).
    if fg.g.opts.checks && !fg.g.prog.ctl_ops.is_empty() {
        fg.line("GM_STACK_CHECK(&F);");
    }
    fg.line("F.h.up = (gm_frame *)env;");
    let clause_lit = matches!(def.kind, FnKind::Clause);
    fg.line(&format!("F.h.clause_lit = {} && env != NULL;", clause_lit as u8));
    fg.line("(void)inst;");
    for (i, p) in def.params.iter().enumerate() {
        let t = def.locals[p.index()].ty;
        let a = if plain { fg.gamma_value(t, &format!("a{}", i)) } else { format!("a{}", i) };
        fg.line(&format!("F.l{} = {};", p.0, a));
    }
    let v = fg.expr(body)?;
    if !body.ty.is_never() {
        fg.line(&format!("F.ret = {};", v));
    }
    if void_ret {
        fg.out.push_str("gm_fret:\n    return;\ngm_fprop:\n    return;\n}\n");
    } else {
        let r = if plain { fg.plain_value(def.ret, "F.ret") } else { "F.ret".into() };
        let _ = write!(fg.out, "gm_fret:\n    return {r};\ngm_fprop:\n    return {r};\n}}\n");
    }
    let out = std::mem::take(&mut fg.out);
    g.bodies.push_str(&out);
    Ok(())
}

/// The labels and types of the `with`s in `e` (excluding nested functions).
fn collect_withs(e: &Expr, out: &mut Vec<(LabelId, Ty)>) {
    if let ExprKind::With { label, .. } = &e.kind {
        out.push((*label, e.ty));
    }
    e.for_each_child(&mut |c| collect_withs(c, out));
}

impl<'g, 'a> FnGen<'g, 'a> {
    pub fn line(&mut self, s: &str) {
        for _ in 0..self.indent {
            self.out.push_str("    ");
        }
        self.out.push_str(s);
        self.out.push('\n');
    }

    /// A fresh number for temporaries and labels.
    pub fn fresh(&mut self) -> u32 {
        self.counter += 1;
        self.counter
    }

    /// Declares a temporary of type `t` initialized to `init`; returns its name.
    pub fn tmp(&mut self, t: Ty, init: &str) -> String {
        let c = self.g.ty.cty(t);
        let n = format!("t{}", self.fresh());
        self.line(&format!("{} {} = {};", c, n, init));
        n
    }

    /// Declares an uninitialized temporary of type `t`.
    pub fn tmp_uninit(&mut self, t: Ty) -> String {
        let c = self.g.ty.cty(t);
        let n = format!("t{}", self.fresh());
        self.line(&format!("{} {};", c, n));
        n
    }

    pub fn zero(&mut self, t: Ty) -> String {
        format!("GM_ZERO({})", self.g.ty.cty(t))
    }

    pub fn unit(&mut self) -> String {
        self.zero(Ty::unit())
    }

    pub fn label(&mut self, name: &str) {
        let _ = writeln!(self.out, "{}:;", name);
    }

    // --- frames ------------------------------------------------------------------------------

    /// The frame `levels` up the capture chain: a `gm_frame *` expression and its function.
    pub fn frame_up(&self, levels: u32) -> (String, FnId) {
        let mut expr = "(&F.h)".to_string();
        let mut f = self.fid;
        for _ in 0..levels {
            expr = format!("({}->up)", expr);
            f = self.g.prog.fns[f.index()].parent.unwrap_or(f);
        }
        (expr, f)
    }

    /// The C lvalue of field `field` of the frame `levels` up.
    pub fn frame_field(&self, levels: u32, field: &str) -> String {
        if levels == 0 {
            return format!("F.{}", field);
        }
        let (p, f) = self.frame_up(levels);
        format!("((struct gm_fr{} *){})->{}", f.0, p, field)
    }

    // --- non-local exits ---------------------------------------------------------------------

    /// After a call that may exit non-locally: run the ladder if an exit is in progress.
    pub fn check_unwind(&mut self) {
        if self.no_check {
            return;
        }
        self.line("if (gm_unw.kind) {");
        self.indent += 1;
        self.ladder();
        self.indent -= 1;
        self.line("}");
    }

    /// Runs pending defers outwards, stopping at the exit's target (see the module docs).
    pub fn ladder(&mut self) {
        let scopes = std::mem::take(&mut self.scopes);
        for s in scopes.iter().rev() {
            match s {
                Scope::Block(defers) => {
                    for (is_err, body) in defers.iter().rev() {
                        self.defer_body(*is_err, body, true);
                    }
                }
                Scope::Loop { label, brk, cont } => {
                    self.line(&format!("if (gm_unw.frame == &F.h && gm_unw.label == {}) {{", label.0));
                    self.line(&format!("    if (gm_unw.kind == GM_BREAK) {{ gm_unw.kind = GM_NONE; goto {}; }}", brk));
                    self.line(&format!(
                        "    if (gm_unw.kind == GM_CONTINUE) {{ gm_unw.kind = GM_NONE; goto {}; }}",
                        cont
                    ));
                    self.line("}");
                }
                Scope::With { label, end } => {
                    self.line(&format!(
                        "if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == {}) {{ gm_unw.kind = GM_NONE; goto {}; }}",
                        label.0, end
                    ));
                }
            }
        }
        self.scopes = scopes;
        if !self.outlined {
            self.line("if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }");
        }
        self.line("goto gm_fprop;");
    }

    /// The code of a deferred statement, run by a ladder or at the end of its block.
    /// `unwinding` is true when it runs during a non-local exit: the exit in progress
    /// is set aside while it runs, so the calls it makes return normally.
    fn defer_body(&mut self, is_err: bool, body: &'a Expr, unwinding: bool) {
        let saved_scopes = std::mem::take(&mut self.scopes);
        let saved_check = std::mem::replace(&mut self.no_check, true);
        if is_err {
            self.line("if (gm_errdefer_runs(&F.h)) {");
        } else {
            self.line("{");
        }
        self.indent += 1;
        if unwinding {
            self.line("gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;");
        }
        // A defer body cannot fail to translate here if it translated before; errors
        // are reported where the defer statement is first seen.
        let _ = self.expr(body);
        if unwinding {
            self.line("gm_unw = gm_saved;");
        }
        self.indent -= 1;
        self.line("}");
        self.no_check = saved_check;
        self.scopes = saved_scopes;
    }

    /// Starts a non-local exit of `kind` to `frame` (a `gm_frame *`) and `label`.
    pub fn start_exit(&mut self, kind: &str, frame: &str, label: &str, is_err: &str) {
        self.line(&format!(
            "gm_unw.kind = {}; gm_unw.frame = {}; gm_unw.label = {}; gm_unw.is_err = {};",
            kind, frame, label, is_err
        ));
        let saved = std::mem::replace(&mut self.no_check, false);
        self.ladder();
        self.no_check = saved;
    }

    // --- blocks and statements ------------------------------------------------------------------

    /// A block. Temporaries have unique names, so no C braces are needed and the
    /// block's value stays visible after it.
    pub fn block(&mut self, b: &'a Block) -> CResult<String> {
        self.scopes.push(Scope::Block(vec![]));
        let mut result = None;
        let r = self.block_inner(b, &mut result);
        let Some(Scope::Block(defers)) = self.scopes.pop() else { unreachable!() };
        r?;
        // Leaving normally runs the plain defers (not the errdefers), last first.
        for (is_err, body) in defers.iter().rev() {
            if !is_err {
                self.defer_body(false, body, false);
            }
        }
        Ok(match result {
            Some(v) => v,
            None => self.unit(),
        })
    }

    fn block_inner(&mut self, b: &'a Block, result: &mut Option<String>) -> CResult<()> {
        for s in &b.stmts {
            self.stmt(s)?;
        }
        if let Some(t) = &b.tail {
            let v = self.expr(t)?;
            // Defers run after the value is computed, so it is kept in a temporary.
            *result = Some(if t.ty.is_never() { self.zero(t.ty) } else { self.tmp(t.ty, &v) });
        }
        Ok(())
    }

    fn stmt(&mut self, s: &'a Stmt) -> CResult<()> {
        match s {
            Stmt::Let { local, init } => {
                if let Some(init) = init {
                    let v = self.expr(init)?;
                    if !init.ty.is_never() {
                        self.line(&format!("F.l{} = {};", local.0, v));
                    }
                }
            }
            Stmt::LetTuple { locals, init } => {
                let v = self.expr(init)?;
                let t = self.tmp(init.ty, &v);
                let offs = self.g.ty.offsets(init.ty);
                for (i, l) in locals.iter().enumerate() {
                    if let Some(l) = l {
                        let lt = self.def.locals[l.index()].ty;
                        let c = self.g.ty.cty(lt);
                        self.line(&format!("F.l{} = GM_LD({}, (uint8_t *)&{} + {});", l.0, c, t, offs[i]));
                    }
                }
            }
            // Assigning a `#bits` field rewrites its bits in the integer.
            Stmt::Assign { place: Expr { kind: ExprKind::Field { base, index }, .. }, value }
                if self.g.ty.bits_layout(base.ty).is_some() =>
            {
                let (u, fields) = self.g.ty.bits_layout(base.ty).unwrap();
                let (off, w, _) = fields[*index as usize];
                let a = self.place_addr(base)?;
                let v = self.expr(value)?;
                let raw = self.bits_raw(value.ty, &v);
                let uc = self.g.ty.cty(u);
                let m = super::expr::mask(w);
                self.line(&format!(
                    "GM_ST({uc}, {a}, ({uc})(((unsigned __int128)GM_LD({uc}, {a}) & ~({m} << {off})) | ((({raw}) & {m}) << {off})));"
                ));
            }
            Stmt::Assign { place, value } => {
                let p = self.place_addr(place)?;
                let v = self.expr(value)?;
                let c = self.g.ty.cty(place.ty);
                self.line(&format!("GM_ST({}, {}, {});", c, p, v));
            }
            Stmt::AssignTuple { places, value } => {
                let mut addrs = vec![];
                for p in places {
                    addrs.push(match p {
                        Some(p) => Some(self.place_addr(p)?),
                        None => None,
                    });
                }
                let v = self.expr(value)?;
                let t = self.tmp(value.ty, &v);
                let offs = self.g.ty.offsets(value.ty);
                for (i, a) in addrs.into_iter().enumerate() {
                    if let (Some(a), Some(pe)) = (a, &places[i]) {
                        let c = self.g.ty.cty(pe.ty);
                        self.line(&format!("GM_ST({}, {}, GM_LD({}, (uint8_t *)&{} + {}));", c, a, c, t, offs[i]));
                    }
                }
            }
            Stmt::Expr(e) => {
                self.expr(e)?;
            }
            Stmt::Defer { is_err, body } => {
                // Check that it translates; its code is emitted where it runs.
                let saved = std::mem::take(&mut self.out);
                let r = {
                    let saved_scopes = std::mem::take(&mut self.scopes);
                    let r = self.expr(body);
                    self.scopes = saved_scopes;
                    r
                };
                self.out = saved;
                r?;
                if let Some(Scope::Block(d)) = self.scopes.last_mut() {
                    d.push((*is_err, body));
                }
            }
        }
        Ok(())
    }
}
