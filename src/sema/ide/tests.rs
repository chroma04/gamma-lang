//! Tests of the editor index on small programs. A `$` in the source marks the position
//! that a query is made at; it is removed before analysis.

use crate::driver::Session;
use crate::ide::{IdeIndex, Kind};
use crate::source::FileId;
use std::path::PathBuf;

struct Fixture {
    sess: Session,
    idx: IdeIndex,
    file: FileId,
    text: String,
    off: u32,
    _dir: Dir,
}

struct Dir(PathBuf);

impl Drop for Dir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn fresh_dir(tag: &str) -> Dir {
    static N: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);
    let n = N.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    let dir = std::env::temp_dir().join(format!("gamma-ide-{}-{}-{}", tag, std::process::id(), n));
    std::fs::create_dir_all(&dir).unwrap();
    Dir(dir)
}

/// Analyzes a module with one file, `main.gm`, holding `src` (without its `$`).
fn analyze(src: &str) -> Fixture {
    analyze_files(&[("main.gm", src)], "main.gm")
}

fn analyze_files(files: &[(&str, &str)], at: &str) -> Fixture {
    let dir = fresh_dir("t");
    let mut off = 0;
    let mut at_text = String::new();
    for (name, text) in files {
        let clean = text.replace('$', "");
        if *name == at {
            if let Some(p) = text.find('$') {
                off = p as u32;
            }
            at_text = clean.clone();
        }
        let path = dir.0.join(name);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, clean).unwrap();
    }
    let root = dir.0.join(at).parent().unwrap().to_path_buf();
    let mut sess = Session::new();
    let idx = crate::sema::analyze_for_ide(&mut sess, &root);
    let path = dir.0.join(at);
    let file = sess.sources.files().find(|(_, f)| f.path == path).map(|(id, _)| id).expect("file loaded");
    Fixture { sess, idx, file, text: at_text, off, _dir: dir }
}

impl Fixture {
    fn hover(&self) -> String {
        self.idx.hover_at(self.file, self.off).map(|h| h.hover.code).unwrap_or_default()
    }

    fn doc(&self) -> Option<String> {
        self.idx.hover_at(self.file, self.off).and_then(|h| h.hover.doc)
    }

    /// The text of the declared name that the position refers to, with its line.
    fn definition(&self) -> Option<(String, u32)> {
        let span = self.idx.definition_at(self.file, self.off)?;
        let f = self.sess.sources.file(span.file);
        let (line, _) = f.line_col(span.lo);
        Some((f.src[span.lo as usize..span.hi as usize].to_string(), line))
    }

    fn errors(&self) -> Vec<String> {
        self.sess.diags.iter().map(|d| d.message.clone()).collect()
    }

    /// The members offered after `receiver`, as written in `context`.
    fn members(&self, context: &str, receiver: &str) -> Vec<String> {
        let lo = (self.text.find(context).unwrap() + context.find(receiver).unwrap()) as u32;
        let m = self.idx.members_at(self.file, lo, lo + receiver.len() as u32);
        m.map(|m| m.iter().map(|i| i.label.clone()).collect()).unwrap_or_default()
    }

    fn local_names(&self) -> Vec<String> {
        let mut v: Vec<String> = self.idx.locals_at(self.file, self.off).map(|s| s.item.label.clone()).collect();
        v.sort();
        v
    }
}

const PROGRAM: &str = r#"Point :: struct {
    x: i32
    y: i32
}

Shape :: union {
    Circle: i32
    Square
}

/// Adds the coordinates.
sum :: fn(p: Point, extra: i32 = 0) -> i32 {
    return p.x + p.y + extra
}

greet :: fn(n: i32) needs Out {
    print("{}\n", n)
}

main :: fn() needs Out {
    pt := Point(x: 1, y: 2)
    total := sum(pt)
    greet(total)
    s := Shape.Circle(3)
    match s {
        .Circle(r) => print("{}\n", r)
        .Square => {}
    }
}
"#;

#[test]
fn program_checks_cleanly() {
    let f = analyze(PROGRAM);
    assert_eq!(f.errors(), Vec::<String>::new());
}

