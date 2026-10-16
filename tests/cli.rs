//! Tests of the `gamma` command line itself: help for every command.

use std::process::Command;

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

fn gamma(args: &[&str]) -> (bool, String) {
    let out = Command::new(GAMMA).args(args).output().unwrap();
    (out.status.success(), String::from_utf8_lossy(&out.stdout).into_owned())
}

#[test]
fn every_command_takes_help() {
    let commands = [
        "lex", "parse", "check", "run", "test", "bench", "build", "repl", "fmt", "doc", "lsp", "new", "init", "add",
        "remove", "fetch", "update", "cache",
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
