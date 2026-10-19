//! Freestanding programs (spec §3.7.1): a program for a target without an operating system builds
//! to an object file, which needs nothing from a C library beyond `memcpy` and its relatives.
//!
//! The end-to-end test builds `tests/freestanding/firmware.gm` with `clang` for the bare-metal
//! target of the host's architecture, checks the object's undefined symbols, links it with a C
//! program that stands in for the hardware (`tests/freestanding/harness.c`) and runs the result.
//! Tests that need `clang`, `nm` or a C compiler for the host are skipped, with a note, when they
//! are missing.

use std::path::{Path, PathBuf};
use std::process::{Command, Output};

const FIRMWARE: &str = "tests/freestanding/firmware.gm";
const HARNESS: &str = "tests/freestanding/harness.c";

/// The bare-metal target triple for the host's architecture, if the test supports it.
fn triple() -> Option<&'static str> {
    if !cfg!(target_os = "linux") {
        return None;
    }
    if cfg!(target_arch = "x86_64") {
        Some("x86_64-unknown-none-elf")
    } else if cfg!(target_arch = "aarch64") {
        Some("aarch64-unknown-none-elf")
    } else {
        None
    }
}

fn runs(program: &str) -> bool {
    Command::new(program).arg("--version").output().is_ok_and(|o| o.status.success())
}

/// The target triple, when the tools the tests need are there.
fn setup(test: &str) -> Option<(&'static str, PathBuf)> {
    let triple = triple().or_else(|| {
        eprintln!("note: skipping {test}: needs x86-64 or AArch64 Linux");
        None
    })?;
    if !runs("clang") || !runs("nm") || !runs("cc") {
        eprintln!("note: skipping {test}: needs clang, nm and cc");
        return None;
    }
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("freestanding").join(test);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    Some((triple, dir))
}

/// Runs `gamma` with `args`, from the root of the repository, with the C toolchain variables unset
/// (a bare-metal `--target` picks `clang`).
fn gamma(args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_gamma"))
        .args(args)
        .current_dir(env!("CARGO_MANIFEST_DIR"))
        .env_remove("CC")
        .env_remove("CFLAGS")
        .env_remove("LDFLAGS")
        .output()
        .unwrap()
}

fn text(b: &[u8]) -> String {
    String::from_utf8_lossy(b).into_owned()
}

/// The undefined symbols of the object file `obj`.
fn undefined_symbols(obj: &Path) -> Vec<String> {
    let out = Command::new("nm").arg("-u").arg(obj).output().unwrap();
    assert!(out.status.success(), "nm failed");
    text(&out.stdout).lines().filter_map(|l| l.split_whitespace().last().map(String::from)).collect()
}

/// Checks that `obj` is undefined only in the memory functions and the symbols `foreign`, which
/// the program declares foreign.
fn assert_needs_only(obj: &Path, foreign: &[&str]) {
    for sym in undefined_symbols(obj) {
        let ok = ["memcpy", "memmove", "memset", "memcmp"].contains(&sym.as_str()) || foreign.contains(&sym.as_str());
        assert!(ok, "the object needs `{sym}`, which a freestanding program may not");
    }
}

/// The defined global symbols of the object file `obj`.
fn global_symbols(obj: &Path) -> Vec<String> {
    let out = Command::new("nm").arg("-g").arg("--defined-only").arg(obj).output().unwrap();
    assert!(out.status.success(), "nm failed");
    text(&out.stdout).lines().filter_map(|l| l.split_whitespace().last().map(String::from)).collect()
}

/// Builds the firmware in build mode `mode` and checks that the object needs nothing from a C
/// library, then links it with the harness and returns the executable.
fn build_firmware(triple: &str, dir: &Path, mode: &str) -> PathBuf {
    let obj = dir.join(format!("firmware-{mode}.o"));
    let target = format!("--target={triple}");
    let mode_arg = format!("--mode={mode}");
    let out = gamma(&["build", &target, &mode_arg, FIRMWARE, "-o", obj.to_str().unwrap()]);
    assert!(out.status.success(), "build failed:\n{}", text(&out.stderr));
    // Only the entry points are defined, and only memory functions and what the program declares
    // foreign are undefined.
    let mut defined = global_symbols(&obj);
    defined.sort();
    assert_eq!(defined, ["gm_crash", "gm_entry"], "the object defines other symbols");
    assert_needs_only(&obj, &["uart_putc", "harness_trap"]);
    let exe = dir.join(format!("firmware-{mode}"));
    let harness = Path::new(env!("CARGO_MANIFEST_DIR")).join(HARNESS);
    // The object is not position independent, as for firmware: the host program is linked to match.
    let link = Command::new("cc").arg("-no-pie").arg("-o").arg(&exe).arg(harness).arg(&obj).output().unwrap();
    assert!(link.status.success(), "link failed:\n{}", text(&link.stderr));
    exe
}

