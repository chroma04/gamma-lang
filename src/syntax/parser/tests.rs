//! Parser unit tests. They need the lexer, so source text is the input.
//!
//! Expectations are single-line renderings of the tree (see `ast_dump`).

use super::parse_file;
use crate::diag::{Diagnostics, Severity};
use crate::source::FileId;
use crate::syntax::ast::*;
use crate::syntax::{ast_dump, lexer};

/// Parses `src`, returning the tree and the error messages (lexer and parser).
fn parse(src: &str) -> (File, Vec<String>) {
    let mut diags = Diagnostics::new();
    let lexed = lexer::lex(FileId(0), src, &mut diags);
    let mut ids = NodeIdGen::new();
    let file = parse_file(FileId(0), &lexed, &mut ids, &mut diags);
    let errors = diags.iter().filter(|d| d.severity == Severity::Error).map(|d| d.message.clone()).collect();
    (file, errors)
}

/// The initializer of `x :: <src>`, rendered on one line. Panics on errors.
fn ex(src: &str) -> String {
    let (file, errors) = parse(&format!("x :: {}\n", src));
    assert!(errors.is_empty(), "unexpected errors for {:?}: {:?}", src, errors);
    match &file.items[..] {
        [Item { kind: ItemKind::Binding(b), .. }] => ast_dump::dump_expr(&b.init),
        other => panic!("expected one binding for {:?}, got {} items", src, other.len()),
    }
}

/// The errors for `x :: <src>`.
fn ex_errs(src: &str) -> Vec<String> {
    parse(&format!("x :: {}\n", src)).1
}

/// Asserts that `x :: <src>` parses as `expected`.
#[track_caller]
fn check(src: &str, expected: &str) {
    let got = ex(src);
    assert_eq!(got, expected, "for source {:?}", src);
}

/// Asserts that `x :: <src>` gives exactly one error containing `needle`, and
/// that the tree for it is still produced as `expected` (when not empty).
#[track_caller]
fn check_err(src: &str, needle: &str) {
    let errs = ex_errs(src);
    assert_eq!(errs.len(), 1, "expected one error for {:?}, got {:?}", src, errs);
    assert!(errs[0].contains(needle), "error {:?} for {:?} does not contain {:?}", errs[0], src, needle);
}

/// The whole file's dump collapsed to one line.
fn flat(src: &str) -> String {
    let (file, errors) = parse(src);
    assert!(errors.is_empty(), "unexpected errors for {:?}: {:?}", src, errors);
    collapse(&ast_dump::dump_file(&file))
}

fn collapse(dump: &str) -> String {
    let mut out = String::new();
    for (i, line) in dump.lines().enumerate() {
        if i > 0 {
            out.push(' ');
        }
        out.push_str(line.trim_start());
    }
    out
}

/// The statements of `f :: fn() { <src> }` rendered on one line each, joined by spaces.
#[track_caller]
fn stmts(src: &str) -> String {
    let full = flat(&format!("f :: fn() {{\n{}\n}}\n", src));
    let prefix = "(file (item (binding const f (fn (params) (block";
    let inner = full.strip_prefix(prefix).unwrap_or_else(|| panic!("unexpected dump {:?}", full));
    let inner = inner.strip_suffix(")))))").unwrap_or_else(|| panic!("unexpected dump {:?}", full));
    inner.trim().to_string()
}

#[track_caller]
fn check_stmts(src: &str, expected: &str) {
    assert_eq!(stmts(src), expected, "for source {:?}", src);
}

/// The errors for a statement list inside a function body.
fn stmt_errs(src: &str) -> Vec<String> {
    parse(&format!("f :: fn() {{\n{}\n}}\n", src)).1
}

