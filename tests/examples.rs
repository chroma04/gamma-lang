//! Runs the programs in `examples/` and checks what they print.
//!
//! Each file `tests/examples/<case>.run` describes one run of one example, with
//! one `key: value` setting per line (`#` starts a comment line):
//!
//! - `example: wordfreq.gm` names the example, a `.gm` file or a module
//!   directory, relative to `examples/`. Required.
//! - `command: run` (the default) runs the program with `gamma run`; `command: test`
//!   runs its `test` declarations with `gamma test`.
//! - `args: --top 3 data/words.txt` are the program's arguments, separated by
//!   spaces (there is no quoting).
//! - `stdin: data/words.txt` is the program's standard input (default: empty).
//! - `exit: N` is the expected exit status (default 0).
//! - `backend: interp` runs the case only on the interpreter. By default it also
//!   runs through the C back end (`--backend=c`), where a C compiler is available
//!   and `GAMMA_TEST_C` is not 0, and both must give the same result.
//!
//! The expected standard output is `tests/examples/<case>.stdout` (empty if the
//! file does not exist). Programs run with `examples/` as their working directory,
//! so the paths in `args` and `stdin` are the ones a reader would type there.
//!
//! The tests also check that every example has at least one case, and that the
//! examples are formatted as `gamma fmt` formats them.
//!
//! Set `BLESS=1` to rewrite the `.stdout` files from the interpreter's output, and
//! `GAMMA_TEST_FILTER=substring` to run only the cases whose name contains it.

use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

struct Case {
    name: String,
    example: String,
    command: String,
    args: Vec<String>,
    stdin: Option<String>,
    exit: i32,
    c_backend: bool,
}

fn root() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

fn examples_dir() -> PathBuf {
    root().join("examples")
}

fn cases_dir() -> PathBuf {
    root().join("tests/examples")
}

fn parse_case(path: &Path) -> Result<Case, String> {
    let text = fs::read_to_string(path).map_err(|e| format!("{}: {}", path.display(), e))?;
    let name = path.file_stem().unwrap().to_string_lossy().into_owned();
    let mut case = Case {
        name,
        example: String::new(),
        command: "run".into(),
        args: vec![],
        stdin: None,
        exit: 0,
        c_backend: true,
    };
    for (i, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let bad = |what: &str| format!("{}:{}: {}", path.display(), i + 1, what);
        let (key, value) = line.split_once(':').ok_or_else(|| bad("expected `key: value`"))?;
        let value = value.trim();
        match key.trim() {
            "example" => case.example = value.to_string(),
            "command" if value == "run" || value == "test" => case.command = value.to_string(),
            "args" => case.args = value.split_whitespace().map(String::from).collect(),
            "stdin" => case.stdin = Some(value.to_string()),
            "exit" => case.exit = value.parse().map_err(|_| bad("`exit` takes a number"))?,
            "backend" if value == "interp" => case.c_backend = false,
            _ => return Err(bad("unknown setting")),
        }
    }
    if case.example.is_empty() {
        return Err(format!("{}: no `example:` line", path.display()));
    }
    if !examples_dir().join(&case.example).exists() {
        return Err(format!("{}: example {} does not exist", path.display(), case.example));
    }
    Ok(case)
}

fn cases() -> Vec<Case> {
    let mut paths: Vec<PathBuf> = fs::read_dir(cases_dir())
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "run"))
        .collect();
    paths.sort();
    paths.iter().map(|p| parse_case(p).unwrap_or_else(|e| panic!("{}", e))).collect()
}

/// Whether the C back end can be tested: a C compiler runs and `GAMMA_TEST_C` is not 0.
fn c_backend_available() -> bool {
    if std::env::var("GAMMA_TEST_C").is_ok_and(|v| v == "0") {
        return false;
    }
    gamma::toolchain::Toolchain::from_env()
        .is_ok_and(|t| t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()))
}

/// Runs `case`, through the C back end if `c` is set; returns (exit status, stdout, stderr).
fn run(case: &Case, c: bool) -> Result<(i32, String, String), String> {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.env("GAMMA_CACHE_DIR", Path::new(env!("CARGO_TARGET_TMPDIR")).join("gamma-cache"));
    cmd.current_dir(examples_dir());
    cmd.arg(&case.command);
    if c {
        cmd.arg("--backend=c");
    }
    cmd.arg(&case.example);
    cmd.args(&case.args);
    match &case.stdin {
        Some(f) => {
            let file = fs::File::open(examples_dir().join(f)).map_err(|e| format!("stdin {}: {}", f, e))?;
            cmd.stdin(file);
        }
        None => {
            cmd.stdin(Stdio::null());
        }
    }
    let out = cmd.output().map_err(|e| format!("cannot run gamma: {}", e))?;
    Ok((
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    ))
}

