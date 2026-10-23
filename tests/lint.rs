//! Tests of the `gamma lint` command: `--fix`, levels from the command line and
//! gamma.toml, `--rules`, JSON output, and that the library and examples lint clean.
//! The findings of each rule are tested in `tests/conformance/lint/`.

use serde_json::Value;
use std::path::{Path, PathBuf};
use std::process::Command;

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

/// A fresh directory for one test.
fn dir(name: &str) -> PathBuf {
    let d = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("lint").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

/// Runs `gamma` with `args`; returns whether it succeeded, its standard output and
/// its standard error.
fn gamma(args: &[&str], cwd: &Path) -> (bool, String, String) {
    let out = Command::new(GAMMA).args(args).current_dir(cwd).output().unwrap();
    let text = |b: &[u8]| String::from_utf8_lossy(b).into_owned();
    (out.status.success(), text(&out.stdout), text(&out.stderr))
}

/// A program with warnings of several rules, each with a fix.
const MESSY: &str = "\
fmt :: import \"std/fmt\"

main :: fn() needs Out {
    unused := 5
    done := false
    if done is false {
        print(\"hi\\n\")
    } else {
        if not not done { print(\"x\\n\") }
    }
    n := (3)
    print(\"{}\\n\", n)
}
";

const TIDY: &str = "\
main :: fn() needs Out {
    _ = 5
    done := false
    if not done {
        print(\"hi\\n\")
    } else if done { print(\"x\\n\") }
    n := 3
    print(\"{}\\n\", n)
}
";

#[test]
fn reports_each_rule_by_name() {
    let d = dir("report");
    std::fs::write(d.join("main.gm"), MESSY).unwrap();
    let (ok, _, err) = gamma(&["lint", "main.gm"], &d);
    assert!(ok, "warnings alone do not fail: {}", err);
    for rule in ["unused-import", "unused-variable", "bool-comparison", "collapsible-else-if", "double-negation"] {
        assert!(err.contains(&format!("warning[{}]", rule)), "no {} in:\n{}", rule, err);
    }
    assert!(err.contains("gamma lint: 6 warnings"), "{}", err);
}

#[test]
fn fix_applies_every_fix_and_keeps_the_program_working() {
    let d = dir("fix");
    std::fs::write(d.join("main.gm"), MESSY).unwrap();
    let (ok, _, err) = gamma(&["lint", "--fix", "main.gm"], &d);
    assert!(ok, "{}", err);
    assert!(err.contains("gamma: applied 6 fixes in 1 file"), "{}", err);
    assert!(!err.contains("warning"), "{}", err);
    assert_eq!(std::fs::read_to_string(d.join("main.gm")).unwrap(), TIDY);
    let (ok, out, err) = gamma(&["run", "main.gm"], &d);
    assert!(ok, "{}", err);
    assert_eq!(out, "hi\n3\n");
    // A second run has nothing left to do.
    let (ok, _, err) = gamma(&["lint", "--fix", "main.gm"], &d);
    assert!(ok && err.is_empty(), "{}", err);
}

#[test]
fn collapsible_else_if_fix_keeps_multi_line_raw_strings() {
    let d = dir("raw_string_else");
    let src = "\
f :: fn(a: bool, c: bool) -> string {
    if a {
        return \"a\"
    } else {
        if c {
            return `line1
            line2`
        }
    }
    return \"z\"
}

main :: fn() needs Out {
    print(\"{}\\n\", f(false, true))
}
";
    std::fs::write(d.join("main.gm"), src).unwrap();
    let (_, _, err) = gamma(&["lint", "--fix", "main.gm"], &d);
    assert!(err.contains("collapsible-else-if"), "{}", err);
    assert_eq!(std::fs::read_to_string(d.join("main.gm")).unwrap(), src);
}

#[test]
fn fix_leaves_out_fixes_that_break_the_program() {
    // `::` evaluates a call of a pure function with constant arguments at compile
    // time, which a foreign function cannot be, so the prefer-immutable fix of `r`
    // is undone and reported.
    let d = dir("refused");
    let src = "\
libc :: foreign \"c\" {
    abs :: fn(n: i32) -> i32 needs not
}

main :: fn() needs Out {
    r := libc.abs(i32(-4))
    unused := r
    print(\"{}\\n\", r)
}
";
    std::fs::write(d.join("main.gm"), src).unwrap();
    let (ok, _, err) = gamma(&["lint", "--fix", "--warn=prefer-immutable", "main.gm"], &d);
    assert!(ok, "{}", err);
    let fixed = std::fs::read_to_string(d.join("main.gm")).unwrap();
    assert_eq!(fixed, src.replace("unused := r", "_ = r"));
    assert!(err.contains("1 lint fix left the program failing to compile, so it was not applied"), "{}", err);
    assert!(err.contains("applied 1 fix in 1 file") && err.contains("warning[prefer-immutable]"), "{}", err);
}

#[test]
fn deny_fails_the_command() {
    let d = dir("deny");
    std::fs::write(d.join("main.gm"), MESSY).unwrap();
    let (ok, _, err) = gamma(&["lint", "--deny=unused-variable", "main.gm"], &d);
    assert!(!ok);
    assert!(err.contains("error[unused-variable]") && err.contains("warning[unused-import]"), "{}", err);
    assert!(err.contains("gamma lint: 1 error and 5 warnings"), "{}", err);
    let (ok, _, err) = gamma(&["lint", "-D", "main.gm"], &d);
    assert!(!ok);
    assert!(err.contains("gamma lint: 6 errors"), "{}", err);
    let (ok, _, err) = gamma(&["lint", "--allow=all", "main.gm"], &d);
    assert!(ok && err.is_empty(), "{}", err);
    let (ok, _, err) = gamma(&["lint", "--allow=no-such-rule", "main.gm"], &d);
    assert!(!ok && err.contains("unknown lint rule `no-such-rule`"), "{}", err);
}

#[test]
fn a_program_that_does_not_compile_gets_its_errors() {
    let d = dir("broken");
    std::fs::write(d.join("main.gm"), "main :: fn() {\n    unused := 5\n    x: i32 = true\n}\n").unwrap();
    let (ok, _, err) = gamma(&["lint", "main.gm"], &d);
    assert!(!ok);
    assert!(err.contains("error") && !err.contains("warning["), "{}", err);
}

#[test]
fn manifest_lint_table_sets_levels() {
    let d = dir("manifest");
    std::fs::write(
        d.join("gamma.toml"),
        "[package]\nname = \"tidy\"\nversion = \"0.1.0\"\n\n[lint]\nunused-variable = \"allow\"\nbool-comparison = \"deny\"\n",
    )
    .unwrap();
    std::fs::write(d.join("main.gm"), MESSY).unwrap();
    let (ok, _, err) = gamma(&["lint", "."], &d);
    assert!(!ok);
    assert!(!err.contains("unused-variable"), "{}", err);
    assert!(err.contains("error[bool-comparison]"), "{}", err);
    // The command line comes after the manifest.
    let (ok, _, err) = gamma(&["lint", "--warn=bool-comparison", "."], &d);
    assert!(ok, "{}", err);
    assert!(err.contains("warning[bool-comparison]"), "{}", err);

    std::fs::write(
        d.join("gamma.toml"),
        "[package]\nname = \"tidy\"\nversion = \"0.1.0\"\n\n[lint]\nunused-thing = \"allow\"\n",
    )
    .unwrap();
    let (ok, _, err) = gamma(&["lint", "."], &d);
    assert!(!ok && err.contains("unused-thing"), "{}", err);
}

#[test]
fn allow_attribute_silences_a_declaration() {
    let d = dir("allow");
    let src = "\
#allow(\"unused-declaration\")
spare :: fn() -> i32 => 1

#allow(\"all\")
helper :: fn(x: i32) -> i32 => 2

main :: fn() needs Out {
    print(\"{}\\n\", helper(1))
}
";
    std::fs::write(d.join("main.gm"), src).unwrap();
    let (ok, _, err) = gamma(&["lint", "main.gm"], &d);
    assert!(ok && err.is_empty(), "{}", err);
    std::fs::write(d.join("main.gm"), src.replace("unused-declaration", "unused-decl")).unwrap();
    // Which names are rules is the tool's concern: the program compiles, and the tool
    // reports the name it does not know.
    let (ok, _, err) = gamma(&["check", "main.gm"], &d);
    assert!(ok && err.is_empty(), "{}", err);
    let (ok, _, err) = gamma(&["lint", "main.gm"], &d);
    assert!(!ok && err.contains("unknown lint rule `unused-decl`"), "{}", err);
}

#[test]
fn json_output_names_the_rule() {
    let d = dir("json");
    std::fs::write(d.join("main.gm"), MESSY).unwrap();
    let (_, _, err) = gamma(&["lint", "--diag-format=json", "main.gm"], &d);
    let diags: Vec<Value> = err.lines().map(|l| serde_json::from_str(l).expect(l)).collect();
    assert_eq!(diags.len(), 6, "{}", err);
    assert!(diags.iter().all(|d| d["severity"] == "warning" && d["code"].is_string()));
    let unused = diags.iter().find(|d| d["code"] == "unused-variable").unwrap();
    assert_eq!(unused["fixes"][0]["edits"][0]["text"], "_ =");
}

#[test]
fn rules_lists_every_rule() {
    let (ok, out, _) = gamma(&["lint", "--rules"], Path::new("."));
    assert!(ok);
    assert_eq!(out.lines().count(), gamma::lint::RULES.len());
    assert!(out.lines().any(|l| l.starts_with("prefer-immutable") && l.contains("allow")));
    assert!(out.lines().any(|l| l.starts_with("unused-variable") && l.contains("warn")));
}

#[test]
fn library_and_examples_lint_clean() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    // Every module of the standard library, and every example program.
    fn modules(d: &Path, out: &mut Vec<PathBuf>) {
        let mut has_gm = false;
        for e in std::fs::read_dir(d).unwrap() {
            let p = e.unwrap().path();
            if p.is_dir() {
                modules(&p, out);
            } else if p.extension().is_some_and(|x| x == "gm") {
                has_gm = true;
            }
        }
        if has_gm {
            out.push(d.to_path_buf());
        }
    }
    let mut inputs = vec![];
    modules(&root.join("lib/std"), &mut inputs);
    for e in std::fs::read_dir(root.join("examples")).unwrap() {
        let p = e.unwrap().path();
        if p.extension().is_some_and(|x| x == "gm") || p.join("main.gm").exists() {
            inputs.push(p);
        }
    }
    for input in inputs {
        let (ok, _, err) = gamma(&["lint", "--diag-format=short", input.to_str().unwrap()], root);
        assert!(ok && err.is_empty(), "`gamma lint {}`:\n{}", input.display(), err);
    }
}
