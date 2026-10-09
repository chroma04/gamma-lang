//! The C back end: translates a checked [`Program`] to one C translation unit
//! and compiles it with the system C compiler.
//!
//! The generated code follows the interpreter's semantics exactly: values have
//! the same bytes (see [`types`]), checks trap with the same messages, and
//! `@write` formats through the runtime (`runtime/gm_rt.c`) using type
//! descriptors. The design is described in `docs/architecture.md`.

mod arith;
mod elementwise;
mod expr;
mod fiber;
mod func;
mod helpers;
mod types;
mod wide;

use crate::layout::{Layouts, Target};
use crate::source::{SourceMap, Span};
use crate::tir::{FnId, FnKind, Program};
use crate::types::{AdtId, CallConv, EffectRef, Ty, TyKind};
use std::collections::HashMap;
use std::fmt::Write;
use std::path::{Path, PathBuf};
use types::{Types, c_string};

const RT_H: &str = include_str!("../../runtime/gm_rt.h");
const RT_C: &str = include_str!("../../runtime/gm_rt.c");

/// A construct the C back end cannot translate yet.
#[derive(Debug)]
pub struct Unsupported {
    pub message: String,
    pub span: Span,
}

pub type CResult<T> = Result<T, Unsupported>;

pub fn unsupported<T>(span: Span, what: impl Into<String>) -> CResult<T> {
    Err(Unsupported { message: format!("not supported by the C back end yet: {}", what.into()), span })
}

/// Options of a C build.
#[derive(Clone, Debug)]
pub struct Options {
    /// Debug or Checked: checked operations trap (§1.4). Fast drops the checks.
    pub checks: bool,
    /// Debug: heap blocks are tracked so double and invalid frees trap.
    pub debug: bool,
}

/// The output of [`generate`].
pub struct Generated {
    /// The C translation unit.
    pub code: String,
    /// Libraries to link (from `foreign` blocks), beyond the C library.
    pub libs: Vec<String>,
}

/// Translates `prog` (whose entry is `entry`) to C source.
pub fn generate(prog: &Program, sources: &SourceMap, entry: FnId, opts: &Options) -> CResult<Generated> {
    let target = Target::host();
    let lookup = |id: AdtId| prog.adts.get(&id);
    let layouts = Layouts::new(target, &lookup);
    let mut g = Gen {
        prog,
        ty: Types::new(prog, &layouts),
        sources,
        opts: opts.clone(),
        effect_ids: HashMap::new(),
        statics: HashMap::new(),
        static_defs: String::new(),
        frames: String::new(),
        protos: String::new(),
        bodies: String::new(),
        helpers: helpers::Helpers::default(),
        reachable: vec![],
        queued: vec![false; prog.fns.len()],
        handler_tables: String::new(),
        libs: std::collections::BTreeSet::new(),
        handler_done: vec![false; prog.handlers.len()],
        decl_done: vec![false; prog.handler_decls.len()],
    };
    g.queue(entry);
    for (_, f) in &prog.exports {
        g.queue(*f);
    }
    while let Some(f) = g.reachable.pop() {
        func::gen_fn(&mut g, f)?;
    }
    let consts = g.gen_consts()?;
    let exports = g.gen_exports()?;
    let main = g.gen_main(entry)?;
    let mut out = String::new();
    out.push_str(RT_H);
    out.push('\n');
    out.push_str(RT_C);
    out.push_str("\n/* ---- generated ---- */\n");
    let _ = writeln!(out, "static int gm_checks = {};", opts.checks as u8);
    out.push_str(helpers::SUPPORT);
    out.push_str(&g.ty.blob_typedefs());
    out.push_str(g.ty.mirror_defs());
    out.push_str(&g.ty.descriptors());
    out.push_str(&g.static_defs);
    out.push_str(&g.frames);
    out.push_str(&g.protos);
    out.push_str(&g.helpers.protos);
    out.push_str(&g.handler_tables);
    out.push_str(&consts);
    out.push_str(&g.helpers.bodies);
    out.push_str(&g.bodies);
    out.push_str(&exports);
    out.push_str(&main);
    Ok(Generated { code: out, libs: g.libs.into_iter().collect() })
}

