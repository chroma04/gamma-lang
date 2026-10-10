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

/// A clone of a function specialized for the handler literal installed around its
/// call (Appendix D.3): performs of that effect at the function's own level call the
/// clause directly, and the C compiler can inline it.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct Spec {
    /// The handler literal.
    pub handler: u32,
    /// The effect instance it handles (see `Gen::effect_id`).
    pub effect: u32,
}

pub struct FnGen<'g, 'a> {
    pub g: &'g mut Gen<'a>,
    pub fid: FnId,
    /// Distinguishes the C names of this function's helpers from those of its
    /// specialized clones.
    pub tag: String,
    /// Set when generating a specialized clone: the node of the handler is `cx`.
    pub spec: Option<Spec>,
    /// The call that a `with` being generated specializes for its handler.
    pub spec_call: Option<(*const Expr, Spec)>,
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
    /// The bytes of the temporaries declared so far, for the stack check.
    pub temp_bytes: u64,
}

/// Generates function `f` into `g`, or its clone specialized by `spec`.
pub fn gen_fn(g: &mut Gen, f: FnId, spec: Option<Spec>) -> CResult<()> {
    let prog = g.prog;
    let def = &prog.fns[f.index()];
    if let FnKind::Foreign { .. } = def.kind {
        return g.foreign_decl(f);
    }
    let Some(body) = &def.body else {
        return unsupported(def.span, format!("the function `{}` has no body", def.name));
    };
    if g.frame_done(f) {
        return gen_body(g, f, def, body, spec);
    }
    // The frame struct, shared by the function and its clones.
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
    gen_body(g, f, def, body, spec)
}

fn gen_body<'a>(g: &mut Gen<'a>, f: FnId, def: &'a FnDef, body: &'a Expr, spec: Option<Spec>) -> CResult<()> {
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
        "static __attribute__((aligned(4))) {}{}{} {}({})",
        hint_attrs(def.hints),
        conv_attr(def.callconv),
        ret,
        match spec {
            Some(s) => g.spec_name(f, s),
            None => g.fn_name(f),
        },
        params.join(", ")
    );
    let _ = writeln!(g.protos, "{};", sig);
    let tag = match spec {
        Some(s) => format!("{}_h{}", f.0, s.handler),
        None => f.0.to_string(),
    };
    let mut fg = FnGen {
        g,
        fid: f,
        tag,
        spec,
        spec_call: None,
        def,
        out: String::new(),
        indent: 1,
        counter: 0,
        scopes: vec![],
        cx: "cx".into(),
        no_check: false,
        outlined: false,
        temp_bytes: 0,
    };
    let _ = writeln!(fg.out, "{} {{", sig);
    if def.hints.fast_math {
        fg.line("GM_FAST_MATH_BODY");
    }
    if plain {
        fg.line("void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;");
    }
    fg.line(&format!("struct gm_fr{} F;", f.0));
    // Running out of stack is checked (§14.1, §18): the thread's or the fiber's.
    if fg.g.opts.checks {
        fg.line("GM_STACK_CHECK(&F, GM_TEMP_BYTES);");
    }
    // Debug builds keep a backtrace chain (see gm_bt in the runtime).
    let pop = if fg.g.opts.debug {
        let name = match fg.g.prog.backtrace_name(f, fg.g.sources) {
            Some(n) => super::c_string(n.as_bytes()),
            None => "NULL".into(),
        };
        fg.line(&format!("gm_bt B = {{ gm_bt_top, {name}, NULL, 0, 0 }}; gm_bt_top = &B;"));
        "gm_bt_top = B.prev; "
    } else {
        ""
    };
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
    // Debug builds fill a dead frame with 0xDD (§18.3).
    let kill = if fg.g.opts.debug { format!("{pop}memset(&F, 0xDD, sizeof F); ") } else { String::new() };
    if void_ret {
        let _ = write!(fg.out, "gm_fret:\n    {kill}return;\ngm_fprop:\n    {kill}return;\n}}\n");
    } else if kill.is_empty() {
        let r = if plain { fg.plain_value(def.ret, "F.ret") } else { "F.ret".into() };
        let _ = write!(fg.out, "gm_fret:\n    return {r};\ngm_fprop:\n    return {r};\n}}\n");
    } else {
        let r = if plain { fg.plain_value(def.ret, "gm_r") } else { "gm_r".into() };
        let ct = fg.g.ty.cty(def.ret);
        let tail = format!("{{ {ct} gm_r = F.ret; {kill}return {r}; }}");
        let _ = write!(fg.out, "gm_fret:\n    {tail}\ngm_fprop:\n    {tail}\n}}\n");
    }
    // The epilogue copies the result (and a plain function converts it).
    fg.temp_bytes += 2 * fg.g.ty.size(def.ret);
    let out = std::mem::take(&mut fg.out).replacen("GM_TEMP_BYTES", &format!("{}u", fg.temp_bytes), 1);
    g.bodies.push_str(&out);
    Ok(())
}

