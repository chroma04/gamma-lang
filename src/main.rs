//! The `gamma` command-line tool.

use gamma::diag::DiagFormat;
use gamma::driver::{self, Session};
use std::path::PathBuf;
use std::process::ExitCode;

const USAGE: &str = "\
usage: gamma <command> [options] <file>

commands:
  lex     print the tokens of a source file
  parse   check the syntax of a source file (--dump prints the syntax tree)
  check   type-check a program (a .gm file or a module directory)
  run     run a program; arguments after the file go to the program
  build   compile a program to a native executable (via the system C compiler)
  test    run the `test` declarations of a module (natively if it needs C)
  fmt     format source files in place: gamma fmt [--check] <file or directory>... (`-` filters stdin to stdout)
  help    print this message
  version print the compiler version

options:
  --diag-format=human|short   how to print diagnostics (default: human)
  --dump                      (parse) print the syntax tree
  --backend=interp|c          (run, test) use the interpreter (default) or compile via C
  -o <file>                   (build) the executable to write (default: the input's name)
  -g                          (build, and run and test via C) add debug information that maps
                              the executable to the Gamma source, for debuggers; a Debug build
                              is then compiled without optimization
  --mode=debug|checked|fast   (run, build, test) the build mode (default: debug): Debug and
                              Checked trap on failed checks, Fast omits them; Debug also
                              checks every free
";

struct Options {
    command: String,
    file: Option<PathBuf>,
    /// Arguments for the program (`gamma run file.gm args…`).
    program_args: Vec<String>,
    format: DiagFormat,
    dump: bool,
    /// `--backend=c`: run through the C back end.
    c_backend: bool,
    /// `-o`: the output of `build`.
    output: Option<PathBuf>,
    /// `--mode=…`: the build mode.
    mode: gamma::interp::Mode,
    /// `-g`: native code carries debug information.
    debug_info: bool,
}

fn parse_args() -> Result<Options, String> {
    let mut args = std::env::args().skip(1);
    let command = args.next().ok_or_else(|| "missing command".to_string())?;
    let mut opts = Options {
        command,
        file: None,
        program_args: vec![],
        format: DiagFormat::Human,
        dump: false,
        c_backend: false,
        output: None,
        mode: gamma::interp::Mode::Debug,
        debug_info: false,
    };
    let mut args = args.peekable();
    while let Some(arg) = args.next() {
        if opts.file.is_some() && opts.command == "run" {
            opts.program_args.push(arg);
            continue;
        }
        match arg.as_str() {
            "--dump" => opts.dump = true,
            "--backend=c" => opts.c_backend = true,
            "--backend=interp" => opts.c_backend = false,
            "-o" => opts.output = Some(PathBuf::from(args.next().ok_or_else(|| "`-o` needs a file name".to_string())?)),
            "-g" => opts.debug_info = true,
            "--mode=debug" => opts.mode = gamma::interp::Mode::Debug,
            "--mode=checked" => opts.mode = gamma::interp::Mode::Checked,
            "--mode=fast" => opts.mode = gamma::interp::Mode::Fast,
            "--diag-format=human" => opts.format = DiagFormat::Human,
            "--diag-format=short" => opts.format = DiagFormat::Short,
            a if a.starts_with('-') => return Err(format!("unknown option `{}`", a)),
            a => {
                if opts.file.is_some() {
                    return Err("more than one input file given".to_string());
                }
                opts.file = Some(PathBuf::from(a));
            }
        }
    }
    Ok(opts)
}

/// Lets a closed output pipe end the process quietly, as for other command-line tools
/// (`gamma test … | head`), instead of a panic on the failed write. Rust ignores
/// `SIGPIPE` by default.
#[cfg(unix)]
fn default_sigpipe() {
    unsafe extern "C" {
        fn signal(sig: i32, handler: usize) -> usize;
    }
    const SIGPIPE: i32 = 13; // the same on Linux, macOS and the BSDs
    const SIG_DFL: usize = 0;
    // SAFETY: restoring the default disposition of a signal has no preconditions.
    unsafe {
        signal(SIGPIPE, SIG_DFL);
    }
}

fn main() -> ExitCode {
    #[cfg(unix)]
    default_sigpipe();
    if std::env::args().nth(1).as_deref() == Some("fmt") {
        return fmt_command(std::env::args().skip(2).collect());
    }
    let opts = match parse_args() {
        Ok(o) => o,
        Err(e) => {
            eprintln!("gamma: {}\n\n{}", e, USAGE);
            return ExitCode::from(2);
        }
    };
    match opts.command.as_str() {
        "help" | "--help" | "-h" => {
            print!("{}", USAGE);
            ExitCode::SUCCESS
        }
        "version" | "--version" => {
            println!("gamma {}", env!("CARGO_PKG_VERSION"));
            ExitCode::SUCCESS
        }
        "lex" | "parse" => run_front_end(&opts),
        "check" => check(&opts),
        "run" => run(&opts),
        "test" => test(&opts),
        "build" => build(&opts),
        other => {
            eprintln!("gamma: unknown command `{}`\n\n{}", other, USAGE);
            ExitCode::from(2)
        }
    }
}

fn run_front_end(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
    sess.debug_info = opts.debug_info;
    let mut out = String::new();
    if let Some(file) = sess.load(path) {
        if opts.command == "lex" {
            let lexed = sess.lex(file);
            out = driver::dump_tokens(&sess.sources, &lexed.tokens);
        } else {
            let ast = sess.parse(file);
            if opts.dump {
                out = driver::dump_ast(&ast);
            }
        }
    }
    print!("{}", out);
    eprint!("{}", sess.render_diagnostics(opts.format));
    if sess.diags.has_errors() { ExitCode::FAILURE } else { ExitCode::SUCCESS }
}

fn check(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
    sess.debug_info = opts.debug_info;
    let ok = gamma::sema::analyze(&mut sess, path).is_some();
    eprint!("{}", sess.render_diagnostics(opts.format));
    if ok { ExitCode::SUCCESS } else { ExitCode::FAILURE }
}

fn run(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
    sess.debug_info = opts.debug_info;
    let prog = gamma::sema::analyze(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    // The interpreter cannot call C or run assembly, so such programs run natively.
    let uses_c = prog.needs_native();
    if opts.c_backend || uses_c {
        return run_native(&sess, &prog, path, opts);
    }
    let cfg = gamma::interp::RunConfig { target: gamma::layout::Target::host(), mode: opts.mode, step_budget: None };
    let mut args = vec![path.display().to_string()];
    args.extend(opts.program_args.iter().cloned());
    let mut stdout = std::io::stdout();
    let mut stderr = std::io::stderr();
    match gamma::interp::run_main(&prog, &cfg, &args, &mut stdout, &mut stderr) {
        gamma::interp::Outcome::Exit(code) => ExitCode::from(code),
        gamma::interp::Outcome::Trap(t) => {
            use std::io::Write;
            let _ = stdout.flush();
            eprint!("trap: {}", trap_report(&sess, &prog, &t));
            // The status of a process terminated by `abort` (SIGABRT).
            ExitCode::from(134)
        }
    }
}

/// The source location of `span` as `file:line:col`, if it has one.
fn location(sess: &Session, span: gamma::source::Span) -> Option<String> {
    if span == gamma::source::Span::DUMMY {
        return None;
    }
    let f = sess.sources.file(span.file);
    let (line, col) = f.line_col(span.lo);
    Some(format!("{}:{}:{}", f.path.display(), line, col))
}

/// A trap's message and where it happened: its location and, in Debug builds, the
/// calls leading to it (the same report the C runtime prints).
fn trap_report(sess: &Session, prog: &gamma::tir::Program, trap: &gamma::interp::Trap) -> String {
    let mut s = format!("{}\n", trap.message);
    let name = |i: usize| trap.trace.get(i).and_then(|(f, _)| prog.backtrace_name(*f, &sess.sources));
    if let Some(at) = location(sess, trap.span) {
        s.push_str(&format!("  at {}", at));
        if let Some(n) = name(0) {
            s.push_str(&format!(" in {}", n));
        }
        s.push('\n');
    }
    // Each call site lies in the function that the next (outer) entry called.
    let callers: Vec<String> = (0..trap.trace.len())
        .filter_map(|i| Some(format!("  called from {} in {}\n", location(sess, trap.trace[i].1)?, name(i + 1)?)))
        .collect();
    // Past BACKTRACE_MAX callers, only the first and last BACKTRACE_MAX / 2.
    const BACKTRACE_MAX: usize = 20;
    let n = callers.len();
    for (i, line) in callers.iter().enumerate() {
        if n <= BACKTRACE_MAX || i < BACKTRACE_MAX / 2 || i >= n - BACKTRACE_MAX / 2 {
            s.push_str(line);
        } else if i == BACKTRACE_MAX / 2 {
            s.push_str(&format!("  ... {} more calls\n", n - BACKTRACE_MAX));
        }
    }
    s
}

/// Counts and prints the results of tests, in the format shared by both back ends.
#[derive(Default)]
struct TestReport {
    passed: usize,
    failed: usize,
}

impl TestReport {
    /// Prints the line for test `name`; `failure` is the detail of a failing test
    /// (its output and the reason), printed indented below the line.
    fn record(&mut self, name: &str, failure: Option<String>) {
        match failure {
            None => {
                self.passed += 1;
                println!("test \"{}\" ... ok", name);
            }
            Some(detail) => {
                self.failed += 1;
                println!("test \"{}\" ... FAILED", name);
                for line in detail.lines() {
                    println!("    {}", line);
                }
            }
        }
    }

    fn finish(&self) -> ExitCode {
        println!("{} passed, {} failed", self.passed, self.failed);
        if self.failed == 0 { ExitCode::SUCCESS } else { ExitCode::FAILURE }
    }
}

/// Runs each test of the root module with captured output (§3.8). A failing
/// test's output and failure reason are printed after the summary line for it.
fn test(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
    sess.debug_info = opts.debug_info;
    let prog = gamma::sema::analyze_tests(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    // The interpreter cannot call C or run assembly, so such programs test natively.
    if opts.c_backend || prog.needs_native() {
        return test_native(&sess, &prog);
    }
    let cfg = gamma::interp::RunConfig { target: gamma::layout::Target::host(), mode: opts.mode, step_budget: None };
    let mut report = TestReport::default();
    for t in &prog.tests {
        let (mut out, mut err) = (Vec::new(), Vec::new());
        let outcome = gamma::interp::run_entry(&prog, &cfg, t.entry, &[], &mut out, &mut err);
        let reason = match outcome {
            gamma::interp::Outcome::Exit(0) => None,
            gamma::interp::Outcome::Exit(_) => Some(String::new()),
            gamma::interp::Outcome::Trap(trap) => Some(trap_report(&sess, &prog, &trap)),
        };
        let failure = reason.map(|reason| {
            let mut detail = String::from_utf8_lossy(&out).into_owned();
            detail.push_str(&String::from_utf8_lossy(&err));
            detail.push_str(&reason);
            detail
        });
        report.record(&t.name, failure);
    }
    report.finish()
}

/// `gamma test --backend=c`: compiles all tests into one executable, which runs
/// the test whose index it is given, and runs it once per test.
fn test_native(sess: &Session, prog: &gamma::tir::Program) -> ExitCode {
    let entries: Vec<_> = prog.tests.iter().map(|t| t.entry).collect();
    let generated = gamma::cgen::generate_tests(prog, &sess.sources, &entries, &c_options(sess));
    let Some(dir) = native_dir("test") else { return ExitCode::FAILURE };
    let exe = dir.join("tests");
    let built = generated.map_err(|u| cgen_error(sess, u)).and_then(|out| gamma::cgen::compile(&out, &exe).map(|_| ()));
    let code = match built {
        Ok(()) => {
            let mut report = TestReport::default();
            for (i, t) in prog.tests.iter().enumerate() {
                let mut cmd = std::process::Command::new(&exe);
                cmd.arg(i.to_string()).stdin(std::process::Stdio::null());
                let failure = match cmd.output() {
                    Ok(o) if o.status.code() == Some(0) => None,
                    Ok(o) => {
                        let mut detail = String::from_utf8_lossy(&o.stdout).into_owned();
                        detail.push_str(&native_stderr(&String::from_utf8_lossy(&o.stderr)));
                        Some(detail)
                    }
                    Err(e) => Some(format!("cannot run {}: {}\n", exe.display(), e)),
                };
                report.record(&t.name, failure);
            }
            report.finish()
        }
        Err(msg) => {
            eprintln!("{}", msg);
            ExitCode::FAILURE
        }
    };
    let _ = std::fs::remove_dir_all(&dir);
    code
}

/// The standard error of a failed native test as the interpreter reports it: a
/// trap's message and location are in it, without the `trap: ` prefix.
fn native_stderr(err: &str) -> String {
    const PREFIX: &str = "trap: ";
    match err.rfind(PREFIX) {
        Some(i) if i == 0 || err.as_bytes()[i - 1] == b'\n' => {
            format!("{}{}", &err[..i], &err[i + PREFIX.len()..])
        }
        _ => err.to_string(),
    }
}

/// The C back end options for the session's mode.
fn c_options(sess: &Session) -> gamma::cgen::Options {
    gamma::cgen::Options {
        checks: sess.mode != gamma::interp::Mode::Fast,
        debug: sess.mode == gamma::interp::Mode::Debug,
        debug_info: sess.debug_info,
    }
}

/// The message for a construct the C back end cannot translate.
fn cgen_error(sess: &Session, u: gamma::cgen::Unsupported) -> String {
    match location(sess, u.span) {
        Some(at) => format!("{}: error: {}", at, u.message),
        None => format!("error: {}", u.message),
    }
}

/// Translates the program to C and compiles it to `exe`.
fn compile_native(sess: &Session, prog: &gamma::tir::Program, exe: &std::path::Path) -> Result<(), String> {
    let Some(entry) = prog.main else { return Err("the program has no `main` function".into()) };
    let out = gamma::cgen::generate(prog, &sess.sources, entry, &c_options(sess)).map_err(|u| cgen_error(sess, u))?;
    gamma::cgen::compile(&out, exe).map(|_| ())
}

/// Creates a temporary directory for a native build, reporting a failure.
fn native_dir(what: &str) -> Option<PathBuf> {
    // A directory no other process uses: created here, never reused (a process id alone
    // can repeat across sandboxes sharing one temporary directory).
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    for attempt in 0..100u32 {
        let dir = std::env::temp_dir().join(format!("gamma-{}-{}-{:x}-{}", what, std::process::id(), nanos, attempt));
        match std::fs::create_dir(&dir) {
            Ok(()) => return Some(dir),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(e) => {
                eprintln!("gamma: cannot create {}: {}", dir.display(), e);
                return None;
            }
        }
    }
    eprintln!("gamma: cannot create a temporary directory in {}", std::env::temp_dir().display());
    None
}

fn build(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
    sess.debug_info = opts.debug_info;
    let prog = gamma::sema::analyze(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    let exe = opts.output.clone().unwrap_or_else(|| {
        let stem = path.file_stem().map_or("main".into(), |s| s.to_string_lossy().into_owned());
        PathBuf::from(stem)
    });
    match compile_native(&sess, &prog, &exe) {
        Ok(()) => ExitCode::SUCCESS,
        Err(msg) => {
            eprintln!("{}", msg);
            ExitCode::FAILURE
        }
    }
}

/// `gamma run --backend=c`: compiles to a temporary executable and runs it.
fn run_native(sess: &Session, prog: &gamma::tir::Program, path: &std::path::Path, opts: &Options) -> ExitCode {
    let Some(dir) = native_dir("run") else { return ExitCode::FAILURE };
    let exe = dir.join("prog");
    let result = compile_native(sess, prog, &exe);
    let code = match result {
        Ok(()) => {
            let mut cmd = std::process::Command::new(&exe);
            // The program sees the source path as its name, as with the interpreter.
            #[cfg(unix)]
            std::os::unix::process::CommandExt::arg0(&mut cmd, path);
            let status = cmd.args(&opts.program_args).status();
            match status {
                Ok(s) => ExitCode::from(s.code().map_or(134, |c| c as u8)),
                Err(e) => {
                    eprintln!("gamma: cannot run {}: {}", exe.display(), e);
                    ExitCode::FAILURE
                }
            }
        }
        Err(msg) => {
            eprintln!("{}", msg);
            ExitCode::FAILURE
        }
    };
    let _ = std::fs::remove_dir_all(&dir);
    code
}

/// `gamma fmt [--check] <file or directory>... (`-` filters stdin to stdout)`
fn fmt_command(args: Vec<String>) -> ExitCode {
    let mut check = false;
    let mut paths = Vec::new();
    for a in args {
        match a.as_str() {
            "--check" => check = true,
            s if s.starts_with('-') && s != "-" => {
                eprintln!("gamma: unknown option `{}`\n\n{}", s, USAGE);
                return ExitCode::from(2);
            }
            _ => paths.push(PathBuf::from(a)),
        }
    }
    if paths.is_empty() {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    }
    if paths == [PathBuf::from("-")] {
        // Filter mode: standard input to standard output.
        let mut src = String::new();
        if let Err(e) = std::io::Read::read_to_string(&mut std::io::stdin(), &mut src) {
            eprintln!("gamma: <stdin>: {}", e);
            return ExitCode::FAILURE;
        }
        return match gamma::fmt::format_source(&src) {
            Ok(out) => {
                print!("{}", out);
                ExitCode::SUCCESS
            }
            Err(e) => {
                eprintln!("<stdin>:{}", e);
                ExitCode::FAILURE
            }
        };
    }
    let mut files = Vec::new();
    for p in &paths {
        if let Err(e) = collect_gm_files(p, &mut files) {
            eprintln!("gamma: {}: {}", p.display(), e);
            return ExitCode::FAILURE;
        }
    }
    let mut failed = false;
    for f in &files {
        let src = match std::fs::read_to_string(f) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("gamma: {}: {}", f.display(), e);
                failed = true;
                continue;
            }
        };
        match gamma::fmt::format_source(&src) {
            Ok(out) if out != src => {
                if check {
                    println!("{}", f.display());
                    failed = true;
                } else if let Err(e) = std::fs::write(f, out) {
                    eprintln!("gamma: {}: {}", f.display(), e);
                    failed = true;
                }
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("{}:{}", f.display(), e);
                failed = true;
            }
        }
    }
    if failed { ExitCode::FAILURE } else { ExitCode::SUCCESS }
}

/// Adds `path` (a file) or every `.gm` file below it (a directory) to `out`, in name order.
fn collect_gm_files(path: &std::path::Path, out: &mut Vec<PathBuf>) -> std::io::Result<()> {
    if path.is_dir() {
        let mut entries: Vec<PathBuf> =
            std::fs::read_dir(path)?.map(|e| e.map(|e| e.path())).collect::<Result<_, _>>()?;
        entries.sort();
        for e in entries {
            if e.is_dir() || e.extension().is_some_and(|x| x == "gm") {
                collect_gm_files(&e, out)?;
            }
        }
    } else {
        std::fs::metadata(path)?;
        out.push(path.to_path_buf());
    }
    Ok(())
}
