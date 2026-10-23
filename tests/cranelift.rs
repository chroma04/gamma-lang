//! Tests of the Cranelift back end (`--backend=cranelift`) through the `gamma` binary:
//! the command-line options, and values crossing the C calling convention checked
//! against code compiled by the C compiler (structs of every register class passed and
//! returned by value in both directions, variadic calls with floats, inline assembly).
//! They need a C compiler and a target the back end supports (64-bit Linux or macOS on
//! x86-64 or AArch64), and skip themselves otherwise. The whole conformance suite also
//! runs through this back end (see `tests/conformance.rs`).

use std::path::PathBuf;
use std::process::Command;

const GAMMA: &str = env!("CARGO_BIN_EXE_gamma");

/// Whether the C toolchain and its target can run programs of this back end.
fn supported() -> bool {
    let ok = gamma::toolchain::Toolchain::from_env()
        .and_then(|t| t.target())
        .is_ok_and(|t| gamma::clif::check_target(&t).is_ok());
    if !ok {
        eprintln!("skipped: no C compiler for a target the Cranelift back end supports");
    }
    ok
}

/// A fresh directory for test `name`.
fn dir(name: &str) -> PathBuf {
    let d = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("cranelift").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

struct Out {
    ok: bool,
    stdout: String,
    stderr: String,
}

fn gamma(cwd: &PathBuf, args: &[&str]) -> Out {
    let out =
        Command::new(GAMMA).args(args).current_dir(cwd).env("GAMMA_CACHE_DIR", cwd.join("cache")).output().unwrap();
    Out {
        ok: out.status.success(),
        stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
        stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
    }
}

#[test]
fn backend_option() {
    let d = dir("option");
    std::fs::write(d.join("hi.gm"), "main :: fn() needs Out { print(\"hi\\n\") }\n").unwrap();
    let out = gamma(&d, &["run", "--backend=llvm", "hi.gm"]);
    assert!(!out.ok);
    assert!(out.stderr.contains("c, cranelift or interp"), "{}", out.stderr);
    if !supported() {
        return;
    }
    let out = gamma(&d, &["run", "--backend=cranelift", "hi.gm"]);
    assert!(out.ok, "{}", out.stderr);
    assert_eq!(out.stdout, "hi\n");
    // `build` writes an executable that runs on its own.
    let out = gamma(&d, &["build", "--backend=cranelift", "hi.gm", "-o", "hi.out"]);
    assert!(out.ok, "{}", out.stderr);
    // `GAMMA_RUNNER` runs programs built for another architecture (under an emulator).
    let runner = std::env::var("GAMMA_RUNNER").unwrap_or_default();
    let mut words = runner.split_whitespace();
    let run = match words.next() {
        Some(r) => Command::new(r).args(words).arg(d.join("hi.out")).output().unwrap(),
        None => Command::new(d.join("hi.out")).output().unwrap(),
    };
    assert_eq!(String::from_utf8_lossy(&run.stdout), "hi\n");
    // Debug information needs the C back end.
    let out = gamma(&d, &["run", "--backend=cranelift", "-g", "hi.gm"]);
    assert!(!out.ok);
    assert!(
        out.stderr.contains("not supported by the Cranelift back end yet") && out.stderr.contains("--backend=c"),
        "{}",
        out.stderr
    );
}

#[test]
fn tests_and_traps() {
    if !supported() {
        return;
    }
    let d = dir("tests");
    std::fs::write(
        d.join("t.gm"),
        "\
add :: fn(a, b: i32) -> i32 => a + b

main :: fn() needs Out {
    xs := [_]i32(1, 2, 3)
    i: usize = 3
    print(\"{}\\n\", add(xs[0], xs[i]))
}

test \"adds\" {
    expect(add(2, 3) is 5)
}

test \"fails\" {
    expect(add(2, 2) is 5)
}
",
    )
    .unwrap();
    let out = gamma(&d, &["test", "--backend=cranelift", "t.gm"]);
    assert!(!out.ok);
    assert!(out.stdout.contains("1 passed") && out.stdout.contains("1 failed"), "{}\n{}", out.stdout, out.stderr);
    // A trap reports the same message and location as the other back ends.
    let c = gamma(&d, &["run", "--backend=c", "t.gm"]);
    let cl = gamma(&d, &["run", "--backend=cranelift", "t.gm"]);
    assert!(!c.ok && !cl.ok);
    assert!(cl.stderr.contains("index 3 out of bounds for length 3"), "{}", cl.stderr);
    assert!(cl.stderr.contains("t.gm:6:30 in main"), "{}", cl.stderr);
    assert_eq!(c.stderr.lines().take(2).collect::<Vec<_>>(), cl.stderr.lines().take(2).collect::<Vec<_>>());
}

