//! Conformance test runner.
//!
//! Runs every `.gm` file under `tests/conformance/` through the `gamma` binary
//! and checks the result against expectations written in the file itself:
//!
//! - `//! stage: lex|parse|check|run|test|lint` (at the top of the file) selects
//!   how far the compiler goes; `test` runs the file's `test` declarations with
//!   `gamma test`, and `lint` runs `gamma lint`. The default is `run`.
//! - `//! lint: <options>` passes options to `gamma lint` (`--warn=prefer-immutable`).
//! - `//! dump` compares the stage's dump output with a sibling golden file:
//!   `<name>.tokens` for `lex`, `<name>.ast` for `parse`.
//! - `//! stderr` also compares the standard error of a `run` or `test` test
//!   with the sibling `<name>.stderr`, for tests of trap reports. Paths into the
//!   standard library are written `<std>/…` there (see `normalize_stderr`).
//! - `//! mode: fast` (or `checked`, `debug`) builds a `run` or `test` test in
//!   that build mode (default: debug). `//! mode: safe` tests run only through
//!   the C back end, and only where Safe builds are available (below), since
//!   they test what Safe builds catch and the interpreter does not.
//! - `//! safe: skip <reason>` leaves a test out of the Safe lane (below), for
//!   tests whose behavior differs in Safe builds by design, such as tests that
//!   overflow the stack or turn integers into pointers.
//! - `//! arch: x86_64` (or `aarch64`, …) runs the test only when compiling for
//!   that architecture (the host's, or the one a cross compiler in `CC` targets),
//!   for tests of features that exist on some targets only.
//! - `//! endian: little` (or `big`) runs the test only when compiling for a target of
//!   that byte order, for tests whose output shows the layout of values in memory.
//! - `//! os: unix` (or `windows`, `wasi`) runs the test only when compiling for that kind
//!   of operating system: `unix` is everything but Windows and WASI. It is for tests of what
//!   the other kinds lack (POSIX-only C functions, the System V calling convention, child
//!   processes); keep such tests few.
//! - `//! ptr_bits: 64` (or `32`) runs the test only when compiling for a target with
//!   pointers of that size, for tests whose output shows the size of pointers and `usize`.
//! - `//! needs: f16` leaves the test out of the C back end lanes when the target's C
//!   compiler has no `_Float16` (GCC on s390x); the interpreter still runs it. Likewise
//!   `//! needs: threads` and `//! needs: sockets` when the target has no threads or
//!   sockets (WASI, where they fail with `IoError.Unsupported`). Several requirements
//!   may be given, separated by commas.
//! - `//! native: reason` runs a `run` or `test` test only through the C back end, for
//!   tests of what the interpreter cannot do (such as calls through function pointers
//!   re-typed to another signature).
//! - `//! filter: text` runs a `test` test with `gamma test --filter=text`.
//! - `//! exit: N` sets the expected exit status of a `run` or `test` test
//!   (default 0). Their expected standard output is the sibling `<name>.stdout`
//!   (empty if the file does not exist). If a sibling `<name>.stdin` exists, it
//!   is the program's standard input; otherwise the input is empty.
//! - `//~ ERROR text` / `//~ WARNING text` at the end of a line expects a
//!   diagnostic of that severity, reported on that line, whose message contains
//!   `text`. `//~? ERROR text` (anywhere) expects one without a source location.
//!   One line may carry several annotations (`//~ ERROR a //~ NOTE b`).
//!   Every reported error or warning must be expected this way. The message of a lint
//!   warning starts with its rule in brackets: `//~ WARNING [unused-variable] never used`. `//~ NOTE text`
//!   expects a note of some diagnostic on that line: a secondary location (such as
//!   a call in a compile-time call stack) or an explanation attached to a diagnostic
//!   reported there; notes that no annotation names are allowed.
//!
//! A directory whose name ends in `.test` is one test of several modules. The
//! program's root module is its `main/` subdirectory if there is one, and the
//! directory itself otherwise. Directives may appear in any of its `.gm` files,
//! `//~` annotations refer to the line of the file they are written in, and the
//! expected output of `name.test` is the sibling `name.stdout`.
//!
//! Every `run` test that expects no errors also runs through the C back end
//! (`gamma run --backend=c`), and every such `test` test through
//! `gamma test --backend=c`. Both must give the same output and exit status as
//! the interpreter, unless the system has no C compiler or `GAMMA_TEST_C=0` is set.
//!
//! Safe builds need Fil-C (found as for `gamma build --mode=safe`). Where it is
//! found, the `mode: safe` tests run. `GAMMA_TEST_SAFE=1` requires it and adds
//! the Safe lane: every `run` and `test` test in the default or Checked mode
//! (except those marked `safe: skip`) also runs through the C back end in a Safe
//! build, and must give the same output and exit status. Standard error is not
//! compared there, since Fil-C reports memory errors in its own words.
//! `GAMMA_TEST_SAFE=0` turns Safe tests off.
//!
//! Tests run in parallel, those that took longest in the previous run first (the
//! times are kept in Cargo's temporary directory for integration tests).
//!
//! Set `BLESS=1` to rewrite golden files from the current output instead of
//! comparing, `GAMMA_TEST_FILTER=substring` to run only matching tests, and
//! `GAMMA_TEST_DEBUG_INFO=1` to build the C back end's programs with `-g`.

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
    Lint,
}

