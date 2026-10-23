//! Lexing of numeric, character and string literals (spec §2.5).

use super::Lexer;
use crate::diag::Diagnostic;
use crate::syntax::token::{FloatLit, FloatSuffix, IntLit, IntSuffix, Punct, StrKind, StrLit, Token, TokenKind};
use num_bigint::{BigInt, BigUint};
use num_rational::BigRational;

/// Decimal and binary exponents beyond this magnitude are rejected so that a
/// literal such as `1e999999999999` cannot exhaust memory.
const MAX_EXPONENT: i64 = 100_000;

/// A processed escape sequence.
enum Escape {
    /// A Unicode scalar value.
    Char(char),
    /// A raw byte written as `\xHH`.
    Byte(u8),
}

impl<'a> Lexer<'a> {
    // ----- numbers -----

    /// Lexes an integer or float literal starting at the digit at `self.pos`.
    pub(super) fn lex_number(&mut self) {
        let lo = self.pos;
        if matches!(self.tokens.last(), Some(Token { kind: TokenKind::Punct(Punct::Dot), .. })) {
            self.lex_field_index(lo);
            return;
        }
        // The first problem found is reported at the end, against the whole literal.
        let mut err: Option<String> = None;
        // The lowercase prefix, when an uppercase one is the problem reported.
        let mut prefix_fix = None;
        let radix = match (self.bytes[lo], self.peek_at(1)) {
            (b'0', Some(c @ (b'x' | b'o' | b'b' | b'X' | b'O' | b'B'))) => {
                if c.is_ascii_uppercase() {
                    let lower = c.to_ascii_lowercase() as char;
                    err = Some(format!("radix prefixes must be lowercase; write `0{}`", lower));
                    prefix_fix = Some(format!("0{}", lower));
                }
                self.pos += 2;
                match c.to_ascii_lowercase() {
                    b'x' => 16,
                    b'o' => 8,
                    _ => 2,
                }
            }
            _ => 10,
        };

        let int_part = self.scan_digits(radix, &mut err);
        if int_part.is_empty() {
            err.get_or_insert_with(|| {
                format!("expected {} digits after `{}`", radix_name(radix), &self.src[lo..self.pos])
            });
        }
        if radix == 10 && int_part.len() > 1 && self.bytes[int_part.start] == b'0' {
            err.get_or_insert_with(|| "decimal literals may not have a leading zero; use 0o for octal".to_string());
        }

        // Fraction and exponent.
        let mut frac_part = None;
        let mut exp: Option<i64> = None;
        // In a hexadecimal literal, `.` starts a fraction only before a decimal digit or
        // before hex digits and a `p` exponent; otherwise it is member access (`0xFF.abs()`).
        let fraction_follows = |lx: &Self, i: usize| match radix {
            10 => lx.bytes.get(i).is_some_and(|b| b.is_ascii_digit()),
            _ => {
                let mut j = i;
                while lx.bytes.get(j).is_some_and(|&b| b.is_ascii_hexdigit() || b == b'_') {
                    j += 1;
                }
                lx.bytes.get(i).is_some_and(|b| b.is_ascii_digit()) || lx.bytes.get(j) == Some(&b'p')
            }
        };
        if (radix == 10 || radix == 16) && self.peek() == Some(b'.') && fraction_follows(self, self.pos + 1) {
            self.pos += 1;
            frac_part = Some(self.scan_digits(radix, &mut err));
        }
        if radix == 10 {
            if self.at_exponent(b"eE") {
                exp = Some(self.scan_exponent(&mut err));
            }
        } else if radix == 16 {
            if self.at_exponent(b"p") {
                exp = Some(self.scan_exponent(&mut err));
            } else if frac_part.is_some() {
                err.get_or_insert_with(|| "hexadecimal float literals require a `p` exponent".to_string());
            }
        }
        // A decimal integer with a float suffix (`1f32`) is a float literal.
        let float_by_suffix = radix == 10 && frac_part.is_none() && exp.is_none() && {
            let at = self.pos;
            let is_float_suffix = float_suffix(self.scan_ident()).is_some();
            self.pos = at;
            is_float_suffix
        };
        let is_float = frac_part.is_some() || exp.is_some() || float_by_suffix;

        // Suffix: any identifier characters directly after the literal.
        let suffix = self.scan_ident();
        let (int_suffix, float_suffix) = if suffix.is_empty() {
            (None, None)
        } else if is_float {
            match float_suffix(suffix) {
                Some(s) => (None, Some(s)),
                None => {
                    err.get_or_insert_with(|| {
                        format!("invalid suffix `{}` for float literal; expected f16, f32 or f64", suffix)
                    });
                    (None, None)
                }
            }
        } else {
            match int_suffix(suffix) {
                Ok(s) => (Some(s), None),
                Err(msg) => {
                    err.get_or_insert(msg);
                    (None, None)
                }
            }
        };

        if let Some(msg) = err {
            let mut d = Diagnostic::error(self.span(lo, self.pos), msg);
            if let Some(p) = prefix_fix {
                d = d.with_fix(format!("write `{p}`"), self.span(lo, lo + 2), p);
            }
            self.diags.push(d);
        }
        let kind = if is_float {
            let value = self.float_value(radix, &int_part, frac_part.as_ref(), exp.unwrap_or(0));
            TokenKind::Float(Box::new(FloatLit { value, suffix: float_suffix }))
        } else {
            let value = self.int_value(&int_part, radix);
            TokenKind::Int(Box::new(IntLit { value, suffix: int_suffix, is_char: false }))
        };
        self.emit(kind, lo, self.pos);
    }

