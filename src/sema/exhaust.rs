//! Exhaustiveness and reachability of `match` arms (§8.6), by the usefulness
//! algorithm over deconstructed patterns. Integer patterns are handled by
//! splitting the type's range at the boundaries of the ranges in a column.

use crate::layout::{Target, int_bits};
use crate::tir::{AdtDef, AdtKind, ExprKind, Pat, PatKind};
use crate::types::{AdtId, Ty, TyKind};
use num_bigint::BigInt;
use num_traits::One;
use std::collections::HashMap;

#[derive(Clone, Debug, PartialEq)]
enum Ctor {
    Bool(bool),
    Null,
    Some,
    Ok,
    Err,
    Variant(u32),
    /// The single constructor of a tuple or struct.
    Single,
    /// An inclusive range of integers.
    Range(BigInt, BigInt),
    Str(Vec<u8>),
}

#[derive(Clone, Debug)]
enum DPat {
    Wild(Ty),
    Ctor(Ty, Ctor, Vec<DPat>),
}

impl DPat {
    fn ty(&self) -> Ty {
        match self {
            DPat::Wild(t) | DPat::Ctor(t, _, _) => *t,
        }
    }
}

pub struct Checker<'a> {
    pub adts: &'a HashMap<AdtId, AdtDef>,
    pub target: &'a Target,
}

/// The outcome of checking one `match`.
pub struct MatchReport {
    /// A pattern (as source text) that no arm matches, if the match is not exhaustive.
    pub missing: Option<String>,
    /// Indices of arms that can never be chosen.
    pub unreachable: Vec<usize>,
}

impl<'a> Checker<'a> {
    /// `arms` are (pattern, has_guard) in order; `ty` is the scrutinee type.
    pub fn check_match(&self, ty: Ty, arms: &[(&Pat, bool)]) -> MatchReport {
        let mut rows: Vec<Vec<DPat>> = vec![];
        let mut unreachable = vec![];
        for (i, (p, guarded)) in arms.iter().enumerate() {
            let alts = self.alternatives(p);
            let mut any_useful = false;
            for alt in &alts {
                if self.useful(&rows, std::slice::from_ref(alt)).is_some() {
                    any_useful = true;
                }
            }
            if !any_useful {
                unreachable.push(i);
            }
            if !guarded {
                for alt in alts {
                    rows.push(vec![alt]);
                }
            }
        }
        let missing = self.useful(&rows, &[DPat::Wild(ty)]).map(|w| self.show(&w[0]));
        MatchReport { missing, unreachable }
    }

    /// True if the pattern matches every value of its type (§9.4).
    pub fn irrefutable(&self, p: &Pat) -> bool {
        let rows: Vec<Vec<DPat>> = self.alternatives(p).into_iter().map(|d| vec![d]).collect();
        self.useful(&rows, &[DPat::Wild(p.ty)]).is_none()
    }

    fn alternatives(&self, p: &Pat) -> Vec<DPat> {
        match &p.kind {
            PatKind::Or(ps) => ps.iter().flat_map(|q| self.alternatives(q)).collect(),
            _ => self.lower_product(p),
        }
    }

    /// Lowers a pattern; nested or-patterns expand into several patterns.
    fn lower_product(&self, p: &Pat) -> Vec<DPat> {
        let ty = p.ty;
        let one = |c: Ctor, subs: Vec<DPat>| vec![DPat::Ctor(ty, c, subs)];
        match &p.kind {
            PatKind::Wild | PatKind::Bind(_) => vec![DPat::Wild(ty)],
            PatKind::Or(ps) => ps.iter().flat_map(|q| self.lower_product(q)).collect(),
            PatKind::Const(e) => match &e.kind {
                ExprKind::Bool(b) => one(Ctor::Bool(*b), vec![]),
                ExprKind::Int(v) => one(Ctor::Range(v.clone(), v.clone()), vec![]),
                ExprKind::Str(s) => one(Ctor::Str(s.to_vec()), vec![]),
                _ => vec![DPat::Wild(ty)],
            },
            PatKind::Range { lo, hi, inclusive } => {
                let hi = if *inclusive { hi.clone() } else { hi - 1 };
                one(Ctor::Range(lo.clone(), hi), vec![])
            }
            PatKind::Null => one(Ctor::Null, vec![]),
            PatKind::Some(q) => {
                self.lower_product(q).into_iter().map(|d| DPat::Ctor(ty, Ctor::Some, vec![d])).collect()
            }
            PatKind::Ok(q) => self.lower_product(q).into_iter().map(|d| DPat::Ctor(ty, Ctor::Ok, vec![d])).collect(),
            PatKind::Err(q) => self.lower_product(q).into_iter().map(|d| DPat::Ctor(ty, Ctor::Err, vec![d])).collect(),
            PatKind::Variant { variant, payload } => {
                let subs_tys = self.sub_types(ty, &Ctor::Variant(*variant));
                match (payload, subs_tys.first()) {
                    (Some(q), _) => self
                        .lower_product(q)
                        .into_iter()
                        .map(|d| DPat::Ctor(ty, Ctor::Variant(*variant), vec![d]))
                        .collect(),
                    (None, Some(t)) => one(Ctor::Variant(*variant), vec![DPat::Wild(*t)]),
                    (None, None) => one(Ctor::Variant(*variant), vec![]),
                }
            }
            PatKind::Fields(fs) => {
                let tys = self.sub_types(ty, &Ctor::Single);
                let mut combos: Vec<Vec<DPat>> = vec![tys.iter().map(|t| DPat::Wild(*t)).collect()];
                for (i, q) in fs {
                    let options = self.lower_product(q);
                    let mut next = vec![];
                    for c in &combos {
                        for o in &options {
                            let mut c2 = c.clone();
                            if (*i as usize) < c2.len() {
                                c2[*i as usize] = o.clone();
                            }
                            next.push(c2);
                        }
                    }
                    combos = next;
                }
                combos.into_iter().map(|subs| DPat::Ctor(ty, Ctor::Single, subs)).collect()
            }
        }
    }

