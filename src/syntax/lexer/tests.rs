use super::lex;
use crate::diag::{Diagnostics, Severity};
use crate::source::{FileId, Span};
use crate::syntax::token::{Punct, StrKind, Token, TokenKind};
use num_bigint::BigInt;
use num_rational::BigRational;

/// Lexes `src`, returning the tokens and the error messages.
fn run(src: &str) -> (Vec<Token>, Vec<String>) {
    let mut diags = Diagnostics::new();
    let out = lex(FileId(0), src, &mut diags);
    assert_eq!(out.tokens.last().map(|t| &t.kind), Some(&TokenKind::Eof), "stream must end in Eof");
    assert_eq!(out.tokens.iter().filter(|t| t.kind == TokenKind::Eof).count(), 1, "exactly one Eof");
    let errors = diags.iter().filter(|d| d.severity == Severity::Error).map(|d| d.message.clone()).collect();
    (out.tokens, errors)
}

/// A compact rendering of one token for assertions.
fn show(kind: &TokenKind) -> String {
    match kind {
        TokenKind::Ident(s) => s.to_string(),
        TokenKind::Underscore => "_".into(),
        TokenKind::Keyword(k) => k.as_str().to_string(),
        TokenKind::Int(l) => {
            let c = if l.is_char { "'" } else { "" };
            match l.suffix {
                Some(s) => format!("{}{}{}", c, l.value, s),
                None => format!("{}{}", c, l.value),
            }
        }
        TokenKind::Float(l) => match l.suffix {
            Some(s) => format!("f{}{:?}", l.value, s),
            None => format!("f{}", l.value),
        },
        TokenKind::Str(s) => format!("{:?}{}", s.kind, crate::driver::escape_bytes(&s.bytes)),
        TokenKind::Intrinsic(s) => format!("@{}", s),
        TokenKind::Attribute(s) => format!("#{}", s),
        TokenKind::Punct(p) => p.as_str().to_string(),
        TokenKind::Newline => "NL".into(),
        TokenKind::Eof => "EOF".into(),
    }
}

/// Lexes `src`, asserts there are no errors, and returns the tokens as a space-separated string.
fn toks(src: &str) -> String {
    let (tokens, errors) = run(src);
    assert!(errors.is_empty(), "unexpected errors for {:?}: {:?}", src, errors);
    tokens.iter().map(|t| show(&t.kind)).collect::<Vec<_>>().join(" ")
}

/// Lexes `src` and returns the error messages (tokens are checked to end in Eof).
fn errs(src: &str) -> Vec<String> {
    run(src).1
}

/// Asserts that lexing `src` produces exactly one error containing `needle`.
fn one_err(src: &str, needle: &str) {
    let e = errs(src);
    assert_eq!(e.len(), 1, "expected one error for {:?}, got {:?}", src, e);
    assert!(e[0].contains(needle), "error {:?} for {:?} does not contain {:?}", e[0], src, needle);
}

fn first_kind(src: &str) -> TokenKind {
    run(src).0.remove(0).kind
}

fn rat(n: i64, d: i64) -> BigRational {
    BigRational::new(BigInt::from(n), BigInt::from(d))
}

fn float_value(src: &str) -> BigRational {
    match first_kind(src) {
        TokenKind::Float(l) => l.value,
        k => panic!("{:?} lexed as {:?}, not a float", src, k),
    }
}

fn int_value(src: &str) -> String {
    match first_kind(src) {
        TokenKind::Int(l) => l.value.to_string(),
        k => panic!("{:?} lexed as {:?}, not an int", src, k),
    }
}

// ----- basics -----

#[test]
fn empty_and_trivia_only() {
    assert_eq!(toks(""), "EOF");
    assert_eq!(toks("  \t \n\n  // c\n /* b */ \n"), "EOF");
    assert_eq!(toks("\u{feff}"), "EOF");
}

#[test]
fn bom_is_skipped_and_spans_are_offsets_into_source() {
    let (tokens, errors) = run("\u{feff}ab");
    assert!(errors.is_empty());
    assert_eq!(tokens[0].span, Span::new(FileId(0), 3, 5));
    assert_eq!(tokens[1].span, Span::new(FileId(0), 5, 5));
}

#[test]
fn identifiers_keywords_and_underscore() {
    assert_eq!(toks("foo _x x_1 _ __ A9"), "foo _x x_1 _ __ A9 EOF");
    assert_eq!(first_kind("_"), TokenKind::Underscore);
    assert!(matches!(first_kind("__"), TokenKind::Ident(_)));
    assert_eq!(toks("fn while match true null"), "fn while match true null EOF");
    // Contextual keywords and the predeclared type names are plain identifiers.
    for w in ["const", "on", "where", "test", "type", "cont"] {
        assert!(matches!(first_kind(w), TokenKind::Ident(_)), "{}", w);
    }
    // Only `c` directly followed by a quote starts a C string.
    assert_eq!(toks("c \"a\""), "c Regular\"a\" EOF");
    assert_eq!(toks("abc\"a\""), "abc Regular\"a\" EOF");
}

#[test]
fn intrinsics_and_attributes() {
    assert_eq!(toks("@size_of(T) #packed"), "@size_of ( T ) #packed EOF");
    assert_eq!(toks("@ x # y"), "@ x # y EOF");
    assert_eq!(toks("@1"), "@ 1 EOF");
    assert_eq!(toks("#[a]"), "# [ a ] EOF");
}