    /// Lexes the digits of a numeric field name such as the `0` in `t.0` (§2.6).
    fn lex_field_index(&mut self, lo: usize) {
        while self.peek().is_some_and(|b| b.is_ascii_digit()) {
            self.pos += 1;
        }
        if self.pos - lo > 1 && self.bytes[lo] == b'0' {
            self.error(lo, self.pos, "numeric field names may not have a leading zero");
        }
        let value = self.int_value(&(lo..self.pos), 10);
        self.emit(TokenKind::Int(Box::new(IntLit { value, suffix: None, is_char: false })), lo, self.pos);
    }

    /// Consumes digits of `radix` and `_` separators and returns their range.
    /// Binary and octal literals accept all decimal digits here so that an
    /// invalid digit is reported as such rather than as a bad suffix.
    fn scan_digits(&mut self, radix: u32, err: &mut Option<String>) -> std::ops::Range<usize> {
        let start = self.pos;
        while let Some(b) = self.peek() {
            let ok = match radix {
                16 => b.is_ascii_hexdigit(),
                _ => b.is_ascii_digit(),
            };
            if !(ok || b == b'_') {
                break;
            }
            self.pos += 1;
        }
        let range = start..self.pos;
        let text = &self.bytes[range.clone()];
        if text.first() == Some(&b'_') || text.last() == Some(&b'_') || text.windows(2).any(|w| w == b"__") {
            err.get_or_insert_with(|| "digit separators `_` must be single and between digits".to_string());
        }
        if let Some(&bad) = text.iter().find(|&&b| b != b'_' && !(b as char).is_digit(radix)) {
            err.get_or_insert_with(|| format!("invalid digit `{}` in {} literal", bad as char, radix_name(radix)));
        }
        range
    }

    /// True if an exponent marker from `markers`, an optional sign and a digit follow.
    fn at_exponent(&self, markers: &[u8]) -> bool {
        if !self.peek().is_some_and(|b| markers.contains(&b)) {
            return false;
        }
        let mut i = 1;
        if matches!(self.peek_at(i), Some(b'+' | b'-')) {
            i += 1;
        }
        self.peek_at(i).is_some_and(|b| b.is_ascii_digit())
    }

