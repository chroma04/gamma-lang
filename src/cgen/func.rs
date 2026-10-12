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
//!
//! The ladder is emitted once, not at every exit: each scope ends with its rung
//! (a *segment*), reached only by `goto`, that runs its part and jumps to the rung
//! of the enclosing scope. A block's segment has one entry per `defer`, so an exit
//! runs only the ones registered before it; the outermost rung (`gm_lad`) returns
//! or propagates. An exit is then a single `goto`, which keeps the C functions of
//! code with many calls and `defer`s small (C compilers, Fil-C's especially, are
//! slow on huge functions).

use super::{CResult, Gen, conv_attr, unsupported};
use crate::tir::{Block, Expr, ExprKind, FnDef, FnId, FnKind, LabelId, Stmt};
use crate::types::Ty;
use std::fmt::Write;

pub enum Scope<'a> {
    /// A block: its `defer`s registered so far, `(is_errdefer, body)`, and how many
    /// of them the exits so far run (the deepest entry of its segment in use).
    Block { id: u32, defers: Vec<(bool, &'a Expr)>, used: usize },
    /// A loop: C labels for leaving it and for its `continue` point.
    Loop { id: u32, label: LabelId, brk: String, cont: String, used: bool },
    /// A `with`: the C label after it.
    With { id: u32, label: LabelId, end: String, used: bool },
}

/// A specialization (Appendix D.3): the handler literal whose node is the `cx` of a
/// clone of a function, so performs of its effect at the clone's own level call the
/// clause directly and the C compiler can inline it. Interned by `Gen::spec`; the
/// number names clones (`gm_fN_sK`).
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct Spec(pub u32);

/// What a [`Spec`] stands for.
#[derive(Clone, PartialEq, Eq, Hash, Debug)]
pub struct SpecKey {
    /// The handler literal.
    pub handler: u32,
    /// The effect instance it handles (see `Gen::effect_id`).
    pub effect: u32,
    /// Set when the handler was installed by a clone specialized for `outer` at its own
    /// level: the handler's clauses run with `outer`'s node as their context, so they
    /// are called as clones specialized for `outer` too (an iterator adapter's loop
    /// body yields straight into the loop body of its consumer).
    pub outer: Option<Spec>,
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
    /// The call expression whose large result goes straight into a destination, and
    /// that destination (a C lvalue); see `expr_into`.
    pub dest_call: Option<(*const Expr, String)>,
    /// The destination of the call being emitted, from `dest_call`.
    pub call_dest: Option<String>,
    pub def: &'a FnDef,
    pub out: String,
    pub indent: usize,
    counter: u32,
    pub scopes: Vec<Scope<'a>>,
    /// The C expression for the current handler context.
    pub cx: String,
    /// Set while generating a `defer` body: it cannot exit non-locally.
    pub no_check: bool,
    /// Set once an exit jumps to `gm_lad`, the outermost rung of the ladder.
    pub lad_used: bool,
    /// Set while generating the body of a `with … on`, moved into a function of its
    /// own: its ladder ends at the fiber's base.
    pub outlined: bool,
    /// The bytes of the temporaries declared so far, for the stack check.
    pub temp_bytes: u64,
    /// For each parameter, by position: whether it is read in place, through its
    /// address `aN`, instead of from a copy in the frame (see `Gen::borrowed_params`).
    pub borrowed: Vec<bool>,
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
    // With `-g`, a field also has its local's name, with the type's debugger view.
    let names = if g.opts.debug_info {
        super::debug::unique_members(def.locals.iter().map(|l| l.name.as_str()), &mut Default::default())
    } else {
        vec![None; def.locals.len()]
    };
    for (i, l) in def.locals.iter().enumerate() {
        let c = g.ty.cty(l.ty);
        match &names[i] {
            Some(n) => {
                let view = g.ty.debug_view(l.ty);
                let (alias, dims) = view.map_or((c.clone(), String::new()), |v| (v.cty, v.dims));
                g.ty.add_member_name(n);
                let _ = writeln!(frame, "    union {{ {} l{}; {} {}{}; }};", c, i, alias, n, dims);
            }
            None => {
                let _ = writeln!(frame, "    {} l{}; /* {} */", c, i, l.name);
            }
        }
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
    // Large values cross by address (`Types::by_ref`): a large result is written to
    // `gm_out`, and a plain function's C-ABI entry point is then a wrapper
    // (`Gen::needs_wrapper`).
    let out_ret = g.ty.by_ref(def.ret);
    let void_ret = (plain && def.ret.is_unit()) || out_ret;
    let blob_ret = !plain;
    let ret = if void_ret {
        "void".to_string()
    } else if blob_ret {
        g.ty.cty(def.ret)
    } else {
        g.ty.plain_cty(def.ret)
    };
    let mut params = if out_ret { vec!["void *gm_out".to_string()] } else { vec![] };
    if !plain {
        params.extend(["void *env".to_string(), "gm_node *cx".into(), "gm_node *inst".into()]);
    }
    for (i, p) in def.params.iter().enumerate() {
        let t = def.locals[p.index()].ty;
        let c = if plain && !g.ty.by_ref(t) { g.ty.plain_cty(t) } else { g.ty.param_cty(t) };
        params.push(format!("{} a{}", c, i));
    }
    if params.is_empty() {
        params.push("void".into());
    }
    if spec.is_none() && g.needs_wrapper(f) {
        c_abi_wrapper(g, f, def);
    }
    let borrowed = g.borrowed_params(f);
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
        Some(s) => format!("{}_s{}", f.0, s.0),
        None => f.0.to_string(),
    };
    let mut fg = FnGen {
        g,
        fid: f,
        tag,
        spec,
        spec_call: None,
        dest_call: None,
        call_dest: None,
        def,
        out: String::new(),
        indent: 1,
        counter: 0,
        scopes: vec![],
        cx: "cx".into(),
        no_check: false,
        lad_used: false,
        outlined: false,
        temp_bytes: 0,
        borrowed,
    };
    let _ = writeln!(fg.out, "{} {{", sig);
    fg.src_line(def.span);
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
        if fg.borrowed[i] {
            continue;
        }
        if fg.g.ty.by_ref(t) {
            fg.line(&format!("memcpy(&F.l{}, a{}, sizeof F.l{});", p.0, i, p.0));
            continue;
        }
        let a = if plain { fg.gamma_value(t, &format!("a{}", i)) } else { format!("a{}", i) };
        fg.line(&format!("F.l{} = {};", p.0, a));
    }
    if out_ret && fg.builds_in_place(body) {
        // Built straight in the caller's destination: the body has no `defer`.
        let ct = fg.g.ty.cty(def.ret);
        fg.expr_into(body, &format!("(*({} *)gm_out)", ct))?;
        fg.line("goto gm_fdone;");
    } else if !body.ty.is_never() {
        fg.expr_into(body, "F.ret")?;
    } else {
        fg.expr(body)?;
    }
    if fg.lad_used {
        fg.line("goto gm_fret;");
        fg.label("gm_lad");
        fg.line("if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }");
        fg.line("goto gm_fprop;");
    }
    // Debug builds fill a dead frame with 0xDD (§18.3).
    let kill = if fg.g.opts.debug { format!("{pop}memset(&F, 0xDD, sizeof F); ") } else { String::new() };
    if out_ret {
        // `gm_fdone`: a `return` that wrote its value to `gm_out` itself.
        let _ = write!(
            fg.out,
            "gm_fret:\n    memcpy(gm_out, &F.ret, sizeof F.ret);\ngm_fdone:\n    {kill}return;\ngm_fprop:\n    {kill}return;\n}}\n"
        );
    } else if void_ret {
        let _ = write!(fg.out, "gm_fret:\n    {kill}return;\ngm_fprop:\n    {kill}return;\n}}\n");
    } else if kill.is_empty() {
        let r = if !blob_ret { fg.plain_value(def.ret, "F.ret") } else { "F.ret".into() };
        let _ = write!(fg.out, "gm_fret:\n    return {r};\ngm_fprop:\n    return {r};\n}}\n");
    } else {
        let r = if !blob_ret { fg.plain_value(def.ret, "gm_r") } else { "gm_r".into() };
        let ct = fg.g.ty.cty(def.ret);
        let tail = format!("{{ {ct} gm_r = F.ret; {kill}return {r}; }}");
        let _ = write!(fg.out, "gm_fret:\n    {tail}\ngm_fprop:\n    {tail}\n}}\n");
    }
    // The epilogue copies the result (and a plain function converts it).
    if !out_ret {
        fg.temp_bytes += 2 * fg.g.ty.size(def.ret);
    }
    fg.end_lines();
    let out = std::mem::take(&mut fg.out).replacen("GM_TEMP_BYTES", &format!("{}u", fg.temp_bytes), 1);
    g.bodies.push_str(&out);
    Ok(())
}

