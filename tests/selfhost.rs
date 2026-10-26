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
