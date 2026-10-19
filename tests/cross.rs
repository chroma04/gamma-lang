//! The C toolchain from the environment: `CC` with arguments, `CFLAGS`, `LDFLAGS`,
//! and the target a cross compiler implies.
//!
//! The cross-compilation tests need `clang` (which targets every architecture it
//! was built for); the end-to-end tests also need a cross gcc (`aarch64-linux-gnu-gcc`,
//! `s390x-linux-gnu-gcc`) and a way to run its executables (qemu-user, with binfmt or
//! through `GAMMA_RUNNER`). Each test is skipped, with a note, when what it needs is
//! missing.

use gamma::toolchain::Toolchain;
use std::path::PathBuf;
use std::process::{Command, Output};

const PROGRAM: &str = "\
S :: struct { a: u8, p: rawptr, c: u16 }

main :: fn() needs Out {
    when target.arch is .Aarch64 {
        print(\"aarch64\\n\")
    } else when target.arch is .X86_64 {
        print(\"x86_64\\n\")
    } else when target.arch is .S390x {
        print(\"s390x\\n\")
    } else {
        print(\"other\\n\")
    }
    when target.endian is .Big {
        print(\"big\\n\")
    } else {
        print(\"little\\n\")
    }
    print(\"{} {} {}\\n\", target.ptr_bits, @size_of(S), @offset_of(S, c))
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

/// Runs the executable `exe`, behind `GAMMA_RUNNER` if that is set; `Err` if it cannot be run.
fn run_exe(exe: &std::path::Path) -> std::io::Result<Output> {
    let runner = gamma::toolchain::runner_from_env().unwrap();
    match runner.split_first() {
        Some((prog, args)) => Command::new(prog).args(args).arg(exe).output(),
        None => Command::new(exe).output(),
    }
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
        let order = if t.target().unwrap().little_endian { "little\n" } else { "big\n" };
        match run_exe(&exe) {
            Ok(run) => assert!(text(&run.stdout).contains(order), "{}", text(&run.stdout)),
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
    let (dir, src) = setup("target");
    let src = src.to_str().unwrap();
    let out = gamma(&["run", src], &[("CC", "clang --target=aarch64-linux-gnu")]);
    assert_eq!(text(&out.stdout), "aarch64\nlittle\n64 24 16\n", "{}", text(&out.stderr));
    let out = gamma(&["run", src], &[("CC", "clang"), ("CFLAGS", "--target=x86_64-linux-gnu")]);
    assert_eq!(text(&out.stdout), "x86_64\nlittle\n64 24 16\n", "{}", text(&out.stderr));
    // A 4-byte-pointer target with `__int128` (wasm32 in clang). A compiler that defines no
    // operating system makes a freestanding program (§3.7.1), which has no `main` to run, so
    // its layouts are checked at compile time.
    let wasm = dir.join("wasm_layout.gm");
    std::fs::write(
        &wasm,
        "S :: struct { a: u8, p: rawptr, c: u16 }\n@static_assert(target.ptr_bits is 32)\n@static_assert(@size_of(S) is 12)\n@static_assert(@offset_of(S, c) is 8)\n",
    )
    .unwrap();
    let out = gamma(&["check", wasm.to_str().unwrap()], &[("CC", "clang --target=wasm32-wasi")]);
    assert!(out.status.success(), "{}", text(&out.stderr));
    let out = gamma(&["run", src], &[("CC", "clang --target=s390x-linux-gnu")]);
    assert_eq!(text(&out.stdout), "s390x\nbig\n64 24 16\n", "{}", text(&out.stderr));
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
    // Directly (binfmt), else under qemu-user.
    let run = Command::new(&exe).output().or_else(|_| Command::new("qemu-aarch64").arg(&exe).output());
    match run {
        Ok(run) => assert_eq!(text(&run.stdout), "aarch64\nlittle\n64 24 16\n"),
        Err(e) => eprintln!("note: built for AArch64 but cannot run it here: {}", e),
    }
    // `--target` finds the same compiler.
    let out = gamma(&["build", "--target=aarch64-linux-gnu", "-o", exe.to_str().unwrap(), src.to_str().unwrap()], &[]);
    assert!(out.status.success(), "{}", text(&out.stderr));
}

/// A big-endian cross build with `--target`, run under emulation through `GAMMA_RUNNER`.
#[test]
fn big_endian_cross_build_runs() {
    let cc = "s390x-linux-gnu-gcc";
    if !runs(cc) || !runs("qemu-s390x") {
        eprintln!("note: skipping: needs {} and qemu-s390x", cc);
        return;
    }
    let (dir, src) = setup("s390x");
    let exe = dir.join("layout");
    let src = src.to_str().unwrap();
    let env = [("LDFLAGS", "-static"), ("GAMMA_RUNNER", "qemu-s390x -L /usr/s390x-linux-gnu")];
    let out = gamma(&["build", "--target=s390x-linux-gnu", "-o", exe.to_str().unwrap(), src], &env);
    assert!(out.status.success(), "{}", text(&out.stderr));
    let run = Command::new("qemu-s390x").arg(&exe).output().unwrap();
    assert_eq!(text(&run.stdout), "s390x\nbig\n64 24 16\n", "{}", text(&run.stderr));
    // `run --backend=c` and `test` start the executable behind the runner.
    let out = gamma(&["run", "--backend=c", "--target=s390x-linux-gnu", src], &env);
    assert_eq!(text(&out.stdout), "s390x\nbig\n64 24 16\n", "{}", text(&out.stderr));
}

/// `--target` picks the compiler and checks that it produces code for the triple.
#[test]
fn target_flag_checks_the_compiler() {
    let (_, src) = setup("target-flag");
    let src = src.to_str().unwrap();
    // Without a compiler for the triple, say so.
    let out = gamma(&["check", "--target=riscv64-nowhere-gnu", src], &[("PATH", "/nonexistent")]);
    assert!(!out.status.success());
    let err = text(&out.stderr);
    assert!(err.contains("riscv64-nowhere-gnu-gcc") && err.contains("CC"), "{}", err);
    if runs("clang") {
        // clang takes the triple.
        let out = gamma(&["run", "--target=s390x-linux-gnu", src], &[("CC", "clang")]);
        assert_eq!(text(&out.stdout), "s390x\nbig\n64 24 16\n", "{}", text(&out.stderr));
        let out = gamma(&["run", "--target=aarch64-linux-gnu", src], &[("CC", "clang")]);
        assert_eq!(text(&out.stdout), "aarch64\nlittle\n64 24 16\n", "{}", text(&out.stderr));
        // A compiler for another target than the triple is an error.
        let out = gamma(&["run", "--target=s390x-linux-gnu", src], &[("CC", "clang --target=aarch64-linux-gnu")]);
        assert!(!out.status.success());
        assert!(text(&out.stderr).contains("does not target"), "{}", text(&out.stderr));
        let out = gamma(&["run", "--target=aarch64_be-linux-gnu", src], &[("CC", "clang --target=aarch64-linux-gnu")]);
        assert!(text(&out.stderr).contains("big-endian") || text(&out.stderr).contains("little-endian"));
    }
    let out = gamma(&["run", "--target=", src], &[]);
    assert!(!out.status.success());
    let out = gamma(&["run", "--target=s390x-linux-gnu", "--mode=safe", src], &[("CC", "clang")]);
    assert!(text(&out.stderr).contains("--mode=safe"), "{}", text(&out.stderr));
}
