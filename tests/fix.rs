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
        else => 0,
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
    assert_eq!(err, "gamma: applied 9 fixes in 1 file\n");
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
