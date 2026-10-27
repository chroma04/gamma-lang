//! Tests of the source formatter (`gamma fmt`).
//!
//! The corpus tests format every `.gm` file under `lib/` and `tests/conformance/`
//! (without writing anything back) and check the two safety properties: the
//! output lexes to the same tokens as the input, and formatting is idempotent.
//! The unit tests pin down one spacing or indentation rule each.

mod common;

use gamma::diag::Diagnostics;
use gamma::fmt::format_source;
use gamma::source::FileId;
use gamma::syntax::lexer::lex;
use gamma::syntax::token::{Punct, TokenKind};
use std::path::{Path, PathBuf};

fn gm_files(dir: &Path, out: &mut Vec<PathBuf>) {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(dir).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    for p in entries {
        if p.is_dir() {
            gm_files(&p, out);
        } else if p.extension().is_some_and(|e| e == "gm") {
            out.push(p);
        }
    }
}

fn corpus() -> Vec<PathBuf> {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let mut files = Vec::new();
    gm_files(&root.join("lib"), &mut files);
    gm_files(&root.join("tests/conformance"), &mut files);
    assert!(files.len() > 50, "the corpus should not be empty");
    files
}

/// The tokens of `src` as (kind, text) pairs with newline tokens as plain `\n`.
fn tokens(src: &str) -> Vec<(TokenKind, String)> {
    let mut diags = Diagnostics::new();
    let lexed = lex(FileId(0), src, &mut diags);
    lexed
        .tokens
        .iter()
        .map(|t| {
            let text = match t.kind {
                TokenKind::Newline | TokenKind::Eof | TokenKind::Punct(Punct::IsNot | Punct::NotIn) => String::new(),
                _ => src[t.span.lo as usize..t.span.hi as usize].to_string(),
            };
            (t.kind.clone(), text)
        })
        .collect()
}

#[test]
fn corpus_keeps_tokens_and_is_idempotent() {
    let mut formatted = 0;
    for path in corpus() {
        let src = std::fs::read_to_string(&path).unwrap();
        let out = match format_source(&src) {
            Ok(out) => out,
            Err(e) => {
                // Files that test lexical errors cannot be formatted.
                assert!(!e.contains("internal error"), "{}: {}", path.display(), e);
                continue;
            }
        };
        formatted += 1;
        assert_eq!(tokens(&src), tokens(&out), "{}: tokens changed", path.display());
        let again = format_source(&out).unwrap_or_else(|e| panic!("{}: second pass failed: {}", path.display(), e));
        assert_eq!(out, again, "{}: not idempotent", path.display());
    }
    assert!(formatted > 250, "only {} files were formatted", formatted);
}

#[test]
fn library_is_formatted() {
    // The library is kept in the formatter's style: run `gamma fmt lib` after editing it.
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let mut files = Vec::new();
    gm_files(&root.join("lib"), &mut files);
    let changed: Vec<String> = files
        .into_iter()
        .filter(|path| {
            let src = std::fs::read_to_string(path).unwrap();
            format_source(&src).unwrap() != src
        })
        .map(|p| p.display().to_string())
        .collect();
    assert!(changed.is_empty(), "not formatted (run `gamma fmt lib`): {}", changed.join(", "));
}

fn check(input: &str, expected: &str) {
    let out = format_source(input).unwrap_or_else(|e| panic!("cannot format {:?}: {}", input, e));
    assert_eq!(out, expected, "formatting {:?}", input);
    assert_eq!(format_source(&out).unwrap(), out, "not idempotent on {:?}", input);
    assert_eq!(tokens(input), tokens(&out), "tokens changed for {:?}", input);
}

#[test]
fn crlf_input_gives_lf_output_inside_block_comments_too() {
    let out = format_source("x := 1 /* a\r\n b */\r\ny := 2\r\n").unwrap();
    assert_eq!(out, "x := 1 /* a\n b */\ny := 2\n");
}

#[test]
fn tokens_that_lex_differently_when_joined_stay_apart() {
    // `..` next to `..` would join into `...` and `.`.
    let src = "x := a .. ..b\n";
    if let Ok(out) = format_source(src) {
        assert_eq!(tokens(src), tokens(&out));
    }
}

