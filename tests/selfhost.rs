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
//! On Unix, `gammac run` compiles every conformance `run` test through its own C back end
//! and runs it, and the programs must give the expected output and exit status, as the
//! reference's do. `gammac check` must report what `gamma check` reports on every program,
//! in the human format.
//! Finally `gammac` compiles itself: the `gammac` it builds (stage 2) must generate exactly the
//! C that the `gammac` built by the reference implementation (stage 1) generated for it.
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
    gammac_env(&mut cmd);
    common::output(&mut cmd).unwrap()
}

/// The standard library and the runtime the reference implementation uses.
fn gammac_env(cmd: &mut Command) {
    cmd.env("GAMMA_STD", gamma::sema::std_root());
    cmd.env("GAMMA_RUNTIME", root().join("runtime"));
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
    /// `check`: the diagnostics of checking a program, in the human format.
    Check,
}

impl Stage {
    fn args(self) -> &'static [&'static str] {
        match self {
            Stage::Lex => &["lex"],
            Stage::Parse => &["parse", "--dump"],
            Stage::Modules => &["modules"],
            Stage::Decls => &["decls"],
            Stage::Tir => &["tir"],
            Stage::Check => &["check"],
        }
    }

    /// The format the diagnostics are compared in: JSON, which carries every part of a
    /// diagnostic, except for `check`, which tests the human format.
    fn format(self) -> DiagFormat {
        if let Stage::Check = self { DiagFormat::Human } else { DiagFormat::Json }
    }

    /// What the dump is called in messages.
    fn output(self) -> &'static str {
        match self {
            Stage::Lex => "tokens",
            Stage::Parse => "syntax tree",
            Stage::Modules => "module graph",
            Stage::Decls => "declarations",
            Stage::Tir => "typed program",
            Stage::Check => "output",
        }
    }
}

/// Runs `f` on a thread with the large stack the `gamma` command gives the compiler,
/// which checking deeply nested programs needs.
fn on_big_stack<R: Send>(f: impl FnOnce() -> R + Send) -> R {
    std::thread::scope(|s| std::thread::Builder::new().stack_size(1 << 30).spawn_scoped(s, f).unwrap().join().unwrap())
}