/// The C attributes (each with a trailing space) for a function's hints (§11.10).
/// Emits `gm_fN_c`, the C-ABI entry point of plain function `f` (see
/// `Gen::needs_wrapper`): it takes and returns values as C does and calls `gm_fN`.
fn c_abi_wrapper(g: &mut Gen, f: FnId, def: &FnDef) {
    let mut params = vec![];
    let mut args = vec![];
    for (i, p) in def.params.iter().enumerate() {
        let t = def.locals[p.index()].ty;
        params.push(format!("{} a{}", g.ty.plain_cty(t), i));
        args.push(if g.ty.by_ref(t) { format!("&a{}", i) } else { format!("a{}", i) });
    }
    if params.is_empty() {
        params.push("void".into());
    }
    let call = format!("{}({})", g.fn_name(f), args.join(", "));
    let (ret, body) = if def.ret.is_unit() {
        ("void".to_string(), format!("{};", call))
    } else if g.ty.by_ref(def.ret) {
        let r = g.ty.plain_cty(def.ret);
        let c = g.ty.cty(def.ret);
        args.insert(0, "&gm_r".into());
        let call = format!("{}({})", g.fn_name(f), args.join(", "));
        let v = match g.ty.mirror(def.ret) {
            Some(m) => format!("GM_CONV({}, gm_r)", m),
            None => "gm_r".into(),
        };
        (r, format!("{} gm_r; {}; return {};", c, call, v))
    } else {
        let r = g.ty.plain_cty(def.ret);
        let v = match g.ty.mirror(def.ret) {
            Some(m) if g.ty.by_ref(def.ret) => format!("GM_CONV({}, {})", m, call),
            _ => call,
        };
        (r, format!("return {};", v))
    };
    let sig = format!(
        "static __attribute__((aligned(4))) {}{} gm_f{}_c({})",
        conv_attr(def.callconv),
        ret,
        f.0,
        params.join(", ")
    );
    let _ = writeln!(g.protos, "{};", sig);
    let _ = writeln!(g.bodies, "{} {{ {} }}", sig, body);
}

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

