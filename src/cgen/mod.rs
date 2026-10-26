//! The C back end: translates a checked [`Program`] to C and compiles it with the
//! system C compiler, together with the runtime (`runtime/gm_rt.c`).
//!
//! The generated code follows the interpreter's semantics exactly: values have
//! the same bytes (see [`types`]), checks trap with the same messages, and
//! `@write` formats through the runtime (`runtime/gm_rt.c`) using type
//! descriptors. The design is described in `docs/architecture.md`.

mod arith;
mod borrow;
mod chain;
mod constarg;
mod csrc;
pub use csrc::{CSource, Extras, Link, link_inputs, load_for_interpreter};
pub mod debug;
mod expr;
mod fiber;
mod func;
mod helpers;
mod outline;
mod rtcache;
pub use rtcache::{cache_dir, cached_program, private_dir_builder, runtime_object_from};
mod types;
mod units;
mod vector;
mod views;
mod wasi;

use crate::layout::Layouts;
use crate::source::{SourceMap, Span};
use crate::tir::{FnId, FnKind, Program};
use crate::toolchain::Toolchain;
use crate::types::{AdtId, CallConv, EffectRef, Ty, TyKind};
use std::collections::HashMap;
use std::fmt::Write;
use std::path::{Path, PathBuf};
use types::Types;
pub(crate) use types::c_string;

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
    /// Debug, Checked or Safe: checked operations trap (§1.4). Fast drops the checks.
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
    /// The same code by role, for a build split into several units (see [`units`]).
    parts: units::Parts,
    /// Libraries to link (from `foreign` blocks), beyond the C library.
    pub libs: Vec<String>,
    /// The options it was generated with.
    pub opts: Options,
    /// The target it was generated for.
    pub target: crate::layout::Target,
    /// The program is for a freestanding target (§3.7.1): the code is compiled with the
    /// runtime into one object, and has no `main`.
    pub freestanding: bool,
    /// A freestanding program with a `#trap_hook`.
    pub trap_hook: bool,
}

/// Translates `prog` (whose entry is `entry`) to C source.
pub fn generate(prog: &Program, sources: &SourceMap, entry: FnId, opts: &Options) -> CResult<Generated> {
    generate_entries(prog, sources, &[entry], false, opts)
}

