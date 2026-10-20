//! The Cranelift back end: translates a checked [`Program`] to machine code with
//! [Cranelift](https://cranelift.dev), without going through C.
//!
//! The program's functions are lowered from TIR to Cranelift IR here and emitted as one
//! object file. The object is linked with the C runtime (`runtime/gm_rt.c`) and its glue
//! for this back end (`runtime/gm_clif.c`), compiled once into a cached object by the
//! system C compiler, which also drives the link. Values have the bytes the interpreter
//! and the C back end give them, checks trap with the same messages through the runtime,
//! and `@write` formats through the runtime's type descriptors, so a program behaves the
//! same on every back end. The design is described in `docs/architecture.md`.
//!
//! The back end supports 64-bit Linux and macOS on x86-64 and AArch64. What it does not
//! support yet is reported as an [`Unsupported`] error naming the construct.

mod cabi;
mod data;
mod expr;
mod func;
mod ops;

use crate::cgen::{Options, Unsupported};
use crate::layout::Layouts;
use crate::source::{SourceMap, Span};
use crate::tir::{Expr, FnId, FnKind, LabelId, LocalId, Program};
use crate::toolchain::Toolchain;
use crate::types::{AdtId, EffectRef, EffectSet, FloatTy, Ty, TyKind};
use cranelift_codegen::ir::{self, AbiParam, Signature, types};
use cranelift_codegen::isa::CallConv;
use cranelift_codegen::settings::{self, Configurable};
use cranelift_frontend::FunctionBuilderContext;
use cranelift_module::{DataId, FuncId, Linkage, Module};
use cranelift_object::{ObjectBuilder, ObjectModule};
use std::collections::{BTreeSet, HashMap, HashSet, VecDeque};
use std::path::Path;
use std::rc::Rc;

pub type CResult<T> = Result<T, Unsupported>;

/// The error for a construct this back end cannot translate.
pub fn unsupported<T>(span: Span, what: impl Into<String>) -> CResult<T> {
    Err(Unsupported { message: format!("not supported by the Cranelift back end yet: {}", what.into()), span })
}

/// The glue between generated code and the runtime, compiled with it.
const GLUE: &str = include_str!("../../runtime/gm_clif.c");

/// The output of [`generate`]: an object file holding the program.
pub struct Generated {
    pub object: Vec<u8>,
    /// Libraries to link (from `foreign` blocks), beyond the C library.
    pub libs: Vec<String>,
    /// C functions holding the program's inline assembly, compiled with the C compiler.
    pub asm: Vec<String>,
    pub opts: Options,
    pub target: crate::layout::Target,
}

/// Whether this back end can compile programs for `target`, and if not, why.
pub fn check_target(target: &crate::layout::Target) -> Result<&'static str, String> {
    let triple = match (target.os, target.arch) {
        ("Linux", "X86_64") => "x86_64-unknown-linux-gnu",
        ("Linux", "Aarch64") => "aarch64-unknown-linux-gnu",
        ("MacOS", "X86_64") => "x86_64-apple-darwin",
        ("MacOS", "Aarch64") => "aarch64-apple-darwin",
        (os, arch) => {
            return Err(format!(
                "the Cranelift back end supports Linux and macOS on x86-64 and AArch64; this target is {} on {}",
                os, arch
            ));
        }
    };
    if target.filc {
        return Err("Safe builds need the C back end (Fil-C)".into());
    }
    Ok(triple)
}

