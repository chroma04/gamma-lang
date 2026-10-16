//! A walk over the syntax tree of the analyzed module that resolves names the way the
//! checker does, recording a reference for each use and the visible locals at each point.
//!
//! The checker's own tables answer the questions that need types (which function a call
//! resolves to, the type of a receiver); this walk answers the rest from the declarations
//! of the module and from its own lexical scopes.

use super::facts::Facts;
use crate::ide::{Hover, IdeIndex, Item, Kind, LocalScope, Ref};
use crate::sema::{DeclId, DeclValue, ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ArgKind, CondPart, ExprKind, StmtKind};
use crate::types::{Ty, TyKind};
use std::collections::{HashMap, HashSet};
use std::rc::Rc;

pub(super) struct Scope {
    names: Vec<(Symbol, Span)>,
    /// Locals declared here are visible up to this offset.
    end: u32,
}

/// What a name written as the base of `.` denotes.
pub(super) enum Base {
    Module(ModuleId),
    /// A struct or union type, by its declaration.
    Type(DeclId),
    Effect(DeclId),
    Value,
}

pub(super) struct Walker<'a, 's> {
    pub sema: &'a mut Sema<'s>,
    pub facts: &'a Facts,
    pub idx: &'a mut IdeIndex,
    pub module: ModuleId,
    pub scopes: Vec<Scope>,
    /// The labels of the enclosing loops and `with`s.
    labels: Vec<(Symbol, Span)>,
    /// Declarations whose hover text is registered.
    pub hovered: HashSet<DeclId>,
    pub receivers: HashMap<DeclId, Rc<Vec<Item>>>,
    /// The declaration of each top-level function.
    fn_decls: HashMap<crate::tir::FnId, DeclId>,
}

impl<'a, 's> Walker<'a, 's> {
    pub fn new(sema: &'a mut Sema<'s>, facts: &'a Facts, idx: &'a mut IdeIndex, module: ModuleId) -> Self {
        let mut fn_decls = HashMap::new();
        for i in 0..sema.decls.len() {
            if let Some(DeclValue::Fn(f)) = sema.decl_done(DeclId(i as u32)) {
                fn_decls.entry(f).or_insert(DeclId(i as u32));
            }
        }
        Walker {
            sema,
            facts,
            idx,
            module,
            scopes: vec![],
            hovered: HashSet::new(),
            receivers: HashMap::new(),
            fn_decls,
            labels: vec![],
        }
    }

    // --- scopes ---

    pub fn push_scope(&mut self, end: u32) {
        self.scopes.push(Scope { names: vec![], end });
    }

    pub fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    fn lookup_local(&self, name: Symbol) -> Option<Span> {
        self.scopes.iter().rev().flat_map(|s| s.names.iter().rev()).find(|(n, _)| *n == name).map(|(_, s)| *s)
    }

    /// Declares a local that is visible from offset `from` to the end of the scope.
    pub fn declare(&mut self, id: ast::Ident, kind: Kind, from: u32) {
        if id.name.as_str() == "_" || self.scopes.is_empty() {
            return;
        }
        let end = self.scopes.last().map_or(u32::MAX, |s| s.end);
        self.scopes.last_mut().unwrap().names.push((id.name, id.span));
        let (code, detail) = match self.facts.locals.get(&id.span) {
            Some((n, ty, _)) => (format!("{}: {}", n, ty), ty.to_string()),
            None => (id.name.to_string(), String::new()),
        };
        self.idx.defs.entry(id.span).or_insert(Hover { code, doc: None });
        let item = Item { label: id.name.to_string(), kind, detail, doc: None };
        self.idx.scopes.push(LocalScope { item, def: id.span, from, to: end });
    }

    // --- references ---

    pub fn add_ref(&mut self, from: Span, to: Span) {
        if from != Span::DUMMY && to != Span::DUMMY {
            self.idx.refs.push(Ref { from, to });
        }
    }

    pub fn ensure_hover(&mut self, d: DeclId) {
        if self.hovered.insert(d) {
            let span = self.sema.decls[d.0 as usize].name.span;
            if span != Span::DUMMY {
                let (_, hover) = self.sema.decl_info(d);
                self.idx.defs.entry(span).or_insert(hover);
            }
        }
    }

