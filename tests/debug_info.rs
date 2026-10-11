//! Tests of `gamma build -g`: the executable maps back to the Gamma source.
//!
//! The program is built with debug information and must behave exactly as without it.
//! The generated C (kept next to the executable) must carry `#line` directives naming
//! the source file, and frame fields must also have the names of the Gamma locals. When
//! `gdb` is installed, it must stop at a Gamma line and print a local by its name.
//! Without a usable C compiler the tests are skipped.

use std::path::{Path, PathBuf};
use std::process::Command;

const PROGRAM: &str = "\
Point :: struct { x, y: i64 }

// Names that are also macros of system headers.
Stat :: struct { st_mtime, linux: i64 }

sum :: fn(n: i64) -> i64 {
    total := 0
    for i in 0..n {
        total += i
    }
    return total
}

main :: fn() \\ {Out} {
    p := Point(x: 3, y: 4)
    stdout := sum(10)
    s := Stat(st_mtime: 1, linux: 2)
    print(\"{} {} {}\\n\", (p.x + p.y, stdout, s.st_mtime + s.linux))
}
";

fn c_compiler_available() -> bool {
    let cc = std::env::var("CC").unwrap_or_else(|_| "cc".into());
    Command::new(cc).arg("--version").output().is_ok_and(|o| o.status.success())
}

/// Writes the program to a fresh directory and builds it with `args`; returns the
/// executable.
fn build(name: &str, args: &[&str]) -> PathBuf {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("debug-info").join(name);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let src = dir.join("points.gm");
    std::fs::write(&src, PROGRAM).unwrap();
    let exe = dir.join("points");
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .env("GAMMA_CACHE_DIR", PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("gamma-cache"))
        .arg("build")
        .args(args)
        .arg("-o")
        .arg(&exe)
        .arg(&src)
        .output()
        .unwrap();
    assert!(out.status.success(), "gamma build failed:\n{}", String::from_utf8_lossy(&out.stderr));
    exe
}

fn run(exe: &Path) -> String {
    let out = Command::new(exe).output().unwrap();
    assert!(out.status.success());
    String::from_utf8_lossy(&out.stdout).into_owned()
}

#[test]
fn debug_information_maps_to_the_source() {
    if !c_compiler_available() {
        eprintln!("note: skipping: no C compiler");
        return;
    }
    for mode in ["--mode=debug", "--mode=fast"] {
        let exe = build(&mode[7..], &["-g", mode]);
        assert_eq!(run(&exe), "7 45 3\n");
        let c = std::fs::read_to_string(exe.with_extension("c")).unwrap();
        let src = std::fs::canonicalize(exe.with_file_name("points.gm")).unwrap();
        // `total += i` is line 9.
        let directive = format!("#line 9 \"{}\"", src.display());
        assert!(c.contains(&directive), "no `{}` in the generated C", directive);
        assert!(c.contains(" total; };"), "the frame field of `total` has no name");
        assert!(c.contains("#undef linux"), "a Gamma name that is a macro is not suspended");
    }
    // Without `-g` the C has no source mapping.
    let exe = build("plain", &[]);
    assert_eq!(run(&exe), "7 45 3\n");
    assert!(!std::fs::read_to_string(exe.with_extension("c")).unwrap().contains("#line"));
}

#[test]
fn gdb_stops_at_gamma_lines() {
    let gdb_works = Command::new("gdb").arg("--version").output().is_ok_and(|o| o.status.success());
    if !c_compiler_available() || !gdb_works || !cfg!(target_os = "linux") {
        eprintln!("note: skipping: needs a C compiler and gdb on Linux");
        return;
    }
    let exe = build("gdb", &["-g"]);
    let out = Command::new("gdb")
        .args(["-batch", "-nx", "-ex", "break points.gm:17", "-ex", "run", "-ex", "print F.p", "-ex", "print F.p.y"])
        .arg(&exe)
        .output()
        .unwrap();
    let text = String::from_utf8_lossy(&out.stdout);
    if text.contains("ptrace") || text.contains("Operation not permitted") {
        eprintln!("note: skipping: gdb cannot trace processes here");
        return;
    }
    assert!(text.contains("points.gm:17"), "gdb did not stop at points.gm:17:\n{}", text);
    assert!(text.contains("{x = 3, y = 4}"), "gdb did not print `p`:\n{}", text);
    assert!(text.contains("= 4\n"), "gdb did not print `p.y`:\n{}", text);
}