#[test]
fn dot_after_a_space_at_line_start_stays_apart() {
    // `. p` is not a continuation line; gluing it to `p` would turn it into one (§2.8) and
    // change the indentation the second time around.
    check(
        "f :: fn() {\n    if b {\n.        p.q = 1\n    }\n}\n",
        "f :: fn() {\n    if b {\n        . p.q = 1\n    }\n}\n",
    );
}

#[test]
fn binary_operators_get_spaces() {
    check("x:=a+b*c\n", "x := a + b * c\n");
    check("x   =   a<<2|b\n", "x = a << 2 | b\n");
    check("y +%= 1\nz=a+%b\n", "y +%= 1\nz = a +% b\n");
    check("f :: fn(a:i32)->i32=>a\n", "f :: fn(a: i32) -> i32 => a\n");
    check("c = a and b or not d\n", "c = a and b or not d\n");
    check("c = a   is   not b\nd = x not   in y\n", "c = a is not b\nd = x not in y\n");
}

#[test]
fn unary_and_postfix_operators_are_tight() {
    check("x = - y\n", "x = -y\n");
    check("x = a - -b\n", "x = a - -b\n");
    check("x = a-b\n", "x = a - b\n");
    check("p = & x\nq = ~ m\nr = a ~ b\n", "p = &x\nq = ~m\nr = a ~ b\n");
    check("v = p ^ + 1\n", "v = p^ + 1\n");
    check("v = x ? + 1\nw = y ! + 1\n", "v = x? + 1\nw = y! + 1\n");
    check("return - 1\n", "return -1\n");
    check("f(-1, - x, ! e)\n", "f(-1, -x, !e)\n");
    check("return ! . OutOfMemory\n", "return !.OutOfMemory\n");
}

#[test]
fn ranges_and_dots_are_tight() {
    check("for i in 0 .. n { }\n", "for i in 0..n {}\n");
    check("s = a[ 1 ..= 3 ]\nt = a[ .. n ]\n", "s = a[1..=3]\nt = a[..n]\n");
    check("x = a . b . c ( )\n", "x = a.b.c()\n");
    check("f(a, .. rest)\n", "f(a, ..rest)\n");
    check("c = .Red\nd = f( .Red )\n", "c = .Red\nd = f(.Red)\n");
}

#[test]
fn commas_semicolons_and_colons() {
    check("f(a ,b ,c)\n", "f(a, b, c)\n");
    check("a ; b\n", "a; b\n");
    check("x : i32 = 1\n", "x: i32 = 1\n");
    check("struct { a : i32 ; b :u8 }\n", "struct { a: i32; b: u8 }\n");
    check("MASK: u32 : 0xFF\n", "MASK: u32 : 0xFF\n");
    check("MASK : u32 :0xFF\n", "MASK: u32 : 0xFF\n");
}

#[test]
fn brackets() {
    check("f ( a , b )\n", "f(a, b)\n");
    check("x = a [ i ]\n", "x = a[i]\n");
    check("x = ( a + b ) * c\n", "x = (a + b) * c\n");
    check("x = fn ( a ) => a\n", "x = fn(a) => a\n");
    check("x = @size_of ( T )\n", "x = @size_of(T)\n");
    check("#align ( 8 )\nS :: struct { }\n", "#align(8)\nS :: struct {}\n");
    check("x = f ( ) ( 1 )\n", "x = f()(1)\n");
}

#[test]
fn braces() {
    check("if a{ b }\n", "if a { b }\n");
    check("if a {b}\n", "if a { b }\n");
    check("S :: struct { }\n", "S :: struct {}\n");
    check("f :: fn ( ) {\n}\n", "f :: fn() {\n}\n");
    check("x = f({ a })\n", "x = f({ a })\n");
}