/// What `gamma <stage>` prints for each file with the stage's diagnostic format, in the
/// layout of `gammac` with several files: standard output,
/// standard error and the exit status.
fn reference(stage: Stage, files: &[String], recover: bool) -> (String, String, i32) {
    let (mut out, mut err, mut status) = (String::new(), String::new(), 0);
    for path in files {
        out.push_str(&format!("==> {} <==\n", path));
        err.push_str(&format!("==> {} <==\n", path));
        let mut sess = Session::new();
        sess.recover_layout = recover;
        if let Stage::Modules | Stage::Decls | Stage::Tir | Stage::Check = stage {
            let dump = match stage {
                Stage::Modules => gamma::sema::load_modules(&mut sess, Path::new(path)),
                Stage::Decls => gamma::sema::declarations(&mut sess, Path::new(path)),
                Stage::Check => gamma::sema::analyze(&mut sess, Path::new(path)).map(|_| String::new()),
                _ => gamma::sema::typed_program(&mut sess, Path::new(path)),
            };
            out.push_str(&dump.unwrap_or_default());
            err.push_str(&sess.render_diagnostics(stage.format()));
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
            Stage::Modules | Stage::Decls | Stage::Tir | Stage::Check => unreachable!(),
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
        let (out, err, status) = on_big_stack(|| reference(stage, files, recover));
        let mut args: Vec<String> = stage.args().iter().map(|a| a.to_string()).collect();
        args.push(
            if let DiagFormat::Human = stage.format() { "--diag-format=human" } else { "--diag-format=json" }
                .to_string(),
        );
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
const DECLS_FLOOR: usize = 800;

/// The number of programs whose function bodies `gammac tir` must check without
/// reporting a construct it cannot check yet. Raise it as gammac learns more; it may not
/// drop.
const TIR_FLOOR: usize = 800;

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
fn checking_matches_the_reference_on_every_program() {
    check(Stage::Check, false);
}

#[test]
fn usage_errors() {
    for args in [&["check"][..], &["frobnicate", "x.gm"], &["build", "--mode=slow", "x.gm"]] {
        let got = run_gammac(&args.iter().map(|a| a.to_string()).collect::<Vec<_>>());
        assert_eq!(got.status.code(), Some(2), "gammac {:?}", args);
        assert!(String::from_utf8_lossy(&got.stderr).contains("\nusage: gammac"), "gammac {:?}", args);
    }
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

/// A conformance `run` test that `gammac build` compiles: its root, build mode, standard
/// input, expected output and exit status (see `tests/conformance.rs` for the directives).
#[cfg(unix)]
struct RunTest {
    name: String,
    root: PathBuf,
    mode: String,
    stdin: Vec<u8>,
    stdout: Vec<u8>,
    exit: i32,
}

/// The `run` tests that expect no compile errors and apply to this host.
#[cfg(unix)]
fn run_tests() -> Vec<RunTest> {
    let dir = root().join("tests/conformance/run");
    let mut entries: Vec<PathBuf> = std::fs::read_dir(&dir).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    let mut tests = vec![];
    for p in entries {
        let name = p.file_name().unwrap().to_str().unwrap().to_string();
        let base = if let Some(b) = name.strip_suffix(".gm") {
            b.to_string()
        } else if let Some(b) = name.strip_suffix(".test") {
            b.to_string()
        } else {
            continue;
        };
        let mut text = String::new();
        if p.is_dir() {
            let mut files = vec![];
            gm_files(p.strip_prefix(root()).unwrap(), &mut files);
            for f in files {
                text.push_str(&std::fs::read_to_string(root().join(f)).unwrap());
            }
        } else {
            text = std::fs::read_to_string(&p).unwrap();
        }
        let directive = |key: &str| {
            text.lines()
                .find_map(|l| l.strip_prefix("//! ").and_then(|r| r.strip_prefix(key)).map(|v| v.trim().to_string()))
        };
        if text.contains("//~") || directive("stage:").is_some_and(|s| s != "run") {
            continue;
        }
        let host = [
            ("arch:", std::env::consts::ARCH.to_string()),
            ("endian:", if cfg!(target_endian = "little") { "little" } else { "big" }.to_string()),
            ("os:", if cfg!(windows) { "windows" } else { "unix" }.to_string()),
            ("ptr_bits:", (8 * std::mem::size_of::<usize>()).to_string()),
        ];
        if host.iter().any(|(k, v)| directive(k).is_some_and(|d| &d != v)) {
            continue;
        }
        let mode = directive("mode:").unwrap_or_else(|| "debug".to_string());
        if mode == "safe" {
            continue;
        }
        let root_dir = if p.is_dir() && p.join("main").is_dir() { p.join("main") } else { p.clone() };
        let read = |ext: &str| std::fs::read(dir.join(format!("{}.{}", base, ext))).unwrap_or_default();
        tests.push(RunTest {
            name: format!("tests/conformance/run/{}", name),
            root: root_dir,
            mode,
            stdin: read("stdin"),
            stdout: read("stdout"),
            exit: directive("exit:").map_or(0, |e| e.parse().unwrap()),
        });
    }
    tests
}

/// Runs `t` with `gammac run`, which compiles it to a temporary executable; returns why it
/// failed, if it did.
#[cfg(unix)]
fn build_and_run(gammac: &Path, t: &RunTest, dir: &Path) -> Option<String> {
    let stdin = dir.join(format!("{}.stdin", t.name.replace('/', "_")));
    std::fs::write(&stdin, &t.stdin).unwrap();
    let mut cmd = Command::new(gammac);
    cmd.current_dir(root()).arg("run").arg(format!("--mode={}", t.mode)).arg(&t.root);
    cmd.stdin(std::fs::File::open(&stdin).unwrap());
    gammac_env(&mut cmd);
    let ran = common::output(&mut cmd).unwrap();
    let _ = std::fs::remove_file(&stdin);
    if ran.status.code() != Some(t.exit) {
        return Some(format!(
            "exit status {:?}, expected {}:\n{}",
            ran.status.code(),
            t.exit,
            String::from_utf8_lossy(&ran.stderr)
        ));
    }
    if ran.stdout != t.stdout {
        let (e, a) = (String::from_utf8_lossy(&t.stdout), String::from_utf8_lossy(&ran.stdout));
        let (_, line, e, a) = first_difference(&e, &a).unwrap_or_default();
        return Some(format!("output differs at line {}:\nexpected: {}\n  actual: {}", line, e, a));
    }
    None
}

#[test]
#[cfg(unix)]
fn programs_compiled_by_gammac_behave_as_expected() {
    let Gammac::Native(gammac) = gammac() else { return };
    let tests = run_tests();
    assert!(tests.len() > 400, "only {} run tests found", tests.len());
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-run");
    std::fs::create_dir_all(&dir).unwrap();
    let next = std::sync::atomic::AtomicUsize::new(0);
    let failures = std::sync::Mutex::new(vec![]);
    let workers = std::thread::available_parallelism().map_or(4, |n| n.get());
    std::thread::scope(|s| {
        for _ in 0..workers {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(t) = tests.get(i) else { break };
                    if let Some(why) = build_and_run(gammac, t, &dir) {
                        failures.lock().unwrap().push(format!("{}: {}", t.name, why));
                    }
                }
            });
        }
    });
    let mut failures = failures.into_inner().unwrap();
    failures.sort();
    assert!(failures.is_empty(), "{} of {} programs failed:\n{}", failures.len(), tests.len(), failures.join("\n\n"));
}

/// `gammac build --emit-c compiler` with `gammac`: the C it generates for itself.
#[cfg(unix)]
fn self_c(gammac: &Path, out: &Path) -> String {
    let mut cmd = Command::new(gammac);
    cmd.current_dir(root()).args(["build", "--emit-c", "-o"]).arg(out).arg("compiler");
    gammac_env(&mut cmd);
    let built = common::output(&mut cmd).unwrap();
    assert!(
        built.status.success(),
        "{} failed to compile gammac:\n{}",
        gammac.display(),
        String::from_utf8_lossy(&built.stderr)
    );
    std::fs::read_to_string(format!("{}.c", out.display())).unwrap()
}

#[test]
#[cfg(unix)]
fn gammac_compiles_itself_to_a_fixed_point() {
    let Gammac::Native(stage1) = gammac() else { return };
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-bootstrap");
    std::fs::create_dir_all(&dir).unwrap();
    // Stage 1 (built by the reference implementation) builds stage 2.
    let stage2 = dir.join("gammac2");
    let mut cmd = Command::new(stage1);
    cmd.current_dir(root()).args(["build", "-o"]).arg(&stage2).arg("compiler");
    gammac_env(&mut cmd);
    let built = common::output(&mut cmd).unwrap();
    assert!(built.status.success(), "gammac failed to compile itself:\n{}", String::from_utf8_lossy(&built.stderr));
    let c1 = std::fs::read_to_string(format!("{}.c", stage2.display())).unwrap();
    // Stage 2 generates the same C for itself.
    let c2 = self_c(&stage2, &dir.join("gammac3"));
    if let Some((_, line, a, b)) = first_difference(&c1, &c2) {
        panic!("the C that stage 2 generates differs from stage 1's at line {}:\nstage 1: {}\nstage 2: {}", line, a, b);
    }
}

/// `gammac fmt -` on `input`: standard output, standard error and exit status.
fn gammac_fmt_filter(input: &[u8]) -> (Vec<u8>, Vec<u8>, i32) {
    use std::io::Write;
    use std::process::Stdio;
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
    cmd.args(["fmt", "-"]);
    gammac_env(&mut cmd);
    let mut child = cmd.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().unwrap();
    let mut stdin = child.stdin.take().unwrap();
    let data = input.to_vec();
    let writer = std::thread::spawn(move || {
        let _ = stdin.write_all(&data);
    });
    let out = child.wait_with_output().unwrap();
    writer.join().unwrap();
    (out.stdout, out.stderr, out.status.code().unwrap_or(-1))
}

/// What `gamma fmt -` gives for `src`, as `gammac fmt -` must.
fn reference_fmt_filter(src: &str) -> (Vec<u8>, Vec<u8>, i32) {
    match gamma::fmt::format_source(src) {
        Ok(out) => (out.into_bytes(), Vec::new(), 0),
        Err(e) => (Vec::new(), format!("<stdin>:{}\n", e).into_bytes(), 1),
    }
}

/// Formats each input with both formatters, and fails on the first disagreement. Returns
/// how many there were.
fn check_fmt_filter(inputs: impl Iterator<Item = (String, String)>) -> usize {
    let mut n = 0;
    for (name, src) in inputs {
        let want = reference_fmt_filter(&src);
        let got = gammac_fmt_filter(src.as_bytes());
        if want != got {
            let (w, g) = (String::from_utf8_lossy(&want.0).into_owned(), String::from_utf8_lossy(&got.0).into_owned());
            let line = first_difference(&w, &g).map_or(String::new(), |(_, line, a, b)| {
                format!("\nfirst difference at line {}:\nreference: {}\ngammac:    {}", line, a, b)
            });
            panic!(
                "{}: gammac fmt differs from the reference\nreference: status {}, stderr {:?}\ngammac:    status {}, stderr {:?}{}",
                name,
                want.2,
                String::from_utf8_lossy(&want.1),
                got.2,
                String::from_utf8_lossy(&got.1),
                line
            );
        }
        n += 1;
    }
    n
}

#[test]
fn formatter_matches_the_reference_on_the_whole_repository() {
    let inputs = corpus().into_iter().map(|f| {
        let src = std::fs::read_to_string(root().join(&f)).unwrap();
        (f, src)
    });
    assert!(check_fmt_filter(inputs) > 500);
}

#[test]
fn formatter_matches_the_reference_on_unformatted_text() {
    // Variants of every file that the formatter has to change: no indentation, runs of
    // spaces, spaces inside brackets, line ends as CR LF, and a cut in the middle (which
    // may end in a lexical error).
    let mut inputs = Vec::new();
    for (i, f) in corpus().into_iter().enumerate() {
        let src = std::fs::read_to_string(root().join(&f)).unwrap();
        let noindent: String = src.split('\n').map(|l| l.trim_start()).collect::<Vec<_>>().join("\n");
        let mut collapsed = String::new();
        for c in src.chars() {
            if !(c == ' ' && collapsed.ends_with(' ')) {
                collapsed.push(c);
            }
        }
        let spaced: String =
            src.chars().flat_map(|c| if "(){},;[]".contains(c) { vec![' ', c, ' '] } else { vec![c] }).collect();
        let cut = src.char_indices().map(|(j, _)| j).nth(src.len() * (i % 7 + 1) / 8 % src.len().max(1)).unwrap_or(0);
        inputs.push((format!("{} (no indentation)", f), noindent));
        inputs.push((format!("{} (spaces collapsed)", f), collapsed));
        inputs.push((format!("{} (spaced brackets)", f), spaced));
        inputs.push((format!("{} (CR LF)", f), src.replace('\n', "\r\n")));
        inputs.push((format!("{} (cut)", f), src[..cut].to_string()));
    }
    assert!(check_fmt_filter(inputs.into_iter()) > 2500);
}

#[test]
fn formatting_files_and_directories_matches_the_reference() {
    let base = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-fmt");
    let setup = |dir: &Path| {
        let _ = std::fs::remove_dir_all(dir);
        let ugly = "main::fn(){\n   x:=1\n}\n";
        for d in ["src", "src/sub", ".git", "target", "src/target", ".hidden/x"] {
            std::fs::create_dir_all(dir.join(d)).unwrap();
            std::fs::write(dir.join(d).join("a.gm"), ugly).unwrap();
        }
        std::fs::write(dir.join("src/good.gm"), "main :: fn() {\n    x := 1\n}\n").unwrap();
        std::fs::write(dir.join("src/broken.gm"), "x := \"unterminated\n").unwrap();
        std::fs::write(dir.join("src/sub/notes.txt"), ugly).unwrap();
        std::fs::write(dir.join("gamma.toml"), "[package]\nname = \"w\"\nversion = \"0.1.0\"\n").unwrap();
    };
    // Runs the reference or `gammac` on paths below `dir`; the output names `dir` as `DIR`.
    let run = |reference: bool, args: &[&str], dir: &Path| {
        let mut cmd = match (reference, gammac()) {
            (false, Gammac::Native(exe)) => Command::new(exe),
            (false, Gammac::Interpreted) => {
                let mut c = gamma();
                c.args(["run", "compiler"]);
                c
            }
            (true, _) => gamma(),
        };
        cmd.arg("fmt");
        for a in args {
            if a.starts_with('-') {
                cmd.arg(a);
            } else {
                cmd.arg(dir.join(a));
            }
        }
        gammac_env(&mut cmd);
        let out = common::output(&mut cmd).unwrap();
        let fix =
            |s: &[u8]| String::from_utf8_lossy(s).replace(dir.to_str().unwrap(), "DIR").replace("gammac:", "gamma:");
        (fix(&out.stdout), fix(&out.stderr), out.status.code())
    };
    let contents = |dir: &Path| {
        let mut out = Vec::new();
        let mut stack = vec![dir.to_path_buf()];
        while let Some(d) = stack.pop() {
            for e in std::fs::read_dir(&d).unwrap() {
                let p = e.unwrap().path();
                if p.is_dir() {
                    stack.push(p);
                } else {
                    out.push((p.strip_prefix(dir).unwrap().to_path_buf(), std::fs::read(&p).unwrap()));
                }
            }
        }
        out.sort();
        out
    };
    let cases: [&[&str]; 7] = [
        &["--check", "."],
        &["."],
        &["src"],
        &["--check", "src/good.gm"],
        &["--check", "src/a.gm", "src/broken.gm"],
        &["nothing"],
        &["--check", "src", "src/sub"],
    ];
    for args in cases {
        let (r, c) = (base.join("reference"), base.join("gammac"));
        setup(&r);
        setup(&c);
        let want = run(true, args, &r);
        let got = run(false, args, &c);
        assert_eq!(want, got, "fmt {:?}", args);
        assert_eq!(contents(&r), contents(&c), "files after fmt {:?}", args);
    }
}

// ---------------------------------------------------------------------------------
// gammac doc
// ---------------------------------------------------------------------------------

/// Every file below `dir`, as (path relative to `dir` with `/`, contents), in path order.
fn tree(dir: &Path) -> Vec<(String, Vec<u8>)> {
    fn walk(dir: &Path, rel: &str, out: &mut Vec<(String, Vec<u8>)>) {
        let Ok(rd) = std::fs::read_dir(dir) else { return };
        for e in rd.flatten() {
            let name = format!("{}{}", rel, e.file_name().to_string_lossy());
            if e.path().is_dir() {
                walk(&e.path(), &format!("{}/", name), out);
            } else {
                out.push((name, std::fs::read(e.path()).unwrap()));
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, "", &mut out);
    out.sort();
    out
}

/// A fresh directory for the output of one run of `doc`, below the tests' temporary directory.
fn doc_dir(name: &str) -> PathBuf {
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-doc").join(name);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// Runs `gamma doc` and `gammac doc` with `args`, in which `OUT` is replaced by an output
/// path of its own for each, and requires the same standard output, standard error (the
/// reference calls itself `gamma`), exit status and files written.
fn doc_matches(name: &str, args: &[&str]) {
    let run = |who: &str| {
        let out = doc_dir(&format!("{}-{}", name, who));
        let out_str = out.to_str().unwrap();
        let args: Vec<String> = args.iter().map(|a| a.replace("OUT", out_str)).collect();
        let got = if who == "gamma" {
            let mut cmd = gamma();
            cmd.arg("doc").args(&args);
            common::output(&mut cmd).unwrap()
        } else {
            let mut full = vec!["doc".to_string()];
            full.extend(args);
            run_gammac(&full)
        };
        let text = |bytes: &[u8]| String::from_utf8_lossy(bytes).replace(out_str, "OUT");
        // The reference names itself in its messages.
        let stderr: String = text(&got.stderr)
            .lines()
            .map(|l| {
                let l = l.strip_prefix("gamma: ").map_or(l.to_string(), |r| format!("gammac: {}", r));
                format!("{}\n", l.replace("usage: gamma doc", "usage: gammac doc"))
            })
            .collect();
        (text(&got.stdout), stderr, got.status.code(), tree(&out))
    };
    let (want, got) = (run("gamma"), run("gammac"));
    assert_eq!(want.0, got.0, "doc {}: standard output differs", name);
    assert_eq!(want.1, got.1, "doc {}: standard error differs", name);
    assert_eq!(want.2, got.2, "doc {}: exit status differs", name);
    assert_eq!(want.3.len(), got.3.len(), "doc {}: different files written", name);
    for ((p, a), (q, b)) in want.3.iter().zip(&got.3) {
        assert_eq!(p, q, "doc {}: different files written", name);
        assert!(a == b, "doc {}: {} differs from the reference's", name, p);
    }
}

#[test]
fn doc_matches_the_reference_on_the_standard_library() {
    for format in ["markdown", "html"] {
        let fmt = format!("--format={}", format);
        doc_matches(&format!("std-{}", format), &["lib/std", "--all", &fmt, "-o", "OUT"]);
    }
    doc_matches("std-missing", &["lib/std", "--all", "--missing", "-o", "OUT"]);
}

#[test]
fn doc_matches_the_reference_on_single_modules() {
    let modules = [
        "lib/std/io",
        "lib/std/str",
        "lib/std/fs",
        "lib/std/collections/hash_map",
        "lib/core",
        "compiler/doc",
        "compiler/ast",
        "compiler/load",
        "compiler/main.gm",
        "tests/doc/fixture/shapes",
        "tests/doc/fixture/shapes/extra.gm",
    ];
    for (i, module) in modules.iter().enumerate() {
        for format in ["markdown", "html"] {
            let fmt = format!("--format={}", format);
            doc_matches(&format!("module{}-{}", i, format), &[module, &fmt]);
            doc_matches(&format!("module{}-{}-missing", i, format), &[module, &fmt, "--missing"]);
        }
    }
}

#[test]
fn doc_matches_the_reference_on_trees_and_files() {
    doc_matches("fixture-tree", &["tests/doc/fixture", "--all", "--format=html", "-o", "OUT"]);
    doc_matches("fixture-file", &["tests/doc/fixture/shapes", "-o", "OUT/shapes.md"]);
    doc_matches("fixture-dir", &["tests/doc/fixture/shapes", "-o", "OUT/sub/dir"]);
    doc_matches("compiler-tree", &["compiler", "--all", "-o", "OUT"]);
}

#[test]
fn doc_matches_the_reference_on_errors() {
    let cases: [&[&str]; 11] = [
        &["--format=pdf", "x"],
        &["tests/doc/fixture/shapes", "--format"],
        &["tests/doc/fixture/shapes", "-o"],
        &["tests/doc/fixture/shapes", "--bogus"],
        &["tests/doc/fixture/shapes", "lib/std/io"],
        &[],
        &["lib/std", "--all"],
        &["no/such/dir"],
        &["lib", "--missing"],
        &["tests", "--all", "-o", "OUT"],
        &["tests/doc/fixture/shapes", "-o", "/proc/nope/x"],
    ];
    for (i, args) in cases.iter().enumerate() {
        doc_matches(&format!("error{}", i), args);
    }
    // Sources that do not parse, and one that is not text.
    let dir = doc_dir("sources");
    std::fs::create_dir_all(dir.join("broken")).unwrap();
    std::fs::create_dir_all(dir.join("binary")).unwrap();
    std::fs::write(dir.join("broken/broken.gm"), "pub f :: fn( {\n").unwrap();
    std::fs::write(dir.join("binary/bad.gm"), b"pub g :: fn() {}\n\xff\n").unwrap();
    for sub in ["broken", "binary"] {
        // The diagnostics name the files as they are given.
        let path = dir.join(sub).to_str().unwrap().to_string();
        let want = common::output(gamma().arg("doc").arg(&path)).unwrap();
        let got = run_gammac(&["doc".to_string(), path]);
        assert_eq!(want.stdout, got.stdout);
        assert_eq!(want.stderr, got.stderr, "doc {}: standard error differs", sub);
        assert_eq!(want.status.code(), got.status.code());
        assert_eq!(got.status.code(), Some(1));
    }
}

/// Runs `f` on every item with one worker per processor; the results come back in order.
#[cfg(unix)]
fn in_parallel<T: Sync, R: Send>(items: &[T], f: impl Fn(&T) -> R + Sync) -> Vec<R> {
    let next = std::sync::atomic::AtomicUsize::new(0);
    let results = std::sync::Mutex::new(Vec::new());
    let workers = std::thread::available_parallelism().map_or(4, |n| n.get());
    std::thread::scope(|s| {
        for _ in 0..workers {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(item) = items.get(i) else { break };
                    let r = f(item);
                    results.lock().unwrap().push((i, r));
                }
            });
        }
    });
    let mut results = results.into_inner().unwrap();
    results.sort_by_key(|(i, _)| *i);
    results.into_iter().map(|(_, r)| r).collect()
}

/// What a command printed and how it ended.
#[cfg(unix)]
type Ran = (String, String, Option<i32>);

#[cfg(unix)]
fn ran(out: std::process::Output) -> Ran {
    (
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
        out.status.code(),
    )
}

/// Runs `gammac` natively with `args` in `dir`.
#[cfg(unix)]
fn gammac_in(exe: &Path, dir: &Path, args: &[&str]) -> Ran {
    let mut cmd = Command::new(exe);
    cmd.current_dir(dir).args(args);
    gammac_env(&mut cmd);
    ran(common::output(&mut cmd).unwrap())
}

/// Runs `gamma` with `args` in `dir`.
#[cfg(unix)]
fn gamma_in(dir: &Path, args: &[&str]) -> Ran {
    let mut cmd = gamma();
    cmd.current_dir(dir).args(args);
    ran(common::output(&mut cmd).unwrap())
}

/// Why `actual` differs from `expected`, if it does.
#[cfg(unix)]
fn difference(what: &str, name: &str, expected: &Ran, actual: &Ran) -> Option<String> {
    for (part, e, a) in [("standard output", &expected.0, &actual.0), ("diagnostics", &expected.1, &actual.1)] {
        if let Some((_, line, e, a)) = first_difference(e, a) {
            return Some(format!(
                "{}: the {} of {} differ (line {}):\nexpected: {}\n  gammac: {}",
                what, part, name, line, e, a
            ));
        }
    }
    (expected.2 != actual.2)
        .then(|| format!("{}: exit status on {}: expected {:?}, gammac {:?}", what, name, expected.2, actual.2))
}

#[test]
#[cfg(unix)]
fn lint_matches_the_reference_on_every_program() {
    let Gammac::Native(exe) = gammac() else { return };
    let roots = program_roots();
    for levels in [None, Some("--warn=all")] {
        let failures: Vec<String> = in_parallel(&roots, |r| {
            let mut args = vec!["lint", "--diag-format=json"];
            args.extend(levels);
            args.push(r);
            difference("gammac lint", r, &gamma_in(root(), &args), &gammac_in(exe, root(), &args))
        })
        .into_iter()
        .flatten()
        .collect();
        assert!(
            failures.is_empty(),
            "{} of {} programs differ:\n{}",
            failures.len(),
            roots.len(),
            failures.join("\n\n")
        );
    }
}

/// The options that a conformance test's `//! lint:` lines give.
#[cfg(unix)]
fn lint_options(file: &Path) -> Vec<String> {
    let text = std::fs::read_to_string(file).unwrap();
    text.lines()
        .filter_map(|l| l.strip_prefix("//! lint:"))
        .flat_map(|o| o.split_whitespace().map(String::from).collect::<Vec<_>>())
        .collect()
}

/// Runs a command on a scratch copy of `file` as `main.gm` (with `gammac` if `exe` is
/// given, else with `gamma`), and gives what it printed and the file afterwards.
#[cfg(unix)]
fn fixed_copy(exe: Option<&Path>, file: &Path, name: &str, args: &[String]) -> (Ran, String) {
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-fix").join(name);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::copy(file, dir.join("main.gm")).unwrap();
    let args: Vec<&str> = args.iter().map(String::as_str).collect();
    let out = match exe {
        Some(exe) => gammac_in(exe, &dir, &args),
        None => gamma_in(&dir, &args),
    };
    (out, std::fs::read_to_string(dir.join("main.gm")).unwrap())
}

#[test]
#[cfg(unix)]
fn fixing_matches_the_reference() {
    let Gammac::Native(exe) = gammac() else { return };
    let mut files = Vec::new();
    for dir in ["tests/conformance/lint", "tests/conformance/check"] {
        gm_files(Path::new(dir), &mut files);
    }
    // Single-file programs only: a `.test` directory is several modules.
    files.retain(|f| !f.contains(".test/"));
    let commands: [&[&str]; 2] = [&["fix"], &["lint", "--fix"]];
    let failures: Vec<String> = in_parallel(&files, |f| {
        let path = root().join(f);
        let mut failures = Vec::new();
        for (i, command) in commands.iter().enumerate() {
            let mut args: Vec<String> = command.iter().map(|a| a.to_string()).collect();
            if command[0] == "lint" {
                args.extend(lint_options(&path));
            }
            args.push("main.gm".to_string());
            let name = format!("{}-{}", f.replace('/', "_"), i);
            let (expected, expected_text) = fixed_copy(None, &path, &format!("{}-ref", name), &args);
            let (actual, actual_text) = fixed_copy(Some(exe), &path, &format!("{}-gammac", name), &args);
            if let Some(why) = difference(&command.join(" "), f, &expected, &actual) {
                failures.push(why);
            } else if let Some((_, line, e, a)) = first_difference(&expected_text, &actual_text) {
                failures.push(format!(
                    "{}: the fixed {} differs (line {}):\nexpected: {}\n  gammac: {}",
                    command.join(" "),
                    f,
                    line,
                    e,
                    a
                ));
            }
        }
        failures
    })
    .into_iter()
    .flatten()
    .collect();
    assert!(failures.is_empty(), "{} of {} programs differ:\n{}", failures.len(), files.len(), failures.join("\n\n"));
}

/// The program of `gammac_debug_information_maps_to_the_source`: line 10 is `total += i`
/// and line 26 the `print` that ends `main`, where its locals can be inspected.
#[cfg(unix)]
const DEBUG_PROGRAM: &str = "\
Shape :: union {
    Circle: f64,
    Rect: (w: i64, h: i64),
    Empty,
}