/// Where statement `s` starts, for debug information.
fn stmt_span(s: &Stmt) -> Option<crate::source::Span> {
    match s {
        Stmt::Let { init: Some(e), .. } | Stmt::LetTuple { init: e, .. } | Stmt::Expr(e) => Some(e.span),
        Stmt::Assign { place, .. } => Some(place.span),
        Stmt::AssignTuple { places, value } => Some(places.iter().flatten().next().map_or(value.span, |p| p.span)),
        Stmt::Let { init: None, .. } | Stmt::Defer { .. } => None,
    }
}

/// The labels and types of the `with`s in `e` (excluding nested functions).
fn collect_withs(e: &Expr, out: &mut Vec<(LabelId, Ty)>) {
    if let ExprKind::With { label, .. } = &e.kind {
        out.push((*label, e.ty));
    }
    e.for_each_child(&mut |c| collect_withs(c, out));
}

impl<'g, 'a> FnGen<'g, 'a> {
    /// With `-g`, maps the code that follows to the source line where `span` starts.
    pub fn src_line(&mut self, span: crate::source::Span) {
        if let Some(d) = self.g.line_directive(span) {
            self.out.push_str(&d);
            self.out.push('\n');
        }
    }

    /// With `-g`, ends the text of a C function: what follows is mapped to the C file.
    pub fn end_lines(&mut self) {
        if self.g.opts.debug_info {
            self.out.push_str(super::debug::LINE_RESET);
            self.out.push('\n');
        }
    }

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