const ABI_C: &str = r#"
#include <stdint.h>
#include <string.h>

typedef struct { int64_t x, y, z; } S3;
typedef struct { double x, y; } V2;
typedef struct { int64_t n; double f; } IF;
typedef struct { float a, b, c; } F3;
typedef struct { float a; int32_t b; } FI;
typedef struct { uint8_t a, b, c; } B3;
typedef struct { double a, b, c, d; } D4;
typedef struct { int32_t a; int64_t b; int32_t c; } Big;

int64_t c_s3(S3 s) { return s.x + 10 * s.y + 100 * s.z; }
double c_v2(V2 v) { return v.x * 10 + v.y; }
double c_if(IF p, int k) { return p.n * 100 + p.f * 10 + k; }
float c_f3(F3 v) { return v.a * 100 + v.b * 10 + v.c; }
int64_t c_fi(FI v) { return (int64_t)(v.a * 10) + v.b; }
int64_t c_b3(B3 v) { return v.a + v.b * 10 + v.c * 100; }
double c_d4(D4 v) { return v.a * 1000 + v.b * 100 + v.c * 10 + v.d; }
int64_t c_big(Big v) { return v.a + v.b * 10 + v.c * 100; }

/* Six integers fill the integer registers of x86-64: the struct after them goes to the
   stack, while the doubles still use vector registers. */
double c_many(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, IF p, V2 q, int64_t g) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + p.n * 100 + p.f * 1000 + q.x * 10000 + q.y * 100000 + g * 1000000;
}

S3 c_ret_s3(int64_t k) { S3 s = {k, k * 2, k * 3}; return s; }
V2 c_ret_v2(double a, double b) { V2 v = {a + 1, b + 2}; return v; }
IF c_ret_if(int64_t n, double f) { IF v = {n * 2, f * 2}; return v; }
F3 c_ret_f3(float k) { F3 v = {k, k * 2, k * 3}; return v; }
FI c_ret_fi(float a, int32_t b) { FI v = {a, b}; return v; }
B3 c_ret_b3(uint8_t k) { B3 v = {k, (uint8_t)(k + 1), (uint8_t)(k + 2)}; return v; }
D4 c_ret_d4(double k) { D4 v = {k, k + 1, k + 2, k + 3}; return v; }
Big c_ret_big(int32_t k) { Big v = {k, (int64_t)k * 1000000000, -k}; return v; }

/* C calling exported Gamma functions with the same structs. */
extern int64_t ex_s3(S3 s);
extern V2 ex_v2(V2 v, double k);
extern F3 ex_f3(F3 v, float k);
extern D4 ex_d4(D4 v);
extern B3 ex_b3(B3 v);
extern FI ex_fi(FI v);
extern IF ex_if(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, IF p);

int64_t c_callbacks(void) {
    S3 s = {1, 2, 3};
    V2 v = {1.5, 2.5};
    F3 f = {1, 2, 3};
    D4 d = {1, 2, 3, 4};
    B3 b = {1, 2, 3};
    FI fi = {2.5f, 7};
    IF p = {4, 0.5};
    int64_t r = ex_s3(s);
    V2 v2 = ex_v2(v, 2);
    F3 f3 = ex_f3(f, 0.5f);
    D4 d4 = ex_d4(d);
    B3 b3 = ex_b3(b);
    FI fi2 = ex_fi(fi);
    IF p2 = ex_if(1, 2, 3, 4, 5, p);
    return r + (int64_t)(v2.x * 10 + v2.y) * 1000 + (int64_t)(f3.a + f3.b + f3.c) * 100000
        + (int64_t)(d4.a + d4.b + d4.c + d4.d) * 10000000 + (b3.a + b3.b + b3.c) * 1000000000LL
        + (int64_t)(fi2.a * 2) * 100000000000LL + fi2.b * 10000000000000LL + (p2.n + (int64_t)(p2.f * 4)) * 1000000000000000LL;
}
"#;

