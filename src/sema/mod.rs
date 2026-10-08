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
mod decls;
mod effects;
mod exhaust;
mod expr;
mod finalize;
mod fnlit;
mod func;
mod generics;
mod handler_decl;
mod infer;
mod matching;
mod methods;
mod modules;
mod pattern;
mod seq;
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
    /// True for the core prelude, whose public names are in the universe scope.
    pub is_prelude: bool,
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
    pub modules: Vec<Module>,
    pub decls: Vec<Decl>,
    pub prog: Program,
    /// Signatures of functions in `prog.fns`.
    pub sigs: HashMap<FnId, FnSig>,
    pub effects: HashMap<EffectId, EffectInfo>,
    pub adts: HashMap<AdtId, AdtInfo>,
    pending_bodies: Vec<PendingBody>,
    /// Stack of declarations being resolved, for cycle reports.
    resolving: Vec<DeclId>,
    pub prelude: Option<ModuleId>,
    /// The core modules other than the prelude (`core/list`, `core/map`).
    pub core_modules: Vec<ModuleId>,
    /// Type parameters in scope while resolving types (innermost last).
    pub type_params: Vec<(Symbol, Ty)>,
    /// Compile-time value parameters in scope (innermost last), by name (§16.5).
    pub value_scope: Vec<(Symbol, crate::types::ParamId)>,
    /// The type of each compile-time value parameter.
    pub value_params: HashMap<crate::types::ParamId, Ty>,
    /// The predeclared `Yield` effect (§13.1).
    pub yield_effect: Option<EffectId>,
    /// Types and locations of clause-level `break`s of each handler literal,
    /// checked against the value of the `with` that installs it.
    pub handler_breaks: HashMap<u32, Vec<(Ty, Span)>>,
    /// Handler declarations (§12.6) found while loading modules.
    handler_items: Vec<(ModuleId, Rc<ast::Item>)>,
    /// For each declared handler (indexed like `prog.handler_decls`): the effects
    /// its clauses may perform.
    pub handler_decl_effects: Vec<EffectSet>,
    /// Constraints on type parameters (§16.3).
    pub constraints: HashMap<crate::types::ParamId, Vec<generics::Constraint>>,
    /// Instantiations of generic functions, by generic function and type arguments.
    pub instances: HashMap<(FnId, Vec<Ty>), FnId>,
    /// The generic function and type arguments of each instantiation.
    pub instance_of: HashMap<FnId, (FnId, Vec<Ty>)>,
    /// Instantiations whose bodies are still to be produced.
    pub mono_queue: Vec<generics::MonoRequest>,
    /// Nested functions (literals, clauses) and the outermost function they belong to.
    pub nested_owner: HashMap<FnId, FnId>,
    /// Functions whose bodies are being checked (for compile-time evaluation cycles).
    pub checking: Vec<FnId>,
    /// Type parameters of generic structs and unions.
    pub adt_params: HashMap<AdtId, Vec<crate::types::ParamId>>,
    /// Default arguments of the parameters of generic structs and unions.
    pub adt_param_defaults: HashMap<AdtId, Vec<Option<Rc<ast::Expr>>>>,
    /// Instantiations of generic structs and unions.
    pub adt_instances: HashMap<(AdtId, Vec<Ty>), AdtId>,
}

/// Analyzes the program whose root module is `root` (a `.gm` file or a
/// directory). Returns the lowered program if there were no errors.
pub fn analyze(sess: &mut Session, root: &Path) -> Option<Program> {
    let mut sema = Sema::new(sess);
    sema.load_prelude();
    let root_mod = sema.load_root(root)?;
    sema.check_all(root_mod);
    if sema.sess.diags.has_errors() {
        return None;
    }
    Some(sema.prog)
}

