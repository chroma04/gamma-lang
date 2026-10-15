//! Tests of how native builds link the runtime: Debug builds link a runtime object
//! compiled once and kept in the cache directory (`GAMMA_CACHE_DIR`); Checked and Fast
//! builds, and Debug builds without a cache, compile it with the program. Each way must
//! give the same program. Safe builds need Fil-C. Without a usable C compiler (or, for
//! the Safe build itself, without Fil-C) the tests are skipped.

use std::path::{Path, PathBuf};
use std::process::Command;

const PROGRAM: &str = "\
main :: fn() \\ {Out} {
    total := 0
    for i in 0..10 {
        total += i
    }
    print(\"{}\\n\", total)
}
";

fn c_compiler_available() -> bool {
    gamma::toolchain::Toolchain::from_env()
        .is_ok_and(|t| t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()))
}

/// A fresh directory for test `name`, holding the program.
fn setup(name: &str) -> (PathBuf, PathBuf) {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("native-build").join(name);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let src = dir.join("sum.gm");
    std::fs::write(&src, PROGRAM).unwrap();
    (dir, src)
}

/// Builds `src` with `args` and the cache directory `cache`, runs it and checks its
/// output; returns the generated C.
fn build_and_run(src: &Path, exe: &Path, cache: &Path, args: &[&str]) -> String {
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .env("GAMMA_CACHE_DIR", cache)
        .arg("build")
        .args(args)
        .arg("-o")
        .arg(exe)
        .arg(src)
        .output()
        .unwrap();
    assert!(out.status.success(), "gamma build failed:\n{}", String::from_utf8_lossy(&out.stderr));
    let run = Command::new(exe).output().unwrap();
    assert_eq!(String::from_utf8_lossy(&run.stdout), "45\n");
    std::fs::read_to_string(exe.with_extension("c")).unwrap()
}

/// The runtime objects in `cache`.
fn objects(cache: &Path) -> Vec<PathBuf> {
    let Ok(rd) = std::fs::read_dir(cache) else { return vec![] };
    let mut v: Vec<_> = rd.map(|e| e.unwrap().path()).filter(|p| p.extension().is_some_and(|e| e == "o")).collect();
    v.sort();
    v
}

#[test]
fn debug_builds_reuse_a_cached_runtime() {
    if !c_compiler_available() {
        return;
    }
    let (dir, src) = setup("cached");
    let cache = dir.join("cache");
    let c = build_and_run(&src, &dir.join("a"), &cache, &[]);
    assert!(c.starts_with("#define GM_RT_SEPARATE"), "a Debug build compiled the runtime with the program");
    let first = objects(&cache);
    assert_eq!(first.len(), 1, "expected one runtime object in {}", cache.display());
    // A second build reuses it; no temporary files are left behind.
    build_and_run(&src, &dir.join("b"), &cache, &[]);
    assert_eq!(objects(&cache), first);
    for e in std::fs::read_dir(&cache).unwrap() {
        let name = e.unwrap().file_name().to_string_lossy().into_owned();
        assert!(!name.contains(".tmp-"), "temporary file {} left in the cache", name);
    }
    // Debug information needs an object of its own.
    let c = build_and_run(&src, &dir.join("g"), &cache, &["-g"]);
    assert!(c.starts_with("#define GM_RT_SEPARATE"));
    assert_eq!(objects(&cache).len(), 2);
}

#[test]
fn optimized_builds_and_builds_without_a_cache_are_one_unit() {
    if !c_compiler_available() {
        return;
    }
    let (dir, src) = setup("single");
    let cache = dir.join("cache");
    for (name, args) in [("checked", &["--mode=checked"][..]), ("fast", &["--mode=fast"][..])] {
        let c = build_and_run(&src, &dir.join(name), &cache, args);
        assert!(!c.contains("GM_RT_SEPARATE 1"), "{} build linked a separate runtime", name);
    }
    assert!(objects(&cache).is_empty());
    let c = build_and_run(&src, &dir.join("nocache"), Path::new(""), &[]);
    assert!(!c.contains("GM_RT_SEPARATE 1"));
}

/// The target of the C compiler in `CC`, if it runs.
fn cc_target() -> Option<gamma::layout::Target> {
    gamma::toolchain::Toolchain::from_env().and_then(|t| t.target()).ok()
}

#[test]
fn safe_builds_need_fil_c() {
    // Pointing CC at a compiler that is not Fil-C must fail, naming the problem.
    if !c_compiler_available() || cc_target().is_none_or(|t| t.filc) {
        return;
    }
    let (dir, src) = setup("not-filc");
    let cc = std::env::var("CC").unwrap_or_else(|_| "cc".into());
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .env("CC", cc)
        .arg("build")
        .arg("--mode=safe")
        .arg("-o")
        .arg(dir.join("a"))
        .arg(&src)
        .output()
        .unwrap();
    assert!(!out.status.success());
    let err = String::from_utf8_lossy(&out.stderr);
    assert!(err.contains("is not Fil-C"), "unexpected error:\n{}", err);
}

