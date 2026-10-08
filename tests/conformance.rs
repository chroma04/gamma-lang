//! Conformance test runner.
//!
//! Runs every `.gm` file under `tests/conformance/` through the `gamma` binary
//! and checks the result against expectations written in the file itself:
//!
//! - `//! stage: lex|parse|check|run|test` (at the top of the file) selects
//!   how far the compiler goes; `test` runs the file's `test` declarations with
//!   `gamma test`. The default is `run`.
//! - `//! dump` compares the stage's dump output with a sibling golden file:
//!   `<name>.tokens` for `lex`, `<name>.ast` for `parse`.
//! - `//! exit: N` sets the expected exit status of a `run` or `test` test
//!   (default 0). Their expected standard output is the sibling `<name>.stdout`
//!   (empty if the file does not exist).
//! - `//~ ERROR text` / `//~ WARNING text` at the end of a line expects a
//!   diagnostic of that severity, reported on that line, whose message contains
//!   `text`. `//~? ERROR text` (anywhere) expects one without a source location.
//!   Every reported error or warning must be expected this way.
//!
//! A directory whose name ends in `.test` is one test of several modules. The
//! program's root module is its `main/` subdirectory if there is one, and the
//! directory itself otherwise. Directives may appear in any of its `.gm` files,
//! `//~` annotations refer to the line of the file they are written in, and the
//! expected output of `name.test` is the sibling `name.stdout`.
//!
//! Every `run` test that expects no errors also runs through the C back end
//! (`gamma run --backend=c`) and must give the same output and exit status,
//! unless the system has no C compiler or `GAMMA_TEST_C=0` is set.
//!
//! Set `BLESS=1` to rewrite golden files from the current output instead of
//! comparing, and `GAMMA_TEST_FILTER=substring` to run only matching tests.

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Stage {
    Lex,
    Parse,
    Check,
    Run,
    Test,
}

struct Expectations {
    stage: Stage,
    dump: bool,
    exit: i32,
    /// (file, line, severity, message substring); line 0 means no location.
    diags: Vec<(PathBuf, u32, String, String)>,
}

fn parse_expectations(files: &[PathBuf]) -> Result<Expectations, String> {
    let mut e = Expectations { stage: Stage::Run, dump: false, exit: 0, diags: vec![] };
    for path in files {
        let src = fs::read_to_string(path).map_err(|err| format!("cannot read {}: {}", path.display(), err))?;
        parse_file_expectations(path, &src, &mut e)?;
    }
    Ok(e)
}

fn parse_file_expectations(path: &Path, src: &str, e: &mut Expectations) -> Result<(), String> {
    for (i, line) in src.lines().enumerate() {
        let trimmed = line.trim_start();
        if let Some(directive) = trimmed.strip_prefix("//!") {
            let directive = directive.trim();
            if let Some(stage) = directive.strip_prefix("stage:") {
                e.stage = match stage.trim() {
                    "lex" => Stage::Lex,
                    "parse" => Stage::Parse,
                    "check" => Stage::Check,
                    "run" => Stage::Run,
                    "test" => Stage::Test,
                    other => return Err(format!("{}: unknown stage `{}`", path.display(), other)),
                };
            } else if directive == "dump" {
                e.dump = true;
            } else if let Some(code) = directive.strip_prefix("exit:") {
                e.exit = code.trim().parse().map_err(|_| format!("{}: bad exit code", path.display()))?;
            } else {
                return Err(format!("{}:{}: unknown directive `{}`", path.display(), i + 1, directive));
            }
        }
        if let Some(pos) = line.find("//~") {
            // `//~?` expects a diagnostic that has no source location.
            let (unlocated, rest) = match line[pos + 3..].strip_prefix('?') {
                Some(r) => (true, r.trim()),
                None => (false, line[pos + 3..].trim()),
            };
            let (sev, msg) = rest.split_once(' ').unwrap_or((rest, ""));
            let sev = match sev {
                "ERROR" => "error",
                "WARNING" => "warning",
                other => return Err(format!("{}:{}: unknown annotation `{}`", path.display(), i + 1, other)),
            };
            let line_no = if unlocated { 0 } else { i as u32 + 1 };
            e.diags.push((path.to_path_buf(), line_no, sev.to_string(), msg.trim().to_string()));
        }
    }
    Ok(())
}