#[test]
fn types() {
    check("x : ? T\n", "x: ?T\n");
    check("x : ^ T\ny: [ ] u8\nz:[ 4 ]u8\n", "x: ^T\ny: []u8\nz: [4]u8\n");
    check("p : [ ^ ] u8\nq : [] const u8\nr : ^ const T\n", "p: [^]u8\nq: []const u8\nr: ^const T\n");
    check("p : ? [ ^ ] u8\nq : [] ? u8\n", "p: ?[^]u8\nq: []?u8\n");
    check("r : IoError ! usize\ns : E ! ? ^ T\n", "r: IoError!usize\ns: E!?^T\n");
    check("f :: fn ( ) -> AllocError ! ( ) { }\n", "f :: fn() -> AllocError!() {}\n");
    check("g :: fn ( ) -> E ! [ ] u8 { }\n", "g :: fn() -> E![]u8 {}\n");
    check("h :: fn ( p : ^ fn ( a : i32 ) needs Out ) { }\n", "h :: fn(p: ^fn(a: i32) needs Out) {}\n");
    check("x = f ( ) !\ny = z ^ . w\n", "x = f()!\ny = z^.w\n");
}

#[test]
fn effect_sets_are_tight() {
    check("f :: fn() needs Out + Err { }\n", "f :: fn() needs Out + Err {}\n");
    check("f :: fn() needs not  { }\n", "f :: fn() needs not {}\n");
}

#[test]
fn indentation_follows_brackets() {
    check("f :: fn() {\nx := 1\n  if a {\ny\n   }\n}\n", "f :: fn() {\n    x := 1\n    if a {\n        y\n    }\n}\n");
    check("f(\na,\n   b\n)\n", "f(\n    a,\n    b\n)\n");
    check("x := [\n1,\n2,\n]\n", "x := [\n    1,\n    2,\n]\n");
    check("if a {\n  x\n} else {\n  y\n}\n", "if a {\n    x\n} else {\n    y\n}\n");
    check("f(g(\na\n))\n", "f(g(\n    a\n))\n");
}

#[test]
fn continuation_lines() {
    check("x := a +\nb\n", "x := a +\n    b\n");
    check("x := obj\n.a()\n.b()\n", "x := obj\n    .a()\n    .b()\n");
    check("if a\nand b {\nc\n}\n", "if a\n    and b {\n    c\n}\n");
}

#[test]
fn aligned_parameters_stay_aligned() {
    check("f :: fn(a: i32,\n        b: i32) {\n  x\n}\n", "f :: fn(a: i32,\n        b: i32) {\n    x\n}\n");
    check("    f :: fn(a: i32,\n            b: i32) {\n  x\n}\n", "f :: fn(a: i32,\n        b: i32) {\n    x\n}\n");
}

#[test]
fn blank_lines() {
    check("a\n\n\n\nb\n", "a\n\nb\n");
    check("{\n\n  a\n\n}\n", "{\n    a\n}\n");
    check("\n\na\n", "a\n");
    check("a\n\n\n", "a\n");
    check("a   \nb\t\n", "a\nb\n");
    check("", "");
    check("\n\n", "");
}

#[test]
fn comments() {
    check("// a\n   // b\nx\n", "// a\n// b\nx\n");
    check("f {\n// c\nx\n}\n", "f {\n    // c\n    x\n}\n");
    check("x := 1 // c\n", "x := 1 // c\n");
    check("x := 1// c\n", "x := 1 // c\n");
    check("x := 1       // c\ny := 22      // d\n", "x := 1       // c\ny := 22      // d\n");
    check("x   :=   1 // c   \n", "x := 1 // c\n");
    check("/// doc\npub f :: fn() {}\n", "/// doc\npub f :: fn() {}\n");
    check("x := /* c */ 1\n", "x := /* c */ 1\n");
    check("  /* a\n   b */\nx\n", "/* a\n   b */\nx\n");
    check("f {\n  x\n  // end\n}\n", "f {\n    x\n    // end\n}\n");
}

#[test]
fn aligned_columns_are_kept() {
    check("a    :: 1\nbcde :: 2\n", "a    :: 1\nbcde :: 2\n");
    check("match x {\n.A     => 1\n.Bcdef => 2\n}\n", "match x {\n    .A     => 1\n    .Bcdef => 2\n}\n");
}

#[test]
fn literals_are_verbatim() {
    check("x = \"a  +  b\"\ny = `raw\n   text`\n", "x = \"a  +  b\"\ny = `raw\n   text`\n");
    check("x = 0x1_F + 1.5e3f32 + 'a'\n", "x = 0x1_F + 1.5e3f32 + 'a'\n");
    check("x = c\"hi\"\n", "x = c\"hi\"\n");
}

