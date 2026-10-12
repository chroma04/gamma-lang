//! The C toolchain from the environment: `CC` with arguments, `CFLAGS`, `LDFLAGS`,
//! and the target a cross compiler implies.
//!
//! The cross-compilation tests need `clang` (which targets every architecture it
//! was built for); the end-to-end test also needs `aarch64-linux-gnu-gcc` and a way
//! to run AArch64 Linux executables (such as qemu-user with binfmt). Each test is
//! skipped, with a note, when what it needs is missing.

use gamma::toolchain::Toolchain;
use std::path::PathBuf;
use std::process::{Command, Output};

const PROGRAM: &str = "\
S :: struct { a: u8, p: rawptr, c: u16 }

main :: fn() \\ {Out} {
    when target.arch is .Aarch64 {
        print(\"aarch64\\n\", ())
    } else when target.arch is .X86_64 {
        print(\"x86_64\\n\", ())
    } else {
        print(\"other\\n\", ())
    }
    when target.endian is .Big {
        print(\"big\\n\", ())
    } else {
        print(\"little\\n\", ())
    }
    print(\"{} {} {}\\n\", (target.ptr_bits, @size_of(S), @offset_of(S, c)))
}
";

/// A fresh directory holding the program; returns the directory and the source.
fn setup(name: &str) -> (PathBuf, PathBuf) {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("cross").join(name);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let src = dir.join("layout.gm");
    std::fs::write(&src, PROGRAM).unwrap();
    (dir, src)
}

/// Runs `gamma` with `args` and the toolchain variables `env` (the others unset).
fn gamma(args: &[&str], env: &[(&str, &str)]) -> Output {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_gamma"));
    cmd.args(args).env_remove("CC").env_remove("CFLAGS").env_remove("LDFLAGS");
    for (k, v) in env {
        cmd.env(k, v);
    }
    cmd.output().unwrap()
}

fn text(b: &[u8]) -> String {
    String::from_utf8_lossy(b).into_owned()
}

fn runs(program: &str) -> bool {
    Command::new(program).arg("--version").output().is_ok_and(|o| o.status.success())
}

/// The C toolchain of the environment (`CC`, default `cc`), if its compiler runs.
fn env_toolchain() -> Option<Toolchain> {
    let t = Toolchain::from_env().ok()?;
    t.base_command().arg("--version").output().ok()?.status.success().then_some(t)
}

/// Words quoted for the toolchain variables.
fn quote(words: &[String]) -> String {
    words.iter().map(|w| format!("'{}'", w)).collect::<Vec<_>>().join(" ")
}

#[test]
fn cc_with_arguments() {
    let Some(t) = env_toolchain() else {
        eprintln!("note: skipping: needs a C compiler");
        return;
    };
    let (dir, src) = setup("cc-args");
    let exe = dir.join("layout");
    let (cflags, ldflags) = (quote(&t.cflags), quote(&t.ldflags));
    // An argument added to CC, and CC with its program quoted.
    for cc in [format!("{} -DGAMMA_UNUSED=1", quote(&t.cc)), format!("'{}' -O1 {}", t.cc[0], quote(&t.cc[1..]))] {
        let env = [("CC", cc.as_str()), ("CFLAGS", &cflags), ("LDFLAGS", &ldflags)];
        let out = gamma(&["build", "-o", exe.to_str().unwrap(), src.to_str().unwrap()], &env);
        assert!(out.status.success(), "CC={}:\n{}", cc, text(&out.stderr));
        match Command::new(&exe).output() {
            Ok(run) => assert!(text(&run.stdout).contains("little\n"), "{}", text(&run.stdout)),
            Err(e) => eprintln!("note: built with CC={} but cannot run the result: {}", cc, e),
        }
    }
}

#[test]
fn cflags_and_ldflags_reach_the_compiler() {
    let Some(t) = env_toolchain() else {
        eprintln!("note: skipping: needs a C compiler");
        return;
    };
    let cc = quote(&t.cc);
    let (dir, src) = setup("flags");
    let exe = dir.join("layout");
    let args = ["build", "-o", exe.to_str().unwrap(), src.to_str().unwrap()];
    let out = gamma(&args, &[("CC", &cc), ("CFLAGS", "-include gamma-no-such-header.h")]);
    assert!(!out.status.success());
    assert!(text(&out.stderr).contains("gamma-no-such-header.h"), "{}", text(&out.stderr));
    let out = gamma(&args, &[("CC", &cc), ("LDFLAGS", "-lgamma-no-such-library")]);
    assert!(!out.status.success());
    assert!(text(&out.stderr).contains("gamma-no-such-library"), "{}", text(&out.stderr));
    let out = gamma(&args, &[("CC", &cc), ("CFLAGS", "-DX='unterminated")]);
    assert!(!out.status.success());
    assert!(text(&out.stderr).contains("CFLAGS"), "{}", text(&out.stderr));
}

/// The target follows the compiler: the interpreter, which needs no C headers or
/// libraries, sees AArch64 layouts and constants when CC targets AArch64.
#[test]
fn target_follows_the_compiler() {
    if !runs("clang") {
        eprintln!("note: skipping: needs clang");
        return;
    }
    let (_, src) = setup("target");
    let src = src.to_str().unwrap();
    let out = gamma(&["run", src], &[("CC", "clang --target=aarch64-linux-gnu")]);
    assert_eq!(text(&out.stdout), "aarch64\nlittle\n64 24 16\n", "{}", text(&out.stderr));
    let out = gamma(&["run", src], &[("CC", "clang"), ("CFLAGS", "--target=x86_64-linux-gnu")]);
    assert_eq!(text(&out.stdout), "x86_64\nlittle\n64 24 16\n", "{}", text(&out.stderr));
    // A 4-byte-pointer target with `__int128` (wasm32 in clang).
    let out = gamma(&["run", src], &[("CC", "clang --target=wasm32-wasi")]);
    assert_eq!(text(&out.stdout), "other\nlittle\n32 12 8\n", "{}", text(&out.stderr));
    let out = gamma(&["run", src], &[("CC", "clang --target=s390x-linux-gnu")]);
    assert_eq!(text(&out.stdout), "other\nbig\n64 24 16\n", "{}", text(&out.stderr));
}

#[test]
fn unsupported_targets_are_reported() {
    if !runs("clang") {
        eprintln!("note: skipping: needs clang");
        return;
    }
    let (dir, src) = setup("unsupported");
    let exe = dir.join("layout");
    let args = ["build", "-o", exe.to_str().unwrap(), src.to_str().unwrap()];
    let out = gamma(&args, &[("CC", "clang --target=i686-linux-gnu")]);
    assert!(!out.status.success());
    assert!(text(&out.stderr).contains("without `__int128`"), "{}", text(&out.stderr));
    let out = gamma(&args, &[("CC", "clang --target=s390x-linux-gnu")]);
    assert!(!out.status.success());
    assert!(text(&out.stderr).contains("big-endian targets"), "{}", text(&out.stderr));
}

/// A whole cross build: compiled by a cross compiler and run under emulation.
#[test]
fn cross_build_runs() {
    let cc = "aarch64-linux-gnu-gcc";
    if !runs(cc) {
        eprintln!("note: skipping: needs {}", cc);
        return;
    }
    let (dir, src) = setup("aarch64");
    let exe = dir.join("layout");
    let out =
        gamma(&["build", "-o", exe.to_str().unwrap(), src.to_str().unwrap()], &[("CC", cc), ("LDFLAGS", "-static")]);
    assert!(out.status.success(), "{}", text(&out.stderr));
    match Command::new(&exe).output() {
        Ok(run) => assert_eq!(text(&run.stdout), "aarch64\nlittle\n64 24 16\n"),
        Err(e) => eprintln!("note: built for AArch64 but cannot run it here: {}", e),
    }
}
