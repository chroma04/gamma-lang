//! Untyped constants (§6.2): exact arithmetic on integers and rationals.

use crate::syntax::ast::BinOp;
use num_bigint::BigInt;
use num_rational::BigRational;
use num_traits::{Signed, ToPrimitive, Zero};

#[derive(Clone, Debug, PartialEq)]
pub enum Untyped {
    Int(BigInt),
    Float(BigRational),
    /// The float constant negative zero, which a rational cannot represent.
    NegZero,
    Bool(bool),
    /// A complex constant: its real and imaginary parts (§2.5.2). Zero parts are `+0`.
    Complex(BigRational, BigRational),
}

impl Untyped {
    pub fn describe(&self) -> &'static str {
        match self {
            Untyped::Int(_) => "integer constant",
            Untyped::Float(_) | Untyped::NegZero => "float constant",
            Untyped::Bool(_) => "boolean constant",
            Untyped::Complex(..) => "complex constant",
        }
    }

    fn as_rational(&self) -> Option<BigRational> {
        match self {
            Untyped::Int(i) => Some(BigRational::from_integer(i.clone())),
            Untyped::Float(r) => Some(r.clone()),
            Untyped::NegZero => Some(BigRational::zero()),
            Untyped::Bool(_) | Untyped::Complex(..) => None,
        }
    }

    /// The constant as the parts of a complex number, if it is numeric.
    fn as_complex(&self) -> Option<(BigRational, BigRational)> {
        match self {
            Untyped::Complex(re, im) => Some((re.clone(), im.clone())),
            _ => self.as_rational().map(|r| (r, BigRational::zero())),
        }
    }
}

/// Applies unary minus.
pub fn negate(v: &Untyped) -> Result<Untyped, String> {
    match v {
        Untyped::Int(i) => Ok(Untyped::Int(-i)),
        Untyped::Float(r) if r.is_zero() => Ok(Untyped::NegZero),
        Untyped::Float(r) => Ok(Untyped::Float(-r)),
        Untyped::NegZero => Ok(Untyped::Float(BigRational::zero())),
        Untyped::Bool(_) => Err("cannot negate a boolean constant".into()),
        Untyped::Complex(re, im) => Ok(Untyped::Complex(-re, -im)),
    }
}

/// The largest size, in bits, of an integer (or of a rational's numerator or denominator)
/// that untyped constant arithmetic produces (§6.2).
pub const MAX_CONST_BITS: u64 = 1 << 22;

/// An integer for a diagnostic: in full unless it is very large, then only its size.
pub fn show_int(v: &BigInt) -> String {
    const SHOWN_BITS: u64 = 512;
    if v.bits() <= SHOWN_BITS { v.to_string() } else { format!("<{}-bit integer>", v.bits()) }
}

/// Applies a binary operator to two untyped constants.
pub fn binary(op: BinOp, a: &Untyped, b: &Untyped) -> Result<Untyped, String> {
    let r = binary_exact(op, a, b)?;
    let rational_bits = |q: &BigRational| q.numer().bits().max(q.denom().bits());
    let bits = match &r {
        Untyped::Int(i) => i.bits(),
        Untyped::Float(q) => rational_bits(q),
        Untyped::Complex(re, im) => rational_bits(re).max(rational_bits(im)),
        _ => 0,
    };
    if bits > MAX_CONST_BITS {
        return Err(format!("the result of this constant expression is too large (more than {} bits)", MAX_CONST_BITS));
    }
    Ok(r)
}

