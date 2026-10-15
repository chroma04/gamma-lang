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
        format!("main :: fn() \\ {{Out}} {{\n    y := 1\n    x := {sum}\n    print(\"{{}} {{}}\\n\", x, y)\n}}\n"),
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

/// Runs `gamma <command>` on a program and returns its exit status (None if a signal ended
/// it, as a stack overflow does) and standard error.
fn gamma(command: &str, name: &str, src: &str) -> (Option<i32>, String) {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("long-expressions");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(format!("{name}.gm"));
    std::fs::write(&path, src).unwrap();
    let out = Command::new(env!("CARGO_BIN_EXE_gamma")).arg(command).arg(&path).output().unwrap();
    (out.status.code(), String::from_utf8_lossy(&out.stderr).chars().take(600).collect())
}

/// An empty `main` and a function `g` whose body is `x := <operand><link>…` with `n` links.
fn chain_program(prelude: &str, params: &str, operand: &str, link: &str, n: usize) -> String {
    format!("main :: fn() {{}}\n{prelude}\ng :: fn({params}) {{\n    x := {operand}{}\n}}\n", link.repeat(n))
}

/// Type-checking a long chain succeeds in time linear in its length.
fn check_ok(name: &str, src: &str) {
    let (code, err) = gamma("check", name, src);
    assert_eq!(code, Some(0), "gamma check failed:\n{err}");
}

/// A chain that is too long or ill-typed is reported as an error, never a crash.
fn check_error(command: &str, name: &str, src: &str, message: &str) {
    let (code, err) = gamma(command, name, src);
    assert_eq!(code, Some(1), "expected an error exit from `{command}`, got {code:?}:\n{err}");
    assert!(err.contains(message), "missing `{message}` in:\n{err}");
}

const NODE: &str = "N :: struct {\n    c: ^N\n    v: i32\n}\n";

#[test]
fn long_field_chain() {
    check_ok("field_chain", &chain_program(NODE, "a: N", "a", ".c", 20000));
}

#[test]
fn long_method_chain() {
    let prelude = "N :: struct { v: i32 }\nf :: fn(n: N) -> N { return n }\n";
    check_ok("method_chain", &chain_program(prelude, "a: N", "a", ".f()", 20000));
}

#[test]
fn long_builtin_method_chain() {
    check_ok("builtin_chain", &chain_program("", "a: i32", "a", ".abs()", 20000));
}

#[test]
fn long_ill_typed_chains_are_errors() {
    // Each chain fails at its first link; the rest must not crash any pass.
    let cases = [
        ("bad_index", "[0]", "cannot index"),
        ("bad_call", "(1)", "cannot call"),
        ("bad_deref", "^", "dereferences a pointer"),
        ("bad_unwrap", "?", "unwraps an optional"),
    ];
    for (name, link, message) in cases {
        check_error("check", name, &chain_program("", "a: i32", "a", link, 50000), message);
    }
}

#[test]
fn postfix_chain_limit_is_an_error() {
    for (name, link) in [("limit_call", "(1)"), ("limit_index", "[0]"), ("limit_field", ".c"), ("limit_deref", "^")] {
        let src = chain_program(NODE, "a: N", "a", link, 150_000);
        check_error("check", name, &src, "nested too deeply");
        check_error("run", name, &src, "nested too deeply");
    }
}