pub struct Gen<'a> {
    pub prog: &'a Program,
    pub ty: Types<'a>,
    pub sources: &'a SourceMap,
    pub opts: Options,
    effect_ids: HashMap<EffectRef, u32>,
    /// Static byte arrays by content.
    statics: HashMap<Vec<u8>, usize>,
    static_defs: String,
    pub frames: String,
    pub protos: String,
    pub bodies: String,
    pub helpers: helpers::Helpers,
    reachable: Vec<FnId>,
    queued: Vec<bool>,
    pub handler_tables: String,
    /// Libraries to link, from foreign blocks.
    pub libs: std::collections::BTreeSet<String>,
    handler_done: Vec<bool>,
    decl_done: Vec<bool>,
}

impl<'a> Gen<'a> {
    /// Marks `f` as needed; it is generated later.
    pub fn queue(&mut self, f: FnId) {
        if !self.queued[f.index()] {
            self.queued[f.index()] = true;
            self.reachable.push(f);
        }
    }

    pub fn fn_name(&self, f: FnId) -> String {
        format!("gm_f{}", f.0)
    }

    /// The number identifying an effect instance in handler contexts.
    pub fn effect_id(&mut self, e: &EffectRef) -> u32 {
        let n = self.effect_ids.len() as u32;
        *self.effect_ids.entry(e.clone()).or_insert(n)
    }

    /// The name of a static array holding `bytes` followed by a NUL byte.
    pub fn static_bytes(&mut self, bytes: &[u8]) -> String {
        if let Some(&i) = self.statics.get(bytes) {
            return format!("gm_s{}", i);
        }
        let i = self.statics.len();
        self.statics.insert(bytes.to_vec(), i);
        let _ =
            writeln!(self.static_defs, "static const uint8_t gm_s{}[{}] = {};", i, bytes.len() + 1, c_string(bytes));
        format!("gm_s{}", i)
    }

    /// `"file", line, col` for a trap at `span` (NULL location for none).
    pub fn loc(&self, span: Span) -> String {
        if span == Span::DUMMY {
            return "NULL, 0, 0".into();
        }
        let f = self.sources.file(span.file);
        let (line, col) = f.line_col(span.lo);
        format!("{}, {}, {}", c_string(f.path.display().to_string().as_bytes()), line, col)
    }

    /// The table of clause functions of handler literal `h`, generating it on first use.
    pub fn handler_table(&mut self, h: usize) -> String {
        if !self.handler_done[h] {
            self.handler_done[h] = true;
            let clauses = self.prog.handlers[h].clauses.clone();
            for c in &clauses {
                self.queue(*c);
            }
            let names: Vec<String> = clauses.iter().map(|c| format!("(void *){}", self.fn_name(*c))).collect();
            let _ = writeln!(self.handler_tables, "static void *const gm_hl{}[] = {{{}}};", h, names.join(", "));
        }
        format!("gm_hl{}", h)
    }

    /// The table of clause thunks of handler declaration `d`. A thunk has the uniform
    /// clause signature and loads `self` from the node's `env` before calling the clause.
    pub fn decl_table(&mut self, d: usize) -> CResult<String> {
        if !self.decl_done[d] {
            self.decl_done[d] = true;
            let clauses = self.prog.handler_decls[d].clauses.clone();
            let mut names = vec![];
            for (k, c) in clauses.iter().enumerate() {
                self.queue(*c);
                let def = &self.prog.fns[c.index()];
                let ret = self.ty.cty(def.ret);
                let ptys: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
                let self_ty = self.ty.cty(ptys[0]);
                let mut params = vec!["void *env".to_string(), "gm_node *cx".into(), "gm_node *inst".into()];
                let mut args = vec!["NULL".to_string(), "cx".into(), "inst".into(), format!("GM_LD({}, env)", self_ty)];
                for (i, t) in ptys.iter().enumerate().skip(1) {
                    params.push(format!("{} a{}", self.ty.cty(*t), i));
                    args.push(format!("a{}", i));
                }
                let name = format!("gm_dt{}_{}", d, k);
                let _ = writeln!(self.protos, "static {} {}({});", ret, name, params.join(", "));
                let _ = writeln!(
                    self.bodies,
                    "static {} {}({}) {{ return {}({}); }}",
                    ret,
                    name,
                    params.join(", "),
                    self.fn_name(*c),
                    args.join(", ")
                );
                names.push(format!("(void *){}", name));
            }
            let _ = writeln!(self.handler_tables, "static void *const gm_hd{}[] = {{{}}};", d, names.join(", "));
        }
        Ok(format!("gm_hd{}", d))
    }

