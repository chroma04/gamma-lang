//! The C back end: translates a checked [`Program`] to one C translation unit
//! and compiles it with the system C compiler.
//!
//! The generated code follows the interpreter's semantics exactly: values have
//! the same bytes (see [`types`]), checks trap with the same messages, and
//! `@write` formats through the runtime (`runtime/gm_rt.c`) using type
//! descriptors. The design is described in `docs/architecture.md`.

mod arith;
mod expr;
mod func;
mod helpers;
mod types;

use crate::layout::{Layouts, Target};
use crate::source::{SourceMap, Span};
use crate::tir::{FnId, FnKind, Program};
use crate::types::{AdtId, EffectRef, Ty, TyKind};
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

/// Translates `prog` (whose entry is `entry`) to C source.
pub fn generate(prog: &Program, sources: &SourceMap, entry: FnId, opts: &Options) -> CResult<String> {
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
        handler_done: vec![false; prog.handlers.len()],
        decl_done: vec![false; prog.handler_decls.len()],
    };
    g.queue(entry);
    while let Some(f) = g.reachable.pop() {
        func::gen_fn(&mut g, f)?;
    }
    let consts = g.gen_consts()?;
    let main = g.gen_main(entry)?;
    let mut out = String::new();
    out.push_str(RT_H);
    out.push('\n');
    out.push_str(RT_C);
    out.push_str("\n/* ---- generated ---- */\n");
    let _ = writeln!(out, "static int gm_checks = {};", opts.checks as u8);
    out.push_str(PRELUDE);
    out.push_str(helpers::SUPPORT);
    out.push_str(&g.ty.blob_typedefs());
    out.push_str(&g.ty.descriptors());
    out.push_str(&g.static_defs);
    out.push_str(&g.frames);
    out.push_str(&g.protos);
    out.push_str(&g.helpers.protos);
    out.push_str(&g.handler_tables);
    out.push_str(&consts);
    out.push_str(&g.helpers.bodies);
    out.push_str(&g.bodies);
    out.push_str(&main);
    Ok(out)
}

/// Support code shared by all generated programs: frames, handler contexts and
/// non-local exits (decisions documented in docs/architecture.md).
const PRELUDE: &str = r#"
typedef struct gm_frame { struct gm_frame *up; uint8_t clause_lit; } gm_frame;
typedef struct gm_node {
    uint32_t eff;
    uint8_t kind;               /* GM_LIT, GM_DECL or GM_NATIVE */
    void *const *clauses;       /* clause functions, by operation */
    void *env;                  /* literal: its frame; declaration: the handler value */
    struct gm_node *hctx;       /* the context clauses run in */
    gm_frame *with_frame;       /* the installing `with`, for clause exits */
    uint32_t with_label;
    void *result;               /* where a `break` from a clause stores the `with`'s value */
    struct gm_node *next;
} gm_node;
enum { GM_LIT, GM_DECL, GM_NATIVE };
typedef struct { void *code; void *env; gm_node *cx; } gm_closure;
typedef struct { void *env; gm_node *cx; } gm_hval;
enum { GM_NONE, GM_BREAK, GM_CONTINUE, GM_RETURN, GM_EXIT };
typedef struct { int kind; gm_frame *frame; uint32_t label; uint8_t is_err; uint8_t code; } gm_unwind;
static gm_unwind gm_unw;

static gm_node *gm_find(gm_node *cx, uint32_t eff) {
    for (; cx; cx = cx->next) {
        if (cx->eff == eff) return cx;
    }
    return NULL;
}

/* Whether `target` is the function lexically containing the code of frame `fr`. */
static bool gm_lexically_same(gm_frame *fr, gm_frame *target) {
    for (;;) {
        if (fr == target) return true;
        if (!fr->clause_lit || !fr->up) return false;
        fr = fr->up;
    }
}

/* Whether errdefers run as the current exit leaves a block of frame `fr` (§8.8). */
static bool gm_errdefer_runs(gm_frame *fr) {
    switch (gm_unw.kind) {
    case GM_RETURN: return gm_lexically_same(fr, gm_unw.frame) ? gm_unw.is_err : true;
    case GM_BREAK: case GM_CONTINUE: return !gm_lexically_same(fr, gm_unw.frame);
    case GM_EXIT: return true;
    default: return false;
    }
}
"#;

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
                        format!("(void *){}", self.fn_name(*f))
                    }
                };
                let _ = writeln!(
                    init,
                    "    {{ void *p = (uint8_t *){} + {}; memcpy(gm_c{} + {}, &p, sizeof p); }}",
                    target, r.addend, i, r.offset
                );
            }
        }
        // Constants may refer to functions not otherwise reached.
        while let Some(f) = self.reachable.pop() {
            func::gen_fn(self, f)?;
        }
        init.push_str("}\n");
        Ok(format!("{}{}", decls, init))
    }

    /// The C `main`: installs the native ambient handlers, runs the entry function and
    /// turns its result into the exit status (as the interpreter's `run_main`).
    fn gen_main(&mut self, entry: FnId) -> CResult<String> {
        let def = &self.prog.fns[entry.index()];
        let ret = def.ret;
        let rc = self.ty.cty(ret);
        let mut s = String::from("int main(int argc, char **argv) {\n    gm_set_args(argc, argv);\n");
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
        let _ = writeln!(s, "    {} r = {}(NULL, cx, NULL);", rc, self.fn_name(entry));
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
        !def.effects.is_empty() || matches!(def.kind, FnKind::Clause | FnKind::Literal)
    }
}

/// Where the C compiler and its output go.
pub struct Build {
    pub c_file: PathBuf,
    pub exe: PathBuf,
}

/// Compiles C source `code` to an executable at `exe` (the source is written next to
/// it with the extension `.c`). Returns the compiler's error output on failure.
pub fn compile(code: &str, exe: &Path) -> Result<Build, String> {
    let c_file = exe.with_extension("c");
    std::fs::write(&c_file, code).map_err(|e| format!("cannot write {}: {}", c_file.display(), e))?;
    let cc = std::env::var("CC").unwrap_or_else(|_| "cc".into());
    let out = std::process::Command::new(&cc)
        .args(["-std=gnu11", "-O2", "-w", "-fwrapv", "-o"])
        .arg(exe)
        .arg(&c_file)
        .arg("-lm")
        .output()
        .map_err(|e| format!("cannot run the C compiler `{}`: {}", cc, e))?;
    if !out.status.success() {
        return Err(format!("the C compiler failed:\n{}", String::from_utf8_lossy(&out.stderr)));
    }
    Ok(Build { c_file, exe: exe.to_path_buf() })
}