sum :: fn(n: i64) -> i64 {
    total := 0
    for i in 0..n {
        total += i
    }
    return total
}

Point :: struct { x, y: i64 }

main :: fn() needs Out {
    p := Point(x: 3, y: 4)
    rect := Shape.Rect(w: 2, h: 3)
    xs := [3]i64(10, 20, 30)
    slice := xs[0..3]
    word := \"hello\"
    some: ?i64 = 5
    pair := (1, true)
    stdout := sum(10)
    print(\"{} {} {}\\n\", p.x + p.y, stdout, slice.len)
}
";

/// The values `print`ed at the end of `main` in `DEBUG_PROGRAM`.
#[cfg(unix)]
const DEBUG_PRINTS: [&str; 8] = ["F.p", "F.p.y", "F.rect", "F.slice", "F.word", "F.some", "F.pair", "F.stdout"];

/// Runs gdb in batch mode on `exe`, stopped at `breakpoint` (`file:line`), printing `exprs`;
/// returns the `$N = …` lines and the first line of the backtrace, or `None` if gdb cannot
/// trace processes here.
#[cfg(unix)]
fn gdb_session(exe: &Path, breakpoint: &str, exprs: &[&str]) -> Option<(Vec<String>, String)> {
    let mut cmd = Command::new("gdb");
    cmd.args(["-batch", "-nx", "-iex"]).arg(format!("add-auto-load-safe-path {}", exe.parent().unwrap().display()));
    cmd.args(["-ex", &format!("break {breakpoint}"), "-ex", "run", "-ex", "backtrace 1"]);
    for e in exprs {
        cmd.args(["-ex", &format!("print {e}")]);
    }
    let out = cmd.arg(exe).output().ok()?;
    let text = String::from_utf8_lossy(&out.stdout).into_owned();
    if !out.status.success() || text.contains("ptrace") || text.contains("Operation not permitted") {
        eprintln!("note: skipping the debugger session: gdb cannot trace processes here");
        return None;
    }
    let values = text.lines().filter(|l| l.starts_with('$')).map(String::from).collect();
    let frame = text.lines().find(|l| l.starts_with("#0")).unwrap_or_default().to_string();
    Some((values, frame))
}

/// `gammac build -g`: the C maps to the Gamma source with `#line` directives, frame
/// fields have the names of the locals, and the lldb formatters are written next to the
/// executable; with gdb installed, a breakpoint by `file.gm:line` stops, locals print by
/// their Gamma names, and the values print through the pretty printers exactly as they do
/// in an executable built by the reference implementation.
#[test]
#[cfg(unix)]
fn gammac_debug_information_maps_to_the_source() {
    let Gammac::Native(gammac) = gammac() else { return };
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-debug");
    let _ = std::fs::remove_dir_all(&dir);
    for flavor in ["ref", "gammac", "plain"] {
        std::fs::create_dir_all(dir.join(flavor)).unwrap();
    }
    let src = dir.join("debuggee.gm");
    std::fs::write(&src, DEBUG_PROGRAM).unwrap();
    let build = |flavor: &str, args: &[&str]| {
        let exe = dir.join(flavor).join("debuggee");
        let mut cmd = if flavor == "ref" { gamma() } else { Command::new(gammac) };
        cmd.current_dir(root()).arg("build").args(args).arg("-o").arg(&exe).arg(&src);
        gammac_env(&mut cmd);
        let built = common::output(&mut cmd).unwrap();
        assert!(built.status.success(), "{flavor} build failed:\n{}", String::from_utf8_lossy(&built.stderr));
        let ran = common::output(&mut Command::new(&exe)).unwrap();
        assert_eq!(String::from_utf8_lossy(&ran.stdout), "7 45 3\n", "{flavor}: the program misbehaves");
        exe
    };
    let reference = build("ref", &["-g"]);
    let exe = build("gammac", &["-g"]);
    let plain = build("plain", &[]);

    // The C names the source file and line, and the local `total` by its Gamma name.
    let c = std::fs::read_to_string(format!("{}.c", exe.display())).unwrap();
    let directive = format!("#line 10 \"{}\"", std::fs::canonicalize(&src).unwrap().display());
    assert!(c.contains(&directive), "no `{directive}` in the generated C");
    assert!(c.contains(" total; };"), "the frame field of `total` has no name");
    assert!(c.contains("_union {") && c.contains("_slice {"), "the frame has no debugger views");
    let plain_c = std::fs::read_to_string(format!("{}.c", plain.display())).unwrap();
    assert!(!plain_c.contains("#line") && !plain_c.contains("gm_dv"), "a build without -g has debug information");
    // The lldb formatters come with a build with debug information only.
    assert!(exe.with_file_name("gamma_lldb.py").is_file(), "no gamma_lldb.py next to the executable");
    assert!(!plain.with_file_name("gamma_lldb.py").exists());
    // The printers are embedded where gdb finds them.
    if cfg!(target_os = "linux") {
        assert!(c.contains(".debug_gdb_scripts"), "the gdb pretty printers are not embedded");
    }

    // gdb stops at a Gamma line and shows what the reference's executable shows.
    let gdb_works = Command::new("gdb").arg("--version").output().is_ok_and(|o| o.status.success());
    if !gdb_works || !cfg!(target_os = "linux") {
        eprintln!("note: skipping the debugger session: needs gdb on Linux");
        return;
    }
    let Some((ours, frame)) = gdb_session(&exe, "debuggee.gm:26", &DEBUG_PRINTS) else { return };
    assert!(frame.contains("debuggee.gm:26"), "gdb did not stop at debuggee.gm:26: {frame}");
    let want = [
        "{x = 3, y = 4}",
        "4",
        "Shape_Rect = {w = 2, h = 3}",
        "length 3 = {10, 20, 30}",
        "\"hello\"",
        "5",
        "{_0 = 1, _1 = true}",
        "45",
    ];
    for (i, w) in want.iter().enumerate() {
        assert_eq!(ours.get(i), Some(&format!("${} = {}", i + 1, w)), "gdb printed {:?}", ours);
    }
    // A breakpoint in `sum` stops at `total += i`, where `total` has its Gamma name.
    let Some((values, frame)) = gdb_session(&exe, "debuggee.gm:10", &["F.total", "F.n"]) else { return };
    assert!(frame.contains("debuggee.gm:10"), "gdb did not stop at debuggee.gm:10: {frame}");
    assert_eq!(values, ["$1 = 0", "$2 = 10"]);
    // The values print as in the reference's executable.
    if let Some((theirs, _)) = gdb_session(&reference, "debuggee.gm:26", &DEBUG_PRINTS) {
        assert_eq!(ours, theirs, "gdb prints different values than for the reference's executable");
    }
}

/// A `test` conformance test: what `gammac test` is run with and the output it must have.
#[cfg(unix)]
struct TestStageTest {
    name: String,
    root: PathBuf,
    mode: String,
    filter: Option<String>,
    stdout: Vec<u8>,
    exit: i32,
}

/// The `test` tests that expect no compile errors and apply to this host.
#[cfg(unix)]
fn test_stage_tests() -> Vec<TestStageTest> {
    let dir = root().join("tests/conformance/test");
    let mut entries: Vec<PathBuf> = std::fs::read_dir(&dir).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    let mut tests = vec![];
    for p in entries {
        let name = p.file_name().unwrap().to_str().unwrap().to_string();
        let Some(base) = name.strip_suffix(".gm").or_else(|| name.strip_suffix(".test")) else { continue };
        let mut text = String::new();
        if p.is_dir() {
            let mut files = vec![];
            gm_files(p.strip_prefix(root()).unwrap(), &mut files);
            for f in files {
                text.push_str(&std::fs::read_to_string(root().join(f)).unwrap());
            }
        } else {
            text = std::fs::read_to_string(&p).unwrap();
        }
        let directive = |key: &str| {
            text.lines()
                .find_map(|l| l.strip_prefix("//! ").and_then(|r| r.strip_prefix(key)).map(|v| v.trim().to_string()))
        };
        if text.contains("//~") || directive("stage:").is_some_and(|s| s != "test") {
            continue;
        }
        let host = [
            ("arch:", std::env::consts::ARCH.to_string()),
            ("endian:", if cfg!(target_endian = "little") { "little" } else { "big" }.to_string()),
            ("os:", if cfg!(windows) { "windows" } else { "unix" }.to_string()),
            ("ptr_bits:", (8 * std::mem::size_of::<usize>()).to_string()),
        ];
        if host.iter().any(|(k, v)| directive(k).is_some_and(|d| &d != v)) {
            continue;
        }
        let mode = directive("mode:").unwrap_or_else(|| "debug".to_string());
        if mode == "safe" {
            continue;
        }
        let root_dir = if p.is_dir() && p.join("main").is_dir() { p.join("main") } else { p.clone() };
        tests.push(TestStageTest {
            name: format!("tests/conformance/test/{}", name),
            root: root_dir,
            mode,
            filter: directive("filter:"),
            stdout: std::fs::read(dir.join(format!("{}.stdout", base))).unwrap_or_default(),
            exit: directive("exit:").map_or(0, |e| e.parse().unwrap()),
        });
    }
    tests
}

#[test]
#[cfg(unix)]
fn tests_run_by_gammac_behave_as_expected() {
    let Gammac::Native(gammac) = gammac() else { return };
    let tests = test_stage_tests();
    assert!(tests.len() > 30, "only {} test tests found", tests.len());
    let next = std::sync::atomic::AtomicUsize::new(0);
    let failures = std::sync::Mutex::new(vec![]);
    let workers = std::thread::available_parallelism().map_or(4, |n| n.get());
    std::thread::scope(|s| {
        for _ in 0..workers {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some(t) = tests.get(i) else { break };
                    let mut cmd = Command::new(gammac);
                    cmd.current_dir(root()).arg("test").arg("--diag-format=short").arg(format!("--mode={}", t.mode));
                    if let Some(f) = &t.filter {
                        cmd.arg(format!("--filter={}", f));
                    }
                    cmd.arg(t.root.strip_prefix(root()).unwrap()).stdin(std::process::Stdio::null());
                    gammac_env(&mut cmd);
                    let ran = common::output(&mut cmd).unwrap();
                    let why = if ran.status.code() != Some(t.exit) {
                        Some(format!(
                            "exit status {:?}, expected {}:\n{}",
                            ran.status.code(),
                            t.exit,
                            String::from_utf8_lossy(&ran.stderr)
                        ))
                    } else if ran.stdout != t.stdout {
                        let (e, a) = (String::from_utf8_lossy(&t.stdout), String::from_utf8_lossy(&ran.stdout));
                        let (_, line, e, a) = first_difference(&e, &a).unwrap_or_default();
                        Some(format!("output differs at line {}:\nexpected: {}\n  actual: {}", line, e, a))
                    } else {
                        None
                    };
                    if let Some(why) = why {
                        failures.lock().unwrap().push(format!("{}: {}", t.name, why));
                    }
                }
            });
        }
    });
    let mut failures = failures.into_inner().unwrap();
    failures.sort();
    assert!(failures.is_empty(), "{} of {} programs failed:\n{}", failures.len(), tests.len(), failures.join("\n\n"));
}

