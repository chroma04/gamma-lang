//! `gamma run` calls foreign functions from the interpreter. Each test sets `CC` to a
//! command that fails, so a program that still ran must have run in the interpreter and
//! not in a native build.

use std::path::Path;
use std::process::{Command, Output};

fn gamma(cc: &str, args: &[&str], file: &Path) -> Output {
    Command::new(env!("CARGO_BIN_EXE_gamma"))
        .env("CC", cc)
        .env("GAMMA_CACHE_DIR", Path::new(env!("CARGO_TARGET_TMPDIR")).join("gamma-cache-ffi"))
        .args(args)
        .arg(file)
        .output()
        .expect("cannot run gamma")
}

fn program(name: &str, src: &str) -> std::path::PathBuf {
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("ffi_interp");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(format!("{}.gm", name));
    std::fs::write(&path, src).unwrap();
    path
}

const LIBC_CALLS: &str = r#"
c :: import "std/c"
libc :: foreign "c" {
    strlen :: fn(s: cstring) -> usize needs not
    abs :: fn(x: c.c_int) -> c.c_int needs not
    puts :: fn(s: cstring) -> c.c_int
}
libm :: foreign "m" {
    sqrt :: fn(x: f64) -> f64 needs not
}
main :: fn() needs Out + Sys {
    print("{} {} {}\n", libc.strlen(c"four"), libc.abs(-3), libm.sqrt(16.0))
    print("before\n")
    _ = libc.puts(c"from C")
    print("after\n")
}
"#;

#[test]
fn interpreter_calls_foreign_functions() {
    let file = program("libc_calls", LIBC_CALLS);
    let out = gamma("/bin/false", &["run"], &file);
    assert!(out.status.success(), "stderr: {}", String::from_utf8_lossy(&out.stderr));
    assert_eq!(String::from_utf8_lossy(&out.stdout), "4 3 4\nbefore\nfrom C\nafter\n");
}

#[test]
fn the_failing_compiler_stops_a_native_run() {
    // Guards the tests here: with the C back end the same program needs the compiler.
    let file = program("libc_calls_native", LIBC_CALLS);
    let out = gamma("/bin/false", &["run", "--backend=c"], &file);
    assert!(!out.status.success());
}

// On Windows not every C function used here is exported under its POSIX name (UCRT's
// `snprintf` is an inline function of its headers), so the program runs natively there,
// which the failing compiler of these tests stops.
#[cfg(unix)]
#[test]
fn callbacks_and_pointers_run_in_the_interpreter() {
    let src = r#"
c :: import "std/c"
libc :: foreign "c" {
    qsort :: fn(base: rawptr, n, size: usize, cmp: ^fn(a, b: rawptr) -> c.c_int needs Sys) needs Sys
    snprintf :: fn(buf: [^]u8, n: usize, fmt: cstring, ...) -> c.c_int
}
by_value :: fn(a, b: rawptr) -> c.c_int needs Sys {
    x := a.cast(^const i32)^
    y := b.cast(^const i32)^
    return if x < y { -1 } else if x > y { 1 } else { 0 }
}
main :: fn() needs Out + Sys {
    xs := [_]i32(5, 3, 9, 1, 4)
    libc.qsort(&xs, xs.len, @size_of(i32), by_value)
    print("{}\n", xs)
    buf: [32]u8 = ---
    n := libc.snprintf(buf[..].ptr, buf.len, c"%d-%.1f-%s", c.c_int(7), 2.5, c"x")
    print("{}\n", buf[0..n.trunc(usize)])
}
"#;
    let file = program("callbacks", src);
    let out = gamma("/bin/false", &["run"], &file);
    assert!(out.status.success(), "stderr: {}", String::from_utf8_lossy(&out.stderr));
    assert_eq!(String::from_utf8_lossy(&out.stdout), "[1, 3, 4, 5, 9]\n7-2.5-x\n");
}

