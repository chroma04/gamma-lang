//! Tests of the self-hosted compiler in `compiler/` (`gammac`), written in Gamma.
//!
//! The differential tests build `gammac` with `gamma build` (through the C back end when a
//! C compiler is available, else they run it with `gamma run`), run it over every `.gm`
//! file in the repository, and require output identical to the reference implementation's,
//! byte for byte: for `gammac lex`, the token dump of `gamma lex` and its diagnostics in
//! the JSON format (which carries every field of a diagnostic: spans, labels, notes and
//! fixes), with and without the layout recovery that tools use after errors.
//!
//! `gammac`'s own unit tests run with `gamma test compiler`, and its sources must be
//! formatted as `gamma fmt` formats them.

mod common;

use gamma::diag::DiagFormat;
use gamma::driver::{self, Session};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

fn root() -> &'static Path {
    Path::new(env!("CARGO_MANIFEST_DIR"))
}

/// A command running `gamma`, which caches compiled runtime objects under the target
/// directory rather than in the user's cache.
fn gamma() -> Command {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.env("GAMMA_CACHE_DIR", Path::new(env!("CARGO_TARGET_TMPDIR")).join("gamma-cache"));
    cmd.current_dir(root());
    cmd
}

/// The `.gm` files under `dir`, sorted, as paths relative to the repository root.
fn gm_files(dir: &Path, out: &mut Vec<String>) {
    let mut entries: Vec<PathBuf> = std::fs::read_dir(root().join(dir)).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    for p in entries {
        let rel = p.strip_prefix(root()).unwrap().to_path_buf();
        if p.is_dir() {
            gm_files(&rel, out);
        } else if p.extension().is_some_and(|e| e == "gm") {
            out.push(rel.to_str().unwrap().replace('\\', "/"));
        }
    }
}

/// Every Gamma source in the repository: the conformance tests, the libraries, the
/// examples, the benchmarks and the self-hosted compiler itself.
fn corpus() -> Vec<String> {
    let mut files = Vec::new();
    for dir in ["tests/conformance", "lib", "examples", "bench", "compiler"] {
        gm_files(Path::new(dir), &mut files);
    }
    assert!(files.len() > 500, "only {} files found", files.len());
    files
}

/// How `gammac` is run: a native executable, or the interpreter.
enum Gammac {
    Native(PathBuf),
    Interpreted,
}

/// `gammac`, built once for all the tests.
fn gammac() -> &'static Gammac {
    static GAMMAC: OnceLock<Gammac> = OnceLock::new();
    GAMMAC.get_or_init(|| {
        if !common::c_backend_available() {
            return Gammac::Interpreted;
        }
        let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost");
        std::fs::create_dir_all(&dir).unwrap();
        let exe = dir.join(if cfg!(windows) { "gammac.exe" } else { "gammac" });
        let out = common::output(gamma().args(["build", "compiler", "-o"]).arg(&exe)).unwrap();
        assert!(
            out.status.success(),
            "building gammac failed:\n{}{}",
            String::from_utf8_lossy(&out.stdout),
            String::from_utf8_lossy(&out.stderr)
        );
        Gammac::Native(exe)
    })
}

/// Runs `gammac` with `args`.
fn run_gammac(args: &[String]) -> std::process::Output {
    let mut cmd = match gammac() {
        Gammac::Native(exe) => {
            let mut c = Command::new(exe);
            c.current_dir(root());
            c
        }
        Gammac::Interpreted => {
            let mut c = gamma();
            c.args(["run", "compiler"]);
            c
        }
    };
    cmd.args(args);
    common::output(&mut cmd).unwrap()
}