impl<'s> Sema<'s> {
    fn new(sess: &'s mut Session) -> Sema<'s> {
        Sema {
            sess,
            target: Target::host(),
            modules: vec![],
            decls: vec![],
            prog: Program::default(),
            sigs: HashMap::new(),
            effects: HashMap::new(),
            adts: HashMap::new(),
            pending_bodies: vec![],
            checking: vec![],
            resolving: vec![],
            prelude: None,
            core_modules: vec![],
            type_params: vec![],
            value_scope: vec![],
            value_params: HashMap::new(),
            yield_effect: None,
            handler_breaks: HashMap::new(),
            handler_items: vec![],
            handler_decl_effects: vec![],
            constraints: HashMap::new(),
            instances: HashMap::new(),
            instance_of: HashMap::new(),
            mono_queue: vec![],
            nested_owner: HashMap::new(),
            adt_params: HashMap::new(),
            adt_param_defaults: HashMap::new(),
            adt_instances: HashMap::new(),
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

    /// Resolves every declaration of the root module (and, transitively, what
    /// they use), checks all queued function bodies, and finds `main`.
    fn check_all(&mut self, root: ModuleId) {
        let mut ids: Vec<DeclId> = self.modules[root.0 as usize].scope.values().copied().collect();
        ids.sort();
        for id in ids {
            self.resolve_decl(id);
        }
        // The core library is checked in full, so its errors surface even when unused.
        let core: Vec<ModuleId> = self.prelude.into_iter().chain(self.core_modules.iter().copied()).collect();
        for m in core {
            let mut ids: Vec<DeclId> = self.modules[m.0 as usize].scope.values().copied().collect();
            ids.sort();
            for id in ids {
                self.resolve_decl(id);
            }
        }
        let clauses = self.declare_handlers();
        self.find_main(root);
        self.drain_bodies();
        self.check_handler_clauses(clauses);
        self.drain_bodies();
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

    /// The function the runtime calls: `main`, wrapped so that `Alloc` is handled
    /// by the system heap when `main` performs it (§3.7, §12.9).
    fn entry_point(&mut self, main: FnId, sig: &FnSig) -> FnId {
        let Some(alloc) = self.prog.ambient.alloc else { return main };
        if !sig.effects.effects.iter().any(|e| e.effect == alloc) {
            return main;
        }
        let Some(&heap) =
            self.prelude.and_then(|p| self.modules[p.0 as usize].scope.get(&Symbol::intern("heap_allocator")))
        else {
            return main;
        };
        let DeclValue::Fn(heap) = self.resolve_decl(heap) else { return main };
        let allocator = self.sigs[&heap].ret;
        let decls = self.handlers_for(allocator);
        let span = self.prog.fns[main.index()].span;
        let effects = EffectSet::new(sig.effects.effects.iter().filter(|e| e.effect != alloc).cloned().collect());
        let entry_sig =
            FnSig { module: sig.module, params: vec![], ret: sig.ret, effects: effects.clone(), generics: vec![] };
        let entry = self.new_fn(Symbol::intern("<entry>"), span, entry_sig, tir::FnKind::Named);
        let call = |f: FnId, ty: Ty| tir::Expr {
            ty,
            kind: tir::ExprKind::Call { callee: tir::Callee::Fn(f), args: vec![] },
            span,
        };
        let with = tir::Expr {
            ty: sig.ret,
            kind: tir::ExprKind::With {
                label: tir::LabelId(0),
                handlers: vec![tir::HandlerValue::Decl { decls, value: call(heap, allocator) }],
                on: None,
                body: Box::new(call(main, sig.ret)),
            },
            span,
        };
        self.prog.fns[entry.index()].body = Some(with);
        entry
    }

    /// True if `e` is one of the ambient effects of §12.9.
    pub fn is_ambient(&self, e: &EffectRef) -> bool {
        let a = &self.prog.ambient;
        [a.out, a.err, a.env, a.alloc].contains(&Some(e.effect))
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
            body: None,
        });
        self.sigs.insert(id, sig);
        id
    }
}
