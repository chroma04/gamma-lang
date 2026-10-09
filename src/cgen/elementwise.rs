//! Elementwise array arithmetic (§7.4.6): `a + b` on `[N]T` applies the scalar
//! operation, with its checks, to each pair of elements in a loop.

use super::CResult;
use super::func::FnGen;
use super::types::Repr;
use crate::tir::{BinOp, Expr, ExprKind, Flavor, UnOp};
use crate::types::{Ty, TyKind};

impl<'g, 'a> FnGen<'g, 'a> {
    /// `l op r` (or `op r` when `l` is `None`) on arrays.
    pub fn elementwise(
        &mut self,
        e: &'a Expr,
        op: Result<BinOp, UnOp>,
        l: Option<&'a Expr>,
        r: &'a Expr,
    ) -> CResult<String> {
        let a = match l {
            Some(l) => Some(self.expr(l)?),
            None => None,
        };
        let b = self.expr(r)?;
        self.elementwise_values(e, op, e.ty, a.as_deref(), &b)
    }

    fn elementwise_values(
        &mut self,
        e: &Expr,
        op: Result<BinOp, UnOp>,
        t: Ty,
        a: Option<&str>,
        b: &str,
    ) -> CResult<String> {
        let TyKind::Array { len, elem } = t.kind() else {
            return self.scalar(e, op, t, a, b);
        };
        let (len, elem) = (*len, *elem);
        let size = self.g.ty.size(elem);
        let ce = self.g.ty.cty(elem);
        let res = self.tmp_uninit(t);
        let pa = a.map(|a| self.addr_of(t, a));
        let pb = self.addr_of(t, b);
        let i = format!("i{}", self.fresh());
        self.line(&format!("for (size_t {i} = 0; {i} < {len}; {i}++) {{"));
        self.indent += 1;
        let x = pa.map(|p| self.tmp(elem, &format!("GM_LD({ce}, (const uint8_t *){p} + {i} * {size})")));
        let y = self.tmp(elem, &format!("GM_LD({ce}, (const uint8_t *){pb} + {i} * {size})"));
        let v = self.elementwise_values(e, op, elem, x.as_deref(), &y)?;
        self.line(&format!("GM_ST({ce}, (uint8_t *)&{res} + {i} * {size}, {v});"));
        self.indent -= 1;
        self.line("}");
        Ok(res)
    }

    /// One element's operation, with the checks of the scalar operator.
    fn scalar(&mut self, e: &Expr, op: Result<BinOp, UnOp>, t: Ty, a: Option<&str>, b: &str) -> CResult<String> {
        // The scalar helpers take the result type and location from an expression.
        let at = Expr { ty: t, kind: ExprKind::Uninit, span: e.span };
        let (op, a) = match op {
            Ok(op) => (op, a.expect("a binary operation has two operands").to_string()),
            // `-x` is `0 - x`, which overflows exactly when the negation does.
            Err(_) => (
                BinOp::Sub(Flavor::Checked),
                if matches!(self.g.ty.repr(t), Repr::Float(_)) { "-0.0".into() } else { "0".into() },
            ),
        };
        match self.g.ty.repr(t) {
            Repr::Float(_) if a == "-0.0" => Ok(self.tmp(t, &format!("(-{})", b))),
            Repr::Float(_) => {
                let sym = match op {
                    BinOp::Add(_) => "+",
                    BinOp::Sub(_) => "-",
                    BinOp::Mul(_) => "*",
                    _ => "/",
                };
                Ok(self.tmp(t, &format!("({} {} {})", a, sym, b)))
            }
            _ => self.int_binary(&at, op, t, t, &a, b),
        }
    }
}
