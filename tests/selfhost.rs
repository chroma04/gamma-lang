//! Tests of how the compiler in `compiler/` builds itself: `gamma`, the compiler under test
//! (`GAMMA_TEST_COMPILER`), compiles its own sources to a fixed point, its unit tests pass,
//! its sources are formatted, the split C form of the bootstrap seed builds, and a packaged
//! release runs from its installed layout.

mod common;

use common::root;
use std::path::Path;
use std::process::Command;

/// A command running the compiler under test in the repository's root.
fn gamma() -> Command {
    let mut cmd = common::gamma();
    cmd.current_dir(root());
    cmd
}

#[test]
fn usage_errors() {
    for args in [&["check"][..], &["frobnicate", "x.gm"], &["build", "--mode=slow", "x.gm"]] {
        let got = common::output(gamma().args(args)).unwrap();
        assert_eq!(got.status.code(), Some(2), "gamma {:?}", args);
        assert!(String::from_utf8_lossy(&got.stderr).contains("\nusage: gamma"), "gamma {:?}", args);
    }
    let got = common::output(gamma().args(["lex", "no/such/file.gm"])).unwrap();
    assert_eq!(got.status.code(), Some(2));
    assert!(String::from_utf8_lossy(&got.stderr).contains("cannot read `no/such/file.gm`"));
}