#[test]
fn a_trap_in_a_callback_is_reported() {
    let src = r#"
c :: import "std/c"
libc :: foreign "c" {
    qsort :: fn(base: rawptr, n, size: usize, cmp: ^fn(a, b: rawptr) -> c.c_int) needs not
}
boom :: fn(a, b: rawptr) -> c.c_int {
    xs := [_]i32(1)
    i := a.cast(^const i32)^.trunc(usize)
    return xs[i + 5]
}
main :: fn() needs Out {
    nums := [_]i32(3, 1, 2)
    libc.qsort(&nums, 3, 4, boom)
}
"#;
    let file = program("callback_trap", src);
    let out = gamma("/bin/false", &["run"], &file);
    assert_eq!(out.status.code(), Some(134));
    assert!(String::from_utf8_lossy(&out.stderr).contains("out of bounds"));
}

#[test]
fn a_missing_symbol_falls_back_to_the_native_build() {
    let src = r#"
lib :: foreign "c" {
    gamma_no_such_function_anywhere :: fn(x: i32) -> i32
}
main :: fn() needs Sys {
    _ = lib.gamma_no_such_function_anywhere(1)
}
"#;
    let file = program("missing_symbol", src);
    // The interpreter cannot find the symbol, so the native build (which needs the
    // compiler that is not there) is tried.
    let out = gamma("/bin/false", &["run"], &file);
    assert!(!out.status.success());
    assert!(!String::from_utf8_lossy(&out.stderr).contains("trap:"));
}

#[test]
fn foreign_calls_are_rejected_at_compile_time() {
    let src = r#"
c :: import "std/c"
libc :: foreign "c" {
    abs :: fn(x: c.c_int) -> c.c_int needs not
}
N :: libc.abs(-4)
main :: fn() needs Out {
    print("{}\n", N)
}
"#;
    let file = program("compile_time", src);
    let out = gamma("/bin/false", &["run"], &file);
    assert!(!out.status.success());
    assert!(String::from_utf8_lossy(&out.stderr).contains("cannot be called at compile time"));
}

// On Windows not every C function used here is exported under its POSIX name (UCRT's
// `snprintf` is an inline function of its headers), so the program runs natively there,
// which the failing compiler of these tests stops.
#[cfg(unix)]
#[test]
fn host_memory_persistent_pointers_and_errno() {
    // Memory C allocates is read and written directly, a pointer C keeps between calls
    // (`strtok`) stays valid, and `errno` is a foreign variable.
    let src = r#"
c :: import "std/c"
libc :: foreign "c" {
    strtok :: fn(s: ?[^]u8, delim: cstring) -> ?[^]u8 needs Sys
    malloc :: fn(n: usize) -> ?[^]u8 needs Sys
    free :: fn(p: rawptr) needs Sys
    strlen :: fn(s: cstring) -> usize needs not
    open :: fn(path: cstring, flags: c.c_int) -> c.c_int needs Sys
    #global errno: c.c_int
}
main :: fn() needs Out + Sys {
    buf := [_]u8('a', ',', 'b', ',', 'c', 0)
    t := libc.strtok(buf[..].ptr, c",")
    n := 0
    while t is not null {
        n += 1
        t = libc.strtok(null, c",")
    }
    print("tokens {}\n", n)
    p := libc.malloc(16) else trap("out of memory")
    p[0] = 'h'
    p[1] = 'i'
    p[2] = 0
    print("{} {}\n", libc.strlen(p), p[0..2])
    libc.free(p)
    _ = libc.open(c"/nonexistent/dir/file", 0)
    print("errno {}\n", libc.errno)
}
"#;
    let file = program("host_memory", src);
    let out = gamma("/bin/false", &["run"], &file);
    assert!(out.status.success(), "stderr: {}", String::from_utf8_lossy(&out.stderr));
    assert_eq!(String::from_utf8_lossy(&out.stdout), "tokens 3\n2 hi\nerrno 2\n");
}

