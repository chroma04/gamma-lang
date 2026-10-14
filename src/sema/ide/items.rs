//! The walk over declarations: functions, type literals, handlers and the outline.

use super::walk::{Base, Walker};
use crate::ide::{DocSymbol, Hover, Kind};
use crate::sema::{DeclId, DeclValue};
use crate::source::Span;
use crate::syntax::ast::{self, ExprKind, ItemKind};

impl<'a, 's> Walker<'a, 's> {
    // --- functions ---

    pub fn fn_literal(&mut self, lit: &ast::FnLit) {
        self.push_scope(lit.span.hi);
        self.params(&lit.sig.params, true);
        self.sig_rest(&lit.sig);
        for c in &lit.where_ {
            self.expr(&c.ty);
        }
        match &lit.body {
            ast::FnBody::Block(b) => self.block(b),
            ast::FnBody::Expr(e) => self.expr(e),
        }
        self.pop_scope();
    }

    /// A function type: parameter types, result and effects.
    pub fn sig(&mut self, sig: &ast::FnSig) {
        self.push_scope(sig.span.hi);
        self.params(&sig.params, false);
        self.sig_rest(sig);
        self.pop_scope();
    }

    fn sig_rest(&mut self, sig: &ast::FnSig) {
        if let Some(r) = &sig.ret {
            self.expr(r);
        }
        for e in sig.effects.iter().flatten() {
            self.expr(e);
        }
    }

    /// Parameter groups, in order; with `declare`, their names become locals.
    pub fn params(&mut self, params: &[ast::Param], declare: bool) {
        for p in params {
            if let Some(t) = &p.ty {
                self.expr(t);
            }
            if let Some(d) = &p.default {
                self.expr(d);
            }
            if !declare {
                continue;
            }
            let is_type =
                matches!(p.ty.as_ref().map(|t| &t.kind), Some(ExprKind::Ident(i)) if i.name.as_str() == "type");
            let kind = if is_type {
                Kind::TypeParam
            } else if p.comptime {
                Kind::Const
            } else {
                Kind::Parameter
            };
            for n in &p.names {
                self.declare(*n, kind, n.span.hi);
            }
        }
    }

    // --- handlers ---

    pub fn handler_literal(&mut self, h: &ast::HandlerLit) {
        self.expr(&h.effect);
        let effect = match self.classify(&h.effect) {
            Base::Effect(d) => Some(d),
            _ => None,
        };
        self.clauses(&h.clauses, effect, None);
    }

    /// Handler clauses; for a handler declaration, `self` stands for the value of `self_ty`.
    pub fn clauses(&mut self, clauses: &[ast::Clause], effect: Option<DeclId>, self_ty: Option<Span>) {
        for c in clauses {
            if let Some(d) = effect {
                self.operation_use(d, c.name);
            }
            self.push_scope(c.span.hi);
            if let Some(ty) = self_ty {
                let me = ast::Ident { name: crate::symbol::Symbol::intern("self"), span: ty };
                self.declare(me, Kind::Parameter, c.name.span.hi);
            }
            for p in &c.params {
                self.declare(*p, Kind::Parameter, p.span.hi);
            }
            self.expr(&c.body);
            self.pop_scope();
        }
    }

    // --- type literals ---

    /// A struct, union or effect literal written inside an expression.
    pub fn type_literal(&mut self, e: &ast::Expr) {
        let mut out = vec![];
        self.type_lit(e, &mut out);
    }