/// Translates `prog` to an object file whose `main` runs `entries[0]`, or with `select`
/// the entry whose index is the program's first argument (`gamma test`).
pub fn generate(
    prog: &Program,
    sources: &SourceMap,
    entries: &[FnId],
    select: bool,
    opts: &Options,
) -> CResult<Generated> {
    let triple = check_target(&prog.target).map_err(|m| Unsupported { message: m, span: Span::DUMMY })?;
    if opts.debug_info {
        return unsupported(Span::DUMMY, "debug information (`-g`); use `--backend=c`");
    }
    let mut flags = settings::builder();
    let set = |f: &mut settings::Builder, k: &str, v: &str| {
        f.set(k, v).map_err(|e| Unsupported { message: format!("cranelift setting {k}: {e}"), span: Span::DUMMY })
    };
    set(&mut flags, "opt_level", "speed")?;
    set(&mut flags, "is_pic", "true")?;
    set(&mut flags, "enable_verifier", if cfg!(debug_assertions) { "true" } else { "false" })?;
    set(&mut flags, "preserve_frame_pointers", "true")?;
    // 128-bit integers are passed in pairs of registers, as C passes `__int128`.
    set(&mut flags, "enable_llvm_abi_extensions", "true")?;
    set(&mut flags, "tls_model", if prog.target.os == "MacOS" { "macho" } else { "elf_gd" })?;
    let isa = cranelift_codegen::isa::lookup_by_name(triple)
        .map_err(|e| Unsupported { message: format!("no Cranelift target {triple}: {e}"), span: Span::DUMMY })?
        .finish(settings::Flags::new(flags))
        .map_err(|e| Unsupported { message: format!("Cranelift target {triple}: {e}"), span: Span::DUMMY })?;
    let builder = ObjectBuilder::new(isa, "gamma", cranelift_module::default_libcall_names())
        .map_err(|e| Unsupported { message: format!("Cranelift object: {e}"), span: Span::DUMMY })?;
    let module = ObjectModule::new(builder);
    let lookup = |id: AdtId| prog.adts.get(&id);
    let layouts = Layouts::new(prog.target.clone(), &lookup);
    let cc = module.target_config().default_call_conv;
    let mut g = Gen {
        prog,
        sources,
        lay: &layouts,
        opts: opts.clone(),
        module,
        cc,
        fns: HashMap::new(),
        queue: VecDeque::new(),
        rt: HashMap::new(),
        va_tramps: HashMap::new(),
        c_thunks: HashMap::new(),
        c_shims: HashMap::new(),
        asm: vec![],
        bytes: HashMap::new(),
        descs: HashMap::new(),
        specs: HashMap::new(),
        consts: HashMap::new(),
        globals: HashMap::new(),
        srcs: HashMap::new(),
        handler_tables: HashMap::new(),
        decl_tables: HashMap::new(),
        effect_ids: HashMap::new(),
        frames: HashMap::new(),
        captured: None,
        libs: BTreeSet::new(),
        fctx: FunctionBuilderContext::new(),
        anon: 0,
    };
    g.declare_exports()?;
    let prog_data = g.gen_entries(entries, select)?;
    g.drain()?;
    let _ = prog_data;
    let product = g.module.finish();
    let object = product.emit().map_err(|e| Unsupported { message: format!("object file: {e}"), span: Span::DUMMY })?;
    Ok(Generated {
        object,
        libs: g.libs.into_iter().collect(),
        asm: g.asm,
        opts: opts.clone(),
        target: prog.target.clone(),
    })
}

/// Work queued while generating: functions to generate later, since a function is
/// generated with a builder of its own.
enum Work<'a> {
    /// A Gamma function's body.
    Fn(FnId),
    /// The body of a `with … on` of function `fid`, which runs on a fiber (§14): a
    /// `void (gm_fiber *)` that reaches the frame of `fid` through the fiber.
    WithBody {
        func: FuncId,
        fid: FnId,
        label: LabelId,
        ty: Ty,
        body: &'a Expr,
    },
    /// The thunk that calls the clause of a `ctl` operation for a pending perform (§14.2):
    /// `void (void *req, gm_cont k)`; the request holds the node and the arguments at
    /// `offsets`.
    CtlThunk {
        func: FuncId,
        op: u32,
        tys: Vec<Ty>,
        offsets: Vec<u64>,
        ret: Ty,
    },
    /// The start function of a fiber made by `@fiber` (§14.6): calls the function pointer
    /// `fb->fn` with the argument at `fb->arg`.
    FiberStart {
        func: FuncId,
        aty: Ty,
        hidden: bool,
    },
    /// The clause thunk of a handler declaration: loads `self` from the node's `env`.
    DeclThunk {
        func: FuncId,
        clause: FnId,
    },
    /// The exported symbol of function `f` when the C ABI passes some of its values
    /// differently from the plain convention: converts and calls `f`.
    ExportThunk {
        func: FuncId,
        f: FnId,
        cs: Rc<cabi::CSig>,
    },
    CShim {
        func: FuncId,
        tys: Vec<Ty>,
        ret: Ty,
        cs: Rc<cabi::CSig>,
    },
}

/// How a value of a type is held in generated code.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Rep {
    /// No bytes (unit, `never`, empty structs and arrays).
    Zero,
    /// An integer of `bits` bits in a register of type `ty`, kept normalized (sign- or
    /// zero-extended from `bits`).
    Int {
        ty: ir::Type,
        bits: u32,
        signed: bool,
    },
    /// 0 or 1 in an `i8`.
    Bool,
    /// The bits of an `f16` in an `i16`.
    F16,
    F32,
    F64,
    /// Any pointer, and optional pointers (null is 0).
    Ptr,
    /// Bytes in memory: a value is the address of its bytes.
    Mem {
        size: u64,
        align: u64,
    },
}

impl Rep {
    /// The register type of a scalar.
    pub fn ty(self) -> Option<ir::Type> {
        match self {
            Rep::Int { ty, .. } => Some(ty),
            Rep::Bool => Some(types::I8),
            Rep::F16 => Some(types::I16),
            Rep::F32 => Some(types::F32),
            Rep::F64 => Some(types::F64),
            Rep::Ptr => Some(types::I64),
            Rep::Zero | Rep::Mem { .. } => None,
        }
    }

    /// The parameter or result of a signature for a value of this representation:
    /// memory is passed by address.
    pub fn abi(self) -> Option<AbiParam> {
        match self {
            Rep::Zero => None,
            Rep::Mem { .. } => Some(AbiParam::new(types::I64)),
            Rep::Int { ty, signed, .. } if ty.bits() < 64 => {
                Some(if signed { AbiParam::new(ty).sext() } else { AbiParam::new(ty).uext() })
            }
            Rep::Bool | Rep::F16 => Some(AbiParam::new(self.ty().unwrap()).uext()),
            _ => Some(AbiParam::new(self.ty().unwrap())),
        }
    }
}

