//! Untyped constants (§6.2): exact arithmetic on integers and rationals.

use crate::syntax::ast::BinOp;
use num_bigint::BigInt;
use num_rational::BigRational;
use num_traits::{Signed, ToPrimitive, Zero};

#[derive(Clone, Debug, PartialEq)]
pub enum Untyped {
    Int(BigInt),
    Float(BigRational),
    Bool(bool),
}

impl Untyped {
    pub fn describe(&self) -> &'static str {
        match self {
            Untyped::Int(_) => "integer constant",
            Untyped::Float(_) => "float constant",
            Untyped::Bool(_) => "boolean constant",
        }
    }

    fn as_rational(&self) -> Option<BigRational> {
        match self {
            Untyped::Int(i) => Some(BigRational::from_integer(i.clone())),
            Untyped::Float(r) => Some(r.clone()),
            Untyped::Bool(_) => None,
        }
    }
}

/// Applies unary minus.
pub fn negate(v: &Untyped) -> Result<Untyped, String> {
    match v {
        Untyped::Int(i) => Ok(Untyped::Int(-i)),
        Untyped::Float(r) => Ok(Untyped::Float(-r)),
        Untyped::Bool(_) => Err("cannot negate a boolean constant".into()),
    }
}

/// Applies a binary operator to two untyped constants.
pub fn binary(op: BinOp, a: &Untyped, b: &Untyped) -> Result<Untyped, String> {
    use Untyped::*;
    match op {
        BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge | BinOp::Is | BinOp::IsNot => {
            if let (Bool(x), Bool(y)) = (a, b) {
                return match op {
                    BinOp::Is => Ok(Bool(x == y)),
                    BinOp::IsNot => Ok(Bool(x != y)),
                    _ => Err("booleans are not ordered".into()),
                };
            }
            let (x, y) = (num(a)?, num(b)?);
            Ok(Bool(match op {
                BinOp::Lt => x < y,
                BinOp::Le => x <= y,
                BinOp::Gt => x > y,
                BinOp::Ge => x >= y,
                BinOp::Is => x == y,
                _ => x != y,
            }))
        }
        BinOp::Add(_) | BinOp::Sub(_) | BinOp::Mul(_) | BinOp::Div => {
            if let (Int(x), Int(y)) = (a, b) {
                return Ok(Int(match op {
                    BinOp::Add(_) => x + y,
                    BinOp::Sub(_) => x - y,
                    BinOp::Mul(_) => x * y,
                    _ => {
                        if y.is_zero() {
                            return Err("division by zero in constant expression".into());
                        }
                        x / y
                    }
                }));
            }
            let (x, y) = (num(a)?, num(b)?);
            Ok(Float(match op {
                BinOp::Add(_) => x + y,
                BinOp::Sub(_) => x - y,
                BinOp::Mul(_) => x * y,
                _ => {
                    if y.is_zero() {
                        return Err("division by zero in constant expression".into());
                    }
                    x / y
                }
            }))
        }
        BinOp::Rem | BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor | BinOp::Shl(_) | BinOp::Shr => {
            let (Int(x), Int(y)) = (a, b) else {
                return Err(format!("`{}` requires integer constants", op.as_str()));
            };
            Ok(Int(match op {
                BinOp::Rem => {
                    if y.is_zero() {
                        return Err("division by zero in constant expression".into());
                    }
                    x % y
                }
                BinOp::BitAnd => x & y,
                BinOp::BitOr => x | y,
                BinOp::BitXor => x ^ y,
                _ => {
                    if y.is_negative() {
                        return Err("negative shift amount in constant expression".into());
                    }
                    let n = y.to_u32().filter(|n| *n <= 1 << 20).ok_or("shift amount too large")?;
                    if matches!(op, BinOp::Shr) { x >> n } else { x << n }
                }
            }))
        }
        _ => Err(format!("`{}` is not allowed in an untyped constant expression", op.as_str())),
    }
}

fn num(v: &Untyped) -> Result<BigRational, String> {
    v.as_rational().ok_or_else(|| "expected a numeric constant".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::syntax::ast::Flavor;

    fn int(i: i64) -> Untyped {
        Untyped::Int(BigInt::from(i))
    }

    #[test]
    fn arithmetic() {
        assert_eq!(binary(BinOp::Sub(Flavor::Checked), &int(4096), &int(1)), Ok(int(4095)));
        assert_eq!(binary(BinOp::Div, &int(7), &int(2)), Ok(int(3)));
        assert_eq!(binary(BinOp::Div, &int(-7), &int(2)), Ok(int(-3)));
        let half = Untyped::Float(BigRational::new(BigInt::from(1), BigInt::from(2)));
        assert_eq!(
            binary(BinOp::Mul(Flavor::Checked), &int(3), &half),
            Ok(Untyped::Float(BigRational::new(BigInt::from(3), BigInt::from(2))))
        );
        assert!(binary(BinOp::Div, &int(1), &int(0)).is_err());
        assert_eq!(binary(BinOp::Lt, &int(1), &half), Ok(Untyped::Bool(false)));
        assert_eq!(
            binary(BinOp::Shl(Flavor::Checked), &int(1), &int(100)).unwrap(),
            Untyped::Int(BigInt::from(1) << 100)
        );
    }
}