const ABI_GM: &str = r#"
c :: import "std/c"

S3 :: struct { x: i64, y: i64, z: i64 }
V2 :: struct { x: f64, y: f64 }
IF :: struct { n: i64, f: f64 }
F3 :: struct { a: f32, b: f32, c: f32 }
FI :: struct { a: f32, b: i32 }
B3 :: struct { a: u8, b: u8, c: u8 }
D4 :: struct { a: f64, b: f64, c: f64, d: f64 }
Big :: struct { a: i32, b: i64, c: i32 }

lib :: foreign "c" {
    c_s3 :: fn(s: S3) -> i64 needs not
    c_v2 :: fn(v: V2) -> f64 needs not
    c_if :: fn(p: IF, k: c.c_int) -> f64 needs not
    c_f3 :: fn(v: F3) -> f32 needs not
    c_fi :: fn(v: FI) -> i64 needs not
    c_b3 :: fn(v: B3) -> i64 needs not
    c_d4 :: fn(v: D4) -> f64 needs not
    c_big :: fn(v: Big) -> i64 needs not
    c_many :: fn(a, b, c, d, e, f: i64, p: IF, q: V2, g: i64) -> f64 needs not
    c_ret_s3 :: fn(k: i64) -> S3 needs not
    c_ret_v2 :: fn(a, b: f64) -> V2 needs not
    c_ret_if :: fn(n: i64, f: f64) -> IF needs not
    c_ret_f3 :: fn(k: f32) -> F3 needs not
    c_ret_fi :: fn(a: f32, b: i32) -> FI needs not
    c_ret_b3 :: fn(k: u8) -> B3 needs not
    c_ret_d4 :: fn(k: f64) -> D4 needs not
    c_ret_big :: fn(k: i32) -> Big needs not
    c_callbacks :: fn() -> i64
    snprintf :: fn(buf: [^]u8, size: usize, fmt: cstring, ...) -> c.c_int
}

#export("ex_s3")
ex_s3 :: fn(s: S3) -> i64 => s.x * 3 + s.y * 2 + s.z

#export("ex_v2")
ex_v2 :: fn(v: V2, k: f64) -> V2 => V2(x: v.x * k, y: v.y * k)

#export("ex_f3")
ex_f3 :: fn(v: F3, k: f32) -> F3 => F3(a: v.a + k, b: v.b + k, c: v.c + k)

#export("ex_d4")
ex_d4 :: fn(v: D4) -> D4 => D4(a: v.d, b: v.c, c: v.b, d: v.a + 10.0)

#export("ex_b3")
ex_b3 :: fn(v: B3) -> B3 => B3(a: v.c, b: v.b, c: v.a + 1)

#export("ex_fi")
ex_fi :: fn(v: FI) -> FI => FI(a: v.a * 2.0, b: v.b + 1)

#export("ex_if")
ex_if :: fn(a, b, c, d, e: i64, p: IF) -> IF => IF(n: p.n + a + b + c + d + e, f: p.f * 2.0)

main :: fn() needs Out + Sys {
    print("{}\n", lib.c_s3(S3(x: 1, y: 2, z: 3)))
    print("{}\n", lib.c_v2(V2(x: 1.5, y: 2.5)))
    print("{}\n", lib.c_if(IF(n: 3, f: 0.5), 7))
    print("{}\n", lib.c_f3(F3(a: 1.0, b: 2.0, c: 3.0)))
    print("{}\n", lib.c_fi(FI(a: 2.5, b: 9)))
    print("{}\n", lib.c_b3(B3(a: 1, b: 2, c: 3)))
    print("{}\n", lib.c_d4(D4(a: 1.0, b: 2.0, c: 3.0, d: 4.0)))
    print("{}\n", lib.c_big(Big(a: 1, b: 2, c: 3)))
    print("{}\n", lib.c_many(1, 1, 1, 1, 1, 1, IF(n: 1, f: 2.0), V2(x: 3.0, y: 4.0), 5))
    s := lib.c_ret_s3(5)
    print("{} {} {}\n", s.x, s.y, s.z)
    v := lib.c_ret_v2(1.0, 2.0)
    print("{} {}\n", v.x, v.y)
    p := lib.c_ret_if(21, 1.25)
    print("{} {}\n", p.n, p.f)
    f := lib.c_ret_f3(1.5)
    print("{} {} {}\n", f.a, f.b, f.c)
    fi := lib.c_ret_fi(0.25, -3)
    print("{} {}\n", fi.a, fi.b)
    b := lib.c_ret_b3(7)
    print("{} {} {}\n", b.a, b.b, b.c)
    d := lib.c_ret_d4(0.5)
    print("{} {} {} {}\n", d.a, d.b, d.c, d.d)
    g := lib.c_ret_big(4)
    print("{} {} {}\n", g.a, g.b, g.c)
    print("{}\n", lib.c_callbacks())
    buf: [64]u8 = ---
    n := lib.snprintf(&buf, buf.len, c"%d %.2f %s %.1f %ld", c.c_int(7), 2.5, c"x", f64(-1.25), c.c_long(9))
    written := n.checked(usize) else trap("snprintf failed")
    print("{}\n", buf[0..written])
}
"#;