#[track_caller]
fn check_stmt_err(src: &str, needle: &str) {
    let errs = stmt_errs(src);
    assert_eq!(errs.len(), 1, "expected one error for {:?}, got {:?}", src, errs);
    assert!(errs[0].contains(needle), "error {:?} for {:?} does not contain {:?}", errs[0], src, needle);
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

#[test]
fn arithmetic_precedence_and_associativity() {
    check("a + b * c", "(binary + (ident a) (binary * (ident b) (ident c)))");
    check("a * b + c", "(binary + (binary * (ident a) (ident b)) (ident c))");
    check("a - b - c", "(binary - (binary - (ident a) (ident b)) (ident c))");
    check("a / b % c", "(binary % (binary / (ident a) (ident b)) (ident c))");
    check("-a * b", "(binary * (unary neg (ident a)) (ident b))");
    check("a +% b -| c *? d", "(binary -| (binary +% (ident a) (ident b)) (binary *? (ident c) (ident d)))");
    check("-x.y", "(unary neg (field (ident x) y))");
    check("- -x", "(unary neg (unary neg (ident x)))");
    check("~a & b", "(binary & (unary bit-not (ident a)) (ident b))");
    check("&a.b", "(unary addr-of (field (ident a) b))");
    check("!a + try b", "(binary + (unary make-err (ident a)) (unary try (ident b)))");
}

#[test]
fn shifts_bitwise_ranges_comparisons() {
    check("1 << n", "(binary << (int 1) (ident n))");
    check("(1 << n) - 1", "(binary - (paren (binary << (int 1) (ident n))) (int 1))");
    check("1 << (n - 1)", "(binary << (int 1) (paren (binary - (ident n) (int 1))))");
    check("x >> 2", "(binary >> (ident x) (int 2))");
    check("x <<? 2", "(binary <<? (ident x) (int 2))");
    check("a & b & c", "(binary & (binary & (ident a) (ident b)) (ident c))");
    check("a ~ b ~ c", "(binary ~ (binary ~ (ident a) (ident b)) (ident c))");
    check("(a & b) | c", "(binary | (paren (binary & (ident a) (ident b))) (ident c))");
    check("a + 1 & b", "(binary & (binary + (ident a) (int 1)) (ident b))");
    check("a & b << 2", "(binary & (ident a) (binary << (ident b) (int 2)))");
    check("lo..hi", "(binary .. (ident lo) (ident hi))");
    check("lo..=hi + 1", "(binary ..= (ident lo) (binary + (ident hi) (int 1)))");
    check("a | b .. c", "(binary .. (binary | (ident a) (ident b)) (ident c))");
    check("a < b", "(binary < (ident a) (ident b))");
    check("a .. b < c", "(binary < (binary .. (ident a) (ident b)) (ident c))");
    check("x is not null", "(binary is-not (ident x) null)");
    check("x not in xs", "(binary not-in (ident x) (ident xs))");
    check("x in 0..10", "(binary in (ident x) (binary .. (int 0) (int 10)))");
    check("c in ('\\n', '\\r')", "(binary in (ident c) (tuple (char 10) (char 13)))");
}

#[test]
fn logical_operators_and_else() {
    check("not a and b", "(binary and (unary not (ident a)) (ident b))");
    check("not not a", "(unary not (unary not (ident a)))");
    check("not a < b", "(unary not (binary < (ident a) (ident b)))");
    check("a and b and c", "(binary and (binary and (ident a) (ident b)) (ident c))");
    check("a or b or c", "(binary or (binary or (ident a) (ident b)) (ident c))");
    check("(a and b) or c", "(binary or (paren (binary and (ident a) (ident b))) (ident c))");
    check("x < y and y < z", "(binary and (binary < (ident x) (ident y)) (binary < (ident y) (ident z)))");
    check("x else y else z", "(else (ident x) (else (ident y) (ident z)))");
    check("a and b else c", "(else (binary and (ident a) (ident b)) (ident c))");
    check("x else e => e + 1", "(else (ident x) (capture e) (binary + (ident e) (int 1)))");
    check("x else a and b", "(else (ident x) (binary and (ident a) (ident b)))");
    check("x else e => y else z", "(else (ident x) (capture e) (else (ident y) (ident z)))");
    check(
        "n *? 10 else return !.TooBig",
        "(else (binary *? (ident n) (int 10)) (return (unary make-err (dot TooBig))))",
    );
    check("x else break", "(else (ident x) (break))");
    check("x else _ => 0", "(else (ident x) (capture _) (int 0))");
    check("(try x) else d", "(else (paren (unary try (ident x))) (ident d))");
    check("try (x else d)", "(unary try (paren (else (ident x) (ident d))))");
    check("try x + 1", "(binary + (unary try (ident x)) (int 1))");
}

#[test]
fn precedence_errors() {
    check_err("a and b or c", "cannot mix `and` and `or`");
    check_err("a or b and c", "cannot mix `and` and `or`");
    check_err("a and b and c or d", "cannot mix `and` and `or`");
    check_err("a & b | c", "cannot mix `&`, `|` and `~`");
    check_err("a | b ~ c", "cannot mix `&`, `|` and `~`");
    check_err("a ~ b & c", "cannot mix `&`, `|` and `~`");
    check_err("a < b < c", "cannot be chained");
    check_err("a is b is c", "cannot be chained");
    check_err("a in b < c", "cannot be chained");
    check_err("a <= b >= c", "cannot be chained");
    check_err("1 << n - 1", "must be parenthesized");
    check_err("a + b << 1", "must be parenthesized");
    check_err("a * b >> 1", "must be parenthesized");
    check_err("a << b + c", "must be parenthesized");
    check_err("a << b << c", "shifts cannot be chained");
    check_err("a >> b << c", "shifts cannot be chained");
    check_err("a..b..c", "range operators cannot be chained");
}

// ---------------------------------------------------------------------------
// Statements and the disambiguation rules of §A.7
// ---------------------------------------------------------------------------

#[test]
fn statements_and_disambiguation() {
    check_stmts("x := 1", "(binding var x (int 1))");
    check_stmts("x: u8 = ---", "(binding var x (ty (ident u8)) uninit)");
    check_stmts("a, b := f()", "(binding var (names a b) (call (ident f)))");
    check_stmts("a, b = (b, a)", "(assign = (places (ident a) (ident b)) (tuple (ident b) (ident a)))");
    check_stmts("_ = f()", "(assign = (places underscore) (call (ident f)))");
    check_stmts(
        "outer: for i in xs { break outer }",
        "(expr (for (label outer) (pats (bind i)) (ident xs) (block (expr (break (label outer))))))",
    );
    check_stmts("for i in xs { break i }", "(expr (for (pats (bind i)) (ident xs) (block (expr (break (ident i))))))");
    check_stmts("y := x![i]", "(binding var y (index (res-unwrap (ident x)) (ident i)))");
    check_stmts("y := x!(a)", "(binding var y (bang (ident x) (paren (ident a))))");
    check_stmts("T :: E![]u8", "(binding const T (bang (ident E) (slice-type (ident u8))))");
    check_stmts(
        "if v := f() and v > 0 { }",
        "(expr (if (branch (cond (bind (bind v) (call (ident f))) (binary > (ident v) (int 0))) (block))))",
    );
    check_stmts("while line := next() { }", "(while (cond (bind (bind line) (call (ident next)))) (block))");
    check_stmts("resume k(1)", "(expr (resume (ident k) (int 1)))");
    check_stmts("defer x += 1", "(defer (assign += (places (ident x)) (int 1)))");
    check_stmt_err("x := 1 +", "expected");
}

#[test]
fn a_doc_comment_attaches_only_to_the_next_line_in_crlf_files() {
    let docs = |src: &str| -> Vec<Option<String>> {
        let (file, errors) = parse(src);
        assert!(errors.is_empty(), "{errors:?}");
        file.items.iter().map(|i| i.doc.clone()).collect()
    };
    assert_eq!(docs("/// far\r\n\r\n\r\n\r\n\r\na :: 1\r\n"), vec![None]);
    assert_eq!(docs("/// near\r\na :: 1\r\n"), vec![Some("near".to_string())]);
    assert_eq!(docs("/// far\n\n\n\n\na :: 1\n"), vec![None]);
}
