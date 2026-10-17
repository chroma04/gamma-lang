//! Tests of `gamma build -g`: the executable maps back to the Gamma source.
//!
//! The program is built with debug information and must behave exactly as without it.
//! The generated C (kept next to the executable) must carry `#line` directives naming
//! the source file, and frame fields must also have the names of the Gamma locals. When
//! `gdb` is installed, it must stop at a Gamma line and print a local by its name, and
//! show tuples, tagged unions, slices, optionals and results readably, with and without
//! the embedded pretty printers; when `lldb` is installed, it must show them too.
//! Without a usable C compiler (or debugger) the tests are skipped.

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

main :: fn() needs Out {
    p := Point(x: 3, y: 4)
    stdout := sum(10)
    s := Stat(st_mtime: 1, linux: 2)
    print(\"{} {} {}\\n\", p.x + p.y, stdout, s.st_mtime + s.linux)
}
";

/// Values of the kinds whose debugger views are tested. `main` stops at line 33.
const VIEWS: &str = "\
Shape :: union {
    Circle: f64,
    Rect: (w: i64, h: i64),
    Empty,
}

Dir :: union { N, E, S, W }

Node :: struct { value: i64, next: ?^Node, tags: []const u8 }

Error :: union { Missing, Bad: i64 }

parse :: fn(x: i64) -> Error!i64 {
    if x < 0 {
        return !.Bad(x)
    }
    return x * 2
}

