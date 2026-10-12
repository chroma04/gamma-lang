//! Very long expressions compile and run: the compiler's passes recurse over the syntax
//! tree, so a long operator chain is as deep as it is long.

use std::path::PathBuf;
use std::process::Command;

/// Runs a program whose `main` prints a sum of `n` terms `term` with the interpreter.
fn run_sum(name: &str, n: usize, term: &str) -> String {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("long-expressions");
    std::fs::create_dir_all(&dir).unwrap();
    let src = dir.join(format!("{name}.gm"));
    let sum = vec![term; n].join(" + ");
    std::fs::write(
        &src,
        format!("main :: fn() \\ {{Out}} {{\n    y := 1\n    x := {sum}\n    print(\"{{}} {{}}\\n\", (x, y))\n}}\n"),
    )
    .unwrap();
    let out = Command::new(env!("CARGO_BIN_EXE_gamma")).arg("run").arg(&src).output().unwrap();
    assert!(out.status.success(), "gamma run failed:\n{}", String::from_utf8_lossy(&out.stderr));
    String::from_utf8_lossy(&out.stdout).into_owned()
}

#[test]
fn long_constant_chain() {
    assert_eq!(run_sum("constant", 20000, "1"), "20000 1\n");
}

#[test]
fn long_run_time_chain() {
    assert_eq!(run_sum("run_time", 20000, "y"), "20000 1\n");
}
