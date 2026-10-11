//! Tests of the C back end's runtime (`runtime/gm_rt.c`).
//!
//! `runtime/rt_test.c` includes the runtime and checks it directly. This test compiles it
//! with the system C compiler (`$CC`, else `cc`), runs the checks, and runs the scenarios in
//! which the runtime ends the process (traps) to check their exit status and output. Without
//! a usable C compiler the tests are skipped.

use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::OnceLock;

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

/// Compiles `rt_test.c` once; `None` if there is no C compiler to do it with.
fn test_binary() -> Option<&'static Path> {
    static BIN: OnceLock<Option<PathBuf>> = OnceLock::new();
    BIN.get_or_init(build).as_deref()
}

fn build() -> Option<PathBuf> {
    let cc = std::env::var("CC").ok().filter(|s| !s.is_empty()).unwrap_or_else(|| "cc".to_string());
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("runtime-test");
    std::fs::create_dir_all(&dir).ok()?;
    let exe = dir.join(if cfg!(windows) { "rt_test.exe" } else { "rt_test" });
    let out = Command::new(&cc)
        .args(["-std=gnu11", "-O1", "-pthread", "-Wall", "-Werror", "-Wno-unused-function", "-I"])
        .arg(root().join("runtime"))
        .arg(root().join("runtime/rt_test.c"))
        .arg("-o")
        .arg(&exe)
        .output();
    match out {
        Err(e) => {
            eprintln!("note: skipping the runtime tests: cannot run the C compiler `{}`: {}", cc, e);
            None
        }
        Ok(o) if !o.status.success() => {
            panic!(
                "`{}` failed to compile runtime/rt_test.c:\n{}{}",
                cc,
                String::from_utf8_lossy(&o.stdout),
                String::from_utf8_lossy(&o.stderr)
            );
        }
        Ok(_) => Some(exe),
    }
}

/// Runs the test binary with `args`, or returns `None` when it could not be built.
fn run(args: &[&str]) -> Option<Output> {
    let exe = test_binary()?;
    Some(Command::new(exe).args(args).output().expect("run the compiled runtime test"))
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

#[test]
fn checks_pass() {
    let Some(out) = run(&[]) else { return };
    assert!(out.status.success(), "runtime checks failed:\n{}", text(&out.stderr));
    assert_eq!(text(&out.stdout), "runtime ok\n");
}

#[test]
fn trap_flushes_stdout_and_reports_location() {
    let Some(out) = run(&["trap"]) else { return };
    assert_exit_134(&out);
    assert_eq!(text(&out.stdout), "before\n");
    assert_eq!(text(&out.stderr), "err\ntrap: boom\n  at f.gm:3:4\n");
}

#[test]
fn trap_without_location_has_one_line() {
    let Some(out) = run(&["trap-nofile"]) else { return };
    assert_exit_134(&out);
    assert_eq!(text(&out.stderr), "trap: bytes!\n");
}

#[test]
fn double_free_traps() {
    let Some(out) = run(&["double-free"]) else { return };
    assert_exit_134(&out);
    let err = text(&out.stderr);
    assert!(err.starts_with("trap: double free of heap block 0x"), "{}", err);
    assert!(err.ends_with("\n  at d.gm:7:8\n"), "{}", err);
}

#[test]
fn invalid_free_traps() {
    let Some(out) = run(&["invalid-free"]) else { return };
    assert_exit_134(&out);
    let err = text(&out.stderr);
    assert!(err.starts_with("trap: invalid free of 0x"), "{}", err);
    assert!(err.contains(": not the start of a live heap block of 4 bytes\n  at i.gm:1:2\n"), "{}", err);
}

#[test]
fn free_with_the_wrong_size_traps() {
    let Some(out) = run(&["wrong-size"]) else { return };
    assert_exit_134(&out);
    let err = text(&out.stderr);
    assert!(err.starts_with("trap: invalid free of 0x"), "{}", err);
    assert!(err.contains(": the heap block there has 16 bytes, not 8\n  at w.gm:1:2\n"), "{}", err);
}

/// The status of a process killed by `abort` as a shell reports it.
fn assert_exit_134(out: &Output) {
    assert_eq!(out.status.code(), Some(134), "stderr: {}", text(&out.stderr));
}
