//! Tests of the experimental LLVM back end (`--backend=llvm`) through the `gamma` binary:
//! a program using every supported construct prints what the interpreter prints in every
//! build mode and at `-O0` and `-O2`; failed checks trap with the interpreter's report;
//! Fast builds drop the checks the spec lets them drop; unsupported constructs and
//! commands are reported. They need a C compiler, `clang` and a target the back end
//! supports (64-bit Linux or macOS on x86-64 or AArch64), and skip themselves otherwise.

mod common;

use std::path::{Path, PathBuf};
use std::process::Command;

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

/// Whether programs of this back end can be built here.
fn supported() -> bool {
    let target = gamma::toolchain::Toolchain::from_env().and_then(|t| t.target());
    let clang = std::env::var_os("GAMMA_LLVM_CLANG").unwrap_or_else(|| "clang".into());
    let ok = common::c_backend_available()
        && target.is_ok_and(|t| gamma::clif::check_target(&t).is_ok())
        && Command::new(clang).arg("--version").output().is_ok_and(|o| o.status.success());
    if !ok {
        eprintln!("skipped: no C compiler and clang for a target the LLVM back end supports");
    }
    ok
}

/// A fresh directory for test `name`.
fn dir(name: &str) -> PathBuf {
    let d = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("llvm").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

struct Out {
    code: Option<i32>,
    stdout: String,
    stderr: String,
}

fn gamma(cwd: &Path, args: &[&str], env: &[(&str, &str)]) -> Out {
    let mut cmd = Command::new(GAMMA);
    cmd.args(args).current_dir(cwd).env("GAMMA_CACHE_DIR", cwd.join("cache")).envs(env.iter().copied());
    let out = common::output(&mut cmd).unwrap();
    Out {
        code: out.status.code(),
        stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
        stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
    }
}

#[test]
fn subset_matches_the_interpreter() {
    if !supported() {
        return;
    }
    let d = dir("subset");
    let src = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/llvm/subset.gm");
    std::fs::copy(&src, d.join("subset.gm")).unwrap();
    let want = gamma(&d, &["run", "subset.gm"], &[]);
    assert_eq!(want.code, Some(0), "{}", want.stderr);
    for mode in ["--mode=debug", "--mode=checked", "--mode=fast"] {
        for level in ["0", "2"] {
            let got = gamma(&d, &["run", "--backend=llvm", mode, "subset.gm"], &[("GAMMA_LLVM_OPT", level)]);
            assert_eq!(got.code, Some(0), "{mode} -O{level}: {}", got.stderr);
            assert_eq!(got.stdout, want.stdout, "{mode} -O{level}");
        }
    }
}

/// The programs of the traps tests, each with the start of its trap report.
const TRAPS: &[(&str, &str, &str)] = &[
    (
        "index",
        "pick :: fn(a: ^[3]i32, i: usize) -> i32 => a[i]\n\
         main :: fn() needs Out {\n    a := [3]i32(fill: 1)\n    print(\"{}\\n\", pick(&a, @black_box(5)))\n}\n",
        "trap: index 5 out of bounds for length 3\n  at index.gm:1:44\n",
    ),
    (
        "add",
        "main :: fn() needs Out {\n    x: i32 = @black_box(2147483647)\n    print(\"{}\\n\", x + 1)\n}\n",
        "trap: integer overflow in `+`\n  at add.gm:3:19\n",
    ),
    (
        "div",
        "main :: fn() needs Out {\n    x: i32 = @black_box(0)\n    print(\"{}\\n\", 10 / x)\n}\n",
        "trap: division by zero\n  at div.gm:3:19\n",
    ),
    (
        "shift",
        "main :: fn() needs Out {\n    n: u32 = @black_box(40)\n    x: u32 = 1\n    print(\"{}\\n\", x << n)\n}\n",
        "trap: shift amount 40 too large for u32\n  at shift.gm:4:19\n",
    ),
    (
        "trap",
        "main :: fn() needs Out {\n    print(\"before\\n\")\n    trap(\"boom\")\n}\n",
        "trap: boom\n  at trap.gm:3:5\n",
    ),
];

#[test]
fn checks_trap_as_the_interpreter_reports() {
    if !supported() {
        return;
    }
    let d = dir("traps");
    for (name, src, report) in TRAPS {
        let file = format!("{name}.gm");
        std::fs::write(d.join(&file), src).unwrap();
        let want = gamma(&d, &["run", "--mode=checked", &file], &[]);
        let got = gamma(&d, &["run", "--mode=checked", "--backend=llvm", &file], &[]);
        assert_eq!(got.code, want.code, "{name}: {}", got.stderr);
        assert_eq!(got.stdout, want.stdout, "{name}");
        assert_eq!(got.stderr, want.stderr, "{name}");
        assert!(got.stderr.starts_with(report), "{name}: {}", got.stderr);
    }
}

#[test]
fn fast_builds_drop_checks_but_not_division_by_zero() {
    if !supported() {
        return;
    }
    let d = dir("fast");
    for (name, src, _) in TRAPS.iter().filter(|t| t.0 == "add" || t.0 == "div") {
        std::fs::write(d.join(format!("{name}.gm")), src).unwrap();
    }
    let add = gamma(&d, &["run", "--mode=fast", "--backend=llvm", "add.gm"], &[]);
    assert_eq!(add.code, Some(0), "{}", add.stderr);
    assert_eq!(add.stdout, "-2147483648\n");
    let div = gamma(&d, &["run", "--mode=fast", "--backend=llvm", "div.gm"], &[]);
    assert!(div.stderr.starts_with("trap: division by zero\n"), "{}", div.stderr);
}

#[test]
fn stack_overflow_traps() {
    if !supported() {
        return;
    }
    let d = dir("stack");
    let src = "down :: fn(n: i64) -> i64 {\n    if n is 0 {\n        return 0\n    }\n    \
               return down(n - 1) + 1\n}\n\nmain :: fn() needs Out {\n    print(\"{}\\n\", down(@black_box(1000000000)))\n}\n";
    std::fs::write(d.join("deep.gm"), src).unwrap();
    let out = gamma(&d, &["run", "--mode=checked", "--backend=llvm", "deep.gm"], &[("GAMMA_LLVM_OPT", "0")]);
    assert!(out.stderr.starts_with("trap: stack overflow\n"), "{}", out.stderr);
}

#[test]
fn unsupported_constructs_and_commands_are_reported() {
    let d = dir("unsupported");
    std::fs::write(
        d.join("point.gm"),
        "Point :: struct { x, y: i32 }\n\nmain :: fn() needs Out {\n    p := Point(x: 1, y: 2)\n    print(\"{}\\n\", p.x)\n}\n",
    )
    .unwrap();
    std::fs::write(d.join("t.gm"), "test \"one\" {\n    ok(1 + 1 is 2)\n}\n").unwrap();
    let out = gamma(&d, &["test", "--backend=llvm", "t.gm"], &[]);
    assert_eq!(out.code, Some(2));
    assert!(out.stderr.contains("builds and runs programs only"), "{}", out.stderr);
    if !supported() {
        return;
    }
    let out = gamma(&d, &["build", "--backend=llvm", "point.gm", "-o", "point.out"], &[]);
    assert_eq!(out.code, Some(1));
    assert!(out.stderr.contains("not supported by the LLVM back end"), "{}", out.stderr);
    std::fs::write(d.join("hi.gm"), "main :: fn() needs Out {\n    print(\"hi\\n\")\n}\n").unwrap();
    let out = gamma(&d, &["run", "--backend=llvm", "hi.gm"], &[]);
    assert_eq!((out.code, out.stdout.as_str()), (Some(0), "hi\n"), "{}", out.stderr);
    let out = gamma(&d, &["run", "--backend=llvm", "hi.gm"], &[("GAMMA_LLVM_OPT", "7")]);
    assert!(out.stderr.contains("GAMMA_LLVM_OPT must be 0, 1, 2 or 3"), "{}", out.stderr);
}