    pub fn use_decl(&mut self, from: Span, d: DeclId) {
        let to = self.sema.decls[d.0 as usize].name.span;
        self.add_ref(from, to);
        self.ensure_hover(d);
        self.note_receiver(from, d);
    }

    /// Remembers the members that `.` offers after the name at `span`, which denotes `d`.
    fn note_receiver(&mut self, span: Span, d: DeclId) {
        if let Some(r) = self.receivers.get(&d) {
            self.idx.receivers.insert(span, r.clone());
            return;
        }
        let items = match self.sema.decl_done(d) {
            Some(DeclValue::Module(m)) => self.sema.module_members(m),
            Some(DeclValue::Type(t)) => self.sema.variant_members(t),
            Some(DeclValue::Effect(_)) => self.sema.operation_members(d),
            _ => return,
        };
        let items = Rc::new(items);
        self.receivers.insert(d, items.clone());
        self.idx.receivers.insert(span, items);
    }

    fn lookup_decl(&mut self, name: Symbol) -> Option<DeclId> {
        let m = self.module;
        self.sema.lookup_module_name(m, name)
    }

    /// Resolves the name `id`, used as a value or a type.
    pub fn name_use(&mut self, id: ast::Ident) {
        if let Some(def) = self.lookup_local(id.name) {
            self.add_ref(id.span, def);
        } else if let Some(d) = self.lookup_decl(id.name) {
            self.use_decl(id.span, d);
        }
    }

    /// What `e`, written before a `.`, denotes.
    pub fn classify(&mut self, e: &ast::Expr) -> Base {
        match &e.kind {
            ExprKind::Paren(inner) => self.classify(inner),
            ExprKind::Ident(id) => {
                if self.lookup_local(id.name).is_some() {
                    return Base::Value;
                }
                match self.lookup_decl(id.name) {
                    Some(d) => self.base_of(d),
                    None => Base::Value,
                }
            }
            ExprKind::Field { base, name } => match self.classify(base) {
                Base::Module(m) => match self.sema.modules[m.0 as usize].scope.get(&name.name).copied() {
                    Some(d) => self.base_of(d),
                    None => Base::Value,
                },
                _ => Base::Value,
            },
            // `List(i32).new()`: a generic type applied to arguments.
            ExprKind::Call { callee, .. } => match self.classify(callee) {
                Base::Type(d) => Base::Type(d),
                _ => Base::Value,
            },
            _ => Base::Value,
        }
    }

    fn base_of(&self, d: DeclId) -> Base {
        match self.sema.decl_done(d) {
            Some(DeclValue::Module(m)) => Base::Module(m),
            Some(DeclValue::Type(_) | DeclValue::GenericType(_)) => Base::Type(d),
            Some(DeclValue::Effect(_)) => Base::Effect(d),
            _ => Base::Value,
        }
    }

    // --- members ---

    /// The span of the field, variant or operation `name` declared by type or effect `d`;
    /// registers its hover text.
    pub fn member_def(&mut self, d: DeclId, name: Symbol) -> Option<Span> {
        let item = self.sema.decls[d.0 as usize].item.clone();
        let owner = self.sema.decls[d.0 as usize].name.name;
        let ast::ItemKind::Binding(b) = &item.kind else { return None };
        let mut found: Option<(Span, Span, bool)> = None;
        match &b.init.kind {
            ExprKind::Struct(lit) => find_field(&lit.members, name, &mut found),
            ExprKind::Union(lit) => find_variant(&lit.members, name, &mut found),
            ExprKind::Attributed { inner, .. } => match &inner.kind {
                ExprKind::Struct(lit) => find_field(&lit.members, name, &mut found),
                ExprKind::Union(lit) => find_variant(&lit.members, name, &mut found),
                _ => {}
            },
            _ => {}
        }
        let (name_span, whole, is_variant) = found?;
        let text = self.sema.src(whole);
        let code = if is_variant { format!("{}.{}", owner, text.trim()) } else { text.trim().to_string() };
        self.idx.defs.entry(name_span).or_insert(Hover { code, doc: None });
        Some(name_span)
    }