#[test]
fn every_punct_lexes_as_itself() {
    for &p in Punct::ALL {
        if matches!(p, Punct::IsNot | Punct::NotIn) {
            continue;
        }
        // A trailing operand keeps the operator from being last (no effect on the token itself).
        let (tokens, errors) = run(&format!("a {} b", p.as_str()));
        // Unbalanced brackets are errors in their own right.
        if matches!(
            p,
            Punct::RParen | Punct::RBracket | Punct::RBrace | Punct::LParen | Punct::LBracket | Punct::LBrace
        ) {
            assert_eq!(errors.len(), 1);
            continue;
        }
        assert!(errors.is_empty(), "{:?}: {:?}", p, errors);
        let kinds: Vec<_> = tokens.iter().map(|t| t.kind.clone()).collect();
        let found = kinds.contains(&TokenKind::Punct(p));
        assert!(found, "{:?} not produced: {:?}", p, kinds);
    }
}

#[test]
fn maximal_munch() {
    assert_eq!(toks("<<%= <<| <<? <<= << <= < >>= >> >= >"), "<<%= <<| <<? <<= << <= < >>= >> >= > EOF");
    assert_eq!(
        toks("+%= +| +? += -> -% -|= -? *% *|= *? /= %= &= |= ~="),
        "+%= +| +? += -> -% -|= -? *% *|= *? /= %= &= |= ~= EOF"
    );
    assert_eq!(toks(".. ..= ... . :: := : =>"), ".. ..= ... . :: := : => EOF");
    // Operators with no combined form split.
    assert_eq!(toks("= = ! = !x"), "= = ! = ! x EOF");
    assert_eq!(toks("a<<?=b"), "a <<? = b EOF");
    assert_eq!(toks("a+-b"), "a + - b EOF");
}

#[test]
fn uninit_is_always_one_token() {
    assert_eq!(toks("x: i32 = ---"), "x : i32 = --- EOF");
    assert_eq!(toks("----"), "--- - EOF");
    assert_eq!(toks("a---b"), "a --- b EOF");
    assert_eq!(toks("a--b"), "a - - b EOF");
    assert_eq!(toks("-->"), "- -> EOF");
}

#[test]
fn array_pointer_brackets_are_three_tokens() {
    assert_eq!(toks("[^]T [^]const T"), "[ ^ ] T [ ^ ] const T EOF");
}

// ----- comments -----

#[test]
fn line_and_block_comments() {
    assert_eq!(toks("a // b\nc"), "a NL c EOF");
    assert_eq!(toks("a /* b */ c"), "a c EOF");
    assert_eq!(toks("a /* b /* c */ d */ e"), "a e EOF");
    assert_eq!(toks("a /**/ b"), "a b EOF");
    assert_eq!(toks("a /* * / */ b"), "a b EOF");
    assert_eq!(toks("//"), "EOF");
}

#[test]
fn nested_block_comment_with_slashes() {
    // `/*/` does not close itself: the `*` belongs to the opener.
    assert_eq!(toks("a /*/ x */ b"), "a b EOF");
    assert_eq!(toks("a /* /* */ */ b"), "a b EOF");
}

#[test]
fn unterminated_block_comment_is_an_error() {
    one_err("a /* b", "unterminated block comment");
    one_err("/* /* */", "unterminated block comment");
    assert_eq!(toks_ignoring_errors("a /* b"), "a EOF");
}

fn toks_ignoring_errors(src: &str) -> String {
    run(src).0.iter().map(|t| show(&t.kind)).collect::<Vec<_>>().join(" ")
}

#[test]
fn doc_comments_are_recorded() {
    let mut diags = Diagnostics::new();
    let out = lex(FileId(0), "/// first\n///second\n///  third\n//// not doc\n////\n/// \nfn", &mut diags);
    let texts: Vec<_> = out.docs.iter().map(|d| d.text.as_str()).collect();
    assert_eq!(texts, ["first", "second", " third", ""]);
    assert_eq!(out.docs[0].span, Span::new(FileId(0), 0, 9));
    let kinds: Vec<_> = out.tokens.iter().map(|t| show(&t.kind)).collect();
    assert_eq!(kinds, ["fn", "EOF"]);
    let out = lex(FileId(0), "///x", &mut diags);
    assert_eq!(out.docs[0].text, "x");
    let out = lex(FileId(0), "/** not a doc */ /// doc /* not nested", &mut diags);
    assert_eq!(out.docs.len(), 1);
    assert_eq!(out.docs[0].text, "doc /* not nested");
    assert!(diags.is_empty());
}

// ----- integer literals -----

#[test]
fn integer_values() {
    assert_eq!(int_value("0"), "0");
    assert_eq!(int_value("42"), "42");
    assert_eq!(int_value("1_000_000"), "1000000");
    assert_eq!(int_value("0xFF"), "255");
    assert_eq!(int_value("0xdead_beef"), "3735928559");
    assert_eq!(int_value("0o17"), "15");
    assert_eq!(int_value("0b1010_1010"), "170");
    assert_eq!(int_value("0b0"), "0");
    assert_eq!(int_value("0x0"), "0");
    // Unbounded.
    assert_eq!(int_value("340282366920938463463374607431768211456"), "340282366920938463463374607431768211456");
    assert_eq!(int_value("0xffff_ffff_ffff_ffff_ffff"), "1208925819614629174706175");
}

