//! Tests of `gamma test` and `gamma bench`: which tests run (`--all`, `--filter`), what is
//! printed (`--show-output`, `--time`), `--jobs`, `--coverage`, and benchmarks.

use std::path::PathBuf;
use std::process::Command;

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

struct Out {
    ok: bool,
    stdout: String,
    stderr: String,
}

struct Package {
    root: PathBuf,
}

impl Package {
    fn new(name: &str, files: &[(&str, &str)]) -> Package {
        let root = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("testing_cli").join(name);
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(&root).unwrap();
        let root = gamma::source::canonicalize(&root).unwrap();
        std::fs::write(root.join("gamma.toml"), format!("[package]\nname = \"{}\"\nversion = \"0.1.0\"\n", name))
            .unwrap();
        for (rel, text) in files {
            let p = root.join(rel);
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, text).unwrap();
        }
        Package { root }
    }

    fn gamma(&self, args: &[&str]) -> Out {
        let out = Command::new(GAMMA)
            .args(args)
            .arg(&self.root)
            .env("GAMMA_CACHE_DIR", self.root.join("cache"))
            .output()
            .unwrap();
        Out {
            ok: out.status.success(),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        }
    }
}

fn have_cc() -> bool {
    Command::new("cc").arg("--version").output().is_ok_and(|o| o.status.success())
}

const MAIN: &str = r#"
sum :: fn(n: u64) -> u64 {
    s: u64 = 0
    i: u64 = 0
    while i < n {
        s += i
        i += 1
    }
    return s
}

never_called :: fn() -> i32 {
    return 3
}

main :: fn() needs Out {
    print("{}\n", sum(10))
}

test "sum" {
    print("hello from the test\n")
    expect_eq(sum(10), 45)
}

bench "sum of a thousand" {
    _ = @black_box(sum(@black_box(1000)))
}
"#;

const UTIL: &str = r#"
double :: fn(x: i32) -> i32 => x * 2

test "double" {
    expect_eq(double(4), 8)
}

bench "double" {
    _ = @black_box(double(@black_box(21)))
}
"#;

fn package() -> Vec<(&'static str, &'static str)> {
    vec![("main.gm", MAIN), ("util/util.gm", UTIL), ("target/ignored.gm", "this is not Gamma")]
}

#[test]
fn only_imported_modules_run_without_all() {
    let p = Package::new("imported", &package());
    let out = p.gamma(&["test"]);
    assert!(out.ok, "{}{}", out.stdout, out.stderr);
    assert_eq!(out.stdout, "test \"sum\" ... ok\n1 passed, 0 failed\n");
}

#[test]
fn all_runs_the_tests_of_every_module_of_the_package() {
    let p = Package::new("all", &package());
    let out = p.gamma(&["test", "--all"]);
    assert!(out.ok, "{}{}", out.stdout, out.stderr);
    assert_eq!(out.stdout, "test \"sum\" ... ok\ntest \"double\" in util ... ok\n2 passed, 0 failed\n");
}

#[test]
fn show_output_prints_what_passing_tests_wrote() {
    let p = Package::new("show_output", &package());
    let out = p.gamma(&["test", "--show-output"]);
    assert_eq!(out.stdout, "test \"sum\" ... ok\n    hello from the test\n1 passed, 0 failed\n");
}

#[test]
fn time_is_opt_in() {
    let p = Package::new("time", &package());
    let out = p.gamma(&["test", "--time"]);
    assert!(out.ok, "{}{}", out.stdout, out.stderr);
    let first = out.stdout.lines().next().unwrap();
    assert!(first.starts_with("test \"sum\" ... ok ("), "{}", first);
    let last = out.stdout.lines().last().unwrap();
    assert!(last.starts_with("1 passed, 0 failed (finished in "), "{}", last);
}

#[test]
fn parallel_runs_print_in_order() {
    let mut main = String::from("main :: fn() {}\n");
    for i in 0..12 {
        main.push_str(&format!("test \"t{:02}\" {{\n    expect_eq({}, {})\n}}\n", i, i, if i == 7 { 8 } else { i }));
    }
    let p = Package::new("jobs", &[("main.gm", &main)]);
    let sequential = p.gamma(&["test"]);
    let parallel = p.gamma(&["test", "--jobs=4"]);
    assert!(!sequential.ok);
    assert!(!parallel.ok);
    assert_eq!(parallel.stdout, sequential.stdout);
    assert!(parallel.stdout.starts_with("test \"t00\" ... ok\ntest \"t01\" ... ok\n"), "{}", parallel.stdout);
    assert!(parallel.stdout.ends_with("11 passed, 1 failed\n"), "{}", parallel.stdout);
}

#[test]
fn coverage_counts_the_lines_the_tests_run() {
    let p = Package::new("coverage", &package());
    let lcov = p.root.join("lcov.info");
    let flag = format!("--coverage={}", lcov.display());
    let out = p.gamma(&["test", "--all", &flag]);
    assert!(out.ok, "{}{}", out.stdout, out.stderr);
    // `never_called`, and `main`, which no test calls, are the lines left out.
    assert!(out.stdout.contains("main.gm"), "{}", out.stdout);
    assert!(out.stdout.contains("util/util.gm"), "{}", out.stdout);
    assert!(out.stdout.contains("total"), "{}", out.stdout);
    let info = std::fs::read_to_string(&lcov).unwrap();
    assert!(info.contains("SF:") && info.contains("end_of_record"), "{}", info);
    let line_of = |needle: &str| MAIN.lines().position(|l| l.contains(needle)).unwrap() + 1;
    assert!(info.contains(&format!("DA:{},0\n", line_of("return 3"))), "{}", info);
    assert!(info.contains(&format!("DA:{},1\n", line_of("s: u64 = 0"))), "{}", info);
    // Without a test run, nothing in the standard library counts.
    assert!(!info.contains("prelude"), "{}", info);
}

