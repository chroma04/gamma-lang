//! Semantic analysis: name resolution, type checking, effect checking and
//! lowering to the typed IR ([`crate::tir`]).
//!
//! Analysis is demand-driven. Each top-level declaration is resolved the first
//! time something needs it ([`Sema::resolve_decl`]); a declaration requested
//! while it is being resolved is a dependency cycle (§3.4). Function bodies are
//! checked after their signatures are known, from a work queue, so functions may
//! refer to each other freely.

mod adt;
mod builtins;
mod call;
mod consteval;
mod construct;
mod cont_check;
mod decls;
mod effects;
mod escape;
mod exhaust;
mod expr;
mod finalize;
mod fnlit;
mod foreign;
mod func;
mod generics;
mod handler_decl;
mod imports;
mod infer;
mod matching;
mod methods;
mod modules;
mod pattern;
mod seq;
mod stack_size;
mod tests_decl;
mod ty_expr;
mod untyped;

use crate::diag::{Diagnostic, Diagnostics};
use crate::driver::Session;
use crate::layout::{Layouts, Target};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast;
use crate::tir::{self, FnId, Program};
use crate::types::{AdtId, EffectId, EffectRef, EffectSet, Ty};
use std::collections::HashMap;
use std::path::Path;
use std::rc::Rc;

pub use decls::DeclValue;
pub use untyped::Untyped;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ModuleId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct DeclId(pub u32);

/// A module: the `.gm` files directly inside one directory (§3.1), or a single
/// file given on the command line.
pub struct Module {
    pub name: Symbol,
    /// Top-level declarations by name.
    pub scope: HashMap<Symbol, DeclId>,
    /// True for the core library's modules (the prelude's public names are in
    /// the universe scope).
    pub is_prelude: bool,
    /// True for the standard library's modules (under the std root), which may use the
    /// system intrinsics.
    pub is_std: bool,
    /// The module's directory (`None` for the core library), for relative imports.
    pub dir: Option<std::path::PathBuf>,
    /// The package the module belongs to (an index into `Sema::packages`).
    pub package: Option<usize>,
    /// Top-level `when`s not yet decided.
    pub pending_whens: Vec<ast::Item>,
}

/// A top-level binding of some module.
pub struct Decl {
    pub name: ast::Ident,
    pub module: ModuleId,
    pub item: Rc<ast::Item>,
    pub state: DeclState,
    /// For an effect operation (§12.1): the effect's declaration and the operation index.
    pub op_of: Option<(DeclId, u32)>,
}

pub enum DeclState {
    Unresolved,
    InProgress,
    Done(DeclValue),
}

/// The signature of a function, known before its body is checked.
#[derive(Clone, Debug)]
pub struct FnSig {
    /// The module the function is declared in (where default arguments are resolved).
    pub module: ModuleId,
    pub params: Vec<ParamInfo>,
    pub ret: Ty,
    pub effects: EffectSet,
    /// The type parameters, in order (empty unless the function is generic, §16.1).
    pub generics: Vec<crate::types::ParamId>,
}

#[derive(Clone, Debug)]
pub struct ParamInfo {
    pub name: Symbol,
    pub ty: Ty,
    pub span: Span,
    /// A default argument, as a constant expression evaluated at each call.
    pub default: Option<Rc<ast::Expr>>,
    /// For a type parameter (`T: type`) or a compile-time value parameter
    /// (`N :: usize`): its identity in the signature's types.
    pub generic: Option<crate::types::ParamId>,
}

impl ParamInfo {
    /// A parameter defaulting to `@caller_location()`: it is bound only by name (§11.3).
    pub fn is_site(&self) -> bool {
        matches!(self.default.as_deref(), Some(ast::Expr { kind: ast::ExprKind::Intrinsic { name, .. }, .. }) if name.name.as_str() == "caller_location")
    }
}

/// An effect declaration (§12.1).
pub struct EffectInfo {
    pub name: Symbol,
    pub module: ModuleId,
    /// Type parameters of a generic effect (`Yield(T)`); operation types refer to them.
    pub params: Vec<crate::types::ParamId>,
    pub ops: Vec<OpInfo>,
}

#[derive(Clone, Debug)]
pub struct OpInfo {
    pub name: Symbol,
    pub kind: ast::OpKind,
    pub params: Vec<ParamInfo>,
    pub ret: Ty,
}

