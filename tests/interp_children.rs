//! Child processes that a program started in the interpreter and never waited for are
//! reaped when its run ends, so that a host running many programs (the test runner) does not
//! collect zombies.

#![cfg(target_os = "linux")]

use std::time::{Duration, Instant};

const SRC: &str = r#"
process :: import "std/process"

main :: fn() needs Proc {
    _ = Proc.spawn([_]string("true"), process.SpawnOptions())
    _ = Proc.spawn([_]string("sh", "-c", "sleep 0.3"), process.SpawnOptions(stdin: .Null))
}
"#;

/// The processes that are children of this one and have exited without being waited for.
fn zombies() -> usize {
    let me = std::process::id().to_string();
    let mut n = 0;
    for entry in std::fs::read_dir("/proc").unwrap().flatten() {
        let Ok(stat) = std::fs::read_to_string(entry.path().join("stat")) else { continue };
        // "pid (comm) state ppid …": the name may hold anything, so parse after its `)`.
        let Some(rest) = stat.rsplit_once(')').map(|(_, r)| r) else { continue };
        let mut fields = rest.split_whitespace();
        if fields.next() == Some("Z") && fields.next() == Some(me.as_str()) {
            n += 1;
        }
    }
    n
}

#[test]
fn children_that_were_not_waited_for_are_reaped() {
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("interp_children");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.gm");
    std::fs::write(&path, SRC).unwrap();
    let mut sess = gamma::driver::Session::new();
    let prog = gamma::sema::analyze(&mut sess, &path);
    let Some(prog) = prog else { panic!("{}", sess.render_diagnostics(gamma::diag::DiagFormat::Human)) };
    let cfg = gamma::interp::RunConfig {
        target: prog.target.clone(),
        mode: gamma::interp::Mode::Debug,
        step_budget: None,
        deadline: None,
    };
    let (mut out, mut err) = (Vec::new(), Vec::new());
    let outcome = gamma::interp::run_main(&prog, &cfg, &[], &mut out, &mut err);
    assert_eq!(outcome, gamma::interp::Outcome::Exit(0), "{}", String::from_utf8_lossy(&err));
    // The second child outlives the run; a helper thread reaps it when it ends.
    std::thread::sleep(Duration::from_millis(600));
    let deadline = Instant::now() + Duration::from_secs(10);
    while zombies() > 0 && Instant::now() < deadline {
        std::thread::sleep(Duration::from_millis(20));
    }
    assert_eq!(zombies(), 0);
}