    /// The address (`uint8_t *`) of local `l`: in the frame, or the caller's value for
    /// a parameter read in place.
    pub fn local_addr(&self, l: crate::tir::LocalId) -> String {
        match self.def.params.iter().position(|p| *p == l) {
            Some(i) if self.borrowed[i] => format!("((uint8_t *)a{})", i),
            _ => format!("((uint8_t *)&F.l{})", l.0),
        }
    }

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

    /// Runs pending defers outwards, stopping at the exit's target (see the module docs):
    /// jumps to the rung of the ladder for the current position.
    pub fn ladder(&mut self) {
        let target = self.ladder_target();
        self.line(&format!("goto {};", target));
    }

    /// The C label of the rung of the ladder for the current position, marked as used.
    fn ladder_target(&mut self) -> String {
        for s in self.scopes.iter_mut().rev() {
            match s {
                Scope::Block { defers, .. } if defers.is_empty() => {}
                Scope::Block { id, defers, used } => {
                    *used = (*used).max(defers.len());
                    return format!("gm_ld{}_{}", id, defers.len());
                }
                Scope::Loop { id, used, .. } | Scope::With { id, used, .. } => {
                    *used = true;
                    return format!("gm_ll{}", id);
                }
            }
        }
        if self.outlined {
            return "gm_fprop".into();
        }
        self.lad_used = true;
        "gm_lad".into()
    }

