//! Predeclared names of the universe scope that are built into the compiler
//! rather than declared in the core prelude (§21.1).

use crate::types::{FloatTy, Ty, TyKind};

/// Parses `u1`…`u128` / `i1`…`i128`.
fn int_type_name(name: &str) -> Option<(bool, u16)> {
    let (signed, digits) = match name.as_bytes().first()? {
        b'u' => (false, &name[1..]),
        b'i' => (true, &name[1..]),
        _ => return None,
    };
    if digits.is_empty() || digits.starts_with('0') || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let bits: u32 = digits.parse().ok()?;
    if (1..=128).contains(&bits) { Some((signed, bits as u16)) } else { None }
}

/// Whether `name` looks like an integer type but is wider than 128 bits (`u256`).
pub fn is_oversized_int_name(name: &str) -> bool {
    let digits = match name.as_bytes().first() {
        Some(b'u' | b'i') => &name[1..],
        _ => return false,
    };
    !digits.is_empty()
        && !digits.starts_with('0')
        && digits.bytes().all(|b| b.is_ascii_digit())
        && (digits.len() > 3 || digits.parse::<u32>().is_ok_and(|n| n > 128))
}

/// The reserved type names, which may not be redeclared anywhere.
pub fn is_reserved_type_name(name: &str) -> bool {
    int_type_name(name).is_some()
        || matches!(name, "usize" | "isize" | "f16" | "f32" | "f64" | "bool" | "never" | "type" | "rawptr" | "cont")
}

/// A predeclared type name (reserved names and the aliases `string`, `cstring`, `rune`).
pub fn builtin_type(name: &str) -> Option<Ty> {
    if let Some((signed, bits)) = int_type_name(name) {
        return Some(Ty::int(signed, bits));
    }
    Some(match name {
        "usize" => Ty::usize(),
        "isize" => Ty::isize(),
        "f16" => Ty::intern(TyKind::Float(FloatTy::F16)),
        "f32" => Ty::intern(TyKind::Float(FloatTy::F32)),
        "f64" => Ty::f64(),
        "bool" => Ty::bool(),
        "never" => Ty::never(),
        "type" => Ty::type_(),
        "rawptr" => Ty::intern(TyKind::Rawptr),
        "string" => Ty::string(),
        "cstring" => Ty::many_ptr(true, Ty::u8()),
        "rune" => Ty::u32(),
        _ => return None,
    })
}

/// Functions of the universe scope implemented directly by the compiler.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BuiltinFn {
    Print,
    Eprint,
    Trap,
    Assert,
    ExpectEq,
}

pub fn builtin_fn(name: &str) -> Option<BuiltinFn> {
    Some(match name {
        "print" => BuiltinFn::Print,
        "eprint" => BuiltinFn::Eprint,
        "trap" => BuiltinFn::Trap,
        "assert" => BuiltinFn::Assert,
        "expect_eq" => BuiltinFn::ExpectEq,
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn int_names() {
        assert_eq!(int_type_name("u8"), Some((false, 8)));
        assert_eq!(int_type_name("i128"), Some((true, 128)));
        assert_eq!(int_type_name("u0"), None);
        assert_eq!(int_type_name("u129"), None);
        assert_eq!(int_type_name("u65535"), None);
        assert!(is_oversized_int_name("u129"));
        assert!(is_oversized_int_name("i65536"));
        assert!(!is_oversized_int_name("u128"));
        assert!(!is_oversized_int_name("u08"));
        assert!(!is_oversized_int_name("used"));
        assert_eq!(int_type_name("u08"), None);
        assert_eq!(int_type_name("used"), None);
        assert!(is_reserved_type_name("cont"));
        assert!(!is_reserved_type_name("string"));
    }
}