#[test]
fn integer_suffixes() {
    assert_eq!(
        toks("1u8 2i32 3u1 4i65535 5usize 6isize 0xFFu16 0b1i2 0o7u64 1_0u8"),
        "1u8 2i32 3u1 4i65535 5usize 6isize 255u16 1i2 7u64 10u8 EOF"
    );
    for bad in ["1u0", "1u01", "1u65536", "1i99999999999", "1u"] {
        let e = errs(bad);
        assert_eq!(e.len(), 1, "{}: {:?}", bad, e);
        assert!(e[0].contains("suffix"), "{}: {:?}", bad, e);
    }
    one_err("12abc", "invalid suffix `abc`");
    one_err("0x1g", "invalid suffix `g`");
    one_err("1usize2", "invalid suffix");
    one_err("1f32", "invalid suffix `f32` for integer literal");
    one_err("1.5u8", "invalid suffix `u8` for float literal");
    one_err("1_a", "separators");
}

#[test]
fn integer_errors() {
    one_err("017", "leading zero");
    one_err("00", "leading zero");
    one_err("0_7", "leading zero");
    one_err("0x", "expected hexadecimal digits after `0x`");
    one_err("0b", "expected binary digits after `0b`");
    one_err("0o", "expected octal digits after `0o`");
    one_err("0b12", "invalid digit `2` in binary literal");
    one_err("0o8", "invalid digit `8` in octal literal");
    one_err("0XFF", "lowercase");
    one_err("0B1", "lowercase");
    one_err("0x_1", "separators");
    one_err("1_", "separators");
    one_err("1__0", "separators");
    one_err("0x1_", "separators");
    one_err("0b1__1", "separators");
}

#[test]
fn error_literals_still_produce_a_token() {
    assert_eq!(toks_ignoring_errors("12abc + 1"), "12 + 1 EOF");
    assert_eq!(toks_ignoring_errors("017"), "17 EOF");
    assert_eq!(toks_ignoring_errors("0x"), "0 EOF");
}

// ----- float literals -----

#[test]
fn float_values_are_exact() {
    assert_eq!(float_value("0.1"), rat(1, 10));
    assert_eq!(float_value("1.5"), rat(3, 2));
    assert_eq!(float_value("1.5e-3"), rat(3, 2000));
    assert_eq!(float_value("1.5E+3"), rat(1500, 1));
    assert_eq!(float_value("2e3"), rat(2000, 1));
    assert_eq!(float_value("5E-1"), rat(1, 2));
    assert_eq!(float_value("0.0"), rat(0, 1));
    assert_eq!(float_value("0.05"), rat(1, 20));
    assert_eq!(float_value("1_0.2_5e1_0"), rat(1025, 100) * BigRational::from_integer(BigInt::from(10u64).pow(10)));
    assert_eq!(float_value("3.14159"), rat(314159, 100000));
    assert_eq!(float_value("1e0"), rat(1, 1));
    assert_eq!(float_value("1e05"), rat(100000, 1));
    // Hexadecimal floats.
    assert_eq!(float_value("0x1.8p1"), rat(3, 1));
    assert_eq!(float_value("0x1.0p0"), rat(1, 1));
    assert_eq!(float_value("0x1.8p-1"), rat(3, 4));
    assert_eq!(float_value("0xA.8p+2"), rat(42, 1));
    assert_eq!(float_value("0x0.1p0"), rat(1, 16));
    assert_eq!(float_value("0xf.fp0"), rat(255, 16));
}

#[test]
fn float_suffixes() {
    assert_eq!(toks("1.5f32 2.0f64 3.5e2f16 0x1.8p1f32 1e3f64"), "f3/2F32 f2F64 f350F16 f3F32 f1000F64 EOF");
}

#[test]
fn float_boundaries() {
    // Both sides of the dot are required.
    assert_eq!(toks("1."), "1 . EOF");
    assert_eq!(toks(".5"), ". 5 EOF");
    assert_eq!(toks("1..10"), "1 .. 10 EOF");
    assert_eq!(toks("1..=10"), "1 ..= 10 EOF");
    assert_eq!(toks("1.e5"), "1 . e5 EOF");
    assert_eq!(toks("1._5"), "1 . _5 EOF");
    assert_eq!(toks("0x1..0xf"), "1 .. 15 EOF");
    assert_eq!(toks("1.5.5"), "f3/2 . 5 EOF");
    // `e` without exponent digits is not an exponent.
    assert_eq!(toks_ignoring_errors("1e"), "1 EOF");
    one_err("1e", "invalid suffix `e`");
    one_err("1e+", "invalid suffix `e`");
    one_err("1else", "invalid suffix `else`");
}

#[test]
fn float_errors() {
    one_err("0x1.8", "require a `p` exponent");
    one_err("1.5f33", "invalid suffix `f33` for float literal");
    one_err("1.5_", "separators");
    one_err("1e99999999", "exponent is too large");
    one_err("0x1.0p99999999", "exponent is too large");
}

// ----- numeric field names -----

