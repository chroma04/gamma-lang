//! A run in the interpreter with a deadline ends when the deadline comes, also while the
//! program sleeps.

use std::time::{Duration, Instant};

const SRC: &str = r#"
time :: import "std/time"

main :: fn() needs Clock {
    Clock.sleep(time.secs(3600))
}
"#;

#[test]
fn a_sleep_past_the_deadline_ends_the_run_at_the_deadline() {
    let dir = std::path::Path::new(env!("CARGO_TARGET_TMPDIR")).join("interp_deadline");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("main.gm");
    std::fs::write(&path, SRC).unwrap();
    let mut sess = gamma::driver::Session::new();
    let prog = gamma::sema::analyze(&mut sess, &path);
    let Some(prog) = prog else { panic!("{}", sess.render_diagnostics(gamma::diag::DiagFormat::Human)) };
    let started = Instant::now();
    let cfg = gamma::interp::RunConfig {
        target: prog.target.clone(),
        mode: gamma::interp::Mode::Debug,
        step_budget: None,
        deadline: Some(started + Duration::from_millis(300)),
    };
    let (mut out, mut err) = (Vec::new(), Vec::new());
    let outcome = gamma::interp::run_main(&prog, &cfg, &[], &mut out, &mut err);
    let elapsed = started.elapsed();
    match outcome {
        gamma::interp::Outcome::Trap(t) => assert_eq!(t.message, gamma::interp::DEADLINE_TRAP),
        other => panic!("{:?}", other),
    }
    assert!(elapsed >= Duration::from_millis(300), "{:?}", elapsed);
    assert!(elapsed < Duration::from_secs(30), "{:?}", elapsed);
}