    /// Top-level constants: storage initialized at startup from their compile-time
    /// values, with pointers patched in.
    fn gen_consts(&mut self) -> CResult<String> {
        let mut decls = String::new();
        let mut init = String::from("static void gm_init_consts(void) {\n");
        for (i, c) in self.prog.consts.iter().enumerate() {
            let Some(v) = &c.value else {
                return unsupported(c.span, "a constant without a compile-time value");
            };
            let (size, align) = (self.ty.size(c.ty), self.ty.align(c.ty).max(1));
            let _ = writeln!(decls, "static uint8_t gm_c{}[{}] __attribute__((aligned({})));", i, size.max(1), align);
            if !v.bytes.is_empty() {
                let s = self.static_bytes(&v.bytes);
                let _ = writeln!(init, "    memcpy(gm_c{}, {}, {});", i, s, v.bytes.len());
            }
            for r in &v.relocs {
                let target = match &r.target {
                    crate::tir::StaticData::Bytes(b) => self.static_bytes(b),
                    crate::tir::StaticData::Const(k) => format!("gm_c{}", k.0),
                    crate::tir::StaticData::Fn(f) => {
                        self.queue(*f);
                        format!("(void *){}", self.callee_name(*f))
                    }
                };
                let _ = writeln!(
                    init,
                    "    {{ void *p = (uint8_t *){} + {}; memcpy(gm_c{} + {}, &p, sizeof p); }}",
                    target, r.addend, i, r.offset
                );
            }
        }
        for (i, gl) in self.prog.globals.iter().enumerate() {
            let (size, align) = (self.ty.size(gl.ty), self.ty.align(gl.ty).max(1));
            if let Some(link) = gl.link_name {
                let _ = writeln!(decls, "extern uint8_t gm_g{}[] __asm__(GM_SYM(\"{}\"));", i, link);
                continue;
            }
            let tls = if gl.thread_local { "_Thread_local " } else { "" };
            let _ = writeln!(
                decls,
                "static {}uint8_t gm_g{}[{}] __attribute__((aligned({})));",
                tls,
                i,
                size.max(1),
                align
            );
            // A thread-local global gets its initial value in each thread, on first use,
            // through its accessor `gm_ga{i}()`.
            let mut tls_init = String::new();
            let init = if gl.thread_local { &mut tls_init } else { &mut init };
            let Some(v) = &gl.value else { continue };
            if !v.bytes.is_empty() {
                let s = self.static_bytes(&v.bytes);
                let _ = writeln!(init, "    memcpy(gm_g{}, {}, {});", i, s, v.bytes.len());
            }
            for r in &v.relocs {
                let target = match &r.target {
                    crate::tir::StaticData::Bytes(b) => self.static_bytes(b),
                    crate::tir::StaticData::Const(k) => format!("gm_c{}", k.0),
                    crate::tir::StaticData::Fn(f) => {
                        self.queue(*f);
                        format!("(void *){}", self.callee_name(*f))
                    }
                };
                let _ = writeln!(
                    init,
                    "    {{ void *p = (uint8_t *){} + {}; memcpy(gm_g{} + {}, &p, sizeof p); }}",
                    target, r.addend, i, r.offset
                );
            }
            if gl.thread_local {
                let _ = writeln!(
                    decls,
                    "static _Thread_local uint8_t gm_gi{i};\nstatic void gm_ginit{i}(void);\nstatic inline uint8_t *gm_ga{i}(void) {{ if (!gm_gi{i}) {{ gm_gi{i} = 1; gm_ginit{i}(); }} return gm_g{i}; }}"
                );
                let _ = writeln!(self.bodies, "static void gm_ginit{i}(void) {{\n{tls_init}}}");
            }
        }
        // Constants may refer to functions not otherwise reached.
        while let Some(f) = self.reachable.pop() {
            func::gen_fn(self, f)?;
        }
        init.push_str("}\n");
        Ok(format!("{}{}", decls, init))
    }