#[test]
fn numeric_field_names() {
    assert_eq!(toks("t.0.1"), "t . 0 . 1 EOF");
    assert_eq!(toks("t.0"), "t . 0 EOF");
    assert_eq!(toks("t.10.20.30"), "t . 10 . 20 . 30 EOF");
    assert_eq!(toks("t . 0 . 1"), "t . 0 . 1 EOF");
    assert_eq!(toks("f(x).0"), "f ( x ) . 0 EOF");
    // No suffix, exponent, underscore or radix prefix after a dot.
    assert_eq!(toks("t.1u8"), "t . 1 u8 EOF");
    assert_eq!(toks("t.1e5"), "t . 1 e5 EOF");
    assert_eq!(toks("t.1_0"), "t . 1 _0 EOF");
    assert_eq!(toks("t.0x1"), "t . 0 x1 EOF");
    // Elsewhere floats work as usual, also right after `..`.
    assert_eq!(toks("0..1.5"), "0 .. f3/2 EOF");
    assert_eq!(toks("a ... 1.5"), "a ... f3/2 EOF");
    one_err("t.01", "leading zero");
}

// ----- character literals -----

#[test]
fn character_literals() {
    assert_eq!(toks("'a' 'Z' ' ' '\"' '\\''"), "'97 '90 '32 '34 '39 EOF");
    assert_eq!(toks("'é' '€' '😀'"), "'233 '8364 '128512 EOF");
    assert_eq!(toks("'\\n' '\\r' '\\t' '\\0' '\\\\'"), "'10 '13 '9 '0 '92 EOF");
    assert_eq!(toks("'\\x41' '\\xff' '\\x00'"), "'65 '255 '0 EOF");
    assert_eq!(toks("'\\u{41}' '\\u{1F600}' '\\u{10FFFF}'"), "'65 '128512 '1114111 EOF");
    match first_kind("'a'") {
        TokenKind::Int(l) => assert!(l.is_char && l.suffix.is_none()),
        k => panic!("{:?}", k),
    }
}

#[test]
fn character_literal_errors() {
    one_err("''", "empty character literal");
    one_err("'a", "unterminated character literal");
    one_err("'", "unterminated character literal");
    assert_eq!(errs("'\n'").len(), 2); // each quote opens a literal
    one_err("'ab'", "exactly one character");
    one_err("'\\q'", "unknown escape");
    one_err("'\\xZ1'", "invalid `\\x` escape");
    one_err("'\\u{D800}'", "surrogate");
    one_err("'\\u{110000}'", "above U+10FFFF");
}

// ----- strings -----

fn str_bytes(src: &str) -> (StrKind, Vec<u8>) {
    match first_kind(src) {
        TokenKind::Str(s) => (s.kind, s.bytes),
        k => panic!("{:?} lexed as {:?}", src, k),
    }
}

#[test]
fn regular_strings() {
    assert_eq!(str_bytes("\"\""), (StrKind::Regular, vec![]));
    assert_eq!(str_bytes("\"abc\""), (StrKind::Regular, b"abc".to_vec()));
    assert_eq!(str_bytes("\"a\\n\\r\\t\\0\\\\\\\"\\'b\""), (StrKind::Regular, b"a\n\r\t\0\\\"'b".to_vec()));
    assert_eq!(str_bytes("\"\\x41\\xff\\x80\""), (StrKind::Regular, vec![0x41, 0xff, 0x80]));
    assert_eq!(str_bytes("\"\\u{41}\\u{e9}\\u{1F600}\""), (StrKind::Regular, "Aé😀".as_bytes().to_vec()));
    assert_eq!(str_bytes("\"é€\""), (StrKind::Regular, "é€".as_bytes().to_vec()));
    // Adjacent strings are separate tokens.
    assert_eq!(toks("\"a\" \"b\""), "Regular\"a\" Regular\"b\" EOF");
}

#[test]
fn string_errors() {
    one_err("\"abc", "unterminated string literal");
    one_err("\"abc\ndef", "unterminated string literal");
    // The second line has its own error (it opens a string that never closes).
    assert_eq!(errs("\"abc\r\n\"").len(), 2);
    one_err("\"\\q\"", "unknown escape sequence `\\q`");
    one_err("\"\\x4\"", "invalid `\\x` escape");
    one_err("\"\\xg0\"", "invalid `\\x` escape");
    one_err("\"\\u41\"", "expected `{`");
    one_err("\"\\u{}\"", "1 to 6");
    one_err("\"\\u{1234567}\"", "1 to 6");
    one_err("\"\\u{41\"", "expected `}`");
    one_err("\"\\u{DFFF}\"", "surrogate");
    one_err("\"\\u{110000}\"", "above U+10FFFF");
    one_err("\"a\\", "unterminated string literal");
    one_err("\"a\\\nb", "unterminated string literal");
    // Recovery: the rest of the line still lexes.
    assert_eq!(toks_ignoring_errors("\"\\q\" x"), "Regular\"\" x EOF");
    assert_eq!(toks_ignoring_errors("\"abc\nx"), "Regular\"abc\" NL x EOF");
}

#[test]
fn c_strings() {
    assert_eq!(str_bytes("c\"hello\""), (StrKind::C, b"hello".to_vec()));
    assert_eq!(str_bytes("c\"a\\n\\xff\""), (StrKind::C, vec![b'a', b'\n', 0xff]));
    assert_eq!(str_bytes("c\"\""), (StrKind::C, vec![]));
    one_err("c\"a\\0b\"", "NUL");
    one_err("c\"a\\x00b\"", "NUL");
    one_err("c\"a\\u{0}b\"", "NUL");
    one_err("c\"abc", "unterminated string literal");
    // NUL is fine in a regular string.
    assert_eq!(str_bytes("\"a\\0\""), (StrKind::Regular, vec![b'a', 0]));
}

