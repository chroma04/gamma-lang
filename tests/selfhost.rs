//! Tests of the self-hosted compiler in `compiler/` (`gammac`), written in Gamma.
//!
//! The differential tests build `gammac` with `gamma build` (through the C back end when a
//! C compiler is available, else they run it with `gamma run`), run it over every `.gm`
//! file in the repository, and require output identical to the reference implementation's,
//! byte for byte: for `gammac lex`, the token dump of `gamma lex`, and for `gammac parse
//! --dump`, the syntax tree dump of `gamma parse --dump` (with and without the layout
//! recovery that tools use after errors); for `gammac modules`, run on the root of every
//! program in the repository, the module graph of `gamma modules`; for `gammac decls`, on
//! the same roots, what `gamma decls` prints for every declaration, on each program that
//! gammac does not report as using something it cannot check yet (their number may only
//! grow: see `DECLS_FLOOR`); and for `gammac tir`, likewise, the typed program of `gamma
//! tir` (see `TIR_FLOOR`). The diagnostics are
//! compared too, in the JSON format, which carries every field of a diagnostic: spans,
//! labels, notes and fixes.
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

/// The roots of every program in the repository, as `gamma check` and the test runners
/// load them: the conformance tests past the parse stage (a `.test` directory by its
/// `main/` module when it has one), the examples, the benchmarks, every module of the
/// standard library, and the self-hosted compiler.
fn program_roots() -> Vec<String> {
    let mut roots = Vec::new();
    let mut files = Vec::new();
    gm_files(Path::new("tests/conformance"), &mut files);
    for f in files {
        if let Some(i) = f.find(".test/") {
            let dir = &f[..i + 5];
            let main = format!("{}/main", dir);
            let module = if root().join(&main).is_dir() { main } else { dir.to_string() };
            if !roots.contains(&module) {
                roots.push(module);
            }
            continue;
        }
        let text = std::fs::read_to_string(root().join(&f)).unwrap();
        let header: Vec<&str> = text.lines().take_while(|l| l.starts_with("//!")).collect();
        if !header.iter().any(|l| matches!(l.trim(), "//! stage: lex" | "//! stage: parse")) {
            roots.push(f);
        }
    }
    let mut dirs = Vec::new();
    for top in ["examples", "bench", "lib/std"] {
        let mut gm = Vec::new();
        gm_files(Path::new(top), &mut gm);
        for f in gm {
            let dir = f.rsplit_once('/').map_or(".", |(d, _)| d).to_string();
            if top == "lib/std" {
                if !dirs.contains(&dir) {
                    dirs.push(dir);
                }
            } else if dir == top || dir == "bench/programs" {
                roots.push(f);
            } else if !dirs.contains(&dir) {
                dirs.push(dir);
            }
        }
    }
    roots.extend(dirs);
    roots.push("compiler".to_string());
    assert!(roots.len() > 500, "only {} program roots found", roots.len());
    roots
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
    // The standard library the reference implementation uses.
    cmd.env("GAMMA_STD", gamma::sema::std_root());
    common::output(&mut cmd).unwrap()
}

/// A stage of the front end that `gammac` implements, with the output it is compared on.
#[derive(Clone, Copy)]
enum Stage {
    /// `lex`: the token dump.
    Lex,
    /// `parse --dump`: the syntax tree dump.
    Parse,
    /// `modules`: the module graph of a program, from its root module.
    Modules,
    /// `decls`: the declarations of a program, from its root module.
    Decls,
    /// `tir`: the checked function bodies of a program, from its root module.
    Tir,
}

impl Stage {
    fn args(self) -> &'static [&'static str] {
        match self {
            Stage::Lex => &["lex"],
            Stage::Parse => &["parse", "--dump"],
            Stage::Modules => &["modules"],
            Stage::Decls => &["decls"],
            Stage::Tir => &["tir"],
        }
    }

    /// What the dump is called in messages.
    fn output(self) -> &'static str {
        match self {
            Stage::Lex => "tokens",
            Stage::Parse => "syntax tree",
            Stage::Modules => "module graph",
            Stage::Decls => "declarations",
            Stage::Tir => "typed program",
        }
    }
}

/// Runs `f` on a thread with the large stack the `gamma` command gives the compiler,
/// which checking deeply nested programs needs.
fn on_big_stack<R: Send>(f: impl FnOnce() -> R + Send) -> R {
    std::thread::scope(|s| std::thread::Builder::new().stack_size(1 << 30).spawn_scoped(s, f).unwrap().join().unwrap())
}