/// A nominal type as sema sees it (fields are resolved on demand).
pub struct AdtInfo {
    pub name: Symbol,
    pub module: ModuleId,
    /// For structs: each field's default value, if it has one.
    pub defaults: Vec<Option<tir::Expr>>,
    /// For structs: the first field is declared `using` (§5.10.3).
    pub has_using: bool,
}

/// A function body waiting to be checked.
struct PendingBody {
    fn_id: FnId,
    module: ModuleId,
    item: Rc<ast::Item>,
}

pub struct Sema<'s> {
    pub sess: &'s mut Session,
    pub target: Target,
    /// The build mode's name, as a variant of `target.Mode` (§1.4).
    pub build_mode: &'static str,
    /// While a parameter default is checked: the call's location and the calling
    /// function's name, for `@caller_location()`.
    pub caller_site: Option<(Span, Symbol)>,
    /// The step budget of compile-time evaluations (§17.3).
    pub fuel: u64,
    /// Set while resolving a foreign function's signature, which may be variadic.
    pub allow_variadic: bool,
    /// Foreign functions declared with a final `...` (§19.2).
    pub variadic_fns: std::collections::HashSet<FnId>,
    /// Whether `test` declarations are compiled (`gamma test`, §3.8).
    pub tests_enabled: bool,
    /// While a test body is checked (`expect` is allowed).
    pub in_test: bool,
    /// Test declarations of the root module, in source order.
    pub test_items: Vec<(ModuleId, Rc<ast::Item>)>,
    /// Top-level `@static_assert`s, checked after the declarations.
    pub static_asserts: Vec<(ModuleId, Rc<ast::Item>)>,
    /// Messages of `@static_assert`s deferred to each instance of a generic body.
    pub deferred_asserts: Vec<Option<String>>,
    pub modules: Vec<Module>,
    pub decls: Vec<Decl>,
    /// Uses of `#deprecated` declarations already warned about.
    deprecated_uses: std::collections::HashSet<(DeclId, Span)>,
    /// `#inline` functions, checked for recursion once every body is known.
    pub inline_fns: Vec<(FnId, Span)>,
    /// Generic nested functions (§11.8), declared like top-level ones, by name.
    pub nested_generic: HashMap<FnId, ast::Ident>,
    pub prog: Program,
    /// Signatures of functions in `prog.fns`.
    pub sigs: HashMap<FnId, FnSig>,
    pub effects: HashMap<EffectId, EffectInfo>,
    pub adts: HashMap<AdtId, AdtInfo>,
    pending_bodies: Vec<PendingBody>,
    /// Stack of declarations being resolved, for cycle reports.
    resolving: Vec<DeclId>,
    /// Structural type aliases being resolved a second time (see `resolve_decl`).
    alias_reentry: std::collections::HashSet<DeclId>,
    pub prelude: Option<ModuleId>,
    /// The core modules other than the prelude (`core/list`, `core/map`).
    pub core_modules: Vec<ModuleId>,
    /// Packages seen so far (§3.1).
    pub packages: Vec<imports::Package>,
    /// Loaded modules by canonical directory (§3.2).
    pub module_by_dir: HashMap<std::path::PathBuf, ModuleId>,
    /// Import edges `(importer, imported, location)`, for cycle detection.
    pub import_edges: Vec<(ModuleId, ModuleId, Span)>,
    /// Type parameters in scope while resolving types (innermost last).
    pub type_params: Vec<(Symbol, Ty)>,
    /// The capture-free nested functions in scope in the body being checked: like
    /// top-level functions, they may be compile-time arguments (§11.8).
    pub local_fns: Vec<(Symbol, FnId)>,
    /// Compile-time value parameters in scope (innermost last), by name (§16.5).
    pub value_scope: Vec<(Symbol, crate::types::ParamId)>,
    /// The type of each compile-time value parameter.
    pub value_params: HashMap<crate::types::ParamId, Ty>,
    /// The effect-set parameters (`E: effects`, §16.6); their names are in `type_params`.
    pub effect_params: std::collections::HashSet<crate::types::ParamId>,
    /// The predeclared `Yield` effect (§13.1).
    pub yield_effect: Option<EffectId>,
    /// Types and locations of clause-level `break`s of each handler literal,
    /// checked against the value of the `with` that installs it.
    pub handler_breaks: HashMap<u32, Vec<(Ty, Span)>>,
    /// The values of `break`s to the installing `with` in each clause of a handler
    /// declaration.
    pub decl_clause_breaks: HashMap<FnId, Vec<(Ty, Span)>>,
    /// Each `with` that installs handler declarations: their indices and its type.
    pub decl_with_checks: Vec<(Vec<usize>, Ty, Span)>,
    /// Handler declarations (§12.6) found while loading modules.
    handler_items: Vec<(ModuleId, Rc<ast::Item>)>,
    /// For each declared handler (indexed like `prog.handler_decls`): the effects
    /// its clauses may perform.
    pub handler_decl_effects: Vec<EffectSet>,
    /// Handler declarations for generic types, instantiated by `handlers_for` (§12.6).
    pub handler_templates: Vec<handler_decl::HandlerTemplate>,
    /// Their instances, by template and arguments.
    pub handler_instances: HashMap<(usize, Vec<Ty>), tir::HandlerDeclId>,
    /// Constraints on type parameters (§16.3).
    pub constraints: HashMap<crate::types::ParamId, Vec<generics::Constraint>>,
    /// Instantiations of generic functions, by generic function and type arguments.
    pub instances: HashMap<(FnId, Vec<Ty>), FnId>,
    /// The generic function and type arguments of each instantiation.
    pub instance_of: HashMap<FnId, (FnId, Vec<Ty>)>,
    /// Instantiations whose bodies are still to be produced.
    pub mono_queue: Vec<generics::MonoRequest>,
    /// The depth of the instantiation whose body is being produced.
    pub mono_depth: u32,
    /// Nested functions (literals, clauses) and the outermost function they belong to.
    pub nested_owner: HashMap<FnId, FnId>,
    /// Functions whose bodies are being checked (for compile-time evaluation cycles).
    pub checking: Vec<FnId>,
    /// Type parameters of generic structs and unions.
    pub adt_params: HashMap<AdtId, Vec<crate::types::ParamId>>,
    /// Unions declared with an explicit tag type (`union(u8) { … }`).
    pub explicit_tags: std::collections::HashSet<AdtId>,
    /// Default arguments of the parameters of generic structs and unions.
    pub adt_param_defaults: HashMap<AdtId, Vec<Option<Rc<ast::Expr>>>>,
    /// Instantiations of generic structs and unions.
    pub adt_instances: HashMap<(AdtId, Vec<Ty>), AdtId>,
    /// The literal of each generic struct or union, for errors found when instantiating it.
    pub adt_spans: HashMap<AdtId, Span>,
    /// Structs and unions whose fields are being resolved (their layout is not known).
    pub adts_in_progress: Vec<AdtId>,
    /// The declaration of each top-level struct and union.
    pub adt_decls: HashMap<AdtId, DeclId>,
    /// Signatures and generic types being declared, whose `where` clauses come after
    /// the types that mention their parameters.
    pub constraint_defer: u32,
    /// Constraints on type arguments to check when `constraint_defer` drops to 0.
    pub deferred_constraints: Vec<(Ty, generics::Constraint, crate::types::ParamId, AdtId, Span)>,
    /// How many generic type instantiations are in progress, one inside the other.
    pub adt_inst_depth: u32,
    /// Instances created during the outermost instantiation in progress, with their templates.
    pub adt_new_instances: Vec<(AdtId, Ty)>,
    /// Every array length computed from compile-time value parameters, with its
    /// span: checked for overflow at each instantiation (§16.5).
    pub len_exprs: Vec<(Ty, Span)>,
    /// Whether ever-deeper instantiation has been reported.
    pub adt_inst_overflowed: bool,
}