#[test]
fn raw_strings() {
    assert_eq!(str_bytes("`C:\\dir\\n`"), (StrKind::Raw, b"C:\\dir\\n".to_vec()));
    assert_eq!(str_bytes("`a\nb`"), (StrKind::Raw, b"a\nb".to_vec()));
    // The terminator after the opening delimiter is dropped, only one.
    assert_eq!(str_bytes("`\nabc`"), (StrKind::Raw, b"abc".to_vec()));
    assert_eq!(str_bytes("`\n\nabc`"), (StrKind::Raw, b"\nabc".to_vec()));
    assert_eq!(str_bytes("`\r\nabc`"), (StrKind::Raw, b"abc".to_vec()));
    // CR LF is normalized to LF inside.
    assert_eq!(str_bytes("`a\r\nb\r\n`"), (StrKind::Raw, b"a\nb\n".to_vec()));
    // Longer and shorter runs are content.
    assert_eq!(str_bytes("``a`b``"), (StrKind::Raw, b"a`b".to_vec()));
    assert_eq!(str_bytes("``a```b``"), (StrKind::Raw, b"a```b".to_vec()));
    assert_eq!(str_bytes("```a``b````c```"), (StrKind::Raw, b"a``b````c".to_vec()));
    assert_eq!(str_bytes("`a``b`"), (StrKind::Raw, b"a``b".to_vec()));
    assert_eq!(str_bytes("`\"quoted\" // not a comment`"), (StrKind::Raw, b"\"quoted\" // not a comment".to_vec()));
    assert_eq!(str_bytes("`é`"), (StrKind::Raw, "é".as_bytes().to_vec()));
}

#[test]
fn raw_string_spans_and_following_tokens() {
    let (tokens, errors) = run("`a\nb` x");
    assert!(errors.is_empty());
    assert_eq!(tokens[0].span, Span::new(FileId(0), 0, 5));
    assert_eq!(show(&tokens[1].kind), "x");
    // The newline inside the raw string is not a newline token.
    assert_eq!(toks("`a\nb`"), "Raw\"a\\nb\" EOF");
}

#[test]
fn raw_string_errors() {
    one_err("`abc", "unterminated raw string literal");
    one_err("``abc`", "unterminated raw string literal");
    one_err("`abc``", "unterminated raw string literal");
    one_err("`", "unterminated raw string literal");
    one_err("`a\rb`", "carriage return");
}

// ----- whitespace, line terminators and stray characters -----

#[test]
fn carriage_return_rules() {
    assert_eq!(toks("a\r\nb"), "a NL b EOF");
    one_err("a\rb", "carriage return");
    one_err("\"a\rb\"", "carriage return");
    one_err("// a\rb", "carriage return");
    one_err("/* a\rb */", "carriage return");
    assert_eq!(toks_ignoring_errors("a\rb"), "a b EOF");
    // Doc comments end before the CR of a CR LF.
    let mut diags = Diagnostics::new();
    let out = lex(FileId(0), "/// hi\r\n", &mut diags);
    assert_eq!(out.docs[0].text, "hi");
    assert!(diags.is_empty());
}

#[test]
fn unexpected_characters() {
    one_err("$", "unexpected character `$`");
    one_err("a \u{a0} b", "whitespace");
    one_err("a\u{2003}b", "whitespace");
    one_err("é", "non-ASCII");
    one_err("x = ü", "non-ASCII");
    one_err("\u{0c}", "U+000C");
    one_err("a\0b", "U+0000");
    one_err("a\u{feff}", "non-ASCII");
    assert_eq!(errs("$ $").len(), 2);
    assert_eq!(toks_ignoring_errors("a $ b é c"), "a b c EOF");
    // Non-ASCII is fine in comments and strings.
    assert_eq!(toks("a // é\n/* ü */ b \"é\" '€'"), "a NL b Regular\"\\xc3\\xa9\" '8364 EOF");
}

// ----- newlines -----

#[test]
fn newline_basics() {
    assert_eq!(toks("a\nb"), "a NL b EOF");
    assert_eq!(toks("a\n\n\n  \n b"), "a NL b EOF");
    assert_eq!(toks("a\n"), "a NL EOF");
    assert_eq!(toks("a"), "a EOF");
    assert_eq!(toks("\n\na\n\n"), "a NL EOF");
    assert_eq!(toks("a // c\n// d\n\nb"), "a NL b EOF");
    assert_eq!(toks("a\n/// doc\nb"), "a NL b EOF");
}

#[test]
fn semicolons() {
    assert_eq!(toks("a; b"), "a NL b EOF");
    assert_eq!(toks("a;\nb"), "a NL b EOF");
    assert_eq!(toks("a\n;b"), "a NL b EOF");
    assert_eq!(toks("a;;b"), "a NL b EOF");
    assert_eq!(toks("a ; ; ; b ;"), "a NL b NL EOF");
    assert_eq!(toks(";a"), "a EOF");
    assert_eq!(toks("(a; b)"), "( a NL b ) EOF");
    assert_eq!(toks("{ a; b }"), "{ a NL b } EOF");
    // `;` produces a newline even after a token that would suppress one.
    assert_eq!(toks("a +; b"), "a + NL b EOF");
    // The span of a `;` newline is the `;`.
    let (tokens, _) = run("a ;");
    assert_eq!(tokens[1].span, Span::new(FileId(0), 2, 3));
}

#[test]
fn newline_span_is_the_line_terminator() {
    let (tokens, _) = run("a  \r\n\n b");
    assert_eq!(tokens[1].kind, TokenKind::Newline);
    assert_eq!(tokens[1].span, Span::new(FileId(0), 3, 5));
}

