//! Struct and union declarations (§5.10, §5.11).

use super::{AdtInfo, DeclId, DeclState, DeclValue, ModuleId, Sema, Untyped};
use crate::diag::Diagnostic;
use crate::source::Span;
use crate::symbol::Symbol;
use crate::syntax::ast::{self, ExprKind};
use crate::tir::{self, AdtDef, AdtKind, FieldDef, VariantDef};
use crate::types::{AdtId, Ty, TyKind};
use num_bigint::BigInt;
use num_traits::ToPrimitive;

impl<'s> Sema<'s> {
    /// Declares the nominal type written by a `struct` or `union` literal (possibly
    /// preceded by attributes). `decl` is the binding that names it, if any.
    pub fn declare_adt(&mut self, module: ModuleId, decl: Option<DeclId>, name: Symbol, lit: &ast::Expr) -> Ty {
        let (attrs, inner): (&[ast::Attribute], &ast::Expr) = match &lit.kind {
            ExprKind::Attributed { attrs, inner } => (attrs, inner),
            _ => (&[], lit),
        };
        let id = AdtId::fresh(name);
        let ty = Ty::intern(TyKind::Adt(id, Box::new([])));
        // Type parameters of a generic struct or union (§16.4).
        let params = match &inner.kind {
            ExprKind::Struct(s) => s.params.as_ref(),
            ExprKind::Union(u) => u.params.as_ref(),
            _ => None,
        };
        let scope_len = self.type_params.len();
        let value_scope_len = self.value_scope.len();
        let mut generics = vec![];
        let mut defaults = vec![];
        for p in params.into_iter().flatten() {
            let is_type =
                p.ty.as_ref().is_some_and(|t| matches!(&t.kind, ExprKind::Ident(i) if i.name.as_str() == "type"));
            // A value parameter (`N :: usize` or `hash: ?^fn(…) = null`): every
            // parameter of a type is compile-time (§16.4).
            let value_ty = match &p.ty {
                Some(t) if !is_type => Some(self.resolve_type(module, t)),
                Some(_) => None,
                None => {
                    self.error(p.span, "a type parameter needs a type: `T: type` or `N :: usize`");
                    Some(Ty::error())
                }
            };
            for n in &p.names {
                let pid = crate::types::ParamId::fresh(n.name);
                generics.push(pid);
                defaults.push(p.default.clone().map(std::rc::Rc::new));
                match value_ty {
                    Some(t) => {
                        self.value_params.insert(pid, t);
                        self.value_scope.push((n.name, pid));
                    }
                    None => self.type_params.push((n.name, Ty::intern(TyKind::Param(pid)))),
                }
            }
        }
        if !generics.is_empty() {
            self.adt_params.insert(id, generics.clone());
            self.adt_param_defaults.insert(id, defaults);
        }
        // The type is usable (through pointers) by its own fields.
        if let Some(d) = decl {
            let v = if generics.is_empty() { DeclValue::Type(ty) } else { DeclValue::GenericType(id) };
            self.decls[d.0 as usize].state = DeclState::Done(v);
        }
        self.adts.insert(id, AdtInfo { name, module, defaults: vec![], has_using: false });
        let placeholder =
            AdtDef { name, kind: AdtKind::Struct { fields: vec![], packed: false, align: None, bits: None } };
        self.prog.adts.insert(id, placeholder);
        let kind = match &inner.kind {
            ExprKind::Struct(s) => self.struct_kind(module, id, s, attrs),
            ExprKind::Union(u) => self.union_kind(module, u, attrs),
            _ => {
                self.error(lit.span, "expected a struct or union literal after the attributes");
                return Ty::error();
            }
        };
        self.prog.adts.insert(id, AdtDef { name, kind });
        let wheres = match &inner.kind {
            ExprKind::Struct(s) => &s.where_,
            ExprKind::Union(u) => &u.where_,
            _ => &s_empty(),
        };
        for c in wheres.clone() {
            self.declare_constraint(module, &c);
        }
        self.type_params.truncate(scope_len);
        self.value_scope.truncate(value_scope_len);
        if !generics.is_empty() {
            return ty;
        }
        if self.is_infinitely_sized(ty, &mut vec![]) {
            self.error(
                lit.span,
                format!("`{}` contains itself by value and would be infinitely large; use a pointer", name),
            );
            return Ty::error();
        }
        ty
    }