#[test]
fn lexical_errors_are_reported() {
    let e = format_source("x := \"abc\n").unwrap_err();
    assert!(e.contains("unterminated"), "{}", e);
    assert!(format_source("a == b\n").is_err());
}

#[test]
fn padding_is_kept_only_when_it_aligns() {
    check("f   :: fn() {}\n", "f :: fn() {}\n");
    check("a    :: 1\nbcd  :: 2\n", "a    :: 1\nbcd  :: 2\n");
    check("P :: struct {\n    x:    i32,\n    yy:   i32,\n}\n", "P :: struct {\n    x:    i32,\n    yy:   i32,\n}\n");
}

#[test]
fn else_chains_after_blocks_align_with_their_statement() {
    let src = "f :: fn() {\n    when A { g() }\n        else when B { h() }\n        else { k() }\n    if c {\n        g()\n    }\n    else { h() }\n    x := if c { 1 }\n        else { 2 }\n}\n";
    let want = "f :: fn() {\n    when A { g() }\n    else when B { h() }\n    else { k() }\n    if c {\n        g()\n    }\n    else { h() }\n    x := if c { 1 }\n        else { 2 }\n}\n";
    check(src, want);
}

#[test]
fn pattern_bindings_starting_with_a_dot_are_statements() {
    check(
        "f :: fn(u: U) -> i32 {\n    x := 1\n    .A(n) := u else return 0\n    y := a\n        .b()\n    return n\n}\n",
        "f :: fn(u: U) -> i32 {\n    x := 1\n    .A(n) := u else return 0\n    y := a\n        .b()\n    return n\n}\n",
    );
}

#[test]
fn a_variant_starting_a_block_is_not_a_continuation() {
    check(
        "f :: fn(a: bool) -> V {\n    k: V = if a {\n            .A\n    } else {\n    .B\n    }\n    return k\n}\n",
        "f :: fn(a: bool) -> V {\n    k: V = if a {\n        .A\n    } else {\n        .B\n    }\n    return k\n}\n",
    );
}

#[test]
fn missing_final_newline_is_added() {
    assert_eq!(format_source("a :: 1").unwrap(), "a :: 1\n");
    assert_eq!(format_source("f :: fn() {\n    g()\n}").unwrap(), "f :: fn() {\n    g()\n}\n");
    assert_eq!(format_source("a :: 1 // done").unwrap(), "a :: 1 // done\n");
}

#[test]
fn aligned_padding_is_decided_the_same_after_reindenting() {
    // The lines are aligned at the same source column but indented differently, so the
    // first pass used to keep the padding and the second to collapse it.
    for src in [
        "main :: fn() {\n\ta   :: 1\n  bc :: 2\n}\n",
        "main :: fn() {\n  bc :: 2\n\ta   :: 1\n}\n",
        "T :: struct {\n\tab:   i32\n  c:  i32\n}\n",
    ] {
        let once = format_source(src).unwrap();
        assert_eq!(format_source(&once).unwrap(), once, "not idempotent on {:?}", src);
    }
}

#[test]
fn directory_walk_skips_hidden_directories_and_package_target() {
    let root = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("fmt_walk");
    let _ = std::fs::remove_dir_all(&root);
    let ugly = "main::fn(){\n}\n";
    for d in ["src", ".git", "target", "src/target", ".claude/x"] {
        std::fs::create_dir_all(root.join(d)).unwrap();
        std::fs::write(root.join(d).join("a.gm"), ugly).unwrap();
    }
    std::fs::write(root.join("gamma.toml"), "[package]\nname = \"w\"\nversion = \"0.1.0\"\n").unwrap();
    let gamma = |args: &[&Path]| common::gamma().arg("fmt").args(args).output().unwrap();
    assert!(gamma(&[&root]).status.success());
    let read = |d: &str| std::fs::read_to_string(root.join(d).join("a.gm")).unwrap();
    assert_ne!(read("src"), ugly);
    assert_ne!(read("src/target"), ugly, "only the package's own target directory is skipped");
    for d in [".git", "target", ".claude/x"] {
        assert_eq!(read(d), ugly, "{} must be left alone", d);
    }
    // A file named explicitly is always formatted.
    assert!(gamma(&[&root.join("target/a.gm")]).status.success());
    assert_ne!(read("target"), ugly);
}