struct Expectations {
    stage: Stage,
    dump: bool,
    /// Compare standard error with `<name>.stderr`.
    stderr: bool,
    /// The build mode for `run` and `test`, if not the default.
    mode: Option<String>,
    /// The `--filter` of a `test` test.
    filter: Option<String>,
    exit: i32,
    /// The only architecture the test applies to, as `std::env::consts::ARCH` names it.
    arch: Option<String>,
    /// The only byte order (`little` or `big`) the test applies to.
    endian: Option<String>,
    /// The only kind of operating system the test applies to (`unix` or `windows`).
    os: Option<String>,
    /// Leave the test out of the Safe lane.
    safe_skip: bool,
    /// The C lanes need `_Float16`.
    needs_f16: bool,
    /// The C lanes need threads, or sockets.
    needs_threads: bool,
    needs_sockets: bool,
    /// The only pointer size (in bits) the test applies to.
    ptr_bits: Option<u32>,
    /// Run the test only through the C back end.
    native: bool,
    /// The options of `gamma lint`.
    lint_args: Vec<String>,
    /// (file, line, severity, message substring); line 0 means no location.
    diags: Vec<(PathBuf, u32, String, String)>,
}

fn parse_expectations(files: &[PathBuf]) -> Result<Expectations, String> {
    let mut e = Expectations {
        stage: Stage::Run,
        dump: false,
        stderr: false,
        mode: None,
        filter: None,
        exit: 0,
        arch: None,
        endian: None,
        os: None,
        safe_skip: false,
        needs_f16: false,
        needs_threads: false,
        needs_sockets: false,
        ptr_bits: None,
        native: false,
        lint_args: vec![],
        diags: vec![],
    };
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
                    "lint" => Stage::Lint,
                    other => return Err(format!("{}: unknown stage `{}`", path.display(), other)),
                };
            } else if let Some(args) = directive.strip_prefix("lint:") {
                e.lint_args.extend(args.split_whitespace().map(str::to_string));
            } else if directive == "dump" {
                e.dump = true;
            } else if directive == "stderr" {
                e.stderr = true;
            } else if let Some(mode) = directive.strip_prefix("mode:") {
                e.mode = Some(mode.trim().to_string());
            } else if let Some(filter) = directive.strip_prefix("filter:") {
                e.filter = Some(filter.trim().to_string());
            } else if let Some(os) = directive.strip_prefix("os:") {
                match os.trim() {
                    "unix" | "windows" | "wasi" => e.os = Some(os.trim().to_string()),
                    other => return Err(format!("{}:{}: unknown os `{}`", path.display(), i + 1, other)),
                }
            } else if let Some(endian) = directive.strip_prefix("endian:") {
                e.endian = Some(endian.trim().to_string());
            } else if let Some(arch) = directive.strip_prefix("arch:") {
                e.arch = Some(arch.trim().to_string());
            } else if let Some(needs) = directive.strip_prefix("needs:") {
                for need in needs.split(',') {
                    match need.trim() {
                        "f16" => e.needs_f16 = true,
                        "threads" => e.needs_threads = true,
                        "sockets" => e.needs_sockets = true,
                        other => return Err(format!("{}:{}: unknown requirement `{}`", path.display(), i + 1, other)),
                    }
                }
            } else if let Some(bits) = directive.strip_prefix("ptr_bits:") {
                match bits.trim() {
                    "32" => e.ptr_bits = Some(32),
                    "64" => e.ptr_bits = Some(64),
                    other => return Err(format!("{}:{}: unknown pointer size `{}`", path.display(), i + 1, other)),
                }
            } else if let Some(safe) = directive.strip_prefix("safe:") {
                if !safe.trim().starts_with("skip ") {
                    return Err(format!("{}:{}: expected `safe: skip <reason>`", path.display(), i + 1));
                }
                e.safe_skip = true;
            } else if directive.starts_with("native:") {
                e.native = true;
            } else if let Some(code) = directive.strip_prefix("exit:") {
                e.exit = code.trim().parse().map_err(|_| format!("{}: bad exit code", path.display()))?;
            } else {
                return Err(format!("{}:{}: unknown directive `{}`", path.display(), i + 1, directive));
            }
        }
        // A line may carry several annotations: `//~ ERROR a //~ NOTE b`.
        for ann in line.split("//~").skip(1) {
            // `//~?` expects a diagnostic that has no source location.
            let (unlocated, rest) = match ann.strip_prefix('?') {
                Some(r) => (true, r.trim()),
                None => (false, ann.trim()),
            };
            let (sev, msg) = rest.split_once(' ').unwrap_or((rest, ""));
            let sev = match sev {
                "ERROR" => "error",
                "WARNING" => "warning",
                "NOTE" => "note",
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

/// Standard error with the standard library's directory, which differs between
/// checkouts, written as `<std>` and the rest of such a path with `/` separators, and
/// without the report a WebAssembly runtime adds to a trap.
fn normalize_stderr(stderr: &str) -> String {
    let stderr = gamma::toolchain::strip_wasm_trap_report(stderr);
    let native = Path::new(env!("CARGO_MANIFEST_DIR")).join("lib").join("std").display().to_string();
    // On Windows the path may be printed with either separator.
    let forward = native.replace('\\', "/");
    let mut out = String::with_capacity(stderr.len());
    for line in stderr.split_inclusive('\n') {
        let found = [&native, &forward].into_iter().find_map(|d| line.find(d.as_str()).map(|i| (i, d.len())));
        match found {
            Some((i, len)) => {
                let rest = &line[i + len..];
                let end = rest.find(':').unwrap_or(rest.len());
                out.push_str(&line[..i]);
                out.push_str("<std>");
                out.push_str(&rest[..end].replace('\\', "/"));
                out.push_str(&rest[end..]);
            }
            None => out.push_str(line),
        }
    }
    out
}

/// Parses `path:line:col: severity: message` and `severity: message` lines.
/// `files` are the test's files, to recognize the path prefix.
fn parse_reported(stderr: &str, files: &[PathBuf]) -> Vec<Reported> {
    let mut out = vec![];
    for line in stderr.lines() {
        // Diagnostics name files with `/` on every system.
        let located = files.iter().find_map(|f| {
            line.strip_prefix(&format!("{}:", f.display().to_string().replace('\\', "/"))).map(|r| (f, r))
        });
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
        // A lint's rule follows the severity: `warning[unused-variable]: …`.
        let (sev, message) = match sev.split_once('[') {
            Some((s, rule)) => (s, format!("[{}] {}", rule.trim_end_matches(']'), msg)),
            None => (sev, msg.to_string()),
        };
        if sev == "error" || sev == "warning" || sev == "note" {
            out.push(Reported { file: Some(file.clone()), line: line_no, severity: sev.to_string(), message });
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

/// The architecture the tests compile for, as `std::env::consts::ARCH` names it: the
/// target of the C compiler in `CC` and `CFLAGS`, which is the host's by default.
fn target_arch() -> &'static str {
    static ARCH: std::sync::OnceLock<&'static str> = std::sync::OnceLock::new();
    ARCH.get_or_init(|| {
        let target = gamma::toolchain::Toolchain::from_env().and_then(|t| t.target());
        match target.map(|t| t.arch) {
            Ok("X86_64") => "x86_64",
            Ok("Aarch64") => "aarch64",
            Ok("Riscv64") => "riscv64",
            Ok("S390x") => "s390x",
            Ok("Wasm32") => "wasm32",
            Ok(_) => "other",
            Err(_) => std::env::consts::ARCH,
        }
    })
}

/// The kind of operating system the tests compile for (`unix`, `windows` or `wasi`), as for
/// [`target_arch`].
fn target_os_kind() -> &'static str {
    static OS: std::sync::OnceLock<&'static str> = std::sync::OnceLock::new();
    OS.get_or_init(|| {
        let target = gamma::toolchain::Toolchain::from_env().and_then(|t| t.target());
        match target.map(|t| t.os) {
            Ok("Windows") => "windows",
            Ok("Wasi") => "wasi",
            Ok(_) => "unix",
            Err(_) => {
                if cfg!(windows) {
                    "windows"
                } else {
                    "unix"
                }
            }
        }
    })
}

/// A directory for the temporary files of WebAssembly modules, as a path relative to the
/// package's root, where the tests run.
fn wasi_tmp_dir() -> &'static str {
    static DIR: std::sync::OnceLock<String> = std::sync::OnceLock::new();
    DIR.get_or_init(|| {
        let tmp = Path::new(env!("CARGO_TARGET_TMPDIR")).join("wasi-tmp");
        let _ = fs::create_dir_all(&tmp);
        let rel = tmp.strip_prefix(env!("CARGO_MANIFEST_DIR")).unwrap_or(&tmp);
        rel.display().to_string().replace('\\', "/")
    })
}

/// The pointer size in bits of the target the tests compile for, as for [`target_arch`].
fn target_ptr_bits() -> u32 {
    static BITS: std::sync::OnceLock<u32> = std::sync::OnceLock::new();
    *BITS.get_or_init(|| {
        let target = gamma::toolchain::Toolchain::from_env().and_then(|t| t.target());
        target.map_or(usize::BITS, |t| t.ptr_bits())
    })
}

/// The byte order the tests compile for (`little` or `big`), as for [`target_arch`].
fn target_endian() -> &'static str {
    static ENDIAN: std::sync::OnceLock<&'static str> = std::sync::OnceLock::new();
    ENDIAN.get_or_init(|| {
        let target = gamma::toolchain::Toolchain::from_env().and_then(|t| t.target());
        let little = target.map_or(cfg!(target_endian = "little"), |t| t.little_endian);
        if little { "little" } else { "big" }
    })
}

/// Whether the target's C compiler has `_Float16`.
fn target_has_f16() -> bool {
    static F16: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *F16.get_or_init(|| gamma::toolchain::Toolchain::from_env().and_then(|t| t.target()).is_ok_and(|t| t.f16))
}

/// Whether the C back end can be tested: a C compiler runs and `GAMMA_TEST_C` is not 0.
fn c_backend_available() -> bool {
    static AVAILABLE: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *AVAILABLE.get_or_init(|| {
        if std::env::var("GAMMA_TEST_C").is_ok_and(|v| v == "0") {
            return false;
        }
        gamma::toolchain::Toolchain::from_env()
            .is_ok_and(|t| t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()))
    })
}