/// Where a function keeps its locals and results: one stack slot laid out like the C back
/// end's frame struct, starting with a `gm_frame` header (the enclosing frame, for
/// capturing functions, and whether it is a handler clause's).
#[derive(Debug)]
pub struct FrameLayout {
    pub size: u64,
    pub align: u64,
    /// The offset of each local that lives in the frame.
    pub locals: Vec<Option<u64>>,
    /// The function's result.
    pub ret: u64,
    /// The result of each `with` of the body.
    pub withs: HashMap<LabelId, u64>,
}

/// The size of the frame header `gm_frame`.
const FRAME_HEADER: u64 = 16;

pub struct Gen<'a> {
    pub prog: &'a Program,
    pub sources: &'a SourceMap,
    pub lay: &'a Layouts<'a>,
    pub opts: Options,
    pub module: ObjectModule,
    /// The target's C calling convention, which generated functions use too.
    pub cc: CallConv,
    fns: HashMap<FnId, FuncId>,
    queue: VecDeque<Work<'a>>,
    /// Runtime functions by name.
    rt: HashMap<&'static str, FuncId>,
    /// The trampolines that call variadic C functions with floats on x86-64, by callee.
    va_tramps: HashMap<FuncId, FuncId>,
    /// The C-convention wrappers of functions used as values of C-convention pointer types
    /// (see `FnGen::c_abi_ptr`), by function.
    c_thunks: HashMap<FnId, FuncId>,
    /// The code of second-class values made from such pointers, by pointer type.
    c_shims: HashMap<Ty, FuncId>,
    /// The C function of each `@asm` (see `FnGen::asm`).
    pub asm: Vec<String>,
    /// Static byte strings (each followed by a NUL byte), by content.
    bytes: HashMap<Vec<u8>, DataId>,
    descs: HashMap<Ty, DataId>,
    specs: HashMap<[u8; 16], DataId>,
    consts: HashMap<u32, DataId>,
    globals: HashMap<u32, DataId>,
    srcs: HashMap<u32, DataId>,
    handler_tables: HashMap<usize, DataId>,
    decl_tables: HashMap<usize, DataId>,
    effect_ids: HashMap<EffectRef, u32>,
    frames: HashMap<FnId, Rc<FrameLayout>>,
    captured: Option<HashSet<(FnId, LocalId)>>,
    libs: BTreeSet<String>,
    fctx: FunctionBuilderContext,
    /// Numbers the local symbols.
    anon: u32,
}

fn module_err(e: cranelift_module::ModuleError) -> Unsupported {
    Unsupported { message: format!("Cranelift: {e:?}"), span: Span::DUMMY }
}

impl<'a> Gen<'a> {
    pub fn size(&self, t: Ty) -> u64 {
        self.lay.size_of(t)
    }

    pub fn align(&self, t: Ty) -> u64 {
        self.lay.align_of(t).max(1)
    }

    pub fn offsets(&self, t: Ty) -> Vec<u64> {
        self.lay.layout(t).offsets
    }