    /// The field `name` of the struct type `ty` (through pointers).
    fn field_use(&mut self, ty: Ty, name: ast::Ident) -> bool {
        let Some(d) = self.sema.adt_decl_of(ty) else { return false };
        if let Some(to) = self.member_def(d, name.name) {
            self.add_ref(name.span, to);
            return true;
        }
        // A field reached through an embedded first field (`using`, §5.10.3).
        let mut t = ty;
        while let TyKind::Ptr { pointee, .. } = t.kind() {
            t = *pointee;
        }
        if let TyKind::Adt(id, _) = t.kind()
            && self.sema.adts.get(id).is_some_and(|a| a.has_using)
            && let Some(crate::tir::AdtKind::Struct { fields, .. }) = self.sema.prog.adts.get(id).map(|a| &a.kind)
            && let Some(first) = fields.first()
        {
            return self.field_use(first.ty, name);
        }
        false
    }

    /// The variant `name` of one of the union types of the expressions at `span`.
    fn variant_at(&mut self, span: Span, name: ast::Ident) -> Option<DeclId> {
        let tys = self.facts.all_types.get(&span).cloned().unwrap_or_default();
        tys.into_iter().find_map(|t| self.variant_use(t, name))
    }

    /// The variant `name` of the union type `ty`, seen through optionals and results.
    fn variant_use(&mut self, ty: Ty, name: ast::Ident) -> Option<DeclId> {
        let mut t = ty;
        loop {
            match t.kind() {
                TyKind::Optional(i) => t = *i,
                TyKind::Result { ok, .. } => t = *ok,
                TyKind::Ptr { pointee, .. } => t = *pointee,
                _ => break,
            }
        }
        if let Some(d) = self.sema.adt_decl_of(t)
            && let Some(to) = self.member_def(d, name.name)
        {
            self.add_ref(name.span, to);
            return Some(d);
        }
        None
    }

    /// The name `field` in the tuple payload of variant `variant` of union `d`.
    fn payload_field(&mut self, d: DeclId, variant: Symbol, field: ast::Ident) {
        let item = self.sema.decls[d.0 as usize].item.clone();
        let ast::ItemKind::Binding(b) = &item.kind else { return };
        let ExprKind::Union(lit) = &b.init.kind else { return };
        let mut found = None;
        find_payload_field(&lit.members, variant, field.name, &mut found);
        if let Some(to) = found {
            self.add_ref(field.span, to);
        }
    }

    fn type_of(&self, span: Span) -> Option<Ty> {
        self.facts.types.get(&span).map(|t| t.ty)
    }

    // --- expressions ---