/// Whether Safe builds can be tested: Fil-C is found and `GAMMA_TEST_SAFE` is not 0.
/// With `GAMMA_TEST_SAFE=1`, its absence fails the tests.
fn safe_available() -> bool {
    static AVAILABLE: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *AVAILABLE.get_or_init(|| {
        let setting = std::env::var("GAMMA_TEST_SAFE").unwrap_or_default();
        if setting == "0" || !c_backend_available() {
            return false;
        }
        let found = gamma::toolchain::Toolchain::for_safe_build().and_then(|t| t.target()).map(|t| t.filc);
        match found {
            Ok(true) => true,
            _ if setting == "1" => panic!("GAMMA_TEST_SAFE=1, but no Fil-C compiler was found: {:?}", found),
            _ => false,
        }
    })
}

/// Whether the Safe lane runs: `GAMMA_TEST_SAFE=1`.
fn safe_lane() -> bool {
    std::env::var("GAMMA_TEST_SAFE").is_ok_and(|v| v == "1") && safe_available()
}

/// A command running `gamma`, which caches compiled runtime objects under the target
/// directory rather than in the user's cache.
fn gamma() -> Command {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.env("GAMMA_CACHE_DIR", Path::new(env!("CARGO_TARGET_TMPDIR")).join("gamma-cache"));
    cmd
}

