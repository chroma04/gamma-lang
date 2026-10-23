//! Tests of the fixes that diagnostics carry: `gamma fix` and `--diag-format=json`.

use serde_json::Value;
use std::path::{Path, PathBuf};
use std::process::Command;

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

/// A fresh directory for one test.
fn dir(name: &str) -> PathBuf {
    let d = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("fix").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

/// Runs `gamma` with `args`; returns whether it succeeded and its standard error.
fn gamma(args: &[&str], cwd: &Path) -> (bool, String) {
    let out = Command::new(GAMMA).args(args).current_dir(cwd).output().unwrap();
    (out.status.success(), String::from_utf8_lossy(&out.stderr).into_owned())
}

/// A program with one mistake of each kind that has a single fix.
const BROKEN: &str = "\
limit := 0X10

Sh :: union {
    Circle: i32,
    Dot,
}

area :: fn(s: Sh) -> i32 {
    return match s {
        .Circle => 1,
        _ => 0,
    }
}

twice :: fn(x: i32) -> i32 => x * 2

main :: fn() needs Out {
    xs := [3]i32(1, 2, 3)
    s := xs[..]
    twice(4)
    if s.len() > 2 && limit > 1 {
        print(\"{}\\n\", area(.Dot))
    }
}
";

const FIXED: &str = "\
limit :: 0x10

Sh :: union {
    Circle: i32,
    Dot,
}

area :: fn(s: Sh) -> i32 {
    return match s {
        .Circle(_) => 1
        _ => 0
    }
}

twice :: fn(x: i32) -> i32 => x * 2

main :: fn() needs Out {
    xs := [3]i32(1, 2, 3)
    s := xs[..]
    _ = twice(4)
    if s.len > 2 and limit > 1 {
        print(\"{}\\n\", area(.Dot))
    }
}
";

#[test]
fn fix_applies_every_single_fix_until_the_program_checks() {
    let d = dir("all");
    std::fs::write(d.join("main.gm"), BROKEN).unwrap();
    let (ok, err) = gamma(&["fix", "main.gm"], &d);
    assert!(ok, "{err}");
    assert_eq!(err, "gamma: applied 8 fixes in 1 file\n");
    assert_eq!(std::fs::read_to_string(d.join("main.gm")).unwrap(), FIXED);
    // Nothing is left to do.
    let (ok, err) = gamma(&["fix", "main.gm"], &d);
    assert!(ok && err.is_empty(), "{err}");
}

#[test]
fn fix_leaves_ambiguous_diagnostics_and_reports_them() {
    let d = dir("ambiguous");
    let text = "g :: fn() -> ?i32 => 1\n\nh :: fn() -> ?i32 {\n    x := try g() else 0\n    return x\n}\n\nmain :: fn() {\n    _ = h()\n}\n";
    std::fs::write(d.join("main.gm"), text).unwrap();
    let (ok, err) = gamma(&["fix", "main.gm"], &d);
    assert!(!ok);
    assert!(err.contains("`try x else d` is ambiguous"), "{err}");
    assert!(!err.contains("applied"), "{err}");
    assert_eq!(std::fs::read_to_string(d.join("main.gm")).unwrap(), text);
}

#[test]
fn fix_changes_only_the_files_of_the_package() {
    let d = dir("scope");
    // The program imports a module from outside its package, which has a fixable mistake.
    let outside = d.join("outside");
    std::fs::create_dir_all(&outside).unwrap();
    let lib = "pub f :: fn() -> i32 => 0X1\n";
    std::fs::write(outside.join("gamma.toml"), "[package]\nname = \"lib\"\n").unwrap();
    std::fs::write(outside.join("lib.gm"), lib).unwrap();
    let pkg = d.join("pkg");
    std::fs::create_dir_all(&pkg).unwrap();
    std::fs::write(
        pkg.join("gamma.toml"),
        "[package]\nname = \"p\"\n\n[dependencies]\nlib = { path = \"../outside\" }\n",
    )
    .unwrap();
    std::fs::write(pkg.join("main.gm"), "lib :: import \"lib\"\n\nmain :: fn() {\n    _ = lib.f() + 0B1\n}\n").unwrap();
    let (ok, err) = gamma(&["fix", "main.gm"], &pkg);
    assert!(!ok, "{err}");
    assert!(err.contains("radix prefixes must be lowercase"), "{err}");
    assert!(err.contains("applied 1 fix in 1 file"), "{err}");
    assert!(std::fs::read_to_string(pkg.join("main.gm")).unwrap().contains("+ 0b1"));
    assert_eq!(std::fs::read_to_string(outside.join("lib.gm")).unwrap(), lib);
}

#[test]
fn json_diagnostics_have_locations_and_fixes() {
    let d = dir("json");
    std::fs::write(d.join("main.gm"), "main :: fn() {\n    x := 1 && 2\n}\n").unwrap();
    let (ok, err) = gamma(&["check", "--diag-format=json", "main.gm"], &d);
    assert!(!ok);
    let lines: Vec<Value> = err.lines().map(|l| serde_json::from_str(l).expect("one JSON object per line")).collect();
    assert_eq!(lines.len(), 1, "{err}");
    let e = &lines[0];
    assert_eq!(e["severity"], "error");
    assert_eq!(e["message"], "`&&` is not an operator; use `and`");
    assert_eq!(e["file"], "main.gm");
    assert_eq!((e["line"].as_u64(), e["column"].as_u64()), (Some(2), Some(12)));
    assert_eq!((e["offset"].as_u64(), e["end_offset"].as_u64()), (Some(26), Some(28)));
    assert_eq!(e["notes"], serde_json::json!([]));
    let fix = &e["fixes"][0];
    assert_eq!(fix["message"], "replace `&&` with `and`");
    assert_eq!(fix["edits"][0]["text"], "and");
    assert_eq!(fix["edits"][0]["offset"], 26);
    assert_eq!(fix["edits"][0]["end_column"], 14);
}

/// Runs `gamma fix main.gm` on `broken`, expecting `fixed` afterwards and a clean check.
fn fixes_to(name: &str, broken: &str, fixed: &str) {
    let d = dir(name);
    std::fs::write(d.join("main.gm"), broken).unwrap();
    let (ok, err) = gamma(&["fix", "main.gm"], &d);
    assert!(ok, "{err}");
    assert_eq!(std::fs::read_to_string(d.join("main.gm")).unwrap(), fixed);
    let (ok, err) = gamma(&["check", "main.gm"], &d);
    assert!(ok, "{err}");
}

#[test]
fn fix_stray_braces_chained_comparisons_and_assignment_in_conditions() {
    fixes_to(
        "syntax2",
        "main :: fn() needs Out {\n    a := 1\n    b := 2\n    c := 3\n    if a < b <= c {\n        print(\"100}\\n\")\n    }\n    if a = 1 {\n        print(\"{}\\n\", a)\n    }\n}\n",
        "main :: fn() needs Out {\n    a := 1\n    b := 2\n    c := 3\n    if a < b and b <= c {\n        print(\"100}}\\n\")\n    }\n    if a is 1 {\n        print(\"{}\\n\", a)\n    }\n}\n",
    );
}

#[test]
fn json_fixes_of_try_in_a_test_and_a_stray_brace() {
    let d = dir("json2");
    let src = "main :: fn() {}\n\nf :: fn() -> i32!i32 => 1\n\ntest \"t\" {\n    x := try f()\n    expect(x is 1)\n}\n";
    std::fs::write(d.join("main.gm"), src).unwrap();
    let (ok, err) = gamma(&["test", "--diag-format=json", "main.gm"], &d);
    assert!(!ok);
    let e: Value = serde_json::from_str(err.lines().next().unwrap()).unwrap();
    let edits = e["fixes"][0]["edits"].as_array().unwrap();
    let at = src.find("try f()").unwrap() as u64;
    assert_eq!((edits[0]["offset"].as_u64(), edits[0]["end_offset"].as_u64()), (Some(at), Some(at + 4)));
    assert_eq!(edits[0]["text"], "ok(");
    assert_eq!((edits[1]["offset"].as_u64(), edits[1]["text"].as_str()), (Some(at + 7), Some(")")));

    let src = "main :: fn() needs Out {\n    print(\"a}\\n\")\n}\n";
    std::fs::write(d.join("main.gm"), src).unwrap();
    let (ok, err) = gamma(&["check", "--diag-format=json", "main.gm"], &d);
    assert!(!ok);
    let e: Value = serde_json::from_str(err.lines().next().unwrap()).unwrap();
    let edit = &e["fixes"][0]["edits"][0];
    let at = src.find('}').unwrap() as u64;
    assert_eq!(
        (edit["offset"].as_u64(), edit["end_offset"].as_u64(), edit["text"].as_str()),
        (Some(at), Some(at + 1), Some("}}"))
    );
}

#[test]
fn fix_c_style_equality_operators() {
    fixes_to(
        "equality",
        "main :: fn() {\n    a := 1\n    if a==1 or a != 2 {\n    }\n}\n",
        "main :: fn() {\n    a := 1\n    if a is 1 or a is not 2 {\n    }\n}\n",
    );
}

#[test]
fn chained_comparisons_with_side_effects_or_under_not_are_left_alone() {
    for cond in ["a < f() < c", "not a < b < c", "a < b < c < a", "a is b < c"] {
        let d = dir("chain_keep");
        let text = format!(
            "f :: fn() -> i32 => 2\n\nmain :: fn() {{\n    a := 1\n    b := 2\n    c := 3\n    if {cond} {{\n    }}\n}}\n"
        );
        std::fs::write(d.join("main.gm"), &text).unwrap();
        let (ok, _) = gamma(&["fix", "main.gm"], &d);
        assert!(!ok, "{cond}");
        assert_eq!(std::fs::read_to_string(d.join("main.gm")).unwrap(), text, "{cond}");
    }
}

#[test]
fn fix_adds_the_import_and_the_module_prefix_of_std_names() {
    // No import yet: it goes to the top of the file.
    fixes_to(
        "import_none",
        "main :: fn() needs Fs {\n    f: ?File = null\n    _ = f\n}\n",
        "fs :: import \"std/fs\"\n\nmain :: fn() needs Fs {\n    f: ?fs.File = null\n    _ = f\n}\n",
    );
    // Below the comments that document the file.
    fixes_to(
        "import_doc",
        "//! A tool.\n//! It opens files.\n\nmain :: fn() needs Fs {\n    f: ?File = null\n    _ = f\n}\n",
        "//! A tool.\n//! It opens files.\n\nfs :: import \"std/fs\"\n\nmain :: fn() needs Fs {\n    f: ?fs.File = null\n    _ = f\n}\n",
    );
    // After the last import; several uses share the one import.
    fixes_to(
        "import_after",
        "io :: import \"std/io\"\n\nmain :: fn() needs Fs {\n    f: ?File = null\n    g: ?File = null\n    _ = f\n    _ = g\n}\n",
        "io :: import \"std/io\"\nfs :: import \"std/fs\"\n\nmain :: fn() needs Fs {\n    f: ?fs.File = null\n    g: ?fs.File = null\n    _ = f\n    _ = g\n}\n",
    );
    // Already imported: only the prefix is added.
    fixes_to(
        "import_have",
        "files :: import \"std/fs\"\n\nmain :: fn() needs Fs {\n    f: ?File = null\n    _ = f\n}\n",
        "files :: import \"std/fs\"\n\nmain :: fn() needs Fs {\n    f: ?files.File = null\n    _ = f\n}\n",
    );
}

#[test]
fn fix_qualifies_effect_operations() {
    fixes_to(
        "ops",
        "main :: fn() needs Fs {\n    make_dir(\"target/gamma-fix-ops\") else ()\n}\n",
        "main :: fn() needs Fs {\n    Fs.make_dir(\"target/gamma-fix-ops\") else ()\n}\n",
    );
}

#[test]
fn fix_adds_undeclared_effects_to_signatures() {
    // Each round adds what the functions perform; the callers get theirs in the next.
    // `main` gets only ambient effects: it must handle the others itself.
    let d = dir("needs");
    let broken = "Log :: effect {\n    log: fn(msg: string)\n}\n\ngreet :: fn(name: string) {\n    print(\"hi {}\\n\", name)\n}\n\nwarn :: fn() -> i32 needs Out {\n    eprint(\"careful\\n\")\n    Log.log(\"tick\")\n    return 2\n}\n\nmain :: fn() {\n    greet(\"a\")\n    _ = warn()\n}\n";
    let fixed = "Log :: effect {\n    log: fn(msg: string)\n}\n\ngreet :: fn(name: string) needs Out {\n    print(\"hi {}\\n\", name)\n}\n\nwarn :: fn() -> i32 needs Out + Err + Log {\n    eprint(\"careful\\n\")\n    Log.log(\"tick\")\n    return 2\n}\n\nmain :: fn() needs Out + Err {\n    greet(\"a\")\n    _ = warn()\n}\n";
    std::fs::write(d.join("main.gm"), broken).unwrap();
    let (ok, err) = gamma(&["fix", "main.gm"], &d);
    assert!(!ok);
    assert_eq!(std::fs::read_to_string(d.join("main.gm")).unwrap(), fixed);
    assert!(err.contains("performs the effect `Log`, which the function does not declare"), "{err}");
    assert_eq!(err.matches("error:").count(), 1, "{err}");
}

#[test]
fn effects_not_named_as_written_get_no_needs_fix() {
    // An effect of another module is written with its module's name, which the fix does
    // not guess.
    let d = dir("needs_qualified");
    std::fs::create_dir_all(d.join("lg")).unwrap();
    std::fs::write(d.join("lg/lg.gm"), "pub Log :: effect {\n    log: fn(msg: string)\n}\n").unwrap();
    std::fs::write(d.join("main.gm"), "lg :: import \"./lg\"\n\nmain :: fn() {\n    lg.Log.log(\"x\")\n}\n").unwrap();
    let (ok, out) = gamma(&["check", "--diag-format=json", "main.gm"], &d);
    assert!(!ok);
    assert!(out.contains("does not declare") && out.contains("\"fixes\":[]"), "{out}");
}