/// What `gamma lex --diag-format=json` prints for each file, in the layout of
/// `gammac lex` with several files: standard output, standard error and the exit status.
fn reference_lex(files: &[String], recover: bool) -> (String, String, i32) {
    let (mut out, mut err, mut status) = (String::new(), String::new(), 0);
    for path in files {
        out.push_str(&format!("==> {} <==\n", path));
        err.push_str(&format!("==> {} <==\n", path));
        let text = std::fs::read_to_string(root().join(path)).unwrap();
        let mut sess = Session::new();
        sess.recover_layout = recover;
        // Diagnostics name the file as it is given on the command line.
        let file = sess.sources.add(Path::new(path), text);
        let lexed = sess.lex(file);
        out.push_str(&driver::dump_tokens(&sess.sources, &lexed.tokens));
        err.push_str(&sess.render_diagnostics(DiagFormat::Json));
        if sess.diags.has_errors() {
            status = 1;
        }
    }
    (out, err, status)
}

/// The first line where `actual` differs from `expected`, as (the file it belongs to, its
/// line number in the output, the expected line, the actual line).
fn first_difference(expected: &str, actual: &str) -> Option<(String, usize, String, String)> {
    let (mut e, mut a) = (expected.lines(), actual.lines());
    let mut file = String::new();
    for n in 1.. {
        match (e.next(), a.next()) {
            (None, None) => return None,
            (x, y) if x == y => {
                if let Some(name) = x.and_then(|l| l.strip_prefix("==> ")).and_then(|l| l.strip_suffix(" <==")) {
                    file = name.to_string();
                }
            }
            (x, y) => {
                let show = |l: Option<&str>| l.unwrap_or("(end of output)").to_string();
                return Some((file, n, show(x), show(y)));
            }
        }
    }
    None
}

fn check_lex(recover: bool) {
    // In batches, since a command line is limited in length (to 32 KiB on Windows).
    for files in corpus().chunks(100) {
        let (out, err, status) = reference_lex(files, recover);
        let mut args = vec!["lex".to_string(), "--diag-format=json".to_string()];
        if recover {
            args.push("--recover".to_string());
        }
        args.extend(files.iter().cloned());
        let got = run_gammac(&args);
        let got_out = String::from_utf8_lossy(&got.stdout);
        let got_err = String::from_utf8_lossy(&got.stderr);
        for (what, expected, actual) in [("tokens", &out, &got_out), ("diagnostics", &err, &got_err)] {
            if let Some((file, line, e, a)) = first_difference(expected, actual) {
                panic!(
                    "gammac lex: the {} of {} differ (output line {}):\nexpected: {}\n  gammac: {}",
                    what, file, line, e, a
                );
            }
        }
        assert_eq!(got.status.code(), Some(status), "exit status of gammac lex on {} …", files[0]);
    }
}

#[test]
fn lexer_matches_the_reference_on_the_whole_repository() {
    check_lex(false);
}

#[test]
fn recovering_lexer_matches_the_reference_on_the_whole_repository() {
    check_lex(true);
}

#[test]
fn usage_errors() {
    let got = run_gammac(&["parse".to_string()]);
    assert_eq!(got.status.code(), Some(2));
    assert!(String::from_utf8_lossy(&got.stderr).starts_with("usage: gammac"));
    let got = run_gammac(&["lex".to_string(), "no/such/file.gm".to_string()]);
    assert_eq!(got.status.code(), Some(2));
    assert!(String::from_utf8_lossy(&got.stderr).contains("cannot read `no/such/file.gm`"));
}

#[test]
fn unit_tests_pass() {
    let out = common::output(gamma().args(["test", "compiler"])).unwrap();
    assert!(
        out.status.success(),
        "gamma test compiler failed:\n{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
}

#[test]
fn sources_are_formatted() {
    // Run `gamma fmt compiler` after editing it.
    let mut files = Vec::new();
    gm_files(Path::new("compiler"), &mut files);
    let changed: Vec<&String> = files
        .iter()
        .filter(|path| {
            let src = std::fs::read_to_string(root().join(path)).unwrap();
            gamma::fmt::format_source(&src).unwrap() != src
        })
        .collect();
    assert!(changed.is_empty(), "not formatted (run `gamma fmt compiler`): {:?}", changed);
}
