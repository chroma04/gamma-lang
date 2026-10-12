//! Tests of how native builds link the runtime: Debug builds link a runtime object
//! compiled once and kept in the cache directory (`GAMMA_CACHE_DIR`); Checked and Fast
//! builds, and Debug builds without a cache, compile it with the program. Each way must
//! give the same program. Without a usable C compiler the tests are skipped.

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