/// The C attributes (each with a trailing space) for a function's hints (§11.10).
fn hint_attrs(h: crate::tir::FnHints) -> String {
    let mut s = String::new();
    match h.inline {
        Some(true) => s.push_str("__attribute__((always_inline)) inline "),
        Some(false) => s.push_str("__attribute__((noinline)) "),
        None => {}
    }
    if h.cold {
        s.push_str("__attribute__((cold)) ");
    }
    if h.fast_math {
        s.push_str("GM_FAST_MATH_ATTR ");
    }
    s
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
        self.temp_bytes += self.g.ty.size(t);
        let c = self.g.ty.cty(t);
        let n = format!("t{}", self.fresh());
        self.line(&format!("{} {} = {};", c, n, init));
        n
    }

    /// Declares an uninitialized temporary of type `t`.
    pub fn tmp_uninit(&mut self, t: Ty) -> String {
        self.temp_bytes += self.g.ty.size(t);
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

    /// True if `e` reads a place whose type is large enough that copying it through a
    /// C temporary would cost noticeable stack.
    pub fn big_place(&mut self, e: &Expr) -> bool {
        let place = match &e.kind {
            ExprKind::Local(_) | ExprKind::Upvar { .. } | ExprKind::Deref(_) | ExprKind::Global(_) => true,
            ExprKind::Field { base, .. } => self.g.ty.bits_layout(base.ty).is_none(),
            ExprKind::Index { .. } | ExprKind::VariantPayload { .. } => true,
            _ => false,
        };
        place && self.g.ty.size(e.ty) > 256
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
                        let guard = is_err.then_some("gm_errdefer_runs(&F.h)");
                        self.defer_body(guard, body, true);
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

    /// The code of a deferred statement, run by a ladder or at the end of its block,
    /// only if `guard` (a C condition) holds when there is one. `unwinding` is true
    /// when it runs during a non-local exit: the exit in progress is set aside while
    /// it runs, so the calls it makes return normally.
    fn defer_body(&mut self, guard: Option<&str>, body: &'a Expr, unwinding: bool) {
        let saved_scopes = std::mem::take(&mut self.scopes);
        let saved_check = std::mem::replace(&mut self.no_check, true);
        match guard {
            Some(g) => self.line(&format!("if ({}) {{", g)),
            None => self.line("{"),
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
        if frame == "(&F.h)" && !self.outlined && self.local_exit(kind, label, is_err) {
            return;
        }
        self.line(&format!(
            "gm_unw.kind = {}; gm_unw.frame = {}; gm_unw.label = {}; gm_unw.is_err = {};",
            kind, frame, label, is_err
        ));
        let saved = std::mem::replace(&mut self.no_check, false);
        self.ladder();
        self.no_check = saved;
    }

    /// An exit whose target is in this C function jumps there directly, running the
    /// defers it crosses, without going through `gm_unw`: the frame's address need
    /// not be taken, so the C compiler can keep its locals in registers. Returns false
    /// (emitting nothing) when the target is not statically known.
    fn local_exit(&mut self, kind: &str, label: &str, is_err: &str) -> bool {
        let target = match kind {
            "GM_RETURN" => None,
            "GM_BREAK" | "GM_CONTINUE" => {
                let found = self.scopes.iter().rposition(|s| match s {
                    Scope::Loop { label: l, .. } => l.0.to_string() == label,
                    Scope::With { label: l, .. } => kind == "GM_BREAK" && l.0.to_string() == label,
                    Scope::Block(_) => false,
                });
                match found {
                    Some(i) => Some(i),
                    None => return false,
                }
            }
            _ => return false,
        };
        let scopes = std::mem::take(&mut self.scopes);
        let start = target.map_or(0, |i| i + 1);
        // Errdefers run on a `return` of an error only: a `break` or `continue` within
        // the function never runs them.
        let err_guard = (kind == "GM_RETURN" && is_err != "0").then_some(is_err);
        for s in scopes[start..].iter().rev() {
            if let Scope::Block(defers) = s {
                for (is_errdefer, body) in defers.iter().rev() {
                    match (is_errdefer, err_guard) {
                        (false, _) => self.defer_body(None, body, false),
                        (true, Some(g)) => self.defer_body(Some(g), body, false),
                        (true, None) => {}
                    }
                }
            }
        }
        let dest = match target.map(|i| &scopes[i]) {
            None => "gm_fret".to_string(),
            Some(Scope::Loop { brk, cont, .. }) => if kind == "GM_BREAK" { brk } else { cont }.clone(),
            Some(Scope::With { end, .. }) => end.clone(),
            Some(Scope::Block(_)) => unreachable!(),
        };
        self.scopes = scopes;
        self.line(&format!("goto {};", dest));
        true
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
                self.defer_body(None, body, false);
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
            // A large value read from a place is copied directly, without a temporary.
            Stmt::Let { local, init: Some(init) } if self.big_place(init) => {
                let a = self.place_addr(init)?;
                self.line(&format!("memmove(&F.l{}, {}, sizeof F.l{});", local.0, a, local.0));
            }
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
            Stmt::Assign { place, value } if self.big_place(value) => {
                let p = self.place_addr(place)?;
                let a = self.place_addr(value)?;
                self.line(&format!("memmove({}, {}, {});", p, a, self.g.ty.size(value.ty)));
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
