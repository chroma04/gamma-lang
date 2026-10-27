//! WebAssembly for WASI (`--target=wasm32-wasi`): modules built with clang and a WASI
//! sysroot, transformed by Binaryen's `wasm-opt` when they switch fibers, and run by
//! Wasmtime. The tests are skipped, with a note, unless all three are available (clang
//! able to link a C program for `wasm32-wasi`, `wasm-opt`, and `wasmtime` on `PATH`).
//!
//! The conformance suite runs the same way with `CC="clang --target=wasm32-wasi"` (the
//! modules are started by Wasmtime, as `gamma run` does for any `.wasm` program).

mod common;

use std::path::{Path, PathBuf};
use std::process::{Command, Output};

/// Whether the tools are there; prints a note when not.
fn available() -> bool {
    static OK: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *OK.get_or_init(|| {
        let runs = |p: &str| Command::new(p).arg("--version").output().is_ok_and(|o| o.status.success());
        let dir = tmp("probe");
        let c = dir.join("probe.c");
        std::fs::write(&c, "#include <stdio.h>\nint main(void) { puts(\"ok\"); return 0; }\n").unwrap();
        let links = Command::new("clang")
            .args(["--target=wasm32-wasi", "-o"])
            .arg(dir.join("probe.wasm"))
            .arg(&c)
            .output()
            .is_ok_and(|o| o.status.success());
        let ok = links && runs("wasm-opt") && runs("wasmtime");
        if !ok {
            eprintln!("note: skipping the WASI tests: they need clang with a WASI sysroot, wasm-opt and wasmtime");
        }
        ok
    })
}

