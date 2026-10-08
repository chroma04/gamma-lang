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
  run     run a program with the interpreter; arguments after the file go to the program
  test    run the `test` declarations of a module
  help    print this message
  version print the compiler version

options:
  --diag-format=human|short   how to print diagnostics (default: human)
  --dump                      (parse) print the syntax tree
";

struct Options {
    command: String,
    file: Option<PathBuf>,
    /// Arguments for the program (`gamma run file.gm args…`).
    program_args: Vec<String>,
    format: DiagFormat,
    dump: bool,
}

fn parse_args() -> Result<Options, String> {
    let mut args = std::env::args().skip(1);
    let command = args.next().ok_or_else(|| "missing command".to_string())?;
    let mut opts = Options { command, file: None, program_args: vec![], format: DiagFormat::Human, dump: false };
    for arg in args {
        if opts.file.is_some() && opts.command == "run" {
            opts.program_args.push(arg);
            continue;
        }
        match arg.as_str() {
            "--dump" => opts.dump = true,
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
    let prog = gamma::sema::analyze(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    let cfg = gamma::interp::RunConfig {
        target: gamma::layout::Target::host(),
        mode: gamma::interp::Mode::Debug,
        step_budget: None,
    };
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
    let prog = gamma::sema::analyze_tests(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(opts.format));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    let cfg = gamma::interp::RunConfig {
        target: gamma::layout::Target::host(),
        mode: gamma::interp::Mode::Debug,
        step_budget: None,
    };
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
