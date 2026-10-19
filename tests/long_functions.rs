//! Functions with thousands of statements compile through C in time linear in their
//! length: the C back end moves runs of their statements into helper functions that share
//! the frame (see `src/cgen/outline.rs`). These tests build such functions with every kind
//! of exit that can leave a run (`break`, `continue`, `return`, `try`, an `esc` clause's
//! `break`, a `with … on` body ending) and compare the native program with the interpreter.

use std::path::PathBuf;
use std::process::Command;

/// Whether a C compiler is available (the tests are skipped otherwise, or with `GAMMA_TEST_C=0`).
fn have_cc() -> bool {
    if std::env::var("GAMMA_TEST_C").is_ok_and(|v| v == "0") {
        return false;
    }
    gamma::toolchain::Toolchain::from_env()
        .is_ok_and(|t| t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()))
}

fn dir() -> PathBuf {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("long-functions");
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// `stmt` on `n` lines indented by `indent` levels.
fn rep(stmt: &str, n: usize, indent: usize) -> String {
    format!("{}{stmt}\n", "    ".repeat(indent)).repeat(n)
}

/// A program whose functions each have `n` statements per run between their exits.
fn program(n: usize) -> String {
    let mut p = String::from(
        "Big :: struct { a: i64, b: i64, c: i64, d: i64 }
Fail :: effect {
    fail: esc(msg: string) -> never
}
Ids :: effect {
    next_id: fn() -> i64
}
Ask :: effect {
    ask: ctl(q: i64) -> i64
}
E :: union { Bad: i64 }

check :: fn(k: i64) -> E!i64 {
    if k > 2 { return !.Bad(k) }
    return k
}
",
    );
    // `continue` and `break` in a long loop body, `return` past a `defer`.
    p += "loops :: fn(n: i64) -> i64 needs Out {\n    s := 0\n    defer print(\"loops done {}\\n\", n)\n    i := 0\n";
    p += "    while i < n {\n        i += 1\n        defer print(\"iteration {}\\n\", i)\n";
    p += &rep("s = s + i % 3", n, 2);
    p += "        if i is 2 { continue }\n";
    p += &rep("s = s + i % 5", n, 2);
    p += "        if i is 4 { break }\n    }\n";
    p += &rep("s = s - i % 7", n, 1);
    p += "    if n is 3 { return s }\n";
    p += &rep("s = s + n % 4", n, 1);
    p += "    return s + 1\n}\n";
    // `try` past an `errdefer`.
    p += "results :: fn(k: i64) -> E!i64 needs Out {\n    s := 0\n";
    p += "    errdefer print(\"errdefer {}\\n\", k)\n    defer print(\"defer {}\\n\", k)\n";
    p += &rep("s = s + k % 3", n, 1);
    p += "    x := try check(k)\n";
    p += &rep("s = s + x % 5", n, 1);
    p += "    y := try check(k + 1)\n";
    p += &rep("s = s + y % 2", n, 1);
    p += "    return s + x + y\n}\n";
    // Performs in the body of a `with`, and an `esc` clause leaving it.
    p += "handlers :: fn(k: i64) -> i64 needs Out {\n    counter := 0\n";
    p += "    ids :: handler Ids {\n        next_id() => {\n            counter += 1\n            counter\n        }\n    }\n";
    p += "    stop :: handler Fail {\n        fail(msg) => {\n            print(\"fail: {}\\n\", msg)\n            break\n        }\n    }\n";
    p += "    s := 0\n    with stop {\n        with ids {\n";
    p += &rep("s = s + Ids.next_id() % 3", n, 3);
    p += "            if k is 1 { Fail.fail(\"one\") }\n";
    p += &rep("s = s + Ids.next_id() % 5", n, 3);
    p += "        }\n    }\n    return s + counter\n}\n";
    // A large result returned from the middle, built in the caller's destination.
    p += "big :: fn(k: i64) -> Big {\n    s := 0\n";
    p += &rep("s = s + k % 3", n, 1);
    p += "    if k is 1 { return Big(a: s, b: s + 1, c: s + 2, d: s + 3) }\n";
    p += &rep("s = s + k % 5", n, 1);
    p += "    return Big(a: s, b: k, c: 0, d: 1)\n}\n";
    // A large parameter read in place by a pure function.
    p += "pure_sum :: fn(b: Big) -> i64 {\n    s := 0\n";
    p += &rep("s = s + b.a % 3", n, 1);
    p += &rep("s = s + b.d % 5", n, 1);
    p += "    return s\n}\n";
    // The body of a `with … on`, which runs on a fiber, suspending at each `ctl` perform.
    p += "fiber :: fn() -> i64 needs Out {\n    stack: [65536]u8 = ---\n    n := 0\n";
    p += "    answerer :: handler Ask {\n        ask(q, k) => {\n            n += q\n            resume k(n)\n        }\n    }\n";
    p += "    total := with answerer on stack[..] {\n        s := 0\n";
    p += &rep("s = s + Ask.ask(1) % 3", n, 2);
    p += &rep("s = s + Ask.ask(2) % 5", n, 2);
    p += "        s\n    }\n    return total + n\n}\n";
    p += "main :: fn() needs Out {
    print(\"{} {} {}\\n\", loops(3), loops(5), loops(1))
    for k in 1..5 {
        print(\"{}\\n\", results(k))
    }
    print(\"{} {}\\n\", handlers(1), handlers(2))
    print(\"{} {}\\n\", big(1), big(2))
    print(\"{}\\n\", pure_sum(Big(a: 7, b: 8, c: 9, d: 10)))
    print(\"{}\\n\", fiber())
}
";
    p
}

/// Runs `gamma` with `args` and `env` on the program; returns its exit code and output.
fn gamma(args: &[&str], env: &[(&str, &str)], path: &std::path::Path) -> (Option<i32>, String) {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.args(args).arg(path).env("GAMMA_CACHE_DIR", dir().join("cache"));
    for (k, v) in env {
        cmd.env(k, v);
    }
    let out = cmd.output().unwrap();
    let stderr = String::from_utf8_lossy(&out.stderr);
    assert!(!stderr.contains("C compiler"), "the C compiler failed:\n{}", &stderr[..stderr.len().min(800)]);
    (out.status.code(), String::from_utf8_lossy(&out.stdout).into_owned())
}

#[test]
fn long_functions_run_as_in_the_interpreter() {
    if !have_cc() {
        eprintln!("skipped: no C compiler");
        return;
    }
    let path = dir().join("exits.gm");
    std::fs::write(&path, program(300)).unwrap();
    let interp = gamma(&["run"], &[], &path);
    assert_eq!(interp.0, Some(0));
    for line in ["iteration 2\n", "loops done 5\n", "errdefer 2\n", "errdefer 3\n", "fail: one\n"] {
        assert!(interp.1.contains(line), "missing {line:?} in:\n{}", interp.1);
    }
    // Debug builds keep every local in the frame anyway; the others move them there.
    for mode in ["--mode=debug", "--mode=checked", "--mode=fast"] {
        let native = gamma(&["run", "--backend=c", mode], &[], &path);
        assert_eq!(native, interp, "{mode} differs from the interpreter");
    }
    // Split into several C files, the helpers must go with the functions that call them.
    let native = gamma(&["run", "--backend=c"], &[("GAMMA_C_UNITS", "3")], &path);
    assert_eq!(native, interp, "the program split into units differs from the interpreter");
}

#[test]
fn long_functions_are_split_into_helpers() {
    if !have_cc() {
        return;
    }
    let path = dir().join("split.gm");
    std::fs::write(&path, program(300)).unwrap();
    let exe = dir().join("split");
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .args(["build", "--mode=fast"])
        .arg(&path)
        .arg("-o")
        .arg(&exe)
        .env("GAMMA_CACHE_DIR", dir().join("cache"))
        .env("GAMMA_C_UNITS", "1")
        .output()
        .unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let c = std::fs::read_to_string(exe.with_extension("c")).unwrap();
    assert!(c.contains("static GM_NOINLINE int gm_c"), "no helpers in the C code");
    // No function is long enough left to need Clang's `optnone`.
    let long = c.lines().find(|l| l.starts_with("static ") && l.contains("GM_LONG_FN"));
    assert!(long.is_none(), "a function is still marked GM_LONG_FN: {long:?}");
}
