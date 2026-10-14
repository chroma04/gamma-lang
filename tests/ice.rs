//! Tests of the report printed for an internal compiler error.

use std::process::Command;

fn gamma() -> Command {
    Command::new(env!("CARGO_BIN_EXE_gamma"))
}

#[test]
fn panic_prints_a_short_report_and_exits_101() {
    let out = gamma().arg("version").env("GAMMA_DEBUG_PANIC", "1").env_remove("RUST_BACKTRACE").output().unwrap();
    assert_eq!(out.status.code(), Some(101));
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(err.starts_with("internal compiler error: requested by GAMMA_DEBUG_PANIC at src/main.rs:"), "{err}");
    assert!(err.contains("gamma version"), "{err}");
    assert!(!err.contains("stack backtrace"), "{err}");
    assert!(!err.contains("panicked at"), "{err}");
}

#[test]
fn backtrace_is_printed_only_when_requested() {
    let out = gamma().arg("version").env("GAMMA_DEBUG_PANIC", "1").env("RUST_BACKTRACE", "1").output().unwrap();
    assert_eq!(out.status.code(), Some(101));
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(err.contains("internal compiler error"), "{err}");
    assert!(err.contains("panicked at"), "{err}");
}
