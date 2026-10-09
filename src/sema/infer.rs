//! Literal inference (§6.1): type variables for untyped literals, unified with
//! each other and with concrete types, with fallbacks for the unconstrained.

use crate::types::{InferVar, Ty, TyKind};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VarKind {
    /// Created for an integer or character literal: any integer or float type.
    Int,
    /// Created for a float literal: float types only.
    Float,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Fallback {
    I32,
    /// Used as a shift amount.
    U32,
    /// Used as a range bound, array length, index or slicing bound.
    Usize,
}

#[derive(Clone, Debug)]
struct Var {
    parent: u32,
    kind: VarKind,
    /// Restricted to unsigned integer types (indices, shift amounts).
    unsigned: bool,
    fallback: Fallback,
    resolved: Option<Ty>,
}

#[derive(Default)]
pub struct InferTable {
    vars: Vec<Var>,
    /// Type parameters with a `Num` constraint, at which untyped literals are allowed (§16.3).
    pub numeric_params: Vec<crate::types::ParamId>,
}

/// Why two types could not be unified.
#[derive(Debug)]
pub struct Mismatch;

impl InferTable {
    pub fn new_var(&mut self, kind: VarKind) -> Ty {
        let v = InferVar(self.vars.len() as u32);
        self.vars.push(Var { parent: v.0, kind, unsigned: false, fallback: Fallback::I32, resolved: None });
        Ty::infer(v)
    }

    fn root(&mut self, v: u32) -> u32 {
        let p = self.vars[v as usize].parent;
        if p == v {
            return v;
        }
        let r = self.root(p);
        self.vars[v as usize].parent = r;
        r
    }

    /// Replaces resolved variables at the top level of `ty` (not inside it).
    pub fn shallow(&mut self, ty: Ty) -> Ty {
        if let TyKind::Infer(v) = ty.kind() {
            let r = self.root(v.0);
            return self.vars[r as usize].resolved.unwrap_or(Ty::infer(InferVar(r)));
        }
        ty
    }

    /// Replaces every resolved variable in `ty`.
    pub fn resolve(&mut self, ty: Ty) -> Ty {
        if !ty.has_infer() {
            return ty;
        }
        ty.map(&mut |t| if t.is_infer() { Some(self.shallow(t)) } else { None })
    }

    /// The kind of an unresolved variable type, if `ty` is one.
    pub fn var_kind(&mut self, ty: Ty) -> Option<VarKind> {
        match self.shallow(ty).kind() {
            TyKind::Infer(v) => {
                let r = self.root(v.0);
                Some(self.vars[r as usize].kind)
            }
            _ => None,
        }
    }

    /// Restricts the variable `ty` (if unresolved) to unsigned types and raises its
    /// fallback. Returns false if `ty` is a concrete type that is not an unsigned integer.
    pub fn require_unsigned(&mut self, ty: Ty, fallback: Fallback) -> bool {
        match self.shallow(ty).kind() {
            TyKind::Infer(v) => {
                let r = self.root(v.0) as usize;
                if self.vars[r].kind == VarKind::Float {
                    return false;
                }
                self.vars[r].unsigned = true;
                self.vars[r].fallback = self.vars[r].fallback.max(fallback);
                true
            }
            _ => self.shallow(ty).is_unsigned_integer(),
        }
    }

    /// Raises the fallback of an unresolved variable (e.g. for range bounds).
    pub fn set_fallback(&mut self, ty: Ty, fallback: Fallback) {
        if let TyKind::Infer(v) = self.shallow(ty).kind() {
            let r = self.root(v.0) as usize;
            self.vars[r].fallback = self.vars[r].fallback.max(fallback);
        }
    }

    fn compatible(&self, var: &Var, ty: Ty) -> bool {
        if let TyKind::Param(p) = ty.kind() {
            return !var.unsigned && self.numeric_params.contains(p);
        }
        match var.kind {
            VarKind::Float => ty.is_float(),
            VarKind::Int if var.unsigned => ty.is_unsigned_integer(),
            VarKind::Int => ty.is_integer() || ty.is_float(),
        }
    }

    /// Makes `a` and `b` the same type.
    pub fn unify(&mut self, a: Ty, b: Ty) -> Result<(), Mismatch> {
        let (a, b) = (self.shallow(a), self.shallow(b));
        if a == b || a.is_error() || b.is_error() {
            return Ok(());
        }
        match (a.kind(), b.kind()) {
            (TyKind::Infer(x), TyKind::Infer(y)) => {
                let (rx, ry) = (self.root(x.0), self.root(y.0));
                let (vx, vy) = (self.vars[rx as usize].clone(), self.vars[ry as usize].clone());
                let kind =
                    if vx.kind == VarKind::Float || vy.kind == VarKind::Float { VarKind::Float } else { VarKind::Int };
                let unsigned = vx.unsigned || vy.unsigned;
                if kind == VarKind::Float && unsigned {
                    return Err(Mismatch);
                }
                self.vars[rx as usize].parent = ry;
                let v = &mut self.vars[ry as usize];
                v.kind = kind;
                v.unsigned = unsigned;
                v.fallback = vx.fallback.max(vy.fallback);
                Ok(())
            }
            (TyKind::Infer(x), _) => self.bind(x.0, b),
            (_, TyKind::Infer(y)) => self.bind(y.0, a),
            _ if a.has_infer() || b.has_infer() => self.unify_structural(a, b),
            _ => Err(Mismatch),
        }
    }

    fn bind(&mut self, v: u32, ty: Ty) -> Result<(), Mismatch> {
        let r = self.root(v) as usize;
        if !self.compatible(&self.vars[r], ty) {
            return Err(Mismatch);
        }
        self.vars[r].resolved = Some(ty);
        Ok(())
    }

    fn unify_structural(&mut self, a: Ty, b: Ty) -> Result<(), Mismatch> {
        use TyKind::*;
        match (a.kind(), b.kind()) {
            (Ptr { is_const: c1, pointee: p1 }, Ptr { is_const: c2, pointee: p2 }) if c1 == c2 => self.unify(*p1, *p2),
            (ManyPtr { is_const: c1, elem: e1 }, ManyPtr { is_const: c2, elem: e2 })
            | (Slice { is_const: c1, elem: e1 }, Slice { is_const: c2, elem: e2 })
                if c1 == c2 =>
            {
                self.unify(*e1, *e2)
            }
            (Array { len: l1, elem: e1 }, Array { len: l2, elem: e2 }) if l1 == l2 => self.unify(*e1, *e2),
            (ParamArray { len: l1, elem: e1 }, ParamArray { len: l2, elem: e2 }) if l1 == l2 => self.unify(*e1, *e2),
            (Optional(x), Optional(y)) | (Cont(x), Cont(y)) => self.unify(*x, *y),
            (Result { err: e1, ok: o1 }, Result { err: e2, ok: o2 }) => {
                self.unify(*e1, *e2)?;
                self.unify(*o1, *o2)
            }
            (Tuple { elems: x, names: n1 }, Tuple { elems: y, names: n2 }) if x.len() == y.len() && n1 == n2 => {
                for (p, q) in x.iter().zip(y.iter()) {
                    self.unify(*p, *q)?;
                }
                Ok(())
            }
            _ => Err(Mismatch),
        }
    }

    /// Gives the unresolved variables in `ty` their fallback types now (§6.1).
    pub fn settle(&mut self, ty: Ty) -> Ty {
        let t = self.resolve(ty);
        let mut vars = vec![];
        t.walk(&mut |x| {
            if let TyKind::Infer(v) = x.kind() {
                vars.push(v.0);
            }
        });
        for v in vars {
            let r = self.root(v) as usize;
            if self.vars[r].resolved.is_none() {
                let var = &self.vars[r];
                let fixed = match (var.kind, var.fallback) {
                    (VarKind::Float, _) => Ty::f64(),
                    (VarKind::Int, Fallback::Usize) => Ty::usize(),
                    (VarKind::Int, Fallback::U32) => Ty::u32(),
                    (VarKind::Int, Fallback::I32) if var.unsigned => Ty::u32(),
                    (VarKind::Int, Fallback::I32) => Ty::i32(),
                };
                self.vars[r].resolved = Some(fixed);
            }
        }
        self.resolve(ty)
    }

    /// Gives every unresolved variable its fallback type (§6.1).
    pub fn apply_fallbacks(&mut self) {
        for i in 0..self.vars.len() as u32 {
            let r = self.root(i) as usize;
            if self.vars[r].resolved.is_none() {
                let v = &self.vars[r];
                let ty = match (v.kind, v.fallback) {
                    (VarKind::Float, _) => Ty::f64(),
                    (VarKind::Int, Fallback::Usize) => Ty::usize(),
                    (VarKind::Int, Fallback::U32) => Ty::u32(),
                    (VarKind::Int, Fallback::I32) => {
                        if v.unsigned {
                            Ty::u32()
                        } else {
                            Ty::i32()
                        }
                    }
                };
                self.vars[r].resolved = Some(ty);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unify_and_fallback() {
        let mut t = InferTable::default();
        let a = t.new_var(VarKind::Int);
        let b = t.new_var(VarKind::Int);
        let c = t.new_var(VarKind::Float);
        let d = t.new_var(VarKind::Int);
        t.unify(a, b).unwrap();
        t.unify(b, Ty::u8()).unwrap();
        assert_eq!(t.resolve(a), Ty::u8());
        assert!(t.unify(c, Ty::i32()).is_err());
        t.set_fallback(d, Fallback::Usize);
        t.apply_fallbacks();
        assert_eq!(t.resolve(c), Ty::f64());
        assert_eq!(t.resolve(d), Ty::usize());
        let e = t.new_var(VarKind::Int);
        let f = t.new_var(VarKind::Float);
        t.unify(e, f).unwrap();
        t.apply_fallbacks();
        assert_eq!(t.resolve(e), Ty::f64());
    }

    #[test]
    fn unsigned_restriction() {
        let mut t = InferTable::default();
        let a = t.new_var(VarKind::Int);
        assert!(t.require_unsigned(a, Fallback::Usize));
        assert!(t.unify(a, Ty::i32()).is_err());
        let tup = Ty::tuple(vec![a, Ty::bool()]);
        t.unify(tup, Ty::tuple(vec![Ty::u16_for_test(), Ty::bool()])).unwrap();
        assert_eq!(t.resolve(tup), Ty::tuple(vec![Ty::u16_for_test(), Ty::bool()]));
    }

    trait TestTy {
        fn u16_for_test() -> Ty;
    }
    impl TestTy for Ty {
        fn u16_for_test() -> Ty {
            Ty::int(false, 16)
        }
    }
}
