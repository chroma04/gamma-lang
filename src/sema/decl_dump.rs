//! `gamma decls`: what each top-level declaration of a program denotes, after resolving
//! the declarations but before checking function bodies. The dump is a stable textual
//! form of declaration checking, which the self-hosted compiler is compared against.

use super::{DeclId, DeclState, DeclValue, ParamInfo, Sema};
use crate::tir::AdtKind;
use crate::types::{AdtId, EffectSet, Ty, TyKind};
use std::fmt::Write as _;

impl Sema<'_> {
    /// Resolves the declarations of every loaded module, as `gamma check` does before it
    /// checks the bodies: module by module in load order, each module's declarations in
    /// declaration order, then the checks that span declarations.
    pub(super) fn resolve_declarations(&mut self) {
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
        self.check_method_candidates();
        self.check_import_cycles();
        self.check_pub_signatures();
    }

    /// The modules loaded, other than the core library's, in load order, each with its
    /// declarations in declaration order and what each denotes:
    ///
    /// ```text
    /// module main
    ///   Point :: struct Point
    ///     x: i32
    ///   main :: fn() needs Out
    /// ```
    ///
    /// A struct or union is followed by its fields or variants, an effect by its
    /// operations, each on an indented line.
    pub(super) fn decl_dump(&self) -> String {
        let mut out = String::new();
        for m in self.modules.iter().filter(|m| !m.is_prelude) {
            let _ = writeln!(out, "module {}", m.name);
            let mut ids: Vec<DeclId> = m.scope.values().copied().collect();
            ids.sort();
            for id in ids {
                let d = &self.decls[id.0 as usize];
                let _ = write!(out, "  {} :: ", d.name.name);
                match &d.state {
                    DeclState::Done(v) => self.dump_value(id, v, &mut out),
                    _ => out.push_str("unresolved\n"),
                }
            }
        }
        out
    }

    fn dump_value(&self, id: DeclId, v: &DeclValue, out: &mut String) {
        match v {
            DeclValue::Type(t) => match t.kind() {
                TyKind::Adt(a, args) if args.is_empty() && self.adt_decls.get(a) == Some(&id) => self.dump_adt(*a, out),
                _ => {
                    let _ = writeln!(out, "type {}", t);
                }
            },
            DeclValue::GenericType(a) => {
                out.push_str("generic ");
                self.dump_adt(*a, out);
            }
            DeclValue::Effect(e) => {
                let info = &self.effects[e];
                let _ = write!(out, "effect {}", info.name);
                self.dump_params(&info.params, out);
                out.push('\n');
                for op in &info.ops {
                    let kind = match op.kind {
                        crate::syntax::ast::OpKind::Fn => "fn",
                        crate::syntax::ast::OpKind::Esc => "esc",
                        crate::syntax::ast::OpKind::Ctl => "ctl",
                    };
                    let _ =
                        writeln!(out, "    {}: {}{}", op.name, kind, sig_text(&op.params, op.ret, &EffectSet::empty()));
                }
            }
            DeclValue::EffectInst(r) => {
                let _ = writeln!(out, "effect {}", r);
            }
            DeclValue::EffectSet(set) => {
                let _ = writeln!(out, "effects {}", effects_text(set));
            }
            DeclValue::Fn(f) => {
                let sig = &self.sigs[f];
                let _ = writeln!(out, "fn{}", sig_text(&sig.params, sig.ret, &sig.effects));
            }
            DeclValue::Op(e, i) => {
                let _ = writeln!(out, "op {}.{}", e.name(), self.effects[e].ops[*i as usize].name);
            }
            DeclValue::Const { ty, .. } => {
                let _ = writeln!(out, "const {}", ty);
            }
            DeclValue::Untyped(u) => {
                let _ = writeln!(out, "untyped {}", super::untyped::dump(u));
            }
            DeclValue::Module(m) => {
                let _ = writeln!(out, "module {}", self.modules[m.0 as usize].name);
            }
            DeclValue::Global { ty, .. } => {
                let _ = writeln!(out, "global {}", ty);
            }
            DeclValue::Error => out.push_str("error\n"),
        }
    }

    /// `struct S` or `union U(T: type)` with its attributes, then its fields or variants.
    fn dump_adt(&self, a: AdtId, out: &mut String) {
        let Some(def) = self.prog.adts.get(&a) else {
            out.push_str("error\n");
            return;
        };
        match &def.kind {
            AdtKind::Struct { packed, align, bits, .. } => {
                out.push_str("struct");
                if *packed {
                    out.push_str(" #packed");
                }
                if let Some(n) = align {
                    let _ = write!(out, " #align({})", n);
                }
                if let Some(t) = bits {
                    let _ = write!(out, " #bits({})", t);
                }
            }
            AdtKind::Union { tag, .. } => {
                let _ = write!(out, "union({})", tag);
            }
            AdtKind::CUnion { .. } => out.push_str("union #c_union"),
        }
        let _ = write!(out, " {}", def.name);
        if let Some(params) = self.adt_params.get(&a) {
            self.dump_params(params, out);
        }
        out.push('\n');
        let defaults = self.adts.get(&a).map(|i| &i.defaults[..]).unwrap_or(&[]);
        match &def.kind {
            AdtKind::Struct { fields, .. } | AdtKind::CUnion { fields } => {
                for (i, f) in fields.iter().enumerate() {
                    let _ = write!(
                        out,
                        "    {}{}: {}",
                        match f.vis {
                            crate::syntax::ast::Visibility::Public => "pub ",
                            crate::syntax::ast::Visibility::Package => "pub(package) ",
                            crate::syntax::ast::Visibility::Private => "",
                        },
                        f.name,
                        f.ty
                    );
                    if let Some(n) = f.align {
                        let _ = write!(out, " #align({})", n);
                    }
                    if defaults.get(i).is_some_and(|d| d.is_some()) {
                        out.push_str(" = …");
                    }
                    out.push('\n');
                }
            }
            AdtKind::Union { variants, .. } => {
                for v in variants {
                    let _ = write!(out, "    {}", v.name);
                    if let Some(p) = v.payload {
                        let _ = write!(out, ": {}", p);
                    }
                    let _ = writeln!(out, " = {}", v.tag);
                }
            }
        }
    }

    /// The parameters of a generic type or effect: `(T: type, N: usize)`.
    fn dump_params(&self, params: &[crate::types::ParamId], out: &mut String) {
        if params.is_empty() {
            return;
        }
        out.push('(');
        for (i, p) in params.iter().enumerate() {
            if i > 0 {
                out.push_str(", ");
            }
            let kind = if self.effect_params.contains(p) {
                "effects".to_string()
            } else {
                self.value_params.get(p).map_or_else(|| "type".to_string(), |t| t.to_string())
            };
            let _ = write!(out, "{}: {}", p.name(), kind);
        }
        out.push(')');
    }
}

/// `(a: i32, ..rest: T, b: u8 = …) -> R needs E`, as after `fn`.
fn sig_text(params: &[ParamInfo], ret: Ty, effects: &EffectSet) -> String {
    let mut s = String::from("(");
    for (i, p) in params.iter().enumerate() {
        if i > 0 {
            s.push_str(", ");
        }
        let _ = write!(s, "{}: {}{}", p.name, if p.rest { ".." } else { "" }, p.ty);
        if p.default.is_some() {
            s.push_str(" = …");
        }
    }
    s.push(')');
    if !ret.is_unit() {
        let _ = write!(s, " -> {}", ret);
    }
    if !effects.is_empty() {
        let _ = write!(s, " needs {}", effects_text(effects));
    }
    s
}

fn effects_text(set: &EffectSet) -> String {
    if set.effects.is_empty() {
        return "()".to_string();
    }
    set.effects.iter().map(|e| e.to_string()).collect::<Vec<_>>().join(" + ")
}