    /// Walks a struct, union or effect literal, collecting its members for the outline.
    pub fn type_lit(&mut self, e: &ast::Expr, out: &mut Vec<DocSymbol>) {
        match &e.kind {
            ExprKind::Struct(lit) => {
                self.push_scope(lit.span.hi);
                self.params(lit.params.as_deref().unwrap_or(&[]), true);
                for c in &lit.where_ {
                    self.expr(&c.ty);
                }
                self.struct_members(&lit.members, out);
                self.pop_scope();
            }
            ExprKind::Union(lit) => {
                self.push_scope(lit.span.hi);
                self.params(lit.params.as_deref().unwrap_or(&[]), true);
                if let Some(t) = &lit.tag_type {
                    self.expr(t);
                }
                self.union_members(&lit.members, out);
                self.pop_scope();
            }
            ExprKind::Effect(lit) => {
                self.push_scope(lit.span.hi);
                self.params(lit.params.as_deref().unwrap_or(&[]), true);
                for op in &lit.ops {
                    self.push_scope(op.span.hi);
                    self.params(&op.params, false);
                    if let Some(r) = &op.ret {
                        self.expr(r);
                    }
                    self.pop_scope();
                    let detail = self.sema.src(op.span);
                    out.push(DocSymbol {
                        name: op.name.name.to_string(),
                        kind: Kind::Operation,
                        detail: detail.trim().to_string(),
                        range: op.span,
                        name_span: op.name.span,
                        children: vec![],
                    });
                }
                self.pop_scope();
            }
            ExprKind::Attributed { inner, .. } => self.type_lit(inner, out),
            _ => self.expr(e),
        }
    }

    fn struct_members(&mut self, members: &[ast::StructMember], out: &mut Vec<DocSymbol>) {
        for m in members {
            match m {
                ast::StructMember::Field(f) => {
                    self.expr(&f.ty);
                    if let Some(d) = &f.default {
                        self.expr(d);
                    }
                    let text = self.sema.src(f.span).trim().to_string();
                    let ty = self.sema.src(f.ty.span);
                    for n in &f.names {
                        self.idx.defs.entry(n.span).or_insert(Hover { code: text.clone(), doc: None });
                        out.push(DocSymbol {
                            name: n.name.to_string(),
                            kind: Kind::Field,
                            detail: ty.clone(),
                            range: f.span,
                            name_span: n.span,
                            children: vec![],
                        });
                    }
                }
                ast::StructMember::When { cond, then, else_, .. } => {
                    self.expr(cond);
                    self.struct_members(then, out);
                    if let Some(e) = else_ {
                        self.struct_members(e, out);
                    }
                }
            }
        }
    }

    fn union_members(&mut self, members: &[ast::UnionMember], out: &mut Vec<DocSymbol>) {
        for m in members {
            match m {
                ast::UnionMember::Variant(v) => {
                    if let Some(p) = &v.payload {
                        self.expr(p);
                    }
                    if let Some(x) = &v.value {
                        self.expr(x);
                    }
                    let text = self.sema.src(v.span).trim().to_string();
                    self.idx.defs.entry(v.name.span).or_insert(Hover { code: text.clone(), doc: None });
                    out.push(DocSymbol {
                        name: v.name.name.to_string(),
                        kind: Kind::Variant,
                        detail: v.payload.as_ref().map_or(String::new(), |p| format!(": {}", self.sema.src(p.span))),
                        range: v.span,
                        name_span: v.name.span,
                        children: vec![],
                    });
                }
                ast::UnionMember::When { cond, then, else_, .. } => {
                    self.expr(cond);
                    self.union_members(then, out);
                    if let Some(e) = else_ {
                        self.union_members(e, out);
                    }
                }
            }
        }
    }

    // --- top-level items ---

    pub fn item(&mut self, it: &ast::Item, out: &mut Vec<DocSymbol>) {
        match &it.kind {
            ItemKind::Binding(b) => self.top_binding(it, b, out),
            ItemKind::When(w) => {
                self.expr(&w.cond);
                for i in &w.then {
                    self.item(i, out);
                }
                for i in w.else_.iter().flatten() {
                    self.item(i, out);
                }
            }
            ItemKind::Handler(h) => {
                self.push_scope(h.span.hi);
                self.params(h.params.as_deref().unwrap_or(&[]), true);
                self.expr(&h.effect);
                self.expr(&h.for_ty);
                for e in h.effects.iter().flatten() {
                    self.expr(e);
                }
                let effect = match self.classify(&h.effect) {
                    Base::Effect(d) => Some(d),
                    _ => None,
                };
                self.clauses(&h.clauses, effect, Some(h.for_ty.span));
                self.pop_scope();
                let head = self.sema.src(it.span);
                let head = head.split('{').next().unwrap_or("").trim().to_string();
                out.push(DocSymbol {
                    name: head,
                    kind: Kind::Handler,
                    detail: String::new(),
                    range: it.span,
                    name_span: h.effect.span,
                    children: vec![],
                });
            }
            ItemKind::Test { name, name_span, body } => {
                self.block(body);
                out.push(DocSymbol {
                    name: format!("test \"{}\"", String::from_utf8_lossy(&name.bytes)),
                    kind: Kind::Test,
                    detail: String::new(),
                    range: it.span,
                    name_span: *name_span,
                    children: vec![],
                });
            }
            ItemKind::Intrinsic(e) => self.expr(e),
        }
    }

