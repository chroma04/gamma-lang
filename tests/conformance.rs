//! Conformance test runner.
//!
//! Runs every `.gm` file under `tests/conformance/` through the `gamma` binary
//! and checks the result against expectations written in the file itself:
//!
//! - `//! stage: lex|parse|check|run` (at the top of the file) selects how far
//!   the compiler goes. The default is `run`.
//! - `//! dump` compares the stage's dump output with a sibling golden file:
//!   `<name>.tokens` for `lex`, `<name>.ast` for `parse`.
//! - `//! exit: N` sets the expected exit status of a `run` test (default 0).
//!   The expected standard output of a `run` test is the sibling `<name>.stdout`
//!   (empty if the file does not exist).
//! - `//~ ERROR text` / `//~ WARNING text` at the end of a line expects a
//!   diagnostic of that severity, reported on that line, whose message contains
//!   `text`. Every reported error or warning must be expected this way.
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
}

struct Expectations {
    stage: Stage,
    dump: bool,
    exit: i32,
    /// (line, severity, message substring)
    diags: Vec<(u32, String, String)>,
}

fn parse_expectations(path: &Path, src: &str) -> Result<Expectations, String> {
    let mut e = Expectations { stage: Stage::Run, dump: false, exit: 0, diags: vec![] };
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
            let rest = line[pos + 3..].trim();
            let (sev, msg) = rest.split_once(' ').unwrap_or((rest, ""));
            let sev = match sev {
                "ERROR" => "error",
                "WARNING" => "warning",
                other => return Err(format!("{}:{}: unknown annotation `{}`", path.display(), i + 1, other)),
            };
            e.diags.push((i as u32 + 1, sev.to_string(), msg.trim().to_string()));
        }
    }
    Ok(e)
}

/// A diagnostic parsed from `--diag-format=short` output.
struct Reported {
    line: u32,
    severity: String,
    message: String,
}

fn parse_reported(stderr: &str, file: &Path) -> Vec<Reported> {
    let prefix = format!("{}:", file.display());
    let mut out = vec![];
    for line in stderr.lines() {
        let Some(rest) = line.strip_prefix(&prefix) else {
            // Diagnostics without a location still count.
            for sev in ["error", "warning"] {
                if let Some(msg) = line.strip_prefix(&format!("{}: ", sev)) {
                    out.push(Reported { line: 0, severity: sev.to_string(), message: msg.to_string() });
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
            out.push(Reported { line: line_no, severity: sev.to_string(), message: msg.to_string() });
        }
    }
    out
}

fn run_one(path: &Path, bless: bool) -> Result<(), String> {
    let src = fs::read_to_string(path).map_err(|e| format!("cannot read: {}", e))?;
    let exp = parse_expectations(path, &src)?;
    let command = match exp.stage {
        Stage::Lex => "lex",
        Stage::Parse => "parse",
        Stage::Check => "check",
        Stage::Run => "run",
    };
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.arg(command).arg("--diag-format=short");
    if exp.dump {
        cmd.arg("--dump");
    }
    cmd.arg(path);
    let output = cmd.output().map_err(|e| format!("cannot run gamma: {}", e))?;
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
    let mut problems = vec![];

    // Diagnostics.
    let mut reported = parse_reported(&stderr, path);
    let mut unmatched_expected = vec![];
    for (line, sev, msg) in &exp.diags {
        if let Some(i) = reported.iter().position(|r| r.line == *line && &r.severity == sev && r.message.contains(msg))
        {
            reported.remove(i);
        } else {
            unmatched_expected.push(format!("  line {}: expected {} containing {:?}", line, sev, msg));
        }
    }
    if !unmatched_expected.is_empty() {
        problems.push(format!("missing diagnostics:\n{}", unmatched_expected.join("\n")));
    }
    if !reported.is_empty() {
        let list: Vec<String> =
            reported.iter().map(|r| format!("  line {}: {}: {}", r.line, r.severity, r.message)).collect();
        problems.push(format!("unexpected diagnostics:\n{}", list.join("\n")));
    }

    // Exit status.
    let expects_errors = exp.diags.iter().any(|(_, sev, _)| sev == "error");
    let code = output.status.code().unwrap_or(-1);
    match exp.stage {
        Stage::Run if !expects_errors => {
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
        Stage::Run if !expects_errors => Some(path.with_extension("stdout")),
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
        if p.is_dir() {
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