    fn sub_types(&self, ty: Ty, c: &Ctor) -> Vec<Ty> {
        match (ty.kind(), c) {
            (TyKind::Optional(t), Ctor::Some) => vec![*t],
            (TyKind::Result { ok, .. }, Ctor::Ok) => vec![*ok],
            (TyKind::Result { err, .. }, Ctor::Err) => vec![*err],
            (TyKind::Tuple { elems, .. }, Ctor::Single) => elems.to_vec(),
            (TyKind::Adt(id, _), _) => match (self.adts.get(id).map(|d| &d.kind), c) {
                (Some(AdtKind::Struct { fields, .. }), Ctor::Single) => fields.iter().map(|f| f.ty).collect(),
                (Some(AdtKind::Union { variants, .. }), Ctor::Variant(v)) => {
                    variants[*v as usize].payload.into_iter().collect()
                }
                _ => vec![],
            },
            _ => vec![],
        }
    }

    /// Every constructor of `ty`, split to agree with the ranges in `column`;
    /// `None` if the type has too many values to enumerate.
    fn all_ctors(&self, ty: Ty, column: &[&DPat]) -> Option<Vec<Ctor>> {
        match ty.kind() {
            TyKind::Bool => Some(vec![Ctor::Bool(false), Ctor::Bool(true)]),
            TyKind::Optional(_) => Some(vec![Ctor::Null, Ctor::Some]),
            TyKind::Result { .. } => Some(vec![Ctor::Ok, Ctor::Err]),
            TyKind::Tuple { .. } => Some(vec![Ctor::Single]),
            TyKind::Never => Some(vec![]),
            TyKind::Adt(id, _) => match self.adts.get(id).map(|d| &d.kind) {
                Some(AdtKind::Struct { .. }) => Some(vec![Ctor::Single]),
                Some(AdtKind::Union { variants, .. }) => Some((0..variants.len() as u32).map(Ctor::Variant).collect()),
                _ => None,
            },
            _ if ty.is_integer() => {
                let (signed, bits) = int_bits(ty, self.target)?;
                let (min, max) = if signed {
                    (-(BigInt::one() << (bits - 1) as usize), (BigInt::one() << (bits - 1) as usize) - 1)
                } else {
                    (BigInt::from(0), (BigInt::one() << bits as usize) - 1)
                };
                let mut cuts = vec![min.clone(), &max + 1];
                for d in column {
                    if let DPat::Ctor(_, Ctor::Range(lo, hi), _) = d {
                        cuts.push(lo.clone());
                        cuts.push(hi + 1);
                    }
                }
                cuts.sort();
                cuts.dedup();
                let mut out = vec![];
                for w in cuts.windows(2) {
                    if w[0] >= min && w[1] <= &max + 1 && w[0] < w[1] {
                        out.push(Ctor::Range(w[0].clone(), &w[1] - 1));
                    }
                }
                Some(out)
            }
            _ => None,
        }
    }

    fn covers(head: &Ctor, c: &Ctor) -> bool {
        match (head, c) {
            (Ctor::Range(a, b), Ctor::Range(x, y)) => a <= x && y <= b,
            _ => head == c,
        }
    }

    fn specialize(&self, row: &[DPat], c: &Ctor, arity_tys: &[Ty]) -> Option<Vec<DPat>> {
        let (head, rest) = row.split_first()?;
        let mut out: Vec<DPat> = match head {
            DPat::Wild(_) => arity_tys.iter().map(|t| DPat::Wild(*t)).collect(),
            DPat::Ctor(_, hc, subs) => {
                if !Self::covers(hc, c) {
                    return None;
                }
                subs.clone()
            }
        };
        out.extend_from_slice(rest);
        Some(out)
    }