#[test]
fn safe_builds_with_fil_c() {
    // Only where Fil-C is installed: a Safe build runs and reports `target.mode`.
    let Ok(target) = gamma::toolchain::Toolchain::for_safe_build().and_then(|t| t.target()) else { return };
    if !target.filc {
        return;
    }
    let (dir, src) = setup("filc");
    std::fs::write(
        &src,
        "main :: fn() \\ {Out} {\n    when target.mode is .Safe { print(\"safe\\n\") } else { print(\"other\\n\") }\n}\n",
    )
    .unwrap();
    let exe = dir.join("a");
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .env("GAMMA_CACHE_DIR", dir.join("cache"))
        .args(["build", "--mode=safe", "-o"])
        .arg(&exe)
        .arg(&src)
        .output()
        .unwrap();
    assert!(out.status.success(), "gamma build failed:\n{}", String::from_utf8_lossy(&out.stderr));
    let run = Command::new(&exe).output().unwrap();
    assert_eq!(String::from_utf8_lossy(&run.stdout), "safe\n");
}

/// A program killed by `SIGPIPE` (its output read only in part) ends with the status a
/// shell reports for that signal, with the interpreter and the C back end alike, and
/// without a trap message.
#[cfg(unix)]
#[test]
fn run_reports_a_closed_pipe_as_a_signal_status() {
    use std::io::{BufRead, BufReader, Read};
    use std::os::unix::process::ExitStatusExt;
    use std::process::Stdio;
    if !c_compiler_available() {
        return;
    }
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("native-build").join("closed_pipe");
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let src = dir.join("lines.gm");
    std::fs::write(&src, "main :: fn() \\ {Out} {\n    for i in 0..200000 { print(\"line {}\\n\", i) }\n}\n").unwrap();
    for backend in ["--backend=interp", "--backend=c"] {
        let mut child = Command::new(env!("CARGO_BIN_EXE_gamma"))
            .args(["run", backend])
            .arg(&src)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .unwrap();
        let mut out = BufReader::new(child.stdout.take().unwrap());
        let mut first = String::new();
        out.read_line(&mut first).unwrap();
        assert_eq!(first, "line 0\n");
        drop(out);
        let mut err = String::new();
        child.stderr.take().unwrap().read_to_string(&mut err).unwrap();
        let status = child.wait().unwrap();
        let shell_status = status.code().unwrap_or_else(|| 128 + status.signal().unwrap());
        assert_eq!(shell_status, 141, "{backend}");
        assert!(!err.contains("trap"), "{backend}: {err}");
    }
}

/// `gamma run --backend=c` keeps the executable in the cache directory and reuses it while
/// the program, its options and the compiler stay the same; a changed program is rebuilt.
#[test]
fn run_reuses_cached_executables() {
    if !c_compiler_available() {
        return;
    }
    let (dir, src) = setup("run_cache");
    let cache = dir.join("cache");
    let run = |args: &[&str]| {
        let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
            .env("GAMMA_CACHE_DIR", &cache)
            .args(["run", "--backend=c"])
            .args(args)
            .arg(&src)
            .output()
            .unwrap();
        assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
        String::from_utf8_lossy(&out.stdout).into_owned()
    };
    let programs = || -> Vec<PathBuf> {
        let mut v: Vec<PathBuf> = std::fs::read_dir(cache.join("run")).unwrap().map(|e| e.unwrap().path()).collect();
        v.sort();
        v
    };
    assert_eq!(run(&[]), "45\n");
    let first = programs();
    assert_eq!(first.len(), 1);
    assert_eq!(run(&[]), "45\n");
    assert_eq!(programs(), first);
    // Another build mode is another executable.
    assert_eq!(run(&["--mode=fast"]), "45\n");
    assert_eq!(programs().len(), 2);
    std::fs::write(&src, PROGRAM.replace("0..10", "0..11")).unwrap();
    assert_eq!(run(&[]), "55\n");
    assert_eq!(programs().len(), 3);
}

/// A function with thousands of prints and `@black_box`es must not make the C compiler
/// work through a copy of the effect dispatch for each: they call shared functions, so
/// the C a statement takes stays small.
#[test]
fn prints_and_black_boxes_do_not_grow_the_c_per_site() {
    if !c_compiler_available() {
        return;
    }
    const N: usize = 200;
    let (dir, _) = setup("many-prints");
    let src = dir.join("many.gm");
    let mut text = String::from("main :: fn() \\ {Out} {\n");
    for i in 0..N {
        text.push_str(&format!("    {{ x: i32 = @black_box({i}); print(\"v {{}} {{}}\\n\", x, x + 1) }}\n"));
    }
    text.push_str("}\n");
    std::fs::write(&src, text).unwrap();
    let exe = dir.join("a");
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .env("GAMMA_CACHE_DIR", dir.join("cache"))
        .args(["build", "-o"])
        .arg(&exe)
        .arg(&src)
        .output()
        .unwrap();
    assert!(out.status.success(), "gamma build failed:\n{}", String::from_utf8_lossy(&out.stderr));
    let run = Command::new(&exe).output().unwrap();
    let stdout = String::from_utf8_lossy(&run.stdout).into_owned();
    assert_eq!(stdout.lines().count(), N);
    assert!(stdout.starts_with("v 0 1\n") && stdout.ends_with("v 199 200\n"));
    let c = std::fs::read_to_string(exe.with_extension("c")).unwrap();
    let start = c.rfind("gm_f0(void *env").expect("the function of main");
    let body = &c[start..];
    let lines = body[..body[1..].find("\nstatic ").unwrap_or(body.len() - 1)].lines().count();
    assert!(lines > N * 10 && lines < N * 45, "main takes {} lines of C for {} statements", lines, N);
}