const ABI_EXPECTED: &str = "\
321
17.5
312
123
34
321
1234
321
5432121
5 10 15
2 4
42 2.5
1.5 3 4.5
0.25 -3
7 8 9
0.5 1.5 2.5 3.5
4 4000000000 -4
23081007200735010
";

#[test]
fn c_abi_against_c() {
    if !supported() {
        return;
    }
    let d = dir("abi");
    std::fs::write(d.join("gamma.toml"), "[package]\nname = \"abi\"\n\n[build]\nc-sources = [\"abi.c\"]\n").unwrap();
    std::fs::write(d.join("abi.c"), ABI_C).unwrap();
    std::fs::write(d.join("main.gm"), ABI_GM).unwrap();
    let c = gamma(&d, &["run", "--backend=c", "main.gm"]);
    assert!(c.ok, "{}", c.stderr);
    assert!(c.stdout.starts_with(ABI_EXPECTED), "{}", c.stdout);
    let cl = gamma(&d, &["run", "--backend=cranelift", "main.gm"]);
    assert!(cl.ok, "{}", cl.stderr);
    assert_eq!(cl.stdout, c.stdout);
    assert!(cl.stdout.ends_with("7 2.50 x -1.2 9\n") || cl.stdout.ends_with("7 2.50 x -1.3 9\n"), "{}", cl.stdout);
}

#[test]
fn inline_assembly() {
    if !supported() {
        return;
    }
    let d = dir("asm");
    std::fs::write(
        d.join("a.gm"),
        "\
when target.arch is .X86_64 {
    add :: fn(a, b: u64) -> u64 needs Sys {
        r := a
        @asm(\"addq %1, %0\", (\"+r\", &r), (\"r\", b), \"cc\")
        return r
    }
} else {
    add :: fn(a, b: u64) -> u64 needs Sys {
        r: u64 = 0
        @asm(\"add %0, %1, %2\", (\"=r\", &r), ((\"r\", a), (\"r\", b)), ())
        return r
    }
}

main :: fn() needs Out + Sys {
    print(\"{}\\n\", add(40, 2))
}
",
    )
    .unwrap();
    let out = gamma(&d, &["run", "--backend=cranelift", "a.gm"]);
    assert!(out.ok, "{}", out.stderr);
    assert_eq!(out.stdout, "42\n");
}

#[test]
fn builds_leave_files_next_to_the_output_alone() {
    if !supported() {
        return;
    }
    let d = dir("own-files");
    std::fs::write(d.join("h.gm"), "main :: fn() needs Out { print(\"hi\\n\") }\n").unwrap();
    for f in ["app.o", "app.rt.c", "app.asm.o"] {
        std::fs::write(d.join(f), "mine").unwrap();
    }
    let out = gamma(&d, &["build", "--backend=cranelift", "-o", "app", "h.gm"]);
    assert!(out.ok, "{}", out.stderr);
    for f in ["app.o", "app.rt.c", "app.asm.o"] {
        assert_eq!(std::fs::read_to_string(d.join(f)).unwrap(), "mine", "{f}");
    }
    // Nothing else is left behind.
    let mut names: Vec<String> =
        std::fs::read_dir(&d).unwrap().map(|e| e.unwrap().file_name().to_string_lossy().into_owned()).collect();
    names.sort();
    assert_eq!(names, ["app", "app.asm.o", "app.o", "app.rt.c", "cache", "h.gm"]);
}
