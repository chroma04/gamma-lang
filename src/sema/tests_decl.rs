//! `test` declarations (§3.8): each body becomes a function `fn() -> E!()`
//! whose error type `E` is that of its first `try` (or `()` if it never fails),
//! with an entry function that runs it under the test handlers.

use super::func::FnCx;
use super::{DeclValue, FnSig, ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ItemKind};
use crate::tir::{self, FnId};
use crate::types::{EffectRef, EffectSet, FnTy, Ty, TyKind};
use std::collections::BTreeMap;
use std::rc::Rc;

impl<'s> Sema<'s> {
    /// Checks the tests of the root module and of the modules of its package
    /// that it imports, and records them in the program: the root module's
    /// first, then each other module's in the order of their paths (§3.8).
    pub(super) fn check_tests(&mut self, root: ModuleId) {
        let package = self.modules[root.0 as usize].package;
        // By module label; the root module's (`None`) sorts first.
        let mut groups: BTreeMap<Option<String>, Vec<(ModuleId, Rc<ast::Item>)>> = BTreeMap::new();
        for (module, item) in std::mem::take(&mut self.test_items) {
            let m = &self.modules[module.0 as usize];
            if module != root && (m.package != package || m.is_prelude) {
                continue;
            }
            let label = (module != root).then(|| self.module_label(module));
            groups.entry(label).or_default().push((module, item));
        }
        for (label, items) in groups {
            let mut names: Vec<(bool, String, Span)> = vec![];
            for (module, item) in items {
                let ItemKind::Test { name, name_span, body, bench } = &item.kind else { continue };
                let bench = *bench;
                let text = String::from_utf8_lossy(&name.bytes).into_owned();
                if let Some((_, _, prev)) = names.iter().find(|(b, n, _)| *b == bench && *n == text) {
                    let what = if bench { "benchmark" } else { "test" };
                    let d = crate::diag::Diagnostic::error(
                        *name_span,
                        format!("the {} \"{}\" is declared twice", what, text),
                    )
                    .with_label(*prev, "first declared here");
                    self.emit(d);
                    continue;
                }
                names.push((bench, text.clone(), *name_span));
                if let Some(entry) = self.check_test(module, &text, *name_span, body, bench) {
                    let case = tir::TestCase { name: text, module: label.clone(), entry, span: *name_span };
                    if bench {
                        self.prog.benches.push(case);
                    } else {
                        self.prog.tests.push(case);
                    }
                }
            }
        }
    }

    /// The path of `module`'s directory relative to its package's root, with `/`
    /// separators (`.` for the root itself), which names it in test reports.
    fn module_label(&self, module: ModuleId) -> String {
        let m = &self.modules[module.0 as usize];
        let dir = m.dir.as_deref().map(|d| crate::source::canonicalize(d).unwrap_or_else(|_| d.to_path_buf()));
        let root = m.package.map(|p| &self.packages[p].root);
        let rel = match (&dir, root) {
            (Some(d), Some(r)) => d.strip_prefix(r).ok(),
            _ => None,
        };
        match rel {
            Some(r) if r.as_os_str().is_empty() => ".".to_string(),
            Some(r) => r.components().map(|c| c.as_os_str().to_string_lossy()).collect::<Vec<_>>().join("/"),
            None => m.name.to_string(),
        }
    }

    /// The effects a test may perform: the ambient effects (§12.9).
    fn test_effects(&self, with_alloc: bool) -> EffectSet {
        let a = &self.prog.ambient;
        let ids = [
            a.out,
            a.err,
            a.env,
            a.sys,
            if with_alloc { a.alloc } else { None },
            if with_alloc { a.input } else { None },
            if with_alloc { a.clock } else { None },
            if with_alloc { a.rand } else { None },
            if with_alloc { a.fs } else { None },
            if with_alloc { a.net } else { None },
        ];
        EffectSet::new(ids.into_iter().flatten().map(|e| EffectRef { effect: e, args: Box::new([]) }).collect())
    }

