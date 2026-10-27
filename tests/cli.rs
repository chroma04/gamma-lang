//! Tests of the `gamma` command line itself: help for every command.

mod common;

fn gamma(args: &[&str]) -> (bool, String) {
    let out = common::gamma().args(args).output().unwrap();
    (out.status.success(), String::from_utf8_lossy(&out.stdout).into_owned())
}

#[test]
fn every_command_takes_help() {
    let commands = [
        "lex", "parse", "check", "fix", "run", "test", "bench", "build", "repl", "fmt", "doc", "lsp", "new", "init",
        "add", "remove", "fetch", "update", "cache", "lint",
    ];
    for cmd in commands {
        for flag in ["--help", "-h"] {
            let (ok, out) = gamma(&[cmd, flag]);
            assert!(ok, "`gamma {} {}` failed", cmd, flag);
            assert!(out.contains("usage: gamma"), "`gamma {} {}` printed {:?}", cmd, flag, out);
        }
    }
    // The details of a command come with its help.
    assert!(gamma(&["doc", "--help"]).1.contains("usage: gamma doc"));
    assert!(gamma(&["help", "doc"]).1.contains("usage: gamma doc"));
    assert!(gamma(&["add", "--help"]).1.contains("gamma add"));
}

#[test]
fn help_after_the_program_belongs_to_the_program() {
    let dir = std::path::PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("cli");
    std::fs::create_dir_all(&dir).unwrap();
    let file = dir.join("args.gm");
    std::fs::write(&file, "main :: fn() needs Out + Env {\n    for a in Env.args() { print(\"{}\\n\", a) }\n}\n")
        .unwrap();
    let (ok, out) = gamma(&["run", file.to_str().unwrap(), "--help"]);
    assert!(ok);
    assert!(out.ends_with("--help\n"), "{:?}", out);
    assert!(!out.contains("usage: gamma"));
}

#[cfg(unix)]
#[test]
fn arguments_that_are_not_utf8_are_usage_errors() {
    use std::os::unix::ffi::OsStrExt;
    let out = common::gamma().arg("check").arg(std::ffi::OsStr::from_bytes(b"\xff.gm")).output().unwrap();
    assert_eq!(out.status.code(), Some(2));
    assert!(String::from_utf8_lossy(&out.stderr).contains("not valid UTF-8"));
}

/// What the program prints for its arguments: each on a line, as raw bytes.
#[cfg(unix)]
#[test]
fn program_arguments_pass_through_as_bytes() {
    use std::os::unix::ffi::OsStrExt;
    let dir = std::path::PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("cli-bytes");
    std::fs::create_dir_all(&dir).unwrap();
    let file = dir.join("args.gm");
    std::fs::write(&file, "main :: fn() needs Out + Env {\n    for a in Env.args() { print(\"{}\\n\", a) }\n}\n")
        .unwrap();
    let mut backends = vec!["--backend=interp"];
    if common::c_backend_available() {
        backends.push("--backend=c");
    }
    for backend in backends {
        let mut cmd = common::gamma();
        cmd.arg("run").arg(backend).arg(&file).arg(std::ffi::OsStr::from_bytes(b"a\xff\xfeb")).arg("plain");
        let out = common::output(&mut cmd).unwrap();
        assert!(out.status.success(), "{}: {}", backend, String::from_utf8_lossy(&out.stderr));
        let tail: &[u8] = b"a\xff\xfeb\nplain\n";
        assert!(out.stdout.ends_with(tail), "{}: {:?}", backend, out.stdout);
    }
    // Options of gamma itself must still be text.
    let out = common::gamma().arg("run").arg(std::ffi::OsStr::from_bytes(b"--mode=\xff")).arg(&file).output().unwrap();
    assert_eq!(out.status.code(), Some(2));
    assert!(String::from_utf8_lossy(&out.stderr).contains("not valid UTF-8"));
}
