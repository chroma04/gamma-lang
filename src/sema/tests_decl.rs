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
use std::rc::Rc;

impl<'s> Sema<'s> {
    /// Checks the tests of the root module and records them in the program.
    pub(super) fn check_tests(&mut self, root: ModuleId) {
        let items: Vec<(ModuleId, Rc<ast::Item>)> =
            std::mem::take(&mut self.test_items).into_iter().filter(|(m, _)| *m == root).collect();
        let mut names: Vec<(String, Span)> = vec![];
        for (module, item) in items {
            let ItemKind::Test { name, name_span, body } = &item.kind else { continue };
            let text = String::from_utf8_lossy(&name.bytes).into_owned();
            if let Some((_, prev)) = names.iter().find(|(n, _)| *n == text) {
                let d = crate::diag::Diagnostic::error(*name_span, format!("the test \"{}\" is declared twice", text))
                    .with_label(*prev, "first declared here");
                self.emit(d);
                continue;
            }
            names.push((text.clone(), *name_span));
            if let Some(entry) = self.check_test(module, &text, *name_span, body) {
                self.prog.tests.push(tir::TestCase { name: text, entry, span: *name_span });
            }
        }
    }

    /// The effects a test may perform: the ambient effects (§12.9).
    fn test_effects(&self, with_alloc: bool) -> EffectSet {
        let a = &self.prog.ambient;
        let ids = [
            a.out,
            a.err,
            a.env,
            if with_alloc { a.alloc } else { None },
            if with_alloc { a.input } else { None },
            if with_alloc { a.clock } else { None },
            if with_alloc { a.rand } else { None },
            if with_alloc { a.fs } else { None },
            if with_alloc { a.net } else { None },
        ];
        EffectSet::new(ids.into_iter().flatten().map(|e| EffectRef { effect: e, args: Box::new([]) }).collect())
    }

    fn check_test(&mut self, module: ModuleId, name: &str, span: Span, body: &ast::Block) -> Option<FnId> {
        let effects = self.test_effects(true);
        let sym = Symbol::intern(&format!("test \"{}\"", name));
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
        self.test_entry(f, ret, sym, span)
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
    fn test_entry(&mut self, f: FnId, ret: Ty, name: Symbol, span: Span) -> Option<FnId> {
        let runner = if ret == Ty::unit() { "run_test_unit" } else { "run_test" };
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
        })));
        let arg = tir::Expr { ty: fn_ty, kind: tir::ExprKind::FnRef(f), span };
        let call = tir::Expr {
            ty: Ty::u8(),
            kind: tir::ExprKind::Call { callee: tir::Callee::Fn(callee), args: vec![arg] },
            span,
        };
        let module = self.sigs[&f].module;
        let sig = FnSig { module, params: vec![], ret: Ty::u8(), effects: self.test_effects(false), generics: vec![] };
        let entry = self.new_fn(name, span, sig, tir::FnKind::Named);
        self.prog.fns[entry.index()].body = Some(call);
        Some(entry)
    }
}
