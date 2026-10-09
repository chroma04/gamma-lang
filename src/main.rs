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
  test    run the `test` declarations of a module
  help    print this message
  version print the compiler version

options:
  --diag-format=human|short   how to print diagnostics (default: human)
  --dump                      (parse) print the syntax tree
  --backend=interp|c          (run) run with the interpreter (default) or compile via C
  -o <file>                   (build) the executable to write (default: the input's name)
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

fn main() -> ExitCode {
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
            eprintln!("trap: {}", t.message);
            if t.span != gamma::source::Span::DUMMY {
                let f = sess.sources.file(t.span.file);
                let (line, col) = f.line_col(t.span.lo);
                eprintln!("  at {}:{}:{}", f.path.display(), line, col);
            }
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

/// Runs each test of the root module with captured output (§3.8). A failing
/// test's output and failure reason are printed after the summary line for it.
fn test(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
    let prog = gamma::sema::analyze_tests(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    let cfg = gamma::interp::RunConfig { target: gamma::layout::Target::host(), mode: opts.mode, step_budget: None };
    let (mut passed, mut failed) = (0, 0);
    for t in &prog.tests {
        let (mut out, mut err) = (Vec::new(), Vec::new());
        let outcome = gamma::interp::run_entry(&prog, &cfg, t.entry, &[], &mut out, &mut err);
        let failure = match outcome {
            gamma::interp::Outcome::Exit(0) => None,
            gamma::interp::Outcome::Exit(_) => Some(String::new()),
            gamma::interp::Outcome::Trap(trap) => Some(match location(&sess, trap.span) {
                Some(at) => format!("{}\n  at {}\n", trap.message, at),
                None => format!("{}\n", trap.message),
            }),
        };
        match failure {
            None => {
                passed += 1;
                println!("test \"{}\" ... ok", t.name);
            }
            Some(reason) => {
                failed += 1;
                println!("test \"{}\" ... FAILED", t.name);
                let mut detail = String::from_utf8_lossy(&out).into_owned();
                detail.push_str(&String::from_utf8_lossy(&err));
                detail.push_str(&reason);
                for line in detail.lines() {
                    println!("    {}", line);
                }
            }
        }
    }
    println!("{} passed, {} failed", passed, failed);
    if failed == 0 { ExitCode::SUCCESS } else { ExitCode::FAILURE }
}

/// Translates the program to C and compiles it to `exe`.
fn compile_native(sess: &Session, prog: &gamma::tir::Program, exe: &std::path::Path) -> Result<(), String> {
    let Some(entry) = prog.main else { return Err("the program has no `main` function".into()) };
    let opts = gamma::cgen::Options {
        checks: sess.mode != gamma::interp::Mode::Fast,
        debug: sess.mode == gamma::interp::Mode::Debug,
    };
    let out = gamma::cgen::generate(prog, &sess.sources, entry, &opts).map_err(|u| match location(sess, u.span) {
        Some(at) => format!("{}: error: {}", at, u.message),
        None => format!("error: {}", u.message),
    })?;
    gamma::cgen::compile(&out.code, &out.libs, exe).map(|_| ())
}

fn build(opts: &Options) -> ExitCode {
    let Some(path) = &opts.file else {
        eprintln!("gamma: no input file\n\n{}", USAGE);
        return ExitCode::from(2);
    };
    let mut sess = Session::new();
    sess.mode = opts.mode;
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
    let dir = std::env::temp_dir().join(format!("gamma-run-{}", std::process::id()));
    if let Err(e) = std::fs::create_dir_all(&dir) {
        eprintln!("gamma: cannot create {}: {}", dir.display(), e);
        return ExitCode::FAILURE;
    }
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