    pub fn expr(&mut self, e: &ast::Expr) {
        match &e.kind {
            ExprKind::Int(_)
            | ExprKind::Float(_)
            | ExprKind::Str(_)
            | ExprKind::Bool(_)
            | ExprKind::Null
            | ExprKind::Uninit
            | ExprKind::Underscore
            | ExprKind::Error => {}
            ExprKind::Continue { label } => self.label_use(*label),
            ExprKind::Ident(id) => self.name_use(*id),
            ExprKind::DotIdent(id) => {
                self.variant_at(e.span, *id);
            }
            ExprKind::Paren(i)
            | ExprKind::OptUnwrap(i)
            | ExprKind::ResUnwrap(i)
            | ExprKind::Deref(i)
            | ExprKind::OptionalType(i)
            | ExprKind::ContType(i) => self.expr(i),
            ExprKind::Unary { operand, .. } => self.expr(operand),
            ExprKind::PtrType { inner, .. }
            | ExprKind::SliceType { inner, .. }
            | ExprKind::ManyPtrType { inner, .. }
            | ExprKind::Attributed { inner, .. } => self.expr(inner),
            ExprKind::Tuple(elems) => elems.iter().for_each(|el| self.expr(&el.value)),
            ExprKind::Binary { lhs, rhs, .. }
            | ExprKind::Bang { lhs, rhs }
            | ExprKind::ErrorUnionType { err: lhs, ok: rhs } => {
                self.expr(lhs);
                self.expr(rhs);
            }
            ExprKind::ArrayType { len, elem } => {
                if let Some(l) = len {
                    self.expr(l);
                }
                self.expr(elem);
            }
            ExprKind::Else { lhs, capture, rhs } => {
                self.expr(lhs);
                self.push_scope(rhs.span.hi);
                if let Some(c) = capture {
                    self.declare(*c, Kind::Variable, c.span.hi);
                }
                self.expr(rhs);
                self.pop_scope();
            }
            ExprKind::Index { base, index } => {
                self.expr(base);
                self.expr(index);
            }
            ExprKind::Slice { base, lo, hi, .. } => {
                self.expr(base);
                lo.iter().chain(hi.iter()).for_each(|x| self.expr(x));
            }
            ExprKind::TupleField { base, .. } => self.expr(base),
            ExprKind::Field { base, name } => self.field(e, base, *name),
            ExprKind::Call { callee, args, .. } => self.call(e, callee, args),
            ExprKind::FnType(sig) => self.sig(sig),
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Effect(_) => self.type_literal(e),
            ExprKind::Fn(lit) => self.fn_literal(lit),
            ExprKind::Handler(h) => self.handler_literal(h),
            ExprKind::Import(_) => {}
            ExprKind::Foreign(f) => {
                for m in &f.members {
                    match &m.kind {
                        ast::ForeignMemberKind::Fn { sig, .. } => self.sig(sig),
                        ast::ForeignMemberKind::Global { ty, .. } => self.expr(ty),
                    }
                }
            }
            ExprKind::If(i) => self.if_expr(i),
            ExprKind::Match(m) => self.match_expr(m),
            ExprKind::With(w) => {
                w.handlers.iter().for_each(|h| self.expr(h));
                if let Some(on) = &w.on {
                    self.expr(on);
                }
                self.with_label(w.label, |this| this.block(&w.body));
            }
            ExprKind::For(f) => self.for_expr(f),
            ExprKind::Block(b) => self.block(b),
            ExprKind::Return(v) => {
                if let Some(v) = v {
                    self.expr(v);
                }
            }
            ExprKind::Break { label, value } => {
                self.label_use(*label);
                if let Some(v) = value {
                    self.expr(v);
                }
            }
            ExprKind::Resume { cont, value } => {
                self.expr(cont);
                if let Some(v) = value {
                    self.expr(v);
                }
            }
            ExprKind::Intrinsic { args, .. } => {
                for a in args.iter().flatten() {
                    self.arg(a);
                }
            }
        }
    }

    fn arg(&mut self, a: &ast::Arg) {
        match &a.kind {
            ArgKind::Positional(e) | ArgKind::Named(_, e) | ArgKind::Spread(e) => self.expr(e),
        }
    }

    /// `base.name`.
    fn field(&mut self, e: &ast::Expr, base: &ast::Expr, name: ast::Ident) {
        match self.classify(base) {
            Base::Module(m) => {
                self.expr(base);
                if let Some(d) = self.sema.modules[m.0 as usize].scope.get(&name.name).copied() {
                    self.use_decl(name.span, d);
                    // `a.b` may itself be a module: `.` after it offers its members.
                    self.note_receiver(e.span, d);
                }
            }
            Base::Type(d) => {
                self.expr(base);
                if let Some(to) = self.member_def(d, name.name) {
                    self.add_ref(name.span, to);
                }
            }
            Base::Effect(d) => {
                self.expr(base);
                self.operation_use(d, name);
            }
            Base::Value => {
                self.expr(base);
                if let Some(t) = self.type_of(base.span) {
                    self.field_use(t, name);
                }
            }
        }
    }

    /// The operation `name` of effect `d`.
    pub fn operation_use(&mut self, d: DeclId, name: ast::Ident) {
        let module = self.sema.decls[d.0 as usize].module;
        if let Some(od) = self.sema.modules[module.0 as usize].scope.get(&name.name).copied()
            && self.sema.decls[od.0 as usize].op_of.is_some_and(|(e, _)| e == d)
        {
            self.use_decl(name.span, od);
        }
    }

