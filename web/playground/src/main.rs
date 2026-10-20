//! The Gamma interpreter for the web playground: a WASI command that reads a program from
//! standard input and checks and runs it (`gamma-play run`) or formats it
//! (`gamma-play fmt`). The standard library is embedded; there is no file system.
//!
//! Output: the program's standard output on fd 1; diagnostics, traps and the program's
//! standard error on fd 2. The exit status is that of `gamma run`: the program's status,
//! 1 for compile errors, 134 for a trap.

use gamma::driver::Session;
use gamma::interp::{Outcome, RunConfig};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

mod embedded {
    include!(concat!(env!("OUT_DIR"), "/std_files.rs"));
}

const MAIN: &str = "/play/main.gm";

fn main() -> ExitCode {
    let mut src = String::new();
    if std::io::stdin().read_to_string(&mut src).is_err() {
        eprintln!("gamma: the program is not valid UTF-8");
        return ExitCode::from(2);
    }
    match std::env::args().nth(1).as_deref() {
        Some("fmt") => match gamma::fmt::format_source(&src) {
            Ok(out) => {
                print!("{}", out);
                ExitCode::SUCCESS
            }
            Err(e) => {
                eprintln!("{}", e);
                ExitCode::FAILURE
            }
        },
        _ => run(src),
    }
}

fn run(src: String) -> ExitCode {
    // SAFETY: single-threaded; nothing else reads the environment yet.
    unsafe { std::env::set_var("GAMMA_STD", "/std") };
    let mut sess = Session::new();
    for (rel, text) in embedded::STD {
        sess.overlays.insert(Path::new("/std").join(rel), text.to_string());
    }
    sess.overlays.insert(PathBuf::from(MAIN), src);
    let path = Path::new(MAIN);
    let prog = gamma::sema::analyze(&mut sess, path);
    eprint!("{}", sess.render_diagnostics(gamma::diag::DiagFormat::Human));
    let Some(prog) = prog else { return ExitCode::FAILURE };
    let cfg = RunConfig { target: prog.target.clone(), mode: sess.mode, step_budget: None };
    if let Err(e) = gamma::interp::foreign_supported(&prog, &cfg) {
        eprintln!("gamma: cannot run this program here: {}", e);
        return ExitCode::FAILURE;
    }
    let mut stdout = std::io::stdout();
    let mut stderr = std::io::stderr();
    let args = vec!["main.gm".to_string()];
    match gamma::interp::run_main(&prog, &cfg, &args, &mut stdout, &mut stderr) {
        Outcome::Exit(code) => ExitCode::from(code),
        Outcome::Trap(t) => {
            let _ = stdout.flush();
            let mut msg = format!("trap: {}\n", t.message);
            if t.span != gamma::source::Span::DUMMY {
                let f = sess.sources.file(t.span.file);
                let (line, col) = f.line_col(t.span.lo);
                msg.push_str(&format!("  at {}:{}:{}\n", f.path.display(), line, col));
            }
            eprint!("{}", msg);
            ExitCode::from(134)
        }
    }
}