/// Translates a program for a freestanding target to C source: no `main`, and the program's
/// exported functions as its entry points (§3.7.1).
pub fn generate_object(prog: &Program, sources: &SourceMap, opts: &Options) -> CResult<Generated> {
    generate_entries(prog, sources, &[], false, opts)
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
    let lookup = |id: AdtId| prog.adts.get(&id);
    let has_f16 = target.f16;
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
        steady: None,
        captured: None,
        site_consts: vec![],
        handler_tables: String::new(),
        libs: std::collections::BTreeSet::new(),
        handler_done: vec![false; prog.handlers.len()],
        decl_done: vec![false; prog.handler_decls.len()],
        line_paths: HashMap::new(),
        loc_files: Default::default(),
        stubs: HashMap::new(),
    };
    let freestanding = prog.target.os == "Freestanding";
    for &entry in entries {
        g.queue(entry);
    }
    for (_, f) in &prog.exports {
        g.queue(*f);
    }
    if let Some(h) = prog.trap_hook {
        g.queue(h);
    }
    g.drain()?;
    let (const_decls, const_init) = g.gen_consts()?;
    let mut exports = g.gen_exports()?;
    if freestanding {
        exports.insert_str(0, &g.gen_start());
        exports.push_str(&g.gen_trap_hook()?);
    }
    let main = if freestanding { String::new() } else { g.gen_main(entries, select)? };
    let mut parts = units::Parts::default();
    let checks = format!("GM_API int gm_checks = {};\n", opts.checks as u8);
    parts.head.push_str(helpers::SUPPORT);
    parts.head.push_str(&g.ty.blob_typedefs());
    // The Gamma names of frame fields and mirror fields (with `-g`) are declared with any
    // macro of the same name suspended.
    let names = g.ty.member_names();
    parts.head.push_str(&debug::guard(&names, g.ty.mirror_defs()));
    parts.head.push_str(&debug::guard(&names, g.ty.view_defs()));
    // Fil-C compiles no top-level assembly, so its executables go without the printers.
    if opts.debug_info && matches!(prog.target.os, "Linux" | "FreeBSD") && !prog.target.filc {
        parts.once.push_str(&debug::gdb_script_section());
    }
    let mut data = g.ty.descriptors();
    for &id in g.loc_files.borrow().iter() {
        let path = g.sources.file(crate::source::FileId(id)).path.display().to_string();
        let _ = writeln!(data, "static const char gm_src{}[] = {};", id, c_string(path.as_bytes()));
    }
    data.push_str(&g.static_defs);
    let frames = debug::guard(&names, &g.frames);
    let protos = format!("{}{}", g.protos, g.helpers.protos);
    // One unit has the parts in this order.
    let mut out = String::from("/* ---- generated ---- */\n");
    out.push_str(&checks);
    out.push_str(&parts.head);
    out.push_str(&parts.once);
    out.push_str(&data);
    out.push_str(&frames);
    out.push_str(&protos);
    out.push_str(&g.handler_tables);
    out.push_str(&const_decls);
    out.push_str(&const_init);
    out.push_str(&g.helpers.bodies);
    out.push_str(&g.bodies);
    if !has_f16 && out.contains("_Float16") {
        return unsupported(
            Span::DUMMY,
            "`f16` needs a C compiler with `_Float16`, which this target's compiler lacks",
        );
    }
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
    if freestanding && out.contains("gm_with_go(") && !matches!(prog.target.arch, "X86_64" | "Aarch64" | "S390x") {
        return Err(Unsupported {
            message: format!(
                "fibers (`with … on`) need a context switch, which the runtime has only for x86-64, AArch64 and s390x; this freestanding target ({}) has none",
                prog.target.arch
            ),
            span: Span::DUMMY,
        });
    }
    // A program that switches fibers runs its entry under the runtime's fiber support
    // (which, on WASI, is code that only such a program should carry).
    let run_main = if out.contains("gm_with_go(") { "gm_run_main_fibers" } else { "gm_run_main" };
    let main = if main.is_empty() { main } else { format!("#define GM_RUN_MAIN {run_main}\n{main}") };
    out.push_str(&exports);
    out.push_str(&main);
    parts.once.insert_str(0, &checks);
    parts.head.push_str(&frames);
    parts.protos = protos;
    parts.data = format!("{}{}{}", data, g.handler_tables, const_decls);
    parts.funcs = format!("{}{}", g.helpers.bodies, g.bodies);
    parts.tail = format!("{}{}{}", const_init, exports, main);
    Ok(Generated {
        code: out,
        parts,
        libs: g.libs.into_iter().collect(),
        opts: opts.clone(),
        target: prog.target.clone(),
        freestanding,
        trap_hook: freestanding && prog.trap_hook.is_some(),
    })
}