    /// `callee(args)`.
    fn call(&mut self, e: &ast::Expr, callee: &ast::Expr, args: &[ast::Arg]) {
        let target = self.facts.calls.get(&e.span).copied();
        // A union variant construction: the union's declaration and the variant.
        let mut variant: Option<(DeclId, Symbol)> = None;
        match &callee.kind {
            ExprKind::Field { base, name } if matches!(self.classify(base), Base::Value) => {
                self.expr(base);
                let base_ty = self.type_of(base.span);
                let is_field = base_ty.is_some_and(|t| self.field_use(t, *name));
                if !is_field && let Some(f) = target {
                    self.fn_use(name.span, f);
                }
            }
            ExprKind::Field { base, name } => {
                if let Base::Type(d) = self.classify(base) {
                    variant = Some((d, name.name));
                }
                self.expr(callee);
            }
            ExprKind::DotIdent(id) => variant = self.variant_at(e.span, *id).map(|d| (d, id.name)),
            _ => self.expr(callee),
        }
        // Named arguments name parameters, or fields in a construction.
        let mut struct_decl = None;
        if target.is_none() && variant.is_none() {
            struct_decl = self.type_of(e.span).and_then(|t| self.sema.adt_decl_of(t));
        }
        for a in args {
            if let ArgKind::Named(n, _) = &a.kind {
                if let Some((d, v)) = variant {
                    self.payload_field(d, v, *n);
                } else if let Some(f) = target {
                    let param = self.sema.sigs.get(&f).and_then(|s| s.params.iter().find(|p| p.name == n.name));
                    if let Some(p) = param {
                        let (ps, pty, pname) = (p.span, p.ty, p.name);
                        self.add_ref(n.span, ps);
                        self.idx.defs.entry(ps).or_insert(Hover { code: format!("{}: {}", pname, pty), doc: None });
                    }
                } else if let Some(d) = struct_decl
                    && let Some(to) = self.member_def(d, n.name)
                {
                    self.add_ref(n.span, to);
                }
            }
            self.arg(a);
        }
    }

    /// A use of the function `f` at `from`: a method or an imported function.
    fn fn_use(&mut self, from: Span, f: crate::tir::FnId) {
        let to = self.sema.prog.fns[f.index()].span;
        // The function's name is its declaration's name, when it is a top-level one.
        match self.fn_decls.get(&f).copied() {
            Some(d) => self.use_decl(from, d),
            None => self.add_ref(from, to),
        }
    }

    // --- statements and control flow ---

    pub fn block(&mut self, b: &ast::Block) {
        self.push_scope(b.span.hi);
        for s in &b.stmts {
            self.stmt(s);
        }
        self.pop_scope();
    }

    fn stmt(&mut self, s: &ast::Stmt) {
        match &s.kind {
            StmtKind::Binding { binding, .. } => self.local_binding(binding),
            StmtKind::Assign { places, value, .. } => {
                places.iter().for_each(|p| self.expr(p));
                self.expr(value);
            }
            StmtKind::Expr(e) | StmtKind::Cancel(e) => self.expr(e),
            StmtKind::Defer { body, .. } => self.stmt(body),
            StmtKind::When(w) => self.when_stmt(w),
            StmtKind::While { label, cond, body } => {
                self.push_scope(body.span.hi);
                self.cond(cond);
                self.with_label(*label, |this| this.block(body));
                self.pop_scope();
            }
            StmtKind::LetElse { pat, value, fallback } => {
                self.expr(value);
                self.expr(fallback);
                let ty = self.type_of(value.span);
                self.pattern(pat, ty, s.span.hi);
            }
            StmtKind::Block(b) => self.block(b),
        }
    }

    fn when_stmt(&mut self, w: &ast::WhenStmt) {
        self.expr(&w.cond);
        self.block(&w.then);
        match &w.else_ {
            Some(ast::WhenElse::Block(b)) => self.block(b),
            Some(ast::WhenElse::When(w)) => self.when_stmt(w),
            None => {}
        }
    }