/// What `gamma lex` or `gamma parse --dump` prints for each file with
/// `--diag-format=json`, in the layout of `gammac` with several files: standard output,
/// standard error and the exit status.
fn reference(stage: Stage, files: &[String], recover: bool) -> (String, String, i32) {
    let (mut out, mut err, mut status) = (String::new(), String::new(), 0);
    for path in files {
        out.push_str(&format!("==> {} <==\n", path));
        err.push_str(&format!("==> {} <==\n", path));
        let mut sess = Session::new();
        sess.recover_layout = recover;
        if let Stage::Modules | Stage::Decls | Stage::Tir = stage {
            let dump = match stage {
                Stage::Modules => gamma::sema::load_modules(&mut sess, Path::new(path)),
                Stage::Decls => gamma::sema::declarations(&mut sess, Path::new(path)),
                _ => gamma::sema::typed_program(&mut sess, Path::new(path)),
            };
            out.push_str(&dump.unwrap_or_default());
            err.push_str(&sess.render_diagnostics(DiagFormat::Json));
            if sess.diags.has_errors() {
                status = 1;
            }
            continue;
        }
        let text = std::fs::read_to_string(root().join(path)).unwrap();
        // Diagnostics name the file as it is given on the command line.
        let file = sess.sources.add(Path::new(path), text);
        match stage {
            Stage::Lex => {
                let lexed = sess.lex(file);
                out.push_str(&driver::dump_tokens(&sess.sources, &lexed.tokens));
            }
            Stage::Parse => {
                let tree = sess.parse(file);
                out.push_str(&driver::dump_ast(&tree));
            }
            Stage::Modules | Stage::Decls | Stage::Tir => unreachable!(),
        }
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

fn check(stage: Stage, recover: bool) {
    // In batches, since a command line is limited in length (to 32 KiB on Windows).
    let inputs = if let Stage::Lex | Stage::Parse = stage { corpus() } else { program_roots() };
    for files in inputs.chunks(100) {
        let (out, err, status) = reference(stage, files, recover);
        let mut args: Vec<String> = stage.args().iter().map(|a| a.to_string()).collect();
        args.push("--diag-format=json".to_string());
        if recover {
            args.push("--recover".to_string());
        }
        args.extend(files.iter().cloned());
        let got = run_gammac(&args);
        let got_out = String::from_utf8_lossy(&got.stdout);
        let got_err = String::from_utf8_lossy(&got.stderr);
        for (what, expected, actual) in [(stage.output(), &out, &got_out), ("diagnostics", &err, &got_err)] {
            if let Some((file, line, e, a)) = first_difference(expected, actual) {
                panic!(
                    "gammac {}: the {} of {} differ (output line {}):\nexpected: {}\n  gammac: {}",
                    stage.args()[0],
                    what,
                    file,
                    line,
                    e,
                    a
                );
            }
        }
        assert_eq!(got.status.code(), Some(status), "exit status of gammac {} on {} …", stage.args()[0], files[0]);
    }
}

/// The number of programs whose declarations `gammac decls` must check without reporting
/// a construct it cannot check yet. Raise it as gammac learns more; it may not drop.
const DECLS_FLOOR: usize = 630;

/// The number of programs whose function bodies `gammac tir` must check without
/// reporting a construct it cannot check yet. Raise it as gammac learns more; it may not
/// drop.
const TIR_FLOOR: usize = 146;

/// The output for each file of a run over several, by the `==> FILE <==` lines.
fn sections(text: &str) -> Vec<(String, String)> {
    let mut out: Vec<(String, String)> = vec![];
    for line in text.split_inclusive('\n') {
        if let Some(name) = line.strip_prefix("==> ").and_then(|l| l.trim_end().strip_suffix(" <==")) {
            out.push((name.to_string(), String::new()));
        } else if let Some(last) = out.last_mut() {
            last.1.push_str(line);
        }
    }
    out
}

/// `gammac decls` or `gammac tir` against the reference, on the programs it can check;
/// there must be at least `floor` of them.
fn check_supported(stage: Stage, floor: usize) {
    let name = stage.args()[0];
    let mut checked = 0;
    let mut skipped = vec![];
    for files in program_roots().chunks(100) {
        let (out, err, _) = on_big_stack(|| reference(stage, files, false));
        let mut args = vec![name.to_string(), "--diag-format=json".to_string()];
        args.extend(files.iter().cloned());
        let got = run_gammac(&args);
        let (got_out, got_err) = (String::from_utf8_lossy(&got.stdout), String::from_utf8_lossy(&got.stderr));
        let (out, err, got_out, got_err) = (sections(&out), sections(&err), sections(&got_out), sections(&got_err));
        assert_eq!(
            got_err.len(),
            files.len(),
            "gammac {} did not report on every program of the batch of {}",
            name,
            files[0]
        );
        for i in 0..files.len() {
            if got_err[i].1.contains("gammac cannot") {
                skipped.push(files[i].clone());
                continue;
            }
            checked += 1;
            for (what, expected, actual) in
                [(stage.output(), &out[i].1, &got_out[i].1), ("diagnostics", &err[i].1, &got_err[i].1)]
            {
                if let Some((_, line, e, a)) = first_difference(expected, actual) {
                    panic!(
                        "gammac {}: the {} of {} differ (line {}):\nexpected: {}\n  gammac: {}",
                        name, what, files[i], line, e, a
                    );
                }
            }
        }
    }
    assert!(
        checked >= floor,
        "gammac {} checked {} programs, fewer than the {} it checked before; it now skips {:?}",
        name,
        checked,
        floor,
        skipped
    );
    eprintln!("gammac {}: {} programs checked, {} skipped", name, checked, skipped.len());
}

#[test]
fn declaration_checking_matches_the_reference_on_the_programs_gammac_supports() {
    check_supported(Stage::Decls, DECLS_FLOOR);
}

#[test]
fn body_checking_matches_the_reference_on_the_programs_gammac_supports() {
    check_supported(Stage::Tir, TIR_FLOOR);
}

#[test]
fn lexer_matches_the_reference_on_the_whole_repository() {
    check(Stage::Lex, false);
}

#[test]
fn recovering_lexer_matches_the_reference_on_the_whole_repository() {
    check(Stage::Lex, true);
}

#[test]
fn parser_matches_the_reference_on_the_whole_repository() {
    check(Stage::Parse, false);
}

#[test]
fn parser_after_layout_recovery_matches_the_reference_on_the_whole_repository() {
    check(Stage::Parse, true);
}

#[test]
fn module_loading_matches_the_reference_on_every_program() {
    check(Stage::Modules, false);
}

#[test]
fn usage_errors() {
    let got = run_gammac(&["check".to_string()]);
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