#[test]
fn rule_1_suppresses_newlines_after_operators() {
    let binary = [
        "*", "/", "%", "*%", "*|", "*?", "+", "-", "+%", "+|", "+?", "-%", "-|", "-?", "<<", ">>", "<<%", "<<|", "<<?",
        "&", "|", "~", "..", "..=", "<", "<=", ">", ">=", "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=",
        "~=", "+%=", "-%=", "*%=", "<<%=", "+|=", "-|=", "*|=", "<<|=", ",", "::", ":=", ":", "=>", "->", "\\", ".",
        "is not", "not in", "is", "in", "and", "or", "else",
    ];
    for op in binary {
        let src = format!("a {}\nb", op);
        let got = toks(&src);
        assert!(!got.contains("NL"), "newline after `{}` must be suppressed: {}", op, got);
        assert_eq!(got, format!("a {} b EOF", op), "{}", op);
    }
    // Opening brackets, at any level.
    assert_eq!(toks("f(\na)"), "f ( a ) EOF");
    assert_eq!(toks("{\na }"), "{ a } EOF");
    assert_eq!(toks("[\na]"), "[ a ] EOF");
}

#[test]
fn rule_1_does_not_apply_after_other_tokens() {
    // Postfix operators, closing brackets, literals, keywords and sigils end a statement.
    for last in [
        "x?", "x!", "x^", "f()", "a[0]", "1", "'a'", "\"s\"", "x", "_", "true", "null", "---", "...", "not", "@a",
        "#a", "@",
    ] {
        let got = toks(&format!("{}\nb", last));
        assert!(got.contains(" NL b"), "newline after `{}` must be significant: {}", last, got);
    }
    assert_eq!(toks("return\nx"), "return NL x EOF");
    assert_eq!(toks("a ?\nb"), "a ? NL b EOF");
    assert_eq!(toks("a !\nb"), "a ! NL b EOF");
}

#[test]
fn rule_1_in_a_definitive_table() {
    // Cross-check against every Punct: exactly the listed ones suppress.
    let suppress: std::collections::HashSet<_> = [
        "+", "-", "*", "/", "%", "<<", ">>", "&", "|", "~", "+%", "-%", "*%", "<<%", "+|", "-|", "*|", "<<|", "+?",
        "-?", "*?", "<<?", "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "~=", "+%=", "-%=", "*%=",
        "<<%=", "+|=", "-|=", "*|=", "<<|=", "<", "<=", ">", ">=", "\\", "..", "..=", "::", ":=", ":", ",", ".", "=>",
        "->", "(", "[", "{", "is not", "not in",
    ]
    .into_iter()
    .collect();
    for &p in Punct::ALL {
        let last = TokenKind::Punct(p);
        assert_eq!(super::suppresses_newline_after(&last), suppress.contains(p.as_str()), "{:?}", p);
    }
}

#[test]
fn rule_2_defers_newlines_before_continuations() {
    assert_eq!(toks("a\nelse b"), "a else b EOF");
    assert_eq!(toks("a\nand b"), "a and b EOF");
    assert_eq!(toks("a\nor b"), "a or b EOF");
    assert_eq!(toks("a\n.b"), "a . b EOF");
    assert_eq!(toks("a\n  .b\n  .c()"), "a . b . c ( ) EOF");
    assert_eq!(toks("a\n\n// c\n/* d */\n .b"), "a . b EOF");
    assert_eq!(toks("a\n._x"), "a . _x EOF");
    // `..` and `.0` and `...` do not continue.
    assert_eq!(toks("a\n..b"), "a NL .. b EOF");
    assert_eq!(toks("a\n.0"), "a NL . 0 EOF");
    assert_eq!(toks("a\n...b"), "a NL ... b EOF");
    assert_eq!(toks("a\n. b"), "a NL . b EOF");
    // Other keywords and `not`/`is` do not continue.
    assert_eq!(toks("a\nnot b"), "a NL not b EOF");
    assert_eq!(toks("a\nis b"), "a NL is b EOF");
    assert_eq!(toks("a\nin b"), "a NL in b EOF");
    assert_eq!(toks("a\nelsewhere"), "a NL elsewhere EOF");
    // `else` as a continuation after a block.
    assert_eq!(toks("if x {\n a\n}\nelse {\n b\n}"), "if x { a NL } else { b NL } EOF");
}

#[test]
fn rule_2_with_unterminated_block_comment_between() {
    // The comment error does not hide the following continuation.
    let (tokens, errors) = run("a\n/* x");
    assert_eq!(errors.len(), 1);
    assert_eq!(tokens.len(), 3); // a NL EOF
}

#[test]
fn newlines_inside_parens_and_brackets_are_ignored() {
    assert_eq!(toks("f(a\nb)"), "f ( a b ) EOF");
    assert_eq!(toks("f(\na,\nb\n)"), "f ( a , b ) EOF");
    assert_eq!(toks("[a\nb]"), "[ a b ] EOF");
    assert_eq!(toks("([a\n]\n)\nx"), "( [ a ] ) NL x EOF");
    assert_eq!(toks("a(b)\nc"), "a ( b ) NL c EOF");
}