/// `gammac test` and `gammac bench` print what `gamma test --backend=c` and `gamma bench`
/// do, for a standard library module and for options and failures (timings masked).
#[test]
#[cfg(unix)]
fn testing_matches_the_reference() {
    let Gammac::Native(gammac) = gammac() else { return };
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-test");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(
        dir.join("cases.gm"),
        "fib :: fn(n: u32) -> u32 => if n < 2 { n } else { fib(n - 1) + fib(n - 2) }\n\
         test \"quick\" { print(\"hello\\n\") }\n\
         test \"spins\" {\n n: u64 = 0\n while true { n = @black_box(n + 1) }\n}\n\
         test \"fails\" {\n print(\"out\\n\")\n expect(false, \"nope\")\n}\n\
         test \"traps\" {\n xs := [3]i32(1, 2, 3)\n print(\"{}\\n\", xs[@black_box(usize(7))])\n}\n\
         bench \"fib\" { _ = @black_box(fib(12)) }\n",
    )
    .unwrap();
    let cases_path = dir.join("cases.gm").to_str().unwrap().to_string();
    // Masks every timing: a number followed by a unit of time.
    let mask = |s: &[u8]| {
        let s = String::from_utf8_lossy(s).into_owned();
        let mut out = String::new();
        let mut rest = s.as_str();
        while let Some(i) = rest.find(|c: char| c.is_ascii_digit()) {
            let tail = &rest[i..];
            let n = tail.find(|c: char| !(c.is_ascii_digit() || c == '.')).unwrap_or(tail.len());
            let after = tail[n..].trim_start_matches(' ');
            let unit =
                ["ns/run", "µs/run", "ms/run", "s/run", "µs", "ms", "s"].into_iter().find(|u| after.starts_with(u));
            out.push_str(&rest[..i]);
            match unit {
                Some(u) if rest[..i].ends_with('(') || rest[..i].ends_with("in ") || u.ends_with("/run") => {
                    out.push('T');
                    rest = &after[u.len()..];
                    // The spread and the number of runs of a benchmark vary too.
                    if rest.starts_with(" (± ") {
                        rest = &rest[rest.find(')').unwrap() + 1..];
                    }
                }
                _ => {
                    out.push_str(&tail[..n]);
                    rest = &tail[n..];
                }
            }
        }
        out.push_str(rest);
        out
    };
    let run = |reference: bool, command: &str, args: &[String]| {
        let mut cmd = if reference { gamma() } else { Command::new(gammac) };
        cmd.current_dir(root()).arg(command);
        if reference && command == "test" {
            cmd.arg("--backend=c");
        }
        cmd.args(args).stdin(std::process::Stdio::null());
        gammac_env(&mut cmd);
        let out = common::output(&mut cmd).unwrap();
        (mask(&out.stdout), mask(&out.stderr), out.status.code())
    };
    let s = |a: &[&str]| a.iter().map(|x| x.to_string()).collect::<Vec<_>>();
    let with = |a: &[&str]| {
        let mut v = s(a);
        v.push(cases_path.clone());
        v
    };
    let jobs: Vec<(&str, Vec<String>)> = vec![
        ("test", s(&["lib/std/str"])),
        ("test", s(&["tests/conformance/test/bench_not_run.gm"])),
        ("test", with(&["--timeout=0.5"])),
        ("test", with(&["--timeout=0.5", "-j", "4", "--time", "--show-output"])),
        ("test", with(&["--filter=fail", "--show-output"])),
        ("test", with(&["--filter=none"])),
        ("test", with(&["--mode=fast", "--filter=traps"])),
        ("test", with(&["--all", "--filter=quick"])),
        ("bench", with(&["--filter=fib"])),
        ("bench", with(&["--time"])),
    ];
    for (command, args) in jobs {
        let want = run(true, command, &args);
        let got = run(false, command, &args);
        assert_eq!(want, got, "{} {:?}", command, args);
    }
    // Options gammac does not have are usage errors.
    for bad in ["--backend=interp", "--backend=cranelift", "--coverage", "--package=x", "--target=x86_64-linux-gnu"] {
        let out = common::output(Command::new(gammac).current_dir(root()).args(["test", bad, "x.gm"])).unwrap();
        assert_eq!(out.status.code(), Some(2), "{}", bad);
    }
}