/// A diagnostic parsed from `--diag-format=short` output.
struct Reported {
    /// The file, or `None` for a diagnostic without a location.
    file: Option<PathBuf>,
    line: u32,
    severity: String,
    message: String,
}

/// Parses `path:line:col: severity: message` and `severity: message` lines.
/// `files` are the test's files, to recognize the path prefix.
fn parse_reported(stderr: &str, files: &[PathBuf]) -> Vec<Reported> {
    let mut out = vec![];
    for line in stderr.lines() {
        let located = files.iter().find_map(|f| line.strip_prefix(&format!("{}:", f.display())).map(|r| (f, r)));
        let Some((file, rest)) = located else {
            // Diagnostics without a location still count.
            for sev in ["error", "warning"] {
                if let Some(msg) = line.strip_prefix(&format!("{}: ", sev)) {
                    out.push(Reported { file: None, line: 0, severity: sev.to_string(), message: msg.to_string() });
                }
            }
            continue;
        };
        let mut parts = rest.splitn(3, ':');
        let line_no = parts.next().and_then(|s| s.parse().ok()).unwrap_or(0);
        let _col = parts.next();
        let tail = parts.next().unwrap_or("").trim_start();
        let (sev, msg) = tail.split_once(": ").unwrap_or((tail, ""));
        if sev == "error" || sev == "warning" {
            out.push(Reported {
                file: Some(file.clone()),
                line: line_no,
                severity: sev.to_string(),
                message: msg.to_string(),
            });
        }
    }
    out
}

/// The `.gm` files under `dir`, sorted.
fn gm_files(dir: &Path) -> Vec<PathBuf> {
    let mut out = vec![];
    let Ok(entries) = fs::read_dir(dir) else { return out };
    for entry in entries.flatten() {
        let p = entry.path();
        if p.is_dir() {
            out.extend(gm_files(&p));
        } else if p.extension().is_some_and(|e| e == "gm") {
            out.push(p);
        }
    }
    out.sort();
    out
}

/// Whether the C back end can be tested: a C compiler runs and `GAMMA_TEST_C` is not 0.
fn c_backend_available() -> bool {
    static AVAILABLE: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *AVAILABLE.get_or_init(|| {
        if std::env::var("GAMMA_TEST_C").is_ok_and(|v| v == "0") {
            return false;
        }
        let cc = std::env::var("CC").unwrap_or_else(|_| "cc".into());
        Command::new(cc).arg("--version").output().is_ok_and(|o| o.status.success())
    })
}

/// Runs a `run` test through the C back end and compares it with the expectations.
fn run_c(target: &Path, golden: &Path, exit: i32) -> Result<(), String> {
    let output = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .args(["run", "--backend=c", "--diag-format=short"])
        .arg(target)
        .output()
        .map_err(|e| format!("cannot run gamma: {}", e))?;
    let stdout = String::from_utf8_lossy(&output.stdout);
    let expected = fs::read_to_string(golden).unwrap_or_default();
    let mut problems = vec![];
    let code = output.status.code().unwrap_or(-1);
    if code != exit {
        problems.push(format!(
            "[C back end] exit status {} (expected {})\nstderr:\n{}",
            code,
            exit,
            String::from_utf8_lossy(&output.stderr)
        ));
    }
    if expected != stdout {
        problems.push(format!("[C back end] output differs from {}:\n{}", golden.display(), diff(&expected, &stdout)));
    }
    if problems.is_empty() { Ok(()) } else { Err(problems.join("\n")) }
}

