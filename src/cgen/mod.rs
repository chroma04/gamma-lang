//! The C back end: translates a checked [`Program`] to C and compiles it with the
//! system C compiler, together with the runtime (`runtime/gm_rt.c`).
//!
//! The generated code follows the interpreter's semantics exactly: values have
//! the same bytes (see [`types`]), checks trap with the same messages, and
//! `@write` formats through the runtime (`runtime/gm_rt.c`) using type
//! descriptors. The design is described in `docs/architecture.md`.

mod arith;
mod borrow;
mod constarg;
pub mod debug;
mod elementwise;
mod expr;
mod fiber;
mod func;
mod helpers;
mod rtcache;
pub use rtcache::cached_program;
mod types;
mod views;
mod wide;

use crate::layout::Layouts;
use crate::source::{SourceMap, Span};
use crate::tir::{FnId, FnKind, Program};
use crate::toolchain::Toolchain;
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
    /// `-g`: the executable carries debug information that maps it to the Gamma
    /// source (see [`debug`]).
    pub debug_info: bool,
}

/// The output of [`generate`].
pub struct Generated {
    /// The generated C code, which follows the runtime (see [`compile`]).
    pub code: String,
    /// Libraries to link (from `foreign` blocks), beyond the C library.
    pub libs: Vec<String>,
    /// The options it was generated with.
    pub opts: Options,
    /// The target it was generated for.
    pub target: crate::layout::Target,
}

/// Translates `prog` (whose entry is `entry`) to C source.
pub fn generate(prog: &Program, sources: &SourceMap, entry: FnId, opts: &Options) -> CResult<Generated> {
    generate_entries(prog, sources, &[entry], false, opts)
}

/// Translates `prog` to C source whose `main` runs one of `entries`, chosen by the
/// decimal index in its first command-line argument (`gamma test`). Each entry runs
/// exactly as the entry of [`generate`] does.
pub fn generate_tests(prog: &Program, sources: &SourceMap, entries: &[FnId], opts: &Options) -> CResult<Generated> {
    generate_entries(prog, sources, entries, true, opts)
}