// ---------------------------------------------------------------------------------
// gammac's package manager and gamma.toml build settings
// ---------------------------------------------------------------------------------

/// A scratch directory for the package tests: `ref` for the reference implementation, `new`
/// for gammac, each with a cache of its own. Both run the same steps in the same relative
/// places, so that what they print and write can be compared.
#[cfg(unix)]
struct Twin {
    root: PathBuf,
}

#[cfg(unix)]
impl Twin {
    fn new(name: &str) -> Twin {
        let root = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-pkg").join(name);
        let _ = std::fs::remove_dir_all(&root);
        for side in ["ref", "new"] {
            std::fs::create_dir_all(root.join(side).join("w")).unwrap();
        }
        Twin { root }
    }

    fn dir(&self, side: &str) -> PathBuf {
        self.root.join(side).join("w")
    }

    /// The command for one side: `gamma` for `ref`, `gammac` for `new`, in `sub`.
    fn command(&self, side: &str, sub: &str) -> Command {
        let mut cmd = if side == "ref" {
            gamma()
        } else {
            let Gammac::Native(exe) = gammac() else { unreachable!() };
            let mut c = Command::new(exe);
            gammac_env(&mut c);
            c
        };
        cmd.current_dir(self.dir(side).join(sub));
        cmd.env("GAMMA_CACHE_DIR", self.root.join(side).join("cache"));
        cmd.env("HOME", self.root.join(side));
        cmd.env_remove("CFLAGS").env_remove("LDFLAGS").env_remove("CC");
        for (k, v) in [
            ("GIT_AUTHOR_NAME", "t"),
            ("GIT_AUTHOR_EMAIL", "t@example.com"),
            ("GIT_COMMITTER_NAME", "t"),
            ("GIT_COMMITTER_EMAIL", "t@example.com"),
            ("GIT_AUTHOR_DATE", "2020-01-01T00:00:00Z"),
            ("GIT_COMMITTER_DATE", "2020-01-01T00:00:00Z"),
        ] {
            cmd.env(k, v);
        }
        cmd
    }