#[test]
fn hover_on_a_local_shows_its_type() {
    let f = analyze(&PROGRAM.replace("total := sum", "to$tal := sum"));
    assert_eq!(f.hover(), "total: i32");
}

#[test]
fn hover_on_a_use_of_a_local() {
    let f = analyze(&PROGRAM.replace("greet(total)", "greet(to$tal)"));
    assert_eq!(f.hover(), "total: i32");
}

#[test]
fn hover_on_a_call_shows_the_signature_and_effects() {
    let f = analyze(&PROGRAM.replace("    greet(total)", "    gr$eet(total)"));
    assert_eq!(f.hover(), "greet :: fn(n: i32) needs Out");
}

#[test]
fn hover_shows_defaults_and_docs() {
    let f = analyze(&PROGRAM.replace("total := sum(pt)", "total := su$m(pt)"));
    assert_eq!(f.hover(), "sum :: fn(p: Point, extra: i32 = 0) -> i32");
    assert_eq!(f.doc().as_deref(), Some("Adds the coordinates."));
}

#[test]
fn hover_on_a_type_shows_its_declaration() {
    let f = analyze(&PROGRAM.replace("pt := Point(", "pt := Poi$nt("));
    assert!(f.hover().starts_with("Point :: struct {"), "{}", f.hover());
}

#[test]
fn definition_of_a_function_and_a_local() {
    let f = analyze(&PROGRAM.replace("total := sum", "total := su$m"));
    assert_eq!(f.definition(), Some(("sum".to_string(), 12)));
    let f = analyze(&PROGRAM.replace("greet(total)", "greet(to$tal)"));
    assert_eq!(f.definition(), Some(("total".to_string(), 22)));
}

#[test]
fn definition_of_a_field() {
    let f = analyze(&PROGRAM.replace("p.x + p.y", "p.$x + p.y"));
    assert_eq!(f.definition(), Some(("x".to_string(), 2)));
    // A named argument of a construction.
    let f = analyze(&PROGRAM.replace("Point(x: 1", "Point($x: 1"));
    assert_eq!(f.definition(), Some(("x".to_string(), 2)));
}

#[test]
fn definition_of_a_variant_and_a_parameter() {
    let f = analyze(&PROGRAM.replace(".Square =>", ".Squ$are =>"));
    assert_eq!(f.definition(), Some(("Square".to_string(), 8)));
    let f = analyze(&PROGRAM.replace("Shape.Circle(3)", "Shape.Cir$cle(3)"));
    assert_eq!(f.definition(), Some(("Circle".to_string(), 7)));
    let f = analyze(&PROGRAM.replace("+ extra", "+ ex$tra"));
    assert_eq!(f.definition(), Some(("extra".to_string(), 12)));
}

#[test]
fn definition_of_a_named_argument() {
    let f = analyze(&PROGRAM.replace("sum(pt)", "sum(pt, ex$tra: 2)"));
    assert_eq!(f.definition(), Some(("extra".to_string(), 12)));
}

#[test]
fn methods_resolve_to_the_function() {
    let src = r#"Counter :: struct { n: i32 }

bump :: fn(c: ^Counter) {
    c.n += 1
}

main :: fn() {
    c := Counter(n: 0)
    c.bu$mp()
}
"#;
    let f = analyze(src);
    assert_eq!(f.errors(), Vec::<String>::new());
    assert_eq!(f.definition(), Some(("bump".to_string(), 3)));
    assert!(f.hover().starts_with("bump :: fn(c: ^Counter)"), "{}", f.hover());
}

#[test]
fn module_members_resolve_across_files() {
    let files = [
        ("main.gm", "util :: import \"./util\"\n\nmain :: fn() {\n    x := util.dou$ble(2)\n    _ = x\n}\n"),
        ("util/util.gm", "/// Twice.\npub double :: fn(n: i32) -> i32 => n * 2\n"),
    ];
    let f = analyze_files(&files, "main.gm");
    assert_eq!(f.errors(), Vec::<String>::new());
    let span = f.idx.definition_at(f.file, f.off).expect("definition");
    let target = f.sess.sources.file(span.file);
    assert!(target.path.ends_with("util/util.gm"));
    assert_eq!(&target.src[span.lo as usize..span.hi as usize], "double");
    assert_eq!(f.hover(), "double :: fn(n: i32) -> i32");
    assert_eq!(f.doc().as_deref(), Some("Twice."));
}