    pub fn rep(&self, t: Ty) -> Rep {
        let size = self.size(t);
        match t.kind() {
            TyKind::Int(i) => Rep::Int { ty: int_type(size), bits: i.bits as u32, signed: i.signed },
            TyKind::Usize | TyKind::Isize => {
                Rep::Int { ty: types::I64, bits: 64, signed: matches!(t.kind(), TyKind::Isize) }
            }
            TyKind::Float(FloatTy::F16) => Rep::F16,
            TyKind::Float(FloatTy::F32) => Rep::F32,
            TyKind::Float(FloatTy::F64) => Rep::F64,
            TyKind::Bool => Rep::Bool,
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } | TyKind::Rawptr | TyKind::FnPtr(_) => Rep::Ptr,
            TyKind::Optional(inner) if crate::layout::is_nullable_pointer(*inner) => Rep::Ptr,
            _ if size == 0 => Rep::Zero,
            _ => Rep::Mem { size, align: self.align(t) },
        }
    }

    /// A fresh name for a local symbol.
    fn anon_name(&mut self, prefix: &str) -> String {
        self.anon += 1;
        format!("{prefix}{}", self.anon)
    }

    // --- functions -------------------------------------------------------------------------

    /// The function implementing `f` (or importing a foreign one), queued for generation.
    pub fn func(&mut self, f: FnId) -> CResult<FuncId> {
        if let Some(&id) = self.fns.get(&f) {
            return Ok(id);
        }
        let def = &self.prog.fns[f.index()];
        let id = if let FnKind::Foreign { lib, link_name, .. } = def.kind {
            let lib = lib.as_str().to_string();
            if lib != "c" && lib != "m" {
                self.libs.insert(lib);
            }
            let sig = self.c_sig_of(f)?.sig;
            match self.module.declare_function(link_name.as_str(), Linkage::Import, &sig) {
                Ok(id) => id,
                // A symbol the program also exports, or declares again, with another C
                // signature (calls use their own; see `FnGen::call_c`).
                Err(cranelift_module::ModuleError::IncompatibleSignature(..)) => {
                    match self.module.get_name(link_name.as_str()) {
                        Some(cranelift_module::FuncOrDataId::Func(id)) => id,
                        _ => return unsupported(def.span, format!("the symbol `{}`", link_name)),
                    }
                }
                Err(e) => return Err(module_err(e)),
            }
        } else {
            let sig = self.fn_sig(f);
            let name = format!("gm_f{}", f.0);
            let id = self.module.declare_function(&name, Linkage::Local, &sig).map_err(module_err)?;
            self.queue.push_back(Work::Fn(f));
            id
        };
        self.fns.insert(f, id);
        Ok(id)
    }

    /// The signature of Gamma function `f` (see [`Gen::sig_of`]).
    pub fn fn_sig(&self, f: FnId) -> Signature {
        let def = &self.prog.fns[f.index()];
        let params: Vec<Ty> = def.params.iter().map(|p| def.locals[p.index()].ty).collect();
        let cc = if self.is_plain(f) { self.callconv(def.callconv) } else { self.cc };
        self.sig_of(&params, def.ret, !self.is_plain(f), cc)
    }

    /// The signature of a Gamma function taking `params` and returning `ret`: a large
    /// result goes to the address in the first parameter; with `hidden`, the environment,
    /// the handler context and the installing handler's node follow (the convention of
    /// every function but the plain ones, §19.1); then the parameters, values in
    /// memory by address.
    pub fn sig_of(&self, params: &[Ty], ret: Ty, hidden: bool, cc: CallConv) -> Signature {
        let mut sig = Signature::new(cc);
        let rr = self.rep(ret);
        match rr {
            Rep::Mem { .. } => sig.params.push(AbiParam::new(types::I64)),
            Rep::Zero => {}
            r => sig.returns.push(r.abi().unwrap()),
        }
        if hidden {
            for _ in 0..3 {
                sig.params.push(AbiParam::new(types::I64));
            }
        }
        for p in params {
            if let Some(a) = self.rep(*p).abi() {
                sig.params.push(a);
            }
        }
        sig
    }

    pub fn callconv(&self, cc: crate::types::CallConv) -> CallConv {
        match cc {
            crate::types::CallConv::C => self.cc,
            crate::types::CallConv::Win64 => CallConv::WindowsFastcall,
        }
    }

    /// A runtime function, declared on first use. `sig` describes it: a letter for each
    /// parameter, then `>` and the results: `p` a pointer, `l` 64 bits, `i` 32 bits,
    /// `h` 16 bits, `b` 8 bits, `q` 128 bits, `d` a double, `f` a float.
    pub fn rt(&mut self, name: &'static str, sig: &str) -> FuncId {
        if let Some(&id) = self.rt.get(name) {
            return id;
        }
        let mut s = Signature::new(self.cc);
        let (ps, rs) = sig.split_once('>').unwrap_or((sig, ""));
        let abi = |c: char| match c {
            'p' | 'l' => AbiParam::new(types::I64),
            'i' => AbiParam::new(types::I32),
            'h' => AbiParam::new(types::I16).uext(),
            'b' => AbiParam::new(types::I8).uext(),
            'q' => AbiParam::new(types::I128),
            'd' => AbiParam::new(types::F64),
            'f' => AbiParam::new(types::F32),
            _ => panic!("bad runtime signature {sig}"),
        };
        s.params.extend(ps.chars().filter(|c| *c != ' ').map(abi));
        s.returns.extend(rs.chars().filter(|c| *c != ' ').map(abi));
        let id = self.module.declare_function(name, Linkage::Import, &s).expect("runtime function");
        self.rt.insert(name, id);
        id
    }

    /// Generates every function queued so far, and those they reach.
    fn drain(&mut self) -> CResult<()> {
        while let Some(w) = self.queue.pop_front() {
            match w {
                Work::Fn(f) => func::gen_fn(self, f)?,
                Work::WithBody { func, fid, label, ty, body } => func::gen_with_body(self, func, fid, label, ty, body)?,
                Work::CtlThunk { func, op, tys, offsets, ret } => {
                    func::gen_ctl_thunk(self, func, op, &tys, &offsets, ret)?
                }
                Work::FiberStart { func, aty, hidden } => func::gen_fiber_start(self, func, aty, hidden)?,
                Work::DeclThunk { func, clause } => func::gen_decl_thunk(self, func, clause)?,
                Work::ExportThunk { func, f, cs } => func::gen_export_thunk(self, func, f, &cs)?,
                Work::CShim { func, tys, ret, cs } => func::gen_c_shim(self, func, &tys, ret, &cs)?,
            }
        }
        Ok(())
    }

    /// Declares a local function for generated code of a kind given by `prefix`.
    fn local_fn(&mut self, prefix: &str, sig: &Signature) -> CResult<FuncId> {
        let name = self.anon_name(prefix);
        self.module.declare_function(&name, Linkage::Local, sig).map_err(module_err)
    }

    /// A function with the C ABI that calls plain function `f`: what a function pointer
    /// of a type that passes aggregates (see `FnGen::c_abi_ptr`) holds for `f`.
    pub fn c_thunk(&mut self, f: FnId) -> CResult<FuncId> {
        if let Some(&t) = self.c_thunks.get(&f) {
            return Ok(t);
        }
        let cs = self.c_sig_of(f)?;
        let id = self.local_fn("gm_cthunk", &cs.sig)?;
        self.c_thunks.insert(f, id);
        self.queue.push_back(Work::ExportThunk { func: id, f, cs: Rc::new(cs) });
        Ok(id)
    }

    /// The code of a second-class value whose environment is a pointer of C-convention
    /// function pointer type `t` (see `FnGen::c_abi_ptr`): it calls the pointer as C does.
    pub fn c_shim(&mut self, t: Ty, span: Span) -> CResult<FuncId> {
        if let Some(&id) = self.c_shims.get(&t) {
            return Ok(id);
        }
        let TyKind::FnPtr(sig) = t.kind() else { return unsupported(span, format!("calling `{}` as C does", t)) };
        let cs = self.c_sig(&sig.params, sig.ret, sig.callconv, span)?;
        let id = self.local_fn("gm_cshim", &self.sig_of(&sig.params, sig.ret, true, self.cc))?;
        self.c_shims.insert(t, id);
        self.queue.push_back(Work::CShim { func: id, tys: sig.params.to_vec(), ret: sig.ret, cs: Rc::new(cs) });
        Ok(id)
    }

    /// A trampoline for calls of variadic C function `callee` on x86-64: the System V ABI
    /// has a variadic callee read in `al` how many vector registers hold arguments (an upper
    /// bound is enough), which Cranelift does not set. The trampoline sets it to 8 and jumps
    /// to `callee`, leaving every argument register as it is.
    pub fn va_trampoline(&mut self, callee: FuncId) -> CResult<FuncId> {
        if let Some(&t) = self.va_tramps.get(&callee) {
            return Ok(t);
        }
        let id = self.local_fn("gm_clif_va", &Signature::new(self.cc))?;
        // mov eax, 8; jmp callee
        let bytes = [0xb8, 8, 0, 0, 0, 0xe9, 0, 0, 0, 0];
        let reloc = cranelift_module::ModuleReloc {
            offset: 6,
            kind: cranelift_codegen::binemit::Reloc::X86CallPLTRel4,
            name: callee.into(),
            addend: -4,
        };
        self.module.define_function_bytes(id, 16, &bytes, &[reloc]).map_err(module_err)?;
        self.va_tramps.insert(callee, id);
        Ok(id)
    }

    /// Defines function `id` from `func`.
    fn define(&mut self, id: FuncId, func: ir::Function, name: &str) -> CResult<()> {
        // `GAMMA_DUMP_CLIF=1` prints the Cranelift IR of every function, for debugging.
        if std::env::var_os("GAMMA_DUMP_CLIF").is_some_and(|v| v == "1") {
            eprintln!("; {name}\n{}", func.display());
        }
        let mut ctx = cranelift_codegen::Context::for_function(func);
        let fail = |detail: String| Unsupported {
            message: format!("internal error: Cranelift rejected the code of `{name}`: {detail}"),
            span: Span::DUMMY,
        };
        let isa = self.module.isa();
        match ctx.compile(isa, &mut Default::default()) {
            Ok(_) => {}
            Err(e) => {
                let detail = match &e.inner {
                    cranelift_codegen::CodegenError::Verifier(errs) => {
                        cranelift_codegen::print_errors::pretty_verifier_error(e.func, None, errs.clone())
                    }
                    other => format!("{other:?}"),
                };
                return Err(fail(detail));
            }
        }
        let code = ctx.compiled_code().expect("compiled");
        // Functions are aligned to at least 16 bytes: the low bit of a function's address
        // marks a plain function where the hidden convention is expected (see `call_indirect`).
        let alignment = (code.buffer.alignment as u64).max(16);
        let bytes = code.buffer.data().to_vec();
        let relocs: Vec<_> = code
            .buffer
            .relocs()
            .iter()
            .map(|r| cranelift_module::ModuleReloc::from_mach_reloc(r, &ctx.func, id))
            .collect();
        self.module.define_function_bytes(id, alignment, &bytes, &relocs).map_err(|e| fail(format!("{e:?}")))
    }

    // --- facts about functions -------------------------------------------------------------

    /// True if every effect of `s` is `Sys`.
    pub fn within_sys(&self, s: &EffectSet) -> bool {
        s.effects.iter().all(|e| Some(e.effect) == self.prog.ambient.sys)
    }

    /// Whether `f` has the plain calling convention: a function (or non-capturing literal)
    /// whose effects are at most `{Sys}`, so it can be a C callback (§19.1).
    pub fn is_plain(&self, f: FnId) -> bool {
        let def = &self.prog.fns[f.index()];
        match def.kind {
            FnKind::Foreign { .. } => true,
            FnKind::Named | FnKind::Literal => def.parent.is_none() && self.within_sys(&def.effects),
            FnKind::Clause => false,
        }
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

    /// The number identifying an effect instance in handler contexts.
    pub fn effect_id(&mut self, e: &EffectRef) -> u32 {
        let n = self.effect_ids.len() as u32;
        *self.effect_ids.entry(e.clone()).or_insert(n)
    }

    /// The locals that function literals or handler clauses capture, by owner.
    fn captured(&mut self) -> &HashSet<(FnId, LocalId)> {
        let prog = self.prog;
        self.captured.get_or_insert_with(|| {
            let mut set = HashSet::new();
            for (i, d) in prog.fns.iter().enumerate() {
                if let Some(b) = &d.body {
                    collect_upvars(prog, FnId(i as u32), b, &mut set);
                }
            }
            set
        })
    }

    /// The frame layout of `f`. A local lives in the frame when a function literal or
    /// handler clause reaches it through the environment, when its address is taken or it
    /// is held in memory anyway, and in Debug builds (whose dead frames are filled with
    /// 0xDD, §18.3) or when a `with … on` body, which runs as a function of its own, reaches
    /// the frame through a pointer. The other locals are SSA variables.
    pub fn frame(&mut self, f: FnId) -> Rc<FrameLayout> {
        if let Some(l) = self.frames.get(&f) {
            return l.clone();
        }
        let prog = self.prog;
        let def = &prog.fns[f.index()];
        let all = self.opts.debug || def.body.as_ref().is_some_and(has_fiber_with);
        let mut addressed = HashSet::new();
        if let Some(b) = &def.body {
            collect_addressed(b, &mut addressed);
        }
        self.captured();
        let captured = self.captured.as_ref().unwrap();
        let mut size = FRAME_HEADER;
        let mut align = 8;
        let mut place = |sz: u64, al: u64| {
            let off = size.next_multiple_of(al);
            size = off + sz;
            align = align.max(al);
            off
        };
        let mut locals = vec![];
        for (i, l) in def.locals.iter().enumerate() {
            let rep = self.rep(l.ty);
            let id = LocalId(i as u32);
            let in_frame = rep != Rep::Zero
                && (all || matches!(rep, Rep::Mem { .. }) || captured.contains(&(f, id)) || addressed.contains(&id));
            locals.push(in_frame.then(|| place(self.lay.size_of(l.ty), self.lay.align_of(l.ty).max(1))));
        }
        let ret = place(self.lay.size_of(def.ret), self.lay.align_of(def.ret).max(1));
        let mut withs = HashMap::new();
        if let Some(b) = &def.body {
            let mut ws = vec![];
            collect_withs(b, &mut ws);
            for (label, t) in ws {
                let off = place(self.lay.size_of(t), self.lay.align_of(t).max(1));
                withs.insert(label, off);
            }
        }
        let layout = Rc::new(FrameLayout { size: size.next_multiple_of(align), align, locals, ret, withs });
        self.frames.insert(f, layout.clone());
        layout
    }

    // --- exports and entries --------------------------------------------------------------

    /// Exported functions (§19.4) are defined under their symbols, with the C ABI.
    fn declare_exports(&mut self) -> CResult<()> {
        if self.prog.exports.is_empty() {
            return Ok(());
        }
        let runtime = runtime_source();
        let idents = crate::cgen::c_identifiers(&runtime);
        for (sym, f) in self.prog.exports.clone() {
            let def = &self.prog.fns[f.index()];
            // Generated functions are named `gm_` and a kind and number.
            let generated = sym.as_str().strip_prefix("gm_").is_some_and(|r| r.ends_with(|c: char| c.is_ascii_digit()));
            if idents.contains(sym.as_str()) || generated || sym.as_str() == "main" {
                return Err(Unsupported {
                    message: format!(
                        "the exported name `{}` is also a name of the compiled program's runtime; choose another",
                        sym
                    ),
                    span: def.span,
                });
            }
            if !self.is_plain(f) {
                return unsupported(def.span, "this exported function's signature");
            }
            // The plain convention is the C ABI for scalars; a function taking or returning
            // aggregates is exported through a thunk that converts between the two.
            let cs = self.c_sig_of(f)?;
            let id = self.module.declare_function(sym.as_str(), Linkage::Export, &cs.sig).map_err(module_err)?;
            if cs.is_scalar() && cs.sig == self.fn_sig(f) {
                self.fns.insert(f, id);
                self.queue.push_back(Work::Fn(f));
            } else {
                self.queue.push_back(Work::ExportThunk { func: id, f, cs: Rc::new(cs) });
            }
        }
        Ok(())
    }

    /// The entry functions and `gm_clif_prog`, which the glue's `main` reads.
    fn gen_entries(&mut self, entries: &[FnId], select: bool) -> CResult<DataId> {
        use cranelift_module::DataDescription;
        let mut table = DataDescription::new();
        let mut bytes = vec![0u8; 48 * entries.len()];
        let mut relocs: Vec<(u32, DataOrFn)> = vec![];
        for (i, &entry) in entries.iter().enumerate() {
            let at = 48 * i;
            let wrapper = func::gen_entry(self, entry)?;
            relocs.push((at as u32, DataOrFn::Fn(wrapper)));
            let ret = self.prog.fns[entry.index()].ret;
            let put32 =
                |b: &mut Vec<u8>, off: usize, v: u64| b[off..off + 4].copy_from_slice(&(v as u32).to_le_bytes());
            put32(&mut bytes, at + 8, self.size(ret));
            put32(&mut bytes, at + 12, self.align(ret));
            match ret.kind() {
                TyKind::Result { err, ok } => {
                    let offs = self.offsets(ret);
                    bytes[at + 16] = 2;
                    if ok.is_integer() {
                        put32(&mut bytes, at + 20, self.size(*ok));
                    }
                    put32(&mut bytes, at + 24, offs[0]);
                    put32(&mut bytes, at + 28, offs[1]);
                    let d = self.desc(*err)?;
                    relocs.push((at as u32 + 32, DataOrFn::Data(d)));
                    // A union's value is named with its type: `E.NotFound` (§3.7).
                    if let TyKind::Adt(id, _) = err.kind()
                        && matches!(self.prog.adts.get(id).map(|d| &d.kind), Some(crate::tir::AdtKind::Union { .. }))
                    {
                        let s = self.cstring(err.to_string().as_bytes())?;
                        relocs.push((at as u32 + 40, DataOrFn::Data(s)));
                    }
                }
                _ if ret.is_integer() => {
                    bytes[at + 16] = 1;
                    put32(&mut bytes, at + 20, self.size(ret));
                }
                _ => {}
            }
        }
        let entries_id = self.module.declare_anonymous_data(false, false).map_err(module_err)?;
        table.define(bytes.into_boxed_slice());
        table.set_align(8);
        for (off, r) in relocs {
            match r {
                DataOrFn::Fn(f) => {
                    let fr = self.module.declare_func_in_data(f, &mut table);
                    table.write_function_addr(off, fr);
                }
                DataOrFn::Data(d) => {
                    let gv = self.module.declare_data_in_data(d, &mut table);
                    table.write_data_addr(off, gv, 0);
                }
            }
        }
        self.module.define_data(entries_id, &table).map_err(module_err)?;
        let mut p = vec![0u8; 32];
        p[0..4].copy_from_slice(&(entries.len() as u32).to_le_bytes());
        p[4] = select as u8;
        p[5] = self.opts.debug as u8;
        p[6] = self.opts.checks as u8;
        let a = self.prog.ambient.clone();
        for (k, e) in [a.out, a.err, a.env].into_iter().enumerate() {
            let id = match e {
                Some(e) => self.effect_id(&EffectRef { effect: e, args: Box::new([]) }),
                None => u32::MAX,
            };
            p[8 + 4 * k..12 + 4 * k].copy_from_slice(&id.to_le_bytes());
        }
        let id = self.module.declare_data("gm_clif_prog", Linkage::Export, false, false).map_err(module_err)?;
        let mut d = DataDescription::new();
        d.define(p.into_boxed_slice());
        d.set_align(8);
        let gv = self.module.declare_data_in_data(entries_id, &mut d);
        d.write_data_addr(24, gv, 0);
        self.module.define_data(id, &d).map_err(module_err)?;
        Ok(id)
    }
}