    fn top_binding(&mut self, it: &ast::Item, b: &ast::Binding, out: &mut Vec<DocSymbol>) {
        if let Some(t) = &b.ty {
            self.expr(t);
        }
        let [name] = b.names.as_slice() else {
            self.expr(&b.init);
            return;
        };
        let module = self.module;
        let decl = self.sema.modules[module.0 as usize]
            .scope
            .get(&name.name)
            .copied()
            .filter(|d| self.sema.decls[d.0 as usize].name.span == name.span);
        let (mut kind, mut detail) = match decl {
            Some(d) => {
                self.ensure_hover(d);
                let item = self.sema.decl_item(d);
                (item.kind, item.detail)
            }
            None => (Kind::Const, String::new()),
        };
        let mut children = vec![];
        let mut init = &*b.init;
        while let ExprKind::Attributed { inner, .. } = &init.kind {
            init = inner;
        }
        match &init.kind {
            ExprKind::Fn(lit) => {
                if decl.is_none() {
                    kind = Kind::Function;
                }
                self.fn_literal(lit);
            }
            ExprKind::Struct(_) | ExprKind::Union(_) | ExprKind::Effect(_) => {
                if decl.is_none() {
                    kind = match init.kind {
                        ExprKind::Struct(_) => Kind::Struct,
                        ExprKind::Union(_) => Kind::Union,
                        _ => Kind::Effect,
                    };
                }
                self.type_lit(init, &mut children);
                detail.clear();
                if let (Some(d), ExprKind::Effect(_)) = (decl, &init.kind) {
                    // The operations are declarations of the module too.
                    let ops: Vec<DeclId> = (0..self.sema.decls.len() as u32)
                        .map(DeclId)
                        .filter(|o| self.sema.decls[o.0 as usize].op_of.is_some_and(|(e, _)| e == d))
                        .collect();
                    for o in ops {
                        self.ensure_hover(o);
                    }
                }
            }
            ExprKind::Import(_) => {
                self.expr(&b.init);
                if let Some(DeclValue::Module(m)) = decl.and_then(|d| self.sema.decl_done(d)) {
                    kind = Kind::Module;
                    if let Some(first) = self.first_file_of(m) {
                        self.add_ref(init.span, Span { file: first, lo: 0, hi: 0 });
                    }
                }
            }
            ExprKind::Foreign(f) => {
                kind = Kind::Module;
                self.expr(&b.init);
                for m in &f.members {
                    if let ast::ForeignMemberKind::Fn { name, sig } = &m.kind {
                        children.push(DocSymbol {
                            name: name.name.to_string(),
                            kind: Kind::Function,
                            detail: self.sema.src(sig.span),
                            range: m.span,
                            name_span: name.span,
                            children: vec![],
                        });
                    }
                }
            }
            _ => self.expr(&b.init),
        }
        out.push(DocSymbol {
            name: name.name.to_string(),
            kind,
            detail,
            range: it.span,
            name_span: name.span,
            children,
        });
    }

    /// The first file of module `m`, by file id.
    fn first_file_of(&self, m: crate::sema::ModuleId) -> Option<crate::source::FileId> {
        let dir = self.sema.modules[m.0 as usize].dir.as_ref()?;
        self.sema.sess.sources.files().find(|(_, f)| f.path.parent() == Some(dir.as_path())).map(|(id, _)| id)
    }
}