    fn local_binding(&mut self, b: &ast::Binding) {
        if let Some(t) = &b.ty {
            self.expr(t);
        }
        let is_fn = matches!(b.init.kind, ExprKind::Fn(_));
        if is_fn && let [name] = b.names.as_slice() {
            // A nested function can call itself.
            self.declare(*name, Kind::Function, name.span.hi);
            if let ExprKind::Fn(lit) = &b.init.kind {
                let head = Span { hi: lit.sig.span.hi, ..name.span };
                let code = self.sema.src(head);
                self.idx.defs.insert(name.span, Hover { code: code.trim().to_string(), doc: None });
            }
            self.expr(&b.init);
            return;
        }
        self.expr(&b.init);
        for n in &b.names {
            self.declare(*n, Kind::Variable, b.span.hi);
        }
    }

    /// Walks a condition; its bindings are declared in the current scope.
    fn cond(&mut self, c: &ast::Cond) {
        for part in &c.parts {
            match part {
                CondPart::Bind { pat, value } => {
                    self.expr(value);
                    let ty = self.type_of(value.span);
                    self.pattern(pat, ty, pat.span.hi);
                }
                CondPart::Expr(e) => self.expr(e),
            }
        }
    }

    fn if_expr(&mut self, i: &ast::IfExpr) {
        for (c, b) in &i.branches {
            self.push_scope(b.span.hi);
            self.cond(c);
            self.block(b);
            self.pop_scope();
        }
        match &i.else_ {
            Some(ast::IfElse::Block(b)) => self.block(b),
            Some(ast::IfElse::Capture { name, body }) => {
                self.push_scope(body.span.hi);
                self.declare(*name, Kind::Variable, name.span.hi);
                self.expr(body);
                self.pop_scope();
            }
            None => {}
        }
    }

    fn match_expr(&mut self, m: &ast::MatchExpr) {
        self.expr(&m.scrutinee);
        let ty = self.type_of(m.scrutinee.span);
        for arm in &m.arms {
            self.push_scope(arm.span.hi);
            for p in &arm.pats {
                self.pattern(p, ty, p.span.hi);
            }
            if let Some(g) = &arm.guard {
                self.expr(g);
            }
            self.expr(&arm.body);
            self.pop_scope();
        }
    }

    fn for_expr(&mut self, f: &ast::ForExpr) {
        self.expr(&f.iter);
        self.push_scope(f.body.span.hi);
        for p in &f.pats {
            self.pattern(p, None, p.span.hi);
        }
        self.with_label(f.label, |this| this.block(&f.body));
        self.pop_scope();
    }

    /// Runs `body` with `label`, if there is one, in scope.
    fn with_label(&mut self, label: Option<ast::Ident>, body: impl FnOnce(&mut Self)) {
        if let Some(l) = label {
            self.labels.push((l.name, l.span));
            self.idx.defs.entry(l.span).or_insert(Hover { code: format!("label {}", l.name), doc: None });
        }
        body(self);
        if label.is_some() {
            self.labels.pop();
        }
    }

    /// The label named by a `break` or `continue`.
    fn label_use(&mut self, label: Option<ast::Ident>) {
        if let Some(l) = label
            && let Some((_, def)) = self.labels.iter().rev().find(|(n, _)| *n == l.name).copied()
        {
            self.add_ref(l.span, def);
        }
    }

    // --- patterns ---

