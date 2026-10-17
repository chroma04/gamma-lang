//! Interoperability (§19): `#global` variables, `foreign` blocks, exported
//! functions, and the C-compatibility of types.

use super::decls::DeclValue;
use super::{Decl, DeclId, DeclState, FnSig, ModuleId, Sema};
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind, ItemKind};
use crate::tir::{self, AdtKind, FnId};
use crate::types::{CallConv, EffectRef, EffectSet, FnTy, Ty, TyKind};
use std::collections::HashMap;
use std::rc::Rc;

impl<'s> Sema<'s> {
    /// The effect set `{Sys}` (§12.10).
    pub fn sys_set(&self) -> EffectSet {
        match self.prog.ambient.sys {
            Some(e) => EffectSet::new(vec![EffectRef { effect: e, args: Box::new([]) }]),
            None => EffectSet::empty(),
        }
    }

    /// True if every effect in `s` is `Sys`.
    pub fn within_sys(&self, s: &EffectSet) -> bool {
        s.effects.iter().all(|e| Some(e.effect) == self.prog.ambient.sys)
    }

    /// `#global name: T = e` (§19.3).
    pub(super) fn declare_global(
        &mut self,
        module: ModuleId,
        name: ast::Ident,
        b: &ast::Binding,
        thread_local: bool,
        align: Option<u64>,
    ) -> DeclValue {
        let Some(ty_expr) = &b.ty else {
            self.error(name.span, "a `#global` needs a type: `#global name: T = value`");
            return DeclValue::Error;
        };
        let ty = self.resolve_type(module, ty_expr);
        if ty.is_error() {
            return DeclValue::Error;
        }
        let Some(init) = self.check_const_expr(module, &b.init, Some(ty)) else { return DeclValue::Error };
        let Some(value) = self.eval_tir_const(&init) else { return DeclValue::Error };
        let id = tir::GlobalId(self.prog.globals.len() as u32);
        self.prog.globals.push(tir::GlobalDef {
            name: name.name,
            ty,
            span: name.span,
            init: Some(init),
            value: Some(value),
            thread_local,
            link_name: None,
            align,
        });
        DeclValue::Global { ty, id }
    }

    /// `name :: foreign "lib" { … }` (§19.2): a module-like block of foreign functions
    /// and variables.
    pub(super) fn declare_foreign(
        &mut self,
        module: ModuleId,
        name: ast::Ident,
        block: &ast::ForeignBlock,
    ) -> DeclValue {
        let lib = Symbol::intern(&String::from_utf8_lossy(&block.lib.bytes));
        let m = self.add_module(name.name, vec![], false);
        self.modules[m.0 as usize].dir = self.modules[module.0 as usize].dir.clone();
        self.modules[m.0 as usize].package = self.modules[module.0 as usize].package;
        let mut scope: HashMap<Symbol, DeclId> = HashMap::new();
        for member in &block.members {
            let mut link_name = None;
            let mut callconv = None;
            for a in &member.attrs {
                match (a.name.name.as_str(), a.args.as_deref()) {
                    ("link_name", Some([ast::Expr { kind: ExprKind::Str(s), .. }])) => {
                        link_name = Some(Symbol::intern(&String::from_utf8_lossy(&s.bytes)));
                    }
                    ("link_name", _) => self.error(a.span, "`#link_name` takes the symbol name: `#link_name(\"sym\")`"),
                    ("callconv", _) if matches!(member.kind, ast::ForeignMemberKind::Fn { .. }) => {
                        if callconv.is_some() {
                            self.error(a.span, "a function has one calling convention");
                        }
                        callconv = self.callconv_attr(a).map(|c| (c, a.span));
                    }
                    (other, _) => {
                        self.error(a.span, format!("attribute `#{}` does not apply to a foreign member", other))
                    }
                }
            }
            let (member_name, value) = match &member.kind {
                ast::ForeignMemberKind::Fn { name: n, sig } => {
                    let link = link_name.unwrap_or(n.name);
                    match self.declare_foreign_fn(module, *n, sig, lib, link) {
                        Some(f) => {
                            if let Some((cc, span)) = callconv {
                                self.apply_callconv(f, cc, span);
                            }
                            (*n, DeclValue::Fn(f))
                        }
                        None => (*n, DeclValue::Error),
                    }
                }
                ast::ForeignMemberKind::Global { name: n, ty } => {
                    let t = self.resolve_type(module, ty);
                    if !t.is_error() && !self.is_c_compatible(t, false) {
                        self.error(ty.span, format!("`{}` is not a C-compatible type (§19.1)", t));
                    }
                    let id = tir::GlobalId(self.prog.globals.len() as u32);
                    self.prog.globals.push(tir::GlobalDef {
                        name: n.name,
                        ty: t,
                        span: n.span,
                        init: None,
                        value: None,
                        thread_local: false,
                        link_name: Some(link_name.unwrap_or(n.name)),
                        align: None,
                    });
                    (*n, DeclValue::Global { ty: t, id })
                }
            };
            if scope.contains_key(&member_name.name) {
                self.error(member_name.span, format!("`{}` is declared twice in this foreign block", member_name.name));
                continue;
            }
            let d = DeclId(self.decls.len() as u32);
            self.decls.push(Decl {
                name: member_name,
                module: m,
                item: synthetic_item(member.span),
                state: DeclState::Done(value),
                op_of: None,
            });
            scope.insert(member_name.name, d);
        }
        self.modules[m.0 as usize].scope = scope;
        DeclValue::Module(m)
    }