#[test]
fn firmware_runs_against_a_harness() {
    let Some((triple, dir)) = setup("firmware_runs_against_a_harness") else { return };
    for mode in ["debug", "checked", "fast"] {
        let exe = build_firmware(triple, &dir, mode);
        let out = Command::new(&exe).output().unwrap();
        assert!(out.status.success(), "{mode}: exit {:?}", out.status);
        assert_eq!(
            text(&out.stdout),
            "hello from firmware with 3.14 and ff\n\
             list: [0, 3, 6, 9, 12]\n\
             squares sum: 30\n\
             used: true\n",
            "{mode}"
        );
    }
}

#[test]
fn trap_calls_the_hook() {
    let Some((triple, dir)) = setup("trap_calls_the_hook") else { return };
    for mode in ["debug", "checked"] {
        let exe = build_firmware(triple, &dir, mode);
        let out = Command::new(&exe).arg("crash").output().unwrap();
        // The hook ends the program with status 3 after the harness reports what it was given.
        assert_eq!(out.status.code(), Some(3), "{mode}: {}", text(&out.stdout));
        let stdout = text(&out.stdout);
        let file_len = FIRMWARE.len();
        assert_eq!(
            stdout,
            format!(
                "in range: 3\nTRAP: index 9 out of bounds for length 4 (line 46, col 12, file name of {file_len} bytes)\n"
            ),
            "{mode}"
        );
    }
}

#[test]
fn trap_without_a_hook_executes_the_trap_instruction() {
    let Some((triple, dir)) = setup("trap_without_a_hook") else { return };
    let src = dir.join("nohook.gm");
    std::fs::write(
        &src,
        "#export(\"crash\")\ncrash :: fn(i: usize) -> u8 {\n    xs := [4]u8(1, 2, 3, 4)\n    return xs[i]\n}\n",
    )
    .unwrap();
    let obj = dir.join("nohook.o");
    let target = format!("--target={triple}");
    let out = gamma(&["build", &target, src.to_str().unwrap(), "-o", obj.to_str().unwrap()]);
    assert!(out.status.success(), "build failed:\n{}", text(&out.stderr));
    assert_needs_only(&obj, &[]);
    let harness = dir.join("nohook.c");
    std::fs::write(&harness, "extern unsigned char crash(unsigned long); int main(void) { return crash(7); }\n")
        .unwrap();
    let exe = dir.join("nohook");
    let link = Command::new("cc").arg("-no-pie").arg("-o").arg(&exe).arg(&harness).arg(&obj).output().unwrap();
    assert!(link.status.success(), "link failed:\n{}", text(&link.stderr));
    let run = Command::new(&exe).output().unwrap();
    // `ud2` or `brk`: the process is killed by SIGILL or SIGTRAP.
    assert!(run.status.code().is_none(), "the program returned {:?}", run.status);
}

/// Checks `source` for the freestanding target and returns the diagnostics (empty if it passes).
fn check_freestanding(triple: &str, dir: &Path, name: &str, source: &str) -> (bool, String) {
    let src = dir.join(name);
    std::fs::write(&src, source).unwrap();
    let target = format!("--target={triple}");
    let out = gamma(&["check", &target, src.to_str().unwrap()]);
    (out.status.success(), text(&out.stderr))
}