pub struct Gen<'a> {
    pub prog: &'a Program,
    pub ty: Types<'a>,
    pub sources: &'a SourceMap,
    pub opts: Options,
    effect_ids: HashMap<EffectRef, u32>,
    /// The shared functions that perform an ambient output effect, by signature.
    pub stubs: HashMap<String, String>,
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
    /// Per function, the parameters whose caller's value no call site lets change (see
    /// `borrow`); computed once for the whole program.
    steady: Option<Vec<Vec<bool>>>,
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
    /// The source files trap locations name; each path is one static string `gm_srcN`
    /// (N = the file id) rather than a literal repeated at every trap site.
    loc_files: std::cell::RefCell<std::collections::BTreeSet<u32>>,
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
        self.loc_files.borrow_mut().insert(span.file.0);
        format!("gm_src{}, {}, {}", span.file.0, line, col)
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
                // Named with `/`, as source files are everywhere.
                crate::source::canonicalize(&f.path)
                    .ok()
                    .map(|p| c_string(p.display().to_string().replace('\\', "/").as_bytes()))
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
    fn gen_consts(&mut self) -> CResult<(String, String)> {
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
            // `errno` is the C library's macro for the calling thread's variable, not a
            // symbol to link with (it is thread-local or a function call).
            if gl.link_name.is_some_and(|l| l.as_str() == "errno") {
                let _ = writeln!(decls, "#define gm_g{} ((uint8_t *)&errno)", i);
                continue;
            }
            if let Some(link) = gl.link_name {
                let _ = writeln!(
                    decls,
                    "extern uint8_t gm_g{}[] __asm__(GM_SYM({}));",
                    i,
                    c_string(link.as_str().as_bytes())
                );
                continue;
            }
            let tls = if gl.thread_local { "GM_TLS " } else { "" };
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
                    "static GM_TLS uint8_t gm_gi{i};\nstatic void gm_ginit{i}(void);\nstatic inline uint8_t *gm_ga{i}(void) {{ if (!gm_gi{i}) {{ gm_gi{i} = 1; gm_ginit{i}(); }} return gm_g{i}; }}"
                );
                let _ = writeln!(self.bodies, "static void gm_ginit{i}(void) {{\n{tls_init}}}");
            }
        }
        // Constants may refer to functions not otherwise reached.
        self.drain()?;
        init.push_str("}\n");
        Ok((decls, init))
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
            let start = if self.ty.lay.target.os == "Freestanding" { "gm_start(); " } else { "" };
            let (r, body) = if def.ret.is_unit() {
                ("void".to_string(), format!("{start}{};", call))
            } else {
                let r = self.c_abi_type(def.ret, def.span)?;
                let call = if self.ty.mirror(def.ret).is_some() { call } else { format!("({}){}", r, call) };
                (r, format!("{start}return {};", call))
            };
            let cc = conv_attr(def.callconv);
            let _ = writeln!(s, "{cc}{r} {cname}({params}) __asm__(GM_SYM(\"{sym}\"));");
            let _ = writeln!(s, "{cc}{r} {cname}({params}) {{ {body} }}");
        }
        Ok(s)
    }

    /// `gm_start`, which every export of a freestanding program calls first: the program has
    /// no `main` to prepare its constants and the build mode before the first call.
    fn gen_start(&self) -> String {
        format!(
            "static void gm_start(void) {{\n    static int started;\n    if (started) return;\n    started = 1;\n    gm_debug = {};\n    gm_init_consts();\n}}\n",
            self.opts.debug as u8
        )
    }

    /// `gm_trap_hook_call`, which the runtime calls on a trap in a freestanding program with a
    /// `#trap_hook` (§3.7.1): it builds the message and the `SourceLocation` and calls the hook.
    fn gen_trap_hook(&mut self) -> CResult<String> {
        let Some(hook) = self.prog.trap_hook else { return Ok(String::new()) };
        let def = &self.prog.fns[hook.index()];
        let loc_ty = def.locals[def.params[1].index()].ty;
        let msg_ty = def.locals[def.params[0].index()].ty;
        let p = self.ty.lay.target.ptr_bytes;
        let (msg_c, loc_size, loc_align) = (self.ty.cty(msg_ty), self.ty.size(loc_ty), self.ty.align(loc_ty).max(1));
        let offs = self.ty.offsets(loc_ty);
        let loc_arg =
            if self.ty.by_ref(loc_ty) { "loc".to_string() } else { format!("GM_LD({}, loc)", self.ty.cty(loc_ty)) };
        let name = self.callee_name(hook);
        let mut s = String::new();
        s.push_str(
            "static _Noreturn void gm_trap_hook_call(const uint8_t *msg, size_t len, const char *file, uint32_t line, uint32_t col) {\n",
        );
        let _ = writeln!(s, "    {msg_c} m;\n    memset(&m, 0, sizeof m);");
        let _ = writeln!(
            s,
            "    {{ const void *mp = msg; uintptr_t ml = len; memcpy((uint8_t *)&m, &mp, sizeof mp); memcpy((uint8_t *)&m + {p}, &ml, sizeof ml); }}"
        );
        let _ = writeln!(
            s,
            "    uint8_t loc[{}] __attribute__((aligned({loc_align})));\n    memset(loc, 0, sizeof loc);",
            loc_size.max(1)
        );
        let _ = writeln!(
            s,
            "    if (file != NULL) {{ const void *fp = file; uintptr_t fl = strlen(file); memcpy(loc + {}, &fp, sizeof fp); memcpy(loc + {} + {p}, &fl, sizeof fl); }}",
            offs[0], offs[0]
        );
        let _ =
            writeln!(s, "    GM_ST(uint32_t, loc + {}, line);\n    GM_ST(uint32_t, loc + {}, col);", offs[1], offs[2]);
        let _ = writeln!(s, "    {name}(m, {loc_arg});\n    __builtin_trap();\n}}");
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
            let _ = writeln!(calls, "        case {i}: return GM_RUN_MAIN(gm_entry{i}, cx);");
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
        // A program instrumented for coverage keeps a counter per line (`crate::coverage`).
        let counters = if select { crate::coverage::counters(self.prog) } else { 0 };
        if counters > 0 {
            // The file to write the counters to is the second argument.
            let _ = writeln!(s, "    gm_cover_init({counters}, argc > 2 ? argv[2] : NULL);");
        }
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
            s.push_str("    return GM_RUN_MAIN(gm_entry0, cx);\n");
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
            ExprKind::With { .. } | ExprKind::Resume { .. } | ExprKind::Cancel(_) => return true,
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

    /// True if calls to `f` can leave by a non-local exit (Appendix D.2). A function
    /// that performs nothing itself can still leave that way through a second-class
    /// function it is passed, which may perform the caller's effects (§11.6).
    pub fn may_unwind(&self, f: FnId) -> bool {
        let def = &self.prog.fns[f.index()];
        match def.kind {
            FnKind::Foreign { .. } => false,
            FnKind::Clause | FnKind::Literal => true,
            FnKind::Named => {
                !self.within_sys(&def.effects) || takes_fn(def.params.iter().map(|p| def.locals[p.index()].ty))
            }
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
            "extern {}{} gm_x{}({}) __asm__(GM_SYM({}));",
            conv_attr(def.callconv),
            ret,
            f.0,
            params.join(", "),
            c_string(link_name.as_str().as_bytes())
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

/// True if one of `params` is a second-class function (§11.6), through which a callee
/// that performs nothing itself may still leave by a non-local exit.
pub fn takes_fn(params: impl IntoIterator<Item = Ty>) -> bool {
    params.into_iter().any(|t| matches!(t.kind(), TyKind::Fn(_)))
}

/// The C attribute (with a trailing space) selecting calling convention `cc`; the
/// target's C ABI needs none.
pub fn conv_attr(cc: CallConv) -> &'static str {
    match cc {
        CallConv::C => "",
        CallConv::Win64 => "__attribute__((ms_abi)) ",
    }
}

/// Text every generated C file holds near its start (the first line of the runtime's
/// header, which each translation unit includes).
const GENERATED_MARK: &str = "The runtime of programs compiled by the Gamma C back end.";

/// Writes generated C to `path`, which is named after the output and next to it. A file
/// there that Gamma did not generate (the user's own `hello.c` beside `hello.gm`) is never
/// overwritten.
pub(crate) fn write_generated_c(path: &Path, code: &str) -> Result<(), String> {
    use std::io::Read;
    if let Ok(f) = std::fs::File::open(path) {
        let mut head = Vec::new();
        let _ = f.take(4096).read_to_end(&mut head);
        if !head.is_empty() && !String::from_utf8_lossy(&head).contains(GENERATED_MARK) {
            return Err(format!(
                "{} exists and was not generated by gamma, so it is not overwritten; choose another output name with `-o`",
                path.display()
            ));
        }
    }
    std::fs::write(path, code.as_bytes()).map_err(|e| format!("cannot write {}: {}", path.display(), e))
}

/// A temporary file next to `out` for an intermediate step of a build: its name extends
/// `out`'s (it never replaces an extension, so it cannot be the user's `prog.o`), is
/// unique to this process, and ends in `ext` for the C compiler's sake.
pub(crate) fn scratch_path(out: &Path, what: &str, ext: &str) -> PathBuf {
    let mut name = out.file_name().unwrap_or_default().to_os_string();
    name.push(format!(".{}.tmp-{}.{}", what, std::process::id(), ext));
    out.with_file_name(name)
}

/// Removes its files when dropped, so no error path leaves intermediate files behind.
#[cfg_attr(target_family = "wasm", allow(dead_code))]
pub(crate) struct Scratch(pub Vec<PathBuf>);

impl Drop for Scratch {
    fn drop(&mut self) {
        for p in &self.0 {
            let _ = std::fs::remove_file(p);
        }
    }
}

/// Where the C compiler and its output go.
pub struct Build {
    /// The C file of the program (of its first unit when it was split).
    pub c_file: PathBuf,
    /// The C files of all its units.
    pub c_files: Vec<PathBuf>,
    pub exe: PathBuf,
}

/// The C compiler flags every build uses.
///
/// Gamma memory is untyped bytes (allocators hand out raw memory that is then used as any
/// type, and values are blobs written and read piecewise with memcpy), so C's type-based
/// alias analysis does not hold: `-fno-strict-aliasing`. Without it gcc 13 at -O2 drops
/// some of those writes (a slice length written into a temporary was lost).
///
/// `-w` hides all warnings, but gcc still runs its access-warning pass unless some such
/// warning is switched off by name, and the pass takes time that grows much faster than
/// the length of a very long function: `-Wno-free-nonheap-object` stops it.
const BASE_CFLAGS: [&str; 6] =
    ["-std=gnu11", "-w", "-Wno-free-nonheap-object", "-fwrapv", "-fno-strict-aliasing", "-ffp-contract=off"];

/// The flags every build uses on `target`: [`BASE_CFLAGS`], and `-pthread` except on Windows,
/// where the runtime uses the Win32 API and programs need no thread library, and on WASI,
/// which has no threads.
pub(crate) fn base_cflags(target: &crate::layout::Target) -> Vec<&'static str> {
    let mut flags = BASE_CFLAGS.to_vec();
    if target.os != "Windows" && target.os != "Wasi" {
        flags.push("-pthread");
    }
    flags
}

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
/// A large Debug build is split into several units compiled in
/// parallel (see [`units`]); unit `i` > 0 is written to `exe` with the extension `.i.c`.
///
/// A WASI module is always compiled to objects first and linked without optimization
/// options, and then goes through [`wasi::asyncify`].
pub fn compile(generated: &Generated, toolchain: &Toolchain, link: &Link, exe: &Path) -> Result<Build, String> {
    if generated.freestanding {
        return compile_object(generated, toolchain, exe);
    }
    let c_file = exe.with_extension("c");
    let opts = &generated.opts;
    let opt = match (opts.debug, opts.debug_info) {
        (true, true) => "-O0",
        (true, false) => "-O1",
        (false, _) => "-O2",
    };
    let separate = opts.debug || generated.target.filc;
    let runtime = if separate { rtcache::runtime_object(toolchain, &rt_flags(opts, &generated.target)) } else { None };
    let mut sources = vec![];
    if runtime.is_some() {
        let prelude = format!("#define GM_RT_SEPARATE 1\n{}\n", RT_H);
        // Safe builds are optimized, and keep every function in reach of inlining.
        let n = if opts.debug { units::count(&generated.parts) } else { 1 };
        match units::split(&generated.parts, n).filter(|_| n > 1) {
            Some(units) => sources.extend(units.into_iter().map(|u| format!("{prelude}{u}"))),
            None if n > 1 && units::forced().is_some() => {
                return Err("the generated C cannot be split into units".into());
            }
            None => sources.push(format!("{prelude}{}", generated.code)),
        }
    } else {
        sources.push(format!("{}\n{}\n{}", RT_H, RT_C, generated.code));
    }
    let c_files: Vec<PathBuf> = (0..sources.len()).map(|i| units::unit_path(exe, i)).collect();
    for (code, path) in sources.iter_mut().zip(&c_files) {
        if opts.debug_info {
            // The debugger finds the C file by this name, so it is absolute when possible.
            let dir = path.parent().filter(|d| !d.as_os_str().is_empty()).unwrap_or(Path::new("."));
            let abs =
                crate::source::canonicalize(dir).map_or(path.clone(), |d| d.join(path.file_name().unwrap_or_default()));
            *code = debug::resolve_lines(code, &abs.display().to_string());
        }
        write_generated_c(path, code)?;
    }
    // `CFLAGS` come after these options, so they can override them; `LDFLAGS` come
    // after the source and the runtime object, before the libraries, as for `make`.
    let mut flags = base_cflags(&generated.target);
    flags.push(opt);
    flags.extend(opts.debug_info.then_some("-g"));
    flags.extend(f16c_flag(&generated.target));
    let run = |cmd: &mut std::process::Command| {
        cmd.stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::piped())
            .spawn()
            .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))
    };
    let failed = |out: std::process::Output| {
        (!out.status.success()).then(|| format!("the C compiler failed:\n{}", String::from_utf8_lossy(&out.stderr)))
    };
    let wasi = generated.target.os == "Wasi";
    // Several units are compiled to objects in parallel, then linked.
    let inputs = if c_files.len() > 1 || wasi {
        let objects: Vec<PathBuf> = (0..c_files.len()).map(|i| scratch_path(exe, &i.to_string(), "o")).collect();
        let mut unit_flags = flags.clone();
        // A call to a function without a declaration would compile, wrongly, under C's
        // old rules: make it an error.
        unit_flags.push("-Werror=implicit-function-declaration");
        let mut children = vec![];
        for (c, o) in c_files.iter().zip(&objects) {
            children.push(run(toolchain.command(&unit_flags).arg("-c").arg("-o").arg(o).arg(c)));
        }
        let mut error = None;
        for child in children {
            let out = child.and_then(|c| c.wait_with_output().map_err(|e| format!("the C compiler failed: {}", e)));
            error = error.or(match out {
                Ok(out) => failed(out),
                Err(e) => Some(e),
            });
        }
        if let Some(e) = error {
            objects.iter().for_each(|o| drop(std::fs::remove_file(o)));
            return Err(e);
        }
        objects
    } else {
        c_files.clone()
    };
    let link_flags: Vec<&str> =
        if wasi { flags.iter().copied().filter(|&f| f != opt).collect() } else { flags.clone() };
    let out = run(toolchain
        .command(&link_flags)
        .args(if wasi { wasi::link_flags() } else { vec![] })
        .arg("-o")
        .arg(exe)
        .args(&inputs)
        .args(&runtime)
        .args(&link.objects)
        .args(&toolchain.ldflags)
        .args(link.library_args(&generated.libs))
        .arg("-lm")
        .args(windows_libs(&generated.target)))
    .and_then(|c| c.wait_with_output().map_err(|e| format!("the C compiler failed: {}", e)));
    if inputs != c_files {
        inputs.iter().for_each(|o| drop(std::fs::remove_file(o)));
    }
    if let Some(e) = failed(out?) {
        return Err(e);
    }
    if wasi {
        wasi::asyncify(exe, opt, opts.debug_info)?;
    }
    Ok(Build { c_file, c_files, exe: exe.to_path_buf() })
}