// A slice cut from an unchecked pointer can claim any length; a system call that fills it
// traps (the interpreter checks what compiled code would overrun) and does not run out of memory.
#[test]
fn a_forged_slice_length_traps_in_system_calls() {
    let src = r#"
main :: fn() needs Out + Rand {
    buf: [8]u8 = ---
    q: [^]u8 = &buf[0]
    big: usize = 1 << 45
    Rand.fill(q[0..big])
    print("unreachable\n")
}
"#;
    let file = program("forged_slice", src);
    let out = gamma("/bin/false", &["run"], &file);
    assert_eq!(out.status.code(), Some(134), "stderr: {}", String::from_utf8_lossy(&out.stderr));
    assert!(String::from_utf8_lossy(&out.stderr).contains("invalid memory access"));
}

// The interpreter's addresses mean nothing to C: a pointer to freed memory is not passed on.
#[test]
fn a_freed_pointer_is_not_passed_to_c() {
    let src = r#"
libc :: foreign "c" {
    strlen :: fn(s: cstring) -> usize needs not
}
main :: fn() needs Out + Alloc {
    buf := make(u8, 8) else return
    buf[0] = 'a'
    buf[1] = 0
    p := buf.ptr
    free(buf)
    print("{}\n", libc.strlen(p.cast(cstring)))
}
"#;
    let file = program("freed_pointer", src);
    let out = gamma("/bin/false", &["run"], &file);
    assert_eq!(out.status.code(), Some(134), "stderr: {}", String::from_utf8_lossy(&out.stderr));
    assert!(String::from_utf8_lossy(&out.stderr).contains("freed memory"));
}

// Strings are bytes: an environment variable that is not UTF-8 is set, not "unset".
#[cfg(unix)]
#[test]
fn env_var_with_non_utf8_value() {
    use std::os::unix::ffi::OsStrExt;
    let src = r#"
main :: fn() needs Out + Env {
    for i in 0..3 {
        v := Env.var("GAMMA_TEST_BIN") else trap("unset")
        print("{} {}\n", v.len, v[0])
    }
    print("{}\n", Env.var("GAMMA_TEST_UNSET_VAR") is null)
    print("{}\n", Env.var("") is null)
}
"#;
    let file = program("env_non_utf8", src);
    let out = Command::new(env!("CARGO_BIN_EXE_gamma"))
        .env("CC", "/bin/false")
        .env("GAMMA_TEST_BIN", std::ffi::OsStr::from_bytes(b"\xff\xfe"))
        .env_remove("GAMMA_TEST_UNSET_VAR")
        .arg("run")
        .arg(&file)
        .output()
        .unwrap();
    assert!(out.status.success(), "stderr: {}", String::from_utf8_lossy(&out.stderr));
    assert_eq!(String::from_utf8_lossy(&out.stdout), "2 255\n2 255\n2 255\ntrue\ntrue\n");
}

// `errno` has an address per OS thread, so a spawned thread reads its own errors.
#[cfg(unix)]
#[test]
fn errno_is_per_thread() {
    let src = r#"
thread :: import "std/thread"
c :: import "std/c"
libc :: foreign "c" {
    open :: fn(path: cstring, flags: c.c_int) -> c.c_int needs Sys
    #global errno: c.c_int
}
work :: fn(x: ^i32) needs Sys {
    libc.errno = 0
    _ = libc.open(c"/nonexistent/zzz", 0)
    x^ = libc.errno
}
main :: fn() needs Out + Sys + Alloc {
    libc.errno = 0
    _ = libc.open(c"/nonexistent/aaa", 0)
    print("main {}\n", libc.errno)
    n: i32 = 0
    h := thread.spawn(work, &n) else e => return
    h.join()
    print("thread {}\n", n)
    print("main again {}\n", libc.errno)
}
"#;
    let file = program("errno_threads", src);
    let out = gamma("/bin/false", &["run"], &file);
    assert!(out.status.success(), "stderr: {}", String::from_utf8_lossy(&out.stderr));
    assert_eq!(String::from_utf8_lossy(&out.stdout), "main 2\nthread 2\nmain again 2\n");
}