#[test]
fn freestanding_rules_are_checked() {
    let Some((triple, dir)) = setup("freestanding_rules_are_checked") else { return };
    let hook = "#trap_hook\nh :: fn(msg: string, loc: SourceLocation) -> never needs Sys { while true {} }\n";
    let export = "#export(\"e\")\ne :: fn() needs Sys { }\n";

    // A program that is all right, with the hook.
    let (ok, err) = check_freestanding(triple, &dir, "ok.gm", &format!("{hook}{export}"));
    assert!(ok, "{err}");
    // A hook with an empty effect set is also fine.
    let (ok, err) = check_freestanding(
        triple,
        &dir,
        "pure_hook.gm",
        &format!("#trap_hook\nh :: fn(msg: string, loc: SourceLocation) -> never {{ while true {{}} }}\n{export}"),
    );
    assert!(ok, "{err}");

    // No `main`.
    let (ok, err) = check_freestanding(triple, &dir, "main.gm", "main :: fn() needs Out { print(\"x\") }\n");
    assert!(!ok);
    assert!(err.contains("does not declare `main`"), "{err}");

    // The default handlers do not exist.
    for name in ["heap_allocator", "system_io", "system_output"] {
        let (ok, err) = check_freestanding(
            triple,
            &dir,
            "default.gm",
            &format!("#export(\"e\")\ne :: fn() needs Sys {{ a := {name}() }}\n"),
        );
        assert!(!ok);
        assert!(err.contains(&format!("`{name}` does not exist on a freestanding target (§3.7.1)")), "{err}");
    }

    // A second hook.
    let (ok, err) = check_freestanding(
        triple,
        &dir,
        "two.gm",
        &format!(
            "{hook}#trap_hook\nh2 :: fn(msg: string, loc: SourceLocation) -> never needs Sys {{ while true {{}} }}\n{export}"
        ),
    );
    assert!(!ok);
    assert!(err.contains("at most one `#trap_hook`"), "{err}");

    // Wrong signatures.
    for sig in [
        "fn(msg: string) -> never",
        "fn(msg: string, loc: SourceLocation)",
        "fn(msg: string, loc: SourceLocation) -> never needs Out",
        "fn(msg: u32, loc: SourceLocation) -> never",
    ] {
        let (ok, err) = check_freestanding(
            triple,
            &dir,
            "sig.gm",
            &format!("#trap_hook\nh :: {sig} {{ while true {{}} }}\n{export}"),
        );
        assert!(!ok, "{sig}");
        assert!(err.contains("a `#trap_hook` function must have the type"), "{sig}: {err}");
    }

    // On something that is not a function.
    let (ok, err) = check_freestanding(triple, &dir, "notfn.gm", &format!("#trap_hook\nX :: 5\n{export}"));
    assert!(!ok);
    assert!(err.contains("`#trap_hook` applies only to a function"), "{err}");

    // A hosted program cannot have a hook (this one checks for the host).
    let src = dir.join("hosted.gm");
    std::fs::write(&src, format!("{hook}main :: fn() {{ }}\n")).unwrap();
    let out = gamma(&["check", src.to_str().unwrap()]);
    assert!(!out.status.success());
    assert!(text(&out.stderr).contains("`#trap_hook` applies only on a freestanding target"), "{}", text(&out.stderr));
}

#[test]
fn freestanding_programs_are_built_not_run() {
    let Some((triple, dir)) = setup("freestanding_programs_are_built_not_run") else { return };
    let target = format!("--target={triple}");
    for cmd in ["run", "test", "bench"] {
        let out = gamma(&[cmd, &target, FIRMWARE]);
        assert!(!out.status.success(), "{cmd}");
        let err = text(&out.stderr);
        assert!(err.contains("freestanding target") && err.contains("gamma build"), "{cmd}: {err}");
    }
    // Building a program with nothing exported has no entry points.
    let src = dir.join("empty.gm");
    std::fs::write(&src, "f :: fn() { }\n").unwrap();
    let out = gamma(&["build", &target, src.to_str().unwrap(), "-o", dir.join("empty.o").to_str().unwrap()]);
    assert!(!out.status.success());
    assert!(text(&out.stderr).contains("exports no function"), "{}", text(&out.stderr));
}

#[test]
fn fibers_work_on_freestanding_targets() {
    let Some((triple, dir)) = setup("fibers_work_on_freestanding_targets") else { return };
    let src = dir.join("fibers.gm");
    std::fs::write(&src, include_str!("freestanding/fibers.gm")).unwrap();
    let obj = dir.join("fibers.o");
    let target = format!("--target={triple}");
    let out = gamma(&["build", &target, src.to_str().unwrap(), "-o", obj.to_str().unwrap()]);
    assert!(out.status.success(), "build failed:\n{}", text(&out.stderr));
    assert_needs_only(&obj, &["put"]);
    let harness = dir.join("fibers_main.c");
    std::fs::write(
        &harness,
        "#include <stdio.h>\nvoid put(int x) { printf(\"%d\\n\", x); }\nextern int run_fibers(void);\n\
         int main(void) { printf(\"total %d\\n\", run_fibers()); return 0; }\n",
    )
    .unwrap();
    let exe = dir.join("fibers");
    let link = Command::new("cc").arg("-no-pie").arg("-o").arg(&exe).arg(&harness).arg(&obj).output().unwrap();
    assert!(link.status.success(), "link failed:\n{}", text(&link.stderr));
    let run = Command::new(&exe).output().unwrap();
    assert!(run.status.success(), "{:?}", run.status);
    assert_eq!(text(&run.stdout), "1\n2\n3\ntotal 6\n");

    // Without a context switch for the architecture, fibers are a compile error.
    if Command::new("clang")
        .args(["--target=riscv64-unknown-none-elf", "-E", "-x", "c", "-"])
        .stdin(std::process::Stdio::null())
        .output()
        .is_ok_and(|o| o.status.success())
    {
        let out = gamma(&[
            "build",
            "--target=riscv64-unknown-none-elf",
            src.to_str().unwrap(),
            "-o",
            dir.join("rv.o").to_str().unwrap(),
        ]);
        assert!(!out.status.success());
        assert!(text(&out.stderr).contains("fibers (`with … on`) need a context switch"), "{}", text(&out.stderr));
    }
}