/// Analyzes the program whose root module is `root` (a `.gm` file or a
/// directory). Returns the lowered program if there were no errors.
pub fn analyze(sess: &mut Session, root: &Path) -> Option<Program> {
    analyze_with(sess, root, false)
}

/// Checks the program rooted at `root` for testing: its `test` declarations
/// are compiled (into [`Program::tests`]) and `main` is not required (§3.8).
pub fn analyze_tests(sess: &mut Session, root: &Path) -> Option<Program> {
    analyze_with(sess, root, true)
}

fn analyze_with(sess: &mut Session, root: &Path, tests: bool) -> Option<Program> {
    let mut sema = Sema::new(sess);
    sema.tests_enabled = tests;
    sema.load_prelude();
    let root_mod = sema.load_root(root)?;
    sema.check_all(root_mod);
    sema.check_inline();
    sema.check_escapes();
    sema.check_continuations();
    if sema.sess.diags.has_errors() {
        return None;
    }
    sema.prog.target = sema.target.clone();
    Some(sema.prog)
}

impl<'s> Sema<'s> {
    fn new(sess: &'s mut Session) -> Sema<'s> {
        let build_mode = match sess.mode {
            crate::interp::Mode::Debug => "Debug",
            crate::interp::Mode::Checked => "Checked",
            crate::interp::Mode::Safe => "Safe",
            crate::interp::Mode::Fast => "Fast",
        };
        let target = sess.target.clone();
        Sema {
            sess,
            target,
            build_mode,
            caller_site: None,
            fuel: consteval::STEP_BUDGET,
            allow_variadic: false,
            variadic_fns: std::collections::HashSet::new(),
            tests_enabled: false,
            in_test: false,
            test_items: vec![],
            static_asserts: vec![],
            deferred_asserts: vec![],
            modules: vec![],
            decls: vec![],
            deprecated_uses: std::collections::HashSet::new(),
            inline_fns: vec![],
            nested_generic: HashMap::new(),
            prog: Program::default(),
            sigs: HashMap::new(),
            effects: HashMap::new(),
            adts: HashMap::new(),
            pending_bodies: vec![],
            checking: vec![],
            resolving: vec![],
            alias_reentry: Default::default(),
            prelude: None,
            core_modules: vec![],
            packages: vec![],
            module_by_dir: HashMap::new(),
            import_edges: vec![],
            type_params: vec![],
            local_fns: vec![],
            value_scope: vec![],
            value_params: HashMap::new(),
            effect_params: Default::default(),
            yield_effect: None,
            handler_breaks: HashMap::new(),
            decl_clause_breaks: HashMap::new(),
            decl_with_checks: vec![],
            handler_items: vec![],
            handler_decl_effects: vec![],
            handler_templates: vec![],
            handler_instances: HashMap::new(),
            constraints: HashMap::new(),
            instances: HashMap::new(),
            instance_of: HashMap::new(),
            mono_queue: vec![],
            mono_depth: 0,
            nested_owner: HashMap::new(),
            adt_params: HashMap::new(),
            adt_param_defaults: HashMap::new(),
            explicit_tags: std::collections::HashSet::new(),
            adt_instances: HashMap::new(),
            adt_spans: HashMap::new(),
            adts_in_progress: vec![],
            adt_decls: HashMap::new(),
            constraint_defer: 0,
            deferred_constraints: vec![],
            adt_inst_depth: 0,
            adt_new_instances: vec![],
            len_exprs: vec![],
            adt_inst_overflowed: false,
        }
    }

    pub fn diags(&mut self) -> &mut Diagnostics {
        &mut self.sess.diags
    }

    pub fn error(&mut self, span: Span, msg: impl Into<String>) {
        self.sess.diags.error(span, msg);
    }

    pub fn emit(&mut self, d: Diagnostic) {
        self.sess.diags.push(d);
    }

    /// `#inline` cannot apply to a function that can call itself (§11.10).
    fn check_inline(&mut self) {
        for (f, span) in std::mem::take(&mut self.inline_fns) {
            let mut seen = std::collections::HashSet::new();
            let mut work = vec![f];
            let mut recursive = false;
            while let Some(g) = work.pop() {
                let Some(body) = &self.prog.fns[g.index()].body else { continue };
                fn calls(e: &tir::Expr, out: &mut Vec<FnId>) {
                    if let tir::ExprKind::Call { callee: tir::Callee::Fn(c), .. } = &e.kind {
                        out.push(*c);
                    }
                    e.for_each_child(&mut |x| calls(x, out));
                }
                let mut out = vec![];
                calls(body, &mut out);
                for c in out {
                    if c == f {
                        recursive = true;
                    } else if seen.insert(c) {
                        work.push(c);
                    }
                }
            }
            if recursive {
                let name = self.prog.fns[f.index()].name;
                self.error(
                    span,
                    format!("`{}` cannot be inlined at every call site: it can call itself (§11.10)", name),
                );
            }
        }
    }

    /// Warns about a use at `span` of declaration `d` if it is `#deprecated("msg")` (§20.2).
    pub fn check_deprecated(&mut self, d: DeclId, span: Span) {
        let decl = &self.decls[d.0 as usize];
        let Some(attr) = decl.item.attrs.iter().find(|a| a.name.name.as_str() == "deprecated") else { return };
        if decl.op_of.is_some() || !self.deprecated_uses.insert((d, span)) {
            return;
        }
        let msg = match attr.args.as_deref() {
            Some([ast::Expr { kind: ast::ExprKind::Str(s), .. }]) => format!(": {}", String::from_utf8_lossy(&s.bytes)),
            _ => String::new(),
        };
        let w = Diagnostic::warning(span, format!("`{}` is deprecated{}", decl.name.name, msg))
            .with_label(decl.name.span, "declared here");
        self.emit(w);
    }

    /// Resolves every declaration of the root module (and, transitively, what
    /// they use), checks all queued function bodies, and finds `main`.
    fn check_all(&mut self, root: ModuleId) {
        // Every declaration of every loaded module is checked; resolving imports
        // loads more modules.
        let mut done = 0;
        while done < self.modules.len() {
            let m = done;
            done += 1;
            if self.modules[m].is_prelude {
                continue;
            }
            let mut ids: Vec<DeclId> = self.modules[m].scope.values().copied().collect();
            ids.sort();
            for id in ids {
                self.resolve_decl(id);
            }
        }
        self.check_import_cycles();
        self.check_pub_signatures();
        let _ = root;
        // The core library is checked in full, so its errors surface even when unused.
        let core: Vec<ModuleId> = self.prelude.into_iter().chain(self.core_modules.iter().copied()).collect();
        for m in core {
            let mut ids: Vec<DeclId> = self.modules[m.0 as usize].scope.values().copied().collect();
            ids.sort();
            for id in ids {
                self.resolve_decl(id);
            }
        }
        for (module, item) in std::mem::take(&mut self.static_asserts) {
            if let ast::ItemKind::Intrinsic(e) = &item.kind
                && let ast::ExprKind::Intrinsic { name, args } = &e.kind
                && name.name.as_str() == "static_assert"
            {
                self.static_assert(module, args.as_deref().unwrap_or(&[]), e.span);
            } else if let ast::ItemKind::Intrinsic(e) = &item.kind
                && let ast::ExprKind::Intrinsic { name, args } = &e.kind
                && name.name.as_str() == "compile_error"
            {
                // In a taken `when` branch, typically (§3.6, §20.3).
                let msg = match args.as_deref().and_then(|a| a.first()).map(|a| &a.kind) {
                    Some(ast::ArgKind::Positional(m)) => self.const_string(module, m),
                    _ => None,
                };
                let msg = msg.unwrap_or_else(|| "`@compile_error` takes a constant string message".into());
                self.error(e.span, msg);
            } else {
                self.error(item.span, "only `@static_assert` may appear as a top-level declaration");
            }
        }
        let clauses = self.declare_handlers();
        if self.tests_enabled {
            self.check_tests(root);
        } else {
            self.find_main(root);
        }
        self.drain_bodies();
        self.check_handler_clauses(clauses);
        self.drain_bodies();
        self.check_decl_breaks();
        self.monomorphize();
    }

    /// Checks queued function bodies until none remain.
    pub fn drain_bodies(&mut self) {
        while let Some(p) = self.pending_bodies.pop() {
            self.check_fn_body(p.fn_id, p.module, &p.item);
        }
    }

    fn find_main(&mut self, root: ModuleId) {
        let main = Symbol::intern("main");
        let Some(&id) = self.modules[root.0 as usize].scope.get(&main) else {
            self.error(Span::DUMMY, "the root module of a program must declare `main`");
            return;
        };
        let span = self.decls[id.0 as usize].name.span;
        match self.resolve_decl(id) {
            DeclValue::Fn(f) => {
                let sig = self.sigs[&f].clone();
                if !sig.params.is_empty() {
                    self.error(span, "`main` must take no parameters");
                }
                let ret_ok = sig.ret.is_unit()
                    || sig.ret == Ty::u8()
                    || matches!(sig.ret.kind(), crate::types::TyKind::Result { ok, .. } if ok.is_unit());
                if !ret_ok {
                    self.error(span, format!("`main` must return `()`, `u8` or `E!()`, not `{}`", sig.ret));
                }
                for e in sig.effects.effects.iter() {
                    if !self.is_ambient(e) {
                        let name = e.effect.name();
                        self.error(span, format!("`main` may only perform ambient effects, but performs `{}`", name));
                    }
                }
                self.prog.main = Some(self.entry_point(f, &sig));
            }
            DeclValue::Error => {}
            _ => self.error(span, "`main` must be a function"),
        }
    }

    /// The function the runtime calls: `main`, wrapped in the core library's default
    /// handlers for the ambient effects it performs that the runtime does not serve
    /// directly: `Alloc` (the system heap) and `In`, `Clock`, `Rand` (§3.7, §12.9).
    fn entry_point(&mut self, main: FnId, sig: &FnSig) -> FnId {
        let a = self.prog.ambient.clone();
        let performs = |e: Option<EffectId>| e.is_some_and(|e| sig.effects.effects.iter().any(|r| r.effect == e));
        let mut providers = vec![];
        if performs(a.alloc) {
            providers.push("heap_allocator");
        }
        if performs(a.input) || performs(a.clock) || performs(a.rand) || performs(a.fs) || performs(a.net) {
            providers.push("system_io");
        }
        let span = self.prog.fns[main.index()].span;
        let mut handlers = vec![];
        let mut handled = vec![];
        for name in providers {
            let Some(&d) = self.prelude.and_then(|p| self.modules[p.0 as usize].scope.get(&Symbol::intern(name)))
            else {
                continue;
            };
            let DeclValue::Fn(f) = self.resolve_decl(d) else { continue };
            let ty = self.sigs[&f].ret;
            let decls = self.handlers_for(ty);
            for d in &decls {
                handled.push(self.prog.handler_decls[d.index()].effect.effect);
            }
            let value = tir::Expr { ty, kind: tir::ExprKind::Call { callee: tir::Callee::Fn(f), args: vec![] }, span };
            handlers.push(tir::HandlerValue::Decl { decls, value });
        }
        if handlers.is_empty() {
            return main;
        }
        let effects =
            EffectSet::new(sig.effects.effects.iter().filter(|e| !handled.contains(&e.effect)).cloned().collect());
        let entry_sig = FnSig { module: sig.module, params: vec![], ret: sig.ret, effects, generics: vec![] };
        let entry = self.new_fn(Symbol::intern("<entry>"), span, entry_sig, tir::FnKind::Named);
        let with = tir::Expr {
            ty: sig.ret,
            kind: tir::ExprKind::With {
                label: tir::LabelId(0),
                handlers,
                on: None,
                body: Box::new(tir::Expr {
                    ty: sig.ret,
                    kind: tir::ExprKind::Call { callee: tir::Callee::Fn(main), args: vec![] },
                    span,
                }),
            },
            span,
        };
        self.prog.fns[entry.index()].body = Some(with);
        entry
    }

    /// True if `e` is one of the ambient effects of §12.9.
    pub fn is_ambient(&self, e: &EffectRef) -> bool {
        let a = &self.prog.ambient;
        [a.out, a.err, a.env, a.alloc, a.sys, a.input, a.clock, a.rand, a.fs, a.net].contains(&Some(e.effect))
    }

    /// Layout computations over the program's nominal types.
    pub fn with_layouts<R>(&self, f: impl FnOnce(&Layouts) -> R) -> R {
        let adts = &self.prog.adts;
        let lookup = |id: AdtId| adts.get(&id);
        let layouts = Layouts::new(self.target.clone(), &lookup);
        f(&layouts)
    }

    /// Adds a function to the program with a placeholder body and returns its id.
    pub fn new_fn(&mut self, name: Symbol, span: Span, sig: FnSig, kind: tir::FnKind) -> FnId {
        let id = FnId(self.prog.fns.len() as u32);
        self.prog.fns.push(tir::FnDef {
            name,
            span,
            params: vec![],
            locals: vec![],
            ret: sig.ret,
            effects: sig.effects.clone(),
            parent: None,
            kind,
            callconv: Default::default(),
            hints: Default::default(),
            body: None,
        });
        self.sigs.insert(id, sig);
        id
    }
}