    /// Consumes an exponent (the marker is already known to be present) and
    /// returns its value.
    fn scan_exponent(&mut self, err: &mut Option<String>) -> i64 {
        self.pos += 1;
        let negative = match self.peek() {
            Some(b'-') => {
                self.pos += 1;
                true
            }
            Some(b'+') => {
                self.pos += 1;
                false
            }
            _ => false,
        };
        let digits = self.scan_digits(10, err);
        let mut value: i64 = 0;
        for &b in &self.bytes[digits] {
            if b != b'_' {
                value = value.saturating_mul(10).saturating_add((b - b'0') as i64).min(MAX_EXPONENT + 1);
            }
        }
        if value > MAX_EXPONENT {
            err.get_or_insert_with(|| {
                format!("the exponent is larger than {}, the limit of this implementation (§23.5)", MAX_EXPONENT)
            });
            value = 0;
        }
        if negative { -value } else { value }
    }

    fn int_value(&self, digits: &std::ops::Range<usize>, radix: u32) -> BigUint {
        let cleaned: Vec<u8> = self.bytes[digits.clone()].iter().copied().filter(|&b| b != b'_').collect();
        BigUint::parse_bytes(&cleaned, radix).unwrap_or_default()
    }

    fn float_value(
        &self,
        radix: u32,
        int_part: &std::ops::Range<usize>,
        frac_part: Option<&std::ops::Range<usize>>,
        exp: i64,
    ) -> BigRational {
        let empty = 0..0;
        let frac = frac_part.unwrap_or(&empty);
        let frac_digits = self.bytes[frac.clone()].iter().filter(|&&b| b != b'_').count() as i64;
        let int_digits: Vec<u8> = self.bytes[int_part.clone()].iter().copied().filter(|&b| b != b'_').collect();
        let mut digits = int_digits;
        digits.extend(self.bytes[frac.clone()].iter().copied().filter(|&b| b != b'_'));
        let mantissa = BigUint::parse_bytes(&digits, radix).unwrap_or_default();
        // value = mantissa * base^scale where base is 10 (decimal) or 2 (hex float).
        let scale = if radix == 16 { exp - 4 * frac_digits } else { exp - frac_digits };
        let base = BigUint::from(if radix == 16 { 2u32 } else { 10 });
        let factor = base.pow(scale.unsigned_abs() as u32);
        if scale >= 0 {
            BigRational::from_integer(BigInt::from(mantissa * factor))
        } else {
            BigRational::new(BigInt::from(mantissa), BigInt::from(factor))
        }
    }

    // ----- characters -----

    /// Lexes a character literal starting at the `'` at `self.pos` (§2.5.3).
    pub(super) fn lex_char(&mut self) {
        let lo = self.pos;
        self.pos += 1;
        let value = match self.peek() {
            None | Some(b'\n' | b'\r') => {
                self.error(lo, self.pos, "unterminated character literal");
                return self.emit_char(0, lo);
            }
            Some(b'\'') => {
                self.pos += 1;
                self.error(lo, self.pos, "empty character literal");
                return self.emit_char(0, lo);
            }
            Some(b'\\') => match self.lex_escape() {
                Some(Escape::Char(c)) => c as u32,
                Some(Escape::Byte(b)) => b as u32,
                None => {
                    self.skip_to_closing_quote();
                    return self.emit_char(0, lo);
                }
            },
            Some(_) => {
                let c = self.current_char();
                self.pos += c.len_utf8();
                c as u32
            }
        };
        if self.peek() == Some(b'\'') {
            self.pos += 1;
        } else if self.skip_to_closing_quote() {
            self.error(lo, self.pos, "a character literal must contain exactly one character");
        } else {
            self.error(lo, self.pos, "unterminated character literal");
        }
        self.emit_char(value, lo);
    }

    /// Advances past the next `'` on the current line, if there is one. Returns
    /// whether it found one.
    fn skip_to_closing_quote(&mut self) -> bool {
        let mut i = self.pos;
        while let Some(&b) = self.bytes.get(i) {
            match b {
                b'\'' => {
                    self.pos = i + 1;
                    return true;
                }
                b'\n' | b'\r' => break,
                _ => i += 1,
            }
        }
        false
    }

    fn emit_char(&mut self, value: u32, lo: usize) {
        let lit = IntLit { value: BigUint::from(value), suffix: None, is_char: true };
        self.emit(TokenKind::Int(Box::new(lit)), lo, self.pos);
    }

    // ----- escapes -----

