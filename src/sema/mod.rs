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
mod construct;
mod decls;
mod effects;
mod exhaust;
mod expr;
mod finalize;
mod fnlit;
mod func;
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
}

#[derive(Clone, Debug)]
pub struct ParamInfo {
    pub name: Symbol,
    pub ty: Ty,
    pub span: Span,
    /// A default argument, as a constant expression evaluated at each call.
    pub default: Option<Rc<ast::Expr>>,
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
    /// Type parameters in scope while resolving types (innermost last).
    pub type_params: Vec<(Symbol, Ty)>,
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
            resolving: vec![],
            prelude: None,
            type_params: vec![],
            yield_effect: None,
            handler_breaks: HashMap::new(),
            handler_items: vec![],
            handler_decl_effects: vec![],
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
        let clauses = self.declare_handlers();
        self.drain_bodies();
        self.check_handler_clauses(clauses);
        self.drain_bodies();
        self.find_main(root);
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
                self.prog.main = Some(f);
            }
            DeclValue::Error => {}
            _ => self.error(span, "`main` must be a function"),
        }
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