fn run_one(path: &Path, bless: bool) -> Result<(), String> {
    // A `.test` directory, or a single file.
    let (files, target) = if path.is_dir() {
        let main = path.join("main");
        (gm_files(path), if main.is_dir() { main } else { path.to_path_buf() })
    } else {
        (vec![path.to_path_buf()], path.to_path_buf())
    };
    let exp = parse_expectations(&files)?;
    let command = match exp.stage {
        Stage::Lex => "lex",
        Stage::Parse => "parse",
        Stage::Check => "check",
        Stage::Run => "run",
        Stage::Test => "test",
    };
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.arg(command).arg("--diag-format=short");
    if exp.dump {
        cmd.arg("--dump");
    }
    cmd.arg(&target);
    let output = cmd.output().map_err(|e| format!("cannot run gamma: {}", e))?;
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
    let mut problems = vec![];

    // Diagnostics.
    let mut reported = parse_reported(&stderr, &files);
    let mut unmatched_expected = vec![];
    for (file, line, sev, msg) in &exp.diags {
        let at =
            |r: &Reported| if *line == 0 { r.file.is_none() } else { r.file.as_ref() == Some(file) && r.line == *line };
        if let Some(i) = reported.iter().position(|r| at(r) && &r.severity == sev && r.message.contains(msg)) {
            reported.remove(i);
        } else {
            unmatched_expected.push(format!("  {}:{}: expected {} containing {:?}", file.display(), line, sev, msg));
        }
    }
    if !unmatched_expected.is_empty() {
        problems.push(format!("missing diagnostics:\n{}", unmatched_expected.join("\n")));
    }
    if !reported.is_empty() {
        let list: Vec<String> = reported
            .iter()
            .map(|r| {
                let file = r.file.as_ref().map_or(String::new(), |f| format!("{}:", f.display()));
                format!("  {}{}: {}: {}", file, r.line, r.severity, r.message)
            })
            .collect();
        problems.push(format!("unexpected diagnostics:\n{}", list.join("\n")));
    }

    // Exit status.
    let expects_errors = exp.diags.iter().any(|(_, _, sev, _)| sev == "error");
    let code = output.status.code().unwrap_or(-1);
    match exp.stage {
        Stage::Run | Stage::Test if !expects_errors => {
            if code != exp.exit {
                problems.push(format!("exit status {} (expected {})\nstderr:\n{}", code, exp.exit, stderr));
            }
        }
        _ => {
            let expected = if expects_errors { 1 } else { 0 };
            if code != expected {
                problems.push(format!("exit status {} (expected {})\nstderr:\n{}", code, expected, stderr));
            }
        }
    }

    // Golden output.
    let golden = match exp.stage {
        Stage::Lex if exp.dump => Some(path.with_extension("tokens")),
        Stage::Parse if exp.dump => Some(path.with_extension("ast")),
        Stage::Run | Stage::Test if !expects_errors => Some(path.with_extension("stdout")),
        _ => None,
    };
    if let Some(golden) = golden {
        if bless {
            if stdout.is_empty() && !golden.exists() {
                // Nothing to record.
            } else {
                fs::write(&golden, &stdout).map_err(|e| format!("cannot write {}: {}", golden.display(), e))?;
            }
        } else {
            let expected = fs::read_to_string(&golden).unwrap_or_default();
            if expected != stdout {
                problems.push(format!("output differs from {}:\n{}", golden.display(), diff(&expected, &stdout)));
            }
        }
    }

    if exp.stage == Stage::Run
        && !expects_errors
        && problems.is_empty()
        && !bless
        && c_backend_available()
        && let Err(p) = run_c(&target, &path.with_extension("stdout"), exp.exit)
    {
        problems.push(p);
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

fn collect(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let p = entry.path();
        if p.is_dir() && p.extension().is_some_and(|e| e == "test") {
            out.push(p);
        } else if p.is_dir() {
            collect(&p, out);
        } else if p.extension().is_some_and(|e| e == "gm") {
            out.push(p);
        }
    }
}

#[test]
fn conformance() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/conformance");
    let mut files = vec![];
    collect(&root, &mut files);
    files.sort();
    let filter = std::env::var("GAMMA_TEST_FILTER").ok();
    let bless = std::env::var("BLESS").is_ok_and(|v| v == "1");
    let base = Path::new(env!("CARGO_MANIFEST_DIR"));
    let mut failures = BTreeMap::new();
    let mut count = 0;
    for f in &files {
        let rel = f.strip_prefix(base).unwrap_or(f).to_path_buf();
        if let Some(filter) = &filter
            && !rel.to_string_lossy().contains(filter.as_str())
        {
            continue;
        }
        count += 1;
        if let Err(msg) = run_one(&rel, bless) {
            failures.insert(rel.display().to_string(), msg);
        }
    }
    for (name, msg) in &failures {
        eprintln!("FAIL {}\n{}\n", name, msg);
    }
    eprintln!("conformance: {} passed, {} failed", count - failures.len(), failures.len());
    assert!(failures.is_empty(), "{} conformance test(s) failed", failures.len());
}