    /// Exported functions (§19.4): external C-ABI wrappers under their symbols.
    fn gen_exports(&mut self) -> CResult<String> {
        let mut s = String::new();
        for (sym, f) in self.prog.exports.clone() {
            let def = &self.prog.fns[f.index()];
            let mut params = vec![];
            let mut args = vec![];
            for (i, p) in def.params.iter().enumerate() {
                let t = def.locals[p.index()].ty;
                params.push(format!("{} a{}", self.c_abi_type(t, def.span)?, i));
                // The function takes mirrors itself (it has the plain convention).
                args.push(match self.ty.mirror(t) {
                    Some(_) => format!("a{}", i),
                    None => format!("({})a{}", self.ty.cty(t), i),
                });
            }
            let call = format!("{}({})", self.fn_name(f), args.join(", "));
            let params = if params.is_empty() { "void".to_string() } else { params.join(", ") };
            if def.ret.is_unit() {
                let _ = writeln!(s, "{}void {}({}) {{ {}; }}", conv_attr(def.callconv), sym, params, call);
            } else {
                let r = self.c_abi_type(def.ret, def.span)?;
                let call = if self.ty.mirror(def.ret).is_some() { call } else { format!("({}){}", r, call) };
                let _ = writeln!(s, "{}{} {}({}) {{ return {}; }}", conv_attr(def.callconv), r, sym, params, call);
            }
        }
        Ok(s)
    }

    /// The C `main`: installs the native ambient handlers, runs the entry function and
    /// turns its result into the exit status (as the interpreter's `run_main`).
    fn gen_main(&mut self, entry: FnId) -> CResult<String> {
        let def = &self.prog.fns[entry.index()];
        let ret = def.ret;
        let rc = self.ty.cty(ret);
        let mut s =
            String::from("int main(int argc, char **argv) {\n    gm_set_args(argc, argv);\n    gm_thread_stack();\n");
        let _ = writeln!(s, "    gm_debug = {};", self.opts.debug as u8);
        s.push_str("    gm_init_consts();\n    gm_node *cx = NULL;\n");
        let a = self.prog.ambient.clone();
        for (k, e) in [(0, a.out), (1, a.err), (2, a.env)] {
            if let Some(e) = e {
                let id = self.effect_id(&EffectRef { effect: e, args: Box::new([]) });
                let _ = writeln!(
                    s,
                    "    static gm_node amb{k}; amb{k} = (gm_node){{.eff = {id}, .kind = GM_NATIVE, .next = cx}}; cx = &amb{k};"
                );
            }
        }
        if self.is_plain(entry) {
            if ret.is_unit() || ret.is_never() {
                let _ = writeln!(s, "    {}();\n    {} r = GM_ZERO({});", self.fn_name(entry), rc, rc);
            } else {
                let _ = writeln!(s, "    {} r = {}();", rc, self.fn_name(entry));
            }
        } else {
            let _ = writeln!(s, "    {} r = {}(NULL, cx, NULL);", rc, self.fn_name(entry));
        }
        s.push_str("    if (gm_unw.kind == GM_EXIT) { gm_flush(); return gm_unw.code; }\n");
        match ret.kind() {
            TyKind::Result { err, ok } => {
                let offs = self.ty.offsets(ret);
                let d = self.ty.desc(*err);
                let _ = writeln!(s, "    if (r.b[{}]) {{", offs[0]);
                s.push_str("        gm_buf b = {0};\n        gm_buf_push(&b, \"error: \", 7);\n");
                let _ = writeln!(s, "        gm_format(&b, {}, r.b + {}, true);", d, offs[1]);
                s.push_str("        gm_buf_push(&b, \"\\n\", 1);\n        gm_flush();\n");
                s.push_str("        gm_write_err(b.data, b.len);\n        return 1;\n    }\n");
                if ok.is_integer() {
                    let _ = writeln!(
                        s,
                        "    gm_flush();\n    return (uint8_t)GM_LD({}, r.b + {});",
                        self.ty.cty(*ok),
                        offs[1]
                    );
                } else {
                    s.push_str("    gm_flush();\n    return 0;\n");
                }
            }
            _ if ret.is_integer() => s.push_str("    gm_flush();\n    return (uint8_t)r;\n"),
            _ => s.push_str("    (void)r;\n    gm_flush();\n    return 0;\n"),
        }
        s.push_str("}\n");
        Ok(s)
    }

    /// True if calls to `f` can leave by a non-local exit (§D.2).
    pub fn may_unwind(&self, f: FnId) -> bool {
        let def = &self.prog.fns[f.index()];
        match def.kind {
            FnKind::Foreign { .. } => false,
            FnKind::Clause | FnKind::Literal => true,
            FnKind::Named => !self.within_sys(&def.effects),
        }
    }