#[test]
fn braces_inside_parens_make_newlines_significant_again() {
    assert_eq!(toks("f(fn() {\na\nb\n})"), "f ( fn ( ) { a NL b NL } ) EOF");
    assert_eq!(toks("f({\na\n}\n, c)"), "f ( { a NL } , c ) EOF");
    // And parens inside such a brace suppress them again.
    assert_eq!(toks("{\nf(\na\n)\nb\n}"), "{ f ( a ) NL b NL } EOF");
    assert_eq!(toks("[{\na\nb\n}]"), "[ { a NL b NL } ] EOF");
}

#[test]
fn multi_line_block_comment_counts_as_a_newline() {
    assert_eq!(toks("a /* x\ny */ b"), "a NL b EOF");
    assert_eq!(toks("a /* x\r\ny */ b"), "a NL b EOF");
    assert_eq!(toks("a /* x y */ b"), "a b EOF");
    assert_eq!(toks("a /* /* \n */ */ b"), "a NL b EOF");
    // The usual rules still apply to it.
    assert_eq!(toks("a + /* x\ny */ b"), "a + b EOF");
    assert_eq!(toks("a /* x\ny */ else b"), "a else b EOF");
    assert_eq!(toks("f(a /* x\ny */ b)"), "f ( a b ) EOF");
    assert_eq!(toks("a /* x\ny */\n b"), "a NL b EOF");
}

#[test]
fn newline_at_eof_is_emitted_subject_to_the_rules() {
    assert_eq!(toks("a\n"), "a NL EOF");
    assert_eq!(toks("a +\n"), "a + EOF");
    assert_eq!(toks("a ;\n"), "a NL EOF");
}

#[test]
fn unclosed_brackets_at_eof_are_errors() {
    one_err("f(a", "unclosed `(`");
    one_err("{", "unclosed `{`");
    one_err("[", "unclosed `[`");
    assert_eq!(errs("( [ {").len(), 3);
    assert_eq!(toks_ignoring_errors("{ a\n"), "{ a NL EOF");
    assert_eq!(toks_ignoring_errors("(a\n"), "( a EOF");
}

// ----- match bodies -----

#[test]
fn match_body_newlines_always_end_arms() {
    // Rule 2 does not apply directly inside a match body.
    let src = "match x {\n  1 => a\n  .b => c\n  else => d\n}";
    assert_eq!(toks(src), "match x { 1 => a NL . b => c NL else => d NL } EOF");
    assert_eq!(toks("match x {\n a\n and b\n or c\n}"), "match x { a NL and b NL or c NL } EOF");
    // Rule 1 still applies.
    assert_eq!(toks("match x {\n 1 =>\n a\n}"), "match x { 1 => a NL } EOF");
    // A newline before the first arm is dropped (after `{`), and arms in nested braces use normal rules.
    assert_eq!(toks("match x {\n 1 => {\n a\n .b\n }\n}"), "match x { 1 => { a . b NL } NL } EOF");
    assert_eq!(toks("match x {\n 1 => f(\na\n.b\n)\n}"), "match x { 1 => f ( a . b ) NL } EOF");
}

#[test]
fn match_head_may_contain_nested_brackets() {
    // The `{` inside the parentheses is deeper than the match, so it is an ordinary brace.
    let src = "match f({\n a\n .b\n}) {\n 1 => x\n .y => z\n}";
    assert_eq!(toks(src), "match f ( { a . b NL } ) { 1 => x NL . y => z NL } EOF");
    let src = "match (a\n.b) {\n 1 => x\n .y => z\n}";
    assert_eq!(toks(src), "match ( a . b ) { 1 => x NL . y => z NL } EOF");
    let src = "match a[0] {\n 1 => x\n else => z\n}";
    assert_eq!(toks(src), "match a [ 0 ] { 1 => x NL else => z NL } EOF");
}

#[test]
fn nested_and_sequential_matches() {
    let src = "match a {\n 1 => match b {\n  2 => x\n  .y => z\n }\n .w => q\n}\nr\n.s";
    assert_eq!(toks(src), "match a { 1 => match b { 2 => x NL . y => z NL } NL . w => q NL } NL r . s EOF");
    // After the body closes, normal rules resume.
    let src = "match a {\n 1 => x\n}\n.foo";
    assert_eq!(toks(src), "match a { 1 => x NL } . foo EOF");
    // A match inside parentheses whose body never opens does not leak out.
    let (tokens, _) = run("(match a)\n{\n x\n .y\n}");
    let shown: Vec<_> = tokens.iter().map(|t| show(&t.kind)).collect();
    assert_eq!(shown.join(" "), "( match a ) NL { x . y NL } EOF");
}

#[test]
fn match_as_an_expression_inside_a_block() {
    let src = "f :: fn() {\n  x := match a {\n    1 => b\n    .c => d\n  }\n  x\n  .y\n}";
    assert_eq!(toks(src), "f :: fn ( ) { x := match a { 1 => b NL . c => d NL } NL x . y NL } EOF");
}

// ----- two-word operators -----

#[test]
fn is_not_and_not_in() {
    assert_eq!(toks("a is not b"), "a is not b EOF");
    assert_eq!(toks("a not in b"), "a not in b EOF");
    let (tokens, _) = run("a is not b");
    assert_eq!(tokens[1].kind, TokenKind::Punct(Punct::IsNot));
    assert_eq!(tokens[1].span, Span::new(FileId(0), 2, 8));
    let (tokens, _) = run("a  not \t in b");
    assert_eq!(tokens[1].kind, TokenKind::Punct(Punct::NotIn));
    assert_eq!(tokens[1].span, Span::new(FileId(0), 3, 11));
    // Comments between the words are fine.
    let (tokens, errors) = run("a is /* c */ not b");
    assert!(errors.is_empty());
    assert_eq!(tokens[1].kind, TokenKind::Punct(Punct::IsNot));
    assert_eq!(tokens[1].span, Span::new(FileId(0), 2, 16));
    let (tokens, _) = run("a not/**//**/in b");
    assert_eq!(tokens[1].kind, TokenKind::Punct(Punct::NotIn));
    let (tokens, _) = run("a is /* /* n */ */ not b");
    assert_eq!(tokens[1].kind, TokenKind::Punct(Punct::IsNot));
}