    fn struct_kind(&mut self, module: ModuleId, id: AdtId, s: &ast::StructLit, attrs: &[ast::Attribute]) -> AdtKind {
        let (mut packed, mut align, mut bits) = (false, None, None);
        for a in attrs {
            match a.name.name.as_str() {
                "packed" => packed = true,
                "align" => align = self.attr_power_of_two(module, a),
                "bits" => match a.args.as_deref() {
                    Some([t]) => bits = Some(self.resolve_type(module, t)),
                    _ => self.error(a.span, "`#bits` takes the underlying unsigned integer type: `#bits(u16)`"),
                },
                other => self.error(a.span, format!("attribute `#{}` does not apply to a struct", other)),
            }
        }
        let mut fields: Vec<FieldDef> = vec![];
        let mut defaults = vec![];
        let mut has_using = false;
        let members = self.taken_struct_members(module, &s.members);
        for (i, f) in members.iter().enumerate() {
            let ty = self.resolve_type(module, &f.ty);
            if matches!(ty.kind(), TyKind::Fn(_)) {
                self.error(
                    f.ty.span,
                    "a field cannot hold a second-class function; use a function pointer `^fn(…)` (§11.6)",
                );
            }
            let mut field_align = None;
            for a in &f.attrs {
                match a.name.name.as_str() {
                    "align" => field_align = self.attr_power_of_two(module, a),
                    other => self.error(a.span, format!("attribute `#{}` does not apply to a field", other)),
                }
            }
            if f.is_using {
                if i != 0 || f.names.len() != 1 {
                    self.error(
                        f.span,
                        "only the first field of a struct may be `using`, and it must be a single field",
                    );
                } else if !matches!(ty.kind(), TyKind::Adt(..)) && !ty.is_error() {
                    self.error(f.span, format!("a `using` field must have a struct type, not `{}`", ty));
                } else {
                    has_using = true;
                }
            }
            let default = f.default.as_ref().and_then(|d| self.check_const_expr(module, d, Some(ty)));
            for n in &f.names {
                if n.name.as_str() != "_" && fields.iter().any(|x| x.name == n.name) {
                    self.error(n.span, format!("duplicate field `{}`", n.name));
                }
                fields.push(FieldDef { name: n.name, ty, is_pub: f.is_pub, align: field_align });
                defaults.push(default.clone());
            }
        }
        if let Some(b) = bits {
            self.check_bits_struct(s.span, b, &fields);
        }
        let info = self.adts.get_mut(&id).unwrap();
        info.defaults = defaults;
        info.has_using = has_using;
        AdtKind::Struct { fields, packed, align, bits }
    }

    fn check_bits_struct(&mut self, span: Span, underlying: Ty, fields: &[FieldDef]) {
        let TyKind::Int(u) = underlying.kind() else {
            self.error(span, format!("`#bits` needs an unsigned integer type, not `{}`", underlying));
            return;
        };
        if u.signed {
            self.error(span, format!("`#bits` needs an unsigned integer type, not `{}`", underlying));
        }
        let mut total: u64 = 0;
        for f in fields {
            match self.bit_width(f.ty) {
                Some(w) => total += w,
                None => self.error(
                    span,
                    format!(
                        "field `{}` of a `#bits` struct must be `bool`, an integer or another `#bits` struct",
                        f.name
                    ),
                ),
            }
        }
        if total != u.bits as u64 {
            self.error(
                span,
                format!("the fields of this `#bits` struct use {} bits, but `{}` has {}", total, underlying, u.bits),
            );
        }
    }

    fn bit_width(&self, t: Ty) -> Option<u64> {
        match t.kind() {
            TyKind::Bool => Some(1),
            TyKind::Int(i) => Some(i.bits as u64),
            TyKind::Adt(id, _) => match &self.prog.adts.get(id)?.kind {
                AdtKind::Struct { bits: Some(u), .. } => self.bit_width(*u),
                _ => None,
            },
            _ => None,
        }
    }