#[test]
fn unit_tests_pass() {
    let out = common::output(gamma().args(["test", "compiler"])).unwrap();
    assert!(
        out.status.success(),
        "gamma test compiler failed:\n{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
}

#[test]
fn sources_are_formatted() {
    // Run `gamma fmt compiler` after editing it.
    let out = common::output(gamma().args(["fmt", "--check", "compiler"])).unwrap();
    assert!(
        out.status.success(),
        "not formatted (run `gamma fmt compiler`):\n{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
}

/// The first line where `actual` differs from `expected`, as (the file it belongs to, its
/// line number in the output, the expected line, the actual line).
fn first_difference(expected: &str, actual: &str) -> Option<(String, usize, String, String)> {
    let (mut e, mut a) = (expected.lines(), actual.lines());
    let mut file = String::new();
    for n in 1.. {
        match (e.next(), a.next()) {
            (None, None) => return None,
            (x, y) if x == y => {
                if let Some(name) = x.and_then(|l| l.strip_prefix("==> ")).and_then(|l| l.strip_suffix(" <==")) {
                    file = name.to_string();
                }
            }
            (x, y) => {
                let show = |l: Option<&str>| l.unwrap_or("(end of output)").to_string();
                return Some((file, n, show(x), show(y)));
            }
        }
    }
    None
}

/// `build --emit-c compiler` with the compiler `exe`: the C it generates for itself.
#[cfg(unix)]
fn self_c(exe: &Path, out: &Path) -> String {
    let mut cmd = common::compiler_command(exe);
    cmd.current_dir(root()).args(["build", "--emit-c", "-o"]).arg(out).arg("compiler");
    // One file, whatever the environment asks.
    cmd.env_remove("GAMMA_C_UNITS");
    let built = common::output(&mut cmd).unwrap();
    assert!(
        built.status.success(),
        "{} failed to compile itself:\n{}",
        exe.display(),
        String::from_utf8_lossy(&built.stderr)
    );
    std::fs::read_to_string(format!("{}.c", out.display())).unwrap()
}

#[test]
#[cfg(unix)]
fn the_compiler_compiles_itself_to_a_fixed_point() {
    let stage1 = common::compiler();
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-bootstrap");
    std::fs::create_dir_all(&dir).unwrap();
    // Stage 1 (the compiler under test) builds stage 2.
    let stage2 = dir.join("gamma2");
    let mut cmd = gamma();
    cmd.args(["build", "-o"]).arg(&stage2).arg("compiler");
    // One C unit, so that `gamma2.c` holds the whole program.
    cmd.env("GAMMA_C_UNITS", "1");
    let built = common::output(&mut cmd).unwrap();
    assert!(
        built.status.success(),
        "{} failed to compile itself:\n{}",
        stage1.display(),
        String::from_utf8_lossy(&built.stderr)
    );
    let mut c1 = std::fs::read_to_string(format!("{}.c", stage2.display())).unwrap();
    // Stage 2 generates the same C for itself.
    let mut c2 = self_c(&stage2, &dir.join("gamma3"));
    // A Debug build links the cached runtime, so its C file starts with the runtime's
    // header only; `--emit-c` writes the runtime too. Compare the program's own C.
    let rt_h = std::fs::read_to_string(root().join("runtime/gm_rt.h")).unwrap();
    let rt_c = std::fs::read_to_string(root().join("runtime/gm_rt.c")).unwrap();
    let separate = format!("#define GM_RT_SEPARATE 1\n{rt_h}\n");
    if let Some(rest) = c1.strip_prefix(&separate) {
        c1 = rest.to_string();
        c2 = c2.strip_prefix(&format!("{rt_h}\n{rt_c}\n")).expect("--emit-c starts with the runtime").to_string();
    }
    if let Some((_, line, a, b)) = first_difference(&c1, &c2) {
        panic!("the C that stage 2 generates differs from stage 1's at line {}:\nstage 1: {}\nstage 2: {}", line, a, b);
    }
}

/// With `GAMMA_C_UNITS`, `gamma build --emit-c` writes a program as a header, the runtime
/// and units that include the header (the form of the bootstrap seed): compiled one by one
/// with the flags of a build and linked, they make the program.
#[test]
#[cfg(unix)]
fn split_emitted_c_builds_the_program() {
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-split-emit");
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let mut cmd = gamma();
    cmd.args(["build", "--emit-c", "-o"]).arg(dir.join("life")).arg("examples/life.gm");
    cmd.env("GAMMA_C_UNITS", "3");
    let emitted = common::output(&mut cmd).unwrap();
    assert!(emitted.status.success(), "gamma build --emit-c failed:\n{}", String::from_utf8_lossy(&emitted.stderr));
    let sources = ["life.rt.c", "life.c", "life.1.c", "life.2.c"];
    for name in sources.iter().chain(&["life.h"]) {
        assert!(dir.join(name).exists(), "{name} was not written");
    }
    let toolchain = gamma::toolchain::Toolchain::from_env().unwrap();
    let mut objects = Vec::new();
    for name in sources {
        let object = dir.join(format!("{name}.o"));
        let mut cc = toolchain.base_command();
        cc.args(["-std=gnu11", "-w", "-fwrapv", "-fno-strict-aliasing", "-ffp-contract=off", "-pthread", "-O1", "-c"])
            .arg(dir.join(name))
            .arg("-o")
            .arg(&object);
        let out = common::output(&mut cc).unwrap();
        assert!(out.status.success(), "{name} does not compile:\n{}", String::from_utf8_lossy(&out.stderr));
        objects.push(object);
    }
    let program = dir.join("life");
    let mut link = toolchain.base_command();
    link.arg("-pthread").arg("-o").arg(&program).args(&objects).arg("-lm");
    let out = common::output(&mut link).unwrap();
    assert!(out.status.success(), "the units do not link:\n{}", String::from_utf8_lossy(&out.stderr));
    let run = common::output(&mut Command::new(&program)).unwrap();
    let expected = std::fs::read_to_string(root().join("tests/examples/life.stdout")).unwrap();
    assert_eq!(String::from_utf8_lossy(&run.stdout), expected);
}

/// A packaged `gamma` finds its libraries and runtime from where it is installed: with no
/// environment variables, started through `PATH` from another directory, it runs a program
/// on the interpreter, through C and as a built executable (`tools/smoke-release.sh`).
#[test]
#[cfg(unix)]
fn packaged_compiler_runs_from_its_installed_layout() {
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("selfhost-package");
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let mut package = Command::new("sh");
    package.current_dir(root()).arg("tools/package-release.sh").arg("test-asset").arg(common::compiler()).arg(&dir);
    let out = common::output(&mut package).unwrap();
    assert!(out.status.success(), "packaging failed:\n{}", String::from_utf8_lossy(&out.stderr));
    let mut unpack = Command::new("tar");
    unpack.arg("-C").arg(&dir).arg("-xzf").arg(dir.join("gamma-test-asset.tar.gz"));
    let out = common::output(&mut unpack).unwrap();
    assert!(out.status.success(), "unpacking failed:\n{}", String::from_utf8_lossy(&out.stderr));
    let installed = std::fs::read_dir(&dir)
        .unwrap()
        .map(|e| e.unwrap().path())
        .find(|p| p.is_dir() && p.file_name().unwrap().to_string_lossy().starts_with("gamma-"))
        .expect("the archive holds no gamma directory");
    for part in ["std", "core", "runtime"] {
        assert!(installed.join("lib/gamma").join(part).is_dir(), "the archive has no lib/gamma/{part}");
    }
    let mut smoke = Command::new("sh");
    smoke.current_dir(root()).arg("tools/smoke-release.sh").arg(installed.join("bin/gamma"));
    let out = common::output(&mut smoke).unwrap();
    assert!(
        out.status.success(),
        "the installed gamma failed:\n{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
}
