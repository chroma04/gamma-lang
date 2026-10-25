//! What `gamma lint` needs from the checker: the findings that depend on name resolution
//! and types (unused locals, parameters and declarations, locals that never change,
//! comparisons whose result is known). `crate::lint` turns them into diagnostics.

use super::{DeclId, DeclState, DeclValue, Sema};
use crate::driver::Session;
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast;
use crate::tir::{Callee, Expr, ExprKind, FnId, FnKind, LocalId, Stmt};
use crate::types::TyKind;
use std::collections::{HashMap, HashSet};
use std::path::Path;

/// One fact `gamma lint` reports, located in the program's own files.
#[derive(Clone, Debug)]
pub enum Finding {
    /// A local (`param`: a parameter of a named function) whose value is never read;
    /// `assigned` if it is assigned after its declaration.
    UnusedLocal { name: Symbol, span: Span, param: bool, assigned: bool },
    /// A mutable local (`x := e`) that is never assigned, nor borrowed or sliced mutably.
    NeverAssigned { name: Symbol, span: Span },
    /// A private top-level declaration that nothing refers to. `item` is the whole
    /// declaration; `what` says what it declares ("function", "import", …).
    UnusedDecl { name: Symbol, span: Span, item: Span, what: &'static str },
    /// `x is true` and its variants: `span` is the comparison, `operand` the other side,
    /// and `same` whether the comparison means `operand` itself (rather than its negation).
    BoolComparison { span: Span, operand: Span, same: bool },
    /// A comparison of a value with itself; `always` is its result.
    SelfComparison { span: Span, always: bool },
    /// `x = x`: `span` is the whole assignment.
    SelfAssignment { span: Span },
    /// A `pub` or `pub(package)` function or constant that no other module names, in a
    /// package where it could be private: any `pub(package)` one, and a `pub` one of a
    /// program package (whose root module declares `main`, so other packages do not import
    /// it).
    NeedlessPub { name: Symbol, span: Span, vis: ast::Visibility, what: &'static str },
    /// The address of local `name` (declared at `local`), taken at `span`, kept where it
    /// outlives the local: `why` is "stored", "passed to be stored" or "returned".
    DanglingAddress { span: Span, name: Symbol, local: Span, why: &'static str },
    /// A bare name below the top of a `match` arm's pattern, in the position of an
    /// optional (or of a result, if `is_result`), whose absent value (or error) no arm
    /// names, so that it goes to the catch-all arm at `catch_all`.
    PresentOnlyBinding { span: Span, name: Symbol, catch_all: Span, is_result: bool },
    /// Local `name` (declared at `local`) holds a copy of the place at `source`, which keeps
    /// its elements in a buffer it resizes, and is changed at `span` as if it were the
    /// original.
    MutatedCopy { span: Span, name: Symbol, local: Span, source: Span },
}

/// Checks the program rooted at `root` with its tests, and with the other modules of its
/// package if it is in one (a directory with `gamma.toml`), and returns the findings located in files for which `in_scope` holds, or `None` if the
/// program has errors (left in `sess.diags`).
pub fn analyze_for_lint(sess: &mut Session, root: &Path, in_scope: &dyn Fn(&Path) -> bool) -> Option<Vec<Finding>> {
    let mut sema = Sema::new(sess);
    sema.tests_enabled = true;
    sema.load_prelude();
    let root_mod = sema.load_root(root)?;
    // Outside a package, a directory's subdirectories are not its modules.
    if crate::pkg::find_root(root).is_some() {
        sema.load_package_modules(root_mod);
    }
    sema.check_all(root_mod);
    sema.check_inline();
    sema.check_escapes();
    sema.check_dangling_addresses();
    sema.check_continuations();
    if sema.sess.diags.has_errors() {
        return None;
    }
    // A program package: no other package imports it.
    let program = crate::pkg::find_root(root).is_some()
        && sema.modules[root_mod.0 as usize].scope.contains_key(&Symbol::intern("main"));
    let mut lint = Lint {
        sema: &sema,
        program,
        uses: HashMap::new(),
        fn_uses: HashSet::new(),
        as_value: HashSet::new(),
        bound: HashSet::new(),
        out: vec![],
    };
    lint.run(in_scope);
    Some(lint.out)
}

/// How a local is changed (its uses by name are in `Sema::local_uses`).
#[derive(Default)]
struct Use {
    /// The uses that are the whole place of an assignment.
    writes: HashSet<Span>,
    /// Assigned through a place rooted at it, or borrowed or sliced mutably.
    mutated: bool,
}

struct Lint<'a, 's> {
    sema: &'a Sema<'s>,
    /// Whether the root module is the root of a package and declares `main`.
    program: bool,
    /// By the function whose frame holds the local.
    uses: HashMap<(FnId, LocalId), Use>,
    /// Declarations of functions called or named by another declaration.
    fn_uses: HashSet<DeclId>,
    /// Functions used as values (so their signature is not theirs to choose).
    as_value: HashSet<FnId>,
    /// The locals that a statement or pattern binds. Checking allocates a local for each
    /// name of every alternative of `p1, p2 => …`, but binds those of the first only.
    bound: HashSet<(FnId, LocalId)>,
    out: Vec<Finding>,
}

impl Lint<'_, '_> {
    fn run(&mut self, in_scope: &dyn Fn(&Path) -> bool) {
        let sema = self.sema;
        let prog = &sema.prog;
        let scoped: HashSet<_> =
            sema.sess.sources.files().filter(|(_, f)| in_scope(&f.path)).map(|(id, _)| id).collect();
        let file_ok = |span: Span| span != Span::DUMMY && scoped.contains(&span.file);
        let mut fn_decl: HashMap<FnId, DeclId> = HashMap::new();
        for (i, d) in sema.decls.iter().enumerate() {
            if let DeclState::Done(DeclValue::Fn(f)) = &d.state {
                fn_decl.entry(*f).or_insert(DeclId(i as u32));
            }
        }
        // The function each literal or clause belongs to.
        let outer = |f: FnId| sema.nested_owner.get(&f).copied().unwrap_or(f);
        for (i, def) in prog.fns.iter().enumerate() {
            let f = FnId(i as u32);
            if sema.instance_of.contains_key(&f) {
                continue;
            }
            if let Some(body) = &def.body {
                let mut calls = vec![];
                self.visit(f, body, &mut calls);
                let me = fn_decl.get(&outer(f)).copied();
                for c in calls {
                    let c = sema.generic_of(c);
                    if let Some(&d) = fn_decl.get(&c)
                        && Some(d) != me
                    {
                        self.fn_uses.insert(d);
                    }
                }
            }
        }
        for c in prog.consts.iter().map(|c| &c.init).chain(prog.globals.iter().filter_map(|g| g.init.as_ref())) {
            let mut calls = vec![];
            self.visit(FnId(u32::MAX), c, &mut calls);
            for c in calls {
                if let Some(&d) = fn_decl.get(&sema.generic_of(c)) {
                    self.fn_uses.insert(d);
                }
            }
        }
        for &(_, f) in &prog.exports {
            self.as_value.insert(f);
        }
        // Locals and parameters.
        let mut named: HashMap<Span, HashSet<Span>> = HashMap::new();
        for &(decl, at) in &sema.local_uses {
            named.entry(decl).or_default().insert(at);
        }

        for (i, def) in prog.fns.iter().enumerate() {
            let f = FnId(i as u32);
            if sema.instance_of.contains_key(&f) || def.body.is_none() || !file_ok(def.span) {
                continue;
            }
            super::lint_escape::dangling_addresses(sema, def, &mut self.out);
            super::lint_copy::mutated_copies(sema, def, &mut self.out);
            let own_params = def.kind == FnKind::Named && !self.as_value.contains(&f) && def.name.as_str() != "main";
            for (j, l) in def.locals.iter().enumerate() {
                let name = l.name.as_str();
                if l.span == Span::DUMMY || name.starts_with('<') || name.starts_with('_') || !file_ok(l.span) {
                    continue;
                }
                let param = def.params.contains(&LocalId(j as u32));
                if param && !own_params || !param && !self.bound.contains(&(f, LocalId(j as u32))) {
                    continue;
                }
                let (writes, mutated) =
                    self.uses.get(&(f, LocalId(j as u32))).map_or((0, false), |u| (u.writes.len(), u.mutated));
                let uses = named.get(&l.span).map_or(0, |s| s.len());
                if uses == writes {
                    self.out.push(Finding::UnusedLocal { name: l.name, span: l.span, param, assigned: writes > 0 });
                } else if l.mutable && !param && !mutated && writes == 0 {
                    self.out.push(Finding::NeverAssigned { name: l.name, span: l.span });
                }
            }
        }
        // Top-level declarations.
        for (i, d) in sema.decls.iter().enumerate() {
            let id = DeclId(i as u32);
            let name = d.name.name.as_str();
            if let Some(what) = self.needless_pub(id, &file_ok) {
                self.out.push(Finding::NeedlessPub { name: d.name.name, span: d.name.span, vis: d.item.vis, what });
            }
            if d.item.vis == ast::Visibility::Public
                || d.op_of.is_some()
                || name == "main"
                || name.starts_with('_')
                || sema.modules[d.module.0 as usize].is_prelude
                || !file_ok(d.name.span)
                || sema.decl_uses.contains(&id)
                || self.fn_uses.contains(&id)
            {
                continue;
            }
            let ast::ItemKind::Binding(b) = &d.item.kind else { continue };
            if d.item.attrs.iter().any(|a| matches!(a.name.name.as_str(), "export" | "trap_hook")) {
                continue;
            }
            if !matches!(d.state, DeclState::Done(_)) {
                continue;
            }
            let what = match &b.init.kind {
                ast::ExprKind::Fn(_) => "function",
                ast::ExprKind::Import(_) => "import",
                ast::ExprKind::Foreign(_) => "foreign block",
                ast::ExprKind::Struct(_) | ast::ExprKind::Union(_) => "type",
                ast::ExprKind::Effect(_) => "effect",
                ast::ExprKind::Attributed { inner, .. }
                    if matches!(inner.kind, ast::ExprKind::Struct(_) | ast::ExprKind::Union(_)) =>
                {
                    "type"
                }
                _ => match &d.state {
                    DeclState::Done(DeclValue::Type(_) | DeclValue::GenericType(_)) => "type alias",
                    DeclState::Done(DeclValue::EffectSet(_) | DeclValue::EffectInst(_)) => "effect",
                    DeclState::Done(DeclValue::Global { .. }) => "global",
                    DeclState::Done(DeclValue::Module(_)) => "import",
                    _ => "constant",
                },
            };
            self.out.push(Finding::UnusedDecl { name: d.name.name, span: d.name.span, item: d.item.span, what });
        }
        self.out.retain(|f| match f {
            Finding::BoolComparison { span, .. }
            | Finding::SelfComparison { span, .. }
            | Finding::SelfAssignment { span } => file_ok(*span),
            _ => true,
        });
    }

    /// For a `pub` or `pub(package)` function or constant `d` that could be private (see
    /// [`Finding::NeedlessPub`]), what it declares.
    fn needless_pub(&self, d: DeclId, file_ok: &dyn Fn(Span) -> bool) -> Option<&'static str> {
        let sema = self.sema;
        let decl = &sema.decls[d.0 as usize];
        let name = decl.name.name.as_str();
        let candidate = match decl.item.vis {
            ast::Visibility::Private => false,
            ast::Visibility::Package => true,
            ast::Visibility::Public => self.program,
        };
        if !candidate
            || decl.op_of.is_some()
            || name == "main"
            || sema.modules[decl.module.0 as usize].is_prelude
            || !file_ok(decl.name.span)
            || sema.outside_uses.contains(&d)
            || decl.item.attrs.iter().any(|a| matches!(a.name.name.as_str(), "export" | "trap_hook"))
        {
            return None;
        }
        let ast::ItemKind::Binding(b) = &decl.item.kind else { return None };
        match (&b.init.kind, &decl.state) {
            (ast::ExprKind::Fn(_), DeclState::Done(DeclValue::Fn(_))) => Some("function"),
            (_, DeclState::Done(DeclValue::Const { .. } | DeclValue::Untyped(_))) => Some("constant"),
            _ => None,
        }
    }

    /// The function whose frame holds the local that an upvar `levels` frames up from `f`
    /// names.
    fn frame_up(&self, mut f: FnId, levels: u32) -> Option<FnId> {
        for _ in 0..levels {
            f = self.sema.prog.fns.get(f.index())?.parent?;
        }
        Some(f)
    }

    /// The local a place expression in function `f` is rooted at, through fields and
    /// array elements (not through pointers or slices, which name other storage).
    fn root(&self, f: FnId, e: &Expr) -> Option<(FnId, LocalId)> {
        match &e.kind {
            ExprKind::Local(l) => Some((f, *l)),
            ExprKind::Upvar { levels, local } => Some((self.frame_up(f, *levels)?, *local)),
            ExprKind::Field { base, .. } | ExprKind::VariantPayload { base, .. } => self.root(f, base),
            ExprKind::Index { base, .. } | ExprKind::Slice { base, .. }
                if matches!(base.ty.kind(), TyKind::Array { .. } | TyKind::ParamArray { .. }) =>
            {
                self.root(f, base)
            }
            _ => None,
        }
    }

    fn mutate(&mut self, f: FnId, place: &Expr) {
        if let Some(k) = self.root(f, place) {
            self.uses.entry(k).or_default().mutated = true;
        }
    }

    /// Records the uses of locals in `e` (an expression of function `f`), and the
    /// functions it calls or names in `calls`.
    fn visit(&mut self, f: FnId, e: &Expr, calls: &mut Vec<FnId>) {
        match &e.kind {
            ExprKind::AddrOf(p) if matches!(e.ty.kind(), TyKind::Ptr { is_const: false, .. }) => self.mutate(f, p),
            ExprKind::Slice { base, .. } if matches!(e.ty.kind(), TyKind::Slice { is_const: false, .. }) => {
                self.mutate(f, base)
            }
            ExprKind::Call { callee: Callee::Fn(c), .. } => calls.push(*c),
            ExprKind::FnRef(c) | ExprKind::Closure(c) => {
                calls.push(*c);
                self.as_value.insert(self.sema.generic_of(*c));
            }
            ExprKind::Block(b) => self.block_stmts(f, &b.stmts),
            ExprKind::Match { arms, .. } => {
                for a in arms {
                    self.binds(f, &a.pat);
                }
                if let Some(def) = self.sema.prog.fns.get(f.index()) {
                    super::lint_present::present_only_bindings(def, arms, &mut self.out);
                }
            }
            ExprKind::If { cond, .. } | ExprKind::Loop { cond, .. } => {
                for c in cond {
                    if let crate::tir::CondPart::Match { pat, .. } = c {
                        self.binds(f, pat);
                    }
                }
            }
            ExprKind::Else { capture: Some(l), .. } => {
                self.bound.insert((f, *l));
            }
            ExprKind::Eq { lhs, rhs, negated } => {
                let bool_lit = |x: &Expr| match x.kind {
                    ExprKind::Bool(b) => Some(b),
                    _ => None,
                };
                if let (Some(b), None) | (None, Some(b)) = (bool_lit(lhs), bool_lit(rhs)) {
                    let other = if bool_lit(lhs).is_some() { rhs } else { lhs };
                    if other.ty.is_bool() && e.span != Span::DUMMY {
                        self.out.push(Finding::BoolComparison {
                            span: e.span,
                            operand: other.span,
                            same: b != *negated,
                        });
                    }
                } else if self.same_value(lhs, rhs) {
                    self.out.push(Finding::SelfComparison { span: e.span, always: !negated });
                }
            }
            ExprKind::Binary { op, lhs, rhs } => {
                use crate::tir::BinOp;
                if matches!(op, BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge) && self.same_value(lhs, rhs) {
                    self.out
                        .push(Finding::SelfComparison { span: e.span, always: matches!(op, BinOp::Le | BinOp::Ge) });
                }
            }
            _ => {}
        }
        if let ExprKind::Loop { body, .. } = &e.kind {
            self.block_stmts(f, &body.stmts);
        }
        // The clauses of handler literals are functions of their own, visited by `run`.
        e.for_each_child(&mut |c| self.visit(f, c, calls));
    }

    /// Records the locals that pattern `p` binds.
    fn binds(&mut self, f: FnId, p: &crate::tir::Pat) {
        use crate::tir::PatKind;
        match &p.kind {
            PatKind::Bind(l) => {
                self.bound.insert((f, *l));
            }
            PatKind::Some(q) | PatKind::Ok(q) | PatKind::Err(q) => self.binds(f, q),
            PatKind::Variant { payload: Some(q), .. } => self.binds(f, q),
            PatKind::Fields(fs) => fs.iter().for_each(|(_, q)| self.binds(f, q)),
            PatKind::Or(alts) => alts.iter().for_each(|q| self.binds(f, q)),
            _ => {}
        }
    }

    /// Records the assignments among `stmts` (their sub-expressions are visited by
    /// `visit`).
    fn block_stmts(&mut self, f: FnId, stmts: &[Stmt]) {
        for s in stmts {
            match s {
                Stmt::Let { local, .. } => {
                    self.bound.insert((f, *local));
                }
                Stmt::LetTuple { locals, .. } => {
                    self.bound.extend(locals.iter().flatten().map(|l| (f, *l)));
                }
                Stmt::Assign { place, value } => {
                    self.assigned(f, place);
                    if self.same_value(place, value) && place.span != Span::DUMMY && value.span != Span::DUMMY {
                        self.out.push(Finding::SelfAssignment { span: place.span.to(value.span) });
                    }
                }
                Stmt::AssignTuple { places, .. } => {
                    for p in places.iter().flatten() {
                        self.assigned(f, p);
                    }
                }
                _ => {}
            }
        }
    }

    fn assigned(&mut self, f: FnId, place: &Expr) {
        self.mutate(f, place);
        if matches!(place.kind, ExprKind::Local(_) | ExprKind::Upvar { .. })
            && let Some(k) = self.root(f, place)
        {
            self.uses.entry(k).or_default().writes.insert(place.span);
        }
    }

    /// Whether `a` and `b` are the same path of locals, fields and constant indices, of a
    /// type whose values always equal themselves (no floats).
    fn same_value(&self, a: &Expr, b: &Expr) -> bool {
        fn path_eq(a: &Expr, b: &Expr) -> bool {
            match (&a.kind, &b.kind) {
                (ExprKind::Local(x), ExprKind::Local(y)) => x == y,
                (ExprKind::Upvar { levels: l1, local: x }, ExprKind::Upvar { levels: l2, local: y }) => {
                    l1 == l2 && x == y
                }
                (ExprKind::Global(x), ExprKind::Global(y)) => x == y,
                (ExprKind::Field { base: b1, index: i1 }, ExprKind::Field { base: b2, index: i2 }) => {
                    i1 == i2 && path_eq(b1, b2)
                }
                (ExprKind::Deref(x), ExprKind::Deref(y)) => path_eq(x, y),
                (ExprKind::Index { base: b1, index: i1, .. }, ExprKind::Index { base: b2, index: i2, .. }) => {
                    path_eq(b1, b2)
                        && match (&i1.kind, &i2.kind) {
                            (ExprKind::Int(x), ExprKind::Int(y)) => x == y,
                            _ => path_eq(i1, i2),
                        }
                }
                _ => false,
            }
        }
        let ty = a.ty;
        let plain = ty.is_integer() || ty.is_bool() || matches!(ty.kind(), TyKind::Ptr { .. } | TyKind::ManyPtr { .. });
        plain && a.span != b.span && path_eq(a, b)
    }
}

impl<'s> Sema<'s> {
    /// Checks an `#allow("rule", …)` attribute (§20.2): its arguments name lint rules.
    pub(super) fn check_allow_attr(&mut self, attr: &ast::Attribute) {
        let args = attr.args.as_deref().unwrap_or(&[]);
        if args.is_empty() {
            self.error(attr.span, "`#allow` takes the names of lint rules: `#allow(\"unused-parameter\")`");
        }
        for a in args {
            match &a.kind {
                // Which names are rules is the lint tool's business, not the program's
                // (§20.2): the tool reports unknown ones.
                ast::ExprKind::Str(_) => {}
                _ => self.error(a.span, "`#allow` takes the names of lint rules as string literals"),
            }
        }
    }
}