    /// What `cmd` printed, with the side's directory and the program's name made alike.
    fn run(&self, side: &str, mut cmd: Command) -> Ran {
        let (out, err, code) = ran(common::output(&mut cmd).unwrap());
        let normal = |s: String| s.replace(self.root.join(side).to_str().unwrap(), "TMP").replace("gammac", "gamma");
        (normal(out), normal(err), code)
    }

    /// Runs `args` in `sub` on both sides and requires the same output, diagnostics and
    /// exit status. Returns the reference's.
    fn both(&self, sub: &str, args: &[&str]) -> Ran {
        self.both_env(sub, args, &[])
    }

    fn both_env(&self, sub: &str, args: &[&str], env: &[(&str, &str)]) -> Ran {
        let side = |name: &str| -> Ran {
            let mut cmd = self.command(name, sub);
            cmd.args(args).envs(env.iter().copied());
            self.run(name, cmd)
        };
        let want = side("ref");
        let got = side("new");
        // The usage text differs in the commands each program has: compare up to it.
        let cut = |s: &str| s.split("\nusage: ").next().unwrap().to_string();
        assert_eq!(cut(&want.0), cut(&got.0), "`{}` standard output differs", args.join(" "));
        assert_eq!(cut(&want.1), cut(&got.1), "`{}` diagnostics differ", args.join(" "));
        assert_eq!(want.2, got.2, "`{}` exit status differs", args.join(" "));
        want
    }

    /// Writes `text` to `rel` on both sides.
    fn write(&self, rel: &str, text: &str) {
        for side in ["ref", "new"] {
            let p = self.dir(side).join(rel);
            std::fs::create_dir_all(p.parent().unwrap()).unwrap();
            std::fs::write(p, text).unwrap();
        }
    }

    /// Requires the files below `rel` (except the C files and executables a build leaves) to
    /// be the same.
    fn same_files(&self, rel: &str) {
        let files = |side: &str| -> Vec<(String, String)> {
            tree(&self.dir(side).join(rel))
                .into_iter()
                .filter(|(n, _)| !n.starts_with(".git/") && !n.ends_with(".c") && !n.ends_with(".out"))
                .map(|(n, c)| (n, String::from_utf8_lossy(&c).replace(self.root.join(side).to_str().unwrap(), "TMP")))
                .collect()
        };
        assert_eq!(files("ref"), files("new"), "the files below {} differ", rel);
    }
}

#[cfg(unix)]
fn tool_available(tool: &str) -> bool {
    Command::new(tool).arg("--version").output().is_ok_and(|o| o.status.success())
}

/// `new`, `init`, `add --path` and `remove`, and the command-line errors of the package
/// commands, give the reference's output and files.
#[test]
#[cfg(unix)]
fn package_scaffolding_and_editing_match_the_reference() {
    let Gammac::Native(_) = gammac() else { return };
    let t = Twin::new("scaffold");
    t.both(".", &["new", "my-app", "--vcs=none"]);
    t.both(".", &["new", "my-app", "--vcs=none"]);
    t.both(".", &["new", "other", "--name=thing", "--vcs=none"]);
    t.both(".", &["new", "x", "--name=1x"]);
    t.both(".", &["new", "--vcs=none", "ünï-cödé"]);
    for name in ["fn", "std", "i32", "_"] {
        t.both(".", &["new", "--vcs=none", &format!("--name={name}"), "d"]);
    }
    t.write("proj/main.gm", "main :: fn() needs Out {\n    print(\"mine\\n\")\n}\n");
    t.write("proj/.gitignore", "custom\n");
    t.both("proj", &["init", "--vcs=none"]);
    t.both("proj", &["init", "--vcs=none"]);
    t.write("bad/gamma.toml", "[package\n");
    t.both("bad", &["init", "--vcs=none"]);
    t.both(".", &["init", "missing"]);
    t.write("lib/gamma.toml", "[package]\nname = \"lib\"\n");
    t.write("lib/lib.gm", "pub value :: fn() -> i32 => 1\n");
    t.write(
        "app/gamma.toml",
        "# my app\n[package]\nname = \"app\"\n\n[dependencies]\n# json\njson = { path = \"../lib\" }\n",
    );
    t.both("app", &["add", "other", "--path", "../lib"]);
    t.both("app", &["add", "other", "--path", "../lib"]);
    t.both("app", &["add", "bad-name", "--path", "../lib"]);
    t.both("app", &["add", "x"]);
    t.both("app", &["add", "x", "--bogus"]);
    t.both("app", &["remove", "other"]);
    t.both("app", &["remove", "json"]);
    t.both("app", &["remove", "json"]);
    t.same_files(".");
    for (sub, text) in [
        ("crlf", "[package]\r\nname = \"o\"\r\n\r\n[dependencies]\r\n"),
        ("dotted", "[package]\nname = \"o\"\n[dependencies]\nz.path = \"../lib\"\n"),
        ("subtable", "[package]\nname = \"o\"\n[dependencies.z]\npath = \"../lib\"\n"),
        ("bare", "[package]\nname = \"o\""),
    ] {
        t.write(&format!("{sub}/gamma.toml"), text);
        t.both(sub, &["add", "w", "--path", "../lib"]);
        t.both(sub, &["remove", "w"]);
        t.both(sub, &["remove", "z"]);
    }
    t.same_files(".");
    // The usage differs in the commands each program has, but both end with the package commands.
    let help = |side: &str| {
        let mut cmd = t.command(side, ".");
        cmd.args(["help", "packages"]);
        t.run(side, cmd).0
    };
    let tail = |s: String| s[s.find("package commands:").unwrap()..].to_string();
    assert_eq!(tail(help("ref")), tail(help("new")));
    t.both(".", &["update", "--bogus"]);
    t.both(".", &["fetch", "--offline=true"]);
    t.both(".", &["cache", "tidy"]);
    t.both(".", &["cache", "dir"]);
}