    fn union_kind(&mut self, module: ModuleId, u: &ast::UnionLit, attrs: &[ast::Attribute]) -> AdtKind {
        let mut c_union = false;
        for a in attrs {
            match a.name.name.as_str() {
                "c_union" => c_union = true,
                other => self.error(a.span, format!("attribute `#{}` does not apply to a union", other)),
            }
        }
        let explicit_tag = u.tag_type.as_ref().map(|t| self.resolve_type(module, t));
        if let Some(t) = explicit_tag
            && !t.is_integer()
            && !t.is_error()
        {
            self.error(u.span, format!("a union's tag type must be an integer type, not `{}`", t));
        }
        let mut variants: Vec<VariantDef> = vec![];
        let mut next = BigInt::from(0);
        let mut spans = vec![];
        let members = self.taken_union_members(module, &u.members);
        for v in &members {
            if variants.iter().any(|x| x.name == v.name.name) {
                self.error(v.name.span, format!("duplicate variant `{}`", v.name.name));
            }
            let payload = v.payload.as_ref().map(|p| self.resolve_type(module, p));
            let tag = match &v.value {
                Some(val) => {
                    if explicit_tag.is_none() {
                        self.error(val.span, "explicit tag values need an explicit tag type: `union(u8) { … }`");
                    }
                    match self.try_untyped(module, val) {
                        Some(Ok(Untyped::Int(i))) => i,
                        Some(Err(())) => next.clone(),
                        _ => {
                            self.error(val.span, "a tag value must be an integer constant");
                            next.clone()
                        }
                    }
                }
                None => next.clone(),
            };
            next = &tag + 1;
            variants.push(VariantDef { name: v.name.name, payload, tag });
            spans.push(v.span);
        }
        if c_union {
            let mut fields = vec![];
            for (v, span) in variants.iter().zip(&spans) {
                match v.payload {
                    Some(t) => fields.push(FieldDef { name: v.name, ty: t, is_pub: true, align: None }),
                    None => self.error(*span, "every field of a `#c_union` needs a type"),
                }
            }
            return AdtKind::CUnion { fields };
        }
        let tag = match explicit_tag {
            Some(t) => t,
            None => match variants.len() {
                0..=256 => Ty::u8(),
                257..=65536 => Ty::int(false, 16),
                _ => Ty::u32(),
            },
        };
        for (i, v) in variants.iter().enumerate() {
            if !tag.is_error() && !super::finalize::int_fits(&v.tag, tag, &self.target) {
                self.error(
                    spans[i],
                    format!("tag value {} of `{}` does not fit the tag type `{}`", v.tag, v.name, tag),
                );
            }
            if let Some(j) = variants[..i].iter().position(|w| w.tag == v.tag) {
                let d = Diagnostic::error(
                    spans[i],
                    format!("variants `{}` and `{}` have the same tag value {}", variants[j].name, v.name, v.tag),
                )
                .with_label(spans[j], "first used here");
                self.emit(d);
            }
        }
        AdtKind::Union { tag, variants }
    }

    fn attr_power_of_two(&mut self, module: ModuleId, a: &ast::Attribute) -> Option<u64> {
        let Some([arg]) = a.args.as_deref() else {
            self.error(a.span, "`#align` takes one argument: `#align(16)`");
            return None;
        };
        match self.try_untyped(module, arg) {
            Some(Ok(Untyped::Int(i))) => match i.to_u64() {
                Some(n) if n.is_power_of_two() => Some(n),
                _ => {
                    self.error(arg.span, "an alignment must be a power of two");
                    None
                }
            },
            _ => {
                self.error(arg.span, "an alignment must be an integer constant");
                None
            }
        }
    }

    /// True if `ty` contains a nominal type in `stack` by value.
    fn is_infinitely_sized(&self, ty: Ty, stack: &mut Vec<AdtId>) -> bool {
        match ty.kind() {
            TyKind::Adt(id, _) => {
                if stack.contains(id) {
                    return true;
                }
                let Some(def) = self.prog.adts.get(id) else { return false };
                stack.push(*id);
                let r = match &def.kind {
                    AdtKind::Struct { fields, .. } | AdtKind::CUnion { fields } => {
                        fields.iter().any(|f| self.is_infinitely_sized(f.ty, stack))
                    }
                    AdtKind::Union { variants, .. } => {
                        variants.iter().any(|v| v.payload.is_some_and(|p| self.is_infinitely_sized(p, stack)))
                    }
                };
                stack.pop();
                r
            }
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } | TyKind::Optional(elem) => {
                self.is_infinitely_sized(*elem, stack)
            }
            TyKind::Tuple { elems, .. } => elems.iter().any(|e| self.is_infinitely_sized(*e, stack)),
            TyKind::Result { err, ok } => self.is_infinitely_sized(*err, stack) || self.is_infinitely_sized(*ok, stack),
            _ => false,
        }
    }

    /// The fields of a struct type, if `ty` is one.
    pub fn struct_fields(&self, ty: Ty) -> Option<&[FieldDef]> {
        let TyKind::Adt(id, _) = ty.kind() else { return None };
        match &self.prog.adts.get(id)?.kind {
            AdtKind::Struct { fields, .. } => Some(fields),
            _ => None,
        }
    }

    /// The variants and tag type of a tagged union type, if `ty` is one.
    pub fn union_variants(&self, ty: Ty) -> Option<(&[VariantDef], Ty)> {
        let TyKind::Adt(id, _) = ty.kind() else { return None };
        match &self.prog.adts.get(id)?.kind {
            AdtKind::Union { variants, tag } => Some((variants, *tag)),
            _ => None,
        }
    }

    pub fn adt_def(&self, ty: Ty) -> Option<&tir::AdtDef> {
        let TyKind::Adt(id, _) = ty.kind() else { return None };
        self.prog.adts.get(id)
    }
}

fn s_empty() -> Vec<ast::Constraint> {
    vec![]
}