    /// Processes the escape sequence starting at the backslash at `self.pos`.
    /// Errors are reported here; `None` means the escape was invalid or cut off
    /// by the end of the line or file (which the caller reports).
    fn lex_escape(&mut self) -> Option<Escape> {
        let lo = self.pos;
        self.pos += 1;
        let b = self.peek()?;
        let simple = match b {
            b'n' => Some('\n'),
            b'r' => Some('\r'),
            b't' => Some('\t'),
            b'0' => Some('\0'),
            b'\\' => Some('\\'),
            b'"' => Some('"'),
            b'\'' => Some('\''),
            _ => None,
        };
        if let Some(c) = simple {
            self.pos += 1;
            return Some(Escape::Char(c));
        }
        match b {
            b'x' => {
                self.pos += 1;
                let hi = self.peek().and_then(|b| (b as char).to_digit(16));
                let lo_digit = self.peek_at(1).and_then(|b| (b as char).to_digit(16));
                if let (Some(h), Some(l)) = (hi, lo_digit) {
                    self.pos += 2;
                    Some(Escape::Byte((h * 16 + l) as u8))
                } else {
                    while self.peek().is_some_and(|b| b.is_ascii_hexdigit()) && self.pos < lo + 4 {
                        self.pos += 1;
                    }
                    self.error(lo, self.pos, "invalid `\\x` escape: expected exactly two hexadecimal digits");
                    None
                }
            }
            b'u' => {
                self.pos += 1;
                self.lex_unicode_escape(lo)
            }
            b'\n' | b'\r' => None,
            _ => {
                let c = self.current_char();
                self.pos += c.len_utf8();
                self.error(lo, self.pos, format!("unknown escape sequence `\\{}`", c));
                None
            }
        }
    }

    /// Processes the `{H…}` of a `\u{H…}` escape; `lo` is the backslash.
    fn lex_unicode_escape(&mut self, lo: usize) -> Option<Escape> {
        if self.peek() != Some(b'{') {
            self.error(lo, self.pos, "invalid unicode escape: expected `{` after `\\u`");
            return None;
        }
        self.pos += 1;
        let start = self.pos;
        while self.peek().is_some_and(|b| b.is_ascii_hexdigit()) {
            self.pos += 1;
        }
        let digits = &self.src[start..self.pos];
        let closed = self.peek() == Some(b'}');
        if closed {
            self.pos += 1;
        }
        if !closed {
            self.error(lo, self.pos, "invalid unicode escape: expected `}` after the hexadecimal digits");
            return None;
        }
        if digits.is_empty() || digits.len() > 6 {
            self.error(lo, self.pos, "invalid unicode escape: expected 1 to 6 hexadecimal digits");
            return None;
        }
        let value = u32::from_str_radix(digits, 16).unwrap_or(u32::MAX);
        match char::from_u32(value) {
            Some(c) => Some(Escape::Char(c)),
            None => {
                let why = if value > 0x10FFFF { "it is above U+10FFFF" } else { "it is a surrogate" };
                self.error(lo, self.pos, format!("invalid unicode escape `\\u{{{}}}`: {}", digits, why));
                None
            }
        }
    }

    // ----- strings -----

    /// Lexes a regular or C string. `lo` is the start of the token (the `c` of a
    /// C string) and `self.pos` is at the opening quote.
    pub(super) fn lex_string(&mut self, lo: usize, kind: StrKind) {
        self.pos += 1;
        let mut bytes = Vec::new();
        loop {
            match self.peek() {
                None => {
                    self.error(lo, self.pos, "unterminated string literal");
                    break;
                }
                Some(b'"') => {
                    self.pos += 1;
                    break;
                }
                Some(b'\n') => {
                    self.unterminated_at_line_end(lo);
                    break;
                }
                Some(b'\r') => {
                    if self.peek_at(1) == Some(b'\n') {
                        self.unterminated_at_line_end(lo);
                        break;
                    }
                    self.lone_cr();
                }
                Some(b'\\') => match self.lex_escape() {
                    Some(Escape::Char(c)) => bytes.extend_from_slice(c.encode_utf8(&mut [0; 4]).as_bytes()),
                    Some(Escape::Byte(b)) => bytes.push(b),
                    None => {}
                },
                Some(b) => {
                    bytes.push(b);
                    self.pos += 1;
                }
            }
        }
        if kind == StrKind::C && bytes.contains(&0) {
            self.error(lo, self.pos, "C string literals may not contain NUL bytes");
        }
        self.emit(TokenKind::Str(Box::new(StrLit { bytes, kind })), lo, self.pos);
    }