/// Gives the command the file `input` as standard input, if it exists.
fn set_stdin(cmd: &mut Command, input: &Path) -> Result<(), String> {
    if input.exists() {
        let f = fs::File::open(input).map_err(|e| format!("{}: {}", input.display(), e))?;
        cmd.stdin(f);
    }
    Ok(())
}

/// Runs a `run` or `test` test (`command`) through the C back end in build mode
/// `mode` and compares it with the expectations.
fn run_c(
    command: &str,
    target: &Path,
    golden: &Path,
    exp: &Expectations,
    mode: Option<&str>,
    stderr_golden: Option<&Path>,
) -> Result<(), String> {
    let exit = exp.exit;
    let lane = if mode == Some("safe") { "[C back end, Safe build]" } else { "[C back end]" };
    let mut cmd = gamma();
    cmd.args([command, "--backend=c", "--diag-format=short"]);
    if std::env::var("GAMMA_TEST_DEBUG_INFO").is_ok_and(|v| v == "1") {
        cmd.arg("-g");
    }
    if let Some(m) = mode {
        cmd.arg(format!("--mode={}", m));
    }
    if let (Some(f), "test") = (&exp.filter, command) {
        cmd.arg(format!("--filter={}", f));
    }
    cmd.arg(target);
    if target_os_kind() == "wasi" {
        // A WebAssembly module sees only the current directory (the package's root): its
        // temporary files go below it.
        cmd.env("TMPDIR", wasi_tmp_dir());
    }
    set_stdin(&mut cmd, &golden.with_extension("stdin"))?;
    let output = cmd.output().map_err(|e| format!("cannot run gamma: {}", e))?;
    let stdout = String::from_utf8_lossy(&output.stdout);
    let expected = fs::read_to_string(golden).unwrap_or_default();
    let mut problems = vec![];
    let code = output.status.code().unwrap_or(-1);
    if code != exit {
        problems.push(format!(
            "{} exit status {} (expected {})\nstderr:\n{}",
            lane,
            code,
            exit,
            String::from_utf8_lossy(&output.stderr)
        ));
    }
    if expected != stdout {
        problems.push(format!("{} output differs from {}:\n{}", lane, golden.display(), diff(&expected, &stdout)));
    }
    if let Some(g) = stderr_golden {
        let expected = fs::read_to_string(g).unwrap_or_default();
        let stderr = normalize_stderr(&String::from_utf8_lossy(&output.stderr));
        if expected != stderr {
            problems.push(format!(
                "{} standard error differs from {}:\n{}",
                lane,
                g.display(),
                diff(&expected, &stderr)
            ));
        }
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
    if exp.endian.as_deref().is_some_and(|e| e != target_endian()) {
        return Ok(());
    }
    if exp.os.as_deref().is_some_and(|o| o != target_os_kind()) {
        return Ok(());
    }
    if exp.ptr_bits.is_some_and(|b| b != target_ptr_bits()) {
        return Ok(());
    }
    if exp.arch.as_deref().is_some_and(|a| a != target_arch()) {
        return Ok(());
    }
    if exp.mode.as_deref() == Some("safe") {
        return run_safe_only(path, &target, &exp);
    }
    let command = match exp.stage {
        Stage::Lex => "lex",
        Stage::Parse => "parse",
        Stage::Check => "check",
        Stage::Run => "run",
        Stage::Test => "test",
        Stage::Lint => "lint",
    };
    let mut cmd = gamma();
    cmd.arg(command).arg("--diag-format=short");
    if exp.stage == Stage::Lint {
        cmd.args(&exp.lint_args);
    }
    if exp.dump {
        cmd.arg("--dump");
    }
    if let (Some(m), Stage::Run | Stage::Test) = (&exp.mode, exp.stage) {
        cmd.arg(format!("--mode={}", m));
    }
    if let (Some(f), Stage::Test) = (&exp.filter, exp.stage) {
        cmd.arg(format!("--filter={}", f));
    }
    if exp.native && matches!(exp.stage, Stage::Run | Stage::Test) {
        if !c_backend_available() {
            return Ok(());
        }
        cmd.arg("--backend=c");
    }
    cmd.arg(&target);
    set_stdin(&mut cmd, &path.with_extension("stdin"))?;
    let output = cmd.output().map_err(|e| format!("cannot run gamma: {}", e))?;
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    let stderr = String::from_utf8_lossy(&output.stderr).into_owned();
    let golden_stderr = normalize_stderr(&stderr);
    let mut problems = vec![];

    // Diagnostics.
    // With `//! stderr`, the golden file accounts for all of standard error.
    let mut reported = if exp.stderr { vec![] } else { parse_reported(&stderr, &files) };
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
    // Notes (secondary locations) need not be expected.
    reported.retain(|r| r.severity != "note");
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

    // Golden standard error.
    let stderr_golden = path.with_extension("stderr");
    let stderr_golden = (exp.stderr && matches!(exp.stage, Stage::Run | Stage::Test) && !expects_errors)
        .then_some(stderr_golden.as_path());
    if let Some(g) = stderr_golden {
        if bless {
            fs::write(g, &golden_stderr).map_err(|e| format!("cannot write {}: {}", g.display(), e))?;
        } else {
            let expected = fs::read_to_string(g).unwrap_or_default();
            if expected != golden_stderr {
                problems.push(format!(
                    "standard error differs from {}:\n{}",
                    g.display(),
                    diff(&expected, &golden_stderr)
                ));
            }
        }
    }

    let compiled = matches!(exp.stage, Stage::Run | Stage::Test)
        && !expects_errors
        && !bless
        && !(exp.needs_f16 && !target_has_f16())
        && !((exp.needs_threads || exp.needs_sockets) && target_os_kind() == "wasi");
    let golden = path.with_extension("stdout");
    if compiled
        && problems.is_empty()
        && !exp.native
        && c_backend_available()
        && let Err(p) = run_c(command, &target, &golden, &exp, exp.mode.as_deref(), stderr_golden)
    {
        problems.push(p);
    }
    if compiled
        && problems.is_empty()
        && !exp.safe_skip
        && matches!(exp.mode.as_deref(), None | Some("checked"))
        && safe_lane()
        && let Err(p) = run_c(command, &target, &golden, &exp, Some("safe"), None)
    {
        problems.push(p);
    }

    if problems.is_empty() { Ok(()) } else { Err(problems.join("\n")) }
}

/// Runs a `mode: safe` test, which only a Safe build through the C back end runs.
fn run_safe_only(path: &Path, target: &Path, exp: &Expectations) -> Result<(), String> {
    if !matches!(exp.stage, Stage::Run | Stage::Test) || !exp.diags.is_empty() {
        return Err(format!(
            "{}: `mode: safe` applies only to `run` and `test` tests without diagnostics",
            path.display()
        ));
    }
    if !safe_available() {
        return Ok(());
    }
    let command = if exp.stage == Stage::Run { "run" } else { "test" };
    run_c(command, target, &path.with_extension("stdout"), exp, Some("safe"), None)
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

/// The run times in milliseconds, by test, recorded in `file` by an earlier run.
fn previous_times(file: &Path) -> BTreeMap<String, u64> {
    let text = fs::read_to_string(file).unwrap_or_default();
    text.lines()
        .filter_map(|l| {
            let (ms, name) = l.split_once(' ')?;
            Some((name.to_string(), ms.parse().ok()?))
        })
        .collect()
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
    let selected: Vec<PathBuf> = files
        .iter()
        .map(|f| f.strip_prefix(base).unwrap_or(f).to_path_buf())
        .filter(|rel| filter.as_ref().is_none_or(|flt| rel.to_string_lossy().contains(flt.as_str())))
        .collect();
    let count = selected.len();
    // Each test runs `gamma` in processes of its own, so tests run in parallel. The tests
    // that took longest last time start first, so that none of them is left running alone
    // at the end; tests without a time (new ones) start before all others.
    let times_file = Path::new(env!("CARGO_TARGET_TMPDIR")).join("conformance-times.txt");
    let mut times = previous_times(&times_file);
    let mut selected = selected;
    selected.sort_by_key(|rel| std::cmp::Reverse(times.get(&rel.display().to_string()).copied().unwrap_or(u64::MAX)));
    let next = std::sync::atomic::AtomicUsize::new(0);
    let failures = std::sync::Mutex::new(BTreeMap::new());
    let new_times = std::sync::Mutex::new(Vec::new());
    let workers = std::thread::available_parallelism().map_or(1, |n| n.get());
    std::thread::scope(|s| {
        for _ in 0..workers {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(rel) = selected.get(i) else { break };
                    let start = std::time::Instant::now();
                    if let Err(msg) = run_one(rel, bless) {
                        failures.lock().unwrap().insert(rel.display().to_string(), msg);
                    }
                    let ms = start.elapsed().as_millis() as u64;
                    new_times.lock().unwrap().push((rel.display().to_string(), ms));
                }
            });
        }
    });
    times.extend(new_times.into_inner().unwrap());
    let _ = fs::write(&times_file, times.iter().map(|(name, ms)| format!("{ms} {name}\n")).collect::<String>());
    let failures = failures.into_inner().unwrap();
    for (name, msg) in &failures {
        eprintln!("FAIL {}\n{}\n", name, msg);
    }
    eprintln!("conformance: {} passed, {} failed", count - failures.len(), failures.len());
    assert!(failures.is_empty(), "{} conformance test(s) failed", failures.len());
}