enum DataOrFn {
    Data(DataId),
    Fn(FuncId),
}

/// The integer register type of an integer stored in `size` bytes.
pub fn int_type(size: u64) -> ir::Type {
    match size {
        1 => types::I8,
        2 => types::I16,
        4 => types::I32,
        8 => types::I64,
        _ => types::I128,
    }
}

/// True if one of `params` is a second-class function (§11.6), through which a callee
/// that performs nothing itself may still leave by a non-local exit.
pub fn takes_fn(params: impl IntoIterator<Item = Ty>) -> bool {
    params.into_iter().any(|t| matches!(t.kind(), TyKind::Fn(_)))
}

/// Whether `e` contains a `with … on` (excluding nested functions).
fn has_fiber_with(e: &Expr) -> bool {
    use crate::tir::ExprKind;
    if let ExprKind::With { on: Some(_), .. } = &e.kind {
        return true;
    }
    let mut found = false;
    e.for_each_child(&mut |c| found = found || has_fiber_with(c));
    found
}

/// Adds the locals of enclosing functions that `e`, in the body of `f`, refers to.
fn collect_upvars(prog: &Program, f: FnId, e: &Expr, out: &mut HashSet<(FnId, LocalId)>) {
    use crate::tir::ExprKind;
    if let ExprKind::Upvar { levels, local } = &e.kind {
        let mut owner = f;
        for _ in 0..*levels {
            owner = prog.fns[owner.index()].parent.unwrap_or(owner);
        }
        out.insert((owner, *local));
    }
    e.for_each_child(&mut |c| collect_upvars(prog, f, c, out));
}