#[test]
fn an_import_binding_resolves_to_the_module() {
    let files = [
        ("main.gm", "util :: import \"./util\"\n\nmain :: fn() {\n    x := ut$il.double(2)\n    _ = x\n}\n"),
        ("util/util.gm", "pub double :: fn(n: i32) -> i32 => n * 2\n"),
    ];
    let f = analyze_files(&files, "main.gm");
    assert_eq!(f.definition(), Some(("util".to_string(), 1)));
    assert!(f.hover().starts_with("util :: import"), "{}", f.hover());
}

#[test]
fn completion_scopes_list_locals_in_scope() {
    let src = PROGRAM.replace("    greet(total)", "    greet(total)\n    $");
    let f = analyze(&src);
    assert_eq!(f.local_names(), vec!["pt", "total"]);
    let src = PROGRAM.replace("return p.x", "$return p.x");
    let f = analyze(&src);
    assert_eq!(f.local_names(), vec!["extra", "p"]);
}

#[test]
fn locals_declared_later_are_not_visible() {
    let src = PROGRAM.replace("pt := Point", "$pt := Point");
    let f = analyze(&src);
    assert!(f.local_names().is_empty(), "{:?}", f.local_names());
}

#[test]
fn members_of_a_struct_value() {
    let f = analyze(PROGRAM);
    // Fields, then the functions of the type's module that take it first.
    assert_eq!(f.members("sum(pt)", "pt"), vec!["x", "y", "sum"]);
}

#[test]
fn members_of_a_module_and_a_union_type() {
    let files = [
        (
            "main.gm",
            "util :: import \"./util\"\nColor :: union { Red, Green }\n\nmain :: fn() {\n    _ = util.double(1)\n    c := Color.Red\n    _ = c\n}\n",
        ),
        ("util/util.gm", "pub double :: fn(n: i32) -> i32 => n * 2\nhidden :: 1\n"),
    ];
    let f = analyze_files(&files, "main.gm");
    assert_eq!(f.members("util.double(1)", "util"), vec!["double"]);
    let lo = f.text.find("Color.Red").unwrap() as u32;
    let m = f.idx.members_at(f.file, lo, lo + 5).unwrap();
    assert_eq!(m.iter().map(|i| i.label.as_str()).collect::<Vec<_>>(), vec!["Red", "Green"]);
}

#[test]
fn outline_is_hierarchical() {
    let f = analyze(PROGRAM);
    let out = &f.idx.outlines[&f.file];
    let names: Vec<&str> = out.iter().map(|s| s.name.as_str()).collect();
    assert_eq!(names, vec!["Point", "Shape", "sum", "greet", "main"]);
    assert_eq!(out[0].kind, Kind::Struct);
    assert_eq!(out[0].children.iter().map(|c| c.name.as_str()).collect::<Vec<_>>(), vec!["x", "y"]);
    assert_eq!(out[1].kind, Kind::Union);
    assert_eq!(out[1].children[0].kind, Kind::Variant);
    assert_eq!(out[2].kind, Kind::Function);
    assert!(out[2].detail.starts_with("fn(p: Point"));
}

#[test]
fn a_syntax_error_keeps_the_rest_of_the_index() {
    let src = PROGRAM.replace("greet(total)", "greet(total");
    let f = analyze(&src);
    assert!(!f.errors().is_empty());
    let out = &f.idx.outlines[&f.file];
    assert!(out.iter().any(|s| s.name == "sum"));
}

#[test]
fn a_type_error_keeps_hover() {
    let src = PROGRAM.replace("total := sum(pt)", "total := sum(pt) + \"x\"\n    ot$her := 1");
    let f = analyze(&src);
    assert!(!f.errors().is_empty());
    assert_eq!(f.hover(), "other: i32");
}

#[test]
fn expression_types_show_on_hover() {
    let src = "main :: fn() {\n    n := 1 +$ 2\n    _ = n\n}\n";
    let f = analyze(src);
    assert_eq!(f.hover(), "i32");
}

const EFFECTS: &str = r#"Log :: effect {
    /// Records a line.
    note: fn(msg: string)
}

