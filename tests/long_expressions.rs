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
        format!("main :: fn() needs Out {{\n    y := 1\n    x := {sum}\n    print(\"{{}} {{}}\\n\", x, y)\n}}\n"),
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
        check_error("check", name, &src, "more than 100000 postfix operations in a row");
        check_error("run", name, &src, "more than 100000 postfix operations in a row");
    }
}

// --- the C back end ---------------------------------------------------------------------------
//
// A chain of operations is emitted as an accumulator updated by one block per link once it is
// longer than a few hundred links (see `src/cgen/chain.rs`); without that, the C compilers take
// minutes on chains of tens of thousands of links. These tests build chains well past the
// threshold and compare the native program with the interpreter. The longest ones take a few
// seconds with gcc at the default optimization level.

/// Whether a C compiler is available (the tests are skipped otherwise, or with `GAMMA_TEST_C=0`).
fn have_cc() -> bool {
    if std::env::var("GAMMA_TEST_C").is_ok_and(|v| v == "0") {
        return false;
    }
    gamma::toolchain::Toolchain::from_env()
        .is_ok_and(|t| t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()))
}

/// Runs `src` with the interpreter and through the C back end; returns the exit code and
/// standard output of each.
fn run_both(name: &str, src: &str) -> [(Option<i32>, String); 2] {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("long-expressions");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(format!("{name}.gm"));
    std::fs::write(&path, src).unwrap();
    ["interp", "c"].map(|backend| {
        let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
            .arg("run")
            .arg(format!("--backend={backend}"))
            .arg(&path)
            .env("GAMMA_CACHE_DIR", dir.join("cache"))
            .output()
            .unwrap();
        let stderr = String::from_utf8_lossy(&out.stderr);
        assert!(!stderr.contains("C compiler"), "the C compiler failed:\n{}", &stderr[..stderr.len().min(800)]);
        (out.status.code(), String::from_utf8_lossy(&out.stdout).into_owned())
    })
}

fn same_output(name: &str, src: &str, expected: &str) {
    if !have_cc() {
        eprintln!("skipped: no C compiler");
        return;
    }
    let [interp, native] = run_both(name, src);
    assert_eq!(interp.1, expected, "interpreter output");
    assert_eq!(native, interp, "native run differs from the interpreter");
}

fn main_with(prelude: &str, body: &str) -> String {
    format!("{prelude}main :: fn() needs Out {{\n    a := -5\n    y := 1\n{body}    print(\"{{}}\\n\", x)\n}}\n")
}

#[test]
fn native_long_method_chain() {
    // The longest chain the parser accepts: 100,000 links (`.abs` and `()` count one each).
    let src = main_with("", &format!("    x := a{}\n", ".abs()".repeat(50_000)));
    same_output("native_abs", &src, "5\n");
}

#[test]
fn native_long_sum() {
    let sum = vec!["y"; 40_000].join(" + ");
    same_output("native_sum", &main_with("", &format!("    x := {sum}\n")), "40000\n");
}

#[test]
fn native_long_user_method_chain() {
    let prelude = "N :: struct { v: i32 }\nf :: fn(n: N) -> N { return N(v: n.v + 1) }\n";
    let src = main_with(prelude, &("    n := N(v: 1)\n    x := n".to_string() + &".f()".repeat(5_000) + ".v\n"));
    same_output("native_user_chain", &src, "5001\n");
}

#[test]
fn native_chain_keeps_evaluation_order() {
    // Each term has a visible effect; the order of the output is the order of evaluation.
    let prelude = "f :: fn(k: i64) -> i64 needs Out {\n    print(\"{} \", k)\n    return k\n}\n";
    let n = 600;
    let sum = (1..=n).map(|k| format!("f({k})")).collect::<Vec<_>>().join(" + ");
    let expected: String = (1..=n).map(|k| format!("{k} ")).collect::<String>() + &format!("{}\n", n * (n + 1) / 2);
    same_output("native_order", &main_with(prelude, &format!("    x := {sum}\n")), &expected);
}

#[test]
fn native_chain_short_circuits() {
    // The right operands of `and` are evaluated only while the chain so far is true.
    let prelude = "t :: fn(k: i64) -> bool needs Out {\n    print(\"{} \", k)\n    return k < 400\n}\n";
    let chain = (1..=600).map(|k| format!("t({k})")).collect::<Vec<_>>().join(" and ");
    let expected: String = (1..=400).map(|k| format!("{k} ")).collect::<String>() + "false\n";
    same_output("native_and", &main_with(prelude, &format!("    x := {chain}\n")), &expected);
    // `or` stops at the first true operand.
    let prelude = "t :: fn(k: i64) -> bool needs Out {\n    print(\"{} \", k)\n    return k >= 300\n}\n";
    let chain = (1..=600).map(|k| format!("t({k})")).collect::<Vec<_>>().join(" or ");
    let expected: String = (1..=300).map(|k| format!("{k} ")).collect::<String>() + "true\n";
    same_output("native_or", &main_with(prelude, &format!("    x := {chain}\n")), &expected);
}

#[test]
fn native_chain_changes_type() {
    // A comparison ends a sum: the chain's accumulator changes from a number to a bool.
    let sum = vec!["y"; 500].join(" + ");
    same_output("native_type", &main_with("", &format!("    x := {sum} > 499\n")), "true\n");
}

#[test]
fn native_chain_traps_where_the_interpreter_does() {
    if !have_cc() {
        return;
    }
    // 300 terms of 1 overflow an `i8` at the 128th.
    let sum = vec!["z"; 300].join(" + ");
    let src = main_with("", &format!("    z: i8 = 1\n    x := {sum}\n"));
    let [interp, native] = run_both("native_trap", &src);
    assert_eq!(interp.0, native.0, "exit codes");
    assert_ne!(interp.0, Some(0));
    assert_eq!(interp.1, native.1);
}

#[test]
fn native_long_function_is_marked_for_clang() {
    // Clang from LLVM 19 on takes time that grows about with the cube of the length of a
    // chain of checked operations, so a function this long is marked `GM_LONG_FN`, which
    // compiles it without optimization there (see `runtime/gm_rt.h`).
    if !have_cc() {
        return;
    }
    let src = main_with("", &format!("    x := a{}\n", ".abs()".repeat(3_000)));
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("long-expressions");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("long_fn.gm");
    std::fs::write(&path, &src).unwrap();
    let exe = dir.join("long_fn");
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .arg("build")
        .arg(&path)
        .arg("-o")
        .arg(&exe)
        .env("GAMMA_CACHE_DIR", dir.join("cache"))
        // One C file, whatever the environment asks.
        .env("GAMMA_C_UNITS", "1")
        .output()
        .unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let c = std::fs::read_to_string(exe.with_extension("c")).unwrap();
    assert!(c.contains("static GM_LONG_FN "), "the function of the chain is not marked");
}