/// Adds the locals whose address `e` takes: the root of every place used as one.
fn collect_addressed(e: &Expr, out: &mut HashSet<LocalId>) {
    use crate::tir::ExprKind;
    let root = |mut x: &Expr| loop {
        match &x.kind {
            ExprKind::Local(l) => return Some(*l),
            ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => x = base,
            ExprKind::Index { base, .. } if base.ty.is_array() => x = base,
            ExprKind::Slice { base, .. } if base.ty.is_array() => x = base,
            _ => return None,
        }
    };
    match &e.kind {
        ExprKind::AddrOf(x) | ExprKind::SliceLen(x) | ExprKind::SlicePtr(x) => out.extend(root(x)),
        ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } | ExprKind::Index { base, .. } => {
            out.extend(root(base))
        }
        ExprKind::Slice { base, .. } => out.extend(root(base)),
        _ => {}
    }
    e.for_each_child(&mut |c| collect_addressed(c, out));
}

/// The labels and types of the `with`s in `e` (excluding nested functions).
fn collect_withs(e: &Expr, out: &mut Vec<(LabelId, Ty)>) {
    if let crate::tir::ExprKind::With { label, .. } = &e.kind {
        out.push((*label, e.ty));
    }
    e.for_each_child(&mut |c| collect_withs(c, out));
}