Counting :: struct { lines: i32 }

handler Log for ^Counting {
    note(self, msg) => { self.lines += 1 }
}

largest :: fn(T: type, xs: []const T) -> ?T where Ord(T) {
    best := xs.first() else return null
    for x in xs {
        if x > best { best = x }
    }
    return best
}

work :: fn() needs Log {
    Log.note("a")
    note("b")
}

main :: fn() {
    c := Counting(lines: 0)
    printer :: handler Log {
        note(msg) => { c.lines += msg.len.trunc(i32) }
    }
    with printer { work() }
    scores := [3]i32(4, 9, 2)
    outer: for i in 0..3 {
        for j in 0..3 {
            if j is 1 { continue outer }
            if i is 2 { break outer }
        }
    }
    _ = largest(scores)
    add :: fn(n: i32) -> i32 => n + c.lines
    _ = add(1)
}

test "counts" {
    c := Counting(lines: 0)
    with &c { work() }
    expect(c.lines is 2)
}
"#;

#[test]
fn effects_and_handlers_resolve() {
    let f = analyze(EFFECTS);
    assert_eq!(f.errors(), Vec::<String>::new());
    // A bare operation name, and one named through its effect.
    let at = |needle: &str, delta: usize| {
        analyze(&EFFECTS.replacen(needle, &format!("{}${}", &needle[..delta], &needle[delta..]), 1))
    };
    let g = at("    note(\"b\")", 8);
    assert_eq!(g.definition(), Some(("note".to_string(), 3)));
    assert!(g.hover().starts_with("Log.note :: fn(msg: string)"), "{}", g.hover());
    assert_eq!(g.doc(), None);
    let g = at("Log.note(\"a\")", 6);
    assert_eq!(g.definition(), Some(("note".to_string(), 3)));
    // A clause names its operation; its parameter is a local.
    let g = at("        note(msg) => { c.lines", 10);
    assert_eq!(g.definition(), Some(("note".to_string(), 3)));
    let g = at("c.lines += msg.len", 14);
    assert_eq!(g.definition().map(|d| d.0), Some("msg".to_string()));
    // `self` in a handler declaration is its subject.
    let g = at("{ self.lines += 1 }", 4);
    assert_eq!(g.definition().map(|d| d.0), Some("self".to_string()));
}

#[test]
fn captured_locals_and_closures_resolve() {
    let g = analyze(&EFFECTS.replace("n + c.lines", "n + c.$lines"));
    assert_eq!(g.definition(), Some(("lines".to_string(), 6)));
    let g = analyze(&EFFECTS.replace("n + c.lines", "n + $c.lines"));
    assert_eq!(g.definition(), Some(("c".to_string(), 26)));
    assert_eq!(g.hover(), "c: Counting");
    let g = analyze(&EFFECTS.replace("n + c.lines", "$n + c.lines"));
    assert_eq!(g.hover(), "n: i32");
}

#[test]
fn generic_parameters_and_loops_resolve() {
    let g = analyze(&EFFECTS.replace("[]const T) -> ?T", "[]const $T) -> ?T"));
    assert_eq!(g.definition(), Some(("T".to_string(), 12)));
    let g = analyze(&EFFECTS.replace("if x > best", "if x > be$st"));
    assert_eq!(g.hover(), "best: T");
    let g = analyze(&EFFECTS.replace("_ = largest(scores)", "_ = larg$est(scores)"));
    assert_eq!(g.hover(), "largest :: fn(T: type, xs: []const T) -> ?T");
}

#[test]
fn labels_resolve() {
    let g = analyze(&EFFECTS.replace("continue outer", "continue ou$ter"));
    assert_eq!(g.definition(), Some(("outer".to_string(), 32)));
    let g = analyze(&EFFECTS.replace("break outer", "break ou$ter"));
    assert_eq!(g.definition(), Some(("outer".to_string(), 32)));
}