    /// Walks pattern `p`, matched against a value of type `ty` when it is known; its
    /// bindings are visible from offset `from`.
    pub fn pattern(&mut self, p: &ast::Pattern, ty: Option<Ty>, from: u32) {
        match &p.kind {
            ast::PatternKind::Wildcard | ast::PatternKind::Null => {}
            ast::PatternKind::Bind(id) => self.declare(*id, Kind::Variable, from),
            ast::PatternKind::Lit(e) => self.expr(e),
            ast::PatternKind::Range { lo, hi, .. } => {
                self.expr(lo);
                self.expr(hi);
            }
            ast::PatternKind::Err(inner) => {
                let err = ty.and_then(|t| match t.kind() {
                    TyKind::Result { err, .. } => Some(*err),
                    _ => None,
                });
                self.pattern(inner, err, from);
            }
            ast::PatternKind::Path { path, args } => {
                // The type being matched: from the path (`T.V`, `S(…)`) or the context (`.V`).
                let mut owner = ty;
                match &path.kind {
                    ExprKind::DotIdent(id) => {
                        if let Some(t) = ty {
                            self.variant_use(t, *id);
                        }
                    }
                    _ => {
                        self.expr(path);
                        owner = self.path_type(path).or(ty);
                    }
                }
                for a in args.iter().flatten() {
                    let mut sub = None;
                    if let Some(n) = a.name
                        && let Some(t) = owner
                    {
                        self.field_use(t, n);
                    }
                    if let (ExprKind::DotIdent(v), Some(t)) = (&path.kind, ty) {
                        sub = self.variant_payload(t, v.name);
                    }
                    self.pattern(&a.pat, sub, from);
                }
            }
            ast::PatternKind::Tuple(args) => {
                for a in args {
                    self.pattern(&a.pat, None, from);
                }
            }
        }
    }

    /// The type that the pattern path `path` names, when it names one.
    fn path_type(&mut self, path: &ast::Expr) -> Option<Ty> {
        match &path.kind {
            ExprKind::Ident(id) if self.lookup_local(id.name).is_none() => {
                let d = self.lookup_decl(id.name)?;
                match self.sema.decl_done(d) {
                    Some(DeclValue::Type(t)) => Some(t),
                    _ => None,
                }
            }
            ExprKind::Field { base, .. } => match self.classify(base) {
                Base::Type(d) => match self.sema.decl_done(d) {
                    Some(DeclValue::Type(t)) => Some(t),
                    _ => None,
                },
                _ => None,
            },
            _ => None,
        }
    }

    fn variant_payload(&self, ty: Ty, name: Symbol) -> Option<Ty> {
        let mut t = ty;
        while let TyKind::Optional(i) | TyKind::Ptr { pointee: i, .. } = t.kind() {
            t = *i;
        }
        let TyKind::Adt(id, _) = t.kind() else { return None };
        match &self.sema.prog.adts.get(id)?.kind {
            crate::tir::AdtKind::Union { variants, .. } => variants.iter().find(|v| v.name == name)?.payload,
            _ => None,
        }
    }
}

/// Finds the field `name` among struct members: the span of its name and of its declaration.
fn find_field(members: &[ast::StructMember], name: Symbol, found: &mut Option<(Span, Span, bool)>) {
    for m in members {
        match m {
            ast::StructMember::Field(f) => {
                if let Some(n) = f.names.iter().find(|n| n.name == name) {
                    found.get_or_insert((n.span, f.span, false));
                }
            }
            ast::StructMember::When { then, else_, .. } => {
                find_field(then, name, found);
                if let Some(e) = else_ {
                    find_field(e, name, found);
                }
            }
        }
    }
}

fn find_payload_field(members: &[ast::UnionMember], variant: Symbol, field: Symbol, found: &mut Option<Span>) {
    for m in members {
        match m {
            ast::UnionMember::Variant(v) if v.name.name == variant => {
                if let Some(ast::Expr { kind: ExprKind::Tuple(elems), .. }) = &v.payload
                    && let Some(n) = elems.iter().filter_map(|e| e.name).find(|n| n.name == field)
                {
                    found.get_or_insert(n.span);
                }
            }
            ast::UnionMember::Variant(_) => {}
            ast::UnionMember::When { then, else_, .. } => {
                find_payload_field(then, variant, field, found);
                if let Some(e) = else_ {
                    find_payload_field(e, variant, field, found);
                }
            }
        }
    }
}

fn find_variant(members: &[ast::UnionMember], name: Symbol, found: &mut Option<(Span, Span, bool)>) {
    for m in members {
        match m {
            ast::UnionMember::Variant(v) => {
                if v.name.name == name {
                    found.get_or_insert((v.name.span, v.span, true));
                }
            }
            ast::UnionMember::When { then, else_, .. } => {
                find_variant(then, name, found);
                if let Some(e) = else_ {
                    find_variant(e, name, found);
                }
            }
        }
    }
}