/// A fresh directory for test `name`.
fn tmp(name: &str) -> PathBuf {
    let dir = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("wasi").join(name);
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// Runs `gamma` in `dir` with `args`, without the toolchain variables of the environment.
fn gamma(dir: &Path, args: &[&str]) -> Output {
    common::gamma()
        .args(args)
        .current_dir(dir)
        .env_remove("CC")
        .env_remove("CFLAGS")
        .env_remove("LDFLAGS")
        .env_remove("GAMMA_RUNNER")
        .env_remove("WASI_SDK_PATH")
        .output()
        .unwrap()
}

fn text(b: &[u8]) -> String {
    String::from_utf8_lossy(b).into_owned()
}

/// Writes `src` as `main.gm` in a fresh directory and builds it for WASI with the extra
/// `args`; returns the directory and the module.
fn build(name: &str, src: &str, args: &[&str]) -> (PathBuf, PathBuf) {
    let dir = tmp(name);
    std::fs::write(dir.join("main.gm"), src).unwrap();
    let mut all = vec!["build", "--target=wasm32-wasi"];
    all.extend(args);
    all.push("main.gm");
    let out = gamma(&dir, &all);
    assert!(out.status.success(), "build failed:\n{}", text(&out.stderr));
    let module = dir.join("main.wasm");
    assert!(module.is_file(), "no main.wasm");
    (dir, module)
}

/// Whether the module was transformed by Asyncify (which exports its functions).
fn asyncified(module: &Path) -> bool {
    let bytes = std::fs::read(module).unwrap();
    bytes.windows(b"asyncify_start_unwind".len()).any(|w| w == b"asyncify_start_unwind")
}

const SERVICES: &str = r#"
fs :: import "std/fs"
thread :: import "std/thread"
process :: import "std/process"

work :: fn(n: ^u32) needs Sys {
    n^ += 1
}

main :: fn() -> u8 needs Out + Err + In + Env + Fs + Net + Proc + Clock + Rand + Alloc + Sys {
    when target.os is .Wasi { print("os: wasi\n") }
    when target.arch is .Wasm32 { print("arch: wasm32\n") }
    print("pointers: {} bits, usize: {} bytes\n", target.ptr_bits, @size_of(usize))
    args := Env.args()
    print("arguments:")
    for i in 1..args.len { print(" {}", args[i]) }
    print("\n")
    print("variable: {}\n", Env.var("GAMMA_WASI_TEST") else "unset")

    // Files: read one, write one, list the directory.
    text := fs.read_all("input.txt") else e => {
        print("cannot read input.txt: {}\n", e)
        return 1
    }
    print("input: {}", string(text))
    fs.write_all("output.txt", "written by wasm\n") else e => print("cannot write: {}\n", e)
    print("exists: {} {}\n", fs.exists("output.txt"), fs.exists("missing.txt"))

    // Clocks and random numbers.
    t0 := Clock.now()
    Clock.sleep(Duration(ns: 2_000_000))
    print("clock advanced: {}\n", Clock.now().ns > t0.ns)
    print("wall clock after 2020: {}\n", Clock.wall() > 1_577_836_800_000_000_000)
    r: [16]u8 = ---
    Rand.fill(r[..])

    // What WASI does not offer.
    if !e := Net.connect("127.0.0.1", 80) { print("connect: {}\n", e) }
    if !e := Net.listen("127.0.0.1", 0) { print("listen: {}\n", e) }
    argv := [_]string("echo", "hi")
    if !e := process.status(argv[..]) { print("process: {}\n", e) }
    n: u32 = 0
    if !e := thread.spawn(work, &n) { print("thread: {}\n", e) }
    Err.write("to standard error\n")
    return 7
}
"#;

#[test]
fn services() {
    if !available() {
        return;
    }
    let (dir, module) = build("services", SERVICES, &[]);
    std::fs::write(dir.join("input.txt"), "hello from the host\n").unwrap();
    // No fibers: the module is left as the C compiler made it.
    assert!(!asyncified(&module));
    let out = Command::new("wasmtime")
        .args(["run", "--dir=.", "--env", "GAMMA_WASI_TEST=set"])
        .arg(&module)
        .args(["one", "two words"])
        .current_dir(&dir)
        .output()
        .unwrap();
    assert_eq!(out.status.code(), Some(7), "{}{}", text(&out.stdout), text(&out.stderr));
    assert_eq!(
        text(&out.stdout),
        "os: wasi\narch: wasm32\npointers: 32 bits, usize: 4 bytes\narguments: one two words\nvariable: set\n\
         input: hello from the host\nexists: true false\nclock advanced: true\nwall clock after 2020: true\n\
         connect: .Unsupported\nlisten: .Unsupported\nprocess: .Unsupported\nthread: .Unsupported\n"
    );
    assert_eq!(text(&out.stderr), "to standard error\n");
    assert_eq!(std::fs::read_to_string(dir.join("output.txt")).unwrap(), "written by wasm\n");
}

const FIBERS: &str = r#"
fiber :: import "std/fiber"

Log :: struct {
    n: u64
    sum: u64
}

task :: fn(l: ^Log) needs fiber.Sched {
    for i in 0..100 {
        l.n += 1
        d := deep(20, i)
        l.sum += d
        fiber.Sched.yield_now()
    }
}

// Recursion, so that each switch saves and restores several frames.
deep :: fn(n: u64, x: u64) -> u64 needs fiber.Sched {
    if n is 0 {
        fiber.Sched.yield_now()
        return x
    }
    return deep(n - 1, x) + 1
}

below :: fn(n: u32) -> u64 needs Out + Alloc {
    if n > 0 { return below(n - 1) + 1 }
    return run()
}

run :: fn() -> u64 needs Out + Alloc {
    l := Log(n: 0, sum: 0)
    s := fiber.scheduler()
    defer s.deinit()
    main_stack: [32768]u8 = ---
    s1: [32768]u8 = ---
    s2: [32768]u8 = ---
    s3: [32768]u8 = ---
    with &s on main_stack[..] {
        fiber.spawn(task, &l, s1[..])
        fiber.spawn(task, &l, s2[..])
        fiber.spawn(task, &l, s3[..])
        task(&l)
        fiber.Sched.join()
    }
    print("{} steps, sum {}\n", l.n, l.sum)
    return l.sum
}

main :: fn() needs Out + Alloc {
    // The scheduler runs below a deep stack on the module's own stack.
    print("{}\n", below(2000))
}
"#;

#[test]
fn fibers() {
    if !available() {
        return;
    }
    for mode in ["--mode=debug", "--mode=fast"] {
        let (dir, module) = build("fibers", FIBERS, &[mode]);
        assert!(asyncified(&module), "{mode}: the module was not transformed");
        let out = Command::new("wasmtime").arg("run").arg(&module).current_dir(&dir).output().unwrap();
        assert!(out.status.success(), "{mode}: {}", text(&out.stderr));
        assert_eq!(text(&out.stdout), "400 steps, sum 27800\n29800\n", "{mode}");
    }
}

#[test]
fn run_and_test_use_wasmtime() {
    if !available() {
        return;
    }
    let dir = tmp("run-test");
    std::fs::write(
        dir.join("main.gm"),
        "main :: fn() needs Out + Env {\n    print(\"{}\\n\", Env.args().len)\n}\n\n\
         test \"arithmetic\" {\n    expect(1 + 1 is 2)\n}\n",
    )
    .unwrap();
    let out = gamma(&dir, &["run", "--backend=c", "--target=wasm32-wasi", "main.gm", "a", "b"]);
    assert!(out.status.success(), "{}", text(&out.stderr));
    assert_eq!(text(&out.stdout), "3\n");
    let out = gamma(&dir, &["test", "--backend=c", "--target=wasm32-wasi", "main.gm"]);
    assert!(out.status.success(), "{}{}", text(&out.stdout), text(&out.stderr));
    assert!(text(&out.stdout).contains("arithmetic"), "{}", text(&out.stdout));
}

#[test]
fn traps_and_stack_overflow() {
    if !available() {
        return;
    }
    let src = "down :: fn(n: u64) -> u64 {\n    big: [256]u8 = ---\n    big[n % 256] = 1\n    return down(n + 1) + u64(big[3])\n}\n\n\
               main :: fn() needs Out + Env {\n    if Env.args().len > 1 { print(\"{}\\n\", down(0)) }\n    \
               x: [3]u8 = ---\n    i := Env.args().len + 5\n    x[i] = 1\n}\n";
    let (dir, module) = build("traps", src, &[]);
    let out = Command::new("wasmtime").arg("run").arg(&module).current_dir(&dir).output().unwrap();
    assert_eq!(out.status.code(), Some(134), "{}", text(&out.stderr));
    assert!(text(&out.stderr).starts_with("trap: index 6 out of bounds for length 3"), "{}", text(&out.stderr));
    // Wasmtime's own stack for WebAssembly frames must be the larger, as `gamma run` makes it.
    let out = Command::new("wasmtime")
        .args(["run", "-W", "max-wasm-stack=268435456"])
        .arg(&module)
        .arg("deep")
        .current_dir(&dir)
        .output()
        .unwrap();
    assert_eq!(out.status.code(), Some(134), "{}", text(&out.stderr));
    assert!(text(&out.stderr).starts_with("trap: stack overflow"), "{}", text(&out.stderr));
}

#[test]
fn freestanding_wasm_is_still_freestanding() {
    // A WebAssembly compiler without WASI targets no operating system.
    if !available() {
        return;
    }
    let dir = tmp("freestanding");
    std::fs::write(dir.join("lib.gm"), "#export(\"add\")\nadd :: fn(a, b: i32) -> i32 => a + b\n").unwrap();
    let out = gamma(&dir, &["build", "--target=wasm32-unknown-unknown", "lib.gm"]);
    assert!(out.status.success(), "{}", text(&out.stderr));
    assert!(dir.join("lib.o").is_file());
}