/// Compiles the generated C of a freestanding program (§3.7.1) and the runtime, as one
/// translation unit, to the object file `obj` (the source is written next to it with the
/// extension `.c`). The object holds the program's exported functions and the runtime support
/// they use; it is linked by the target's own toolchain, and has no `main`.
///
/// The runtime is compiled with the program, never as a separate cached object: it is small
/// without the operating system parts, and the hook of a trap is code of the program.
fn compile_object(generated: &Generated, toolchain: &Toolchain, obj: &Path) -> Result<Build, String> {
    let c_file = obj.with_extension("c");
    let opts = &generated.opts;
    let opt = match (opts.debug, opts.debug_info) {
        (true, true) => "-O0",
        (true, false) => "-O1",
        (false, _) => "-O2",
    };
    let prelude =
        format!("#define GM_FREESTANDING 1\n{}", if generated.trap_hook { "#define GM_TRAP_HOOK 1\n" } else { "" });
    let mut code = format!("{prelude}{}\n{}\n{}", RT_H, RT_C, generated.code);
    if opts.debug_info {
        // The debugger finds the C file by this name, so it is absolute when possible.
        let dir = c_file.parent().filter(|d| !d.as_os_str().is_empty()).unwrap_or(Path::new("."));
        let abs =
            crate::source::canonicalize(dir).map_or(c_file.clone(), |d| d.join(c_file.file_name().unwrap_or_default()));
        code = debug::resolve_lines(&code, &abs.display().to_string());
    }
    write_generated_c(&c_file, &code)?;
    let mut flags: Vec<&str> = BASE_CFLAGS.to_vec();
    flags.extend(["-ffreestanding", opt]);
    flags.extend(opts.debug_info.then_some("-g"));
    let out = toolchain
        .command(&flags)
        .arg("-c")
        .arg("-o")
        .arg(obj)
        .arg(&c_file)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::piped())
        .output()
        .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
    if !out.status.success() {
        return Err(format!("the C compiler failed:\n{}", String::from_utf8_lossy(&out.stderr)));
    }
    Ok(Build { c_file: c_file.clone(), c_files: vec![c_file], exe: obj.to_path_buf() })
}