    /// If `q` matches some value no row matches, returns such a value (one pattern per column).
    fn useful(&self, rows: &[Vec<DPat>], q: &[DPat]) -> Option<Vec<DPat>> {
        let Some((head, rest)) = q.split_first() else {
            return if rows.is_empty() { Some(vec![]) } else { None };
        };
        let ty = head.ty();
        let column: Vec<&DPat> = rows.iter().filter_map(|r| r.first()).collect();
        match head {
            DPat::Ctor(_, c, _) => {
                // A range in `q` is useful if any piece of it is.
                let pieces = match c {
                    Ctor::Range(lo, hi) => {
                        let all = self.all_ctors(ty, &[column.as_slice(), &[head]].concat()).unwrap_or_default();
                        all.into_iter()
                            .filter(|p| matches!(p, Ctor::Range(x, y) if x >= lo && y <= hi))
                            .collect::<Vec<_>>()
                    }
                    _ => vec![c.clone()],
                };
                for piece in pieces {
                    let tys = self.sub_types(ty, &piece);
                    let spec_rows: Vec<Vec<DPat>> =
                        rows.iter().filter_map(|r| self.specialize(r, &piece, &tys)).collect();
                    let spec_q = self.specialize(q, &piece, &tys)?;
                    if let Some(w) = self.useful(&spec_rows, &spec_q) {
                        return Some(rebuild(ty, piece, tys.len(), w));
                    }
                }
                None
            }
            DPat::Wild(_) => {
                let ctors = self.all_ctors(ty, &column);
                let complete = match &ctors {
                    Some(cs) => cs
                        .iter()
                        .all(|c| column.iter().any(|d| matches!(d, DPat::Ctor(_, hc, _) if Self::covers(hc, c)))),
                    None => false,
                };
                if complete {
                    for c in ctors.unwrap() {
                        let tys = self.sub_types(ty, &c);
                        let spec_rows: Vec<Vec<DPat>> =
                            rows.iter().filter_map(|r| self.specialize(r, &c, &tys)).collect();
                        let spec_q = self.specialize(q, &c, &tys).unwrap();
                        if let Some(w) = self.useful(&spec_rows, &spec_q) {
                            return Some(rebuild(ty, c, tys.len(), w));
                        }
                    }
                    return None;
                }
                let default: Vec<Vec<DPat>> =
                    rows.iter().filter(|r| matches!(r.first(), Some(DPat::Wild(_)))).map(|r| r[1..].to_vec()).collect();
                let w = self.useful(&default, rest)?;
                // Name a constructor that no row covers, if there is one to name.
                let missing = ctors.and_then(|cs| {
                    cs.into_iter()
                        .find(|c| !column.iter().any(|d| matches!(d, DPat::Ctor(_, hc, _) if Self::covers(hc, c))))
                });
                let head = match missing {
                    Some(c) => {
                        let subs = self.sub_types(ty, &c).into_iter().map(DPat::Wild).collect();
                        DPat::Ctor(ty, c, subs)
                    }
                    None => DPat::Wild(ty),
                };
                let mut out = vec![head];
                out.extend(w);
                Some(out)
            }
        }
    }

    /// Renders a witness as Gamma pattern syntax.
    fn show(&self, d: &DPat) -> String {
        match d {
            DPat::Wild(_) => "_".to_string(),
            DPat::Ctor(ty, c, subs) => match c {
                Ctor::Bool(b) => b.to_string(),
                Ctor::Null => "null".to_string(),
                Ctor::Some | Ctor::Ok => self.show(&subs[0]),
                Ctor::Err => format!("!{}", self.show(&subs[0])),
                Ctor::Range(lo, hi) if lo == hi => lo.to_string(),
                Ctor::Range(lo, hi) => format!("{}..={}", lo, hi),
                Ctor::Str(s) => format!("{:?}", String::from_utf8_lossy(s)),
                Ctor::Variant(v) => {
                    let name = match ty.kind() {
                        TyKind::Adt(id, _) => match self.adts.get(id).map(|d| &d.kind) {
                            Some(AdtKind::Union { variants, .. }) => variants[*v as usize].name.to_string(),
                            _ => "?".to_string(),
                        },
                        _ => "?".to_string(),
                    };
                    match subs.first() {
                        None => format!(".{}", name),
                        Some(s) => format!(".{}({})", name, self.show(s)),
                    }
                }
                Ctor::Single => {
                    let inner: Vec<String> = subs.iter().map(|s| self.show(s)).collect();
                    match ty.kind() {
                        TyKind::Adt(id, _) => {
                            let def = self.adts.get(id);
                            let names: Vec<String> = match def.map(|d| &d.kind) {
                                Some(AdtKind::Struct { fields, .. }) => {
                                    fields.iter().map(|f| f.name.to_string()).collect()
                                }
                                _ => vec![],
                            };
                            let parts: Vec<String> = names
                                .iter()
                                .zip(&inner)
                                .filter(|(_, v)| *v != "_")
                                .map(|(n, v)| format!("{}: {}", n, v))
                                .collect();
                            format!("{}({})", def.map_or("?".to_string(), |d| d.name.to_string()), parts.join(", "))
                        }
                        _ => format!("({})", inner.join(", ")),
                    }
                }
            },
        }
    }
}

/// Puts the first `arity` patterns of a witness back under constructor `c`.
fn rebuild(ty: Ty, c: Ctor, arity: usize, mut w: Vec<DPat>) -> Vec<DPat> {
    let rest = w.split_off(arity.min(w.len()));
    let mut out = vec![DPat::Ctor(ty, c, w)];
    out.extend(rest);
    out
}