    fn declare_foreign_fn(
        &mut self,
        module: ModuleId,
        name: ast::Ident,
        sig: &ast::FnSig,
        lib: Symbol,
        link_name: Symbol,
    ) -> Option<FnId> {
        self.allow_variadic = true;
        let resolved = self.resolve_fn_sig(module, sig, false, &[]);
        self.allow_variadic = false;
        let mut s = resolved?;
        // Foreign functions perform `Sys` unless annotated (§19.2).
        if sig.effects.is_none() {
            s.effects = self.sys_set();
        }
        for p in &s.params {
            if !p.ty.is_error() && !self.is_c_compatible(p.ty, false) {
                self.error(p.span, format!("`{}` is not a C-compatible parameter type (§19.1)", p.ty));
            }
        }
        // `never`: a C function that does not return, such as `exit` (§19.2).
        if !s.ret.is_error() && !s.ret.is_unit() && !s.ret.is_never() && !self.is_c_compatible(s.ret, false) {
            self.error(name.span, format!("`{}` is not a C-compatible result type (§19.1)", s.ret));
        }
        let params = s.params.clone();
        let ret = s.ret;
        let kind = tir::FnKind::Foreign { lib, link_name, variadic: sig.variadic };
        let f = self.new_fn(name.name, name.span, s, kind);
        let def = &mut self.prog.fns[f.index()];
        def.ret = ret;
        for (i, p) in params.iter().enumerate() {
            def.locals.push(tir::LocalDecl { name: p.name, ty: p.ty, mutable: false, span: p.span });
            def.params.push(tir::LocalId(i as u32));
        }
        if sig.variadic {
            self.variadic_fns.insert(f);
        }
        Some(f)
    }

    /// The convention named by `#callconv(.X)` (§19.2): `.SysV` is the C ABI of the
    /// targets that have it, so it normalizes to `C`.
    pub(super) fn callconv_attr(&mut self, a: &ast::Attribute) -> Option<CallConv> {
        let Some([ast::Expr { kind: ExprKind::DotIdent(id), .. }]) = a.args.as_deref() else {
            self.error(a.span, "`#callconv` takes a convention name: `#callconv(.Win64)`");
            return None;
        };
        let x86_64 = self.target.arch == "X86_64";
        match id.name.as_str() {
            "C" => Some(CallConv::C),
            "SysV" | "Win64" if !x86_64 => {
                self.error(a.span, format!("`.{}` is not supported on this target (x86-64 only)", id.name));
                None
            }
            "SysV" => Some(CallConv::C),
            "Win64" => Some(CallConv::Win64),
            other => {
                self.error(
                    a.span,
                    format!("unknown calling convention `.{}`; the conventions are `.C`, `.SysV` and `.Win64`", other),
                );
                None
            }
        }
    }

    /// Gives the function `f` the calling convention `cc` (§19.2). A convention other
    /// than C needs the plain calling convention: a non-generic function whose
    /// effects are at most `{Sys}`.
    pub(super) fn apply_callconv(&mut self, f: FnId, cc: CallConv, span: Span) {
        if cc == CallConv::C {
            return;
        }
        let sig = &self.sigs[&f];
        let (generic, within_sys) = (!sig.generics.is_empty(), self.within_sys(&sig.effects));
        if generic {
            self.error(span, "a generic function cannot have a calling convention other than C");
        } else if !within_sys {
            self.error(
                span,
                "a function with a calling convention other than C must have effects at most `{Sys}`: it cannot take hidden parameters (§19.2)",
            );
        } else {
            self.prog.fns[f.index()].callconv = cc;
        }
    }