#[test]
fn is_not_and_not_in_are_not_formed_across_lines() {
    assert_eq!(first_kinds("a is\nnot b"), ["a", "is", "not", "b", "EOF"]);
    assert_eq!(first_kinds("a not\nin b"), ["a", "not", "NL", "in", "b", "EOF"]);
    assert_eq!(first_kinds("a is // c\nnot b"), ["a", "is", "not", "b", "EOF"]);
    assert_eq!(first_kinds("a is /* x\ny */ not b"), ["a", "is", "not", "b", "EOF"]);
    assert_eq!(first_kinds("a not /* x\ny */ in b"), ["a", "not", "NL", "in", "b", "EOF"]);
    assert_eq!(first_kinds("a is /* unterminated").len(), 3);
}

fn first_kinds(src: &str) -> Vec<String> {
    run(src).0.iter().map(|t| show(&t.kind)).collect()
}

#[test]
fn two_word_operators_need_whole_words() {
    assert_eq!(toks("a is nothing"), "a is nothing EOF");
    assert_eq!(toks("a is not_b"), "a is not_b EOF");
    assert_eq!(toks("a not inside"), "a not inside EOF");
    assert_eq!(toks("a not in_"), "a not in_ EOF");
    assert_eq!(toks("a is b not c"), "a is b not c EOF");
    assert_eq!(toks("not a"), "not a EOF");
    assert_eq!(toks("a and not b"), "a and not b EOF");
    assert_eq!(toks("a is not not b"), "a is not not b EOF");
    assert_eq!(toks("not not in x"), "not not in x EOF");
}

#[test]
fn two_word_operators_in_newline_rules() {
    // A newline after `is not` / `not in` is suppressed (rule 1).
    assert_eq!(toks("a is not\nb"), "a is not b EOF");
    assert_eq!(toks("a not in\nb"), "a not in b EOF");
    // A bare `not` ends a statement.
    assert_eq!(toks("a not\nb"), "a not NL b EOF");
}

// ----- bracket mismatches -----

#[test]
fn mismatched_closers() {
    // A stray closer is reported and dropped.
    one_err(")", "unmatched closing `)`");
    assert_eq!(toks_ignoring_errors("a ) b"), "a b EOF");
    one_err("a }", "unmatched closing `}`");
    assert_eq!(errs("(]"), ["unmatched closing `]`", "unclosed `(`"]);
    // Mismatch with a matching opener further out: the intervening openers are unclosed.
    let (tokens, errors) = run("( { ) }");
    assert_eq!(errors.len(), 2, "{:?}", errors);
    assert!(errors[0].contains("unclosed `{`"));
    assert!(errors[1].contains("unmatched closing `}`"));
    assert_eq!(tokens.iter().map(|t| show(&t.kind)).collect::<Vec<_>>().join(" "), "( { ) EOF");
    // Recovery keeps newline handling sane afterwards.
    assert_eq!(toks_ignoring_errors("{ ( }\na\n"), "{ ( } NL a NL EOF");
}

// ----- misc -----

#[test]
fn full_statements() {
    assert_eq!(
        toks("x: u8 = 0xFF // c\nfor i in 0..n {\n  s += i\n}"),
        "x : u8 = 255 NL for i in 0 .. n { s += i NL } EOF"
    );
    assert_eq!(toks("p := a.b.0.1(c)?.d!"), "p := a . b . 0 . 1 ( c ) ? . d ! EOF");
    assert_eq!(toks("fn(x: ^const T) -> E!?T \\ {A, B} => x"), "fn ( x : ^ const T ) -> E ! ? T \\ { A , B } => x EOF");
}

#[test]
fn lexing_never_panics_on_truncations() {
    let src = "a := 0x1.8p1f32 + 'x' c\"a\\xff\" `` raw `` /* c */ is not t.0.1 \\u{41} @x #y ---\nmatch x { 1 => 'é' }\n\"\\u{10FFFF}\" 1e5 1.5e-3 0b101u8";
    for i in 0..=src.len() {
        if src.is_char_boundary(i) {
            let (tokens, _) = run(&src[..i]);
            assert_eq!(tokens.last().map(|t| &t.kind), Some(&TokenKind::Eof));
        }
    }
}

#[test]
fn spans_are_in_bounds_and_ordered() {
    let src = "x := f(0x10, \"s\\n\", 'c') // done\n`raw`  is not y\n";
    let (tokens, errors) = run(src);
    assert!(errors.is_empty());
    let mut prev = 0;
    for t in &tokens {
        assert!(t.span.lo >= prev && t.span.hi >= t.span.lo && t.span.hi as usize <= src.len(), "{:?}", t);
        prev = t.span.hi;
    }
}

#[test]
fn c_equality_operators_are_reported() {
    one_err("a == b", "`==` is not an operator; compare with `is`");
    one_err("a != b", "`!=` is not an operator; compare with `is not`");
}