/// The libraries (and linking mode) a program for Windows needs: Winsock, the synchronization
/// API behind the futex, `CommandLineToArgvW` and the random number generator. Linking
/// statically keeps the executable free of the compiler's runtime DLLs (`libwinpthread`,
/// `libgcc_s`), which would otherwise have to be on `PATH` wherever it runs.
pub fn windows_libs(target: &crate::layout::Target) -> Vec<&'static str> {
    if target.os == "Windows" {
        vec!["-static", "-lws2_32", "-lsynchronization", "-lshell32", "-lbcrypt"]
    } else {
        vec![]
    }
}

/// The flags the separately compiled runtime object of a build with `opts` is compiled
/// with. It is optimized whatever the build's optimization: it is compiled only once.
pub(crate) fn rt_flags(opts: &Options, target: &crate::layout::Target) -> Vec<String> {
    let mut flags: Vec<String> = base_cflags(target).iter().map(|f| f.to_string()).collect();
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
pub(crate) fn separate_runtime_source() -> String {
    format!("#define GM_RT_SEPARATE 1\n{}\n{}", RT_H, RT_C)
}

/// The identifiers of C source `code`, outside comments and string and character
/// literals.
/// Whether `sym` is a name of the compiled program's runtime (either back end's) or of the
/// kind generated code uses (`gm_` and a kind and number): a foreign symbol or an export of
/// that name would clash with it.
pub fn is_runtime_symbol(sym: &str) -> bool {
    static NAMES: std::sync::OnceLock<std::collections::HashSet<String>> = std::sync::OnceLock::new();
    let Some(rest) = sym.strip_prefix("gm_") else { return false };
    if rest.ends_with(|c: char| c.is_ascii_digit()) {
        return true;
    }
    NAMES
        .get_or_init(|| {
            #[cfg(not(target_family = "wasm"))]
            let all = format!("{}\n{}\n{}", RT_H, RT_C, crate::clif::runtime_source());
            #[cfg(target_family = "wasm")]
            let all = format!("{}\n{}", RT_H, RT_C);
            c_identifiers(&all).into_iter().filter(|n| n.starts_with("gm_")).map(str::to_string).collect()
        })
        .contains(sym)
}

pub(crate) fn c_identifiers(code: &str) -> std::collections::HashSet<&str> {
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

#[cfg(all(test, not(target_family = "wasm")))]
mod runtime_name_tests {
    /// The self-hosted compiler keeps its own sorted list of the runtime's `gm_` names that
    /// do not end in a digit (`compiler/sema/runtime_names.gm`); it must match the runtime.
    #[test]
    fn gammac_runtime_names_match_the_runtime() {
        let all = format!("{}\n{}\n{}", super::RT_H, super::RT_C, crate::clif::runtime_source());
        let mut want: Vec<&str> = super::c_identifiers(&all)
            .into_iter()
            .filter(|n| n.starts_with("gm_") && !n.ends_with(|c: char| c.is_ascii_digit()))
            .collect();
        want.sort_unstable();
        let path = concat!(env!("CARGO_MANIFEST_DIR"), "/compiler/sema/runtime_names.gm");
        let text = std::fs::read_to_string(path).unwrap();
        let list = &text[text.find("RUNTIME_NAMES ::").unwrap()..];
        let list = &list[..list.find("\n)").unwrap()];
        let got: Vec<&str> = list.split('"').skip(1).step_by(2).collect();
        assert_eq!(got, want, "regenerate compiler/sema/runtime_names.gm from the runtime");
    }
}