    /// `#callconv(.X) ^fn(…)` (§19.2): a function pointer type with a calling
    /// convention. `inner` is the `^fn` type the attributes precede.
    pub(super) fn callconv_fn_ptr(
        &mut self,
        module: ModuleId,
        attrs: &[ast::Attribute],
        inner: &ast::Expr,
        span: Span,
    ) -> Ty {
        let mut callconv = None;
        for a in attrs {
            if a.name.name.as_str() != "callconv" {
                self.error(a.span, format!("attribute `#{}` does not apply to a function pointer type", a.name.name));
            } else if callconv.is_some() {
                self.error(a.span, "a function pointer type has one calling convention");
            } else {
                callconv = self.callconv_attr(a);
            }
        }
        let ty = self.resolve_type(module, inner);
        let (TyKind::FnPtr(sig), Some(cc)) = (ty.kind(), callconv) else { return ty };
        if cc != CallConv::C && !self.within_sys(&sig.effects) {
            self.error(
                span,
                "a function pointer type with a calling convention other than C must have effects at most `{Sys}` (§19.2)",
            );
            return Ty::error();
        }
        Ty::intern(TyKind::FnPtr(Box::new(FnTy { callconv: cc, ..(**sig).clone() })))
    }

    /// `#export("sym")` on a function (§19.4): its signature must be C-compatible and its
    /// effects at most `{Sys}`.
    pub(super) fn export_fn(&mut self, f: FnId, sym: Symbol, span: Span) {
        let sig: FnSig = self.sigs[&f].clone();
        // The symbol is a C identifier, unique among exports, and not the program's
        // own entry point (§19.4).
        let s = sym.as_str();
        let ident = s.chars().next().is_some_and(|c| c.is_ascii_alphabetic() || c == '_')
            && s.chars().all(|c| c.is_ascii_alphanumeric() || c == '_');
        if !ident {
            self.error(span, format!("`{}` is not a valid C symbol name", s));
            return;
        }
        if s == "main" {
            self.error(span, "`main` cannot be exported: it is the program's entry point");
            return;
        }
        if self.prog.exports.iter().any(|(e, _)| *e == sym) {
            self.error(span, format!("another function is already exported as `{}`", s));
            return;
        }
        if !sig.generics.is_empty() {
            self.error(span, "a generic function cannot be exported");
            return;
        }
        if !self.within_sys(&sig.effects) {
            self.error(
                span,
                "an exported function's effects must be empty or `{Sys}`: C callers cannot supply handlers (§19.4)",
            );
        }
        for p in &sig.params {
            if !self.is_c_compatible(p.ty, false) {
                self.error(p.span, format!("`{}` is not a C-compatible parameter type (§19.1)", p.ty));
            }
        }
        if !sig.ret.is_unit() && !self.is_c_compatible(sig.ret, false) {
            self.error(span, format!("`{}` is not a C-compatible result type (§19.1)", sig.ret));
        }
        self.prog.exports.push((sym, f));
    }

    /// Whether `t` is C-compatible (§19.1). `in_struct` allows arrays (as struct fields).
    pub fn is_c_compatible(&self, t: Ty, in_struct: bool) -> bool {
        match t.kind() {
            TyKind::Int(i) => matches!(i.bits, 8 | 16 | 32 | 64 | 128),
            TyKind::Usize | TyKind::Isize | TyKind::Float(_) | TyKind::Bool | TyKind::Rawptr => true,
            TyKind::Ptr { .. } | TyKind::ManyPtr { .. } => true,
            TyKind::Optional(inner) => {
                crate::layout::is_nullable_pointer(*inner) && self.is_c_compatible(*inner, false)
            }
            TyKind::FnPtr(sig) => {
                self.within_sys(&sig.effects)
                    && sig.params.iter().all(|p| self.is_c_compatible(*p, false))
                    && (sig.ret.is_unit() || self.is_c_compatible(sig.ret, false))
            }
            TyKind::Array { elem, kind, .. } => {
                in_struct && *kind != crate::types::ArrayKind::Simd && self.is_c_compatible(*elem, true)
            }
            TyKind::Adt(id, _) => match self.prog.adts.get(id).map(|d| &d.kind) {
                Some(AdtKind::Struct { bits: Some(_), .. }) => true,
                Some(AdtKind::Struct { fields, .. }) | Some(AdtKind::CUnion { fields }) => {
                    fields.iter().all(|f| self.is_c_compatible(f.ty, true))
                }
                _ => false,
            },
            TyKind::Error => true,
            _ => false,
        }
    }
}

/// A placeholder item for declarations that do not come from a top-level item.
pub fn synthetic_item(span: Span) -> Rc<ast::Item> {
    Rc::new(ast::Item {
        id: ast::NodeId(u32::MAX),
        span,
        doc: None,
        attrs: vec![],
        is_pub: true,
        kind: ItemKind::Intrinsic(ast::Expr { id: ast::NodeId(u32::MAX), kind: ExprKind::Underscore, span }),
    })
}