main :: fn() needs Out {
    pair := (1, true)
    rect := Shape.Rect(w: 2, h: 3)
    circle := Shape.Circle(1.5)
    dir := Dir.S
    xs := [3]i64(10, 20, 30)
    slice := xs[0..3]
    word := \"hello\"
    some: ?i64 = 5
    none: ?i64 = null
    last := Node(value: 2, next: null, tags: \"b\")
    first := Node(value: 1, next: &last, tags: \"a\")
    ok, bad := (parse(4), parse(-1))
    print(\"{} {} {}\\n\", pair.0, slice.len, first.value)
}
";

fn c_compiler_available() -> bool {
    gamma::toolchain::Toolchain::from_env()
        .is_ok_and(|t| t.base_command().arg("--version").output().is_ok_and(|o| o.status.success()))
}

/// Writes the program to a fresh directory and builds it with `args`; returns the
/// executable.
fn build(name: &str, args: &[&str]) -> PathBuf {
    build_program(name, "points", PROGRAM, args)
}

/// Writes `program` to `<stem>.gm` in a fresh directory and builds it with `args`;
/// returns the executable.
fn build_program(name: &str, stem: &str, program: &str, args: &[&str]) -> PathBuf {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("debug-info").join(name);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let src = dir.join(format!("{stem}.gm"));
    std::fs::write(&src, program).unwrap();
    let exe = dir.join(stem);
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
        let src = gamma::source::canonicalize(&exe.with_file_name("points.gm")).unwrap();
        // `total += i` is line 9; source files are named with `/` on every system.
        let directive = format!("#line 9 \"{}\"", src.display().to_string().replace('\\', "/"));
        assert!(c.contains(&directive), "no `{}` in the generated C", directive);
        assert!(c.contains(" total; };"), "the frame field of `total` has no name");
        assert!(c.contains("#undef linux"), "a Gamma name that is a macro is not suspended");
    }
    // Without `-g` the C has no source mapping.
    let exe = build("plain", &[]);
    assert_eq!(run(&exe), "7 45 3\n");
    assert!(!std::fs::read_to_string(exe.with_extension("c")).unwrap().contains("#line"));
}

fn gdb_available() -> bool {
    let gdb_works = Command::new("gdb").arg("--version").output().is_ok_and(|o| o.status.success());
    if !c_compiler_available() || !gdb_works || !cfg!(target_os = "linux") {
        eprintln!("note: skipping: needs a C compiler and gdb on Linux");
        return false;
    }
    true
}

/// Runs gdb in batch mode on `exe` with the arguments `args` (each `-ex` command and the
/// like); `None` if gdb cannot trace processes here.
fn gdb(exe: &Path, args: &[&str]) -> Option<String> {
    let out = Command::new("gdb").args(["-batch", "-nx"]).args(args).arg(exe).output().unwrap();
    let text = String::from_utf8_lossy(&out.stdout).into_owned();
    if text.contains("ptrace") || text.contains("Operation not permitted") {
        eprintln!("note: skipping: gdb cannot trace processes here");
        return None;
    }
    Some(text)
}

/// gdb `-ex` arguments that print each expression of `exprs`.
fn prints(exprs: &[&str]) -> Vec<String> {
    exprs.iter().flat_map(|e| ["-ex".to_string(), format!("print {}", e)]).collect()
}

/// Asserts that `text` has the lines `$1 = <expected[0]>`, `$2 = …` in order.
fn assert_printed(text: &str, expected: &[&str]) {
    for (i, e) in expected.iter().enumerate() {
        let line = format!("${} = {}\n", i + 1, e);
        assert!(text.contains(&line), "the debugger did not print `{}`:\n{}", line.trim_end(), text);
    }
}

#[test]
fn gdb_stops_at_gamma_lines() {
    if !gdb_available() {
        return;
    }
    let exe = build("gdb", &["-g"]);
    let Some(text) = gdb(&exe, &["-ex", "break points.gm:17", "-ex", "run", "-ex", "print F.p", "-ex", "print F.p.y"])
    else {
        return;
    };
    assert!(text.contains("points.gm:17"), "gdb did not stop at points.gm:17:\n{}", text);
    assert_printed(&text, &["{x = 3, y = 4}", "4"]);
}

/// The expressions printed at the end of `main` in `VIEWS`.
const VIEWED: &[&str] = &[
    "F.pair",
    "F.rect",
    "F.circle",
    "F.dir",
    "F.slice",
    "F.word",
    "F.some",
    "F.none",
    "*F.first.next",
    "F.ok",
    "F.bad",
    "F.rect.Rect.h",
    "F.slice.ptr[2]",
];

#[test]
fn debug_views_are_declared() {
    if !c_compiler_available() {
        eprintln!("note: skipping: no C compiler");
        return;
    }
    let exe = build_program("views-c", "views", VIEWS, &["-g"]);
    assert_eq!(run(&exe), "1 3 1\n");
    let c = std::fs::read_to_string(exe.with_extension("c")).unwrap();
    for needle in ["Shape_Rect = 1", "Dir_S = 2", "_union {", "_slice {", "_string {", " pair;", " next;"] {
        assert!(c.contains(needle), "no `{}` in the generated C", needle);
    }
    assert_eq!(c.contains(".debug_gdb_scripts"), cfg!(target_os = "linux"));
    // Without `-g` there are no views.
    let exe = build_program("views-plain", "views", VIEWS, &[]);
    assert_eq!(run(&exe), "1 3 1\n");
    assert!(!std::fs::read_to_string(exe.with_extension("c")).unwrap().contains("gm_dv"));
}

#[test]
fn gdb_shows_gamma_values() {
    if !gdb_available() {
        return;
    }
    let exe = build_program("views-gdb", "views", VIEWS, &["-g"]);
    let dir = exe.parent().unwrap().display().to_string();
    let mut args = vec!["-iex".to_string(), format!("add-auto-load-safe-path {}", dir)];
    args.extend(["-ex", "break views.gm:33", "-ex", "run"].map(String::from));
    args.extend(prints(VIEWED));
    let Some(text) = gdb(&exe, &args.iter().map(String::as_str).collect::<Vec<_>>()) else { return };
    assert!(text.contains("views.gm:33"), "gdb did not stop at views.gm:33:\n{}", text);
    assert_printed(
        &text,
        &[
            "{_0 = 1, _1 = true}",
            "Shape_Rect = {w = 2, h = 3}",
            "Shape_Circle(1.5)",
            "Dir_S",
            "length 3 = {10, 20, 30}",
            "\"hello\"",
            "5",
            "null",
            "{value = 2, next = 0x0, tags = \"b\"}",
            "8",
            "!Error_Bad(-1)",
            "3",
            "30",
        ],
    );
}

#[test]
fn gdb_shows_views_without_the_pretty_printers() {
    if !gdb_available() {
        return;
    }
    let exe = build_program("views-gdb-raw", "views", VIEWS, &["-g"]);
    let mut args = vec!["-ex", "break views.gm:33", "-ex", "run"].into_iter().map(String::from).collect::<Vec<_>>();
    args.extend(prints(&["F.pair", "F.rect.tag", "F.rect.Rect", "F.dir", "F.slice.len", "*F.slice.ptr@3", "F.some"]));
    let Some(text) = gdb(&exe, &args.iter().map(String::as_str).collect::<Vec<_>>()) else { return };
    assert_printed(
        &text,
        &[
            "{_0 = 1, _1 = true}",
            "Shape_Rect",
            "{w = 2, h = 3}",
            "Dir_S",
            "3",
            "{10, 20, 30}",
            "{value = 5, present = true}",
        ],
    );
}

#[test]
fn lldb_shows_views() {
    let lldb_works = Command::new("lldb").arg("--version").output().is_ok_and(|o| o.status.success());
    if !c_compiler_available() || !lldb_works {
        eprintln!("note: skipping: needs a C compiler and lldb");
        return;
    }
    let exe = build_program("views-lldb", "views", VIEWS, &["-g"]);
    let mut cmd = Command::new("lldb");
    cmd.args(["--batch", "--no-lldbinit", "-o", "breakpoint set --file views.gm --line 33", "-o", "run"]);
    for e in ["F.dir", "F.rect.tag", "F.rect.Rect.h", "F.slice.ptr[1]", "F.first.next->value", "F.pair._1"] {
        cmd.args(["-o", &format!("expression -- {}", e)]);
    }
    let out = cmd.arg(&exe).output().unwrap();
    let text = String::from_utf8_lossy(&out.stdout);
    if !text.contains("stop reason = breakpoint") {
        eprintln!("note: skipping: lldb cannot run the program here:\n{}", text);
        return;
    }
    for e in ["Dir_S", "Shape_Rect", "= 3\n", "= 20\n", "= 2\n", "= true\n"] {
        assert!(text.contains(e), "lldb did not print `{}`:\n{}", e.trim_end(), text);
    }
}

#[test]
fn lldb_shows_gamma_values() {
    let lldb_works = Command::new("lldb").arg("--version").output().is_ok_and(|o| o.status.success());
    if !c_compiler_available() || !lldb_works {
        eprintln!("note: skipping: needs a C compiler and lldb");
        return;
    }
    let exe = build_program("views-lldb-formatters", "views", VIEWS, &["-g"]);
    let formatters = exe.with_file_name("gamma_lldb.py");
    assert!(formatters.is_file(), "no gamma_lldb.py next to the executable");
    // Without `-g` there are no views, so no formatters.
    let plain = build_program("views-lldb-plain", "views", VIEWS, &[]);
    assert!(!plain.with_file_name("gamma_lldb.py").exists());
    let mut cmd = Command::new("lldb");
    cmd.args(["--batch", "--no-lldbinit", "-o"]).arg(format!("command script import {}", formatters.display()));
    cmd.args(["-o", "breakpoint set --file views.gm --line 33", "-o", "run"]);
    let shown =
        ["F.rect", "F.circle", "F.dir", "F.slice", "F.word", "F.some", "F.none", "F.ok", "F.bad", "*F.first.next"];
    for e in shown {
        cmd.args(["-o", &format!("frame variable {}", e)]);
    }
    let out = cmd.arg(&exe).output().unwrap();
    let text = String::from_utf8_lossy(&out.stdout);
    if !text.contains("stop reason = breakpoint") {
        eprintln!("note: skipping: lldb cannot run the program here:\n{}", text);
        return;
    }
    for e in [
        "F.rect = Shape_Rect {",
        "w = 2",
        "h = 3",
        "F.circle = Shape_Circle(1.5)",
        "F.dir = Dir_S\n",
        "F.slice = length 3 {",
        "[0] = 10",
        "[2] = 30",
        "F.word = \"hello\"",
        "F.some = 5\n",
        "F.none = null\n",
        "F.ok = 8\n",
        "F.bad = !Error_Bad(-1)",
        "tags = \"b\"",
    ] {
        assert!(text.contains(e), "lldb did not print `{}`:\n{}", e.trim_end(), text);
    }
}