    /// True if every effect of `s` is `Sys`.
    pub fn within_sys(&self, s: &crate::types::EffectSet) -> bool {
        s.effects.iter().all(|e| Some(e.effect) == self.prog.ambient.sys)
    }

    /// Whether `f` has the plain C calling convention: a function (or non-capturing
    /// literal) whose effects are at most `{Sys}`, so it can be a C callback (§19.1).
    pub fn is_plain(&self, f: FnId) -> bool {
        let def = &self.prog.fns[f.index()];
        match def.kind {
            FnKind::Foreign { .. } => true,
            FnKind::Named | FnKind::Literal => def.parent.is_none() && self.within_sys(&def.effects),
            FnKind::Clause => false,
        }
    }

    /// The C name used to call `f`: foreign functions have a private alias.
    pub fn callee_name(&self, f: FnId) -> String {
        match self.prog.fns[f.index()].kind {
            FnKind::Foreign { .. } => format!("gm_x{}", f.0),
            _ => self.fn_name(f),
        }
    }

    /// Declares foreign function `f` under a private name bound to its symbol, so it
    /// cannot clash with the declarations of the runtime's C headers.
    pub fn foreign_decl(&mut self, f: FnId) -> CResult<()> {
        let def = &self.prog.fns[f.index()];
        let FnKind::Foreign { lib, link_name, variadic } = def.kind else { unreachable!() };
        let lib = lib.as_str().to_string();
        if lib != "c" && lib != "m" {
            self.libs.insert(lib);
        }
        let mut params = vec![];
        for p in &def.params {
            let t = def.locals[p.index()].ty;
            params.push(self.c_abi_type(t, def.span)?);
        }
        if variadic {
            params.push("...".into());
        }
        if params.is_empty() {
            params.push("void".into());
        }
        let ret = if def.ret.is_unit() { "void".into() } else { self.c_abi_type(def.ret, def.span)? };
        let _ = writeln!(
            self.protos,
            "extern {}{} gm_x{}({}) __asm__(GM_SYM(\"{}\"));",
            conv_attr(def.callconv),
            ret,
            f.0,
            params.join(", "),
            link_name
        );
        Ok(())
    }

    /// The C type passed across the C ABI for `t`.
    pub fn c_abi_type(&mut self, t: Ty, span: Span) -> CResult<String> {
        use types::Repr;
        match self.ty.repr(t) {
            Repr::Bool => Ok("_Bool".into()),
            Repr::Int { .. } | Repr::Float(_) | Repr::Ptr => Ok(self.ty.cty(t)),
            _ => match self.ty.mirror(t) {
                Some(m) => Ok(m),
                None => unsupported(span, format!("passing `{}` by value to or from C", t)),
            },
        }
    }
}

/// The C attribute (with a trailing space) selecting calling convention `cc`; the
/// target's C ABI needs none.
pub fn conv_attr(cc: CallConv) -> &'static str {
    match cc {
        CallConv::C => "",
        CallConv::Win64 => "__attribute__((ms_abi)) ",
    }
}

/// Where the C compiler and its output go.
pub struct Build {
    pub c_file: PathBuf,
    pub exe: PathBuf,
}

/// Compiles C source `code` to an executable at `exe` (the source is written next to
/// it with the extension `.c`). Returns the compiler's error output on failure.
pub fn compile(code: &str, libs: &[String], exe: &Path) -> Result<Build, String> {
    let c_file = exe.with_extension("c");
    std::fs::write(&c_file, code).map_err(|e| format!("cannot write {}: {}", c_file.display(), e))?;
    let cc = std::env::var("CC").unwrap_or_else(|_| "cc".into());
    let out = std::process::Command::new(&cc)
        .args(["-std=gnu11", "-O2", "-w", "-fwrapv", "-ffp-contract=off", "-pthread", "-o"])
        .arg(exe)
        .arg(&c_file)
        .args(libs.iter().map(|l| format!("-l{}", l)))
        .arg("-lm")
        .output()
        .map_err(|e| format!("cannot run the C compiler `{}`: {}", cc, e))?;
    if !out.status.success() {
        return Err(format!("the C compiler failed:\n{}", String::from_utf8_lossy(&out.stderr)));
    }
    Ok(Build { c_file, exe: exe.to_path_buf() })
}