#[test]
fn tests_and_handlers_appear_in_the_outline() {
    let f = analyze(EFFECTS);
    let names: Vec<String> = f.idx.outlines[&f.file].iter().map(|s| s.name.clone()).collect();
    assert_eq!(
        names,
        vec!["Log", "Counting", "handler Log for ^Counting", "largest", "work", "main", "test \"counts\""]
    );
    let log = &f.idx.outlines[&f.file][0];
    assert_eq!(log.kind, Kind::Effect);
    assert_eq!(log.children[0].name, "note");
    assert_eq!(log.children[0].kind, Kind::Operation);
}

#[test]
#[ignore]
fn smoke_over_conformance_tests() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/conformance");
    let mut files = vec![];
    fn walk(d: &std::path::Path, out: &mut Vec<PathBuf>) {
        for e in std::fs::read_dir(d).unwrap().flatten() {
            let p = e.path();
            if p.is_dir() {
                walk(&p, out);
            } else if p.extension().is_some_and(|x| x == "gm") {
                out.push(p);
            }
        }
    }
    walk(&root, &mut files);
    files.sort();
    let t = std::time::Instant::now();
    let (mut n, mut refs, mut panics) = (0, 0, vec![]);
    for f in &files {
        let r = std::panic::catch_unwind(|| {
            let mut sess = Session::new();
            let idx = crate::sema::analyze_for_ide(&mut sess, f);
            idx.refs.len()
        });
        match r {
            Ok(k) => {
                n += 1;
                refs += k;
            }
            Err(_) => panics.push(f.clone()),
        }
    }
    println!("{} files, {} refs, {:?}, panics: {:?}", n, refs, t.elapsed(), panics);
    // Text with syntax errors may check badly; the server falls back to the syntax errors.
    assert!(panics.iter().all(|p| p.to_string_lossy().contains("/parse/")), "{:?}", panics);
}

/// Lists the identifiers of the files named by `GAMMA_COVERAGE` (space separated) that
/// have neither a reference nor a declaration, to find gaps in name resolution.
#[test]
#[ignore]
fn coverage_of_identifiers() {
    use crate::syntax::token::TokenKind;
    let Ok(list) = std::env::var("GAMMA_COVERAGE") else { return };
    for path in list.split_whitespace() {
        let mut sess = Session::new();
        let idx = crate::sema::analyze_for_ide(&mut sess, std::path::Path::new(path));
        let (file, f) = sess.sources.files().find(|(_, f)| f.path.to_string_lossy() == path).unwrap();
        let mut diags = crate::diag::Diagnostics::new();
        let lexed = crate::syntax::lexer::lex(file, &f.src, &mut diags);
        let (mut total, mut missing) = (0, vec![]);
        for t in &lexed.tokens {
            if !matches!(t.kind, TokenKind::Ident(_)) {
                continue;
            }
            total += 1;
            let covered = idx.refs.iter().any(|r| r.from == t.span || r.to == t.span) || idx.defs.contains_key(&t.span);
            if !covered {
                let (line, _) = f.line_col(t.span.lo);
                missing.push(format!("{}:{} {}", line, t.span.lo, &f.src[t.span.lo as usize..t.span.hi as usize]));
            }
        }
        println!("{}: {} identifiers, {} without a reference or declaration", path, total, missing.len());
        for m in &missing {
            println!("   {}", m);
        }
    }
}

#[test]
#[ignore]
fn smoke_over_the_library() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("lib/std");
    let mut dirs: Vec<_> =
        std::fs::read_dir(&root).unwrap().flatten().map(|e| e.path()).filter(|p| p.is_dir()).collect();
    dirs.sort();
    for d in dirs {
        let t = std::time::Instant::now();
        let mut sess = Session::new();
        let idx = crate::sema::analyze_for_ide(&mut sess, &d);
        let lines: usize =
            sess.sources.files().filter(|(_, f)| f.path.starts_with(&d)).map(|(_, f)| f.src.lines().count()).sum();
        let errs = sess.diags.iter().filter(|x| x.severity == crate::diag::Severity::Error).count();
        println!(
            "{:<12} {:>5} lines {:>4} errors {:>6} refs {:>5} defs {:>6} types {:>5} scopes  {:?}",
            d.file_name().unwrap().to_string_lossy(),
            lines,
            errs,
            idx.refs.len(),
            idx.defs.len(),
            idx.types.len(),
            idx.scopes.len(),
            t.elapsed()
        );
    }
}