    fn unterminated_at_line_end(&mut self, lo: usize) {
        let span = self.span(lo, self.pos);
        self.diags.push(
            crate::diag::Diagnostic::error(span, "unterminated string literal")
                .with_note("regular string literals may not contain line terminators; use a raw string (backticks) for multi-line text"),
        );
    }

    /// Lexes a raw string literal opened by the backticks at `self.pos`.
    pub(super) fn lex_raw_string(&mut self) {
        let lo = self.pos;
        let delim = self.backtick_run();
        self.pos += delim;
        if self.peek() == Some(b'\n') {
            self.pos += 1;
        } else if self.peek() == Some(b'\r') && self.peek_at(1) == Some(b'\n') {
            self.pos += 2;
        }
        let mut bytes = Vec::new();
        loop {
            match self.peek() {
                None => {
                    let span = self.span(lo, lo + delim);
                    self.diags.push(
                        crate::diag::Diagnostic::error(span, "unterminated raw string literal")
                            .with_note(format!("expected a closing run of exactly {} backtick(s)", delim)),
                    );
                    break;
                }
                Some(b'`') => {
                    let run = self.backtick_run();
                    self.pos += run;
                    if run == delim {
                        break;
                    }
                    bytes.resize(bytes.len() + run, b'`');
                }
                Some(b'\r') => {
                    if self.peek_at(1) == Some(b'\n') {
                        bytes.push(b'\n');
                        self.pos += 2;
                    } else {
                        self.lone_cr();
                    }
                }
                Some(b) => {
                    bytes.push(b);
                    self.pos += 1;
                }
            }
        }
        self.emit(TokenKind::Str(Box::new(StrLit { bytes, kind: StrKind::Raw })), lo, self.pos);
    }

    /// The number of consecutive backticks at `self.pos`.
    fn backtick_run(&self) -> usize {
        self.bytes[self.pos..].iter().take_while(|&&b| b == b'`').count()
    }
}

fn radix_name(radix: u32) -> &'static str {
    match radix {
        2 => "binary",
        8 => "octal",
        10 => "decimal",
        _ => "hexadecimal",
    }
}

/// Parses an integer literal suffix (`u8`, `i64`, `usize`, …).
fn int_suffix(s: &str) -> Result<IntSuffix, String> {
    match s {
        "usize" => return Ok(IntSuffix::Usize),
        "isize" => return Ok(IntSuffix::Isize),
        _ => {}
    }
    let digits = s.get(1..).unwrap_or("");
    if matches!(s.as_bytes().first(), Some(b'u' | b'i'))
        && !digits.is_empty()
        && digits.bytes().all(|b| b.is_ascii_digit())
    {
        let width = match digits.parse::<u32>() {
            Ok(w) if (1..=128).contains(&w) && !digits.starts_with('0') => w,
            _ => {
                return Err(format!(
                    "invalid integer width in suffix `{}`: widths are 1 to 128 with no leading zero",
                    s
                ));
            }
        };
        return Ok(if s.starts_with('u') { IntSuffix::U(width) } else { IntSuffix::I(width) });
    }
    if float_suffix(s).is_some() {
        return Err(format!(
            "invalid suffix `{}` for a binary or octal literal; only decimal and hexadecimal floats exist",
            s
        ));
    }
    Err(format!("invalid suffix `{}` for integer literal", s))
}

fn float_suffix(s: &str) -> Option<FloatSuffix> {
    match s {
        "f16" => Some(FloatSuffix::F16),
        "f32" => Some(FloatSuffix::F32),
        "f64" => Some(FloatSuffix::F64),
        _ => None,
    }
}