/// Every way a `gamma.toml` can be wrong is reported as the reference does, with its line.
#[test]
#[cfg(unix)]
fn manifest_errors_match_the_reference() {
    let Gammac::Native(_) = gammac() else { return };
    let t = Twin::new("manifest");
    t.write("p/main.gm", "main :: fn() {}\n");
    let cases = [
        "package = 1",
        "[package]\nname = 1",
        "[package]\nname = \"a b\"",
        "[package]\nname = \"fn\"",
        "[package]\nname = \"ok\"\nversion = \"1.x\"",
        "[package]\nname = \"ok\"\nversion = \"01.2.3\"",
        "dependencies = 3",
        "[dependencies]\nx = 3",
        "[dependencies]\nx = { path = \"d\", bogus = 1 }",
        "[dependencies]\nx = { path = \"d\", git = \"u\" }",
        "[dependencies]\nx = { git = \"\" }",
        "[dependencies]\nx = { git = \"https://h/r\", tag = \"a\", branch = \"b\" }",
        "[dependencies]\nx = { git = \"https://h/r\", version = \"x.y\" }",
        "[dependencies]\nx = { git = \"https://h/r\", version = \">=1, <\" }",
        "[dependencies]\nx = { git = \"https://h/r\", rev = \"xyz\" }",
        "[dependencies]\nx = { git = \"https://h/r\", subdir = \"../x\" }",
        "[dependencies]\nx = { git = \"-oProxyCommand=x\" }",
        "[dependencies]\nx = { git = \"a b\" }",
        "[dependencies]\nx = { url = \"u\", sha256 = \"abc\" }",
        "[dependencies]\nx = { url = \"u\", tag = \"t\" }",
        "[dependencies]\nx = { url = \"u\" }\ny = {\n  path = \"d\",\n  bogus = 1 }",
        "[dependencies.x]\npath = \"d\"\nbogus = 3",
        "[build]\nmode = \"turbo\"",
        "[build]\nbogus = 1",
        "[build]\nlibs = [\"-lz\"]",
        "[build]\nc-sources = [\"\"]",
        "[build]\nc-sources = [\"nothere.c\"]",
        "lint = 3",
        "[lint]\nbogus = \"warn\"",
        "[profile.\"a b\"]\nmode = \"fast\"",
        "[profile.dev]\nmode = \"x\"",
        "[package",
        "name = ",
        "x = [1,",
        "a = 01",
        "a = \"\\q\"",
        "a = 1\na = 2",
        "[a]\n[a]",
        "[package]\nname = \"\"\"\nok\"\"\"\nversion = \"x\"",
        "a = 1.5\nb = 1979-05-27\nc = { x = 1, }",
    ];
    for case in cases {
        t.write("p/gamma.toml", &format!("{case}\n"));
        t.both("p", &["check", "."]);
    }
    for args in [["check", ".", "--profile=nope"], ["check", ".", "--profile="], ["check", ".", "--profile"]] {
        t.write("p/gamma.toml", "[build]\nmode = \"fast\"\n[profile.dev]\nmode = \"checked\"\n");
        t.both("p", &args);
    }
    t.write("q/m.gm", "main :: fn() {}\n");
    t.both("q", &["check", "m.gm", "--profile=dev"]);
}

#[cfg(unix)]
fn have_vcs() -> bool {
    Command::new("git").arg("--version").output().is_ok_and(|o| o.status.success())
}

/// Runs `git` in `dir` with a fixed identity and dates, so that the same commits come out on
/// both sides.
#[cfg(unix)]
fn vcs(dir: &Path, args: &[&str]) {
    let out = Command::new("git")
        .args(["-c", "init.defaultBranch=main", "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false"])
        .args(args)
        .current_dir(dir)
        .env("GIT_AUTHOR_NAME", "t")
        .env("GIT_AUTHOR_EMAIL", "t@example.com")
        .env("GIT_COMMITTER_NAME", "t")
        .env("GIT_COMMITTER_EMAIL", "t@example.com")
        .env("GIT_AUTHOR_DATE", "2020-01-01T00:00:00Z")
        .env("GIT_COMMITTER_DATE", "2020-01-01T00:00:00Z")
        .output()
        .unwrap();
    assert!(out.status.success(), "{:?}: {}", args, String::from_utf8_lossy(&out.stderr));
}

/// A git dependency is fetched, locked, built and run; `--locked`, `--offline`, `update`,
/// `add`, `remove` and the cache commands behave as the reference's.
#[test]
#[cfg(unix)]
fn git_dependencies_match_the_reference() {
    let Gammac::Native(_) = gammac() else { return };
    if !have_vcs() {
        eprintln!("skipped: git is not installed");
        return;
    }
    let t = Twin::new("git");
    for side in ["ref", "new"] {
        let w = t.dir(side);
        let repo = w.join("util");
        std::fs::create_dir_all(&repo).unwrap();
        vcs(&repo, &["init", "-q"]);
        for (version, value) in [("1.0.0", 1), ("1.1.0", 2)] {
            std::fs::write(repo.join("gamma.toml"), format!("[package]\nname = \"util\"\nversion = \"{version}\"\n"))
                .unwrap();
            std::fs::write(repo.join("lib.gm"), format!("pub value :: fn() -> i32 => {value}\n")).unwrap();
            vcs(&repo, &["add", "-A"]);
            vcs(&repo, &["commit", "-q", "-m", version]);
            vcs(&repo, &["tag", &format!("v{version}")]);
        }
        std::fs::create_dir_all(w.join("app")).unwrap();
        std::fs::write(w.join("app/gamma.toml"), "[package]\nname = \"app\"\n").unwrap();
        std::fs::write(
            w.join("app/main.gm"),
            "util :: import \"util\"\nmain :: fn() needs Out {\n    print(\"{}\\n\", util.value())\n}\n",
        )
        .unwrap();
    }
    // `add` takes the URL, which contains the side's directory.
    let add = |side: &str| -> Ran {
        let mut cmd = t.command(side, "app");
        let url = format!("file://{}", t.dir(side).join("util").display());
        cmd.args(["add", "util", "--git", &url, "--version", "1"]);
        t.run(side, cmd)
    };
    let (want, got) = (add("ref"), add("new"));
    assert_eq!(want, got);
    assert!(want.1.contains("Locking util v1.1.0"), "{}", want.1);
    t.same_files("app");
    assert_eq!(t.both("app", &["run", "."]).0, "2\n");
    t.both("app", &["check", "."]);
    t.both("app", &["build", ".", "-o", "app.out"]);
    let built = Command::new(t.dir("new").join("app/app.out")).output().unwrap();
    assert_eq!(String::from_utf8_lossy(&built.stdout), "2\n");
    t.both("app", &["fetch", "--locked"]);
    t.both("app", &["run", "--offline", "--locked", "."]);
    t.both("app", &["update"]);
    // A diagnostic names the dependency's module.
    t.write("app/main.gm", "util :: import \"util\"\nmain :: fn() needs Out {\n    print(\"{}\\n\", util.nope())\n}\n");
    t.both("app", &["check", "."]);
    // A newer tag is picked up by `update`, not before.
    t.write(
        "app/main.gm",
        "util :: import \"util\"\nmain :: fn() needs Out {\n    print(\"{}\\n\", util.value())\n}\n",
    );
    for side in ["ref", "new"] {
        let repo = t.dir(side).join("util");
        std::fs::write(repo.join("lib.gm"), "pub value :: fn() -> i32 => 3\n").unwrap();
        vcs(&repo, &["add", "-A"]);
        vcs(&repo, &["commit", "-q", "-m", "next"]);
        vcs(&repo, &["tag", "v1.2.0"]);
    }
    assert_eq!(t.both("app", &["run", "."]).0, "2\n");
    t.both("app", &["update", "nope"]);
    let updated = t.both("app", &["update"]);
    assert!(updated.1.contains("Updating util v1.1.0 -> v1.2.0"), "{}", updated.1);
    assert_eq!(t.both("app", &["run", "."]).0, "3\n");
    // A lock that is edited, damaged or missing.
    for side in ["ref", "new"] {
        let p = t.dir(side).join("app/gamma.lock");
        let text = std::fs::read_to_string(&p).unwrap();
        std::fs::write(&p, text.replace("version = \"1.2.0\"", "version = \"9.9.9\"")).unwrap();
    }
    t.both("app", &["run", "--locked", "."]);
    t.both("app", &["run", "."]);
    t.write("app/gamma.lock", "garbage = [");
    t.both("app", &["run", "."]);
    t.write("app/gamma.lock", "");
    t.both("app", &["run", "--locked", "."]);
    t.both("app", &["run", "."]);
    t.same_files("app");
    t.both("app", &["cache", "clean", "--dry-run"]);
    t.both("app", &["remove", "util"]);
    t.same_files("app");
}