    /// A new block scope.
    pub fn block_scope(&mut self) -> Scope<'a> {
        Scope::Block { id: self.fresh(), defers: vec![], used: 0 }
    }

    /// A new loop scope.
    pub fn loop_scope(&mut self, label: LabelId, brk: String, cont: String) -> Scope<'a> {
        Scope::Loop { id: self.fresh(), label, brk, cont, used: false }
    }

    /// A new `with` scope.
    pub fn with_scope(&mut self, label: LabelId, end: String) -> Scope<'a> {
        Scope::With { id: self.fresh(), label, end, used: false }
    }

    /// Emits the rung of the ladder for scope `s`, just popped, if an exit uses it: it
    /// runs the scope's part of the ladder and jumps to the rung of the enclosing
    /// scope. `fallthrough` is true when control can reach this point normally, so the
    /// rung is jumped over.
    pub fn end_scope(&mut self, s: Scope<'a>, fallthrough: bool) {
        let used = match &s {
            Scope::Block { used, .. } => *used > 0,
            Scope::Loop { used, .. } | Scope::With { used, .. } => *used,
        };
        if !used {
            return;
        }
        let skip = fallthrough.then(|| format!("gm_ls{}", self.fresh()));
        if let Some(l) = &skip {
            self.line(&format!("goto {};", l));
        }
        match s {
            Scope::Block { id, defers, used } => {
                for k in (1..=used).rev() {
                    self.label(&format!("gm_ld{}_{}", id, k));
                    let (is_err, body) = defers[k - 1];
                    let guard = is_err.then_some("gm_errdefer_runs(&F.h)");
                    self.defer_body(guard, body, true);
                }
            }
            Scope::Loop { id, label, brk, cont, .. } => {
                self.label(&format!("gm_ll{}", id));
                self.line(&format!("if (gm_unw.frame == &F.h && gm_unw.label == {}) {{", label.0));
                self.line(&format!("    if (gm_unw.kind == GM_BREAK) {{ gm_unw.kind = GM_NONE; goto {}; }}", brk));
                self.line(&format!("    if (gm_unw.kind == GM_CONTINUE) {{ gm_unw.kind = GM_NONE; goto {}; }}", cont));
                self.line("}");
            }
            Scope::With { id, label, end, .. } => {
                self.label(&format!("gm_ll{}", id));
                self.line(&format!(
                    "if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == {}) {{ gm_unw.kind = GM_NONE; goto {}; }}",
                    label.0, end
                ));
            }
        }
        self.ladder();
        if let Some(l) = &skip {
            self.label(l);
        }
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
                    Scope::Block { .. } => false,
                });
                match found {
                    Some(i) => Some(i),
                    None => return false,
                }
            }
            _ => return false,
        };
        let start = target.map_or(0, |i| i + 1);
        // Errdefers run on a `return` of an error only: a `break` or `continue` within
        // the function never runs them. An exit that crosses a `defer` takes the ladder,
        // so that each deferred body is emitted once.
        let err = kind == "GM_RETURN" && is_err != "0";
        let crosses = self.scopes[start..].iter().any(|s| match s {
            Scope::Block { defers, .. } => defers.iter().any(|(is_errdefer, _)| !is_errdefer || err),
            _ => false,
        });
        if crosses {
            return false;
        }
        let scopes = std::mem::take(&mut self.scopes);
        let dest = match target.map(|i| &scopes[i]) {
            None => "gm_fret".to_string(),
            Some(Scope::Loop { brk, cont, .. }) => if kind == "GM_BREAK" { brk } else { cont }.clone(),
            Some(Scope::With { end, .. }) => end.clone(),
            Some(Scope::Block { .. }) => unreachable!(),
        };
        self.scopes = scopes;
        self.line(&format!("goto {};", dest));
        true
    }

    // --- blocks and statements ------------------------------------------------------------------

    /// True if `e` is a large value (`Types::by_ref`) that `expr_into` builds in its
    /// destination rather than in a temporary copied there.
    pub fn builds_in_place(&mut self, e: &Expr) -> bool {
        if !self.g.ty.by_ref(e.ty) || e.ty.is_never() {
            return false;
        }
        match &e.kind {
            ExprKind::Call { .. } | ExprKind::Array(_) => true,
            ExprKind::Aggregate(_) => self.g.ty.bits_layout(e.ty).is_none(),
            ExprKind::Block(b) => {
                !b.stmts.iter().any(|s| matches!(s, Stmt::Defer { .. }))
                    && b.tail.as_ref().is_some_and(|t| self.builds_in_place(t))
            }
            _ => false,
        }
    }

    /// True if no deferred statement is pending in the function: a `return` runs none.
    pub fn no_defers(&self) -> bool {
        self.scopes.iter().all(|s| !matches!(s, Scope::Block { defers, .. } if !defers.is_empty()))
    }

    /// Evaluates `e` and stores its value in `dest`, a C lvalue of its type. A large
    /// value (see `builds_in_place`) is built there instead of in a temporary: a call
    /// writes its result there and an aggregate stores its elements there. Either way
    /// `dest` is written only once everything in `e` has been evaluated, as an
    /// assignment would, so `e` may read what `dest` holds.
    pub fn expr_into(&mut self, e: &'a Expr, dest: &str) -> CResult<()> {
        if !self.builds_in_place(e) {
            let v = self.expr(e)?;
            if !e.ty.is_never() {
                self.line(&format!("{} = {};", dest, v));
            }
            return Ok(());
        }
        match &e.kind {
            ExprKind::Call { .. } => {
                self.dest_call = Some((e as *const Expr, dest.to_string()));
                let v = self.expr(e);
                self.dest_call = None;
                let v = v?;
                if v != dest {
                    self.line(&format!("{} = {};", dest, v));
                }
            }
            ExprKind::Aggregate(elems) | ExprKind::Array(elems) => {
                let mut vals = vec![];
                for x in elems {
                    vals.push(self.expr(x)?);
                }
                let offs: Vec<u64> = match &e.kind {
                    ExprKind::Aggregate(_) => self.g.ty.offsets(e.ty),
                    _ => elems.iter().enumerate().map(|(i, x)| i as u64 * self.g.ty.size(x.ty)).collect(),
                };
                // Padding is zero, as in the interpreter.
                let filled: u64 = elems.iter().map(|x| self.g.ty.size(x.ty)).sum();
                if filled < self.g.ty.size(e.ty) {
                    self.line(&format!("memset(&{}, 0, sizeof {});", dest, dest));
                }
                for (i, (x, v)) in elems.iter().zip(&vals).enumerate() {
                    let c = self.g.ty.cty(x.ty);
                    self.line(&format!("GM_ST({}, (uint8_t *)&{} + {}, {});", c, dest, offs[i], v));
                }
            }
            ExprKind::Block(b) => {
                let scope = self.block_scope();
                self.scopes.push(scope);
                let r = self.stmts(b).and_then(|_| {
                    let t = b.tail.as_ref().unwrap();
                    self.src_line(t.span);
                    self.expr_into(t, dest)
                });
                let scope = self.scopes.pop().unwrap();
                r?;
                self.end_scope(scope, true);
            }
            _ => unreachable!(),
        }
        Ok(())
    }

    /// A block. Temporaries have unique names, so no C braces are needed and the
    /// block's value stays visible after it.
    pub fn block(&mut self, b: &'a Block) -> CResult<String> {
        let scope = self.block_scope();
        self.scopes.push(scope);
        let mut result = None;
        let r = self.block_inner(b, &mut result);
        let scope = self.scopes.pop().unwrap();
        r?;
        // Leaving normally runs the plain defers (not the errdefers), last first.
        if let Scope::Block { defers, .. } = &scope {
            for &(is_err, body) in defers.iter().rev() {
                if !is_err {
                    self.defer_body(None, body, false);
                }
            }
        }
        self.end_scope(scope, true);
        Ok(match result {
            Some(v) => v,
            None => self.unit(),
        })
    }

    fn block_inner(&mut self, b: &'a Block, result: &mut Option<String>) -> CResult<()> {
        self.stmts(b)?;
        if let Some(t) = &b.tail {
            self.src_line(t.span);
            let v = self.expr(t)?;
            // Defers run after the value is computed, so it is kept in a temporary.
            *result = Some(if t.ty.is_never() { self.zero(t.ty) } else { self.tmp(t.ty, &v) });
        }
        Ok(())
    }

    /// The statements of block `b`.
    fn stmts(&mut self, b: &'a Block) -> CResult<()> {
        // Each statement's temporaries are in a C block of their own, so the C compiler
        // can reuse their stack space for the next statement's.
        for s in &b.stmts {
            if let Some(span) = stmt_span(s) {
                self.src_line(span);
            }
            self.line("{");
            self.indent += 1;
            let r = self.stmt(s);
            self.indent -= 1;
            self.line("}");
            r?;
        }
        Ok(())
    }

    fn stmt(&mut self, s: &'a Stmt) -> CResult<()> {
        match s {
            // `---` leaves the local as it is (filled with 0xAA in Debug builds, §18.3).
            Stmt::Let { local, init: Some(Expr { kind: ExprKind::Uninit, .. }) } => {
                if self.g.opts.debug {
                    self.line(&format!("memset(&F.l{}, 0xAA, sizeof F.l{});", local.0, local.0));
                }
            }
            // A large value read from a place is copied directly, without a temporary.
            Stmt::Let { local, init: Some(init) } if self.big_place(init) => {
                let a = self.place_addr(init)?;
                self.line(&format!("memmove(&F.l{}, {}, sizeof F.l{});", local.0, a, local.0));
            }
            Stmt::Let { local, init } => {
                if let Some(init) = init {
                    self.expr_into(init, &format!("F.l{}", local.0))?;
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
                let (_, fields) = self.g.ty.bits_layout(base.ty).unwrap();
                let (foff, w, _) = fields[*index as usize];
                // A field of a nested `#bits` field: bits of the outermost integer.
                let mut outer = &**base;
                let mut off = foff;
                while let ExprKind::Field { base: b, index: i } = &outer.kind
                    && let Some((_, fs)) = self.g.ty.bits_layout(b.ty)
                {
                    off += fs[*i as usize].0;
                    outer = b;
                }
                let (u, _) = self.g.ty.bits_layout(outer.ty).unwrap();
                let a = self.place_addr(outer)?;
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
            Stmt::Assign { place, value } if self.builds_in_place(value) => {
                let p = self.place_addr(place)?;
                let c = self.g.ty.cty(place.ty);
                self.expr_into(value, &format!("(*({} *){})", c, p))?;
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
                if let Some(Scope::Block { defers, .. }) = self.scopes.last_mut() {
                    defers.push((*is_err, body));
                }
            }
        }
        Ok(())
    }
}