fn check(case: &Case, c: bool, bless: bool) -> Result<(), String> {
    let lane = if c { "[C back end]" } else { "[interpreter]" };
    let (code, stdout, stderr) = run(case, c)?;
    let golden = cases_dir().join(format!("{}.stdout", case.name));
    if bless {
        if stdout.is_empty() {
            let _ = fs::remove_file(&golden);
        } else {
            fs::write(&golden, &stdout).map_err(|e| format!("{}: {}", golden.display(), e))?;
        }
    }
    let expected = fs::read_to_string(&golden).unwrap_or_default();
    let mut problems = vec![];
    if code != case.exit {
        problems.push(format!("{} exit status {} (expected {})\nstderr:\n{}", lane, code, case.exit, stderr));
    }
    if expected != stdout {
        problems.push(format!("{} output differs from {}:\n{}", lane, golden.display(), diff(&expected, &stdout)));
    }
    if problems.is_empty() { Ok(()) } else { Err(problems.join("\n")) }
}

/// A minimal line diff: shows the first differing region with context.
fn diff(expected: &str, actual: &str) -> String {
    let e: Vec<&str> = expected.lines().collect();
    let a: Vec<&str> = actual.lines().collect();
    let first = e.iter().zip(&a).position(|(x, y)| x != y).unwrap_or(e.len().min(a.len()));
    let mut out = String::new();
    let start = first.saturating_sub(3);
    for (i, line) in e.iter().enumerate().skip(start).take(10) {
        out.push_str(&format!("  -{:4} {}\n", i + 1, line));
    }
    for (i, line) in a.iter().enumerate().skip(start).take(10) {
        out.push_str(&format!("  +{:4} {}\n", i + 1, line));
    }
    if e.len() != a.len() {
        out.push_str(&format!("  (expected {} lines, got {})\n", e.len(), a.len()));
    }
    out
}

#[test]
fn examples_run() {
    let filter = std::env::var("GAMMA_TEST_FILTER").ok();
    let bless = std::env::var("BLESS").is_ok_and(|v| v == "1");
    let with_c = c_backend_available();
    let cases: Vec<Case> =
        cases().into_iter().filter(|c| filter.as_ref().is_none_or(|f| c.name.contains(f.as_str()))).collect();
    let interp: Vec<(&Case, bool)> = cases.iter().map(|c| (c, false)).collect();
    let native: Vec<(&Case, bool)> = cases.iter().filter(|c| with_c && c.c_backend).map(|c| (c, true)).collect();
    let runs = interp.len() + native.len();
    let failures = std::sync::Mutex::new(Vec::new());
    if bless {
        // The C lane compares with the goldens the interpreter lane writes.
        run_all(&interp, true, &failures);
        run_all(&native, false, &failures);
    } else {
        run_all(&[interp, native].concat(), false, &failures);
    }
    let failures = failures.into_inner().unwrap();
    for f in &failures {
        eprintln!("{}\n", f);
    }
    eprintln!("examples: {} runs, {} failed", runs, failures.len());
    assert!(failures.is_empty(), "{} example run(s) failed", failures.len());
}

/// Runs `jobs` (a case and whether it goes through the C back end) in parallel.
fn run_all(jobs: &[(&Case, bool)], bless: bool, failures: &std::sync::Mutex<Vec<String>>) {
    let next = std::sync::atomic::AtomicUsize::new(0);
    let workers = std::thread::available_parallelism().map_or(1, |n| n.get());
    std::thread::scope(|s| {
        for _ in 0..workers {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(&(case, c)) = jobs.get(i) else { break };
                    if let Err(msg) = check(case, c, bless) {
                        failures.lock().unwrap().push(format!("FAIL {}\n{}", case.name, msg));
                    }
                }
            });
        }
    });
}

/// Every `.gm` file and module directory directly in `examples/` has a case.
#[test]
fn every_example_has_a_case() {
    let covered: BTreeSet<String> = cases().into_iter().map(|c| c.example.trim_end_matches('/').to_string()).collect();
    let mut missing = vec![];
    for entry in fs::read_dir(examples_dir()).unwrap() {
        let p = entry.unwrap().path();
        let name = p.file_name().unwrap().to_string_lossy().into_owned();
        let is_example = if p.is_dir() {
            fs::read_dir(&p).unwrap().any(|e| e.unwrap().path().extension().is_some_and(|x| x == "gm"))
                || p.join("gamma.toml").exists()
        } else {
            p.extension().is_some_and(|e| e == "gm")
        };
        if is_example && !covered.contains(&name) {
            missing.push(name);
        }
    }
    missing.sort();
    assert!(missing.is_empty(), "examples without a case in tests/examples/: {:?}", missing);
}

fn gm_files(dir: &Path, out: &mut Vec<PathBuf>) {
    for entry in fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            gm_files(&p, out);
        } else if p.extension().is_some_and(|e| e == "gm") {
            out.push(p);
        }
    }
}

#[test]
fn examples_are_formatted() {
    let mut files = vec![];
    gm_files(&examples_dir(), &mut files);
    files.sort();
    let mut unformatted = vec![];
    for path in files {
        let src = fs::read_to_string(&path).unwrap();
        match gamma::fmt::format_source(&src) {
            Ok(out) if out == src => {}
            _ => unformatted.push(path.strip_prefix(root()).unwrap().display().to_string()),
        }
    }
    assert!(unformatted.is_empty(), "run `gamma fmt examples`; not formatted: {:?}", unformatted);
}