impl<'s> Sema<'s> {
    /// The instance of generic struct or union `template` for `args` (§16.4).
    pub fn instantiate_adt(&mut self, template: AdtId, args: Vec<Ty>) -> Ty {
        let gargs: Box<[crate::types::GenericArg]> = args.iter().map(|t| crate::types::GenericArg::Type(*t)).collect();
        if let Some(&inst) = self.adt_instances.get(&(template, args.clone())) {
            return Ty::intern(TyKind::Adt(inst, gargs));
        }
        let params = self.adt_params.get(&template).cloned().unwrap_or_default();
        let name = template.name();
        let inst = AdtId::fresh(name);
        inst.set_template(template);
        self.adt_instances.insert((template, args.clone()), inst);
        let ty = Ty::intern(TyKind::Adt(inst, gargs));
        let map: Vec<(crate::types::ParamId, Ty)> = params.iter().copied().zip(args.iter().copied()).collect();
        let info = &self.adts[&template];
        let (module, defaults, has_using) = (info.module, info.defaults.clone(), info.has_using);
        self.adts.insert(inst, AdtInfo { name, module, defaults, has_using });
        let placeholder =
            AdtDef { name, kind: AdtKind::Struct { fields: vec![], packed: false, align: None, bits: None } };
        self.prog.adts.insert(inst, placeholder);
        let def = self.prog.adts[&template].clone();
        let kind = match def.kind {
            AdtKind::Struct { fields, packed, align, bits } => AdtKind::Struct {
                fields: fields.into_iter().map(|f| FieldDef { ty: self.subst(f.ty, &map), ..f }).collect(),
                packed,
                align,
                bits,
            },
            AdtKind::Union { tag, variants } => AdtKind::Union {
                tag,
                variants: variants
                    .into_iter()
                    .map(|v| VariantDef { payload: v.payload.map(|p| self.subst(p, &map)), ..v })
                    .collect(),
            },
            AdtKind::CUnion { fields } => AdtKind::CUnion {
                fields: fields.into_iter().map(|f| FieldDef { ty: self.subst(f.ty, &map), ..f }).collect(),
            },
        };
        self.prog.adts.insert(inst, AdtDef { name, kind });
        ty
    }

    /// Replaces type parameters in `t`, re-instantiating generic types (§16.4).
    pub fn subst(&mut self, t: Ty, map: &[(crate::types::ParamId, Ty)]) -> Ty {
        if map.is_empty() || !super::generics::has_param(t) {
            return t;
        }
        t.map(&mut |x| match x.kind() {
            TyKind::Param(p) => map.iter().find(|(q, _)| q == p).map(|(_, a)| *a),
            TyKind::Adt(id, args) if !args.is_empty() => {
                let new_args: Vec<Ty> = args
                    .iter()
                    .map(|a| match a {
                        crate::types::GenericArg::Type(t) => self.subst(*t, map),
                        _ => Ty::error(),
                    })
                    .collect();
                Some(self.instantiate_adt(id.template(), new_args))
            }
            TyKind::Fn(sig) | TyKind::FnPtr(sig) => {
                let s = crate::types::FnTy {
                    params: sig.params.iter().map(|t| self.subst(*t, map)).collect(),
                    ret: self.subst(sig.ret, map),
                    effects: super::generics::subst_effects(&sig.effects, map),
                    variadic: sig.variadic,
                };
                Some(Ty::intern(if matches!(x.kind(), TyKind::Fn(_)) {
                    TyKind::Fn(Box::new(s))
                } else {
                    TyKind::FnPtr(Box::new(s))
                }))
            }
            _ => None,
        })
    }
}

impl<'s> Sema<'s> {
    /// The fields of a struct body, with each `when` replaced by its taken branch (§3.6).
    fn taken_struct_members(&mut self, module: ModuleId, members: &[ast::StructMember]) -> Vec<ast::FieldDecl> {
        let mut out = vec![];
        for m in members {
            match m {
                ast::StructMember::Field(f) => out.push(f.clone()),
                ast::StructMember::When { cond, then, else_, .. } => match self.when_cond(module, cond) {
                    Some(true) => out.extend(self.taken_struct_members(module, then)),
                    Some(false) => out.extend(self.taken_struct_members(module, else_.as_deref().unwrap_or(&[]))),
                    None => {}
                },
            }
        }
        out
    }

    /// The variants of a union body, with each `when` replaced by its taken branch.
    fn taken_union_members(&mut self, module: ModuleId, members: &[ast::UnionMember]) -> Vec<ast::Variant> {
        let mut out = vec![];
        for m in members {
            match m {
                ast::UnionMember::Variant(v) => out.push(v.clone()),
                ast::UnionMember::When { cond, then, else_, .. } => match self.when_cond(module, cond) {
                    Some(true) => out.extend(self.taken_union_members(module, then)),
                    Some(false) => out.extend(self.taken_union_members(module, else_.as_deref().unwrap_or(&[]))),
                    None => {}
                },
            }
        }
        out
    }
}