/// Archive dependencies: downloaded, locked and checked against their hash.
#[test]
#[cfg(unix)]
fn archive_dependencies_match_the_reference() {
    let Gammac::Native(_) = gammac() else { return };
    if !tool_available("curl") || !tool_available("tar") {
        eprintln!("skipped: curl or tar is not installed");
        return;
    }
    let t = Twin::new("archive");
    for side in ["ref", "new"] {
        let w = t.dir(side);
        std::fs::create_dir_all(w.join("util-1.0.0")).unwrap();
        std::fs::write(w.join("util-1.0.0/gamma.toml"), "[package]\nname = \"util\"\nversion = \"1.0.0\"\n").unwrap();
        std::fs::write(w.join("util-1.0.0/lib.gm"), "pub value :: fn() -> i32 => 5\n").unwrap();
        let made = Command::new("tar")
            .args(["--sort=name", "--mtime=@0", "--owner=0", "--group=0", "-czf", "util.tgz", "util-1.0.0"])
            .current_dir(&w)
            .output()
            .unwrap();
        if !made.status.success() {
            eprintln!("skipped: tar does not take --sort");
            return;
        }
        std::fs::create_dir_all(w.join("app")).unwrap();
        std::fs::write(
            w.join("app/gamma.toml"),
            format!(
                "[package]\nname = \"app\"\n[dependencies]\nutil = {{ url = \"file://{}\" }}\n",
                w.join("util.tgz").display()
            ),
        )
        .unwrap();
        std::fs::write(
            w.join("app/main.gm"),
            "util :: import \"util\"\nmain :: fn() needs Out {\n    print(\"{}\\n\", util.value())\n}\n",
        )
        .unwrap();
    }
    t.both("app", &["run", "."]);
    t.both("app", &["run", "--offline", "--locked", "."]);
    t.both("app", &["build", ".", "-o", "app.out"]);
    t.both("app", &["run", "--offline", "."]);
    t.write("app/gamma.toml", "[package]\nname = \"app\"\n[dependencies]\nx = { url = \"u\", sha256 = \"abc\" }\n");
    t.both("app", &["check", "."]);
    t.same_files("app");
}

/// `[build]` and `[profile.*]`: the mode, flags and C sources of a package reach the build,
/// and the command line wins over the manifest.
#[test]
#[cfg(unix)]
fn build_settings_match_the_reference() {
    let Gammac::Native(_) = gammac() else { return };
    let t = Twin::new("settings");
    t.write("app/c/include/shim.h", "int shim_add(int a, int b);\n");
    t.write(
        "app/c/shim.c",
        "#include \"shim.h\"\n#ifndef BONUS\n#define BONUS 0\n#endif\nint shim_add(int a, int b) { return a + b + BONUS; }\n",
    );
    t.write(
        "app/main.gm",
        "c :: import \"std/c\"\n\nshim :: foreign \"c\" {\n    shim_add :: fn(a: c.c_int, b: c.c_int) -> c.c_int needs not\n}\n\nmain :: fn() needs Out + Sys {\n    print(\"{}\\n\", shim.shim_add(2, 3))\n}\n",
    );
    t.write(
        "app/mode.gm",
        "mode_name :: fn() -> string {\n    when target.mode is .Debug { return \"debug\" }\n        else when target.mode is .Checked { return \"checked\" }\n        else when target.mode is .Safe { return \"safe\" }\n        else { return \"fast\" }\n}\n\nmain :: fn() needs Out {\n    print(\"{}\\n\", mode_name())\n}\n",
    );
    t.write(
        "app/gamma.toml",
        "[package]\nname = \"app\"\n\n[build]\nmode = \"checked\"\nc-sources = [\"c/shim.c\"]\ninclude-dirs = [\"c/include\"]\n\n[profile.release]\nmode = \"fast\"\ncflags = [\"-DBONUS=100\"]\n\n[profile.dev]\nmode = \"debug\"\n",
    );
    assert_eq!(t.both("app", &["run", "main.gm"]).0, "5\n");
    assert_eq!(t.both("app", &["run", "mode.gm"]).0, "checked\n");
    assert_eq!(t.both("app", &["run", "--profile=release", "mode.gm"]).0, "fast\n");
    assert_eq!(t.both("app", &["run", "--profile=dev", "mode.gm"]).0, "debug\n");
    assert_eq!(t.both("app", &["run", "--profile=release", "--mode=checked", "mode.gm"]).0, "checked\n");
    assert_eq!(t.both("app", &["run", "--mode=fast", "mode.gm"]).0, "fast\n");
    assert_eq!(t.both("app", &["run", "--profile=release", "main.gm"]).0, "105\n");
    t.both("app", &["run", "--profile=nope", "mode.gm"]);
    t.both("app", &["check", "--profile=release", "mode.gm"]);
    // The environment's flags come after the manifest's and win.
    let flags = t.both_env("app", &["run", "--profile=release", "main.gm"], &[("CFLAGS", "-DBONUS=7")]);
    assert_eq!(flags.0, "12\n");
    // A C source that does not compile, and a missing one.
    t.write("app/c/shim.c", "int shim_add(int a, int b) { return a +\n");
    let (_, err, code) = t.both("app", &["build", "main.gm", "-o", "broken"]);
    assert_eq!(code, Some(1));
    assert!(err.contains("the C compiler failed on"), "{err}");
    t.write("app/gamma.toml", "[package]\nname = \"app\"\n[build]\nc-sources = [\"c/nope.c\"]\n");
    t.both("app", &["build", "main.gm"]);
    // A package that wraps C, with an include directory of its own, as a path dependency.
    t.write(
        "wrap/gamma.toml",
        "[package]\nname = \"wrap\"\n\n[build]\nc-sources = [\"native/wrap.c\"]\ninclude-dirs = [\"native\"]\nlibs = [\"m\"]\n",
    );
    t.write("wrap/native/config.h", "#define WRAP_BASE 40\n");
    t.write(
        "wrap/native/wrap.c",
        "#include \"config.h\"\n#include <math.h>\nint wrap_value(void) { return WRAP_BASE + (int)sqrt(4.0); }\n",
    );
    t.write(
        "wrap/lib.gm",
        "c :: import \"std/c\"\nlib :: foreign \"c\" {\n    wrap_value :: fn() -> c.c_int needs not\n}\npub value :: fn() -> i32 needs Sys => lib.wrap_value()\n",
    );
    t.write(
        "app3/gamma.toml",
        "[package]\nname = \"app\"\n\n[dependencies]\nwrap = { path = \"../wrap\" }\n\n[build]\nc-sources = [\"own.c\"]\ninclude-dirs = [\"inc\"]\n",
    );
    t.write("app3/inc/config.h", "#define OWN_BASE 1000\n");
    t.write("app3/own.c", "#include \"config.h\"\nint own_value(void) { return OWN_BASE; }\n");
    t.write(
        "app3/main.gm",
        "wrap :: import \"wrap\"\nc :: import \"std/c\"\n\nown :: foreign \"c\" {\n    own_value :: fn() -> c.c_int needs not\n}\n\nmain :: fn() needs Out + Sys {\n    print(\"{} {}\\n\", wrap.value(), own.own_value())\n}\n",
    );
    assert_eq!(t.both("app3", &["run", "main.gm"]).0, "42 1000\n");
    t.both("app3", &["build", ".", "-o", "a3"]);
    let built = Command::new(t.dir("new").join("app3/a3")).output().unwrap();
    assert_eq!(String::from_utf8_lossy(&built.stdout), "42 1000\n");
}