/// The C runtime and the glue, as one translation unit compiled on its own.
pub fn runtime_source() -> String {
    format!("{}\n{}\n", crate::cgen::separate_runtime_source(), GLUE)
}

/// Links the object of `generated` with the runtime into the executable `exe`, with the C
/// compiler of `toolchain` as the linker driver (the object is written next to `exe`).
/// Returns the compiler's error output on failure.
pub fn compile(
    generated: &Generated,
    toolchain: &Toolchain,
    link: &crate::cgen::Link,
    exe: &Path,
) -> Result<(), String> {
    use crate::cgen::{Scratch, scratch_path};
    let obj = scratch_path(exe, "gamma", "o");
    let asm_obj = scratch_path(exe, "asm", "o");
    let rt_obj = scratch_path(exe, "rt", "o");
    let _scratch = Scratch(vec![obj.clone(), asm_obj.clone(), rt_obj.clone()]);
    std::fs::write(&obj, &generated.object).map_err(|e| format!("cannot write {}: {}", obj.display(), e))?;
    let rt_flags = crate::cgen::rt_flags(&generated.opts, &generated.target);
    let runtime = match crate::cgen::runtime_object_from(toolchain, &rt_flags, &runtime_source(), "gm_clif_rt") {
        Some(o) => o,
        None => {
            // No cache: the runtime is compiled next to the executable.
            let c = scratch_path(exe, "rt", "c");
            let o = rt_obj.clone();
            let _c = Scratch(vec![c.clone()]);
            std::fs::write(&c, runtime_source()).map_err(|e| format!("cannot write {}: {}", c.display(), e))?;
            let out = toolchain
                .command(&rt_flags)
                .arg("-c")
                .arg("-o")
                .arg(&o)
                .arg(&c)
                .output()
                .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
            if !out.status.success() {
                return Err(format!("the C compiler failed:\n{}", String::from_utf8_lossy(&out.stderr)));
            }
            o
        }
    };
    // The functions holding inline assembly.
    if !generated.asm.is_empty() {
        let c = scratch_path(exe, "asm", "c");
        let _c = Scratch(vec![c.clone()]);
        let src = format!("#include <stdint.h>\n\n{}", generated.asm.join("\n"));
        std::fs::write(&c, src).map_err(|e| format!("cannot write {}: {}", c.display(), e))?;
        let mut flags: Vec<String> =
            crate::cgen::base_cflags(&generated.target).iter().map(|s| s.to_string()).collect();
        flags.push("-O2".into());
        let out = toolchain
            .command(&flags)
            .arg("-c")
            .arg("-o")
            .arg(&asm_obj)
            .arg(&c)
            .output()
            .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
        if !out.status.success() {
            return Err(format!(
                "the C compiler failed on the program's `@asm`:\n{}",
                String::from_utf8_lossy(&out.stderr)
            ));
        }
    }
    let mut flags: Vec<String> = crate::cgen::base_cflags(&generated.target).iter().map(|s| s.to_string()).collect();
    if generated.target.os == "Linux" {
        // The object is position independent; the executable may be too.
        flags.push("-pie".into());
    }
    let out = toolchain
        .command(&flags)
        .arg("-o")
        .arg(exe)
        .arg(&obj)
        .arg(&runtime)
        .args((!generated.asm.is_empty()).then_some(&asm_obj))
        .args(&link.objects)
        .args(&toolchain.ldflags)
        .args(link.library_args(&generated.libs))
        .arg("-lm")
        .output()
        .map_err(|e| format!("cannot run the C compiler `{}`: {}", toolchain.display(), e))?;
    if !out.status.success() {
        return Err(format!("linking failed:\n{}", String::from_utf8_lossy(&out.stderr)));
    }
    Ok(())
}