fn binary_exact(op: BinOp, a: &Untyped, b: &Untyped) -> Result<Untyped, String> {
    use Untyped::*;
    if matches!(a, Complex(..)) || matches!(b, Complex(..)) {
        return complex_binary(op, a, b);
    }
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
            let r = match op {
                BinOp::Add(_) => x + y,
                BinOp::Sub(_) => x - y,
                BinOp::Mul(_) => x * y,
                _ => {
                    if y.is_zero() {
                        return Err("division by zero in constant expression".into());
                    }
                    x / y
                }
            };
            Ok(float_result(op, a, b, r))
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

/// `a op b` where one operand is a complex constant: exact arithmetic on the parts, with a
/// real operand taken as a complex number whose imaginary part is zero (§7.4.7).
fn complex_binary(op: BinOp, a: &Untyped, b: &Untyped) -> Result<Untyped, String> {
    let ((p, q), (r, s)) = match (a.as_complex(), b.as_complex()) {
        (Some(x), Some(y)) => (x, y),
        _ => return Err("expected a numeric constant".into()),
    };
    Ok(match op {
        BinOp::Add(_) => Untyped::Complex(p + r, q + s),
        BinOp::Sub(_) => Untyped::Complex(p - r, q - s),
        BinOp::Mul(_) => Untyped::Complex(&p * &r - &q * &s, &p * &s + &q * &r),
        BinOp::Div => {
            let d = &r * &r + &s * &s;
            if d.is_zero() {
                return Err("division by zero in constant expression".into());
            }
            Untyped::Complex((&p * &r + &q * &s) / &d, (&q * &r - &p * &s) / &d)
        }
        BinOp::Is => Untyped::Bool(p == r && q == s),
        BinOp::IsNot => Untyped::Bool(p != r || q != s),
        BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => return Err("complex constants are not ordered".into()),
        _ => return Err(format!("`{}` does not apply to complex constants", op.as_str())),
    })
}

/// A float-valued arithmetic result, with the sign IEEE arithmetic gives a zero result.
fn float_result(op: BinOp, a: &Untyped, b: &Untyped, r: BigRational) -> Untyped {
    if !r.is_zero() {
        return Untyped::Float(r);
    }
    let neg = |v: &Untyped| matches!(v, Untyped::NegZero) || is_negative(v);
    let negative = match op {
        // An exact zero sum is negative only when both addends are negative zeros.
        BinOp::Add(_) => matches!((a, b), (Untyped::NegZero, Untyped::NegZero)),
        BinOp::Sub(_) => matches!(a, Untyped::NegZero) && !matches!(b, Untyped::NegZero) && is_zero(b),
        _ => neg(a) != neg(b),
    };
    if negative { Untyped::NegZero } else { Untyped::Float(r) }
}

fn is_negative(v: &Untyped) -> bool {
    match v {
        Untyped::Int(i) => i.is_negative(),
        Untyped::Float(r) => r.is_negative(),
        _ => false,
    }
}

fn is_zero(v: &Untyped) -> bool {
    v.as_rational().is_some_and(|r| r.is_zero())
}

fn num(v: &Untyped) -> Result<BigRational, String> {
    v.as_rational().ok_or_else(|| "expected a numeric constant".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::syntax::ast::Flavor;
    use num_traits::One;

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
        let big = binary(BinOp::Shl(Flavor::Checked), &int(1), &int(1 << 20)).unwrap();
        let sq = binary(BinOp::Mul(Flavor::Checked), &big, &big).unwrap();
        assert!(binary(BinOp::Mul(Flavor::Checked), &sq, &sq).is_err());
        let Untyped::Int(i) = &sq else { panic!() };
        assert_eq!(show_int(i), "<2097153-bit integer>");
        assert_eq!(show_int(&BigInt::from(-12)), "-12");
        assert_eq!(binary(BinOp::Lt, &int(1), &half), Ok(Untyped::Bool(false)));
        assert_eq!(
            binary(BinOp::Shl(Flavor::Checked), &int(1), &int(100)).unwrap(),
            Untyped::Int(BigInt::from(1) << 100)
        );
    }

    #[test]
    fn signed_zero() {
        let zero = Untyped::Float(BigRational::zero());
        let one = Untyped::Float(BigRational::one());
        let nz = negate(&zero).unwrap();
        assert_eq!(nz, Untyped::NegZero);
        assert_eq!(negate(&nz), Ok(zero.clone()));
        assert_eq!(binary(BinOp::Mul(Flavor::Checked), &nz, &one), Ok(Untyped::NegZero));
        assert_eq!(binary(BinOp::Mul(Flavor::Checked), &nz, &int(-2)), Ok(zero.clone()));
        assert_eq!(binary(BinOp::Div, &zero, &int(-2)), Ok(Untyped::NegZero));
        assert_eq!(binary(BinOp::Add(Flavor::Checked), &nz, &nz), Ok(Untyped::NegZero));
        assert_eq!(binary(BinOp::Add(Flavor::Checked), &nz, &zero), Ok(zero.clone()));
        assert_eq!(binary(BinOp::Sub(Flavor::Checked), &nz, &zero), Ok(Untyped::NegZero));
        assert_eq!(binary(BinOp::Sub(Flavor::Checked), &one, &one), Ok(zero.clone()));
        assert_eq!(binary(BinOp::Is, &nz, &zero), Ok(Untyped::Bool(true)));
    }
}