    fn check_test(&mut self, module: ModuleId, name: &str, span: Span, body: &ast::Block, bench: bool) -> Option<FnId> {
        let effects = self.test_effects(true);
        let sym = Symbol::intern(&format!("{} \"{}\"", if bench { "bench" } else { "test" }, name));
        let sig = FnSig { module, params: vec![], ret: Ty::unit(), effects: effects.clone(), generics: vec![] };
        let f = self.new_fn(sym, span, sig, tir::FnKind::Named);
        // First pass: the error type comes from the first `try`. A body without
        // one is checked again as `fn()`.
        let mark = self.sess.diags.len();
        let (mut ret, mut checked) = self.check_test_body(f, module, body, Ty::result(Ty::error(), Ty::unit()), true);
        if let TyKind::Result { err, .. } = ret.kind()
            && err.is_error()
        {
            self.sess.diags.truncate(mark);
            (ret, checked) = self.check_test_body(f, module, body, Ty::unit(), false);
        }
        let (body, locals) = checked;
        self.sigs.get_mut(&f).unwrap().ret = ret;
        let def = &mut self.prog.fns[f.index()];
        def.ret = ret;
        def.locals = locals;
        def.body = Some(body);
        if bench { self.bench_entry(f, ret, span) } else { self.test_entry(f, ret, span) }
    }

    fn check_test_body(
        &mut self,
        f: FnId,
        module: ModuleId,
        body: &ast::Block,
        ret: Ty,
        infer: bool,
    ) -> (Ty, (tir::Expr, Vec<tir::LocalDecl>)) {
        let effects = self.test_effects(true);
        self.in_test = true;
        self.checking.push(f);
        let mut cx = FnCx::new(self, module, ret, effects);
        cx.fn_id = Some(f);
        cx.infer_test_error = infer;
        let b = cx.fn_body(&ast::FnBody::Block(body.clone()));
        let ret = cx.ret;
        let out = cx.finish(b);
        self.checking.pop();
        self.in_test = false;
        (ret, out)
    }

    /// `run_test(E, &test)` or `run_test_unit(&test)` from the core prelude:
    /// installs the leak-checking allocator and reports a failure.
    fn test_entry(&mut self, f: FnId, ret: Ty, span: Span) -> Option<FnId> {
        let (_, call) = self.runner_call(f, ret, span, if ret == Ty::unit() { "run_test_unit" } else { "run_test" })?;
        let module = self.sigs[&f].module;
        let sig = FnSig { module, params: vec![], ret: Ty::u8(), effects: self.test_effects(false), generics: vec![] };
        let entry = self.new_fn(Symbol::intern("<entry>"), span, sig, tir::FnKind::Named);
        self.prog.fns[entry.index()].body = Some(call);
        Some(entry)
    }

    /// `run_bench(E, &bench)` or `run_bench_unit(&bench)`: times the body. Unlike a test, a
    /// benchmark runs under the system's handlers, as `main` does, so the entry is wrapped
    /// like `main`'s.
    fn bench_entry(&mut self, f: FnId, ret: Ty, span: Span) -> Option<FnId> {
        let (_, call) =
            self.runner_call(f, ret, span, if ret == Ty::unit() { "run_bench_unit" } else { "run_bench" })?;
        let module = self.sigs[&f].module;
        let sig = FnSig { module, params: vec![], ret: Ty::u8(), effects: self.test_effects(true), generics: vec![] };
        let inner = self.new_fn(Symbol::intern("<bench>"), span, sig.clone(), tir::FnKind::Named);
        self.prog.fns[inner.index()].body = Some(call);
        Some(self.entry_point(inner, &sig))
    }

    /// The call of the core prelude function `runner` on the function `f` returning `ret`.
    fn runner_call(&mut self, f: FnId, ret: Ty, span: Span, runner: &str) -> Option<(FnId, tir::Expr)> {
        let d = self.prelude.and_then(|p| self.modules[p.0 as usize].scope.get(&Symbol::intern(runner)).copied())?;
        let DeclValue::Fn(g) = self.resolve_decl(d) else { return None };
        let callee = match ret.kind() {
            TyKind::Result { err, .. } => self.instantiate(g, vec![*err], span),
            _ => g,
        };
        let fn_ty = Ty::intern(TyKind::FnPtr(Box::new(FnTy {
            params: Box::new([]),
            ret,
            effects: self.test_effects(true),
            variadic: false,
            callconv: Default::default(),
        })));
        let arg = tir::Expr { ty: fn_ty, kind: tir::ExprKind::FnRef(f), span };
        let call = tir::Expr {
            ty: Ty::u8(),
            kind: tir::ExprKind::Call { callee: tir::Callee::Fn(callee), args: vec![arg] },
            span,
        };
        Some((callee, call))
    }
}