#[test]
fn a_tracefile_that_cannot_be_written_fails_the_run() {
    let p = Package::new("coverage_unwritable", &package());
    let flag = format!("--coverage={}", p.root.join("missing-dir").join("lcov.info").display());
    let out = p.gamma(&["test", "--all", &flag]);
    assert!(!out.ok, "{}", out.stdout);
    assert!(out.stderr.contains("cannot write"), "{}", out.stderr);
}

#[test]
fn coverage_needs_the_interpreter() {
    if !have_cc() {
        return;
    }
    let p = Package::new("coverage_native", &package());
    let out = p.gamma(&["test", "--backend=c", "--coverage"]);
    assert!(!out.ok);
    assert!(out.stderr.contains("needs the interpreter"), "{}", out.stderr);
}

#[test]
fn duplicate_names_are_per_kind() {
    let main = "main :: fn() {}\ntest \"a\" {}\nbench \"a\" {}\nbench \"a\" {}\n";
    let p = Package::new("duplicate", &[("main.gm", main)]);
    let out = p.gamma(&["test"]);
    assert!(!out.ok);
    assert!(out.stderr.contains("the benchmark \"a\" is declared twice"), "{}", out.stderr);
    assert!(!out.stderr.contains("the test \"a\""), "{}", out.stderr);
}

#[test]
fn benchmarks_report_the_time_per_run() {
    let p = Package::new("bench", &package());
    let out = p.gamma(&["bench", "--backend=interp", "--all"]);
    assert!(out.ok, "{}{}", out.stdout, out.stderr);
    let lines: Vec<&str> = out.stdout.lines().collect();
    assert_eq!(lines.len(), 3, "{}", out.stdout);
    assert!(lines[0].starts_with("bench \"sum of a thousand\" ... "), "{}", lines[0]);
    assert!(lines[0].contains("/run (± ") && lines[0].contains("20 samples of "), "{}", lines[0]);
    assert!(lines[1].starts_with("bench \"double\" in util ... "), "{}", lines[1]);
    assert_eq!(lines[2], "2 run, 0 failed");
    let filtered = p.gamma(&["bench", "--backend=interp", "--all", "--filter=double"]);
    assert!(filtered.stdout.ends_with("1 run, 0 failed, 1 filtered out\n"), "{}", filtered.stdout);
}

#[test]
fn benchmarks_run_natively_by_default() {
    if !have_cc() {
        return;
    }
    let p = Package::new("bench_native", &package());
    let out = p.gamma(&["bench"]);
    assert!(out.ok, "{}{}", out.stdout, out.stderr);
    assert!(out.stdout.starts_with("bench \"sum of a thousand\" ... "), "{}", out.stdout);
    assert!(out.stdout.ends_with("1 run, 0 failed\n"), "{}", out.stdout);
}

#[test]
fn a_failing_benchmark_fails_the_command() {
    let main = "main :: fn() {}\nbench \"broken\" {\n    expect(false)\n}\n";
    let p = Package::new("bench_fails", &[("main.gm", main)]);
    let out = p.gamma(&["bench", "--backend=interp"]);
    assert!(!out.ok);
    assert!(out.stdout.contains("bench \"broken\" ... FAILED"), "{}", out.stdout);
    assert!(out.stdout.ends_with("0 run, 1 failed\n"), "{}", out.stdout);
}

#[test]
fn gamma_test_checks_benchmarks_without_running_them() {
    let main = "main :: fn() {}\nbench \"typo\" {\n    no_such_function()\n}\n";
    let p = Package::new("bench_checked", &[("main.gm", main)]);
    let out = p.gamma(&["test"]);
    assert!(!out.ok);
    assert!(out.stderr.contains("no_such_function"), "{}", out.stderr);
}

fn usage_error(args: &[&str]) -> String {
    let out = Command::new(GAMMA).args(args).output().unwrap();
    assert_eq!(out.status.code(), Some(2), "{:?}", args);
    String::from_utf8_lossy(&out.stderr).into_owned()
}

#[test]
fn invalid_option_values_are_named() {
    let err = usage_error(&["run", "--mode=bogus", "x.gm"]);
    assert!(err.contains("invalid value `bogus` for --mode (expected debug, checked, safe or fast)"), "{}", err);
    let err = usage_error(&["run", "--backend=bogus", "x.gm"]);
    assert!(err.contains("invalid value `bogus` for --backend"), "{}", err);
    let err = usage_error(&["check", "--diag-format=bogus", "x.gm"]);
    assert!(err.contains("invalid value `bogus` for --diag-format"), "{}", err);
}

#[test]
fn lsp_rejects_unknown_options() {
    let err = usage_error(&["lsp", "--bogus"]);
    assert!(err.contains("unknown option `--bogus`"), "{}", err);
}
