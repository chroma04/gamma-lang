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

/// How deeply instantiating a generic type may instantiate others (§16.7).
const MAX_ADT_INST_DEPTH: u32 = 64;

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
            let is_effects = p.ty.as_ref().is_some_and(super::decls::is_effects_kind);
            let is_type = is_effects
                || p.ty.as_ref().is_some_and(|t| matches!(&t.kind, ExprKind::Ident(i) if i.name.as_str() == "type"));
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
                if is_effects {
                    self.effect_params.insert(pid);
                }
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
            self.adt_spans.insert(id, lit.span);
            self.adt_params.insert(id, generics.clone());
            self.adt_param_defaults.insert(id, defaults);
        }
        // The type is usable (through pointers) by its own fields.
        if let Some(d) = decl {
            self.adt_decls.insert(id, d);
            let v = if generics.is_empty() { DeclValue::Type(ty) } else { DeclValue::GenericType(id) };
            self.decls[d.0 as usize].state = DeclState::Done(v);
        }
        self.adts.insert(id, AdtInfo { name, module, defaults: vec![], has_using: false });
        let placeholder =
            AdtDef { name, kind: AdtKind::Struct { fields: vec![], packed: false, align: None, bits: None } };
        self.prog.adts.insert(id, placeholder);
        self.adts_in_progress.push(id);
        self.constraint_defer += 1;
        let kind = match &inner.kind {
            ExprKind::Struct(s) => self.struct_kind(module, id, s, attrs),
            ExprKind::Union(u) => {
                if u.tag_type.is_some() {
                    self.explicit_tags.insert(id);
                }
                self.union_kind(module, u, attrs)
            }
            _ => {
                self.error(lit.span, "expected a struct or union literal after the attributes");
                self.adts_in_progress.retain(|&a| a != id);
                self.end_constraint_defer();
                return Ty::error();
            }
        };
        self.adts_in_progress.retain(|&a| a != id);
        self.prog.adts.insert(id, AdtDef { name, kind });
        let wheres = match &inner.kind {
            ExprKind::Struct(s) => &s.where_,
            ExprKind::Union(u) => &u.where_,
            _ => &s_empty(),
        };
        for c in wheres.clone() {
            self.declare_constraint(module, &c);
        }
        self.end_constraint_defer();
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
            self.empty_adt(id, name);
            return Ty::error();
        }
        if !self.type_size_ok(ty, lit.span) {
            self.empty_adt(id, name);
            return Ty::error();
        }
        ty
    }

    /// Leaves a type that was reported as ill-formed without fields or variants, so that
    /// nothing walks it later.
    fn empty_adt(&mut self, id: AdtId, name: Symbol) {
        let empty = AdtKind::Struct { fields: vec![], packed: false, align: None, bits: None };
        self.prog.adts.insert(id, AdtDef { name, kind: empty });
        if let Some(info) = self.adts.get_mut(&id) {
            info.has_using = false;
            info.defaults.clear();
        }
    }

    /// Reports `ty` if it is larger than any object can be (`isize::MAX` bytes on the
    /// target), and returns whether it is fine.
    pub fn type_size_ok(&mut self, ty: Ty, span: Span) -> bool {
        let max = self.target.max_object_size();
        let aggregate = matches!(
            ty.kind(),
            TyKind::Array { .. } | TyKind::Tuple { .. } | TyKind::Optional(_) | TyKind::Result { .. } | TyKind::Adt(..)
        );
        if !aggregate || super::generics::has_param(ty) || self.with_layouts(|l| l.size_of(ty)) <= max {
            return true;
        }
        self.error(span, format!("the type `{}` is too large: an object is at most {} bytes", ty, max));
        false
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
            // A default that failed (and was reported) still counts as a default, so that
            // constructions do not report the field as missing as well.
            // A default is evaluated once (§17.2), unless its type mentions a type parameter
            // of the struct: then each use checks it.
            let default = f.default.as_ref().map(|d| {
                let v = if super::generics::has_param(ty) {
                    self.check_const_expr(module, d, Some(ty))
                } else {
                    self.default_value(module, d, ty)
                };
                v.unwrap_or_else(|| super::func::error_expr(d.span))
            });
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
                        "field `{}` of a `#bits` struct must be `bool`, an integer, an enum with an explicit tag type or another `#bits` struct",
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

    pub(super) fn bit_width(&self, t: Ty) -> Option<u64> {
        match t.kind() {
            TyKind::Bool => Some(1),
            TyKind::Int(i) => Some(i.bits as u64),
            TyKind::Adt(id, _) => match &self.prog.adts.get(id)?.kind {
                AdtKind::Struct { bits: Some(u), .. } => self.bit_width(*u),
                // An enum with an explicit tag type occupies its tag's bits.
                AdtKind::Union { tag, variants }
                    if self.explicit_tags.contains(id) && variants.iter().all(|v| v.payload.is_none()) =>
                {
                    self.bit_width(*tag)
                }
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
        // Without a usable tag type, the tag values are not checked against it.
        let explicit_tag = explicit_tag.map(|t| if t.is_integer() { t } else { Ty::error() });
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

    pub(super) fn attr_power_of_two(&mut self, module: ModuleId, a: &ast::Attribute) -> Option<u64> {
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

    /// The fields of a `#c_union` type, if `ty` is one.
    pub fn c_union_fields(&self, ty: Ty) -> Option<&[FieldDef]> {
        let TyKind::Adt(id, _) = ty.kind() else { return None };
        match &self.prog.adts.get(id)?.kind {
            AdtKind::CUnion { fields } => Some(fields),
            _ => None,
        }
    }

    /// The underlying integer type of a `#bits` struct, if `ty` is one.
    pub fn bits_struct_ty(&self, ty: Ty) -> Option<Ty> {
        let TyKind::Adt(id, _) = ty.kind() else { return None };
        match &self.prog.adts.get(id)?.kind {
            AdtKind::Struct { bits: Some(u), .. } => Some(*u),
            _ => None,
        }
    }

    /// Whether `T(x)` with `x: from` converts losslessly to `to` (§6.5): numeric
    /// widenings, `bool` and enum tags to integers, and a `#bits` struct of plain
    /// fields to and from its underlying integer.
    pub fn conversion_ok(&self, from: Ty, to: Ty) -> bool {
        if super::finalize::lossless(from, to) {
            return true;
        }
        let plain_bits = |t: Ty| self.bits_struct_ty(t).filter(|_| self.is_plain(t));
        if plain_bits(from) == Some(to) || plain_bits(to) == Some(from) {
            return true;
        }
        // An enum (a union without payloads) converts by its tag.
        if let Some((variants, tag)) = self.union_variants(from)
            && variants.iter().all(|v| v.payload.is_none())
            && to.is_integer()
        {
            return super::finalize::lossless(tag, to);
        }
        false
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
    /// The core library's generic `Range` or `RangeIncl` struct (§5.16).
    pub fn range_template(&mut self, inclusive: bool) -> Option<AdtId> {
        let name = crate::symbol::Symbol::intern(if inclusive { "RangeIncl" } else { "Range" });
        let d = self.prelude.and_then(|p| self.modules[p.0 as usize].scope.get(&name).copied())?;
        match self.resolve_decl(d) {
            super::DeclValue::GenericType(id) => Some(id),
            _ => None,
        }
    }

    /// If `t` is `Range(T)` or `RangeIncl(T)`: whether it is inclusive, and `T`.
    pub fn range_of(&mut self, t: Ty) -> Option<(bool, Ty)> {
        let TyKind::Adt(id, args) = t.kind() else { return None };
        let template = id.template();
        let inclusive = if Some(template) == self.range_template(false) {
            false
        } else if Some(template) == self.range_template(true) {
            true
        } else {
            return None;
        };
        match args.first() {
            Some(crate::types::GenericArg::Type(e)) => Some((inclusive, *e)),
            _ => None,
        }
    }

    /// The instance of generic struct or union `template` for `args` (§16.4).
    pub fn instantiate_adt(&mut self, template: AdtId, args: Vec<Ty>) -> Ty {
        let gargs: Box<[crate::types::GenericArg]> = args.iter().map(|t| crate::types::GenericArg::Type(*t)).collect();
        if let Some(&inst) = self.adt_instances.get(&(template, args.clone())) {
            return Ty::intern(TyKind::Adt(inst, gargs));
        }
        // Instantiating a type's fields instantiates the types they mention, which must
        // end: `Bad :: struct(T: type) { next: ?^Bad(?T) }` would go on forever (§16.7).
        if self.adt_inst_depth >= MAX_ADT_INST_DEPTH {
            if !self.adt_inst_overflowed {
                self.adt_inst_overflowed = true;
                let span = self.adt_spans[&template];
                self.error(
                    span,
                    format!(
                        "`{}` instantiates itself with ever-larger type arguments (more than {} deep) (§16.7)",
                        template.name(),
                        MAX_ADT_INST_DEPTH
                    ),
                );
            }
            return Ty::error();
        }
        let params = self.adt_params.get(&template).cloned().unwrap_or_default();
        let name = template.name();
        let inst = AdtId::fresh(name);
        inst.set_template(template);
        self.adt_instances.insert((template, args.clone()), inst);
        let ty = Ty::intern(TyKind::Adt(inst, gargs));
        let map: Vec<(crate::types::ParamId, Ty)> = params.iter().copied().zip(args.iter().copied()).collect();
        self.check_len_exprs(&map, None);
        let info = &self.adts[&template];
        let (module, defaults, has_using) = (info.module, info.defaults.clone(), info.has_using);
        self.adts.insert(inst, AdtInfo { name, module, defaults, has_using });
        let placeholder =
            AdtDef { name, kind: AdtKind::Struct { fields: vec![], packed: false, align: None, bits: None } };
        self.prog.adts.insert(inst, placeholder);
        let def = self.prog.adts[&template].clone();
        self.adt_inst_depth += 1;
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
        self.adt_inst_depth -= 1;
        if !args.iter().any(|a| super::generics::has_param(*a)) {
            self.adt_new_instances.push((template, ty));
        }
        // Once the outermost instantiation is complete, no instance may contain itself by
        // value (as for non-generic types). Such an instance is left without fields, so
        // that computing its layout ends.
        if self.adt_inst_depth == 0 {
            let new_all = std::mem::take(&mut self.adt_new_instances);
            let infinite: Vec<(AdtId, Ty)> = new_all
                .iter()
                .copied()
                .filter(|&(_, t)| matches!(t.kind(), TyKind::Adt(id, _) if self.contains_by_value(t, *id, &mut vec![])))
                .collect();
            for &(template, t) in &infinite {
                let TyKind::Adt(id, _) = t.kind() else { continue };
                let span = self.adt_spans[&template];
                self.error(
                    span,
                    format!("`{}` contains itself by value and would be infinitely large; use a pointer", t),
                );
                self.empty_adt(*id, template.name());
            }
            for (template, t) in new_all {
                let TyKind::Adt(id, _) = t.kind() else { continue };
                if !infinite.iter().any(|&(_, i)| i == t) && !self.type_size_ok(t, self.adt_spans[&template]) {
                    self.empty_adt(*id, template.name());
                }
            }
        }
        ty
    }

    /// A struct or union whose fields are still being resolved that a value of type `ty`
    /// contains, so that its layout is not known yet: `N :: @size_of(S)` used in a field
    /// of `S` (a cycle, §3.4).
    pub fn pending_layout(&self, ty: Ty) -> Option<AdtId> {
        if self.adts_in_progress.is_empty() {
            return None;
        }
        match ty.kind() {
            TyKind::Adt(id, _) => {
                if self.adts_in_progress.contains(id) {
                    return Some(*id);
                }
                match &self.prog.adts.get(id)?.kind {
                    AdtKind::Struct { fields, .. } | AdtKind::CUnion { fields } => {
                        fields.iter().find_map(|f| self.pending_layout(f.ty))
                    }
                    AdtKind::Union { variants, .. } => variants.iter().find_map(|v| self.pending_layout(v.payload?)),
                }
            }
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } | TyKind::Optional(elem) => {
                self.pending_layout(*elem)
            }
            TyKind::Tuple { elems, .. } => elems.iter().find_map(|e| self.pending_layout(*e)),
            TyKind::Result { err, ok } => self.pending_layout(*err).or_else(|| self.pending_layout(*ok)),
            _ => None,
        }
    }

    /// Whether a value of type `ty` contains a value of the struct or union `target`.
    fn contains_by_value(&self, ty: Ty, target: AdtId, seen: &mut Vec<AdtId>) -> bool {
        match ty.kind() {
            TyKind::Adt(id, _) => {
                if *id == target && !seen.is_empty() {
                    return true;
                }
                if seen.contains(id) {
                    return false;
                }
                seen.push(*id);
                let Some(def) = self.prog.adts.get(id) else { return false };
                match &def.kind {
                    AdtKind::Struct { fields, .. } | AdtKind::CUnion { fields } => {
                        fields.iter().any(|f| self.contains_by_value(f.ty, target, seen))
                    }
                    AdtKind::Union { variants, .. } => {
                        variants.iter().any(|v| v.payload.is_some_and(|p| self.contains_by_value(p, target, seen)))
                    }
                }
            }
            TyKind::Array { elem, .. } | TyKind::ParamArray { elem, .. } | TyKind::Optional(elem) => {
                self.contains_by_value(*elem, target, seen)
            }
            TyKind::Tuple { elems, .. } => elems.iter().any(|e| self.contains_by_value(*e, target, seen)),
            TyKind::Result { err, ok } => {
                self.contains_by_value(*err, target, seen) || self.contains_by_value(*ok, target, seen)
            }
            _ => false,
        }
    }

    /// [`Sema::subst`] on the type arguments of an effect.
    pub fn subst_eref(
        &mut self,
        e: &crate::types::EffectRef,
        map: &[(crate::types::ParamId, Ty)],
    ) -> crate::types::EffectRef {
        let args = e
            .args
            .iter()
            .map(|a| match a {
                crate::types::GenericArg::Type(t) => crate::types::GenericArg::Type(self.subst(*t, map)),
                v => v.clone(),
            })
            .collect();
        crate::types::EffectRef { effect: e.effect, args }
    }

    /// [`Sema::subst`] on the arguments of each effect in `s`.
    pub fn subst_effects(
        &mut self,
        s: &crate::types::EffectSet,
        map: &[(crate::types::ParamId, Ty)],
    ) -> crate::types::EffectSet {
        let effects = s.effects.iter().map(|e| self.subst_eref(e, map)).collect();
        crate::types::EffectSet::new(effects).flatten()
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
                    effects: self.subst_effects(&sig.effects, map),
                    variadic: sig.variadic,
                    callconv: sig.callconv,
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