fn generate_entries(
    prog: &Program,
    sources: &SourceMap,
    entries: &[FnId],
    select: bool,
    opts: &Options,
) -> CResult<Generated> {
    let target = prog.target.clone();
    // The runtime and the generated code read integers as little-endian bytes.
    if !target.little_endian {
        return unsupported(Span::DUMMY, "big-endian targets");
    }
    let lookup = |id: AdtId| prog.adts.get(&id);
    let layouts = Layouts::new(target, &lookup);
    let mut ty = Types::new(prog, &layouts);
    ty.debug_names = opts.debug_info;
    let mut g = Gen {
        prog,
        ty,
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
        frames_done: vec![false; prog.fns.len()],
        spec_keys: vec![],
        spec_ids: HashMap::new(),
        clones: std::collections::HashSet::new(),
        spec_queue: vec![],
        spec_tables: std::collections::HashSet::new(),
        exits: HashMap::new(),
        pure_memo: HashMap::new(),
        early_memo: HashMap::new(),
        captured: None,
        site_consts: vec![],
        handler_tables: String::new(),
        libs: std::collections::BTreeSet::new(),
        handler_done: vec![false; prog.handlers.len()],
        decl_done: vec![false; prog.handler_decls.len()],
        line_paths: HashMap::new(),
    };
    for &entry in entries {
        g.queue(entry);
    }
    for (_, f) in &prog.exports {
        g.queue(*f);
    }
    g.drain()?;
    let consts = g.gen_consts()?;
    let exports = g.gen_exports()?;
    let main = g.gen_main(entries, select)?;
    let mut out = String::from("/* ---- generated ---- */\n");
    let _ = writeln!(out, "GM_API int gm_checks = {};", opts.checks as u8);
    out.push_str(helpers::SUPPORT);
    out.push_str(&g.ty.blob_typedefs());
    // The Gamma names of frame fields and mirror fields (with `-g`) are declared with any
    // macro of the same name suspended.
    let names = g.ty.member_names();
    out.push_str(&debug::guard(&names, g.ty.mirror_defs()));
    out.push_str(&debug::guard(&names, g.ty.view_defs()));
    // Fil-C compiles no top-level assembly, so its executables go without the printers.
    if opts.debug_info && matches!(prog.target.os, "Linux" | "FreeBSD") && !prog.target.filc {
        out.push_str(&debug::gdb_script_section());
    }
    out.push_str(&g.ty.descriptors());
    out.push_str(&g.static_defs);
    out.push_str(&debug::guard(&names, &g.frames));
    out.push_str(&g.protos);
    out.push_str(&g.helpers.protos);
    out.push_str(&g.handler_tables);
    out.push_str(&consts);
    out.push_str(&g.helpers.bodies);
    out.push_str(&g.bodies);
    // An exported symbol must not be a name the generated program uses itself.
    if !prog.exports.is_empty() {
        let whole = format!("{}\n{}\n{}", RT_H, RT_C, out);
        let idents = c_identifiers(&whole);
        for (sym, f) in &prog.exports {
            if idents.contains(sym.as_str()) {
                return Err(Unsupported {
                    message: format!(
                        "the exported name `{}` is also a name of the compiled program's runtime; choose another",
                        sym
                    ),
                    span: prog.fns[f.index()].span,
                });
            }
        }
    }
    out.push_str(&exports);
    out.push_str(&main);
    Ok(Generated { code: out, libs: g.libs.into_iter().collect(), opts: opts.clone(), target: prog.target.clone() })
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
    /// Functions whose frame struct has been emitted.
    frames_done: Vec<bool>,
    /// The specializations interned so far, by number, and the numbers by key.
    spec_keys: Vec<func::SpecKey>,
    spec_ids: HashMap<func::SpecKey, func::Spec>,
    /// Specialized clones requested so far, and those still to generate.
    clones: std::collections::HashSet<(FnId, func::Spec)>,
    spec_queue: Vec<(FnId, func::Spec)>,
    /// Handler literals whose table of clause clones for a specialization was emitted.
    spec_tables: std::collections::HashSet<(usize, func::Spec)>,
    /// Memo of `clause_may_exit` (`None` while being computed).
    exits: HashMap<(FnId, Option<func::Spec>), Option<bool>>,
    /// Memo of `is_pure` (`None` while being computed).
    pure_memo: HashMap<FnId, Option<bool>>,
    early_memo: HashMap<FnId, Vec<bool>>,
    /// The locals that function literals or handler clauses reach through their
    /// environment, by function (computed on first use, see `Gen::frame_locals`).
    captured: Option<std::collections::HashSet<(FnId, crate::tir::LocalId)>>,
    /// Constant large arguments, in static storage `gm_kN` (see `constarg`).
    site_consts: Vec<crate::tir::ConstValue>,
    pub handler_tables: String,
    /// Libraries to link, from foreign blocks.
    pub libs: std::collections::BTreeSet<String>,
    handler_done: Vec<bool>,
    decl_done: Vec<bool>,
    /// The path a `#line` directive names for each source file (`None`: the file is not
    /// on disk, so lines in it are not mapped).
    line_paths: HashMap<crate::source::FileId, Option<String>>,
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

    /// The C name of the clone of `f` specialized by `s`.
    pub fn spec_name(&self, f: FnId, s: func::Spec) -> String {
        format!("gm_f{}_s{}", f.0, s.0)
    }

    /// Marks the clone of `f` specialized by `s` as needed; returns its C name.
    pub fn queue_spec(&mut self, f: FnId, s: func::Spec) -> String {
        if self.clones.insert((f, s)) {
            self.spec_queue.push((f, s));
        }
        self.spec_name(f, s)
    }

    /// The number of specialization `key`.
    pub fn spec(&mut self, key: func::SpecKey) -> func::Spec {
        if let Some(&s) = self.spec_ids.get(&key) {
            return s;
        }
        let s = func::Spec(self.spec_keys.len() as u32);
        self.spec_keys.push(key.clone());
        self.spec_ids.insert(key, s);
        s
    }

    pub fn spec_key(&self, s: func::Spec) -> &func::SpecKey {
        &self.spec_keys[s.0 as usize]
    }

    /// How many specializations `s` is nested in, itself included.
    pub fn spec_depth(&self, s: func::Spec) -> u32 {
        1 + self.spec_key(s).outer.map_or(0, |o| self.spec_depth(o))
    }

    /// The C name of clause `c` of a handler whose node is the context `outer` is
    /// specialized for (or of the clause itself).
    pub fn clause_name(&mut self, c: FnId, outer: Option<func::Spec>) -> String {
        match outer {
            Some(o) => self.queue_spec(c, o),
            None => {
                self.queue(c);
                self.fn_name(c)
            }
        }
    }

    /// Generates every function and clone queued so far, and those they reach.
    fn drain(&mut self) -> CResult<()> {
        loop {
            if let Some(f) = self.reachable.pop() {
                func::gen_fn(self, f, None)?;
            } else if let Some((f, s)) = self.spec_queue.pop() {
                func::gen_fn(self, f, Some(s))?;
            } else {
                return Ok(());
            }
        }
    }

    /// True if the frame struct of `f` has been emitted; marks it as emitted.
    pub fn frame_done(&mut self, f: FnId) -> bool {
        std::mem::replace(&mut self.frames_done[f.index()], true)
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

    /// A `#line` directive for the start of `span`, with debug information on and the
    /// span in a file on disk.
    pub fn line_directive(&mut self, span: Span) -> Option<String> {
        if !self.opts.debug_info || span == Span::DUMMY {
            return None;
        }
        let f = self.sources.file(span.file);
        let path = self
            .line_paths
            .entry(span.file)
            .or_insert_with(|| {
                std::fs::canonicalize(&f.path).ok().map(|p| c_string(p.display().to_string().as_bytes()))
            })
            .clone()?;
        let (line, _) = f.line_col(span.lo);
        Some(format!("#line {} {}", line, path))
    }

    /// The table of the clauses of handler literal `h` as clones specialized for `s`:
    /// the handler is installed where `s`'s node is the context, so its clauses run
    /// with that node as theirs.
    pub fn spec_handler_table(&mut self, h: usize, s: func::Spec) -> String {
        let name = format!("gm_hl{}_s{}", h, s.0);
        if self.spec_tables.insert((h, s)) {
            let clauses = self.prog.handlers[h].clauses.clone();
            let names: Vec<String> = clauses.iter().map(|c| format!("(void *){}", self.queue_spec(*c, s))).collect();
            let _ = writeln!(self.handler_tables, "static void *const {}[] = {{{}}};", name, names.join(", "));
        }
        name
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
                // `self` is passed by address when large, and `env` is that address.
                let self_arg =
                    if self.ty.by_ref(ptys[0]) { "env".to_string() } else { format!("GM_LD({}, env)", self_ty) };
                let mut args = vec!["NULL".to_string(), "cx".into(), "inst".into(), self_arg];
                for (i, t) in ptys.iter().enumerate().skip(1) {
                    params.push(format!("{} a{}", self.ty.param_cty(*t), i));
                    args.push(format!("a{}", i));
                }
                let name = format!("gm_dt{}_{}", d, k);
                // A large result goes to `gm_out`, as for every Gamma function.
                let (ret, ret_kw) = if self.ty.by_ref(def.ret) {
                    params.insert(0, "void *gm_out".into());
                    args.insert(0, "gm_out".into());
                    ("void".to_string(), "")
                } else {
                    (ret, "return ")
                };
                let _ = writeln!(self.protos, "static {} {}({});", ret, name, params.join(", "));
                let _ = writeln!(
                    self.bodies,
                    "static {} {}({}) {{ {}{}({}); }}",
                    ret,
                    name,
                    params.join(", "),
                    ret_kw,
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
        let mut values = vec![];
        for (i, c) in self.prog.consts.iter().enumerate() {
            let Some(v) = &c.value else {
                return unsupported(c.span, "a constant without a compile-time value");
            };
            values.push((format!("gm_c{i}"), v.clone()));
        }
        for (i, v) in std::mem::take(&mut self.site_consts).into_iter().enumerate() {
            values.push((format!("gm_k{i}"), v));
        }
        for (name, v) in &values {
            let (size, align) = (self.ty.size(v.ty), self.ty.align(v.ty).max(1));
            let _ = writeln!(decls, "static uint8_t {}[{}] __attribute__((aligned({})));", name, size.max(1), align);
            if !v.bytes.is_empty() {
                let s = self.static_bytes(&v.bytes);
                let _ = writeln!(init, "    memcpy({}, {}, {});", name, s, v.bytes.len());
            }
            for r in &v.relocs {
                let target = match &r.target {
                    crate::tir::StaticData::Bytes(b) => self.static_bytes(b),
                    crate::tir::StaticData::Const(k) => format!("gm_c{}", k.0),
                    crate::tir::StaticData::Fn(f) => {
                        self.queue(*f);
                        format!("(void *){}", self.addr_name(*f))
                    }
                };
                let _ = writeln!(
                    init,
                    "    {{ void *p = (uint8_t *){} + {}; memcpy({} + {}, &p, sizeof p); }}",
                    target, r.addend, name, r.offset
                );
            }
        }
        for (i, gl) in self.prog.globals.iter().enumerate() {
            let (size, align) = (self.ty.size(gl.ty), self.ty.align(gl.ty).max(gl.align.unwrap_or(1)).max(1));
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
                        format!("(void *){}", self.addr_name(*f))
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
        self.drain()?;
        init.push_str("}\n");
        Ok(format!("{}{}", decls, init))
    }

    /// Exported functions (§19.4): external C-ABI wrappers under their symbols.
    fn gen_exports(&mut self) -> CResult<String> {
        let mut s = String::new();
        for (k, (sym, f)) in self.prog.exports.clone().into_iter().enumerate() {
            let def = &self.prog.fns[f.index()];
            // A private C name bound to the symbol, so it cannot clash with declarations
            // of the C headers the runtime includes.
            let cname = format!("gm_xe{}", k);
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
            let call = format!("{}({})", self.addr_name(f), args.join(", "));
            let params = if params.is_empty() { "void".to_string() } else { params.join(", ") };
            let (r, body) = if def.ret.is_unit() {
                ("void".to_string(), format!("{};", call))
            } else {
                let r = self.c_abi_type(def.ret, def.span)?;
                let call = if self.ty.mirror(def.ret).is_some() { call } else { format!("({}){}", r, call) };
                (r, format!("return {};", call))
            };
            let cc = conv_attr(def.callconv);
            let _ = writeln!(s, "{cc}{r} {cname}({params}) __asm__(GM_SYM(\"{sym}\"));");
            let _ = writeln!(s, "{cc}{r} {cname}({params}) {{ {body} }}");
        }
        Ok(s)
    }

    /// The C `main`: installs the native ambient handlers, runs the entry function and
    /// turns its result into the exit status (as the interpreter's `run_main`).
    /// The C `main`. With `select` it runs the entry whose index is the first
    /// argument; otherwise `entries` is the one entry to run.
    fn gen_main(&mut self, entries: &[FnId], select: bool) -> CResult<String> {
        let mut s = String::new();
        let mut calls = String::new();
        for (i, &entry) in entries.iter().enumerate() {
            s.push_str(&self.gen_entry(entry, i)?);
            let _ = writeln!(calls, "        case {i}: return gm_run_main(gm_entry{i}, cx);");
        }
        s.push_str("int main(int argc, char **argv) {\n");
        if select {
            // The program sees only its own name; the index selects the entry.
            s.push_str("    if (argc < 2) { fputs(\"usage: test-binary INDEX\\n\", stderr); return 2; }\n");
            s.push_str("    long which = strtol(argv[1], NULL, 10);\n    gm_set_args(1, argv);\n");
        } else {
            s.push_str("    gm_set_args(argc, argv);\n");
        }
        s.push_str("    gm_thread_stack();\n");
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
        if select {
            let _ = writeln!(s, "    switch (which) {{\n{calls}        default: return 2;\n    }}");
        } else {
            s.push_str("    return gm_run_main(gm_entry0, cx);\n");
        }
        s.push_str("}\n");
        Ok(s)
    }

    /// `static int gm_entry<n>(gm_node *cx)`: runs `entry` under the ambient handlers
    /// `cx` and returns the process exit status.
    fn gen_entry(&mut self, entry: FnId, n: usize) -> CResult<String> {
        let def = &self.prog.fns[entry.index()];
        let ret = def.ret;
        let rc = self.ty.cty(ret);
        let mut s = format!("static int gm_entry{n}(gm_node *cx) {{\n");
        if self.ty.by_ref(ret) {
            let hidden = if self.is_plain(entry) { "" } else { ", NULL, cx, NULL" };
            let _ = writeln!(s, "    {} r; {}(&r{});", rc, self.fn_name(entry), hidden);
        } else if self.is_plain(entry) {
            if ret.is_unit() || ret.is_never() {
                let _ = writeln!(s, "    {}();\n    {} r = GM_ZERO({});", self.fn_name(entry), rc, rc);
            } else {
                let _ = writeln!(s, "    {} r = {}();", rc, self.fn_name(entry));
            }
        } else {
            let _ = writeln!(s, "    {} r = {}(NULL, cx, NULL);", rc, self.fn_name(entry));
        }
        s.push_str("    (void)cx;\n");
        s.push_str("    if (gm_unw.kind == GM_EXIT) { gm_flush(); return gm_unw.code; }\n");
        match ret.kind() {
            TyKind::Result { err, ok } => {
                let offs = self.ty.offsets(ret);
                let d = self.ty.desc(*err);
                let _ = writeln!(s, "    if (((uint8_t *)&r)[{}]) {{", offs[0]);
                s.push_str("        gm_buf b = {0};\n        gm_buf_push(&b, \"error: \", 7);\n");
                // A union's value is named with its type: `E.NotFound` (§3.7).
                if let TyKind::Adt(id, _) = err.kind()
                    && matches!(self.prog.adts.get(id).map(|d| &d.kind), Some(crate::tir::AdtKind::Union { .. }))
                {
                    let name = err.to_string();
                    let _ = writeln!(s, "        gm_buf_push(&b, {}, {});", c_string(name.as_bytes()), name.len());
                }
                let _ = writeln!(s, "        gm_format(&b, {}, (uint8_t *)&r + {}, true);", d, offs[1]);
                s.push_str("        gm_buf_push(&b, \"\\n\", 1);\n        gm_flush();\n");
                s.push_str("        gm_write_err(b.data, b.len);\n        return 1;\n    }\n");
                if ok.is_integer() {
                    let _ = writeln!(
                        s,
                        "    gm_flush();\n    return (uint8_t)GM_LD({}, (uint8_t *)&r + {});",
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

    /// Whether a call of clause `c`, as a clone specialized for `outer` when given, may
    /// return with an exit in progress. False only when that is certain: its body
    /// leaves only locally, and its calls and performs reach only code that does too.
    /// The caller then needs no test of `gm_unw` after the call.
    pub fn clause_may_exit(&mut self, c: FnId, outer: Option<func::Spec>) -> bool {
        match self.exits.get(&(c, outer)) {
            Some(Some(r)) => return *r,
            // Recursion: assume the worst.
            Some(None) => return true,
            None => {}
        }
        self.exits.insert((c, outer), None);
        let prog = self.prog;
        let r = match &prog.fns[c.index()].body {
            Some(b) => self.expr_may_exit(b, outer),
            None => true,
        };
        self.exits.insert((c, outer), Some(r));
        r
    }

    /// Whether evaluating `e` in a function whose context is the node of `spec` may
    /// leave it by a non-local exit, or return from a call with one in progress.
    fn expr_may_exit(&mut self, e: &'a crate::tir::Expr, spec: Option<func::Spec>) -> bool {
        use crate::tir::{Callee, ExprKind, Target};
        match &e.kind {
            ExprKind::Break { target: Target::InstallingWith, .. } => return true,
            ExprKind::Break { target: Target::Label { levels, .. }, .. }
            | ExprKind::Continue { target: Target::Label { levels, .. } }
            | ExprKind::Return { levels, .. }
            | ExprKind::Try { levels, .. }
                if *levels > 0 =>
            {
                return true;
            }
            // Handlers change the context, and fibers and continuations exit through the
            // runtime.
            ExprKind::With { .. } | ExprKind::Resume { .. } | ExprKind::Discard(_) => return true,
            ExprKind::Builtin { op: crate::tir::Builtin::Fiber, .. } => return true,
            // The sink is a perform or a function value.
            ExprKind::Write { .. } => return true,
            ExprKind::Call { callee, .. } => {
                let may = match callee {
                    Callee::Fn(f) => self.may_unwind(*f),
                    Callee::Indirect(_) => true,
                    Callee::Perform { effect, op } => match spec {
                        Some(s) if !self.prog.ctl_ops.contains(&(effect.effect, *op)) => {
                            let key = self.spec_key(s).clone();
                            match self.prog.handlers[key.handler as usize].clauses.get(*op as usize) {
                                Some(&c) if self.effect_id(effect) == key.effect => self.clause_may_exit(c, key.outer),
                                _ => true,
                            }
                        }
                        _ => true,
                    },
                };
                if may {
                    return true;
                }
            }
            _ => {}
        }
        let mut may = false;
        e.for_each_child(&mut |c| {
            if !may {
                may = self.expr_may_exit(c, spec);
            }
        });
        may
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

    /// Whether plain function `f` needs a separate C-ABI entry point: its own signature
    /// passes large values by address or returns a large value through `gm_out` (see
    /// `Types::by_ref`), so C code and plain function pointers call a wrapper.
    pub fn needs_wrapper(&mut self, f: FnId) -> bool {
        let def = &self.prog.fns[f.index()];
        if matches!(def.kind, FnKind::Foreign { .. }) || !self.is_plain(f) {
            return false;
        }
        let ret = def.ret;
        let params: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
        params.iter().any(|t| self.ty.by_ref(*t)) || self.ty.by_ref(ret)
    }

    /// The C name of `f` where its address is taken: the C-ABI entry point of a plain
    /// function (see `needs_wrapper`), else the name it is called by.
    pub fn addr_name(&mut self, f: FnId) -> String {
        if self.needs_wrapper(f) { format!("gm_f{}_c", f.0) } else { self.callee_name(f) }
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
        let ret = if def.ret.is_never() {
            "__attribute__((noreturn)) void".into()
        } else if def.ret.is_unit() {
            "void".into()
        } else {
            self.c_abi_type(def.ret, def.span)?
        };
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

/// The C compiler flags every build uses.
///
/// Gamma memory is untyped bytes (allocators hand out raw memory that is then used as any
/// type, and values are blobs written and read piecewise with memcpy), so C's type-based
/// alias analysis does not hold: `-fno-strict-aliasing`. Without it gcc 13 at -O2 drops
/// some of those writes (a slice length written into a temporary was lost).
const BASE_CFLAGS: [&str; 6] = ["-std=gnu11", "-w", "-fwrapv", "-fno-strict-aliasing", "-ffp-contract=off", "-pthread"];

/// Compiles generated C to an executable at `exe` with `toolchain` (the source is
/// written next to it with the extension `.c`). Returns the compiler's error output
/// on failure.
///
/// Checked and Fast builds compile the runtime and the generated code as one translation
/// unit at `-O2`, so the optimizer sees the whole program. Debug builds favor turnaround:
/// they are compiled at `-O1` (`-O0` with debug information, so every variable and line is
/// there in the debugger) and linked with a runtime object compiled once and cached (see
/// [`rtcache`]); without a usable cache they fall back to one translation unit. Safe builds
/// (Fil-C, which compiles slowly) are compiled at `-O2` but link the cached runtime too.
pub fn compile(generated: &Generated, toolchain: &Toolchain, exe: &Path) -> Result<Build, String> {
    let c_file = exe.with_extension("c");
    let opts = &generated.opts;
    let opt = match (opts.debug, opts.debug_info) {
        (true, true) => "-O0",
        (true, false) => "-O1",
        (false, _) => "-O2",
    };
    let separate = opts.debug || generated.target.filc;
    let runtime = if separate { rtcache::runtime_object(toolchain, &rt_flags(opts, &generated.target)) } else { None };
    let mut code = String::new();
    if runtime.is_some() {
        code.push_str("#define GM_RT_SEPARATE 1\n");
        code.push_str(RT_H);
    } else {
        code.push_str(RT_H);
        code.push('\n');
        code.push_str(RT_C);
    }
    code.push('\n');
    code.push_str(&generated.code);
    if opts.debug_info {
        // The debugger finds the C file by this name, so it is absolute when possible.
        let dir = c_file.parent().filter(|d| !d.as_os_str().is_empty()).unwrap_or(Path::new("."));
        let abs = std::fs::canonicalize(dir).map_or(c_file.clone(), |d| d.join(c_file.file_name().unwrap_or_default()));
        code = debug::resolve_lines(&code, &abs.display().to_string());
    }
    std::fs::write(&c_file, code.as_bytes()).map_err(|e| format!("cannot write {}: {}", c_file.display(), e))?;
    // `CFLAGS` come after these options, so they can override them; `LDFLAGS` come
    // after the source and the runtime object, before the libraries, as for `make`.
    let mut flags = BASE_CFLAGS.to_vec();
    flags.push(opt);
    flags.extend(opts.debug_info.then_some("-g"));
    flags.extend(f16c_flag(&generated.target));
    let out = toolchain
        .command(&flags)
        .arg("-o")
        .arg(exe)
        .arg(&c_file)
        .args(&runtime)
        .args(&toolchain.ldflags)
        .args(generated.libs.iter().map(|l| format!("-l{}", l)))
        .arg("-lm")
        .output()
        .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
    if !out.status.success() {
        return Err(format!("the C compiler failed:\n{}", String::from_utf8_lossy(&out.stderr)));
    }
    Ok(Build { c_file, exe: exe.to_path_buf() })
}

/// The flags the separately compiled runtime object of a build with `opts` is compiled
/// with. It is optimized whatever the build's optimization: it is compiled only once.
fn rt_flags(opts: &Options, target: &crate::layout::Target) -> Vec<String> {
    let mut flags: Vec<String> = BASE_CFLAGS.iter().map(|f| f.to_string()).collect();
    flags.push("-O2".into());
    flags.extend(f16c_flag(target).map(String::from));
    if opts.debug_info {
        flags.push("-g".into());
    }
    flags
}

/// The option that has Fil-C convert between float and f16 with the F16C instructions,
/// which it gets right, rather than with routines it miscompiles (see `GM_F16` in the
/// runtime). Every x86-64 processor of the last decade has them.
fn f16c_flag(target: &crate::layout::Target) -> Option<&'static str> {
    (target.filc && target.arch == "X86_64").then_some("-mf16c")
}

/// The source of the runtime compiled on its own (`GM_RT_SEPARATE`).
fn separate_runtime_source() -> String {
    format!("#define GM_RT_SEPARATE 1\n{}\n{}", RT_H, RT_C)
}

/// The identifiers of C source `code`, outside comments and string and character
/// literals.
fn c_identifiers(code: &str) -> std::collections::HashSet<&str> {
    let b = code.as_bytes();
    let mut out = std::collections::HashSet::new();
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'"' | b'\'' => {
                let q = b[i];
                i += 1;
                while i < b.len() && b[i] != q {
                    i += if b[i] == b'\\' { 2 } else { 1 };
                }
                i += 1;
            }
            b'/' if b.get(i + 1) == Some(&b'*') => {
                i += 2;
                while i + 1 < b.len() && !(b[i] == b'*' && b[i + 1] == b'/') {
                    i += 1;
                }
                i += 2;
            }
            b'/' if b.get(i + 1) == Some(&b'/') => {
                while i < b.len() && b[i] != b'\n' {
                    i += 1;
                }
            }
            c if c.is_ascii_alphabetic() || c == b'_' => {
                let start = i;
                while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_') {
                    i += 1;
                }
                out.insert(&code[start..i]);
            }
            c if c.is_ascii_digit() => {
                while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_' || b[i] == b'.') {
                    i += 1;
                }
            }
            _ => i += 1,
        }
    }
    out
}
